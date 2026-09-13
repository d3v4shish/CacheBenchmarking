#!/usr/bin/env bash
# Batch 1 source-readability gate.
#
# This is not a style score.  It checks a few minimum, machine-checkable facts
# from roadmap Section 3.10 before we execute code on the DUT: the executable
# source has a beginner-facing purpose/source header, it names the noinline
# instruction construction, and the DUT runner records the required evidence.
# A human still reads the comments; this script merely prevents easy omissions.

set -Eeuo pipefail

# Locate the batch root from this script, so the check works from any current
# directory and does not depend on a hidden developer-specific absolute path.
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BATCH_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)
CPP="$BATCH_ROOT/src/batch1.cpp"
ANALYZER="$BATCH_ROOT/src/analyze_batch1.cpp"
RUNNER="$BATCH_ROOT/scripts/run_batch1_on_dut.sh"
README_FILE="$BATCH_ROOT/README.md"
VISUALS="$BATCH_ROOT/article/batch1_visuals.html"

# These phrases correspond directly to the plain-language contract.  A missing
# phrase is a reason to inspect/fix the source before timing, not a reason to
# waive the requirement because the benchmark happens to compile.
for required_file in "$CPP" "$ANALYZER" "$RUNNER" "$README_FILE" "$VISUALS"; do
  [[ -f "$required_file" ]] || { echo "missing required file: $required_file" >&2; exit 66; }
done

# Analysis code is executable too, so it gets the same beginner-facing reason,
# method source, and explicit treatment of PMU multiplexing as the benchmark.
for required_phrase in \
  'Decision question' \
  'Plain-language model' \
  'time_enabled/time_running' \
  'https://man7.org/linux/man-pages/man2/perf_event_open.2.html'; do
  grep -Fq -- "$required_phrase" "$ANALYZER" || {
    echo "analyzer source-contract phrase is missing: $required_phrase" >&2
    exit 65
  }
done

# Browser-rendered SVG/animation is code as well.  These phrases ensure it
# states its evidence boundary instead of letting a moving dot look measured.
for required_phrase in \
  'Batch 1 visual companion' \
  'teaching diagram only' \
  'not evidence' \
  'raw CSV'; do
  grep -Fq -- "$required_phrase" "$VISUALS" || {
    echo "visual source-contract phrase is missing: $required_phrase" >&2
    exit 65
  }
done

for required_phrase in \
  'Decision question' \
  'Plain-language model' \
  'Method sources' \
  'https://www.agner.org/optimize/instruction_tables.pdf' \
  'https://man7.org/linux/man-pages/man2/perf_event_open.2.html' \
  'BENCH_NOINLINE' \
  'time_enabled/time_running'; do
  grep -Fq -- "$required_phrase" "$CPP" || {
    echo "C++ source-contract phrase is missing: $required_phrase" >&2
    exit 65
  }
done

for required_phrase in \
  'Safety:' \
  'source_and_binary_sha256' \
  'disassembly_with_opcodes' \
  'run_order.csv' \
  'refusing to overwrite'; do
  grep -Fq -- "$required_phrase" "$RUNNER" || {
    echo "runner source-contract phrase is missing: $required_phrase" >&2
    exit 65
  }
done

# Success is intentionally a short, explicit statement suitable for the batch
# manifest.  It means the minimum audit scaffolding exists, not that the
# hardware results are already valid.
echo "Batch 1 source-readability gate passed."
