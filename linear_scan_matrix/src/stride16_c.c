/*
 * C member of the executed stride-16 language matrix.
 *
 * There are sixteen uint32_t values (64 bytes) in one Haswell cache line.
 * Index += 16 therefore reads exactly the first value from each successive
 * line. Allocation and deterministic initialization deliberately precede the
 * PMU/timer region so neither can be mistaken for scan work.
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

static void *allocate_aligned(size_t bytes) {
  void *data = NULL;
  if (posix_memalign(&data, 64, bytes) != 0 || data == NULL) {
    perror("posix_memalign");
    exit(2);
  }
  return data;
}

/* One load is useful from every 64-byte cache line; the other 15 words are
 * intentionally skipped. `sum` is volatile-observed after the PMU stops. */
static uint32_t stride16_sum(const uint32_t *values, size_t words,
                             uint64_t passes) {
  uint32_t sum = 0;
  for (uint64_t pass = 0; pass < passes; ++pass)
    for (size_t index = 0; index < words; index += 16)
      sum += values[index];
  return sum;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s footprint_bytes passes\n", argv[0]);
    return 64;
  }
  const size_t bytes = (size_t)strtoull(argv[1], NULL, 10);
  const uint64_t passes = strtoull(argv[2], NULL, 10);
  if (bytes < 64 || bytes % 64 != 0 || passes == 0) {
    fprintf(stderr, "footprint must be a nonzero multiple of 64 bytes\n");
    return 64;
  }

  const size_t words = bytes / sizeof(uint32_t);
  const uint64_t operations = (uint64_t)(words / 16) * passes;
  uint32_t *values = allocate_aligned(bytes);
  for (size_t index = 0; index < words; ++index) values[index] = UINT32_C(0x01010101);

  scan_pmu_scope pmu;
  scan_pmu_result result;
  if (scan_pmu_open(&pmu) != 0 || scan_pmu_start(&pmu) != 0) {
    fprintf(stderr, "PMU setup failed: %s\n", strerror(errno));
    free(values);
    return 70;
  }
  const uint64_t start_ns = scan_clock_now_ns();
  const uint32_t checksum = stride16_sum(values, words, passes);
  const uint64_t elapsed_ns = scan_clock_now_ns() - start_ns;
  if (scan_pmu_stop(&pmu, &result) != 0) {
    fprintf(stderr, "PMU read failed: %s\n", strerror(errno));
    scan_pmu_close(&pmu); free(values); return 70;
  }
  const int pmu_available = scan_pmu_is_available(&pmu);
  scan_pmu_close(&pmu);

  const uint32_t expected = (uint32_t)(operations * UINT64_C(0x01010101));
  sink = checksum;
  if (checksum != expected || !pmu_available || result.time_enabled == 0 ||
      result.time_enabled != result.time_running) {
    fprintf(stderr, "invalid checksum or PMU interval\n"); free(values); return 65;
  }
  printf("bytes=%zu,passes=%" PRIu64 ",operations=%" PRIu64
         ",elapsed_ns=%" PRIu64 ",checksum=%" PRIu32
         ",expected_checksum=%" PRIu32 ",pmu_cycles=%" PRIu64
         ",pmu_instructions=%" PRIu64 ",pmu_ref_cycles=%" PRIu64
         ",pmu_time_enabled=%" PRIu64 ",pmu_time_running=%" PRIu64
         ",pmu_available=%d\n", bytes, passes, operations, elapsed_ns,
         checksum, expected, result.cycles, result.instructions,
         result.ref_cycles, result.time_enabled, result.time_running,
         pmu_available);
  free(values);
  return 0;
}
