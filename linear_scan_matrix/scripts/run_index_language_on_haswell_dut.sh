#!/usr/bin/env bash
# Run the same deterministic dependent uint32_t-index cycle from C, C++,
# Rust, and Go.  Each accepted row has a 250-ms-or-longer exact PMU interval.
set -Eeuo pipefail

[[ $# == 1 && $1 == /home/d3v/index-language-run-* && ! -e $1 ]] || {
  echo "usage: $0 /home/d3v/index-language-run-UNIQUE" >&2
  exit 64
}
run_root=$1
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cpu=4
sibling=5
repetitions=15
seed=20260909
cases=$(mktemp)
trap 'rm -f "$cases"' EXIT

[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor) == performance ]] || { echo "governor is not performance" >&2; exit 69; }
[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_min_freq) == $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_max_freq) ]] || { echo "frequency is not locked" >&2; exit 69; }
[[ $(cat /sys/devices/system/cpu/intel_pstate/no_turbo) == 1 ]] || { echo "turbo is enabled" >&2; exit 69; }
[[ $(cat /proc/sys/kernel/perf_event_paranoid) -le 0 ]] || { echo "PMU access is restricted" >&2; exit 69; }
grep -q 'model.*: 60' /proc/cpuinfo || { echo "not the Haswell model-60 DUT" >&2; exit 69; }
[[ $(cat /sys/devices/system/cpu/cpu$sibling/online) == 0 ]] || { echo "SMT sibling CPU $sibling must be offline" >&2; exit 69; }

mkdir -p "$run_root"/{bin,raw,manifest}
{
  date --iso-8601=seconds
  uname -a
  lscpu
  lscpu -C
  grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq}
  grep -H . /sys/devices/system/cpu/intel_pstate/{status,no_turbo}
  cat /proc/sys/kernel/perf_event_paranoid
  gcc --version
  g++ --version
  rustc --version
  go version
  perf --version
} > "$run_root/manifest/pre_run_machine.txt" 2>&1
make -C "$root" BUILD_DIR="$run_root/bin" index_language
for language in c cpp rust go; do
  objdump -d -Mintel --no-show-raw-insn "$run_root/bin/index_$language" > "$run_root/manifest/index_$language.disassembly.txt"
done
{
  sha256sum "$root"/src/index_{c.c,cpp.cpp,rust.rs,go.go} "$root"/src/pmu_scope.c "$root"/src/pmu_scope.h "$root"/Makefile "$0"
  find "$run_root/bin" -maxdepth 1 -type f -print0 | sort -z | xargs -0 sha256sum
} > "$run_root/manifest/source_binary_sha256.txt"

printf 'sample_id,round,language,footprint_bytes,passes,operations,elapsed_ns,checksum,expected_checksum,pmu_cycles,pmu_instructions,pmu_ref_cycles,pmu_time_enabled,pmu_time_running,pmu_available\n' > "$run_root/raw/samples.csv"
printf 'round,sample_id,language,footprint_bytes,passes,target_operations,shuffle_key\n' > "$run_root/raw/run_order.csv"
# The budgets deliberately include a large margin over the short first-pass
# intervals.  A row always follows a whole closed cycle; only full traversals
# are added, so checksum semantics and access shape do not change.
for spec in '32768 300000000' '36864 200000000' '262144 80000000' '1048576 40000000' '6291456 20000000' '8388608 16000000' '536870912 8000000'; do
  read -r footprint target <<< "$spec"
  entries=$((footprint / 4))
  passes=$(((target + entries - 1) / entries))
  for language in c cpp rust go; do
    printf '%s,%s,%s,%s\n' "$language" "$footprint" "$passes" "$target" >> "$cases"
  done
done
[[ $(wc -l < "$cases") == 28 ]] || exit 65
shuffle_cases() {
  local round=$1
  awk -F, -v seed="$seed" -v round="$round" 'BEGIN { state=(seed+round*104729)%2147483647 } { state=(state*48271)%2147483647; printf "%010d,%s\n",state,$0 }' "$cases" | sort -t, -k1,1n
}
for round in $(seq 1 "$repetitions"); do
  ordinal=0
  while IFS=, read -r shuffle_key language footprint passes target; do
    ordinal=$((ordinal + 1))
    sample_id=$(printf 'r%02d-o%02d-%s-%s' "$round" "$ordinal" "$language" "$footprint")
    printf '%s,%s,%s,%s,%s,%s,%s\n' "$round" "$sample_id" "$language" "$footprint" "$passes" "$target" "$shuffle_key" >> "$run_root/raw/run_order.csv"
    output=$(taskset --cpu-list "$cpu" "$run_root/bin/index_$language" "$footprint" "$passes")
    declare -A field=()
    IFS=, read -ra terms <<< "$output"
    for term in "${terms[@]}"; do field[${term%%=*}]=${term#*=}; done
    for name in bytes passes operations elapsed_ns checksum expected_checksum pmu_cycles pmu_instructions pmu_ref_cycles pmu_time_enabled pmu_time_running pmu_available; do
      [[ -n ${field[$name]:-} ]] || { echo "missing $name" >&2; exit 65; }
    done
    [[ ${field[bytes]} == "$footprint" && ${field[passes]} == "$passes" &&
       ${field[elapsed_ns]} -ge 250000000 && ${field[checksum]} == "${field[expected_checksum]}" &&
       ( ${field[pmu_available]} == 1 || ${field[pmu_available]} == true ) &&
       ${field[pmu_time_enabled]} == "${field[pmu_time_running]}" && ${field[pmu_time_enabled]} != 0 ]] || {
      echo "bad row $sample_id" >&2
      exit 65
    }
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$sample_id" "$round" "$language" "$footprint" "$passes" "${field[operations]}" "${field[elapsed_ns]}" "${field[checksum]}" "${field[expected_checksum]}" "${field[pmu_cycles]}" "${field[pmu_instructions]}" "${field[pmu_ref_cycles]}" "${field[pmu_time_enabled]}" "${field[pmu_time_running]}" "${field[pmu_available]}" >> "$run_root/raw/samples.csv"
  done < <(shuffle_cases "$round")
done
"$root/scripts/validate_index_language_results.sh" "$run_root/raw"
"$root/scripts/analyze_index_language_results.sh" "$run_root/raw"
grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq} > "$run_root/manifest/post_run_frequency.txt"
sha256sum "$run_root/raw"/*.csv "$run_root/manifest"/*.txt > "$run_root/manifest/result_sha256.txt"
echo "completed $run_root"
