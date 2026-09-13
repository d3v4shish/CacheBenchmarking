#define _GNU_SOURCE
#include "pmu_scope.h"

#include <asm/unistd.h>
#include <errno.h>
#include <linux/perf_event.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

uint64_t scan_clock_now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static int perf_event_open(struct perf_event_attr *attr, int group_fd) {
  return (int)syscall(__NR_perf_event_open, attr, 0, -1, group_fd, 0);
}

static int open_event(uint64_t config, int group_fd) {
  struct perf_event_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.type = PERF_TYPE_HARDWARE;
  attr.size = sizeof(attr);
  attr.config = config;
  attr.disabled = 1;
  attr.exclude_kernel = 1;
  attr.exclude_hv = 1;
  attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED |
                     PERF_FORMAT_TOTAL_TIME_RUNNING;
  return perf_event_open(&attr, group_fd);
}

int scan_pmu_open(scan_pmu_scope *scope) {
  if (!scope) {
    errno = EINVAL;
    return -1;
  }
  scope->leader_fd = -1;
  scope->member_fds[0] = -1;
  scope->member_fds[1] = -1;
  /* Selected external perf/profile passes disable this inner group explicitly. */
  if (getenv("SCAN_PMU_DISABLE") != NULL) {
    scope->leader_fd = -2;
    return 0;
  }

  scope->leader_fd = open_event(PERF_COUNT_HW_CPU_CYCLES, -1);
  if (scope->leader_fd < 0) return -1;
  scope->member_fds[0] =
      open_event(PERF_COUNT_HW_INSTRUCTIONS, scope->leader_fd);
  if (scope->member_fds[0] < 0) goto fail;
  scope->member_fds[1] =
      open_event(PERF_COUNT_HW_REF_CPU_CYCLES, scope->leader_fd);
  if (scope->member_fds[1] < 0) goto fail;
  return 0;

fail:
  scan_pmu_close(scope);
  return -1;
}

int scan_pmu_is_available(const scan_pmu_scope *scope) {
  return scope != NULL && scope->leader_fd >= 0;
}

int scan_pmu_start(const scan_pmu_scope *scope) {
  if (!scope) {
    errno = EINVAL;
    return -1;
  }
  if (scope->leader_fd == -2) return 0;
  if (scope->leader_fd < 0) { errno = EINVAL; return -1; }
  if (ioctl(scope->leader_fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) != 0)
    return -1;
  return ioctl(scope->leader_fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
}

int scan_pmu_stop(const scan_pmu_scope *scope, scan_pmu_result *result) {
  /* nr, time_enabled, time_running, then one value for each group member. */
  uint64_t values[6];
  ssize_t got;
  if (!scope || !result) {
    errno = EINVAL;
    return -1;
  }
  if (scope->leader_fd == -2) {
    memset(result, 0, sizeof(*result));
    return 0;
  }
  if (scope->leader_fd < 0) { errno = EINVAL; return -1; }
  if (ioctl(scope->leader_fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) != 0)
    return -1;
  got = read(scope->leader_fd, values, sizeof(values));
  if (got != (ssize_t)sizeof(values) || values[0] != 3) {
    if (got >= 0) errno = EIO;
    return -1;
  }
  result->time_enabled = values[1];
  result->time_running = values[2];
  result->cycles = values[3];
  result->instructions = values[4];
  result->ref_cycles = values[5];
  return 0;
}

void scan_pmu_close(scan_pmu_scope *scope) {
  if (!scope) return;
  if (scope->member_fds[1] >= 0) close(scope->member_fds[1]);
  if (scope->member_fds[0] >= 0) close(scope->member_fds[0]);
  if (scope->leader_fd >= 0) close(scope->leader_fd);
  scope->leader_fd = -1;
  scope->member_fds[0] = -1;
  scope->member_fds[1] = -1;
}
