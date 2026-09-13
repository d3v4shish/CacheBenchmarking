#!/usr/bin/env bash
# Causal MLP control: the same C++ source is built with one or eight live
# successor lanes.  It changes latency overlap, not data layout or prefetch.
set -Eeuo pipefail

[[ $# == 1 && $1 == /home/d3v/mlp8-lane-pair-run-* && ! -e $1 ]] || {
  echo "usage: $0 /home/d3v/mlp8-lane-pair-run-UNIQUE" >&2
  exit 64
}
run_root=$1
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cpu=4
sibling=5
repetitions=15
seed=20260910
cases=$(mktemp)
trap 'rm -f "$cases"' EXIT
[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor) == performance &&
   $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_min_freq) == $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_max_freq) &&
   $(cat /sys/devices/system/cpu/intel_pstate/no_turbo) == 1 &&
   $(cat /proc/sys/kernel/perf_event_paranoid) -le 0 &&
   $(cat /sys/devices/system/cpu/cpu$sibling/online) == 0 ]] || {
  echo "DUT gate failed" >&2
  exit 69
}
for tool in gcc-13 g++-13 ar objdump; do command -v "$tool" >/dev/null || { echo "missing $tool" >&2; exit 69; }; done
mkdir -p "$run_root"/{bin,raw,manifest}
gcc-13 -std=c17 -O2 -Wall -Wextra -Werror -c "$root/src/pmu_scope.c" -o "$run_root/bin/pmu_scope.o"
ar rcs "$run_root/bin/libscanpmu.a" "$run_root/bin/pmu_scope.o"
common=(-std=c++20 -O3 -march=haswell -mtune=haswell -mavx2 -fno-tree-vectorize
  -fno-tree-slp-vectorize -Wall -Wextra -Werror -I"$root/src" "$root/src/mlp8_cpp.cpp" "$run_root/bin/libscanpmu.a")
g++-13 "${common[@]}" -DMLP_LANES=1 -o "$run_root/bin/gcc13_lane1"
g++-13 "${common[@]}" -DMLP_LANES=8 -o "$run_root/bin/gcc13_lane8"
{
  date --iso-8601=seconds
  uname -a
  lscpu
  grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq}
  cat /sys/devices/system/cpu/intel_pstate/no_turbo
  cat /proc/sys/kernel/perf_event_paranoid
  g++-13 --version
} >"$run_root/manifest/pre_run_machine.txt" 2>&1
printf '%q ' g++-13 "${common[@]}" -DMLP_LANES=1 >"$run_root/manifest/build_commands.txt"
printf '\n%q ' g++-13 "${common[@]}" -DMLP_LANES=8 >>"$run_root/manifest/build_commands.txt"
printf '\n' >>"$run_root/manifest/build_commands.txt"
for variant in gcc13_lane1 gcc13_lane8; do
  objdump -d -Mintel --no-show-raw-insn "$run_root/bin/$variant" >"$run_root/manifest/$variant.disassembly.txt"
done
{
  sha256sum "$root/src/mlp8_cpp.cpp" "$root/src/pmu_scope.c" "$root/src/pmu_scope.h"
  find "$run_root/bin" -maxdepth 1 -type f -print0 | sort -z | xargs -0 sha256sum
} >"$run_root/manifest/source_binary_sha256.txt"
printf 'mode=mlp8_cycle\npair=MLP_LANES 1 versus 8\nrepetitions=%s\nseed=%s\n' "$repetitions" "$seed" >"$run_root/manifest/run_plan.txt"
printf 'sample_id,round,variant,footprint_bytes,passes,operations,elapsed_ns,checksum,expected_checksum,pmu_cycles,pmu_instructions,pmu_ref_cycles,pmu_time_enabled,pmu_time_running,pmu_available\n' >"$run_root/raw/samples.csv"
printf 'round,sample_id,variant,footprint_bytes,passes,target_operations,shuffle_key\n' >"$run_root/raw/run_order.csv"

for spec in \
  '32768 350000000 100000000' '36864 350000000 100000000' \
  '262144 350000000 40000000' '1048576 250000000 20000000' \
  '6291456 120000000 10000000' '8388608 120000000 8000000' \
  '536870912 24000000 4000000'; do
  read -r footprint lane8_target lane1_target <<<"$spec"
  nodes=$((footprint / 64))
  lane8_passes=$(((lane8_target + nodes - 1) / nodes))
  lane1_passes=$(((lane1_target + nodes - 1) / nodes))
  printf 'gcc13_lane1,%s,%s,%s\n' "$footprint" "$lane1_passes" "$lane1_target" >>"$cases"
  printf 'gcc13_lane8,%s,%s,%s\n' "$footprint" "$lane8_passes" "$lane8_target" >>"$cases"
done
shuffle_cases() {
  local round=$1
  awk -F, -v seed="$seed" -v round="$round" 'BEGIN { state=(seed+round*104729)%2147483647 }
    { state=(state*48271)%2147483647; printf "%010d,%s\n",state,$0 }' "$cases" | sort -t, -k1,1n
}
for round in $(seq 1 "$repetitions"); do
  ordinal=0
  while IFS=, read -r shuffle_key variant footprint passes target; do
    ordinal=$((ordinal + 1))
    sample_id=$(printf 'r%02d-o%02d-%s-%s' "$round" "$ordinal" "$variant" "$footprint")
    printf '%s,%s,%s,%s,%s,%s,%s\n' "$round" "$sample_id" "$variant" "$footprint" "$passes" "$target" "$shuffle_key" >>"$run_root/raw/run_order.csv"
    output=$(taskset --cpu-list "$cpu" "$run_root/bin/$variant" "$footprint" "$passes")
    declare -A field=()
    IFS=, read -ra terms <<<"$output"
    for term in "${terms[@]}"; do key=${term%%=*}; value=${term#*=}; field[$key]=$value; done
    for name in bytes passes operations elapsed_ns checksum expected_checksum pmu_cycles pmu_instructions pmu_ref_cycles pmu_time_enabled pmu_time_running pmu_available; do
      [[ -n ${field[$name]:-} ]] || { echo "missing $name" >&2; exit 65; }
    done
    [[ ${field[bytes]} == "$footprint" && ${field[passes]} == "$passes" && ${field[elapsed_ns]} -ge 250000000 &&
       ${field[checksum]} == "${field[expected_checksum]}" && ${field[pmu_time_enabled]} == "${field[pmu_time_running]}" &&
       ${field[pmu_time_enabled]} != 0 ]] || { echo "bad row $sample_id" >&2; exit 65; }
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$sample_id" "$round" "$variant" "$footprint" "$passes" "${field[operations]}" "${field[elapsed_ns]}" "${field[checksum]}" "${field[expected_checksum]}" "${field[pmu_cycles]}" "${field[pmu_instructions]}" "${field[pmu_ref_cycles]}" "${field[pmu_time_enabled]}" "${field[pmu_time_running]}" "${field[pmu_available]}" >>"$run_root/raw/samples.csv"
  done < <(shuffle_cases "$round")
done
"$root/scripts/validate_mlp8_lane_pair_results.sh" "$run_root/raw"
"$root/scripts/analyze_mlp8_lane_pair_results.sh" "$run_root/raw"
grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq} >"$run_root/manifest/post_run_frequency.txt"
sha256sum "$run_root/raw"/*.csv "$run_root/manifest"/*.txt >"$run_root/manifest/result_sha256.txt"
echo "completed $run_root"
