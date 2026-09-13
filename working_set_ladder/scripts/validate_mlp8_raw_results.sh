#!/usr/bin/env bash
# Independently validate the isolated MLP8 raw-event matrix.  Raw counter rows
# are deliberately not merged with basic/branch timing rows.

set -Eeuo pipefail

[[ $# == 1 && -d $1 ]] || { echo "usage: $0 /absolute/mlp8-raw-result-directory" >&2; exit 64; }
run_root=$1
samples="$run_root/raw/samples.csv"
counters="$run_root/raw/counters.csv"
manifest="$run_root/manifest/result_artifact_sha256.txt"
for required in "$samples" "$counters" "$run_root/raw/run_order.csv" "$manifest" \
  "$run_root/manifest/disassembly_with_opcodes.txt"; do
  [[ -f "$required" ]] || { echo "missing result artifact: $required" >&2; exit 66; }
done

rewritten_manifest=$(mktemp)
trap 'rm -f "$rewritten_manifest"' EXIT
sed -E "s#  /home/d3v/working-set-ladder-mlp8-raw-[^/]*/#  $run_root/#" \
  "$manifest" >"$rewritten_manifest"
sha256sum -c "$rewritten_manifest"

awk -F, '
  NR == 1 {
    if ($5 != "mode" || $6 != "implementation" || $7 != "state" ||
        $8 != "perf_profile" || $9 != "footprint_bytes" || $13 != "wall_ns" ||
        $15 != "cpu_before" || $16 != "cpu_after" || $17 != "minor_faults" ||
        $18 != "major_faults") exit 65
    next
  }
  {
    if ($5 != "mlp8_cycle" || $6 != "scalar" || $7 != "warm" ||
        !($8 ~ /^(l1_miss|l2_miss|l3_miss|dtlb_walk)$/)) bad = 1
    key = $8 FS $9
    rows[key]++
    if ($13 >= 250000000 && $15 == 4 && $16 == 4 && $17 == 0 && $18 == 0 && $20 == 0) accepted[key]++
  }
  END {
    for (key in rows) {
      groups++
      if (rows[key] < 6 || accepted[key] < 6) {
        printf "invalid raw group: %s accepted=%d total=%d\n", key, accepted[key], rows[key] > "/dev/stderr"
        bad = 1
      }
    }
    if (groups != 28 || NR - 1 < 168) bad = 1
    exit bad
  }' "$samples" || { echo "invalid MLP8 raw timing matrix" >&2; exit 65; }

awk -F, '
  NR == 1 { if ($2 != "sample_id" || $7 != "time_enabled" || $8 != "time_running") exit 65; next }
  {
    if ($7 == 0 || $7 != $8) bad = 1
    events[$2]++
  }
  END {
    for (sample in events) if (events[sample] != 2) bad = 1
    if (NR - 1 < 336) bad = 1
    exit bad
  }' "$counters" || { echo "invalid MLP8 raw PMU matrix" >&2; exit 65; }

echo "MLP8 raw validation passed: at least 6 accepted rows in every event/footprint cell"
