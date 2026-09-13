#!/usr/bin/env bash
# Retained linear-scan matrix for the i7-4702MQ Haswell DUT.
# This script does not change frequency, turbo, PMU policy, or affinity.  It
# refuses to measure unless the caller has already made the fixed-clock state
# observable and the PMU available.
set -Eeuo pipefail

if [[ $# != 1 || $1 != /home/d3v/linear-scan-matrix-run-* || -e $1 ]]; then
  echo "usage: $0 /home/d3v/linear-scan-matrix-run-UNIQUE" >&2; exit 64
fi
run_root=$1
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cpu=4
[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor) == performance ]] || { echo "governor is not performance" >&2; exit 69; }
[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_min_freq) == $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_max_freq) ]] || { echo "frequency is not locked" >&2; exit 69; }
[[ $(cat /sys/devices/system/cpu/intel_pstate/no_turbo) == 1 ]] || { echo "turbo is enabled" >&2; exit 69; }
[[ $(cat /proc/sys/kernel/perf_event_paranoid) -le 0 ]] || { echo "PMU access is restricted" >&2; exit 69; }
grep -q 'model.*: 60' /proc/cpuinfo || { echo "not the Haswell model-60 DUT" >&2; exit 69; }

mkdir -p "$run_root"/{bin,raw/perf,manifest,telemetry}
{
  date --iso-8601=seconds; uname -a; lscpu; cat /proc/sys/kernel/perf_event_paranoid
  grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq}
  grep -H . /sys/devices/system/cpu/intel_pstate/{status,no_turbo}
  gcc --version; g++ --version; rustc --version; go version; perf --version; turbostat --version
} > "$run_root/manifest/pre_run_machine.txt" 2>&1

# A scalar build has auto-vectorization disabled.  The AVX2 binary is built
# with that same ban: its SIMD comes only from visible intrinsics in source.
gcc -std=c17 -O3 -march=haswell -mtune=haswell -mavx2 -fno-tree-vectorize -fno-tree-slp-vectorize -Wall -Wextra -Werror -o "$run_root/bin/c" "$root/src/linear_scan.c"
g++ -x c++ -std=c++20 -O3 -march=haswell -mtune=haswell -mavx2 -fno-tree-vectorize -fno-tree-slp-vectorize -Wall -Wextra -Werror -o "$run_root/bin/cpp" "$root/src/linear_scan.c"
rustc -O -C target-cpu=haswell -C llvm-args=-vectorize-loops=false -o "$run_root/bin/rust" "$root/src/main.rs"
GOCACHE="$run_root/go-cache" go build -o "$run_root/bin/go" "$root/src/main.go"
sha256sum "$root/src/linear_scan.c" "$root/src/main.rs" "$root/src/main.go" "$run_root/bin"/* > "$run_root/manifest/source_binary_sha256.txt"
objdump -d -Mintel "$run_root/bin/c" > "$run_root/manifest/c_disassembly.txt"
objdump -d -Mintel "$run_root/bin/cpp" > "$run_root/manifest/cpp_disassembly.txt"

printf 'sample_id,round,language,implementation,value_type,footprint_bytes,passes,elapsed_ns,checksum\n' > "$run_root/raw/samples.csv"
printf 'sample_id,event,value,unit,time_running_pct\n' > "$run_root/raw/perf.csv"
printf 'round,language,implementation,value_type,footprint_bytes,passes\n' > "$run_root/raw/run_order.csv"
cases=$(mktemp)
trap 'rm -f "$cases"' EXIT
for language in c cpp rust go; do
  for value_type in short int float double string16; do echo "$language scalar $value_type" >> "$cases"; done
done
for language in c cpp; do
  for value_type in short int float double string16; do echo "$language avx2 $value_type" >> "$cases"; done
done

# Each sample scans at least 2 GiB.  Allocation/fill happens before the timed
# body, and this amount makes external perf's launch overhead negligible.
for round in $(seq 1 15); do
  while read -r language implementation value_type; do
    for footprint in 32768 262144 1048576 8388608 67108864; do
      passes=$(( (2147483648 + footprint - 1) / footprint ))
      sample_id="r${round}-${language}-${implementation}-${value_type}-${footprint}"
      printf '%s,%s,%s,%s,%s,%s\n' "$round" "$language" "$implementation" "$value_type" "$footprint" "$passes" >> "$run_root/raw/run_order.csv"
      perf_file="$run_root/raw/perf/${sample_id}.csv"
      result=$(taskset --cpu-list "$cpu" perf stat -x, -o "$perf_file" -e cycles,instructions,ref-cycles -- "$run_root/bin/$language" "$value_type" "$implementation" "$footprint" "$passes")
      declare -A fields=()
      IFS=, read -ra entries <<< "$result"
      for entry in "${entries[@]}"; do fields[${entry%%=*}]=${entry#*=}; done
      printf '%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$sample_id" "$round" "$language" "$implementation" "$value_type" "${fields[bytes]}" "${fields[passes]}" "${fields[elapsed_ns]}" "${fields[checksum]}" >> "$run_root/raw/samples.csv"
      awk -F, -v id="$sample_id" 'NF >= 3 { gsub(/^[[:space:]]+|[[:space:]]+$/, "", $1); gsub(/^[[:space:]]+|[[:space:]]+$/, "", $3); print id "," $3 "," $1 "," $2 "," $5 }' "$perf_file" >> "$run_root/raw/perf.csv"
    done
  done < <(shuf "$cases")
done
grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq} > "$run_root/manifest/post_run_frequency.txt"
sha256sum "$run_root/raw"/*.csv "$run_root/manifest"/*.txt > "$run_root/manifest/result_sha256.txt"
echo "completed $run_root"
