/* C member of the random-direct-load language control.
 *
 * Two equal-sized arrays form the physical footprint: `values` carries the
 * uint32_t payload, while `queries` is a deterministic permutation of its
 * indices.  `sum += values[queries[i]]` is the whole timed workload.  Data
 * fill and permutation construction happen before the PMU interval.
 */
#define _POSIX_C_SOURCE 200809L
#include "pmu_scope.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile uint32_t sink;

static uint64_t splitmix64(uint64_t *state) {
  *state += UINT64_C(0x9e3779b97f4a7c15);
  uint64_t value = *state;
  value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31);
}

/* Fisher-Yates and the same SplitMix draw make every language visit the same
 * logical sequence.  Modulo bias is irrelevant here: repeatability, not a
 * cryptographic shuffle, is the contract. */
static void shuffle(uint32_t *queries, size_t words, uint64_t seed) {
  uint64_t state = seed ^ UINT64_C(0x5eed5eed);
  for (size_t i = words; i > 1; --i) {
    const size_t j = (size_t)(splitmix64(&state) % i);
    const uint32_t tmp = queries[i - 1]; queries[i - 1] = queries[j]; queries[j] = tmp;
  }
}

static uint32_t random_sum(const uint32_t *values, const uint32_t *queries,
                           size_t words, uint64_t passes) {
  uint32_t sum = 0;
  for (uint64_t pass = 0; pass < passes; ++pass)
    for (size_t i = 0; i < words; ++i)
      sum += values[queries[i]]; /* dependent address calculation; random payload load */
  return sum;
}

int main(int argc, char **argv) {
  if (argc != 3) { fprintf(stderr, "usage: %s footprint_bytes passes\n", argv[0]); return 64; }
  const size_t bytes = (size_t)strtoull(argv[1], NULL, 10);
  const uint64_t passes = strtoull(argv[2], NULL, 10);
  if (bytes < 64 || bytes % 64 != 0 || passes == 0) { fprintf(stderr, "footprint must be a nonzero multiple of 64 bytes\n"); return 64; }
  const size_t words = bytes / sizeof(uint32_t);
  uint32_t *values = NULL, *queries = NULL;
  if (posix_memalign((void **)&values, 64, bytes) || posix_memalign((void **)&queries, 64, bytes)) { perror("posix_memalign"); free(values); free(queries); return 2; }
  uint64_t state = UINT64_C(20260909); uint32_t one_pass = 0;
  for (size_t i = 0; i < words; ++i) { values[i] = (uint32_t)splitmix64(&state); queries[i] = (uint32_t)i; one_pass += values[i]; }
  shuffle(queries, words, UINT64_C(20260909));
  const uint64_t operations = (uint64_t)words * passes;
  scan_pmu_scope pmu; scan_pmu_result result;
  if (scan_pmu_open(&pmu) || scan_pmu_start(&pmu)) { fprintf(stderr, "PMU setup failed: %s\n", strerror(errno)); free(values); free(queries); return 70; }
  const uint64_t start_ns = scan_clock_now_ns();
  const uint32_t checksum = random_sum(values, queries, words, passes);
  const uint64_t elapsed_ns = scan_clock_now_ns() - start_ns;
  if (scan_pmu_stop(&pmu, &result)) { fprintf(stderr, "PMU read failed: %s\n", strerror(errno)); scan_pmu_close(&pmu); free(values); free(queries); return 70; }
  const int available = scan_pmu_is_available(&pmu); scan_pmu_close(&pmu);
  const uint32_t expected = (uint32_t)(one_pass * passes); sink = checksum;
  if (checksum != expected || !available || !result.time_enabled || result.time_enabled != result.time_running) { fprintf(stderr, "invalid checksum or PMU interval\n"); free(values); free(queries); return 65; }
  printf("bytes=%zu,passes=%" PRIu64 ",operations=%" PRIu64 ",elapsed_ns=%" PRIu64 ",checksum=%" PRIu32 ",expected_checksum=%" PRIu32 ",pmu_cycles=%" PRIu64 ",pmu_instructions=%" PRIu64 ",pmu_ref_cycles=%" PRIu64 ",pmu_time_enabled=%" PRIu64 ",pmu_time_running=%" PRIu64 ",pmu_available=%d\n", bytes, passes, operations, elapsed_ns, checksum, expected, result.cycles, result.instructions, result.ref_cycles, result.time_enabled, result.time_running, available);
  free(values); free(queries); return 0;
}
