#!/usr/bin/env bash
# Compile exactly one C++ index-cycle source under GCC 13 and Clang 18,
# scalar-disabled and default-vector policy.  The load-to-use dependency means
# SIMD cannot parallelize the chase; this is a surrounding-codegen control.
set -Eeuo pipefail

[[ $# == 1 && $1 == /home/d3v/index-cpp-compilers-run-* && ! -e $1 ]] || {
  echo "usage: $0 /home/d3v/index-cpp-compilers-run-UNIQUE" >&2
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

[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor) == performance &&
   $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_min_freq) == $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_max_freq) &&
   $(cat /sys/devices/system/cpu/intel_pstate/no_turbo) == 1 &&
   $(cat /proc/sys/kernel/perf_event_paranoid) -le 0 &&
   $(cat /sys/devices/system/cpu/cpu$sibling/online) == 0 ]] || {
  echo "DUT gate failed" >&2
  exit 69
}
mkdir -p "$run_root"/{bin,raw,manifest}
cc=gcc-13
cxx=g++-13
clang=clang++-18
for tool in "$cc" "$cxx" "$clang" ar objdump; do
  command -v "$tool" >/dev/null || { echo "missing $tool" >&2; exit 69; }
done

"$cc" -std=c17 -O2 -Wall -Wextra -Werror -c "$root/src/pmu_scope.c" -o "$run_root/bin/pmu_scope.o"
ar rcs "$run_root/bin/libscanpmu.a" "$run_root/bin/pmu_scope.o"
common=(-std=c++20 -O3 -march=haswell -mtune=haswell -mavx2 -Wall -Wextra -Werror -I"$root/src" "$root/src/index_cpp.cpp" "$run_root/bin/libscanpmu.a")
"$cxx" "${common[@]}" -fno-tree-vectorize -fno-tree-slp-vectorize -o "$run_root/bin/gcc13_scalar"
"$cxx" "${common[@]}" -o "$run_root/bin/gcc13_auto"
"$clang" "${common[@]}" -fno-vectorize -fno-slp-vectorize -o "$run_root/bin/clang18_scalar"
"$clang" "${common[@]}" -o "$run_root/bin/clang18_auto"

{
  date --iso-8601=seconds
  uname -a
  lscpu
  grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq}
  cat /sys/devices/system/cpu/intel_pstate/no_turbo
  cat /proc/sys/kernel/perf_event_paranoid
  "$cxx" --version
  "$clang" --version
} > "$run_root/manifest/pre_run_machine.txt" 2>&1
for compiler in gcc13_scalar gcc13_auto clang18_scalar clang18_auto; do
  objdump -d -Mintel --no-show-raw-insn "$run_root/bin/$compiler" > "$run_root/manifest/$compiler.disassembly.txt"
done
{
  sha256sum "$root/src/index_cpp.cpp" "$root/src/pmu_scope.c" "$root/src/pmu_scope.h" "$0"
  find "$run_root/bin" -maxdepth 1 -type f -print0 | sort -z | xargs -0 sha256sum
} > "$run_root/manifest/source_binary_sha256.txt"

printf 'sample_id,round,compiler,footprint_bytes,passes,operations,elapsed_ns,checksum,expected_checksum,pmu_cycles,pmu_instructions,pmu_ref_cycles,pmu_time_enabled,pmu_time_running,pmu_available\n' > "$run_root/raw/samples.csv"
printf 'round,sample_id,compiler,footprint_bytes,passes,target_operations,shuffle_key\n' > "$run_root/raw/run_order.csv"
# Keep the same conservative whole-cycle budgets as the language matrix so a
# short, otherwise-valid interval cannot become retained evidence.
for spec in '32768 300000000' '36864 200000000' '262144 80000000' '1048576 40000000' '6291456 20000000' '8388608 16000000' '536870912 8000000'; do
  read -r footprint target <<< "$spec"
  entries=$((footprint / 4))
  passes=$(((target + entries - 1) / entries))
  for compiler in gcc13_scalar gcc13_auto clang18_scalar clang18_auto; do
    printf '%s,%s,%s,%s\n' "$compiler" "$footprint" "$passes" "$target" >> "$cases"
  done
done
[[ $(wc -l < "$cases") == 28 ]] || exit 65
shuffle_cases() {
  local round=$1
  awk -F, -v seed="$seed" -v round="$round" 'BEGIN { state=(seed+round*104729)%2147483647 } { state=(state*48271)%2147483647; printf "%010d,%s\n",state,$0 }' "$cases" | sort -t, -k1,1n
}
for round in $(seq 1 "$repetitions"); do
  ordinal=0
  while IFS=, read -r shuffle_key compiler footprint passes target; do
    ordinal=$((ordinal + 1))
    sample_id=$(printf 'r%02d-o%02d-%s-%s' "$round" "$ordinal" "$compiler" "$footprint")
    printf '%s,%s,%s,%s,%s,%s,%s\n' "$round" "$sample_id" "$compiler" "$footprint" "$passes" "$target" "$shuffle_key" >> "$run_root/raw/run_order.csv"
    output=$(taskset --cpu-list "$cpu" "$run_root/bin/$compiler" "$footprint" "$passes")
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
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$sample_id" "$round" "$compiler" "$footprint" "$passes" "${field[operations]}" "${field[elapsed_ns]}" "${field[checksum]}" "${field[expected_checksum]}" "${field[pmu_cycles]}" "${field[pmu_instructions]}" "${field[pmu_ref_cycles]}" "${field[pmu_time_enabled]}" "${field[pmu_time_running]}" "${field[pmu_available]}" >> "$run_root/raw/samples.csv"
  done < <(shuffle_cases "$round")
done
awk -F, 'NR > 1 { print $3 "," $4 "," $7/$6 "," $10/$6 "," $11/$6 "," $12/$6 }' "$run_root/raw/samples.csv" |
  sort -t, -k1,1 -k2,2n -k3,3n |
  awk -F, 'BEGIN { print "compiler,footprint_bytes,repetitions,median_ns_per_operation,p10_ns_per_operation,p90_ns_per_operation,median_cycles_per_operation,p10_cycles_per_operation,p90_cycles_per_operation,median_instructions_per_operation,median_ref_cycles_per_operation" } function emit() { if (n != 15) exit 65; printf "%s,%s,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n", compiler, footprint, n, ns[8], ns[2], ns[14], cycles[8], cycles[2], cycles[14], instructions[8], ref[8] } { if (NR == 1 || $1 != compiler || $2 != footprint) { if (NR > 1) emit(); compiler=$1; footprint=$2; n=0 } ns[++n]=$3; cycles[n]=$4; instructions[n]=$5; ref[n]=$6 } END { emit() }' > "$run_root/raw/summary_index_cpp_compilers.csv"
[[ $(wc -l < "$run_root/raw/summary_index_cpp_compilers.csv") == 29 ]] || exit 65
grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq} > "$run_root/manifest/post_run_frequency.txt"
sha256sum "$run_root/raw"/*.csv "$run_root/manifest"/*.txt > "$run_root/manifest/result_sha256.txt"
echo "completed $run_root"
