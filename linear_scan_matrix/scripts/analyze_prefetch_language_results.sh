#!/usr/bin/env bash
# Derive medians and p10/p90 from retained rows; never mutate raw evidence.
set -Eeuo pipefail
[[ $# == 1 && -f $1/samples.csv ]] || { echo "usage: $0 raw-directory" >&2; exit 64; }
raw=$1
awk -F, 'NR > 1 { print $3 "," $4 "," $7/$6 "," $10/$6 "," $11/$6 "," $12/$6 }' "$raw/samples.csv" |
  sort -t, -k1,1 -k2,2n -k4,4n |
  awk -F, '
    BEGIN { print "language,footprint_bytes,repetitions,median_ns_per_operation,p10_ns_per_operation,p90_ns_per_operation,median_cycles_per_operation,p10_cycles_per_operation,p90_cycles_per_operation,median_instructions_per_operation,median_ref_cycles_per_operation" }
    function emit() { if (n != 15) exit 65; printf "%s,%s,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n", language, footprint, n, ns[8], ns[2], ns[14], cycles[8], cycles[2], cycles[14], instructions[8], ref_cycles[8] }
    { if (NR == 1 || $1 != language || $2 != footprint) { if (NR > 1) emit(); language=$1; footprint=$2; n=0 } ns[++n]=$3; cycles[n]=$4; instructions[n]=$5; ref_cycles[n]=$6 }
    END { emit() }
  ' > "$raw/summary_prefetch_language.csv"
[[ $(wc -l < "$raw/summary_prefetch_language.csv") == 29 ]] || { echo "wrong prefetch summary rows" >&2; exit 65; }
