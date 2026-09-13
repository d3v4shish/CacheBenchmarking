/*
 * Batch 1: measurement qualification and a first integer instruction table.
 *
 * Decision question
 * -----------------
 * Can this specific Haswell laptop measure a simple instruction honestly
 * enough to distinguish (a) loop/timer overhead, (b) a true data-dependency
 * latency chain, and (c) reciprocal throughput from independent chains?
 *
 * Plain-language model
 * --------------------
 * Think of an instruction as a worker.  A latency test gives one worker a
 * note that depends on the previous note: it must wait for the prior worker.
 * A throughput test gives several workers independent notes: the CPU can work
 * on more than one note at a time.  Those are intentionally different tests.
 *
 * What this executable writes
 * ---------------------------
 * One CSV row per retained repetition.  It includes TSC ticks, Linux PMU
 * counts, the PMU enabled/running time, affinity observations, page faults,
 * the known dynamic instruction count, and a checksum.  Analysis happens in
 * the article from these raw rows; this program never silently averages them.
 *
 * Safety and semantic contract
 * ----------------------------
 * This is register-only code except for the explicit page-state probe.  It
 * neither changes kernel settings nor writes outside its requested CSV path.
 * All arithmetic intentionally uses unsigned 64-bit wraparound, whose C++
 * meaning is defined.  A checksum verifies that the timed assembly ran.
 *
 * Method sources
 *  [1] Agner Fog, Instruction tables, "Latency" and "Reciprocal throughput":
 *      https://www.agner.org/optimize/instruction_tables.pdf
 *  [2] Agner Fog, Microarchitecture manual, out-of-order execution and
 *      register renaming:
 *      https://www.agner.org/optimize/microarchitecture.pdf
 *  [3] Intel 64 and IA-32 Architectures Software Developer's Manual,
 *      RDTSC/RDTSCP and LFENCE ordering discussion:
 *      https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
 *  [4] Linux perf_event_open(2), event counts plus time_enabled/time_running:
 *      https://man7.org/linux/man-pages/man2/perf_event_open.2.html
 *
 * Important limitation
 * --------------------
 * The raw Haswell port events used here report cycles in which a port was
 * active, not a universally exact count of uops on that port.  They are
 * supporting evidence for a port hypothesis, never proof by themselves.
 */

#include <asm/unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// The test runner uses this fixed seed so a checksum can expose accidental
// elimination or a changed assembly instruction without needing random input.
constexpr uint64_t kSeed = 1;

// Each published instruction-loop body contains at least this many target
// instructions.  The roadmap requires 100+ static test instructions unless
// code size itself is the subject; 128 makes the loop branch negligible while
// remaining small enough to inspect in the generated disassembly.
constexpr uint64_t kStaticTargetInstructionsPerBody = 128;

// An x86 cache line is 64 bytes on the Haswell DUT.  This constant is used
// only by the explicit cache-eviction state preparation, never by a timed ALU
// loop.  Keeping it named makes the assumption reviewable.
constexpr size_t kCacheLineBytes = 64;

// The roadmap calls for a 24–32 MiB eviction walk.  32 MiB is larger than the
// DUT's 6 MiB LLC, so it is a useful *state preparation* attempt.  It is not
// claimed to flush every private/hardware-prefetched line deterministically.
constexpr size_t kEvictionBytes = 32U * 1024U * 1024U;

// Linux returns this layout because PERF_FORMAT_TOTAL_TIME_ENABLED and
// PERF_FORMAT_TOTAL_TIME_RUNNING are requested below.  The two times let the
// article reject multiplexed counter rows instead of treating scaled values as
// exact measurements.  Source: [4].
struct PerfRead {
  uint64_t value = 0;
  uint64_t time_enabled = 0;
  uint64_t time_running = 0;
};

// A named event keeps the CSV understandable.  `raw_config` is used only for
// event encodings checked with `perf list` on this DUT before Batch 1 runs.
struct EventSpec {
  std::string name;
  uint32_t type;
  uint64_t config;
};

// One opened event plus the exact name requested by the runner.
struct OpenEvent {
  EventSpec spec;
  int fd = -1;
};

// All options are explicit on the command line and emitted into CSV columns.
// This prevents a report reader from guessing which test shape was executed.
struct Options {
  std::string mode;
  std::string state;
  std::string perf_profile;
  int round = -1;
  uint64_t iterations = 0;
  int repetitions = 0;
  int warmups = 0;
  int cpu = -1;
  std::string output_path;
};

// This result deliberately holds raw totals rather than derived cycles/op.
// Derived metrics belong in the article so their formula is visible and can
// change without rewriting experimental evidence.
struct Sample {
  uint64_t tsc_ticks = 0;
  uint64_t wall_ns = 0;
  int cpu_before = -1;
  int cpu_after = -1;
  long minor_faults = 0;
  long major_faults = 0;
  uint64_t checksum = 0;
  std::vector<std::pair<std::string, PerfRead>> events;
};

// `asm volatile` blocks below are intentionally noinline functions.  No
// inlining means the article can show one disassembly per test; `noclone`
// prevents the compiler making a hidden specialized copy.  These attributes
// are part of the experiment construction, not an optimization preference.
#if defined(__GNUC__)
#define BENCH_NOINLINE __attribute__((noinline, noclone))
#else
#define BENCH_NOINLINE
#endif

// A compiler barrier says: do not move ordinary C++ memory operations across
// this point.  It does not emit a CPU fence and therefore does not add a
// hardware ordering cost to the measured instruction loop.
inline void compiler_barrier() { asm volatile("" ::: "memory"); }

// The start timestamp uses LFENCE before RDTSC.  On Intel CPUs this prevents
// later work from beginning before the timestamp in the intended measurement
// construction.  The end uses RDTSCP followed by LFENCE so later work cannot
// move before it.  We also collect PMU core cycles; TSC is a cross-check, not
// the primary latency denominator.  Source: [3].
inline uint64_t tsc_start() {
  uint32_t low = 0;
  uint32_t high = 0;
  asm volatile("lfence\n\trdtsc" : "=a"(low), "=d"(high) :: "memory");
  return (static_cast<uint64_t>(high) << 32U) | low;
}

inline uint64_t tsc_end() {
  uint32_t low = 0;
  uint32_t high = 0;
  asm volatile("rdtscp\n\tlfence" : "=a"(low), "=d"(high) :: "rcx", "memory");
  return (static_cast<uint64_t>(high) << 32U) | low;
}

// CLOCK_MONOTONIC_RAW is a wall-time cross-check that avoids NTP slewing.  It
// is not used to turn a wall time into an instruction latency.
uint64_t monotonic_raw_ns() {
  timespec ts{};
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
    throw std::runtime_error("clock_gettime(CLOCK_MONOTONIC_RAW) failed");
  }
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + ts.tv_nsec;
}

// Pinning makes CPU placement an executable precondition rather than a wish.
// The caller verifies placement both before and after every timed sample.
void pin_to_cpu(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  if (sched_setaffinity(0, sizeof(set), &set) != 0) {
    throw std::runtime_error("sched_setaffinity failed: " + std::string(strerror(errno)));
  }
}

// `sched_getcpu` is cheap enough outside timing and detects a migrated sample.
// A migration makes a per-core cycle result suspect, so the runner marks it.
int current_cpu() {
  const int cpu = sched_getcpu();
  if (cpu < 0) {
    throw std::runtime_error("sched_getcpu failed: " + std::string(strerror(errno)));
  }
  return cpu;
}

// Linux's syscall wrapper is kept local so the source can compile without an
// extra perf library.  It implements the documented perf_event_open ABI [4].
int perf_event_open(perf_event_attr* attr) {
  return static_cast<int>(syscall(__NR_perf_event_open, attr, 0, -1, -1, 0));
}

// Create the standard hardware-counter event.  Excluding kernel and hypervisor
// work keeps this process's user-mode loop separate from unrelated OS activity.
int open_event(const EventSpec& spec) {
  perf_event_attr attr{};
  attr.type = spec.type;
  attr.size = sizeof(attr);
  attr.config = spec.config;
  attr.disabled = 1;
  attr.exclude_kernel = 1;
  attr.exclude_hv = 1;
  attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;

  const int fd = perf_event_open(&attr);
  if (fd < 0) {
    throw std::runtime_error("perf_event_open(" + spec.name + ") failed: " +
                             std::string(strerror(errno)));
  }
  return fd;
}

// The basic profile should fit comfortably on this Haswell PMU.  The expanded
// profile intentionally requests more generic events than the hardware can
// run simultaneously; V06 uses time_running/time_enabled to demonstrate why
// such a group must not be published as an exact single-pass attribution.
std::vector<EventSpec> event_specs_for(std::string_view profile) {
  const std::vector<EventSpec> basic = {
      {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
      {"instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS},
      {"ref_cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_REF_CPU_CYCLES},
      {"branches", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS},
      {"branch_misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES},
  };
  if (profile == "none") {
    return {};
  }
  if (profile == "basic") {
    return basic;
  }
  if (profile == "expanded") {
    auto expanded = basic;
    // Raw Haswell encodings were checked against the DUT's `perf list` output:
    // UOPS_ISSUED.ANY = event 0x0e, umask 0x01; UOPS_RETIRED.ALL = 0xc2/0x01;
    // port events = 0xa1 with the named bit.  See the Batch 1 manifest.
    expanded.push_back({"uops_issued_any", PERF_TYPE_RAW, 0x010e});
    expanded.push_back({"uops_retired_all", PERF_TYPE_RAW, 0x01c2});
    expanded.push_back({"port0_active_cycles", PERF_TYPE_RAW, 0x01a1});
    expanded.push_back({"port1_active_cycles", PERF_TYPE_RAW, 0x02a1});
    expanded.push_back({"port5_active_cycles", PERF_TYPE_RAW, 0x20a1});
    return expanded;
  }
  throw std::runtime_error("unknown perf profile: " + std::string(profile));
}

// Open events separately, rather than as an all-or-nothing perf group, so the
// expanded V06 run can expose each event's own enabled/running ratio.  This is
// intentional multiplexing diagnostics, not an instruction-table measurement.
std::vector<OpenEvent> open_events(std::string_view profile) {
  std::vector<OpenEvent> events;
  for (const EventSpec& spec : event_specs_for(profile)) {
    events.push_back({spec, open_event(spec)});
  }
  return events;
}

void close_events(std::vector<OpenEvent>* events) {
  for (OpenEvent& event : *events) {
    if (event.fd >= 0) {
      close(event.fd);
      event.fd = -1;
    }
  }
}

void reset_and_enable(const std::vector<OpenEvent>& events) {
  for (const OpenEvent& event : events) {
    if (ioctl(event.fd, PERF_EVENT_IOC_RESET, 0) != 0 ||
        ioctl(event.fd, PERF_EVENT_IOC_ENABLE, 0) != 0) {
      throw std::runtime_error("could not enable perf event " + event.spec.name);
    }
  }
}

std::vector<std::pair<std::string, PerfRead>> disable_and_read(const std::vector<OpenEvent>& events) {
  std::vector<std::pair<std::string, PerfRead>> readings;
  for (const OpenEvent& event : events) {
    if (ioctl(event.fd, PERF_EVENT_IOC_DISABLE, 0) != 0) {
      throw std::runtime_error("could not disable perf event " + event.spec.name);
    }
    PerfRead reading{};
    const ssize_t bytes = read(event.fd, &reading, sizeof(reading));
    if (bytes != static_cast<ssize_t>(sizeof(reading))) {
      throw std::runtime_error("could not read perf event " + event.spec.name);
    }
    readings.emplace_back(event.spec.name, reading);
  }
  return readings;
}

// A timer-only body is the negative control for the timestamp construction.
// It intentionally contains no loop and no target instruction.
BENCH_NOINLINE uint64_t run_timer_only(uint64_t /*iterations*/) {
  asm volatile("" ::: "memory");
  return kSeed;
}

// This empty loop is the matched reference for one-instruction latency loops.
// It has the same loop counter, decrement, conditional branch, function call,
// and alignment directive, but no target ALU instruction.
BENCH_NOINLINE uint64_t run_empty(uint64_t iterations) {
  uint64_t counter = iterations;
  asm volatile(
      ".p2align 5\n\t"
      "1:\n\t"
      "decq %[counter]\n\t"
      "jnz 1b\n\t"
      : [counter] "+r"(counter)
      :
      : "cc");
  return counter;
}

// ADD writes `value`, and the next ADD reads that exact same register.  This
// is a closed true-dependency chain, so after matched-loop subtraction its
// core cycles per ADD estimate latency rather than throughput.  Source: [1].
BENCH_NOINLINE uint64_t run_add_latency(uint64_t iterations) {
  uint64_t value = kSeed;
  asm volatile(
      ".p2align 5\n\t"
      "1:\n\t"
      // GNU assembler expands this to 128 visible ADD opcodes.  Every ADD
      // reads/writes the same register, so the entire body is one long chain.
      ".rept 128\n\t"
      "addq $3, %[value]\n\t"
      ".endr\n\t"
      "decq %[iterations]\n\t"
      "jnz 1b\n\t"
      : [value] "+r"(value), [iterations] "+r"(iterations)
      :
      : "cc");
  return value;
}

// IMUL has the same loop shape as ADD but a longer real data dependency.  The
// immediate form is explicitly written so the final opcode can be checked in
// objdump; multiplication wraps modulo 2^64 by architectural/C++ contract.
BENCH_NOINLINE uint64_t run_imul_latency(uint64_t iterations) {
  uint64_t value = kSeed;
  asm volatile(
      ".p2align 5\n\t"
      "1:\n\t"
      ".rept 128\n\t"
      "imulq $3, %[value], %[value]\n\t"
      ".endr\n\t"
      "decq %[iterations]\n\t"
      "jnz 1b\n\t"
      : [value] "+r"(value), [iterations] "+r"(iterations)
      :
      : "cc");
  return value;
}

// This sequence is a zero-idiom *dependency-breaking* probe, not a claim that
// XOR has an ordinary one-way latency.  IMUL creates a long prior result; XOR
// produces zero without needing that result, then ADD makes the final value 1.
// The next iteration's IMUL depends on 1, so IMUL work can overlap iterations.
BENCH_NOINLINE uint64_t run_xor_break(uint64_t iterations) {
  uint64_t value = kSeed;
  asm volatile(
      ".p2align 5\n\t"
      "1:\n\t"
      // Each triplet is one semantic probe.  Repeating 128 triplets gives the
      // loop a large body while preserving the producer→zeroer relationship.
      ".rept 128\n\t"
      "imulq $3, %[value], %[value]\n\t"  // Deliberately long producer.
      "xorq %[value], %[value]\n\t"       // Recognized zero idiom on x86.
      "addq $1, %[value]\n\t"              // Both probes return value 1.
      ".endr\n\t"
      "decq %[iterations]\n\t"
      "jnz 1b\n\t"
      : [value] "+r"(value), [iterations] "+r"(iterations)
      :
      : "cc");
  return value;
}

// AND with an immediate zero has the same final arithmetic value as XOR+ADD,
// but it must read the previous value first.  Comparing this with run_xor_break
// makes the expected dependency-breaking effect visible without changing the
// result checksum or hiding behind a different loop shape.
BENCH_NOINLINE uint64_t run_and_zero_control(uint64_t iterations) {
  uint64_t value = kSeed;
  asm volatile(
      ".p2align 5\n\t"
      "1:\n\t"
      ".rept 128\n\t"
      "imulq $3, %[value], %[value]\n\t"
      "andq $0, %[value]\n\t"  // Same zero result; still reads IMUL result.
      "addq $1, %[value]\n\t"
      ".endr\n\t"
      "decq %[iterations]\n\t"
      "jnz 1b\n\t"
      : [value] "+r"(value), [iterations] "+r"(iterations)
      :
      : "cc");
  return value;
}

// A one-chain shift gives Batch 1 a second ALU operand form.  The shift count
// is an immediate so the code shape is fixed and the result becomes zero once
// enough iterations have passed; that known result is checked after timing.
BENCH_NOINLINE uint64_t run_shift_latency(uint64_t iterations) {
  uint64_t value = kSeed;
  asm volatile(
      ".p2align 5\n\t"
      "1:\n\t"
      ".rept 128\n\t"
      "shlq $1, %[value]\n\t"
      ".endr\n\t"
      "decq %[iterations]\n\t"
      "jnz 1b\n\t"
      : [value] "+r"(value), [iterations] "+r"(iterations)
      :
      : "cc");
  return value;
}

// The following four bodies use eight architecturally distinct registers.
// Each register forms its own cross-iteration chain, while the eight chains
// are independent of one another.  Sweeping to this count helps hide latency;
// the article still checks chain-count convergence before calling it a plateau.
// RAX holds a post-loop checksum, RCX is the loop counter, and r8–r15 contain
// the eight streams.  All clobbers are explicit so the compiler cannot keep a
// live C++ value in a register overwritten by the timed assembly.
//
// The 1/2/4/8 variants below are deliberately separate functions rather than
// a runtime `if (streams)` inside one loop.  A runtime branch would change the
// measured machine code.  Their only intended difference is the number of
// independent ADD chains, which makes the stream-count convergence curve
// auditable in the disassembly.
BENCH_NOINLINE uint64_t run_add_throughput_1(uint64_t iterations) {
  uint64_t checksum = 0;
  asm volatile(
      "movq %[iterations], %%rcx\n\t"
      "movq $1, %%r8\n\t"
      ".p2align 5\n\t"
      "1:\n\t"
      ".rept 128\n\t"
      "addq $3, %%r8\n\t"
      ".endr\n\t"
      "decq %%rcx\n\t"
      "jnz 1b\n\t"
      "addq %%r8, %%rax\n\t"
      : "+a"(checksum)
      : [iterations] "r"(iterations)
      : "rcx", "r8", "cc");
  return checksum;
}

BENCH_NOINLINE uint64_t run_add_throughput_2(uint64_t iterations) {
  uint64_t checksum = 0;
  asm volatile(
      "movq %[iterations], %%rcx\n\t"
      "movq $1, %%r8\n\t" "movq $1, %%r9\n\t"
      ".p2align 5\n\t"
      "1:\n\t"
      ".rept 64\n\t"
      "addq $3, %%r8\n\t" "addq $3, %%r9\n\t"
      ".endr\n\t"
      "decq %%rcx\n\t"
      "jnz 1b\n\t"
      "addq %%r8, %%rax\n\t" "addq %%r9, %%rax\n\t"
      : "+a"(checksum)
      : [iterations] "r"(iterations)
      : "rcx", "r8", "r9", "cc");
  return checksum;
}

BENCH_NOINLINE uint64_t run_add_throughput_4(uint64_t iterations) {
  uint64_t checksum = 0;
  asm volatile(
      "movq %[iterations], %%rcx\n\t"
      "movq $1, %%r8\n\t" "movq $1, %%r9\n\t"
      "movq $1, %%r10\n\t" "movq $1, %%r11\n\t"
      ".p2align 5\n\t"
      "1:\n\t"
      ".rept 32\n\t"
      "addq $3, %%r8\n\t" "addq $3, %%r9\n\t"
      "addq $3, %%r10\n\t" "addq $3, %%r11\n\t"
      ".endr\n\t"
      "decq %%rcx\n\t"
      "jnz 1b\n\t"
      "addq %%r8, %%rax\n\t" "addq %%r9, %%rax\n\t"
      "addq %%r10, %%rax\n\t" "addq %%r11, %%rax\n\t"
      : "+a"(checksum)
      : [iterations] "r"(iterations)
      : "rcx", "r8", "r9", "r10", "r11", "cc");
  return checksum;
}

BENCH_NOINLINE uint64_t run_add_throughput(uint64_t iterations) {
  uint64_t checksum = 0;
  asm volatile(
      "movq %[iterations], %%rcx\n\t"
      "movq $1, %%r8\n\t" "movq $1, %%r9\n\t"
      "movq $1, %%r10\n\t" "movq $1, %%r11\n\t"
      "movq $1, %%r12\n\t" "movq $1, %%r13\n\t"
      "movq $1, %%r14\n\t" "movq $1, %%r15\n\t"
      ".p2align 5\n\t"
      "1:\n\t"
      ".rept 16\n\t"
      "addq $3, %%r8\n\t"  "addq $3, %%r9\n\t"
      "addq $3, %%r10\n\t" "addq $3, %%r11\n\t"
      "addq $3, %%r12\n\t" "addq $3, %%r13\n\t"
      "addq $3, %%r14\n\t" "addq $3, %%r15\n\t"
      ".endr\n\t"
      "decq %%rcx\n\t"
      "jnz 1b\n\t"
      "addq %%r8, %%rax\n\t"  "addq %%r9, %%rax\n\t"
      "addq %%r10, %%rax\n\t" "addq %%r11, %%rax\n\t"
      "addq %%r12, %%rax\n\t" "addq %%r13, %%rax\n\t"
      "addq %%r14, %%rax\n\t" "addq %%r15, %%rax\n\t"
      : "+a"(checksum)
      : [iterations] "r"(iterations)
      : "rcx", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "cc");
  return checksum;
}

BENCH_NOINLINE uint64_t run_imul_throughput(uint64_t iterations) {
  uint64_t checksum = 0;
  asm volatile(
      "movq %[iterations], %%rcx\n\t"
      "movq $1, %%r8\n\t" "movq $1, %%r9\n\t"
      "movq $1, %%r10\n\t" "movq $1, %%r11\n\t"
      "movq $1, %%r12\n\t" "movq $1, %%r13\n\t"
      "movq $1, %%r14\n\t" "movq $1, %%r15\n\t"
      ".p2align 5\n\t"
      "1:\n\t"
      ".rept 16\n\t"
      "imulq $3, %%r8, %%r8\n\t"   "imulq $3, %%r9, %%r9\n\t"
      "imulq $3, %%r10, %%r10\n\t" "imulq $3, %%r11, %%r11\n\t"
      "imulq $3, %%r12, %%r12\n\t" "imulq $3, %%r13, %%r13\n\t"
      "imulq $3, %%r14, %%r14\n\t" "imulq $3, %%r15, %%r15\n\t"
      ".endr\n\t"
      "decq %%rcx\n\t"
      "jnz 1b\n\t"
      "addq %%r8, %%rax\n\t"  "addq %%r9, %%rax\n\t"
      "addq %%r10, %%rax\n\t" "addq %%r11, %%rax\n\t"
      "addq %%r12, %%rax\n\t" "addq %%r13, %%rax\n\t"
      "addq %%r14, %%rax\n\t" "addq %%r15, %%rax\n\t"
      : "+a"(checksum)
      : [iterations] "r"(iterations)
      : "rcx", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "cc");
  return checksum;
}

BENCH_NOINLINE uint64_t run_shift_throughput(uint64_t iterations) {
  uint64_t checksum = 0;
  asm volatile(
      "movq %[iterations], %%rcx\n\t"
      "movq $1, %%r8\n\t" "movq $1, %%r9\n\t"
      "movq $1, %%r10\n\t" "movq $1, %%r11\n\t"
      "movq $1, %%r12\n\t" "movq $1, %%r13\n\t"
      "movq $1, %%r14\n\t" "movq $1, %%r15\n\t"
      ".p2align 5\n\t"
      "1:\n\t"
      ".rept 16\n\t"
      "shlq $1, %%r8\n\t"  "shlq $1, %%r9\n\t"
      "shlq $1, %%r10\n\t" "shlq $1, %%r11\n\t"
      "shlq $1, %%r12\n\t" "shlq $1, %%r13\n\t"
      "shlq $1, %%r14\n\t" "shlq $1, %%r15\n\t"
      ".endr\n\t"
      "decq %%rcx\n\t"
      "jnz 1b\n\t"
      "addq %%r8, %%rax\n\t"  "addq %%r9, %%rax\n\t"
      "addq %%r10, %%rax\n\t" "addq %%r11, %%rax\n\t"
      "addq %%r12, %%rax\n\t" "addq %%r13, %%rax\n\t"
      "addq %%r14, %%rax\n\t" "addq %%r15, %%rax\n\t"
      : "+a"(checksum)
      : [iterations] "r"(iterations)
      : "rcx", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "cc");
  return checksum;
}

BENCH_NOINLINE uint64_t run_xor_throughput(uint64_t iterations) {
  uint64_t checksum = 0;
  asm volatile(
      "movq %[iterations], %%rcx\n\t"
      "movq $1, %%r8\n\t" "movq $1, %%r9\n\t"
      "movq $1, %%r10\n\t" "movq $1, %%r11\n\t"
      "movq $1, %%r12\n\t" "movq $1, %%r13\n\t"
      "movq $1, %%r14\n\t" "movq $1, %%r15\n\t"
      ".p2align 5\n\t"
      "1:\n\t"
      ".rept 16\n\t"
      "xorq %%r8, %%r8\n\t"   "xorq %%r9, %%r9\n\t"
      "xorq %%r10, %%r10\n\t" "xorq %%r11, %%r11\n\t"
      "xorq %%r12, %%r12\n\t" "xorq %%r13, %%r13\n\t"
      "xorq %%r14, %%r14\n\t" "xorq %%r15, %%r15\n\t"
      ".endr\n\t"
      "decq %%rcx\n\t"
      "jnz 1b\n\t"
      "addq %%r8, %%rax\n\t"  "addq %%r9, %%rax\n\t"
      "addq %%r10, %%rax\n\t" "addq %%r11, %%rax\n\t"
      "addq %%r12, %%rax\n\t" "addq %%r13, %%rax\n\t"
      "addq %%r14, %%rax\n\t" "addq %%r15, %%rax\n\t"
      : "+a"(checksum)
      : [iterations] "r"(iterations)
      : "rcx", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "cc");
  return checksum;
}

// The page-state probe is intentionally separate from register tests.  These
// globals name the prepared mapping read by the timed kernel.  The mapping is
// created/touched/discarded outside the timer so the measured interval contains
// only the requested page reads, not allocation or page-state preparation.
volatile uint8_t* g_page_probe_bytes = nullptr;
size_t g_page_probe_size = 0;
size_t g_page_probe_page_size = 0;

// The definition sits beside the other state-preparation routine below.  This
// declaration lets PageProbeRegion use that documented eviction walk here.
void evict_caches_best_effort();

// This small RAII owner guarantees that a failed sample does not leave a test
// mapping behind.  It is not timed.  1,024 pages produce enough fault events
// to validate labels while remaining modest (normally 4 MiB) on the DUT.
class PageProbeRegion {
 public:
  PageProbeRegion() {
    const long page_size_raw = sysconf(_SC_PAGESIZE);
    if (page_size_raw <= 0) throw std::runtime_error("sysconf(_SC_PAGESIZE) failed");
    page_size_ = static_cast<size_t>(page_size_raw);
    size_ = 1024U * page_size_;
    bytes_ = static_cast<uint8_t*>(mmap(nullptr, size_, PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (bytes_ == MAP_FAILED) throw std::runtime_error("mmap page-state buffer failed");
    // Give each warm page a nonzero, reproducible byte.  This makes an
    // accidental page discard observable in the checksum as well as faults.
    for (size_t offset = 0; offset < size_; offset += page_size_) {
      bytes_[offset] = static_cast<uint8_t>(offset / page_size_);
    }
  }

  PageProbeRegion(const PageProbeRegion&) = delete;
  PageProbeRegion& operator=(const PageProbeRegion&) = delete;

  ~PageProbeRegion() {
    if (bytes_ != nullptr && bytes_ != MAP_FAILED) munmap(bytes_, size_);
  }

  // State preparation happens immediately before the timer.  `cache_evicted`
  // keeps the data resident but walks a larger buffer.  `page_cold` asks Linux
  // to discard this anonymous mapping; its later zero-page faults are part of
  // the measurement and its zero checksum is expected, not an error.
  void prepare(std::string_view state) {
    if (state == "warm") return;
    if (state == "cache_evicted") {
      evict_caches_best_effort();
      return;
    }
    if (state == "page_cold") {
      if (madvise(bytes_, size_, MADV_DONTNEED) != 0) {
        throw std::runtime_error("madvise(MADV_DONTNEED) for page probe failed");
      }
      return;
    }
    throw std::runtime_error("unknown page-probe state: " + std::string(state));
  }

  void activate() const {
    g_page_probe_bytes = bytes_;
    g_page_probe_size = size_;
    g_page_probe_page_size = page_size_;
  }

  void deactivate() const {
    g_page_probe_bytes = nullptr;
    g_page_probe_size = 0;
    g_page_probe_page_size = 0;
  }

 private:
  uint8_t* bytes_ = nullptr;
  size_t size_ = 0;
  size_t page_size_ = 0;
};

// One volatile byte is read from each prepared page.  `volatile` prevents the
// compiler combining, vectorizing, or deleting reads because V07 is about
// cache/TLB/page state rather than ALU speed.  The region pointer is checked so
// an accidental call outside PageProbeRegion is rejected rather than measured.
BENCH_NOINLINE uint64_t run_page_state_probe(uint64_t iterations) {
  if (g_page_probe_bytes == nullptr || g_page_probe_page_size == 0) {
    throw std::runtime_error("page-state kernel has no prepared region");
  }
  uint64_t sum = 0;
  for (uint64_t pass = 0; pass < iterations; ++pass) {
    for (size_t offset = 0; offset < g_page_probe_size; offset += g_page_probe_page_size) {
      sum += g_page_probe_bytes[offset];
    }
  }
  return sum;
}

using BenchFunction = uint64_t (*)(uint64_t);

// Return both the function and the number of target instructions per loop
// iteration.  Sequence probes label their whole three-instruction sequence;
// the article never divides it into a fictional XOR or AND latency.
std::pair<BenchFunction, uint64_t> function_for(std::string_view mode) {
  if (mode == "timer_only") return {run_timer_only, 0};
  if (mode == "empty") return {run_empty, 0};
  if (mode == "add_latency") return {run_add_latency, kStaticTargetInstructionsPerBody};
  if (mode == "imul_latency") return {run_imul_latency, kStaticTargetInstructionsPerBody};
  if (mode == "xor_break") return {run_xor_break, 3 * kStaticTargetInstructionsPerBody};
  if (mode == "and_zero_control") return {run_and_zero_control, 3 * kStaticTargetInstructionsPerBody};
  if (mode == "shift_latency") return {run_shift_latency, kStaticTargetInstructionsPerBody};
  if (mode == "add_throughput_1") return {run_add_throughput_1, kStaticTargetInstructionsPerBody};
  if (mode == "add_throughput_2") return {run_add_throughput_2, kStaticTargetInstructionsPerBody};
  if (mode == "add_throughput_4") return {run_add_throughput_4, kStaticTargetInstructionsPerBody};
  if (mode == "add_throughput" || mode == "add_throughput_8")
    return {run_add_throughput, kStaticTargetInstructionsPerBody};
  if (mode == "imul_throughput" || mode == "imul_throughput_8")
    return {run_imul_throughput, kStaticTargetInstructionsPerBody};
  if (mode == "shift_throughput") return {run_shift_throughput, kStaticTargetInstructionsPerBody};
  if (mode == "xor_throughput") return {run_xor_throughput, kStaticTargetInstructionsPerBody};
  if (mode == "page_state_probe") return {run_page_state_probe, 0};
  throw std::runtime_error("unknown mode: " + std::string(mode));
}

// The eviction walk is performed before timing only.  A volatile accumulator
// makes every read observable, while the named global sink stops the compiler
// discarding the accumulated value after the loop.
volatile uint64_t g_eviction_sink = 0;
void evict_caches_best_effort() {
  static std::vector<uint8_t> buffer(kEvictionBytes, 1);
  // A volatile view makes these preparatory loads real while leaving the
  // allocation/initialization outside all timed samples.
  const volatile uint8_t* reader = buffer.data();
  uint64_t sum = 0;
  for (size_t offset = 0; offset < buffer.size(); offset += kCacheLineBytes) {
    sum += reader[offset];
  }
  g_eviction_sink ^= sum;
}

// Page-cold preparation only applies to the explicit page-state probe.  It is
// deliberately rejected for a register loop because calling a register test
// "page cold" would be misleading.  MADV_DONTNEED is used on a fresh mapping
// inside that probe's lifecycle rather than attempting to discard code pages.
void prepare_state(std::string_view state, std::string_view mode) {
  // The page probe prepares its own mapping immediately before timing.  Doing
  // an eviction walk here would happen before that mapping exists and would
  // therefore fail to establish the named state.
  if (mode == "page_state_probe") return;
  if (state == "warm") {
    return;
  }
  if (state == "cache_evicted") {
    evict_caches_best_effort();
    return;
  }
  if (state == "page_cold")
    throw std::runtime_error("page_cold is only valid for page_state_probe in Batch 1");
  throw std::runtime_error("unknown state: " + std::string(state));
}

// This bounded modular exponentiation produces the expected final value for
// ADD/IMUL without executing a second billion-iteration reference loop.  It is
// outside timing and doubles as a simple semantic checker for the assembly.
uint64_t power_mod_2_64(uint64_t base, uint64_t exponent) {
  uint64_t result = 1;
  while (exponent != 0) {
    if ((exponent & 1U) != 0) result *= base;
    base *= base;
    exponent >>= 1U;
  }
  return result;
}

// Every independent-stream body contains 128 total target instructions.  With
// N streams, each register receives 128/N repetitions.  This helper keeps the
// checksum proof aligned with the assembly's `.rept` counts.
uint64_t operations_per_stream(uint64_t iterations, uint64_t streams) {
  return iterations * (kStaticTargetInstructionsPerBody / streams);
}

// The checksum is intentionally simple and mode-specific.  It detects that a
// timed function did not run as designed, but it is not a performance metric.
uint64_t expected_checksum(std::string_view mode, std::string_view state, uint64_t iterations) {
  if (mode == "timer_only") return kSeed;
  if (mode == "empty") return 0;
  if (mode == "add_latency")
    return kSeed + 3U * operations_per_stream(iterations, 1);
  if (mode == "imul_latency") return power_mod_2_64(3, operations_per_stream(iterations, 1));
  if (mode == "xor_break" || mode == "and_zero_control") return 1;
  if (mode == "shift_latency")
    return operations_per_stream(iterations, 1) >= 64 ? 0 :
        (kSeed << operations_per_stream(iterations, 1));
  if (mode == "add_throughput_1")
    return kSeed + 3U * operations_per_stream(iterations, 1);
  if (mode == "add_throughput_2")
    return 2U * (kSeed + 3U * operations_per_stream(iterations, 2));
  if (mode == "add_throughput_4")
    return 4U * (kSeed + 3U * operations_per_stream(iterations, 4));
  if (mode == "add_throughput" || mode == "add_throughput_8")
    return 8U * (kSeed + 3U * operations_per_stream(iterations, 8));
  if (mode == "imul_throughput" || mode == "imul_throughput_8")
    return 8U * power_mod_2_64(3, operations_per_stream(iterations, 8));
  if (mode == "shift_throughput")
    return operations_per_stream(iterations, 8) >= 64 ? 0 :
        8U * (kSeed << operations_per_stream(iterations, 8));
  if (mode == "xor_throughput") return 0;
  // The initialized 1,024 page bytes repeat 0..255 four times.  After a
  // MADV_DONTNEED preparation, anonymous pages read back as zero until written.
  if (mode == "page_state_probe") {
    if (state == "page_cold") return 0;
    return iterations * 4U * (255U * 256U / 2U);
  }
  throw std::runtime_error("no expected checksum for mode");
}

// Parse only documented `--name value` pairs.  Refusing extra or missing flags
// is a reproducibility feature: accidental defaults must not create a result.
Options parse_options(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view flag(argv[i]);
    if (flag == "--help") {
      std::cout << "Usage: batch1 --mode MODE --state STATE --perf-profile PROFILE "
                   "--round N --iterations N --repetitions N --warmups N --cpu N --output PATH\n";
      std::exit(0);
    }
    if (i + 1 >= argc) {
      throw std::runtime_error("missing value after " + std::string(flag));
    }
    const std::string value(argv[++i]);
    if (flag == "--mode") options.mode = value;
    else if (flag == "--state") options.state = value;
    else if (flag == "--perf-profile") options.perf_profile = value;
    else if (flag == "--round") options.round = std::stoi(value);
    else if (flag == "--iterations") options.iterations = std::stoull(value);
    else if (flag == "--repetitions") options.repetitions = std::stoi(value);
    else if (flag == "--warmups") options.warmups = std::stoi(value);
    else if (flag == "--cpu") options.cpu = std::stoi(value);
    else if (flag == "--output") options.output_path = value;
    else throw std::runtime_error("unknown option " + std::string(flag));
  }
  if (options.mode.empty() || options.state.empty() || options.perf_profile.empty() ||
      options.round < 0 || options.iterations == 0 || options.repetitions <= 0 || options.warmups < 0 ||
      options.cpu < 0 || options.output_path.empty()) {
    throw std::runtime_error("all required options must be present and positive");
  }
  return options;
}

// Execute exactly one sample.  Setup, warm state, affinity checks, PMU open,
// page-fault snapshots, and verification are outside the timing boundaries.
Sample run_sample(const Options& options, BenchFunction function) {
  prepare_state(options.state, options.mode);
  const int cpu_before = current_cpu();
  if (cpu_before != options.cpu) {
    throw std::runtime_error("sample started on wrong CPU");
  }

  // The specialized region exists only for V07.  Its construction and state
  // transition occur before perf/timestamp enable; its read kernel is the only
  // page-probe work inside the measured interval.
  std::optional<PageProbeRegion> page_region;
  if (options.mode == "page_state_probe") {
    page_region.emplace();
    page_region->prepare(options.state);
    page_region->activate();
  }

  std::vector<OpenEvent> events = open_events(options.perf_profile);
  rusage usage_before{};
  if (getrusage(RUSAGE_SELF, &usage_before) != 0) {
    close_events(&events);
    throw std::runtime_error("getrusage before sample failed");
  }

  // The barriers keep ordinary compiler work out of the narrow measured region.
  // PMUs are enabled immediately before the timestamp and disabled immediately
  // after it; their small boundary mismatch is documented, not hidden.
  compiler_barrier();
  reset_and_enable(events);
  const uint64_t wall_start = monotonic_raw_ns();
  const uint64_t tsc_before = tsc_start();
  const uint64_t checksum = function(options.iterations);
  const uint64_t tsc_after = tsc_end();
  const uint64_t wall_end = monotonic_raw_ns();
  const auto readings = disable_and_read(events);
  compiler_barrier();

  if (page_region) page_region->deactivate();

  rusage usage_after{};
  if (getrusage(RUSAGE_SELF, &usage_after) != 0) {
    close_events(&events);
    throw std::runtime_error("getrusage after sample failed");
  }
  close_events(&events);

  const uint64_t expected = expected_checksum(options.mode, options.state, options.iterations);
  if (checksum != expected) {
    throw std::runtime_error("checksum mismatch: intended assembly/result did not execute");
  }

  const int cpu_after = current_cpu();
  Sample sample;
  sample.tsc_ticks = tsc_after - tsc_before;
  sample.wall_ns = wall_end - wall_start;
  sample.cpu_before = cpu_before;
  sample.cpu_after = cpu_after;
  sample.minor_faults = usage_after.ru_minflt - usage_before.ru_minflt;
  sample.major_faults = usage_after.ru_majflt - usage_before.ru_majflt;
  sample.checksum = checksum;
  sample.events = readings;
  return sample;
}

// CSV needs a fixed column layout even when `--perf-profile none` is selected.
// Missing events are written as empty cells, never zero; zero could be a valid
// measured counter value and would make absence look like evidence.
std::optional<PerfRead> lookup_event(const Sample& sample, std::string_view name) {
  for (const auto& [event_name, value] : sample.events) {
    if (event_name == name) return value;
  }
  return std::nullopt;
}

void write_event(std::ostream& out, const Sample& sample, std::string_view name,
                 bool write_enabled, bool write_running) {
  const std::optional<PerfRead> event = lookup_event(sample, name);
  if (!event) return;
  if (write_enabled) out << event->time_enabled;
  else if (write_running) out << event->time_running;
  else out << event->value;
}

void write_header(std::ostream& out) {
  out << "mode,state,perf_profile,round,repetition,iterations,target_instructions_per_iteration,"
         "dynamic_target_instructions,tsc_ticks,wall_ns,cpu_before,cpu_after,minor_faults,"
         "major_faults,checksum,cycles,cycles_enabled,cycles_running,instructions,"
         "instructions_enabled,instructions_running,ref_cycles,ref_cycles_enabled,"
         "ref_cycles_running,branches,branches_enabled,branches_running,branch_misses,"
         "branch_misses_enabled,branch_misses_running,uops_issued_any,"
         "uops_issued_any_enabled,uops_issued_any_running,uops_retired_all,"
         "uops_retired_all_enabled,uops_retired_all_running,port0_active_cycles,"
         "port0_active_cycles_enabled,port0_active_cycles_running,port1_active_cycles,"
         "port1_active_cycles_enabled,port1_active_cycles_running,port5_active_cycles,"
         "port5_active_cycles_enabled,port5_active_cycles_running\n";
}

void write_event_triplet(std::ostream& out, const Sample& sample, std::string_view name) {
  out << ','; write_event(out, sample, name, false, false);
  out << ','; write_event(out, sample, name, true, false);
  out << ','; write_event(out, sample, name, false, true);
}

void write_row(std::ostream& out, const Options& options, int repetition,
               uint64_t target_instructions_per_iteration, const Sample& sample) {
  const uint64_t dynamic_target_instructions =
      target_instructions_per_iteration * options.iterations;
  out << options.mode << ',' << options.state << ',' << options.perf_profile << ','
      << options.round << ',' << repetition << ',' << options.iterations << ',' << target_instructions_per_iteration
      << ',' << dynamic_target_instructions << ',' << sample.tsc_ticks << ',' << sample.wall_ns
      << ',' << sample.cpu_before << ',' << sample.cpu_after << ',' << sample.minor_faults
      << ',' << sample.major_faults << ',' << sample.checksum;
  for (const std::string_view name : {"cycles", "instructions", "ref_cycles", "branches",
                                      "branch_misses", "uops_issued_any", "uops_retired_all",
                                      "port0_active_cycles", "port1_active_cycles",
                                      "port5_active_cycles"}) {
    write_event_triplet(out, sample, name);
  }
  out << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parse_options(argc, argv);
    const auto [function, target_instructions_per_iteration] = function_for(options.mode);
    pin_to_cpu(options.cpu);

    // Warm-up runs exercise code and the measurement path but are intentionally
    // discarded.  Their only job is to avoid publishing first-use artifacts.
    for (int warmup = 0; warmup < options.warmups; ++warmup) {
      const Sample discarded = run_sample(options, function);
      (void)discarded;
    }

    std::ofstream output(options.output_path, std::ios::app);
    if (!output) {
      throw std::runtime_error("could not open output file " + options.output_path);
    }
    if (output.tellp() == 0) write_header(output);
    for (int repetition = 0; repetition < options.repetitions; ++repetition) {
      const Sample sample = run_sample(options, function);
      write_row(output, options, repetition, target_instructions_per_iteration, sample);
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "batch1: " << error.what() << '\n';
    return 1;
  }
}
