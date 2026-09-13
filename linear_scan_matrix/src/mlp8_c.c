/* C front end for the MLP8 random-node cycle.
 *
 * Every Node is exactly one cache line.  The next address in each lane comes
 * from that lane's returned node; the eight lanes only share the accumulator.
 * The loop deliberately contains no prefetch instruction or vector operation.
 */
#define _POSIX_C_SOURCE 200809L
#include "pmu_scope.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct __attribute__((aligned(64))) {
  uint32_t next;
  uint32_t value;
  uint8_t padding[56];
} Node;
_Static_assert(sizeof(Node) == 64, "Node must occupy one cache line");
static volatile uint64_t sink;

static uint64_t splitmix64(uint64_t* state) {
  *state += UINT64_C(0x9e3779b97f4a7c15);
  uint64_t value = *state;
  value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31U);
}

static void shuffle(uint32_t* order, const size_t count) {
  uint64_t state = UINT64_C(20260910) ^ UINT64_C(0xabcddcba);
  for (size_t i = count; i > 1; --i) {
    const size_t other = (size_t)(splitmix64(&state) % i);
    const uint32_t temporary = order[i - 1];
    order[i - 1] = order[other];
    order[other] = temporary;
  }
}

/* Explicit lane variables are the test: the issue window can overlap eight
 * separate loads, but none of them knows its own following address early. */
__attribute__((noinline)) static uint64_t traverse(
    const Node* nodes, const uint32_t start, const uint64_t operations) {
  if (operations % 8U != 0) return 0;
  uint32_t lane0 = start;
  Node first = nodes[lane0];
  uint32_t lane1 = first.next;
  first = nodes[lane1];
  uint32_t lane2 = first.next;
  first = nodes[lane2];
  uint32_t lane3 = first.next;
  first = nodes[lane3];
  uint32_t lane4 = first.next;
  first = nodes[lane4];
  uint32_t lane5 = first.next;
  first = nodes[lane5];
  uint32_t lane6 = first.next;
  first = nodes[lane6];
  uint32_t lane7 = first.next;
  uint64_t sum = 0;

  for (uint64_t step = 0; step < operations / 8U; ++step) {
    const Node node0 = nodes[lane0];
    const Node node1 = nodes[lane1];
    const Node node2 = nodes[lane2];
    const Node node3 = nodes[lane3];
    const Node node4 = nodes[lane4];
    const Node node5 = nodes[lane5];
    const Node node6 = nodes[lane6];
    const Node node7 = nodes[lane7];
    sum += node0.value;
    sum += node1.value;
    sum += node2.value;
    sum += node3.value;
    sum += node4.value;
    sum += node5.value;
    sum += node6.value;
    sum += node7.value;
    lane0 = node0.next;
    lane1 = node1.next;
    lane2 = node2.next;
    lane3 = node3.next;
    lane4 = node4.next;
    lane5 = node5.next;
    lane6 = node6.next;
    lane7 = node7.next;
  }
  return sum ^ lane0 ^ lane1 ^ lane2 ^ lane3 ^ lane4 ^ lane5 ^ lane6 ^ lane7;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s footprint_bytes passes\n", argv[0]);
    return 64;
  }
  const size_t bytes = (size_t)strtoull(argv[1], NULL, 10);
  const uint64_t passes = strtoull(argv[2], NULL, 10);
  if (bytes < 1024 || bytes % 64 != 0 || passes == 0) {
    fprintf(stderr, "footprint must be a nonzero multiple of 64 bytes\n");
    return 64;
  }

  const size_t count = bytes / sizeof(Node);
  Node* nodes = NULL;
  uint32_t* order = malloc(count * sizeof(*order));
  if (posix_memalign((void**)&nodes, 64, bytes) != 0 || order == NULL) {
    perror("allocation");
    free(nodes);
    free(order);
    return 2;
  }
  for (size_t i = 0; i < count; ++i) order[i] = (uint32_t)i;
  shuffle(order, count);
  uint64_t payload_state = UINT64_C(20260910);
  for (size_t i = 0; i < count; ++i) {
    nodes[order[i]].next = order[(i + 1) % count];
    nodes[order[i]].value = (uint32_t)splitmix64(&payload_state);
  }

  const uint64_t operations = (uint64_t)count * passes;
  const uint64_t expected = traverse(nodes, order[0], operations);
  scan_pmu_scope scope;
  scan_pmu_result pmu;
  if (scan_pmu_open(&scope) != 0 || scan_pmu_start(&scope) != 0) {
    fprintf(stderr, "PMU setup failed: %s\n", strerror(errno));
    return 70;
  }
  const uint64_t begun = scan_clock_now_ns();
  const uint64_t checksum = traverse(nodes, order[0], operations);
  const uint64_t elapsed = scan_clock_now_ns() - begun;
  if (scan_pmu_stop(&scope, &pmu) != 0) {
    fprintf(stderr, "PMU read failed\n");
    scan_pmu_close(&scope);
    return 70;
  }
  const int available = scan_pmu_is_available(&scope);
  scan_pmu_close(&scope);
  sink = checksum;
  if (checksum != expected || !available || pmu.time_enabled == 0 ||
      pmu.time_enabled != pmu.time_running) {
    fprintf(stderr, "invalid checksum or multiplexed PMU interval\n");
    return 65;
  }
  printf("bytes=%zu,passes=%" PRIu64 ",operations=%" PRIu64
         ",elapsed_ns=%" PRIu64 ",checksum=%" PRIu64
         ",expected_checksum=%" PRIu64 ",pmu_cycles=%" PRIu64
         ",pmu_instructions=%" PRIu64 ",pmu_ref_cycles=%" PRIu64
         ",pmu_time_enabled=%" PRIu64 ",pmu_time_running=%" PRIu64
         ",pmu_available=%d\n",
         bytes, passes, operations, elapsed, checksum, expected, pmu.cycles,
         pmu.instructions, pmu.ref_cycles, pmu.time_enabled, pmu.time_running,
         available);
  free(nodes);
  free(order);
  return 0;
}
