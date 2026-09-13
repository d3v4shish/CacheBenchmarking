#!/usr/bin/env bash
# Reject incomplete MLP8 capacity rows rather than averaging timed faults.
set -Eeuo pipefail

[[ $# == 1 && -d $1 ]] || { echo "usage: $0 result-directory" >&2; exit 64; }
run_root=$1
samples="$run_root/raw/samples.csv"
counters="$run_root/raw/counters.csv"
manifest="$run_root/manifest/result_artifact_sha256.txt"
for file in "$samples" "$counters" "$run_root/raw/run_order.csv" "$manifest" "$run_root/manifest/disassembly_with_opcodes.txt"; do
  [[ -f "$file" ]] || { echo "missing result artifact: $file" >&2; exit 66; }
done

rewritten=$(mktemp)
trap 'rm -f "$rewritten"' EXIT
sed -E "s#  /home/d3v/working-set-ladder-mlp8[^/]*/#  $run_root/#" "$manifest" >"$rewritten"
sha256sum -c "$rewritten"
# GCC may inline the MLP8 member into main, so a named run_mlp8 symbol is not
# a valid acceptance condition.  The retained source/binary hashes and the
# sample matrix's explicit mode field bind this artifact to the MLP8 shape.

awk -F, '
  NR == 1 { next }
  {
    if ($5 != "mlp8_cycle" || $6 != "scalar" ||
        !($7 == "warm" || $7 == "cache_evicted") ||
        !($8 == "basic" || $8 == "branch")) bad = 1
    key = $7 FS $8 FS $9
    rows[key]++
    if ($13 >= 250000000 && $15 == 4 && $16 == 4 &&
        $17 == 0 && $18 == 0 && $20 == 0) accepted[key]++
  }
  END {
    for (key in rows) {
      groups++
      if (accepted[key] < 15) {
        printf "insufficient accepted rows: %s accepted=%d total=%d\n",
          key, accepted[key], rows[key] > "/dev/stderr"
        bad = 1
      }
    }
    if (groups != 68) bad = 1
    exit bad
  }' "$samples" || { echo "invalid MLP8 timing matrix" >&2; exit 65; }

awk -F, '
  NR == 1 { next }
  { if ($7 == 0 || $7 != $8) bad = 1; events[$2]++ }
  END { for (sample in events) if (events[sample] != 3) bad = 1; exit bad }
' "$counters" || { echo "invalid MLP8 PMU matrix" >&2; exit 65; }
echo "MLP8 ladder validation passed: at least 15 accepted rows in every cell"
