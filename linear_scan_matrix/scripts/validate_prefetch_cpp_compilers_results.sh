#!/usr/bin/env bash
# Validate the compiler-policy matrix without silently accepting partial rows.
set -Eeuo pipefail

[[ $# == 1 && -f $1/samples.csv && -f $1/run_order.csv ]] || {
  echo "usage: $0 raw-directory" >&2
  exit 64
}
raw=$1

awk -F, '
  NR == 1 { next }
  NF != 15 || $7 < 250000000 || $8 != $9 || ($15 != 1 && $15 != "true") ||
    $13 != $14 || $13 == 0 { bad = 1 }
  { count[$3 FS $4]++ }
  END {
    for (group in count) if (count[group] != 15) bad = 1
    if (length(count) != 28 || NR != 421) bad = 1
    exit bad
  }
' "$raw/samples.csv" || {
  echo "invalid prefetch C++ compiler sample matrix" >&2
  exit 65
}
[[ $(($(wc -l < "$raw/run_order.csv") - 1)) == 420 ]] || {
  echo "invalid prefetch C++ compiler run order" >&2
  exit 65
}
echo "prefetch C++ compiler matrix validation passed"
