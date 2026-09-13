#!/usr/bin/env bash
# Validate the exact 4-language × 7-footprint × 15-repetition retained matrix.
set -Eeuo pipefail
if [[ $# != 1 || ! -f $1/samples.csv || ! -f $1/run_order.csv ]]; then
  echo "usage: $0 /path/to/raw" >&2; exit 64
fi
raw=$1
expected='sample_id,round,language,footprint_bytes,passes,operations,elapsed_ns,checksum,expected_checksum,pmu_cycles,pmu_instructions,pmu_ref_cycles,pmu_time_enabled,pmu_time_running,pmu_available'
[[ $(head -n1 "$raw/samples.csv") == "$expected" ]] || { echo "unexpected samples header" >&2; exit 65; }
awk -F, '
  NR == 1 { next }
  {
    if ($1 in ids) { print "duplicate sample " $1 > "/dev/stderr"; bad=1 }; ids[$1]=1
    if ($2 < 1 || $2 > 15 || !($3 ~ /^(c|cpp|rust|go)$/) || $4 < 64 || $4 % 64 || $5 < 1 || $6 < 1 || $7 < 1) { print "invalid row " $1 > "/dev/stderr"; bad=1 }
    if ($8 != $9 || !($15 == "1" || $15 == "true") || $10 < 1 || $11 < 1 || $12 < 1 || $13 < 1 || $13 != $14) { print "bad checksum or PMU row " $1 > "/dev/stderr"; bad=1 }
    groups[$3 FS $4]++
  }
  END {
    for (group in groups) if (groups[group] != 15) { print "wrong repetitions " groups[group] " for " group > "/dev/stderr"; bad=1 }
    if (length(groups) != 28 || NR - 1 != 420) { print "wrong matrix dimensions" > "/dev/stderr"; bad=1 }
    if (bad) exit 1
  }
' "$raw/samples.csv"
[[ $(awk 'END { print NR - 1 }' "$raw/run_order.csv") == 420 ]] || { echo "wrong run order rows" >&2; exit 65; }
echo "stride16 language validation: ok (420 samples, 28 groups)"
