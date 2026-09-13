#!/usr/bin/env bash
# Derive medians from retained MLP8 language rows; raw evidence is unchanged.
set -Eeuo pipefail

[[ $# == 1 && -f $1/samples.csv ]] || { echo "usage: $0 raw-directory" >&2; exit 64; }
raw=$1
awk -F, 'NR > 1 { print $3 "," $4 "," $7/$6 "," $10/$6 "," $11/$6 "," $12/$6 }' \
  "$raw/samples.csv" |
  sort -t, -k1,1 -k2,2n -k4,4n |
  awk -F, '
    BEGIN { print "language,footprint_bytes,repetitions,median_ns_per_operation,p10_ns_per_operation,p90_ns_per_operation,median_cycles_per_operation,p10_cycles_per_operation,p90_cycles_per_operation,median_instructions_per_operation,median_ref_cycles_per_operation" }
    function emit() {
      if (count != 15) exit 65
      printf "%s,%s,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n",
        language, footprint, count, ns[8], ns[2], ns[14], cycles[8], cycles[2],
        cycles[14], instructions[8], ref_cycles[8]
    }
    {
      if (NR == 1 || $1 != language || $2 != footprint) {
        if (NR > 1) emit()
        language = $1
        footprint = $2
        count = 0
      }
      ns[++count] = $3
      cycles[count] = $4
      instructions[count] = $5
      ref_cycles[count] = $6
    }
    END { emit() }
  ' >"$raw/summary_mlp8_language.csv"
[[ $(wc -l <"$raw/summary_mlp8_language.csv") == 29 ]] || {
  echo "wrong MLP8 language summary rows" >&2
  exit 65
}
