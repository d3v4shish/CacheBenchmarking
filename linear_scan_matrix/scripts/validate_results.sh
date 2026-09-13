#!/usr/bin/env bash
# Reject incomplete, duplicate, mismatched, or multiplexed v2 retained evidence.
set -Eeuo pipefail

if [[ $# != 1 || ! -f $1/samples.csv || ! -f $1/pmu.csv || ! -f $1/run_order.csv ]]; then
  echo "usage: $0 /path/to/v2/raw" >&2
  exit 64
fi
raw=$1
expected_header='sample_id,round,variant,implementation,value_type,footprint_bytes,passes,elapsed_ns,checksum,expected_checksum,pmu_cycles,pmu_instructions,pmu_ref_cycles,pmu_time_enabled,pmu_time_running,pmu_available'
[[ $(head -n1 "$raw/samples.csv") == "$expected_header" ]] || { echo "unexpected samples header" >&2; exit 65; }
[[ $(head -n1 "$raw/pmu.csv") == 'sample_id,event,value,unit,time_enabled,time_running' ]] || { echo "unexpected PMU header" >&2; exit 65; }

awk -F, '
  NR == 1 { next }
  {
    if ($1 in ids) { print "duplicate sample_id: " $1 > "/dev/stderr"; bad=1 }
    ids[$1]=1
    if ($2 < 1 || $2 > 15 || $6 <= 0 || $7 <= 0 || $8 <= 0) { print "invalid sample fields: " $1 > "/dev/stderr"; bad=1 }
    if ($9 != $10) { print "checksum mismatch: " $1 > "/dev/stderr"; bad=1 }
    if (!(($16 == "1") || ($16 == "true"))) { print "PMU unavailable: " $1 > "/dev/stderr"; bad=1 }
    if ($11 <= 0 || $12 <= 0 || $13 <= 0 || $14 <= 0 || $14 != $15) { print "invalid or multiplexed PMU group: " $1 > "/dev/stderr"; bad=1 }
    group=$3 FS $4 FS $5 FS $6
    groups[group]++
    sample_cycles[$1]=$11; sample_instructions[$1]=$12; sample_refcycles[$1]=$13
    sample_enabled[$1]=$14; sample_running[$1]=$15
  }
  END {
    for (group in groups) if (groups[group] != 15) { print "wrong repetition count " groups[group] ": " group > "/dev/stderr"; bad=1 }
    if (NR - 1 != 4275) { print "expected 4275 samples, got " NR - 1 > "/dev/stderr"; bad=1 }
    if (length(groups) != 285) { print "expected 285 matrix groups, got " length(groups) > "/dev/stderr"; bad=1 }
    if (bad) exit 1
  }
' "$raw/samples.csv"

awk -F, '
  NR == FNR {
    if (FNR > 1) { expected[$1 FS "cycles"]=$11; expected[$1 FS "instructions"]=$12; expected[$1 FS "ref-cycles"]=$13; enabled[$1]=$14; running[$1]=$15 }
    next
  }
  FNR > 1 {
    key=$1 FS $2
    if (!(key in expected)) { print "unexpected PMU record: " key > "/dev/stderr"; bad=1 }
    if (seen[key]++) { print "duplicate PMU record: " key > "/dev/stderr"; bad=1 }
    if ($3 != expected[key] || $5 != enabled[$1] || $6 != running[$1]) { print "PMU record disagrees with sample: " key > "/dev/stderr"; bad=1 }
  }
  END {
    for (key in expected) if (!(key in seen)) { print "missing PMU record: " key > "/dev/stderr"; bad=1 }
    if (FNR - 1 != 12825) { print "expected 12825 PMU rows, got " FNR - 1 > "/dev/stderr"; bad=1 }
    if (bad) exit 1
  }
' "$raw/samples.csv" "$raw/pmu.csv"

[[ $(awk 'END { print NR - 1 }' "$raw/run_order.csv") == 4275 ]] || { echo "wrong run-order row count" >&2; exit 65; }
printf 'v2 result validation: ok (4275 samples, 12825 PMU records)\n'
