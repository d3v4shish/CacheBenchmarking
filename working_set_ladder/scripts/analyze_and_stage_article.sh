#!/usr/bin/env bash
# Analyze one completed DUT result and stage immutable CSV inputs for the
# article companion. This does not edit prose or create numerical claims.

set -Eeuo pipefail

if [[ $# -ne 1 || "$1" != /* ]]; then
  echo "usage: $0 /absolute/completed/result-directory" >&2
  exit 64
fi
RUN_ROOT=$1
[[ -f "$RUN_ROOT/raw/samples.csv" && -f "$RUN_ROOT/raw/counters.csv" ]] || {
  echo "missing retained raw result files" >&2; exit 66; }
[[ ! -e "$RUN_ROOT/derived" ]] || { echo "derived output already exists" >&2; exit 73; }

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
BATCH_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)
ARTICLE_DATA="$BATCH_ROOT/article/data"
if [[ -d "$ARTICLE_DATA" && -n "$(find "$ARTICLE_DATA" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
  echo "article data already exists; refuse to mix result sessions" >&2
  exit 73
fi
for command in g++ sha256sum; do command -v "$command" >/dev/null 2>&1 || { echo "missing $command" >&2; exit 69; }; done

mkdir -p -- "$RUN_ROOT/analysis-build" "$ARTICLE_DATA"
g++ -std=c++20 -O3 -Wall -Wextra -Wpedantic -Werror \
  -o "$RUN_ROOT/analysis-build/analyze_working_set" "$BATCH_ROOT/src/analyze_working_set.cpp"
"$RUN_ROOT/analysis-build/analyze_working_set" "$RUN_ROOT/raw/samples.csv" "$RUN_ROOT/raw/counters.csv" "$RUN_ROOT/derived" 250
cp -- "$RUN_ROOT/derived/visual_data.csv" "$RUN_ROOT/derived/counter_summary.csv" \
  "$RUN_ROOT/derived/quality_summary.csv" "$RUN_ROOT/derived/summary.csv" "$ARTICLE_DATA/"
sha256sum "$ARTICLE_DATA"/*.csv >"$RUN_ROOT/derived/article_input_sha256.txt"
g++ --version >"$RUN_ROOT/analysis-build/compiler_version.txt"
echo "analysis and article data staged from $RUN_ROOT"
