#!/usr/bin/env bash
# One late-prefetch C++ source, four compiler/vectorization policies.
# This is a code-generation control: a predecessor-dependent next pointer
# leaves no independent address that a compiler could validly vectorize.
set -Eeuo pipefail

[[ $# == 1 && $1 == /home/d3v/prefetch-cpp-compilers-run-* && ! -e $1 ]] || {
  echo "usage: $0 /home/d3v/prefetch-cpp-compilers-run-UNIQUE" >&2
  exit 64
}
run_root=$1
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cpu=4; sibling=5; repetitions=15; seed=20260910; cases=$(mktemp)
trap 'rm -f "$cases"' EXIT

[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor) == performance ]] || { echo "governor is not performance" >&2; exit 69; }
[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_min_freq) == $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_max_freq) ]] || { echo "frequency is not locked" >&2; exit 69; }
[[ $(cat /sys/devices/system/cpu/intel_pstate/no_turbo) == 1 && $(cat /proc/sys/kernel/perf_event_paranoid) -le 0 && $(cat /sys/devices/system/cpu/cpu$sibling/online) == 0 ]] || { echo "DUT gate failed" >&2; exit 69; }
for tool in gcc-13 g++-13 clang++-18 ar objdump; do command -v "$tool" >/dev/null || { echo "missing $tool" >&2; exit 69; }; done

mkdir -p "$run_root"/{bin,raw,manifest}
{ date --iso-8601=seconds; uname -a; lscpu; lscpu -C; grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq}; grep -H . /sys/devices/system/cpu/intel_pstate/{status,no_turbo}; cat /proc/sys/kernel/perf_event_paranoid; g++-13 --version; clang++-18 --version; perf --version; } > "$run_root/manifest/pre_run_machine.txt" 2>&1

gcc-13 -std=c17 -O2 -Wall -Wextra -Werror -c "$root/src/pmu_scope.c" -o "$run_root/bin/pmu_scope.o"
ar rcs "$run_root/bin/libscanpmu.a" "$run_root/bin/pmu_scope.o"
common=(-std=c++20 -O3 -march=haswell -mtune=haswell -mavx2 -Wall -Wextra -Werror -I"$root/src" "$root/src/prefetch_cpp.cpp" "$run_root/bin/libscanpmu.a")
g++-13 "${common[@]}" -fno-tree-vectorize -fno-tree-slp-vectorize -o "$run_root/bin/gcc13_scalar"
g++-13 "${common[@]}" -o "$run_root/bin/gcc13_auto"
clang++-18 "${common[@]}" -fno-vectorize -fno-slp-vectorize -o "$run_root/bin/clang18_scalar"
clang++-18 "${common[@]}" -o "$run_root/bin/clang18_auto"
for compiler in gcc13_scalar gcc13_auto clang18_scalar clang18_auto; do
  objdump -d -Mintel --no-show-raw-insn "$run_root/bin/$compiler" > "$run_root/manifest/$compiler.disassembly.txt"
  grep -qi prefetcht2 "$run_root/manifest/$compiler.disassembly.txt" || { echo "missing prefetcht2 in $compiler" >&2; exit 65; }
done
{ sha256sum "$root/src/prefetch_cpp.cpp" "$root/src/pmu_scope.c" "$root/src/pmu_scope.h"; find "$run_root/bin" -maxdepth 1 -type f -print0 | sort -z | xargs -0 sha256sum; } > "$run_root/manifest/source_binary_sha256.txt"

printf 'sample_id,round,compiler,footprint_bytes,passes,operations,elapsed_ns,checksum,expected_checksum,pmu_cycles,pmu_instructions,pmu_ref_cycles,pmu_time_enabled,pmu_time_running,pmu_available\n' > "$run_root/raw/samples.csv"
printf 'round,sample_id,compiler,footprint_bytes,passes,target_operations,shuffle_key\n' > "$run_root/raw/run_order.csv"
for spec in '32768 100000000' '36864 100000000' '262144 40000000' '1048576 20000000' '6291456 10000000' '8388608 8000000' '536870912 4000000'; do
  read -r footprint target <<< "$spec"; nodes=$((footprint / 64)); passes=$(((target + nodes - 1) / nodes))
  for compiler in gcc13_scalar gcc13_auto clang18_scalar clang18_auto; do printf '%s,%s,%s,%s\n' "$compiler" "$footprint" "$passes" "$target" >> "$cases"; done
done
[[ $(wc -l < "$cases") == 28 ]] || exit 65
shuffle_cases() { local round=$1; awk -F, -v seed="$seed" -v round="$round" 'BEGIN { state=(seed+round*104729)%2147483647 } { state=(state*48271)%2147483647; printf "%010d,%s\n",state,$0 }' "$cases" | sort -t, -k1,1n; }
for round in $(seq 1 "$repetitions"); do
  ordinal=0
  while IFS=, read -r shuffle_key compiler footprint passes target; do
    ordinal=$((ordinal + 1)); sample_id=$(printf 'r%02d-o%02d-%s-%s' "$round" "$ordinal" "$compiler" "$footprint")
    printf '%s,%s,%s,%s,%s,%s,%s\n' "$round" "$sample_id" "$compiler" "$footprint" "$passes" "$target" "$shuffle_key" >> "$run_root/raw/run_order.csv"
    output=$(taskset --cpu-list "$cpu" "$run_root/bin/$compiler" "$footprint" "$passes")
    declare -A fields=(); IFS=, read -ra terms <<< "$output"; for term in "${terms[@]}"; do fields[${term%%=*}]=${term#*=}; done
    for name in bytes passes operations elapsed_ns checksum expected_checksum pmu_cycles pmu_instructions pmu_ref_cycles pmu_time_enabled pmu_time_running pmu_available; do [[ -n ${fields[$name]:-} ]] || { echo "missing $name" >&2; exit 65; }; done
    [[ ${fields[bytes]} == "$footprint" && ${fields[passes]} == "$passes" && ${fields[elapsed_ns]} -ge 250000000 && ${fields[checksum]} == "${fields[expected_checksum]}" && ( ${fields[pmu_available]} == 1 || ${fields[pmu_available]} == true ) && ${fields[pmu_time_enabled]} == "${fields[pmu_time_running]}" && ${fields[pmu_time_enabled]} != 0 ]] || { echo "bad row $sample_id" >&2; exit 65; }
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$sample_id" "$round" "$compiler" "$footprint" "$passes" "${fields[operations]}" "${fields[elapsed_ns]}" "${fields[checksum]}" "${fields[expected_checksum]}" "${fields[pmu_cycles]}" "${fields[pmu_instructions]}" "${fields[pmu_ref_cycles]}" "${fields[pmu_time_enabled]}" "${fields[pmu_time_running]}" "${fields[pmu_available]}" >> "$run_root/raw/samples.csv"
  done < <(shuffle_cases "$round")
done
"$root/scripts/validate_prefetch_cpp_compilers_results.sh" "$run_root/raw"
"$root/scripts/analyze_prefetch_cpp_compilers_results.sh" "$run_root/raw"
grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq} > "$run_root/manifest/post_run_frequency.txt"
sha256sum "$run_root/raw"/*.csv "$run_root/manifest"/*.txt > "$run_root/manifest/result_sha256.txt"
echo "completed $run_root"
