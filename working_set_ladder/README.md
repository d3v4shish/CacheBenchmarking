# Cache Search Lab v2 — micro_working_set_ladder

Status: accepted Haswell results exist for the `stride16_scan`,
`random_access`, `arena_pointer_chase`, and `dependent_index_cycle` retests.
The completed dependent-index evidence is rendered in the local blog article;
it has not been pushed or remotely published.

This is the one permitted next benchmark case. It reconstructs the published
working-set ladder as a shared footprint sweep over eight historical access
shapes:

| Mode | Timed operation | Why it belongs here |
|---|---|---|
| linear_scan | contiguous uint32_t load | useful-cache-line baseline |
| stride16_scan | one uint32_t every 64 B | cache-line payload waste |
| random_access | random direct uint32_t load | nonsequential data access |
| arena_pointer_chase | random 64-B node follow | pointer-shaped line use |
| dependent_index_cycle | closed random index cycle | strict load-to-use dependency |
| prefetch_chain | dependency cycle plus prefetch | one-step software-prefetch control |
| mlp8_cycle | eight independent node cycles | memory-level parallelism |
| tlb_page_walk | one random touch/page | page translation pressure |

The historical points (32 KiB, 256 KiB, 1 MiB, 8 MiB, 64 MiB) are retained
inside the v2 ladder. The finer test points bracket the documented Haswell
32-KiB L1D, 256-KiB L2, and 6-MiB LLC capacities. A later dedicated stride,
pointer, prefetch, TLB, or huge-page case is still required; this case only
asks how each historical shape changes with footprint.

## Layout

- src/working_set_ladder.cpp is the measurement binary. It owns the exact data
  construction, timed boundaries, semantic checksum, and conservative generic
  PMU profiles.
- src/analyze_working_set.cpp converts raw CSV files into data used by the
  article. It rejects bad rows instead of averaging them in.
- scripts/generate_build_matrix.sh emits the complete requested compilation
  product with a status field for unavailable toolchains/configurations.
- scripts/run_working_set_on_dut.sh is the only DUT runner. It validates the
  non-mutating machine gate, runs one selected build matrix row, randomizes
  retained case order, saves manifests/disassembly/telemetry, and refuses to
  overwrite evidence.
- article/ contains the pending-results article and its static interactive
  companion. Data is copied there only after analysis of an accepted run.
- scripts/analyze_and_stage_article.sh compiles the retained analyser, rejects
  an already-derived session, and stages hash-identified CSV inputs without
  editing article prose.

## Publication gate

The runner refuses to create evidence unless CPU 4 is pinned, CPU 5 is its
SMT sibling, frequency/turbo controls are already in the required state,
perf_event_paranoid permits PMU collection, turbostat can take a telemetry
sample, and the selected compiler configuration is fully identified.

It deliberately does **not** alter governor, turbo, IRQ affinity, services,
power state, or package installation. Those are privileged DUT preparation
actions, not hidden benchmark setup.

## Data contract

samples.csv has one retained timing sample per row. counters.csv has one row
per event per timing sample, including time_enabled and time_running.
build_manifest.tsv identifies every requested compile configuration, while the
run manifest retains source/binary hashes, compiler reports, disassembly,
machine state, and telemetry.

Normal samples require at least 250 ms, unchanged CPU placement, no timed page
fault, and exact PMU running time. page_cold is an explicit first-touch
exception and is kept in its own article/table state.

The generic `cache` and `tlb` profiles remain available as negative controls.
On this Haswell DUT they were unschedulable (some events had zero running
time), so the stride-16 retest uses separately scheduled raw profiles:
`l1_miss`, `l2_miss`, `l3_miss`, and `dtlb_walk`. They are accepted only when
their raw event and cycles both have exact running time.

For a selected raw-PMU run, continuous `turbostat` is intentionally disabled:
on this Haswell it competes for the constrained load-miss counter. The runner
still retains a preflight attempt and records this decision in telemetry.

## Local smoke test

This validates code only; it is not a benchmark run:

    g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror \
      src/working_set_ladder.cpp -o /tmp/working_set_ladder
    taskset --cpu-list 0 /tmp/working_set_ladder \
      --mode linear_scan --implementation scalar --state warm --perf-profile none \
      --build-id smoke --footprint-bytes 65536 --round 0 --expected-cpu 0 \
      --min-duration-ms 5 --seed 7 --output /tmp/samples.csv \
      --counter-output /tmp/counters.csv

Never publish this command's result: it bypasses the Haswell hardware gate and
uses a deliberately short duration.

## Random-access retained evidence

`results/random_access_v1/gcc13-basic-branch` contains the 27-footprint GCC
scalar random-direct ladder, 15 repetitions per accepted state/profile group.
`results/random_access_v1/raw-selected` is the separately scheduled Haswell
L1/L2/L3/DTLB raw-event supplement. The C/C++/Rust/Go control is retained in
`../linear_scan_matrix/results/random_access_language_v2`; the same-source
GCC/Clang control in `../linear_scan_matrix/results/random_access_cpp_compilers_v1`;
and the deliberately different eight-chain MLP control in
`results/random_access_mlp8_control_v1`. These controls are not merged: they
answer different questions about code generation and dependency overlap.

## Dependent-index retained evidence

`results/dependent_index_v1/working-set-ladder-index-v1-gcc13-basic-branch`
contains the 1,020-row scalar capacity/basic/branch ladder and
`results/dependent_index_v1/working-set-ladder-index-raw-v1` the 168-row raw
L1/L2/L3/DTLB supplement. The operation is the strict closed recurrence
`index = next[index]`: one 4-byte-index load supplies the next address, so it
is neither a direct random read nor a 64-byte arena-node chase.

Independent 420-row language and 420-row same-source compiler controls are in
`../linear_scan_matrix/results/dependent_index_v1`. Each cell has fifteen
accepted repetitions, exact PMU running time, source/binary hashes,
disassembly, and shuffled order. Use the language artifact's corrected
`analysis/` summary; the raw DUT artifact remains unchanged.
