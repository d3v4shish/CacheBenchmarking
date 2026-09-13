#!/usr/bin/env bash
# Validate the four compiler policies for the fixed eight-lane MLP shape.
set -Eeuo pipefail
[[ $# == 1 && -f $1/samples.csv && -f $1/run_order.csv ]] || {
  echo "usage: $0 raw-directory" >&2
  exit 64
}
raw=$1
awk -F, '
  NR > 1 {
    if ($1 in ids) bad = 1
    ids[$1] = 1
    if ($2 < 1 || $2 > 15 ||
        !($3 ~ /^(gcc13_scalar|gcc13_auto|clang18_scalar|clang18_auto)$/) ||
        $4 < 64 || $4 % 64 || $5 < 1 || $6 < 1 || $7 < 250000000 ||
        $8 != $9 || !($15 == "1" || $15 == "true") || $10 < 1 ||
        $11 < 1 || $12 < 1 || $13 < 1 || $13 != $14) bad = 1
    groups[$3 FS $4]++
  }
  END {
    for (group in groups) if (groups[group] != 15) bad = 1
    if (length(groups) != 28 || NR - 1 != 420) bad = 1
    exit bad
  }' "$raw/samples.csv" || {
  echo "invalid MLP8 compiler sample matrix" >&2
  exit 65
}
[[ $(awk 'END { print NR - 1 }' "$raw/run_order.csv") == 420 ]] || {
  echo "wrong MLP8 compiler run order" >&2
  exit 65
}
echo "MLP8 compiler matrix validation passed"
