/* One random 64-byte node cycle plus a deliberately late software prefetch.
 * The current node must arrive before `next` is known, so the prefetch only
 * overlaps loop bookkeeping and the node payload add; it is not look-ahead.
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
_Static_assert(sizeof(Node) == 64, "one node must occupy one cache line");

static volatile uint64_t sink;

static uint64_t splitmix64(uint64_t *state) {
  *state += UINT64_C(0x9e3779b97f4a7c15);
  uint64_t value = *state;
  value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31);
}

static void shuffle(uint32_t *order, size_t count) {
  uint64_t state = UINT64_C(20260910) ^ UINT64_C(0xabcddcba);
  for (size_t index = count; index > 1; --index) {
    const size_t swap_index = (size_t)(splitmix64(&state) % index);
    const uint32_t tmp = order[index - 1];
    order[index - 1] = order[swap_index];
    order[swap_index] = tmp;
  }
}

__attribute__((noinline)) static uint64_t chase_with_prefetch(
    const Node *nodes, uint32_t start, uint64_t operations) {
  uint32_t index = start;
  uint64_t sum = 0;
  for (uint64_t step = 0; step < operations; ++step) {
    const Node *node = &nodes[index];       /* demand-load current cache line */
    const uint32_t next = node->next;       /* only now is next address known */
    __builtin_prefetch(&nodes[next], 0, 1); /* request exactly that next line */
    sum += node->value;                     /* small gap before the next demand */
    index = next;
  }
  return sum ^ index;
}

int main(int argc, char **argv) {
  if (argc != 3) return fprintf(stderr, "usage: %s footprint_bytes passes\n", argv[0]), 64;
  const size_t bytes = (size_t)strtoull(argv[1], NULL, 10);
  const uint64_t passes = strtoull(argv[2], NULL, 10);
  if (bytes < 1024 || bytes % 64 != 0 || passes == 0) {
    return fprintf(stderr, "footprint must be a nonzero multiple of 64 bytes\n"), 64;
  }

  const size_t nodes_count = bytes / sizeof(Node);
  Node *nodes = NULL;
  uint32_t *order = malloc(nodes_count * sizeof(*order));
  if (posix_memalign((void **)&nodes, 64, bytes) != 0 || order == NULL) {
    perror("allocation");
    free(nodes);
    free(order);
    return 2;
  }
  for (size_t index = 0; index < nodes_count; ++index) order[index] = (uint32_t)index;
  shuffle(order, nodes_count);

  uint64_t state = UINT64_C(20260910);
  uint64_t one_pass_sum = 0;
  for (size_t index = 0; index < nodes_count; ++index) {
    const uint32_t current = order[index];
    nodes[current].next = order[(index + 1) % nodes_count];
    nodes[current].value = (uint32_t)splitmix64(&state);
    one_pass_sum += nodes[current].value;
  }
  const uint32_t start = order[0];
  const uint64_t operations = (uint64_t)nodes_count * passes;
  const uint64_t expected = one_pass_sum * passes ^ start;

  scan_pmu_scope scope;
  scan_pmu_result pmu;
  if (scan_pmu_open(&scope) != 0 || scan_pmu_start(&scope) != 0) {
    fprintf(stderr, "PMU setup failed: %s\n", strerror(errno));
    return 70;
  }
  const uint64_t begun = scan_clock_now_ns();
  const uint64_t checksum = chase_with_prefetch(nodes, start, operations);
  const uint64_t elapsed = scan_clock_now_ns() - begun;
  if (scan_pmu_stop(&scope, &pmu) != 0) return fprintf(stderr, "PMU read failed\n"), 70;
  const int available = scan_pmu_is_available(&scope);
  scan_pmu_close(&scope);
  sink = checksum;

  if (checksum != expected || !available || pmu.time_enabled == 0 ||
      pmu.time_enabled != pmu.time_running) {
    return fprintf(stderr, "invalid checksum or multiplexed PMU interval\n"), 65;
  }
  printf("bytes=%zu,passes=%" PRIu64 ",operations=%" PRIu64 ",elapsed_ns=%" PRIu64
         ",checksum=%" PRIu64 ",expected_checksum=%" PRIu64 ",pmu_cycles=%" PRIu64
         ",pmu_instructions=%" PRIu64 ",pmu_ref_cycles=%" PRIu64
         ",pmu_time_enabled=%" PRIu64 ",pmu_time_running=%" PRIu64 ",pmu_available=%d\n",
         bytes, passes, operations, elapsed, checksum, expected, pmu.cycles,
         pmu.instructions, pmu.ref_cycles, pmu.time_enabled, pmu.time_running, available);
  free(nodes);
  free(order);
  return 0;
}
