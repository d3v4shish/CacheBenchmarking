#!/usr/bin/env bash
# Paired answer: does a late one-hop PREFETCHT2 help?
set -Eeuo pipefail

[[ $# == 1 && $1 == /home/d3v/prefetch-paired-control-run-* && ! -e $1 ]] || {
  echo "usage: $0 /home/d3v/prefetch-paired-control-run-UNIQUE" >&2
  exit 64
}
run_root=$1
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cpu=4; sibling=5; repetitions=15; seed=20260910; cases=$(mktemp)
trap 'rm -f "$cases"' EXIT
[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor) == performance ]] || { echo "governor is not performance" >&2; exit 69; }
[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_min_freq) == $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_max_freq) ]] || { echo "frequency is not locked" >&2; exit 69; }
[[ $(cat /sys/devices/system/cpu/intel_pstate/no_turbo) == 1 && $(cat /proc/sys/kernel/perf_event_paranoid) -le 0 && $(cat /sys/devices/system/cpu/cpu$sibling/online) == 0 ]] || { echo "DUT gate failed" >&2; exit 69; }
for tool in gcc-13 g++-13 ar objdump; do command -v "$tool" >/dev/null || { echo "missing $tool" >&2; exit 69; }; done
mkdir -p "$run_root"/{bin,raw,manifest}
{
  date --iso-8601=seconds; uname -a; lscpu; lscpu -C
  grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq}
  grep -H . /sys/devices/system/cpu/intel_pstate/{status,no_turbo}
  cat /proc/sys/kernel/perf_event_paranoid; g++-13 --version; perf --version
} > "$run_root/manifest/pre_run_machine.txt" 2>&1
gcc-13 -std=c17 -O2 -Wall -Wextra -Werror -c "$root/src/pmu_scope.c" -o "$run_root/bin/pmu_scope.o"
ar rcs "$run_root/bin/libscanpmu.a" "$run_root/bin/pmu_scope.o"
common=(-std=c++20 -O3 -march=haswell -mtune=haswell -mavx2 -fno-tree-vectorize -fno-tree-slp-vectorize -Wall -Wextra -Werror -I"$root/src" "$root/src/prefetch_cpp.cpp" "$run_root/bin/libscanpmu.a")
g++-13 "${common[@]}" -DPREFETCH_ENABLED=1 -o "$run_root/bin/gcc13_prefetch"
g++-13 "${common[@]}" -DPREFETCH_ENABLED=0 -o "$run_root/bin/gcc13_no_prefetch"
for variant in gcc13_prefetch gcc13_no_prefetch; do
  objdump -d -Mintel --no-show-raw-insn "$run_root/bin/$variant" > "$run_root/manifest/$variant.disassembly.txt"
done

grep -qi prefetcht2 "$run_root/manifest/gcc13_prefetch.disassembly.txt" || { echo "missing prefetcht2" >&2; exit 65; }
! grep -qi prefetcht2 "$run_root/manifest/gcc13_no_prefetch.disassembly.txt" || { echo "prefetch leaked into no-prefetch binary" >&2; exit 65; }
{
  sha256sum "$root/src/prefetch_cpp.cpp" "$root/src/pmu_scope.c" "$root/src/pmu_scope.h"
  find "$run_root/bin" -maxdepth 1 -type f -print0 | sort -z | xargs -0 sha256sum
} > "$run_root/manifest/source_binary_sha256.txt"

printf 'sample_id,round,variant,footprint_bytes,passes,operations,elapsed_ns,checksum,expected_checksum,pmu_cycles,pmu_instructions,pmu_ref_cycles,pmu_time_enabled,pmu_time_running,pmu_available\n' > "$run_root/raw/samples.csv"
printf 'round,sample_id,variant,footprint_bytes,passes,target_operations,shuffle_key\n' > "$run_root/raw/run_order.csv"
for spec in '32768 100000000' '36864 100000000' '262144 40000000' '1048576 20000000' '6291456 10000000' '8388608 8000000' '536870912 4000000'; do
  read -r footprint target <<< "$spec"
  nodes=$((footprint / 64)); passes=$(((target + nodes - 1) / nodes))
  for variant in gcc13_prefetch gcc13_no_prefetch; do
    printf '%s,%s,%s,%s\n' "$variant" "$footprint" "$passes" "$target" >> "$cases"
  done
done
[[ $(wc -l < "$cases") == 14 ]] || exit 65
shuffle_cases() {
  local round=$1
  awk -F, -v seed="$seed" -v round="$round" 'BEGIN { state=(seed+round*104729)%2147483647 } { state=(state*48271)%2147483647; printf "%010d,%s\n",state,$0 }' "$cases" | sort -t, -k1,1n
}
for round in $(seq 1 "$repetitions"); do
  ordinal=0
  while IFS=, read -r shuffle_key variant footprint passes target; do
    ordinal=$((ordinal + 1))
    sample_id=$(printf 'r%02d-o%02d-%s-%s' "$round" "$ordinal" "$variant" "$footprint")
    printf '%s,%s,%s,%s,%s,%s,%s\n' "$round" "$sample_id" "$variant" "$footprint" "$passes" "$target" "$shuffle_key" >> "$run_root/raw/run_order.csv"
    output=$(taskset --cpu-list "$cpu" "$run_root/bin/$variant" "$footprint" "$passes")
    declare -A fields=()
    IFS=, read -ra terms <<< "$output"
    for term in "${terms[@]}"; do fields[${term%%=*}]=${term#*=}; done
    for name in bytes passes operations elapsed_ns checksum expected_checksum pmu_cycles pmu_instructions pmu_ref_cycles pmu_time_enabled pmu_time_running pmu_available; do
      [[ -n ${fields[$name]:-} ]] || { echo "missing $name" >&2; exit 65; }
    done
    [[ ${fields[bytes]} == "$footprint" && ${fields[passes]} == "$passes" && ${fields[elapsed_ns]} -ge 250000000 && ${fields[checksum]} == "${fields[expected_checksum]}" && ( ${fields[pmu_available]} == 1 || ${fields[pmu_available]} == true ) && ${fields[pmu_time_enabled]} == "${fields[pmu_time_running]}" && ${fields[pmu_time_enabled]} != 0 ]] || { echo "bad row $sample_id" >&2; exit 65; }
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$sample_id" "$round" "$variant" "$footprint" "$passes" "${fields[operations]}" "${fields[elapsed_ns]}" "${fields[checksum]}" "${fields[expected_checksum]}" "${fields[pmu_cycles]}" "${fields[pmu_instructions]}" "${fields[pmu_ref_cycles]}" "${fields[pmu_time_enabled]}" "${fields[pmu_time_running]}" "${fields[pmu_available]}" >> "$run_root/raw/samples.csv"
  done < <(shuffle_cases "$round")
done
awk -F, 'NR>1 { if(NF!=15 || $7<250000000 || $8!=$9 || ($15!=1 && $15!="true") || $13!=$14 || $13==0)bad=1; count[$3 FS $4]++ } END {for(g in count)if(count[g]!=15)bad=1;if(length(count)!=14 || NR!=211)bad=1;exit bad}' "$run_root/raw/samples.csv" || { echo "invalid paired prefetch matrix" >&2; exit 65; }
awk -F, 'NR>1 {print $3 "," $4 "," $7/$6 "," $10/$6 "," $11/$6 "," $12/$6}' "$run_root/raw/samples.csv" |
  sort -t, -k1,1 -k2,2n -k4,4n |
  awk -F, 'BEGIN{print "variant,footprint_bytes,repetitions,median_ns_per_operation,p10_ns_per_operation,p90_ns_per_operation,median_cycles_per_operation,p10_cycles_per_operation,p90_cycles_per_operation,median_instructions_per_operation,median_ref_cycles_per_operation"}function emit(){if(n!=15)exit 65;printf "%s,%s,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n",variant,footprint,n,ns[8],ns[2],ns[14],cycles[8],cycles[2],cycles[14],instructions[8],ref[8]}{if(NR==1||$1!=variant||$2!=footprint){if(NR>1)emit();variant=$1;footprint=$2;n=0}ns[++n]=$3;cycles[n]=$4;instructions[n]=$5;ref[n]=$6}END{emit()}' > "$run_root/raw/summary_prefetch_paired_control.csv"
[[ $(wc -l < "$run_root/raw/summary_prefetch_paired_control.csv") == 15 ]] || { echo "wrong paired summary rows" >&2; exit 65; }
grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq} > "$run_root/manifest/post_run_frequency.txt"
sha256sum "$run_root/raw"/*.csv "$run_root/manifest"/*.txt > "$run_root/manifest/result_sha256.txt"
echo "completed $run_root"
