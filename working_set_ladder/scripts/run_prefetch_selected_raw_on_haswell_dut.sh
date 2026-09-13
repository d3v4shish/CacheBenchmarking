#!/usr/bin/env bash
# Retain prefetch-chain raw cache/TLB passes separately from basic/branch
# timing. Each profile owns cycles plus exactly one Haswell raw diagnostic.

set -Eeuo pipefail

[[ $# == 2 && $1 == /home/d3v/working-set-ladder-prefetch-raw-* && -x $2/working_set_ladder ]] || {
  echo "usage: $0 /home/d3v/working-set-ladder-prefetch-raw-UNIQUE /absolute/build-dir" >&2
  exit 64
}

run_root=$1
build_root=$2
bin=$build_root/working_set_ladder
cpu=4
repetitions=${WSL_RAW_REPETITIONS:-6}
[[ "$repetitions" =~ ^[6-9][0-9]*$ ]] || { echo "WSL_RAW_REPETITIONS must be an integer of at least 6" >&2; exit 64; }
seed=20260910
cases=$(mktemp)
trap 'rm -f "$cases"' EXIT

[[ ! -e $run_root ]] || { echo "result directory exists" >&2; exit 73; }
[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor) == performance && \
   $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_min_freq) == \
   $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_max_freq) && \
   $(cat /sys/devices/system/cpu/intel_pstate/no_turbo) == 1 && \
   $(cat /proc/sys/kernel/perf_event_paranoid) -le 0 ]] || {
  echo "DUT gate failed" >&2
  exit 69
}

mkdir -p "$run_root"/{raw,manifest,telemetry}
{
  date --iso-8601=seconds
  uname -a
  lscpu
  grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq}
  cat /sys/devices/system/cpu/intel_pstate/no_turbo
  cat /proc/sys/kernel/perf_event_paranoid
  perf list --details
} >"$run_root/manifest/pre_run_machine.txt" 2>&1
sha256sum "$bin" >"$run_root/manifest/binary_sha256.txt"
objdump -d -Mintel "$bin" >"$run_root/manifest/disassembly_with_opcodes.txt"
printf '%s\n' 'continuous turbostat disabled: selected raw-PMU run avoids constrained-counter contention' \
  >"$run_root/telemetry/turbostat.csv"
printf 'mode=prefetch_chain\nrepetitions=%s\nminimum_accepted_rows=6\n' "$repetitions" \
  >"$run_root/manifest/run_plan.txt"

for footprint in 32768 36864 262144 1048576 6291456 8388608 536870912; do
  for profile in l1_miss l2_miss l3_miss dtlb_walk; do
    printf '%s,%s\n' "$profile" "$footprint" >>"$cases"
  done
done

samples="$run_root/raw/samples.csv"
counters="$run_root/raw/counters.csv"
printf 'round,profile,footprint_bytes,shuffle_key\n' >"$run_root/raw/run_order.csv"

shuffle_cases() {
  local round=$1
  awk -F, -v seed="$seed" -v round="$round" \
    'BEGIN { state=(seed+round*104729)%2147483647 }
     { state=(state*48271)%2147483647; printf "%010d,%s\n",state,$0 }' "$cases" |
    sort -t, -k1,1n
}

for round in $(seq 1 "$repetitions"); do
  while IFS=, read -r key profile footprint; do
    printf '%s,%s,%s,%s\n' "$round" "$profile" "$footprint" "$key" >>"$run_root/raw/run_order.csv"
    taskset --cpu-list "$cpu" "$bin" --mode prefetch_chain --implementation scalar --state warm \
      --perf-profile "$profile" --build-id prefetch-chain-gcc13-raw \
      --footprint-bytes "$footprint" --round "$round" --expected-cpu "$cpu" \
      --min-duration-ms 250 --seed "$seed" --output "$samples" --counter-output "$counters"
  done < <(shuffle_cases "$round")
done

awk -F, -v repetitions="$repetitions" '
  NR > 1 {
    if ($5 != "prefetch_chain" || $6 != "scalar" || $7 != "warm" ||
        !($8 ~ /^(l1_miss|l2_miss|l3_miss|dtlb_walk)$/)) bad = 1
    else if ($13 >= 250000000 && $15 == 4 && $16 == 4 && $17 == 0 &&
             $18 == 0 && $20 == 0) valid[$8 FS $9]++
    else rejected++
  }
  END {
    for (group in valid) if (valid[group] < 6) bad = 1
    if (length(valid) != 28 || NR - 1 != 28 * repetitions) bad = 1
    exit bad
  }' "$samples" || { echo "invalid raw sample matrix" >&2; exit 65; }
awk -F, -v repetitions="$repetitions" '
  NR > 1 { if ($7 == 0 || $7 != $8) bad = 1; groups[$4 FS $2 FS $5]++ }
  END { for (group in groups) if (groups[group] != 1) bad = 1; if (NR - 1 != 56 * repetitions) bad = 1; exit bad }' \
  "$counters" || { echo "invalid raw PMU rows" >&2; exit 65; }

sha256sum "$run_root/raw"/*.csv "$run_root/manifest"/*.txt >"$run_root/manifest/result_sha256.txt"
echo "completed $run_root"
