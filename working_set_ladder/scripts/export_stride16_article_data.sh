#!/usr/bin/env bash
# Derive publication tables from retained stride-16 v3 analyser output.
# The input is immutable DUT evidence; this script only joins accepted medians.

set -Eeuo pipefail

if [[ $# -ne 1 || "$1" != /* ]]; then
  echo "usage: $0 /absolute/results/stride16_v3" >&2
  exit 64
fi

RESULTS=$1
OUT="$RESULTS/article_data"
[[ -d "$RESULTS" ]] || { echo "missing results directory: $RESULTS" >&2; exit 66; }
[[ ! -e "$OUT" ]] || { echo "refusing to overwrite derived data: $OUT" >&2; exit 73; }
mkdir -p -- "$OUT"

prefix="$RESULTS/working-set-ladder-stride16-v3-20260909-stride16-"
gcc_scalar="$prefix"gcc13-scalar/analysis/summary.csv
gcc_auto="$prefix"gcc13-auto/analysis/summary.csv
clang_scalar="$prefix"clang18-scalar/analysis/summary.csv
clang_auto="$prefix"clang18-auto/analysis/summary.csv
raw_cache="$prefix"gcc13-scalar-raw-no-turbostat/analysis/counter_summary.csv
baseline="$prefix"gcc13-scalar/analysis/counter_summary.csv

for required in "$gcc_scalar" "$gcc_auto" "$clang_scalar" "$clang_auto" "$raw_cache" "$baseline"; do
  [[ -f "$required" ]] || { echo "missing accepted analyser output: $required" >&2; exit 66; }
done

# One row per footprint: accepted-row count, median ns/op, and median TSC
# ticks/op for the four compiler/vector-policy products.  Only warm/basic rows
# enter this main steady-state table.
awk -F, '
  FNR == 1 { file += 1; next }
  $4 == "warm" && $5 == "basic" {
    rows[file, $6] = $7; ns[file, $6] = $8; ticks[file, $6] = $11
  }
  END {
    split("8192 16384 24576 28672 32768 36864 49152 65536 98304 131072 196608 229376 262144 294912 393216 524288 1048576 2097152 4194304 5242880 6291456 7340032 8388608 12582912 33554432 134217728 536870912", footprints, " ")
    print "footprint_bytes,gcc13_scalar_rows,gcc13_scalar_ns_per_op,gcc13_scalar_tsc_ticks_per_op,gcc13_auto_rows,gcc13_auto_ns_per_op,gcc13_auto_tsc_ticks_per_op,clang18_scalar_rows,clang18_scalar_ns_per_op,clang18_scalar_tsc_ticks_per_op,clang18_auto_rows,clang18_auto_ns_per_op,clang18_auto_tsc_ticks_per_op"
    for (position = 1; position in footprints; ++position) {
      footprint = footprints[position]
      printf "%s", footprint
      for (file = 1; file <= 4; ++file) printf ",%s,%.9f,%.9f", rows[file, footprint], ns[file, footprint], ticks[file, footprint]
      print ""
    }
  }
' "$gcc_scalar" "$gcc_auto" "$clang_scalar" "$clang_auto" >"$OUT/warm_timing_matrix.csv"

# The cache diagnostics use separate exact-running raw-PMU passes.  DTLB walks
# were valid in the full scalar baseline; L1/L2/L3 are taken from the later
# no-continuous-turbostat supplement after the generic profiles proved invalid.
awk -F, '
  FILENAME == ARGV[1] && FNR == 1 { next }
  FILENAME == ARGV[2] && FNR == 1 { next }
  FILENAME == ARGV[1] && $4 == "warm" && $5 ~ /^(l1_miss|l2_miss|l3_miss)$/ && $7 != "cycles" {
    rows[$5, $6] = $8; rate[$5, $6] = $9
  }
  FILENAME == ARGV[2] && $4 == "warm" && $5 == "dtlb_walk" && $7 == "dtlb_load_misses_walk_completed" {
    rows[$5, $6] = $8; rate[$5, $6] = $9
  }
  END {
    split("8192 16384 24576 28672 32768 36864 49152 65536 98304 131072 196608 229376 262144 294912 393216 524288 1048576 2097152 4194304 5242880 6291456 7340032 8388608 12582912 33554432 134217728 536870912", footprints, " ")
    print "footprint_bytes,l1_miss_rows,l1_miss_per_op,l2_miss_rows,l2_miss_per_op,l3_miss_rows,l3_miss_per_op,dtlb_walk_rows,dtlb_walks_per_op"
    for (position = 1; position in footprints; ++position) {
      footprint = footprints[position]
      printf "%s", footprint
      for (profile_index = 1; profile_index <= 4; ++profile_index) {
        profile = (profile_index == 1 ? "l1_miss" : profile_index == 2 ? "l2_miss" : profile_index == 3 ? "l3_miss" : "dtlb_walk")
        printf ",%s,%.9f", rows[profile, footprint], rate[profile, footprint]
      }
      print ""
    }
  }
' "$raw_cache" "$baseline" >"$OUT/warm_raw_pmu_matrix.csv"

echo "derived stride-16 article data: $OUT"
