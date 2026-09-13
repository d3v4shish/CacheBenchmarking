#!/usr/bin/env bash
set -Eeuo pipefail
[[ $# == 1 && -f $1/samples.csv && -f $1/run_order.csv ]] || { echo "usage: $0 raw-directory" >&2; exit 64; }
raw=$1
awk -F, 'NR==1 {next} NF!=15 {bad=1;next} $7<250000000 || $8!=$9 || ($15!=1 && $15!="true") || $13!=$14 || $13==0 {bad=1} {count[$3 FS $4]++} END {for(language in a){} for(group in count) if(count[group]!=15) bad=1; if(NR!=421) bad=1; exit bad}' "$raw/samples.csv" || { echo "invalid arena language sample matrix" >&2; exit 65; }
[[ $(($(wc -l < "$raw/run_order.csv")-1)) == 420 ]] || { echo "invalid run order" >&2; exit 65; }
