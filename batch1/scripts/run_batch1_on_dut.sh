#!/usr/bin/env bash
# Batch 1 DUT runner: build one transparent binary and retain every raw sample.
#
# Why this script exists
# ----------------------
# A benchmark command is part of the experiment.  This script makes CPU
# placement, compiler flags, input sizes, warm-ups, run order, result paths,
# machine state, and source/binary hashes explicit so a reader does not have to
# guess what was executed.  It is intentionally verbose and fails on the first
# missing precondition instead of producing a partial-looking success.
#
# Batch/test IDs: V01, V05, V06, V07, IL01–IL05 (small integer subset).
# Decision: distinguish timer/loop overhead, dependency latency, independent
# stream throughput, and PMU multiplexing on CPU 4 of the Haswell DUT.
#
# Sources explaining the method, not fabricating a result:
# [1] https://www.agner.org/optimize/instruction_tables.pdf
# [2] https://www.agner.org/optimize/microarchitecture.pdf
# [3] https://man7.org/linux/man-pages/man2/perf_event_open.2.html
#
# Safety: the required output directory must not exist.  The script creates
# only that named directory and its children.  It does not change CPU governor,
# turbo, kernel PMU policy, filesystems, services, or package installations.

set -Eeuo pipefail

# The caller must pass an explicit new absolute directory.  Refusing a default
# avoids accidentally mixing this batch's raw results with a previous run.
if [[ $# -ne 1 ]]; then
  echo "usage: $0 /absolute/new/result-directory" >&2
  exit 64
fi
RUN_ROOT=$1
if [[ "$RUN_ROOT" != /home/d3v/benchmark-batch1-* || "$RUN_ROOT" == *'..'* ]]; then
  echo "refusing unsafe result directory: $RUN_ROOT" >&2
  exit 64
fi
if [[ -e "$RUN_ROOT" ]]; then
  echo "refusing to overwrite existing result directory: $RUN_ROOT" >&2
  exit 73
fi

# `BASH_SOURCE[0]` locates this copied script.  The source C++ file is expected
# beside it in the run root; the deploy step copies both before execution.
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
SOURCE_FILE="$SCRIPT_DIR/../src/batch1.cpp"
BUILD_DIR="$RUN_ROOT/build"
RAW_DIR="$RUN_ROOT/raw"
MANIFEST_DIR="$RUN_ROOT/manifest"
ARTICLE_INPUT_DIR="$RUN_ROOT/article-input"
BIN="$BUILD_DIR/batch1"
CSV="$RAW_DIR/samples.csv"
ORDER_CSV="$RAW_DIR/run_order.csv"

# Batch 1 intentionally uses CPU 4, while CPU 5 is its SMT sibling.  The
# topology snapshot below proves whether this assumption still holds on the
# current boot; we do not assume it just because an earlier inventory said so.
CPU=4
SMT_SIBLING=5

# Every ALU loop now contains 128 static target instructions, as required by
# roadmap Section 3.9.  Twelve million loop turns therefore execute 1.536
# billion target instructions: long enough for the normal sample-duration rule
# while keeping this corrective rerun practical.  Page-state rows remain a
# deliberate exception because one pass measures first-touch fault semantics.
ITERATIONS=12000000
PAGE_ITERATIONS=1
REPETITIONS=15
WARMUPS=1

# Before creating evidence, verify every executable/tool this script relies on.
for command in g++ objdump nm sha256sum lscpu perf taskset shuf awk sed; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "required command is unavailable: $command" >&2
    exit 69
  fi
done
if [[ ! -r /sys/devices/system/cpu/cpu${CPU}/online && ! -d /sys/devices/system/cpu/cpu${CPU} ]]; then
  echo "requested benchmark CPU $CPU does not exist" >&2
  exit 69
fi
if [[ ! -f "$SOURCE_FILE" ]]; then
  echo "expected source file is missing: $SOURCE_FILE" >&2
  exit 66
fi

# Only now create the new directory tree.  Each location has one purpose:
# raw = unaggregated evidence, manifest = environment/disassembly proof,
# article-input = small copied inputs that the later article may link directly.
mkdir -p -- "$BUILD_DIR" "$RAW_DIR" "$MANIFEST_DIR" "$ARTICLE_INPUT_DIR"

# Capture host state before compiling/running.  `|| true` is used only for
# optional observability files; their absence is itself documented in output.
{
  date --iso-8601=seconds
  uname -a
  lscpu
  cat /proc/cmdline
  cat /proc/sys/kernel/perf_event_paranoid
  grep -H . /sys/devices/system/cpu/cpu${CPU}/cpufreq/scaling_{governor,cur_freq,max_freq,min_freq} 2>&1 || true
  grep -H . /sys/devices/system/cpu/intel_pstate/{status,no_turbo} 2>&1 || true
  grep -H . /sys/devices/system/cpu/cpu${CPU}/topology/{core_id,physical_package_id,thread_siblings_list} 2>&1 || true
  g++ --version
  perf --version
} >"$MANIFEST_DIR/pre_run_machine.txt"

# Save only the PMU aliases used by this batch.  This makes raw-event claims
# inspectable and avoids implying that aliases from another CPU apply here.
perf list --details 2>/dev/null |
  grep -Ei -A2 'uops_issued.any|uops_retired.all|uops_dispatched_port.port_[015]|cpu_clk_unhalted.thread|ref-cycles' \
  >"$MANIFEST_DIR/perf_event_aliases.txt" || true

# Log tasks currently placed on the sibling.  This is evidence about isolation,
# not a promise that CPU 5 is perfectly idle throughout a sample.
ps -eo psr=,pid=,comm= | awk -v cpu="$SMT_SIBLING" '$1 == cpu { print }' \
  >"$MANIFEST_DIR/cpu${SMT_SIBLING}_tasks_before.txt" || true

# Compile with flags that name the Haswell target, retain frame pointers for
# audit, and turn warnings into build failures.  Inline assembly fixes the
# measured instructions; the disassembly below verifies the compiler honored it.
g++ -std=c++20 -O3 -march=haswell -mtune=haswell \
  -Wall -Wextra -Wpedantic -Werror -fno-omit-frame-pointer \
  -o "$BIN" "$SOURCE_FILE"
sha256sum "$SOURCE_FILE" "$BIN" >"$MANIFEST_DIR/source_and_binary_sha256.txt"
nm -C --defined-only "$BIN" >"$MANIFEST_DIR/symbols.txt"
objdump -d -Mintel --no-show-raw-insn "$BIN" >"$MANIFEST_DIR/disassembly_intel.txt"
objdump -d -Mintel "$BIN" >"$MANIFEST_DIR/disassembly_with_opcodes.txt"

# Save the command-line capability check so a future reader can reproduce the
# binary's required arguments without reading this shell implementation.
"$BIN" --help >"$MANIFEST_DIR/binary_usage.txt"

# The fields are: mode | state | PMU profile | iteration count | warm-ups.
# `basic` is the normal counter pass.  `expanded` deliberately asks for more
# generic PMU events than fit at once, so V06 can show time_running shrink.
# `none` gives a timer-only comparison without PMU collection overhead.
CASES=(
  "timer_only|warm|none|1|0"
  # This matched-reference ladder shows when timestamp/loop setup is too large
  # relative to useful ADD work.  With 128 ADDs per body, the three short rows
  # execute 1,024, 102,400, and 10.24M target instructions respectively.
  # The ${ITERATIONS} row is the publication-length sample.
  "empty|warm|basic|8|${WARMUPS}"
  "empty|warm|basic|800|${WARMUPS}"
  "empty|warm|basic|80000|${WARMUPS}"
  "empty|warm|basic|${ITERATIONS}|${WARMUPS}"
  "add_latency|warm|none|${ITERATIONS}|${WARMUPS}"
  "add_latency|warm|basic|8|${WARMUPS}"
  "add_latency|warm|basic|800|${WARMUPS}"
  "add_latency|warm|basic|80000|${WARMUPS}"
  "add_latency|warm|basic|${ITERATIONS}|${WARMUPS}"
  "add_latency|warm|expanded|${ITERATIONS}|${WARMUPS}"
  "imul_latency|warm|basic|${ITERATIONS}|${WARMUPS}"
  "shift_latency|warm|basic|${ITERATIONS}|${WARMUPS}"
  "xor_break|warm|basic|${ITERATIONS}|${WARMUPS}"
  "and_zero_control|warm|basic|${ITERATIONS}|${WARMUPS}"
  "add_throughput_1|warm|basic|${ITERATIONS}|${WARMUPS}"
  "add_throughput_2|warm|basic|${ITERATIONS}|${WARMUPS}"
  "add_throughput_4|warm|basic|${ITERATIONS}|${WARMUPS}"
  "add_throughput_8|warm|basic|${ITERATIONS}|${WARMUPS}"
  "imul_throughput_8|warm|basic|${ITERATIONS}|${WARMUPS}"
  "shift_throughput|warm|basic|${ITERATIONS}|${WARMUPS}"
  "xor_throughput|warm|basic|${ITERATIONS}|${WARMUPS}"
  "page_state_probe|warm|basic|${PAGE_ITERATIONS}|0"
  "page_state_probe|cache_evicted|basic|${PAGE_ITERATIONS}|0"
  "page_state_probe|page_cold|basic|${PAGE_ITERATIONS}|0"
)

printf 'round,mode,state,perf_profile,iterations,warmups\n' >"$ORDER_CSV"

# One repetition of every case is shuffled per round.  The persisted run-order
# file records the actual order, which is more useful than pretending an OS
# random seed alone can reconstruct it.  Each binary invocation appends exactly
# one CSV row and fails closed if its checksum, affinity, or PMU setup fails.
for ((round = 0; round < REPETITIONS; ++round)); do
  while IFS='|' read -r mode state perf_profile iterations warmups; do
    printf '%s,%s,%s,%s,%s,%s\n' "$round" "$mode" "$state" "$perf_profile" \
      "$iterations" "$warmups" >>"$ORDER_CSV"
    taskset --cpu-list "$CPU" "$BIN" \
      --mode "$mode" \
      --state "$state" \
      --perf-profile "$perf_profile" \
      --round "$round" \
      --iterations "$iterations" \
      --repetitions 1 \
      --warmups "$warmups" \
      --cpu "$CPU" \
      --output "$CSV"
  done < <(printf '%s\n' "${CASES[@]}" | shuf)
done

# Re-capture the two environmental facts most likely to change during a laptop
# session: sibling activity and reported CPU frequency/governor state.
ps -eo psr=,pid=,comm= | awk -v cpu="$SMT_SIBLING" '$1 == cpu { print }' \
  >"$MANIFEST_DIR/cpu${SMT_SIBLING}_tasks_after.txt" || true
{
  date --iso-8601=seconds
  grep -H . /sys/devices/system/cpu/cpu${CPU}/cpufreq/scaling_{governor,cur_freq,max_freq,min_freq} 2>&1 || true
  grep -H . /sys/devices/system/cpu/intel_pstate/{status,no_turbo} 2>&1 || true
} >"$MANIFEST_DIR/post_run_frequency.txt"

# A final manifest gives the article a simple integrity check: every expected
# case/round pair must have one raw row, and every retained artifact has a hash.
# The raw file is CSV, so the field separator must be a comma.  This sidecar is
# only a count/check convenience; the article always derives statistics from
# `samples.csv` and preserves that file's independent SHA-256.
awk -F, 'NR > 1 { count[$1 FS $2 FS $3 FS $4]++ } END { for (key in count) print key "," count[key] }' \
  "$CSV" | sort >"$ARTICLE_INPUT_DIR/rows_per_round_mode_state_profile.csv"
sha256sum "$CSV" "$ORDER_CSV" "$MANIFEST_DIR"/*.txt >"$MANIFEST_DIR/result_artifact_sha256.txt"
cp "$CSV" "$ORDER_CSV" "$ARTICLE_INPUT_DIR/"

echo "Batch 1 completed successfully: $RUN_ROOT"
