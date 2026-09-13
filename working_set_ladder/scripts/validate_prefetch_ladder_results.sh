#!/usr/bin/env bash
# Validate the retained prefetch-chain scalar ladder without averaging invalid
# rows into a result.  It accepts extra attempts only when every named cell
# has at least fifteen fault-free, pinned, exact-PMU samples.

set -Eeuo pipefail

if [[ $# -ne 1 || ! -d "$1" ]]; then
  echo "usage: $0 /absolute/prefetch-result-directory" >&2
  exit 64
fi

run_root=$1
samples="$run_root/raw/samples.csv"
counters="$run_root/raw/counters.csv"
manifest="$run_root/manifest/result_artifact_sha256.txt"
for required in "$samples" "$counters" "$run_root/raw/run_order.csv" "$manifest" \
  "$run_root/manifest/disassembly_with_opcodes.txt"; do
  [[ -f "$required" ]] || { echo "missing result artifact: $required" >&2; exit 66; }
done

# DUT manifests retain their absolute creation path.  Rewrite only that known
# prefetch-result prefix for a copied local artifact before checksum checking;
# do not alter the retained manifest itself.
rewritten_manifest=$(mktemp)
trap 'rm -f "$rewritten_manifest"' EXIT
sed -E "s#  /home/d3v/working-set-ladder-prefetch[^/]*/#  $run_root/#" \
  "$manifest" >"$rewritten_manifest"
sha256sum -c "$rewritten_manifest"
grep -q 'prefetcht2' "$run_root/manifest/disassembly_with_opcodes.txt" || {
  echo "selected binary does not retain prefetcht2" >&2
  exit 65
}

awk -F, '
  NR == 1 {
    if ($1 != "schema_version" || $5 != "mode" || $6 != "implementation" ||
        $7 != "state" || $8 != "perf_profile" || $9 != "footprint_bytes" ||
        $13 != "wall_ns" || $15 != "cpu_before" || $16 != "cpu_after" ||
        $17 != "minor_faults" || $18 != "major_faults" || $20 != "first_touch_exception") exit 65
    next
  }
  {
    if ($5 != "prefetch_chain" || $6 != "scalar" || !($7 == "warm" || $7 == "cache_evicted") ||
        !($8 == "basic" || $8 == "branch")) bad = 1
    key = $7 FS $8 FS $9
    rows[key]++
    if ($13 >= 250000000 && $15 == 4 && $16 == 4 && $17 == 0 && $18 == 0 && $20 == 0) accepted[key]++
  }
  END {
    for (key in rows) {
      groups++
      if (accepted[key] < 15) {
        printf "insufficient accepted rows: %s accepted=%d total=%d\n", key, accepted[key], rows[key] > "/dev/stderr"
        bad = 1
      }
    }
    if (groups != 68) bad = 1
    exit bad
  }' "$samples" || { echo "invalid prefetch timing matrix" >&2; exit 65; }

awk -F, '
  NR == 1 { if ($2 != "sample_id" || $7 != "time_enabled" || $8 != "time_running") exit 65; next }
  {
    if ($7 == 0 || $7 != $8) bad = 1
    events[$2]++
  }
  END {
    for (sample in events) if (events[sample] != 3) bad = 1
    exit bad
  }' "$counters" || { echo "invalid prefetch PMU matrix" >&2; exit 65; }

echo "prefetch-chain ladder validation passed: at least 15 accepted rows in every cell"
