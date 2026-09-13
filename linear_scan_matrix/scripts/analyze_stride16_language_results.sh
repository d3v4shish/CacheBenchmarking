#!/usr/bin/env bash
# Derive medians without mutating retained rows. Operations are the actual
# useful one-word-per-line accesses, not the allocated-byte count.
set -Eeuo pipefail
if [[ $# != 1 || ! -f $1/samples.csv ]]; then echo "usage: $0 /path/to/raw" >&2; exit 64; fi
raw=$1
awk -F, 'NR > 1 {
  print $3 "," $4 "," $7/$6 "," $10/$6 "," $11/$6 "," $12/$6
}' "$raw/samples.csv" | sort -t, -k1,1 -k2,2n -k3,3n |
awk -F, 'BEGIN { print "language,footprint_bytes,repetitions,median_ns_per_operation,p10_ns_per_operation,p90_ns_per_operation,median_cycles_per_operation,p10_cycles_per_operation,p90_cycles_per_operation,median_instructions_per_operation,median_ref_cycles_per_operation" }
function emit() {
  if (n != 15) { print "wrong repetitions" > "/dev/stderr"; exit 65 }
  print sprintf("%s,%s,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f", lang, size, n, ns[8], ns[2], ns[14], cy[8], cy[2], cy[14], ins[8], ref[8])
}
{
  if (NR == 1 || $1 != lang || $2 != size) { if (NR > 1) emit(); lang=$1; size=$2; n=0 }
  ns[++n]=$3; cy[n]=$4; ins[n]=$5; ref[n]=$6
}
END { emit() }' > "$raw/summary_language_stride16.csv"
[[ $(wc -l < "$raw/summary_language_stride16.csv") == 29 ]] || { echo "wrong summary row count" >&2; exit 65; }
