#ifndef LINEAR_SCAN_PMU_SCOPE_H
#define LINEAR_SCAN_PMU_SCOPE_H

#include <stdint.h>

/*
 * A deliberately small, thread-scoped perf-event group.  The caller opens it
 * after allocation/fill, enables it immediately before the scan, and stops it
 * immediately after the scan.  It measures user-space cycles, instructions,
 * and reference cycles without process-launch or allocation work.
 */
typedef struct {
  int leader_fd;
  int member_fds[2];
} scan_pmu_scope;

typedef struct {
  uint64_t cycles;
  uint64_t instructions;
  uint64_t ref_cycles;
  uint64_t time_enabled;
  uint64_t time_running;
} scan_pmu_result;

#ifdef __cplusplus
extern "C" {
#endif

uint64_t scan_clock_now_ns(void);
int scan_pmu_open(scan_pmu_scope *scope);
int scan_pmu_is_available(const scan_pmu_scope *scope);
int scan_pmu_start(const scan_pmu_scope *scope);
int scan_pmu_stop(const scan_pmu_scope *scope, scan_pmu_result *result);
void scan_pmu_close(scan_pmu_scope *scope);

#ifdef __cplusplus
}
#endif

#endif
