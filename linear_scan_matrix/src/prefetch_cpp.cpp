// C++ front end for the same random 64-byte-node, late-prefetch control.
// `std::vector` owns setup storage only; the timed loop receives `data()`.
#include "pmu_scope.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

struct alignas(64) Node {
  uint32_t next;
  uint32_t value;
  uint8_t padding[56];
};
static_assert(sizeof(Node) == 64);
static volatile uint64_t sink;

// The paired control compiles this exact source with PREFETCH_ENABLED=0.
// Keeping the surrounding data layout and loop identical prevents a different
// pointer-chase implementation from being mistaken for a prefetch effect.
#ifndef PREFETCH_ENABLED
#define PREFETCH_ENABLED 1
#endif

static uint64_t splitmix64(uint64_t& state) {
  state += UINT64_C(0x9e3779b97f4a7c15);
  uint64_t value = state;
  value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31);
}

static void shuffle(std::vector<uint32_t>& order) {
  uint64_t state = UINT64_C(20260910) ^ UINT64_C(0xabcddcba);
  for (size_t index = order.size(); index > 1; --index) {
    std::swap(order[index - 1], order[splitmix64(state) % index]);
  }
}

__attribute__((noinline)) static uint64_t chase_with_prefetch(
    const Node* nodes, uint32_t start, uint64_t operations) {
  uint32_t index = start;
  uint64_t sum = 0;
  for (uint64_t step = 0; step < operations; ++step) {
    const Node& node = nodes[index];        // demand-load the current line
    const uint32_t next = node.next;        // resolves the only next address
#if PREFETCH_ENABLED
    __builtin_prefetch(&nodes[next], 0, 1); // late one-hop request, no SIMD
#endif
    sum += node.value;
    index = next;
  }
  return sum ^ index;
}

int main(int argc, char** argv) {
  if (argc != 3) throw std::runtime_error("usage: prefetch_cpp footprint_bytes passes");
  const size_t bytes = std::strtoull(argv[1], nullptr, 10);
  const uint64_t passes = std::strtoull(argv[2], nullptr, 10);
  if (bytes < 1024 || bytes % 64 != 0 || passes == 0) {
    throw std::runtime_error("footprint must be a nonzero multiple of 64 bytes");
  }
  const size_t count = bytes / sizeof(Node);
  std::vector<Node> nodes(count);
  std::vector<uint32_t> order(count);
  for (size_t index = 0; index < count; ++index) order[index] = (uint32_t)index;
  shuffle(order);
  uint64_t state = UINT64_C(20260910), one_pass_sum = 0;
  for (size_t index = 0; index < count; ++index) {
    nodes[order[index]].next = order[(index + 1) % count];
    nodes[order[index]].value = (uint32_t)splitmix64(state);
    one_pass_sum += nodes[order[index]].value;
  }
  const uint64_t operations = count * passes;
  const uint64_t expected = one_pass_sum * passes ^ order[0];
  scan_pmu_scope scope{};
  scan_pmu_result pmu{};
  if (scan_pmu_open(&scope) != 0 || scan_pmu_start(&scope) != 0) {
    throw std::runtime_error("could not start PMU");
  }
  const uint64_t begun = scan_clock_now_ns();
  const uint64_t checksum = chase_with_prefetch(nodes.data(), order[0], operations);
  const uint64_t elapsed = scan_clock_now_ns() - begun;
  if (scan_pmu_stop(&scope, &pmu) != 0) throw std::runtime_error("could not read PMU");
  const bool available = scan_pmu_is_available(&scope) != 0;
  scan_pmu_close(&scope);
  sink = checksum;
  if (checksum != expected || !available || pmu.time_enabled == 0 ||
      pmu.time_enabled != pmu.time_running) throw std::runtime_error("invalid PMU row");
  std::cout << "bytes=" << bytes << ",passes=" << passes << ",operations=" << operations
            << ",elapsed_ns=" << elapsed << ",checksum=" << checksum
            << ",expected_checksum=" << expected << ",pmu_cycles=" << pmu.cycles
            << ",pmu_instructions=" << pmu.instructions << ",pmu_ref_cycles=" << pmu.ref_cycles
            << ",pmu_time_enabled=" << pmu.time_enabled << ",pmu_time_running=" << pmu.time_running
            << ",pmu_available=" << available << '\n';
}
