#!/usr/bin/env bash
# Retained v2 matrix. It never overwrites a run directory or changes CPU state.
set -Eeuo pipefail

if [[ $# != 1 || $1 != /home/d3v/linear-scan-matrix-run-v2-* || -e $1 ]]; then
  echo "usage: $0 /home/d3v/linear-scan-matrix-run-v2-UNIQUE" >&2
  exit 64
fi
run_root=$1
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cpu=4
sibling=5
seed=${LINEAR_SCAN_SEED:-20260909}
repetitions=15
work_bytes=2147483648
telemetry_pid=
cases=$(mktemp)

cleanup() {
  rm -f "$cases"
  if [[ -n ${telemetry_pid:-} ]]; then kill "$telemetry_pid" 2>/dev/null || true; fi
}
trap cleanup EXIT

[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor) == performance ]] || { echo "governor is not performance" >&2; exit 69; }
[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_min_freq) == $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_max_freq) ]] || { echo "frequency is not locked" >&2; exit 69; }
[[ $(cat /sys/devices/system/cpu/intel_pstate/no_turbo) == 1 ]] || { echo "turbo is enabled" >&2; exit 69; }
[[ $(cat /proc/sys/kernel/perf_event_paranoid) -le 0 ]] || { echo "PMU access is restricted" >&2; exit 69; }
grep -q 'model.*: 60' /proc/cpuinfo || { echo "not the Haswell model-60 DUT" >&2; exit 69; }
[[ $(cat /sys/devices/system/cpu/cpu$sibling/online) == 0 ]] || { echo "SMT sibling CPU $sibling must be offline" >&2; exit 69; }
command -v clang++ >/dev/null || { echo "clang++ is required for the v2 compiler matrix" >&2; exit 69; }

mkdir -p "$run_root"/{bin,raw,manifest,telemetry,diagnostics/events,diagnostics/profile}
{
  date --iso-8601=seconds; uname -a; lscpu; lscpu -C; lscpu -e
  cat /proc/sys/kernel/perf_event_paranoid
  grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq}
  grep -H . /sys/devices/system/cpu/intel_pstate/{status,no_turbo}
  cat /sys/devices/system/cpu/cpu$sibling/online
  gcc --version; g++ --version; clang++ --version; rustc --version; go version; perf --version; turbostat --version
} > "$run_root/manifest/pre_run_machine.txt" 2>&1
cat /proc/interrupts > "$run_root/telemetry/interrupts_before.txt"
cat /proc/stat > "$run_root/telemetry/proc_stat_before.txt"
perf list > "$run_root/manifest/perf_list.txt" 2>&1 || true

# Each artifact is built in the unique run directory. No executable is shared
# with a prior run, and the source/binary hash below identifies what ran.
make -C "$root" BUILD_DIR="$run_root/bin" build clangpp
for binary in c cpp clangpp cpp_native rust go cpp_unroll rust_unroll; do
  objdump -d -Mintel --no-show-raw-insn "$run_root/bin/$binary" > "$run_root/manifest/$binary.disassembly.txt"
done
{
  sha256sum "$root"/src/* "$root"/Makefile
  find "$run_root/bin" -maxdepth 1 -type f -print0 | sort -z | xargs -0 sha256sum
} > "$run_root/manifest/source_binary_sha256.txt"

if command -v turbostat >/dev/null; then
  (turbostat --quiet --interval 1 > "$run_root/telemetry/turbostat.txt" 2>&1) &
  telemetry_pid=$!
fi

printf 'sample_id,round,variant,implementation,value_type,footprint_bytes,passes,elapsed_ns,checksum,expected_checksum,pmu_cycles,pmu_instructions,pmu_ref_cycles,pmu_time_enabled,pmu_time_running,pmu_available\n' > "$run_root/raw/samples.csv"
printf 'sample_id,event,value,unit,time_enabled,time_running\n' > "$run_root/raw/pmu.csv"
printf 'round,sample_id,variant,implementation,value_type,footprint_bytes,passes,shuffle_key\n' > "$run_root/raw/run_order.csv"

add_case() {
  local variant=$1 implementation=$2 type=$3 footprint=$4
  local passes=$(( (work_bytes + footprint - 1) / footprint ))
  printf '%s,%s,%s,%s,%s\n' "$variant" "$implementation" "$type" "$footprint" "$passes" >> "$cases"
}

for type in short int float double string16; do
  for variant in c cpp; do
    for implementation in scalar avx2; do
      for footprint in 32768 262144 1048576 8388608 67108864; do add_case "$variant" "$implementation" "$type" "$footprint"; done
    done
  done
  for variant in cpp_native clangpp go; do
    for footprint in 32768 262144 1048576 8388608 67108864; do add_case "$variant" scalar "$type" "$footprint"; done
  done
  for implementation in scalar avx2; do
    for footprint in 32768 262144 1048576 8388608 67108864; do add_case rust "$implementation" "$type" "$footprint"; done
  done
done
for variant in cpp_unroll rust_unroll; do
  for type in float double; do
    for implementation in u1 u2 u8; do
      for footprint in 32768 262144 1048576 8388608 67108864; do add_case "$variant" "$implementation" "$type" "$footprint"; done
    done
  done
done
[[ $(wc -l < "$cases") == 285 ]] || { echo "wrong v2 case count" >&2; exit 65; }

# Portable deterministic shuffle: the key uses integers below awk's exact
# range, and the round/seed pair is retained in the manifest.
shuffle_cases() {
  local round=$1
  awk -F, -v seed="$seed" -v round="$round" '
    BEGIN { state=(seed + round * 104729) % 2147483647 }
    { state=(state * 48271) % 2147483647; printf "%010d,%s\n", state, $0 }
  ' "$cases" | sort -t, -k1,1n
}

for round in $(seq 1 "$repetitions"); do
  ordinal=0
  while IFS=, read -r shuffle_key variant implementation type footprint passes; do
    ordinal=$((ordinal + 1))
    sample_id=$(printf 'r%02d-o%03d-%s-%s-%s-%s' "$round" "$ordinal" "$variant" "$implementation" "$type" "$footprint")
    printf '%s,%s,%s,%s,%s,%s,%s,%s\n' "$round" "$sample_id" "$variant" "$implementation" "$type" "$footprint" "$passes" "$shuffle_key" >> "$run_root/raw/run_order.csv"
    result=$(taskset --cpu-list "$cpu" "$run_root/bin/$variant" "$type" "$implementation" "$footprint" "$passes")
    declare -A field=()
    IFS=, read -ra terms <<< "$result"
    for term in "${terms[@]}"; do field[${term%%=*}]=${term#*=}; done
    for name in bytes passes elapsed_ns checksum expected_checksum pmu_cycles pmu_instructions pmu_ref_cycles pmu_time_enabled pmu_time_running pmu_available; do
      [[ -n ${field[$name]:-} ]] || { echo "missing $name from $sample_id" >&2; exit 65; }
    done
    [[ ${field[bytes]} == "$footprint" && ${field[passes]} == "$passes" ]] || { echo "output contract mismatch: $sample_id" >&2; exit 65; }
    [[ ${field[checksum]} == "${field[expected_checksum]}" ]] || { echo "checksum mismatch: $sample_id" >&2; exit 65; }
    [[ ${field[pmu_available]} == 1 || ${field[pmu_available]} == true ]] || { echo "PMU unavailable: $sample_id" >&2; exit 65; }
    [[ ${field[pmu_time_enabled]} == "${field[pmu_time_running]}" && ${field[pmu_time_enabled]} != 0 ]] || { echo "multiplexed PMU group: $sample_id" >&2; exit 65; }
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$sample_id" "$round" "$variant" "$implementation" "$type" "$footprint" "$passes" "${field[elapsed_ns]}" "${field[checksum]}" "${field[expected_checksum]}" "${field[pmu_cycles]}" "${field[pmu_instructions]}" "${field[pmu_ref_cycles]}" "${field[pmu_time_enabled]}" "${field[pmu_time_running]}" "${field[pmu_available]}" >> "$run_root/raw/samples.csv"
    for event in cycles instructions ref-cycles; do
      value=${field[pmu_${event//-/_}]}
      printf '%s,%s,%s,count,%s,%s\n' "$sample_id" "$event" "$value" "${field[pmu_time_enabled]}" "${field[pmu_time_running]}" >> "$run_root/raw/pmu.csv"
    done
  done < <(shuffle_cases "$round")
done

"$root/scripts/validate_results.sh" "$run_root/raw"
"$root/scripts/analyze_results.sh" "$run_root/raw"

# Diagnostics run after retained primary data. They explicitly turn off the
# in-process group so perf gets an uncontended, separately labelled event set.
run_event_set() {
  local label=$1 events=$2 variant=$3 implementation=$4 type=$5
  local output="$run_root/diagnostics/events/${label}-${variant}-${implementation}-${type}.csv"
  local stdout="$run_root/diagnostics/events/${label}-${variant}-${implementation}-${type}.stdout"
  if env SCAN_PMU_DISABLE=1 taskset --cpu-list "$cpu" perf stat -x, -o "$output" -e "$events" -- "$run_root/bin/$variant" "$type" "$implementation" 67108864 32 > "$stdout" 2>&1; then
    printf 'ok,%s,%s,%s,%s,%s\n' "$label" "$events" "$variant" "$implementation" "$type" >> "$run_root/diagnostics/event_status.csv"
  else
    printf 'unsupported-or-failed,%s,%s,%s,%s,%s\n' "$label" "$events" "$variant" "$implementation" "$type" >> "$run_root/diagnostics/event_status.csv"
  fi
}
printf 'status,label,events,variant,implementation,value_type\n' > "$run_root/diagnostics/event_status.csv"
for diagnostic in \
  'core:cycles,instructions,ref-cycles' \
  'l1d:L1-dcache-loads,L1-dcache-load-misses' \
  'llc:LLC-loads,LLC-load-misses' \
  'dtlb:dTLB-loads,dTLB-load-misses' \
  'branch:branches,branch-misses' \
  'scheduler:context-switches,cpu-migrations,page-faults,minor-faults,major-faults'; do
  label=${diagnostic%%:*}; events=${diagnostic#*:}
  run_event_set "$label" "$events" cpp scalar float
  run_event_set "$label" "$events" rust scalar float
  run_event_set "$label" "$events" cpp avx2 int
done

for profile_case in 'cpp scalar float' 'rust scalar float' 'cpp avx2 int'; do
  read -r variant implementation type <<< "$profile_case"
  data="$run_root/diagnostics/profile/${variant}-${implementation}-${type}.data"
  if env SCAN_PMU_DISABLE=1 taskset --cpu-list "$cpu" perf record -q -o "$data" -e cycles:u -c 100000 --call-graph dwarf -- "$run_root/bin/$variant" "$type" "$implementation" 67108864 32 > "${data}.stdout" 2>&1; then
    perf report --stdio -i "$data" > "${data}.report.txt" 2>&1 || true
    perf annotate --stdio -i "$data" > "${data}.annotate.txt" 2>&1 || true
  fi
done

grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq} > "$run_root/manifest/post_run_frequency.txt"
cat /proc/interrupts > "$run_root/telemetry/interrupts_after.txt"
cat /proc/stat > "$run_root/telemetry/proc_stat_after.txt"
sha256sum "$run_root/raw"/*.csv "$run_root/manifest"/*.txt > "$run_root/manifest/result_sha256.txt"
echo "completed $run_root"
