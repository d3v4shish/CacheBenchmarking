#!/usr/bin/env bash
# Validate the four compiler-policy index-cycle control independently of the
# DUT runner.  A valid matrix is 4 policies x 7 footprints x 15 repetitions.
set -Eeuo pipefail

[[ $# == 1 && -f $1/samples.csv && -f $1/run_order.csv ]] || {
  echo "usage: $0 /path/to/raw" >&2
  exit 64
}
raw=$1
expected='sample_id,round,compiler,footprint_bytes,passes,operations,elapsed_ns,checksum,expected_checksum,pmu_cycles,pmu_instructions,pmu_ref_cycles,pmu_time_enabled,pmu_time_running,pmu_available'
[[ $(head -n 1 "$raw/samples.csv") == "$expected" ]] || {
  echo "unexpected samples header" >&2
  exit 65
}
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
  echo "invalid index compiler matrix" >&2
  exit 65
}
[[ $(awk 'END { print NR - 1 }' "$raw/run_order.csv") == 420 ]] || {
  echo "wrong run-order rows" >&2
  exit 65
}
echo "index compiler validation: ok (420 samples, 28 groups)"
