#!/usr/bin/env bash
# Derive medians from retained rows; a logical operation is one payload load.
set -Eeuo pipefail
[[ $# == 1 && -f $1/samples.csv ]] || { echo "usage: $0 /path/to/raw" >&2; exit 64; }
raw=$1
awk -F, 'NR>1 { print $3 "," $4 "," $7/$6 "," $10/$6 "," $11/$6 "," $12/$6 }' "$raw/samples.csv" | sort -t, -k1,1 -k2,2n -k3,3n | awk -F, 'BEGIN { print "language,footprint_bytes,repetitions,median_ns_per_operation,p10_ns_per_operation,p90_ns_per_operation,median_cycles_per_operation,p10_cycles_per_operation,p90_cycles_per_operation,median_instructions_per_operation,median_ref_cycles_per_operation" } function emit(){if(n!=15)exit 65; printf "%s,%s,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n",lang,size,n,ns[8],ns[2],ns[14],cy[8],cy[2],cy[14],ins[8],ref[8]} {if(NR==1||$1!=lang||$2!=size){if(NR>1)emit();lang=$1;size=$2;n=0} ns[++n]=$3;cy[n]=$4;ins[n]=$5;ref[n]=$6} END{emit()}' > "$raw/summary_language_random.csv"
[[ $(wc -l < "$raw/summary_language_random.csv") == 29 ]] || { echo "wrong summary rows" >&2; exit 65; }
