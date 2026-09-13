#!/usr/bin/env bash
# Guarded DUT runner for micro_working_set_ladder. It creates evidence only
# after verifying the documented controls; it never changes machine state.

set -Eeuo pipefail

if [[ $# -lt 2 || $# -gt 8 ]]; then
  echo "usage: $0 /absolute/new/result-directory /absolute/build-directory [--only-mode MODE] [--only-implementation scalar|auto|avx2] [--only-profiles comma-separated-profiles]" >&2
  exit 64
fi
RUN_ROOT=$1
BUILD_ROOT=$2
shift 2
ONLY_MODE=
ONLY_IMPLEMENTATION=
ONLY_PROFILES=
while [[ $# -gt 0 ]]; do
  case "$1" in
    --only-mode)
      [[ $# -ge 2 ]] || { echo "missing mode after --only-mode" >&2; exit 64; }
      ONLY_MODE=$2
      shift 2
      ;;
    --only-implementation)
      [[ $# -ge 2 ]] || { echo "missing implementation after --only-implementation" >&2; exit 64; }
      ONLY_IMPLEMENTATION=$2
      shift 2
      ;;
    --only-profiles)
      [[ $# -ge 2 ]] || { echo "missing profiles after --only-profiles" >&2; exit 64; }
      ONLY_PROFILES=$2
      shift 2
      ;;
    *)
      echo "unknown runner option: $1" >&2
      exit 64
      ;;
  esac
done
if [[ "$RUN_ROOT" != /home/d3v/working-set-ladder-* || "$RUN_ROOT" == *'..'* || -e "$RUN_ROOT" ]]; then
  echo "refusing unsafe or existing result directory: $RUN_ROOT" >&2
  exit 64
fi
if [[ ! -x "$BUILD_ROOT/working_set_ladder" || ! -f "$BUILD_ROOT/manifest/build_row.tsv" ]]; then
  echo "selected build is incomplete" >&2
  exit 66
fi

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
BATCH_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)
BIN="$BUILD_ROOT/working_set_ladder"
CPU=4
SMT_SIBLING=5
OTHER_PHYSICAL=6
# A selected retest may request a few extra attempts when the analyzer must
# reject sporadic timed faults.  Results retain all rows; acceptance remains a
# separate, explicit decision rather than silently replacing a sample.
REPETITIONS=${WSL_REPETITIONS:-15}
[[ "$REPETITIONS" =~ ^[1-9][0-9]*$ ]] || { echo "WSL_REPETITIONS must be a positive integer" >&2; exit 64; }
MIN_DURATION_MS=250
FOOTPRINTS="8192 16384 24576 28672 32768 36864 49152 65536 98304 131072 196608 229376 262144 294912 393216 524288 1048576 2097152 4194304 5242880 6291456 7340032 8388608 12582912 33554432 134217728 536870912"
ALL_MODES="linear_scan stride16_scan random_access arena_pointer_chase dependent_index_cycle prefetch_chain mlp8_cycle tlb_page_walk"
# Generic cache/TLB aliases remain source-level negative controls. The Haswell
# retest uses one DUT-advertised raw diagnostic event per profile instead.
ALL_COUNTER_PROFILES="basic branch l1_miss l2_miss l3_miss dtlb_walk"
COUNTER_PROFILES=$ALL_COUNTER_PROFILES

if [[ -n "$ONLY_MODE" ]]; then
  mode_known=0
  for known_mode in $ALL_MODES; do [[ "$known_mode" == "$ONLY_MODE" ]] && mode_known=1; done
  [[ $mode_known -eq 1 ]] || { echo "unknown selected mode: $ONLY_MODE" >&2; exit 64; }
fi
if [[ -n "$ONLY_IMPLEMENTATION" ]]; then
  [[ "$ONLY_IMPLEMENTATION" == scalar || "$ONLY_IMPLEMENTATION" == auto || "$ONLY_IMPLEMENTATION" == avx2 ]] || {
    echo "unknown selected implementation: $ONLY_IMPLEMENTATION" >&2; exit 64;
  }
fi
if [[ -n "$ONLY_PROFILES" ]]; then
  COUNTER_PROFILES=${ONLY_PROFILES//,/ }
  for selected_profile in $COUNTER_PROFILES; do
    profile_known=0
    for known_profile in $ALL_COUNTER_PROFILES; do [[ "$known_profile" == "$selected_profile" ]] && profile_known=1; done
    [[ $profile_known -eq 1 ]] || { echo "unknown selected profile: $selected_profile" >&2; exit 64; }
  done
fi

for command in awk cat date grep lscpu objdump perf ps sha256sum shuf taskset timeout turbostat; do
  command -v "$command" >/dev/null 2>&1 || { echo "required command missing: $command" >&2; exit 69; }
done
[[ -r "/sys/devices/system/cpu/cpu$CPU/topology/thread_siblings_list" ]] || { echo "benchmark CPU unavailable" >&2; exit 69; }
[[ $(cat "/sys/devices/system/cpu/cpu$CPU/topology/thread_siblings_list") == *"$SMT_SIBLING"* ]] || { echo "unexpected SMT topology" >&2; exit 69; }
[[ $(cat "/sys/devices/system/cpu/cpu$CPU/topology/core_id") != $(cat "/sys/devices/system/cpu/cpu$OTHER_PHYSICAL/topology/core_id") ]] || { echo "CPU 6 is not another core" >&2; exit 69; }
[[ $(cat "/sys/devices/system/cpu/cpu$CPU/cpufreq/scaling_governor") == performance ]] || { echo "governor is not performance" >&2; exit 69; }
if [[ -r /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
  [[ $(cat /sys/devices/system/cpu/intel_pstate/no_turbo) == 1 ]] || { echo "turbo is enabled" >&2; exit 69; }
elif [[ -r /sys/devices/system/cpu/cpufreq/boost ]]; then
  [[ $(cat /sys/devices/system/cpu/cpufreq/boost) == 0 ]] || { echo "boost is enabled" >&2; exit 69; }
else
  echo "cannot verify turbo state" >&2; exit 69
fi
[[ $(cat /proc/sys/kernel/perf_event_paranoid) -le 0 ]] || { echo "PMU access is restricted" >&2; exit 69; }

mkdir -p -- "$RUN_ROOT/raw" "$RUN_ROOT/manifest" "$RUN_ROOT/telemetry" "$RUN_ROOT/sidecars" "$RUN_ROOT/article-input"
TURBOSTAT_PID=
CASES=
cleanup() {
  [[ -n "$CASES" ]] && rm -f "$CASES" || true
  [[ -n "$TURBOSTAT_PID" ]] && kill "$TURBOSTAT_PID" 2>/dev/null || true
}
trap cleanup EXIT

{
  date --iso-8601=seconds
  uname -a
  lscpu
  cat /proc/cmdline
  cat /proc/sys/kernel/perf_event_paranoid
  grep -H . "/sys/devices/system/cpu/cpu$CPU/cpufreq/"{scaling_governor,scaling_cur_freq,scaling_min_freq,scaling_max_freq}
  cat "/sys/devices/system/cpu/cpu$CPU/topology/thread_siblings_list"
  "$BIN" --help
  perf --version
  turbostat --version
} >"$RUN_ROOT/manifest/pre_run_machine.txt"
printf 'only_mode=%s\nonly_implementation=%s\nonly_profiles=%s\nrepetitions=%s\nminimum_duration_ms=%s\n' \
  "${ONLY_MODE:-all}" "${ONLY_IMPLEMENTATION:-all-meaningful}" "$COUNTER_PROFILES" "$REPETITIONS" "$MIN_DURATION_MS" \
  >"$RUN_ROOT/manifest/run_plan.txt"
perf list --details >"$RUN_ROOT/manifest/perf_list_details.txt" 2>&1 || true
ps -eo psr=,pid=,comm= | awk -v cpu="$SMT_SIBLING" '$1 == cpu { print }' >"$RUN_ROOT/manifest/cpu$SMT_SIBLING"_tasks_before.txt
cp -a -- "$BUILD_ROOT/manifest" "$RUN_ROOT/manifest/build"
sha256sum "$BIN" "$BATCH_ROOT/src/working_set_ladder.cpp" >"$RUN_ROOT/manifest/binary_and_source_sha256.txt"
objdump -d -Mintel "$BIN" >"$RUN_ROOT/manifest/disassembly_with_opcodes.txt"
timeout 8s turbostat --quiet --interval 1 --num_iterations 1 >"$RUN_ROOT/telemetry/turbostat_preflight.txt"
# On Haswell, a continuous turbostat process can occupy the constrained PMC
# needed by MEM_LOAD_UOPS_RETIRED (event 0xd1).  Keep generic all-mode
# telemetry unchanged, but do not let it invalidate a deliberately isolated
# selected-mode PMU run. The preflight above remains as environment evidence.
if [[ -z "$ONLY_MODE" ]]; then
  turbostat --quiet --interval 1 >"$RUN_ROOT/telemetry/turbostat.csv" 2>&1 &
  TURBOSTAT_PID=$!
else
  printf '%s\n' 'continuous turbostat disabled: selected raw-PMU mode avoids PMC contention' \
    >"$RUN_ROOT/telemetry/turbostat.csv"
fi

# build_selected.sh stores exactly the selected matrix row (without a header),
# so take its first non-empty field rather than assuming a second TSV line.
BUILD_ID=$(awk -F '\t' 'NF { print $1; exit }' "$BUILD_ROOT/manifest/build_row.tsv")
[[ -n "$BUILD_ID" ]] || { echo "build has no config ID" >&2; exit 66; }
SAMPLES="$RUN_ROOT/raw/samples.csv"
COUNTERS="$RUN_ROOT/raw/counters.csv"
ORDER="$RUN_ROOT/raw/run_order.csv"
printf 'round,mode,implementation,state,perf_profile,footprint_bytes,seed\n' >"$ORDER"

implementations_for_mode() {
  local mode=$1
  local implementations="scalar auto"
  [[ "$mode" == tlb_page_walk ]] && implementations="scalar"
  [[ "$mode" == linear_scan ]] && implementations="scalar auto avx2"
  if [[ -n "$ONLY_IMPLEMENTATION" ]]; then
    for implementation in $implementations; do
      [[ "$implementation" == "$ONLY_IMPLEMENTATION" ]] && { printf '%s\n' "$implementation"; return; }
    done
    echo "implementation $ONLY_IMPLEMENTATION is not meaningful for mode $mode" >&2
    exit 64
  fi
  printf '%s\n' "$implementations"
}

append_case() {
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" "$5" "$6"
}
run_cases() {
  local round=$1
  while IFS=$'\t' read -r mode implementation state profile footprint seed; do
    printf '%s,%s,%s,%s,%s,%s,%s\n' "$round" "$mode" "$implementation" "$state" "$profile" "$footprint" "$seed" >>"$ORDER"
    taskset --cpu-list "$CPU" "$BIN" --mode "$mode" --implementation "$implementation" --state "$state" --perf-profile "$profile" \
      --build-id "$BUILD_ID" --footprint-bytes "$footprint" --round "$round" --expected-cpu "$CPU" \
      --min-duration-ms "$MIN_DURATION_MS" --seed "$seed" --output "$SAMPLES" --counter-output "$COUNTERS"
  done < <(shuf "$CASES")
}

for ((round = 0; round < REPETITIONS; ++round)); do
  CASES=$(mktemp)
  for footprint in $FOOTPRINTS; do
    for mode in ${ONLY_MODE:-$ALL_MODES}; do
      implementations=$(implementations_for_mode "$mode")
      for implementation in $implementations; do
        for profile in $COUNTER_PROFILES; do append_case "$mode" "$implementation" warm "$profile" "$footprint" "$((0x5eed + round))" >>"$CASES"; done
      done
    done
  done
  for footprint in 32768 262144 1048576 6291456 8388608 33554432 536870912; do
    for mode in ${ONLY_MODE:-$ALL_MODES}; do
      implementations=$(implementations_for_mode "$mode")
      for implementation in $implementations; do
        for profile in $COUNTER_PROFILES; do append_case "$mode" "$implementation" cache_evicted "$profile" "$footprint" "$((0x6eed + round))" >>"$CASES"; done
      done
    done
  done
  for footprint in 33554432 536870912; do
    for mode in linear_scan random_access; do
      # page_cold is meaningful only for direct data modes.  A selected mode
      # must never inherit these legacy all-mode cases.
      [[ -z "$ONLY_MODE" || "$mode" == "$ONLY_MODE" ]] || continue
      implementations=$(implementations_for_mode "$mode")
      for implementation in $implementations; do
        for profile in $COUNTER_PROFILES; do append_case "$mode" "$implementation" page_cold "$profile" "$footprint" "$((0x7eed + round))" >>"$CASES"; done
      done
    done
  done
  run_cases "$round"
  rm -f "$CASES"
  CASES=
done

# Separate perf metric passes remain sidecars; their scratch benchmark CSV
# cannot be merged with generic-event rows or represented as simultaneous PMU.
if [[ -z "$ONLY_MODE" ]]; then
  for metric in TopdownL1 Frontend Backend MemoryBound PortsUtil; do
    mkdir -p -- "$RUN_ROOT/sidecars/$metric"
    perf stat --metric-only -M "$metric" -o "$RUN_ROOT/sidecars/$metric/perf_stat.txt" -- \
      taskset --cpu-list "$CPU" "$BIN" --mode dependent_index_cycle --implementation scalar --state warm --perf-profile none \
        --build-id "$BUILD_ID-sidecar-$metric" --footprint-bytes 6291456 --round 0 --expected-cpu "$CPU" \
        --min-duration-ms "$MIN_DURATION_MS" --seed 123 --output "$RUN_ROOT/sidecars/$metric/samples.csv" \
        --counter-output "$RUN_ROOT/sidecars/$metric/counters.csv"
  done
fi

ps -eo psr=,pid=,comm= | awk -v cpu="$SMT_SIBLING" '$1 == cpu { print }' >"$RUN_ROOT/manifest/cpu$SMT_SIBLING"_tasks_after.txt
if [[ -n "$TURBOSTAT_PID" ]]; then
  kill "$TURBOSTAT_PID"
  wait "$TURBOSTAT_PID" || true
  TURBOSTAT_PID=
fi
sha256sum "$SAMPLES" "$COUNTERS" "$ORDER" "$RUN_ROOT/manifest"/*.txt >"$RUN_ROOT/manifest/result_artifact_sha256.txt"
cp "$SAMPLES" "$COUNTERS" "$ORDER" "$RUN_ROOT/article-input/"
echo "working-set-ladder run completed: $RUN_ROOT"
