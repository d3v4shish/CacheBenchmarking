/*
 * Shared C/C++ scan implementation for v2. It is compiled once as C and once
 * as C++. Scalar builds disable auto-vectorisation in the build flags; AVX2
 * builds reach SIMD only through the visible intrinsics below.
 *
 * Allocation and deterministic fill happen before both the clock and PMU
 * region. Float inputs alternate -1,+1, so a long scalar reduction keeps
 * changing instead of becoming insensitive after repeated +1.0f additions.
 */
#define _POSIX_C_SOURCE 200809L
#include "pmu_scope.h"

#include <errno.h>
#include <immintrin.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t bytes[16]; } string16;

static volatile uint64_t sink;

static void *aligned_bytes(size_t bytes) {
  void *pointer = NULL;
  if (posix_memalign(&pointer, 32, bytes) != 0 || !pointer) {
    perror("posix_memalign");
    exit(2);
  }
  return pointer;
}

static uint64_t scalar_u16(const uint16_t *values, size_t count, uint64_t passes) {
  uint16_t sum = 0;
  for (uint64_t pass = 0; pass < passes; ++pass)
    for (size_t index = 0; index < count; ++index)
      sum = (uint16_t)(sum + values[index]);
  return sum;
}

static uint64_t scalar_u32(const uint32_t *values, size_t count, uint64_t passes) {
  uint32_t sum = 0;
  for (uint64_t pass = 0; pass < passes; ++pass)
    for (size_t index = 0; index < count; ++index) sum += values[index];
  return sum;
}

static uint64_t scalar_f32(const float *values, size_t count, uint64_t passes) {
  float sum = 0.0f;
  uint32_t bits;
  for (uint64_t pass = 0; pass < passes; ++pass)
    for (size_t index = 0; index < count; ++index) sum += values[index];
  memcpy(&bits, &sum, sizeof(bits));
  return bits;
}

static uint64_t scalar_f64(const double *values, size_t count, uint64_t passes) {
  double sum = 0.0;
  uint64_t bits;
  for (uint64_t pass = 0; pass < passes; ++pass)
    for (size_t index = 0; index < count; ++index) sum += values[index];
  memcpy(&bits, &sum, sizeof(bits));
  return bits;
}

static uint64_t scalar_s16(const string16 *values, size_t count, uint64_t passes) {
  uint64_t sum = 0;
  for (uint64_t pass = 0; pass < passes; ++pass)
    for (size_t index = 0; index < count; ++index)
      for (size_t byte = 0; byte < 16; ++byte) sum += values[index].bytes[byte];
  return sum;
}

static uint64_t avx_u16(const uint16_t *values, size_t count, uint64_t passes) {
  uint16_t total = 0;
  for (uint64_t pass = 0; pass < passes; ++pass) {
    __m256i lanes = _mm256_setzero_si256();
    size_t index = 0;
    for (; index + 16 <= count; index += 16)
      lanes = _mm256_add_epi16(
          lanes, _mm256_loadu_si256((const __m256i *)(values + index)));
    uint16_t reduced[16];
    _mm256_storeu_si256((__m256i *)reduced, lanes);
    for (size_t lane = 0; lane < 16; ++lane)
      total = (uint16_t)(total + reduced[lane]);
    for (; index < count; ++index) total = (uint16_t)(total + values[index]);
  }
  return total;
}

static uint64_t avx_u32(const uint32_t *values, size_t count, uint64_t passes) {
  uint32_t total = 0;
  for (uint64_t pass = 0; pass < passes; ++pass) {
    __m256i lanes = _mm256_setzero_si256();
    size_t index = 0;
    for (; index + 8 <= count; index += 8)
      lanes = _mm256_add_epi32(
          lanes, _mm256_loadu_si256((const __m256i *)(values + index)));
    uint32_t reduced[8];
    _mm256_storeu_si256((__m256i *)reduced, lanes);
    for (size_t lane = 0; lane < 8; ++lane) total += reduced[lane];
    for (; index < count; ++index) total += values[index];
  }
  return total;
}

static uint64_t avx_f32(const float *values, size_t count, uint64_t passes) {
  float total = 0.0f;
  uint32_t bits;
  for (uint64_t pass = 0; pass < passes; ++pass) {
    __m256 lanes = _mm256_setzero_ps();
    size_t index = 0;
    for (; index + 8 <= count; index += 8)
      lanes = _mm256_add_ps(lanes, _mm256_loadu_ps(values + index));
    float reduced[8];
    _mm256_storeu_ps(reduced, lanes);
    for (size_t lane = 0; lane < 8; ++lane) total += reduced[lane];
    for (; index < count; ++index) total += values[index];
  }
  memcpy(&bits, &total, sizeof(bits));
  return bits;
}

static uint64_t avx_f64(const double *values, size_t count, uint64_t passes) {
  double total = 0.0;
  uint64_t bits;
  for (uint64_t pass = 0; pass < passes; ++pass) {
    __m256d lanes = _mm256_setzero_pd();
    size_t index = 0;
    for (; index + 4 <= count; index += 4)
      lanes = _mm256_add_pd(lanes, _mm256_loadu_pd(values + index));
    double reduced[4];
    _mm256_storeu_pd(reduced, lanes);
    for (size_t lane = 0; lane < 4; ++lane) total += reduced[lane];
    for (; index < count; ++index) total += values[index];
  }
  memcpy(&bits, &total, sizeof(bits));
  return bits;
}

static uint64_t avx_s16(const string16 *values, size_t count, uint64_t passes) {
  uint64_t total = 0;
  const __m256i zero = _mm256_setzero_si256();
  for (uint64_t pass = 0; pass < passes; ++pass) {
    __m256i lanes = _mm256_setzero_si256();
    size_t index = 0;
    for (; index + 2 <= count; index += 2) {
      const __m256i bytes =
          _mm256_loadu_si256((const __m256i *)(values + index));
      lanes = _mm256_add_epi64(lanes, _mm256_sad_epu8(bytes, zero));
    }
    uint64_t reduced[4];
    _mm256_storeu_si256((__m256i *)reduced, lanes);
    total += reduced[0] + reduced[1] + reduced[2] + reduced[3];
    for (; index < count; ++index)
      for (size_t byte = 0; byte < 16; ++byte) total += values[index].bytes[byte];
  }
  return total;
}

static size_t element_width(const char *type) {
  if (strcmp(type, "short") == 0) return sizeof(uint16_t);
  if (strcmp(type, "int") == 0 || strcmp(type, "float") == 0)
    return sizeof(uint32_t);
  if (strcmp(type, "double") == 0) return sizeof(uint64_t);
  if (strcmp(type, "string16") == 0) return sizeof(string16);
  return 0;
}

static uint64_t expected_checksum(const char *type, size_t count, uint64_t passes) {
  const uint64_t operations = (uint64_t)count * passes;
  if (strcmp(type, "short") == 0) return (uint16_t)(operations * UINT64_C(0x0101));
  if (strcmp(type, "int") == 0)
    return (uint32_t)(operations * UINT64_C(0x01010101));
  /* v2 uses an even number of alternating -1,+1 FP inputs. */
  if (strcmp(type, "float") == 0 || strcmp(type, "double") == 0) return 0;
  if (strcmp(type, "string16") == 0) return operations * UINT64_C(16);
  return UINT64_MAX;
}

int main(int argc, char **argv) {
  const char *type;
  const char *implementation;
  size_t bytes;
  size_t width;
  size_t count;
  uint64_t passes;
  uint64_t value;
  uint64_t expected;
  uint64_t begin;
  uint64_t elapsed;
  scan_pmu_scope pmu;
  scan_pmu_result pmu_result;
  int pmu_available;
  int use_avx2;
  void *mapping;

  if (argc != 5) {
    fprintf(stderr, "usage: %s type scalar|avx2 bytes passes\n", argv[0]);
    return 64;
  }
  type = argv[1];
  implementation = argv[2];
  bytes = (size_t)strtoull(argv[3], NULL, 10);
  passes = strtoull(argv[4], NULL, 10);
  width = element_width(type);
  use_avx2 = strcmp(implementation, "avx2") == 0;
  if (!width || (strcmp(implementation, "scalar") != 0 && !use_avx2) ||
      bytes < width || bytes % width != 0 || passes == 0) {
    fprintf(stderr, "bad arguments\n");
    return 64;
  }
  count = bytes / width;
  mapping = aligned_bytes(bytes);

  if (strcmp(type, "short") == 0) {
    for (size_t i = 0; i < count; ++i)
      ((uint16_t *)mapping)[i] = UINT16_C(0x0101);
  } else if (strcmp(type, "int") == 0) {
    for (size_t i = 0; i < count; ++i)
      ((uint32_t *)mapping)[i] = UINT32_C(0x01010101);
  } else if (strcmp(type, "float") == 0) {
    for (size_t i = 0; i < count; ++i)
      ((float *)mapping)[i] = (i & 1) ? 1.0f : -1.0f;
  } else if (strcmp(type, "double") == 0) {
    for (size_t i = 0; i < count; ++i)
      ((double *)mapping)[i] = (i & 1) ? 1.0 : -1.0;
  } else {
    memset(mapping, 1, bytes);
  }

  if (scan_pmu_open(&pmu) != 0) {
    fprintf(stderr, "scan_pmu_open: %s\n", strerror(errno));
    free(mapping);
    return 70;
  }
  if (scan_pmu_start(&pmu) != 0) {
    fprintf(stderr, "scan_pmu_start: %s\n", strerror(errno));
    scan_pmu_close(&pmu);
    free(mapping);
    return 70;
  }
  begin = scan_clock_now_ns();
  if (strcmp(type, "short") == 0)
    value = use_avx2 ? avx_u16((const uint16_t *)mapping, count, passes)
                     : scalar_u16((const uint16_t *)mapping, count, passes);
  else if (strcmp(type, "int") == 0)
    value = use_avx2 ? avx_u32((const uint32_t *)mapping, count, passes)
                     : scalar_u32((const uint32_t *)mapping, count, passes);
  else if (strcmp(type, "float") == 0)
    value = use_avx2 ? avx_f32((const float *)mapping, count, passes)
                     : scalar_f32((const float *)mapping, count, passes);
  else if (strcmp(type, "double") == 0)
    value = use_avx2 ? avx_f64((const double *)mapping, count, passes)
                     : scalar_f64((const double *)mapping, count, passes);
  else
    value = use_avx2 ? avx_s16((const string16 *)mapping, count, passes)
                     : scalar_s16((const string16 *)mapping, count, passes);
  elapsed = scan_clock_now_ns() - begin;
  if (scan_pmu_stop(&pmu, &pmu_result) != 0) {
    fprintf(stderr, "scan_pmu_stop: %s\n", strerror(errno));
    scan_pmu_close(&pmu);
    free(mapping);
    return 70;
  }
  pmu_available = scan_pmu_is_available(&pmu);
  scan_pmu_close(&pmu);

  expected = expected_checksum(type, count, passes);
  sink = value;
  if (value != expected || (pmu_available && (pmu_result.time_enabled == 0 ||
      pmu_result.time_running != pmu_result.time_enabled))) {
    fprintf(stderr, "invalid result: checksum=%" PRIu64 " expected=%" PRIu64
                    " time_enabled=%" PRIu64 " time_running=%" PRIu64 "\n",
            value, expected, pmu_result.time_enabled, pmu_result.time_running);
    free(mapping);
    return 65;
  }
  printf("bytes=%zu,passes=%" PRIu64 ",elapsed_ns=%" PRIu64
         ",checksum=%" PRIu64 ",expected_checksum=%" PRIu64
         ",pmu_cycles=%" PRIu64 ",pmu_instructions=%" PRIu64
         ",pmu_ref_cycles=%" PRIu64 ",pmu_time_enabled=%" PRIu64
         ",pmu_time_running=%" PRIu64 ",pmu_available=%d\n",
         bytes, passes, elapsed, value, expected, pmu_result.cycles,
         pmu_result.instructions, pmu_result.ref_cycles, pmu_result.time_enabled,
         pmu_result.time_running, pmu_available);
  free(mapping);
  return 0;
}
