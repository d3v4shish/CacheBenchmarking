#!/usr/bin/env bash
# Train one named PGO workload for one matrix row. This creates training
# artifacts only; the retained runner measures the profile-use binary later.

set -Eeuo pipefail

if [[ $# -ne 4 ]]; then
  echo "usage: $0 build_manifest.tsv pgo-config-id balanced|hot|dram /absolute/new/profile-directory" >&2
  exit 64
fi
MATRIX_PATH=$1
CONFIG_ID=$2
TRAINING=$3
PROFILE_DIR=$4
[[ "$PROFILE_DIR" == /* && "$PROFILE_DIR" != *'..'* && ! -e "$PROFILE_DIR" ]] || { echo "unsafe profile directory" >&2; exit 64; }
[[ "$TRAINING" == balanced || "$TRAINING" == hot || "$TRAINING" == dram ]] || { echo "unknown training workload" >&2; exit 64; }

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
ROW=$(awk -F '\t' -v id="$CONFIG_ID" 'NR > 1 && $1 == id { print; exit }' "$MATRIX_PATH")
[[ -n "$ROW" ]] || { echo "unknown config ID" >&2; exit 64; }
IFS=$'\t' read -r id compiler_id compiler_family compiler optimization target implementation vector_policy unroll inline lto pgo pie linker stdlib allocator availability <<<"$ROW"
[[ "$availability" == available && "$pgo" == "$TRAINING" ]] || { echo "row is not an available matching PGO configuration" >&2; exit 69; }
command -v taskset >/dev/null 2>&1 || { echo "taskset is required" >&2; exit 69; }
if [[ "$compiler_family" == clang ]]; then
  command -v llvm-profdata >/dev/null 2>&1 || { echo "llvm-profdata is required for Clang PGO" >&2; exit 69; }
fi

mkdir -p -- "$PROFILE_DIR"
WSL_PGO_GENERATE="$PROFILE_DIR/raw" "$SCRIPT_DIR/build_selected.sh" "$MATRIX_PATH" "$CONFIG_ID" "$PROFILE_DIR/instrumented"
BIN="$PROFILE_DIR/instrumented/working_set_ladder"
SCRATCH="$PROFILE_DIR/training-scratch"
mkdir -p -- "$SCRATCH"

if [[ "$TRAINING" == balanced ]]; then
  SPECS="linear_scan:32768 random_access:262144 dependent_index_cycle:1048576 mlp8_cycle:8388608"
elif [[ "$TRAINING" == hot ]]; then
  SPECS="linear_scan:8192 stride16_scan:32768 random_access:32768"
else
  SPECS="random_access:536870912 dependent_index_cycle:536870912 mlp8_cycle:536870912"
fi

for spec in $SPECS; do
  mode=${spec%%:*}
  footprint=${spec##*:}
  implementation=auto
  [[ "$mode" == dependent_index_cycle || "$mode" == mlp8_cycle ]] && implementation=scalar
  if [[ "$compiler_family" == clang ]]; then
    LLVM_PROFILE_FILE="$PROFILE_DIR/raw/$TRAINING-%m.profraw" taskset --cpu-list 4 "$BIN" \
      --mode "$mode" --implementation "$implementation" --state warm --perf-profile none \
      --build-id "$CONFIG_ID-training-$TRAINING" --footprint-bytes "$footprint" --round 0 --expected-cpu 4 \
      --min-duration-ms 250 --seed 991 --output "$SCRATCH/samples.csv" --counter-output "$SCRATCH/counters.csv"
  else
    taskset --cpu-list 4 "$BIN" --mode "$mode" --implementation "$implementation" --state warm --perf-profile none \
      --build-id "$CONFIG_ID-training-$TRAINING" --footprint-bytes "$footprint" --round 0 --expected-cpu 4 \
      --min-duration-ms 250 --seed 991 --output "$SCRATCH/samples.csv" --counter-output "$SCRATCH/counters.csv"
  fi
done

if [[ "$compiler_family" == clang ]]; then
  llvm-profdata merge --output "$PROFILE_DIR/$TRAINING.profdata" "$PROFILE_DIR/raw"/*.profraw
fi
sha256sum "$PROFILE_DIR"/raw/* "$PROFILE_DIR"/*.profdata "$PROFILE_DIR"/training-scratch/*.csv 2>/dev/null >"$PROFILE_DIR/profile_artifact_sha256.txt" || true
printf '%s\n' "$ROW" >"$PROFILE_DIR/training_build_row.tsv"
printf '%s\n' "$TRAINING" >"$PROFILE_DIR/training_workload.txt"
