#!/usr/bin/env bash
# Small machine-checkable complement to the human readability review.

set -Eeuo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
BATCH_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)

for required in \
  "$BATCH_ROOT/README.md" \
  "$BATCH_ROOT/src/working_set_ladder.cpp" \
  "$BATCH_ROOT/src/analyze_working_set.cpp" \
  "$BATCH_ROOT/scripts/generate_build_matrix.sh" \
  "$BATCH_ROOT/scripts/generate_stride16_selected_matrix.sh" \
  "$BATCH_ROOT/scripts/generate_prefetch_selected_matrix.sh" \
  "$BATCH_ROOT/scripts/generate_mlp8_selected_matrix.sh" \
  "$BATCH_ROOT/scripts/export_stride16_article_data.sh" \
  "$BATCH_ROOT/scripts/build_selected.sh" \
  "$BATCH_ROOT/scripts/train_pgo.sh" \
  "$BATCH_ROOT/scripts/run_working_set_on_dut.sh" \
  "$BATCH_ROOT/scripts/run_prefetch_selected_raw_on_haswell_dut.sh" \
  "$BATCH_ROOT/scripts/run_mlp8_selected_raw_on_haswell_dut.sh" \
  "$BATCH_ROOT/scripts/validate_prefetch_ladder_results.sh" \
  "$BATCH_ROOT/scripts/validate_mlp8_ladder_results.sh" \
  "$BATCH_ROOT/scripts/validate_prefetch_raw_results.sh" \
  "$BATCH_ROOT/scripts/validate_mlp8_raw_results.sh" \
  "$BATCH_ROOT/scripts/analyze_and_stage_article.sh" \
  "$BATCH_ROOT/article/WORKING_SET_LADDER_ARTICLE.md" \
  "$BATCH_ROOT/article/working_set_visuals.html"; do
  [[ -f "$required" ]] || { echo "missing required artifact: $required" >&2; exit 66; }
done

for phrase in "Decision question" "Method sources" "time_enabled" "page-cold" "checksum"; do
  grep -Fq -- "$phrase" "$BATCH_ROOT/src/working_set_ladder.cpp" || {
    echo "benchmark source lacks contract phrase: $phrase" >&2; exit 65; }
done
for phrase in "Decision question" "Plain-language model" "time_enabled/time_running"; do
  grep -Fq -- "$phrase" "$BATCH_ROOT/src/analyze_working_set.cpp" || {
    echo "analyzer lacks contract phrase: $phrase" >&2; exit 65; }
done
for phrase in "not a literal trace" "No remote chart library" "generated local article data"; do
  grep -Fq -- "$phrase" "$BATCH_ROOT/article/working_set_visuals.html" || {
    echo "visual companion lacks evidence boundary: $phrase" >&2; exit 65; }
done

bash -n "$BATCH_ROOT/scripts/generate_build_matrix.sh" \
  "$BATCH_ROOT/scripts/generate_stride16_selected_matrix.sh" \
  "$BATCH_ROOT/scripts/generate_prefetch_selected_matrix.sh" \
  "$BATCH_ROOT/scripts/generate_mlp8_selected_matrix.sh" \
  "$BATCH_ROOT/scripts/export_stride16_article_data.sh" \
  "$BATCH_ROOT/scripts/build_selected.sh" \
  "$BATCH_ROOT/scripts/train_pgo.sh" \
  "$BATCH_ROOT/scripts/run_working_set_on_dut.sh" \
  "$BATCH_ROOT/scripts/run_prefetch_selected_raw_on_haswell_dut.sh" \
  "$BATCH_ROOT/scripts/run_mlp8_selected_raw_on_haswell_dut.sh" \
  "$BATCH_ROOT/scripts/validate_prefetch_ladder_results.sh" \
  "$BATCH_ROOT/scripts/validate_mlp8_ladder_results.sh" \
  "$BATCH_ROOT/scripts/validate_prefetch_raw_results.sh" \
  "$BATCH_ROOT/scripts/validate_mlp8_raw_results.sh" \
  "$BATCH_ROOT/scripts/analyze_and_stage_article.sh"
echo "working-set-ladder source contract passed"
