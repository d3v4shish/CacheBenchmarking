/*
 * Cache Search Lab v2 — micro_working_set_ladder
 *
 * Decision question
 * -----------------
 * When the same class of memory operation crosses the measured L1, L2, LLC,
 * and DRAM footprints of the Haswell DUT, which costs change: useful work,
 * cache/TLB misses, branch behavior, or true address dependencies?
 *
 * This is a documented reconstruction of the published working-set ladder.
 * It is deliberately one case ID with several historical access-pattern rows;
 * it does not replace the later, dedicated stride, pointer, prefetch, or TLB
 * experiments.  Allocation, randomization, page preparation, and validation
 * are outside the timed interval.  Every timed row emits a checksum and an
 * unaggregated sample record; counters are emitted in a separate long-form
 * CSV so unavailable events remain visible rather than becoming zero.
 *
 * Method sources
 *  [1] https://man7.org/linux/man-pages/man2/perf_event_open.2.html
 *  [2] https://www.agner.org/optimize/microarchitecture.pdf
 *  [3] https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
 *
 * Safety and limits
 * -----------------
 * This program does not change frequency, turbo, affinity, governor, or page
 * policy.  Its caller must pin it and perform the hardware-quality gate.  The
 * generic perf events below are intentionally conservative; Haswell-specific
 * Topdown/frontend/port profiles are collected by the DUT runner only after
 * runtime alias validation.  A page-cold direct-read row intentionally has a
 * short, first-touch timing boundary and must never be summarized as a normal
 * steady-state sample.
 */

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include <linux/perf_event.h>

#if defined(__x86_64__)
#include <immintrin.h>
#if defined(__GNUC__) || defined(__clang__)
#define WSL_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define WSL_TARGET_AVX2
#endif
#else
#define WSL_TARGET_AVX2
#endif

namespace {

constexpr size_t kCacheLineBytes = 64;
constexpr size_t kPageBytes = 4096;
constexpr uint64_t kSampleSchemaVersion = 1;

struct Options {
  std::string mode;
  std::string implementation;
  std::string state;
  std::string perf_profile;
  std::string build_id;
  std::filesystem::path output_path;
  std::filesystem::path counter_output_path;
  uint64_t footprint_bytes = 0;
  int round = -1;
  int expected_cpu = -1;
  int min_duration_ms = 250;
  uint64_t seed = 0;
};

struct Sample {
  uint64_t passes = 0;
  uint64_t logical_operations = 0;
  uint64_t checksum = 0;
  uint64_t wall_ns = 0;
  uint64_t tsc_ticks = 0;
  int cpu_before = -1;
  int cpu_after = -1;
  long minor_faults = 0;
  long major_faults = 0;
  bool first_touch_exception = false;
};

struct CounterRead {
  std::string name;
  uint64_t value = 0;
  uint64_t time_enabled = 0;
  uint64_t time_running = 0;
};

struct PerfReadFormat {
  uint64_t value;
  uint64_t time_enabled;
  uint64_t time_running;
};

[[noreturn]] void fail(std::string_view message) { throw std::runtime_error(std::string(message)); }

uint64_t parse_u64(std::string_view value, std::string_view name) {
  size_t consumed = 0;
  try {
    const uint64_t parsed = std::stoull(std::string(value), &consumed, 0);
    if (consumed != value.size()) fail(std::string("invalid ") + std::string(name));
    return parsed;
  } catch (const std::exception&) {
    fail(std::string("invalid ") + std::string(name));
  }
}

int parse_int(std::string_view value, std::string_view name) {
  size_t consumed = 0;
  try {
    const int parsed = std::stoi(std::string(value), &consumed, 0);
    if (consumed != value.size()) fail(std::string("invalid ") + std::string(name));
    return parsed;
  } catch (const std::exception&) {
    fail(std::string("invalid ") + std::string(name));
  }
}

Options parse_options(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view flag(argv[index]);
    if (flag == "--help") {
      std::cout << "usage: working_set_ladder --mode MODE --implementation scalar|auto|avx2 "
                   "--state warm|cache_evicted|page_cold --perf-profile none|basic|branch|cache|tlb|l1_miss|l2_miss|l3_miss|dtlb_walk "
                   "--build-id ID --footprint-bytes N --round N --expected-cpu N "
                   "--min-duration-ms N --seed N --output samples.csv --counter-output counters.csv\n";
      std::exit(0);
    }
    if (index + 1 >= argc) fail("missing value after command-line flag");
    const std::string value(argv[++index]);
    if (flag == "--mode") options.mode = value;
    else if (flag == "--implementation") options.implementation = value;
    else if (flag == "--state") options.state = value;
    else if (flag == "--perf-profile") options.perf_profile = value;
    else if (flag == "--build-id") options.build_id = value;
    else if (flag == "--footprint-bytes") options.footprint_bytes = parse_u64(value, "footprint bytes");
    else if (flag == "--round") options.round = parse_int(value, "round");
    else if (flag == "--expected-cpu") options.expected_cpu = parse_int(value, "expected CPU");
    else if (flag == "--min-duration-ms") options.min_duration_ms = parse_int(value, "minimum duration");
    else if (flag == "--seed") options.seed = parse_u64(value, "seed");
    else if (flag == "--output") options.output_path = value;
    else if (flag == "--counter-output") options.counter_output_path = value;
    else fail(std::string("unknown flag: ") + std::string(flag));
  }
  const std::array<std::string, 8> modes = {
      "linear_scan", "stride16_scan", "random_access", "arena_pointer_chase",
      "dependent_index_cycle", "prefetch_chain", "mlp8_cycle", "tlb_page_walk"};
  if (std::find(modes.begin(), modes.end(), options.mode) == modes.end()) fail("unsupported mode");
  if (options.implementation != "scalar" && options.implementation != "auto" &&
      options.implementation != "avx2") fail("unsupported implementation");
  if (options.state != "warm" && options.state != "cache_evicted" && options.state != "page_cold") {
    fail("unsupported state");
  }
  if (options.perf_profile != "none" && options.perf_profile != "basic" &&
      options.perf_profile != "branch" && options.perf_profile != "cache" &&
      options.perf_profile != "tlb" && options.perf_profile != "l1_miss" &&
      options.perf_profile != "l2_miss" && options.perf_profile != "l3_miss" &&
      options.perf_profile != "dtlb_walk") fail("unsupported perf profile");
  if (options.mode == "tlb_page_walk" && options.implementation != "scalar") {
    fail("TLB walk has only the scalar semantic implementation");
  }
  if (options.implementation == "avx2" && options.mode != "linear_scan") {
    fail("the explicit AVX2 implementation currently applies only to linear scan");
  }
  if (options.state == "page_cold" && options.mode != "linear_scan" && options.mode != "random_access") {
    fail("page-cold rows are intentionally limited to direct-read modes");
  }
  if (options.footprint_bytes < kPageBytes || options.round < 0 || options.expected_cpu < 0 ||
      options.min_duration_ms <= 0 || options.build_id.empty() || options.output_path.empty() ||
      options.counter_output_path.empty()) fail("missing or unsafe required option");
  return options;
}

uint64_t monotonic_raw_ns() {
  timespec now{};
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &now) != 0) fail("clock_gettime failed");
  return static_cast<uint64_t>(now.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(now.tv_nsec);
}

inline void compiler_barrier() { asm volatile("" ::: "memory"); }

uint64_t tsc_start() {
#if defined(__x86_64__)
  uint32_t low = 0, high = 0;
  asm volatile("lfence\n\trdtsc" : "=a"(low), "=d"(high) :: "memory");
  return (static_cast<uint64_t>(high) << 32U) | low;
#else
  return 0;
#endif
}

uint64_t tsc_end() {
#if defined(__x86_64__)
  uint32_t low = 0, high = 0;
  asm volatile("rdtscp\n\tlfence" : "=a"(low), "=d"(high) :: "rcx", "memory");
  return (static_cast<uint64_t>(high) << 32U) | low;
#else
  return 0;
#endif
}

int current_cpu() {
#if defined(SYS_getcpu)
  unsigned cpu = 0;
  if (syscall(SYS_getcpu, &cpu, nullptr, nullptr) != 0) fail("getcpu failed");
  return static_cast<int>(cpu);
#else
  return sched_getcpu();
#endif
}

uint64_t splitmix64(uint64_t& state) {
  state += 0x9e3779b97f4a7c15ULL;
  uint64_t value = state;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

template <typename T>
void deterministic_shuffle(std::vector<T>& values, uint64_t seed) {
  for (size_t index = values.size(); index > 1; --index) {
    const size_t other = static_cast<size_t>(splitmix64(seed) % index);
    std::swap(values[index - 1], values[other]);
  }
}

class Mapping {
 public:
  explicit Mapping(size_t bytes) : bytes_(bytes) {
    address_ = mmap(nullptr, bytes_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (address_ == MAP_FAILED) {
      address_ = nullptr;
      throw std::runtime_error("mmap failed: " + std::string(std::strerror(errno)));
    }
  }
  Mapping(const Mapping&) = delete;
  Mapping& operator=(const Mapping&) = delete;
  Mapping(Mapping&& other) noexcept : address_(other.address_), bytes_(other.bytes_) { other.address_ = nullptr; }
  ~Mapping() { if (address_ != nullptr) munmap(address_, bytes_); }
  template <typename T> T* as() { return static_cast<T*>(address_); }
  template <typename T> const T* as() const { return static_cast<const T*>(address_); }
  void discard_pages() {
    if (madvise(address_, bytes_, MADV_DONTNEED) != 0) {
      throw std::runtime_error("MADV_DONTNEED failed: " + std::string(std::strerror(errno)));
    }
  }
  size_t bytes() const { return bytes_; }
 private:
  void* address_ = nullptr;
  size_t bytes_ = 0;
};

struct alignas(kCacheLineBytes) ArenaNode {
  uint32_t next = 0;
  uint32_t value = 0;
  std::array<std::byte, kCacheLineBytes - sizeof(uint32_t) * 2> padding{};
};
static_assert(sizeof(ArenaNode) == kCacheLineBytes);

class LadderWorkload {
 public:
  explicit LadderWorkload(const Options& options) : options_(options) { build(); }

  uint64_t logical_operations_per_pass() const { return logical_ops_per_pass_; }
  size_t physical_footprint_bytes() const { return physical_bytes_; }

  uint64_t expected_checksum(uint64_t passes) const {
    if (options_.state == "page_cold") return expected_page_cold_checksum();
    uint64_t expected = 0;
    for (uint64_t pass = 0; pass < passes; ++pass) expected ^= expected_pass_checksum_;
    return expected;
  }

  void prepare_state() {
    if (options_.state == "cache_evicted") evict_best_effort();
    if (options_.state == "page_cold") {
      // This deliberately discards only direct-read data.  The query sequence
      // stays resident so the first-touch row is a data-page result, not a
      // page-fault measurement of an index generator.
      direct_data_->discard_pages();
    }
  }

  uint64_t run_one_pass() {
    if (options_.mode == "linear_scan") return run_linear();
    if (options_.mode == "stride16_scan") return run_stride16();
    if (options_.mode == "random_access") return run_random();
    if (options_.mode == "arena_pointer_chase") return run_arena_pointer();
    if (options_.mode == "dependent_index_cycle") return run_index_cycle();
    if (options_.mode == "prefetch_chain") return run_prefetch_chain();
    if (options_.mode == "mlp8_cycle") return run_mlp8();
    return run_tlb_walk();
  }

  uint64_t expected_page_cold_checksum() const { return 0; }

 private:
  void build() {
    if (options_.mode == "linear_scan" || options_.mode == "stride16_scan" ||
        options_.mode == "random_access") {
      build_direct();
    } else if (options_.mode == "dependent_index_cycle") {
      build_index_cycle();
    } else if (options_.mode == "tlb_page_walk") {
      build_tlb_walk();
    } else {
      build_arena_nodes();
    }
    // Compute the expected one-pass result before timing.  This is the same
    // semantic traversal used by the timed path, but it happens before state
    // preparation and therefore cannot be mistaken for evidence.
    expected_pass_checksum_ = run_one_pass();
  }

  void build_direct() {
    const size_t bytes = (options_.footprint_bytes / sizeof(uint32_t)) * sizeof(uint32_t);
    if (bytes < kPageBytes) fail("direct footprint too small");
    direct_data_.emplace(bytes);
    direct_words_ = bytes / sizeof(uint32_t);
    uint64_t random = options_.seed;
    uint32_t* values = direct_data_->as<uint32_t>();
    for (size_t index = 0; index < direct_words_; ++index) values[index] = static_cast<uint32_t>(splitmix64(random));
    direct_expected_sum_ = 0;
    for (size_t index = 0; index < direct_words_; ++index) direct_expected_sum_ += values[index];
    if (options_.mode == "random_access") {
      // Query metadata is intentionally separate from the measured data region
      // and remains small enough to be prefaulted.  Its byte count is retained
      // in physical_footprint_bytes so readers can see the full timed state.
      random_queries_.resize(direct_words_);
      for (uint32_t index = 0; index < direct_words_; ++index) random_queries_[index] = index;
      deterministic_shuffle(random_queries_, options_.seed ^ 0x5eed5eedULL);
      physical_bytes_ = bytes + random_queries_.size() * sizeof(uint32_t);
    } else {
      physical_bytes_ = bytes;
    }
    logical_ops_per_pass_ = options_.mode == "stride16_scan" ? (direct_words_ + 15U) / 16U : direct_words_;
  }

  void build_arena_nodes() {
    const size_t bytes = (options_.footprint_bytes / sizeof(ArenaNode)) * sizeof(ArenaNode);
    const size_t nodes = bytes / sizeof(ArenaNode);
    if (nodes < 16) fail("arena footprint is too small for a closed cycle");
    arena_.emplace(bytes);
    ArenaNode* values = arena_->as<ArenaNode>();
    std::vector<uint32_t> order(nodes);
    for (uint32_t index = 0; index < nodes; ++index) order[index] = index;
    deterministic_shuffle(order, options_.seed ^ 0xabcddcbaULL);
    uint64_t random = options_.seed;
    for (size_t index = 0; index < nodes; ++index) {
      values[order[index]].next = order[(index + 1U) % nodes];
      values[order[index]].value = static_cast<uint32_t>(splitmix64(random));
    }
    arena_start_ = order.front();
    logical_ops_per_pass_ = nodes;
    physical_bytes_ = bytes;
  }

  void build_index_cycle() {
    const size_t bytes = (options_.footprint_bytes / sizeof(uint32_t)) * sizeof(uint32_t);
    const size_t entries = bytes / sizeof(uint32_t);
    if (entries < 16) fail("index-cycle footprint is too small");
    index_cycle_.emplace(bytes);
    uint32_t* next = index_cycle_->as<uint32_t>();
    std::vector<uint32_t> order(entries);
    for (uint32_t index = 0; index < entries; ++index) order[index] = index;
    deterministic_shuffle(order, options_.seed ^ 0x12233445ULL);
    for (size_t index = 0; index < entries; ++index) next[order[index]] = order[(index + 1U) % entries];
    index_start_ = order.front();
    logical_ops_per_pass_ = entries;
    physical_bytes_ = bytes;
  }

  void build_tlb_walk() {
    const size_t pages = options_.footprint_bytes / kPageBytes;
    if (pages < 2) fail("TLB footprint needs at least two pages");
    tlb_.emplace(pages * kPageBytes);
    uint8_t* bytes = tlb_->as<uint8_t>();
    std::vector<uint32_t> order(pages);
    for (uint32_t index = 0; index < pages; ++index) order[index] = index;
    deterministic_shuffle(order, options_.seed ^ 0x99887766ULL);
    for (size_t index = 0; index < pages; ++index) {
      const uint32_t next = order[(index + 1U) % pages];
      std::memcpy(bytes + static_cast<size_t>(order[index]) * kPageBytes, &next, sizeof(next));
    }
    tlb_start_page_ = order.front();
    logical_ops_per_pass_ = pages;
    physical_bytes_ = pages * kPageBytes;
  }

  uint64_t run_linear() const {
    const uint32_t* values = direct_data_->as<uint32_t>();
    uint64_t sum = 0;
    if (options_.implementation == "avx2") return run_linear_avx2(values, direct_words_);
    for (size_t index = 0; index < direct_words_; ++index) sum += values[index];
    return sum;
  }

  uint64_t run_stride16() const {
    const uint32_t* values = direct_data_->as<uint32_t>();
    uint64_t sum = 0;
    for (size_t index = 0; index < direct_words_; index += 16) sum += values[index];
    return sum;
  }

  uint64_t run_random() const {
    const uint32_t* values = direct_data_->as<uint32_t>();
    uint64_t sum = 0;
    for (uint32_t query : random_queries_) sum += values[query];
    return sum;
  }

  uint64_t run_arena_pointer() const {
    const ArenaNode* values = arena_->as<ArenaNode>();
    uint32_t index = arena_start_;
    uint64_t sum = 0;
    for (uint64_t step = 0; step < logical_ops_per_pass_; ++step) {
      const ArenaNode& node = values[index];
      sum += node.value;
      index = node.next;
    }
    return sum ^ index;
  }

  uint64_t run_index_cycle() const {
    const uint32_t* next = index_cycle_->as<uint32_t>();
    uint32_t index = index_start_;
    for (uint64_t step = 0; step < logical_ops_per_pass_; ++step) index = next[index];
    return index;
  }

  uint64_t run_prefetch_chain() const {
    const ArenaNode* values = arena_->as<ArenaNode>();
    uint32_t index = arena_start_;
    uint64_t sum = 0;
    for (uint64_t step = 0; step < logical_ops_per_pass_; ++step) {
      const ArenaNode& node = values[index];
      const uint32_t next = node.next;
      __builtin_prefetch(&values[next], 0, 1);
      sum += node.value;
      index = next;
    }
    return sum ^ index;
  }

  uint64_t run_mlp8() const {
    const ArenaNode* values = arena_->as<ArenaNode>();
    std::array<uint32_t, 8> indexes{};
    uint32_t cursor = arena_start_;
    for (size_t lane = 0; lane < indexes.size(); ++lane) {
      indexes[lane] = cursor;
      cursor = values[cursor].next;
    }
    uint64_t sum = 0;
    const uint64_t lane_steps = logical_ops_per_pass_ / indexes.size();
    for (uint64_t step = 0; step < lane_steps; ++step) {
      for (size_t lane = 0; lane < indexes.size(); ++lane) {
        const ArenaNode& node = values[indexes[lane]];
        sum += node.value;
        indexes[lane] = node.next;
      }
    }
    for (uint32_t index : indexes) sum ^= index;
    return sum;
  }

  uint64_t run_tlb_walk() const {
    const uint8_t* bytes = tlb_->as<uint8_t>();
    uint32_t page = tlb_start_page_;
    for (uint64_t step = 0; step < logical_ops_per_pass_; ++step) {
      std::memcpy(&page, bytes + static_cast<size_t>(page) * kPageBytes, sizeof(page));
    }
    return page;
  }

  static WSL_TARGET_AVX2 uint64_t run_linear_avx2(const uint32_t* values, size_t count) {
#if defined(__x86_64__)
    if (!__builtin_cpu_supports("avx2")) fail("AVX2 requested on a CPU without AVX2");
    __m256i accumulator = _mm256_setzero_si256();
    size_t index = 0;
    // Widen four uint32 values to four uint64 lanes before adding.  A 32-bit
    // vector accumulator would overflow at a different point from the scalar
    // reference and would invalidate the exact checksum contract.
    for (; index + 4 <= count; index += 4) {
      const __m128i loaded = _mm_loadu_si128(reinterpret_cast<const __m128i*>(values + index));
      accumulator = _mm256_add_epi64(accumulator, _mm256_cvtepu32_epi64(loaded));
    }
    alignas(32) std::array<uint64_t, 4> partial{};
    _mm256_store_si256(reinterpret_cast<__m256i*>(partial.data()), accumulator);
    uint64_t sum = 0;
    for (uint64_t value : partial) sum += value;
    for (; index < count; ++index) sum += values[index];
    return sum;
#else
    (void)values; (void)count; fail("AVX2 is unavailable on this architecture");
#endif
  }

  void evict_best_effort() const {
    const size_t bytes = std::max<size_t>(32ULL * 1024ULL * 1024ULL, std::min<size_t>(physical_bytes_ * 2ULL, 128ULL * 1024ULL * 1024ULL));
    Mapping eviction(bytes);
    volatile uint64_t sink = 0;
    const uint8_t* values = eviction.as<uint8_t>();
    for (size_t index = 0; index < bytes; index += kCacheLineBytes) sink += values[index];
    asm volatile("" : : "r"(sink) : "memory");
  }

  const Options& options_;
  std::optional<Mapping> direct_data_;
  std::optional<Mapping> arena_;
  std::optional<Mapping> index_cycle_;
  std::optional<Mapping> tlb_;
  size_t direct_words_ = 0;
  uint64_t direct_expected_sum_ = 0;
  std::vector<uint32_t> random_queries_;
  uint32_t arena_start_ = 0;
  uint32_t index_start_ = 0;
  uint32_t tlb_start_page_ = 0;
  uint64_t logical_ops_per_pass_ = 0;
  size_t physical_bytes_ = 0;
  uint64_t expected_pass_checksum_ = 0;
};

int perf_event_open(perf_event_attr* attributes) {
  return static_cast<int>(syscall(__NR_perf_event_open, attributes, 0, -1, -1, 0));
}

struct EventSpec { std::string name; uint32_t type; uint64_t config; };

uint64_t cache_config(perf_hw_cache_id cache, perf_hw_cache_op_id operation, perf_hw_cache_op_result_id result) {
  return static_cast<uint64_t>(cache) | (static_cast<uint64_t>(operation) << 8U) |
         (static_cast<uint64_t>(result) << 16U);
}

std::vector<EventSpec> event_specs(std::string_view profile) {
  if (profile == "none") return {};
  if (profile == "basic") return {
      {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
      {"instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS},
      {"ref_cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_REF_CPU_CYCLES}};
  if (profile == "branch") return {
      {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
      {"branches", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS},
      {"branch_misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES}};
  if (profile == "cache") return {
      {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
      {"l1d_read_access", PERF_TYPE_HW_CACHE, cache_config(PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_ACCESS)},
      {"l1d_read_miss", PERF_TYPE_HW_CACHE, cache_config(PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS)},
      {"ll_read_miss", PERF_TYPE_HW_CACHE, cache_config(PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS)}};
  if (profile == "tlb") return {
      {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
      {"dtlb_read_access", PERF_TYPE_HW_CACHE, cache_config(PERF_COUNT_HW_CACHE_DTLB, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_ACCESS)},
      {"dtlb_read_miss", PERF_TYPE_HW_CACHE, cache_config(PERF_COUNT_HW_CACHE_DTLB, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS)}};
  // Raw encodings from this Haswell DUT's `perf list --details`. Each profile
  // owns one diagnostic event plus cycles and is retained only if both ran for
  // the complete interval; unlike generic cache aliases, it cannot be silently
  // rescaled from a multiplexed partial interval.
  if (profile == "l1_miss") return {
      {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
      {"mem_load_uops_retired_l1_miss", PERF_TYPE_RAW, 0x08D1}};
  if (profile == "l2_miss") return {
      {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
      {"mem_load_uops_retired_l2_miss", PERF_TYPE_RAW, 0x10D1}};
  if (profile == "l3_miss") return {
      {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
      {"mem_load_uops_retired_l3_miss", PERF_TYPE_RAW, 0x20D1}};
  return {
      {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
      {"dtlb_load_misses_walk_completed", PERF_TYPE_RAW, 0x0E08}};
}

class PerfEvents {
 public:
  explicit PerfEvents(std::string_view profile) {
    for (const EventSpec& spec : event_specs(profile)) {
      perf_event_attr attributes{};
      attributes.type = spec.type;
      attributes.size = sizeof(attributes);
      attributes.config = spec.config;
      attributes.disabled = 1;
      attributes.exclude_kernel = 1;
      attributes.exclude_hv = 1;
      attributes.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
      const int descriptor = perf_event_open(&attributes);
      if (descriptor < 0) {
        close_all();
        throw std::runtime_error("perf event unavailable: " + spec.name + ": " + std::strerror(errno));
      }
      descriptors_.push_back({spec.name, descriptor});
    }
  }
  PerfEvents(const PerfEvents&) = delete;
  ~PerfEvents() { close_all(); }
  void start() const {
    for (const auto& [_, descriptor] : descriptors_) {
      if (ioctl(descriptor, PERF_EVENT_IOC_RESET, 0) != 0 || ioctl(descriptor, PERF_EVENT_IOC_ENABLE, 0) != 0) {
        fail("unable to start perf counters");
      }
    }
  }
  std::vector<CounterRead> stop_and_read() const {
    std::vector<CounterRead> rows;
    for (const auto& [name, descriptor] : descriptors_) {
      if (ioctl(descriptor, PERF_EVENT_IOC_DISABLE, 0) != 0) fail("unable to stop perf counter");
      PerfReadFormat event_read{};
      if (::read(descriptor, &event_read, sizeof(event_read)) != static_cast<ssize_t>(sizeof(event_read))) fail("unable to read perf counter");
      rows.push_back({name, event_read.value, event_read.time_enabled, event_read.time_running});
    }
    return rows;
  }
 private:
  void close_all() {
    for (const auto& [_, descriptor] : descriptors_) close(descriptor);
    descriptors_.clear();
  }
  std::vector<std::pair<std::string, int>> descriptors_;
};

std::vector<CounterRead> measure_with_counters(LadderWorkload& workload, const Options& options, Sample& sample) {
  PerfEvents events(options.perf_profile);
  // Warm the executable path and the prepared data before the measured state
  // is established. Cache-evicted state then evicts this warmed footprint;
  // page-cold deliberately skips warm-up because its first touch is the test.
  if (options.state != "page_cold") {
    asm volatile("" : : "r"(workload.run_one_pass()) : "memory");
    asm volatile("" : : "r"(workload.run_one_pass()) : "memory");
  }
  workload.prepare_state();
  // Warm every measurement-boundary helper before taking the rusage baseline.
  // A lazy VDSO or libc page fault is not a data-access result.
  (void)current_cpu();
  (void)monotonic_raw_ns();
  (void)tsc_start();
  (void)tsc_end();
  events.start();
  (void)events.stop_and_read();
  rusage before{};
  rusage after{};
  if (getrusage(RUSAGE_SELF, &before) != 0) fail("getrusage before sample failed");
  sample.cpu_before = current_cpu();
  if (sample.cpu_before != options.expected_cpu) fail("process was not pinned to expected CPU before timing");
  compiler_barrier();
  events.start();
  const uint64_t wall_start = monotonic_raw_ns();
  const uint64_t tsc_before = tsc_start();
  if (options.state == "page_cold") {
    sample.checksum = workload.run_one_pass();
    sample.passes = 1;
    sample.logical_operations = workload.logical_operations_per_pass();
    sample.first_touch_exception = true;
  } else {
    do {
      sample.checksum ^= workload.run_one_pass();
      ++sample.passes;
      sample.logical_operations += workload.logical_operations_per_pass();
    } while (monotonic_raw_ns() - wall_start < static_cast<uint64_t>(options.min_duration_ms) * 1'000'000ULL);
  }
  const uint64_t tsc_after = tsc_end();
  const uint64_t wall_end = monotonic_raw_ns();
  std::vector<CounterRead> counters = events.stop_and_read();
  compiler_barrier();
  sample.cpu_after = current_cpu();
  if (sample.cpu_after != options.expected_cpu) fail("process migrated during timing");
  if (getrusage(RUSAGE_SELF, &after) != 0) fail("getrusage after sample failed");
  sample.wall_ns = wall_end - wall_start;
  sample.tsc_ticks = tsc_after - tsc_before;
  sample.minor_faults = after.ru_minflt - before.ru_minflt;
  sample.major_faults = after.ru_majflt - before.ru_majflt;
  if (sample.checksum != workload.expected_checksum(sample.passes)) {
    fail("checksum mismatch; timed work did not preserve the semantic contract");
  }
  if (!sample.first_touch_exception && sample.wall_ns < static_cast<uint64_t>(options.min_duration_ms) * 1'000'000ULL) {
    fail("normal sample did not meet minimum duration");
  }
  return counters;
}

void ensure_parent(const std::filesystem::path& path) {
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
}

void append_sample(const std::filesystem::path& output, const Options& options, const LadderWorkload& workload,
                   const Sample& sample, const std::string& sample_id) {
  ensure_parent(output);
  const bool write_header = !std::filesystem::exists(output) || std::filesystem::file_size(output) == 0;
  std::ofstream file(output, std::ios::app);
  if (!file) fail("cannot write sample CSV");
  if (write_header) {
    file << "schema_version,sample_id,build_id,round,mode,implementation,state,perf_profile,footprint_bytes,physical_footprint_bytes,logical_operations,passes,wall_ns,tsc_ticks,cpu_before,cpu_after,minor_faults,major_faults,checksum,first_touch_exception\n";
  }
  file << kSampleSchemaVersion << ',' << sample_id << ',' << options.build_id << ',' << options.round << ','
       << options.mode << ',' << options.implementation << ',' << options.state << ',' << options.perf_profile << ','
       << options.footprint_bytes << ',' << workload.physical_footprint_bytes() << ',' << sample.logical_operations << ','
       << sample.passes << ',' << sample.wall_ns << ',' << sample.tsc_ticks << ',' << sample.cpu_before << ','
       << sample.cpu_after << ',' << sample.minor_faults << ',' << sample.major_faults << ',' << sample.checksum << ','
       << (sample.first_touch_exception ? 1 : 0) << '\n';
}

void append_counters(const std::filesystem::path& output, const Options& options, const std::string& sample_id,
                     const std::vector<CounterRead>& counters) {
  ensure_parent(output);
  const bool write_header = !std::filesystem::exists(output) || std::filesystem::file_size(output) == 0;
  std::ofstream file(output, std::ios::app);
  if (!file) fail("cannot write counter CSV");
  if (write_header) file << "schema_version,sample_id,build_id,perf_profile,event,value,time_enabled,time_running\n";
  for (const CounterRead& counter : counters) {
    file << kSampleSchemaVersion << ',' << sample_id << ',' << options.build_id << ',' << options.perf_profile << ','
         << counter.name << ',' << counter.value << ',' << counter.time_enabled << ',' << counter.time_running << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parse_options(argc, argv);
    LadderWorkload workload(options);
    Sample sample;
    const std::vector<CounterRead> counters = measure_with_counters(workload, options, sample);
    const std::string sample_id = options.build_id + "-r" + std::to_string(options.round) + "-" + options.mode +
                                  "-" + options.implementation + "-" + options.state + "-" + options.perf_profile +
                                  "-" + std::to_string(options.footprint_bytes);
    append_sample(options.output_path, options, workload, sample, sample_id);
    append_counters(options.counter_output_path, options, sample_id, counters);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "working_set_ladder: " << error.what() << '\n';
    return 1;
  }
}
