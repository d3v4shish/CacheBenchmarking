// MLP8 control for a random cycle of 64-byte nodes.
//
// The timed body owns eight independent successor registers.  A lane cannot
// discover its own next address until its node has returned, but it has no
// dependency on the other seven lanes.  This is latency overlap (MLP), not
// SIMD, prefetching, or a direct random-load stream.
#include "pmu_scope.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef MLP_LANES
#define MLP_LANES 8
#endif
static_assert(MLP_LANES == 1 || MLP_LANES == 8,
              "the paired control supports one or eight live chains");

struct alignas(64) Node {
  uint32_t next;
  uint32_t value;
  uint8_t padding[56];
};
static_assert(sizeof(Node) == 64);
static volatile uint64_t sink;

static uint64_t splitmix64(uint64_t& state) {
  state += UINT64_C(0x9e3779b97f4a7c15);
  uint64_t value = state;
  value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31U);
}

static void shuffle(std::vector<uint32_t>& order) {
  uint64_t state = UINT64_C(20260910) ^ UINT64_C(0xabcddcba);
  for (size_t i = order.size(); i > 1; --i) {
    const size_t other = splitmix64(state) % i;
    std::swap(order[i - 1], order[other]);
  }
}

// noinline preserves a readable hot loop in the retained disassembly.
// Writing the eight lane updates out explicitly makes the intended independent
// load-to-use chains visible to both the compiler and the reader.
__attribute__((noinline)) static uint64_t traverse(
    const Node* nodes, const uint32_t start, const uint64_t operations) {
  if (operations % MLP_LANES != 0) throw std::runtime_error("unaligned operation count");
  uint32_t lane0 = start;
#if MLP_LANES == 8
  const Node first0 = nodes[lane0];
  uint32_t lane1 = first0.next;
  const Node first1 = nodes[lane1];
  uint32_t lane2 = first1.next;
  const Node first2 = nodes[lane2];
  uint32_t lane3 = first2.next;
  const Node first3 = nodes[lane3];
  uint32_t lane4 = first3.next;
  const Node first4 = nodes[lane4];
  uint32_t lane5 = first4.next;
  const Node first5 = nodes[lane5];
  uint32_t lane6 = first5.next;
  const Node first6 = nodes[lane6];
  uint32_t lane7 = first6.next;
#endif

  uint64_t sum = 0;
  for (uint64_t step = 0; step < operations / MLP_LANES; ++step) {
    const Node node0 = nodes[lane0];
    sum += node0.value;
    lane0 = node0.next;
#if MLP_LANES == 8
    const Node node1 = nodes[lane1];
    const Node node2 = nodes[lane2];
    const Node node3 = nodes[lane3];
    const Node node4 = nodes[lane4];
    const Node node5 = nodes[lane5];
    const Node node6 = nodes[lane6];
    const Node node7 = nodes[lane7];
    sum += node1.value;
    sum += node2.value;
    sum += node3.value;
    sum += node4.value;
    sum += node5.value;
    sum += node6.value;
    sum += node7.value;
    lane1 = node1.next;
    lane2 = node2.next;
    lane3 = node3.next;
    lane4 = node4.next;
    lane5 = node5.next;
    lane6 = node6.next;
    lane7 = node7.next;
#endif
  }
#if MLP_LANES == 8
  return sum ^ lane0 ^ lane1 ^ lane2 ^ lane3 ^ lane4 ^ lane5 ^ lane6 ^ lane7;
#else
  return sum ^ lane0;
#endif
}

int main(int argc, char** argv) {
  if (argc != 3) throw std::runtime_error("usage: mlp8_cpp footprint_bytes passes");
  const size_t bytes = std::strtoull(argv[1], nullptr, 10);
  const uint64_t passes = std::strtoull(argv[2], nullptr, 10);
  if (bytes < 1024 || bytes % 64 != 0 || passes == 0) {
    throw std::runtime_error("footprint must be a nonzero multiple of 64 bytes");
  }
  const size_t count = bytes / sizeof(Node);
  if (count % MLP_LANES != 0) throw std::runtime_error("node count must divide lane count");

  std::vector<Node> nodes(count);
  std::vector<uint32_t> order(count);
  for (size_t i = 0; i < count; ++i) order[i] = static_cast<uint32_t>(i);
  shuffle(order);
  uint64_t payload_state = UINT64_C(20260910);
  for (size_t i = 0; i < count; ++i) {
    nodes[order[i]].next = order[(i + 1) % count];
    nodes[order[i]].value = static_cast<uint32_t>(splitmix64(payload_state));
  }

  const uint64_t operations = static_cast<uint64_t>(count) * passes;
  const uint32_t start = order.front();
  // This reference traversal runs before timing.  It proves the exact checksum
  // for this lane count without adding construction or validation work to PMU.
  const uint64_t expected = traverse(nodes.data(), start, operations);

  scan_pmu_scope scope{};
  scan_pmu_result pmu{};
  if (scan_pmu_open(&scope) != 0 || scan_pmu_start(&scope) != 0) {
    throw std::runtime_error("could not start PMU");
  }
  const uint64_t begun = scan_clock_now_ns();
  const uint64_t checksum = traverse(nodes.data(), start, operations);
  const uint64_t elapsed = scan_clock_now_ns() - begun;
  if (scan_pmu_stop(&scope, &pmu) != 0) throw std::runtime_error("could not read PMU");
  const bool available = scan_pmu_is_available(&scope) != 0;
  scan_pmu_close(&scope);
  sink = checksum;

  if (checksum != expected || !available || pmu.time_enabled == 0 ||
      pmu.time_enabled != pmu.time_running) {
    throw std::runtime_error("invalid checksum or multiplexed PMU interval");
  }
  std::cout << "bytes=" << bytes << ",passes=" << passes
            << ",operations=" << operations << ",elapsed_ns=" << elapsed
            << ",checksum=" << checksum << ",expected_checksum=" << expected
            << ",pmu_cycles=" << pmu.cycles << ",pmu_instructions=" << pmu.instructions
            << ",pmu_ref_cycles=" << pmu.ref_cycles
            << ",pmu_time_enabled=" << pmu.time_enabled
            << ",pmu_time_running=" << pmu.time_running
            << ",pmu_available=" << available << '\n';
}
