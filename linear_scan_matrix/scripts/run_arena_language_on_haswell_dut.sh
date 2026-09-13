#!/usr/bin/env bash
# Four native-language implementations of one strict, cache-line-sized
# pointer chain.  This script is deliberately adaptive by footprint: it keeps
# each row just above the 250-ms quality floor instead of spending minutes at
# DRAM merely to repeat the same latency measurement.
set -Eeuo pipefail
[[ $# == 1 && $1 == /home/d3v/arena-language-run-* && ! -e $1 ]] || { echo "usage: $0 /home/d3v/arena-language-run-UNIQUE" >&2; exit 64; }
run_root=$1; root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd); cpu=4; sibling=5; repetitions=15; seed=20260909; cases=$(mktemp); trap 'rm -f "$cases"' EXIT
[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor) == performance ]] || { echo "governor is not performance" >&2; exit 69; }
[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_min_freq) == $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_max_freq) ]] || { echo "frequency is not locked" >&2; exit 69; }
[[ $(cat /sys/devices/system/cpu/intel_pstate/no_turbo) == 1 ]] || { echo "turbo is enabled" >&2; exit 69; }
[[ $(cat /proc/sys/kernel/perf_event_paranoid) -le 0 ]] || { echo "PMU access is restricted" >&2; exit 69; }
grep -q 'model.*: 60' /proc/cpuinfo || { echo "not the Haswell model-60 DUT" >&2; exit 69; }
[[ $(cat /sys/devices/system/cpu/cpu$sibling/online) == 0 ]] || { echo "SMT sibling CPU $sibling must be offline" >&2; exit 69; }
mkdir -p "$run_root"/{bin,raw,manifest}
{ date --iso-8601=seconds; uname -a; lscpu; lscpu -C; grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq}; grep -H . /sys/devices/system/cpu/intel_pstate/{status,no_turbo}; cat /proc/sys/kernel/perf_event_paranoid; gcc --version; g++ --version; rustc --version; go version; perf --version; } > "$run_root/manifest/pre_run_machine.txt" 2>&1
make -C "$root" BUILD_DIR="$run_root/bin" arena_language
for language in c cpp rust go; do objdump -d -Mintel --no-show-raw-insn "$run_root/bin/arena_$language" > "$run_root/manifest/arena_$language.disassembly.txt"; done
{ sha256sum "$root"/src/arena_{c.c,cpp.cpp,rust.rs,go.go} "$root"/src/pmu_scope.c "$root"/src/pmu_scope.h "$root"/Makefile; find "$run_root/bin" -maxdepth 1 -type f -print0 | sort -z | xargs -0 sha256sum; } > "$run_root/manifest/source_binary_sha256.txt"
printf 'sample_id,round,language,footprint_bytes,passes,operations,elapsed_ns,checksum,expected_checksum,pmu_cycles,pmu_instructions,pmu_ref_cycles,pmu_time_enabled,pmu_time_running,pmu_available\n' > "$run_root/raw/samples.csv"
printf 'round,sample_id,language,footprint_bytes,passes,target_operations,shuffle_key\n' > "$run_root/raw/run_order.csv"
# Operations are reduced at long-latency footprints only.  This changes the
# duration, never the operation: a row remains a full traversal of the same
# closed cycle and each accepted interval is at least 250 ms.
for spec in '32768 100000000' '36864 100000000' '262144 40000000' '1048576 20000000' '6291456 10000000' '8388608 8000000' '536870912 4000000'; do
  read -r footprint target <<< "$spec"; nodes=$((footprint/64)); passes=$(((target+nodes-1)/nodes));
  for language in c cpp rust go; do printf '%s,%s,%s,%s\n' "$language" "$footprint" "$passes" "$target" >> "$cases"; done
done
[[ $(wc -l < "$cases") == 28 ]] || exit 65
shuffle_cases(){ local round=$1; awk -F, -v seed="$seed" -v round="$round" 'BEGIN{state=(seed+round*104729)%2147483647}{state=(state*48271)%2147483647;printf "%010d,%s\n",state,$0}' "$cases" | sort -t, -k1,1n; }
for round in $(seq 1 "$repetitions"); do
  ordinal=0
  while IFS=, read -r shuffle_key language footprint passes target; do
    ordinal=$((ordinal+1)); sample_id=$(printf 'r%02d-o%02d-%s-%s' "$round" "$ordinal" "$language" "$footprint")
    printf '%s,%s,%s,%s,%s,%s,%s\n' "$round" "$sample_id" "$language" "$footprint" "$passes" "$target" "$shuffle_key" >> "$run_root/raw/run_order.csv"
    output=$(taskset --cpu-list "$cpu" "$run_root/bin/arena_$language" "$footprint" "$passes")
    declare -A f=(); IFS=, read -ra terms <<< "$output"; for term in "${terms[@]}"; do f[${term%%=*}]=${term#*=}; done
    for name in bytes passes operations elapsed_ns checksum expected_checksum pmu_cycles pmu_instructions pmu_ref_cycles pmu_time_enabled pmu_time_running pmu_available; do [[ -n ${f[$name]:-} ]] || { echo "missing $name" >&2; exit 65; }; done
    [[ ${f[bytes]} == "$footprint" && ${f[passes]} == "$passes" && ${f[elapsed_ns]} -ge 250000000 && ${f[checksum]} == "${f[expected_checksum]}" && ( ${f[pmu_available]} == 1 || ${f[pmu_available]} == true ) && ${f[pmu_time_enabled]} == "${f[pmu_time_running]}" && ${f[pmu_time_enabled]} != 0 ]] || { echo "bad row $sample_id" >&2; exit 65; }
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$sample_id" "$round" "$language" "$footprint" "$passes" "${f[operations]}" "${f[elapsed_ns]}" "${f[checksum]}" "${f[expected_checksum]}" "${f[pmu_cycles]}" "${f[pmu_instructions]}" "${f[pmu_ref_cycles]}" "${f[pmu_time_enabled]}" "${f[pmu_time_running]}" "${f[pmu_available]}" >> "$run_root/raw/samples.csv"
  done < <(shuffle_cases "$round")
done
"$root/scripts/validate_arena_language_results.sh" "$run_root/raw"
"$root/scripts/analyze_arena_language_results.sh" "$run_root/raw"
grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq} > "$run_root/manifest/post_run_frequency.txt"
sha256sum "$run_root/raw"/*.csv "$run_root/manifest"/*.txt > "$run_root/manifest/result_sha256.txt"
echo "completed $run_root"
