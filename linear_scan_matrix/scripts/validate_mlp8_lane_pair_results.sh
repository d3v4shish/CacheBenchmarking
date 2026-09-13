#!/usr/bin/env bash
# Validate the single-source one-lane versus eight-lane MLP control.
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
    if ($2 < 1 || $2 > 15 || !($3 ~ /^(gcc13_lane1|gcc13_lane8)$/) ||
        $4 < 64 || $4 % 64 || $5 < 1 || $6 < 1 || $7 < 250000000 ||
        $8 != $9 || !($15 == "1" || $15 == "true") || $10 < 1 ||
        $11 < 1 || $12 < 1 || $13 < 1 || $13 != $14) bad = 1
    groups[$3 FS $4]++
  }
  END {
    for (group in groups) if (groups[group] != 15) bad = 1
    if (length(groups) != 14 || NR - 1 != 210) bad = 1
    exit bad
  }' "$raw/samples.csv" || {
  echo "invalid MLP8 lane-pair sample matrix" >&2
  exit 65
}
[[ $(awk 'END { print NR - 1 }' "$raw/run_order.csv") == 210 ]] || {
  echo "wrong MLP8 lane-pair run order" >&2
  exit 65
}
echo "MLP8 lane-pair validation passed"
