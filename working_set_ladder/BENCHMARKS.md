# Benchmarks

The active benchmark is the isolated `stride16_scan` retest.  It pins CPU 4 on
the Haswell DUT, requires its SMT sibling CPU 5 to be identified, requires
performance governor, turbo disabled, and permitted PMU access, then records
15 randomized repetitions of each planned condition.  Every normal timing run
lasts at least 250 ms.

Run a selected build without altering DUT settings:

```bash
scripts/run_working_set_on_dut.sh /home/d3v/working-set-ladder-NEW /absolute/build-dir --only-mode stride16_scan --only-implementation scalar
```

Use `auto` for the vector-default binary. The runner records warm full-ladder
points and cache-evicted capacity-boundary points under separately scheduled
basic, branch, and raw Haswell L1/L2/L3-miss and DTLB-walk PMU profiles. The
generic cache/TLB profiles are documented negative controls because this DUT
proved them unschedulable. Selected raw-PMU runs also omit continuous
`turbostat`, which otherwise occupies the constrained load-miss counter; their
preflight and omission are retained. Results will be added here only after a
DUT run passes analyser acceptance.

## Accepted v3 result

The 2026-09-09 DUT evidence is retained in `results/stride16_v3`. GCC scalar
has 102 accepted profile/footprint/state groups (basic, branch, DTLB walk).
A separate no-continuous-turbostat supplement has all 102 L1/L2/L3 raw-miss
groups accepted. GCC auto, Clang scalar, and Clang auto each have all 68
basic/branch groups accepted. Individual timed-fault rows were rejected before
their medians were calculated.

## Accepted random-access controls

The retained random direct-index ladder is
`results/random_access_v1/gcc13-basic-branch`, with the matching raw-event
supplement in `results/random_access_v1/raw-selected`. It measures a
randomized `uint32_t` payload load selected by a same-size deterministic query
permutation; named payload footprint therefore differs from its physical
payload-plus-query footprint.

Three deliberately separate controls extend that evidence. The 2026-09-09
C/C++/Rust/Go matrix at
`../linear_scan_matrix/results/random_access_language_v2` contains 420
accepted samples: 4 languages × 7 payload landmarks × 15 shuffled
repetitions, all at least 250 ms with exact-running basic PMU groups. The
same-source GCC 13/Clang 18 scalar/default-policy matrix at
`../linear_scan_matrix/results/random_access_cpp_compilers_v1` has the same
420 accepted-sample shape. The `results/random_access_mlp8_control_v1` MLP8
control has 105 accepted samples (7 × 15); it uses eight independent 64-byte
node chains and is explicitly not combined with direct-index rows.

## Accepted arena pointer-chase result

The 2026-09-09 arena result retains the GCC scalar capacity/basic/branch
ladder and raw-PMU supplement in `results/arena_pointer_v1`, plus separate
420-row C/C++/Rust/Go and 420-row same-source compiler controls in
`../linear_scan_matrix/results/arena_pointer_v1`. Each node is one aligned
64-byte cache line in a seeded closed random cycle. The raw supplement has 168
accepted timing rows and 336 exact-running counter rows; every language and
compiler cell has 15 accepted samples.

## Accepted dependent-index-cycle result

The 1,020-row scalar capacity/basic/branch ladder is retained in
`results/dependent_index_v1/working-set-ladder-index-v1-gcc13-basic-branch`.
Its separately scheduled raw-event companion has 168 accepted timing rows and
336 exact-running L1/L2/L3/DTLB rows in
`results/dependent_index_v1/working-set-ladder-index-raw-v1`.

The controls in `../linear_scan_matrix/results/dependent_index_v1` each have
420 accepted rows: `index-language-run-v5` covers C/C++/Rust/Go and
`index-cpp-compilers-run-v1` covers GCC 13/Clang 18 scalar/default policies.
Every one of their 28 cells has fifteen shuffled repetitions, a 250-ms
minimum, checksum equality, exact in-process PMU time, and a locally verified
copied artifact hash.

## Accepted late-prefetch-chain result

The retained Haswell scalar artifact is
results/prefetch_chain_v1/working-set-ladder-prefetch-v2-gcc13-basic-branch-18attempts.
It contains 1,224 scheduled scalar attempts: warm basic/branch at all 27
footprints and cache-evicted basic/branch at seven landmarks. Its independent
validator requires at least 15 checksum-correct, 250-ms, CPU-pinned,
exact-running-PMU rows in every reported cell; 30 timed-page-fault rows are
retained in the quality log and excluded from derived summaries.

The separate raw supplement is
results/prefetch_chain_v1/working-set-ladder-prefetch-raw-v4-8attempts.
It has 224 timing rows and 448 counter rows: seven landmarks, four isolated
raw-event profiles, eight repetitions, and no rejected row. The four
language/front-end and four compiler-policy controls live under
../linear_scan_matrix/results/prefetch_chain_v1, each with 420 accepted rows.
The same-source GCC paired prefetch-on/off control has 210 accepted rows and
retains disassembly proving PREFETCHT2 is present only in the on binary. These
artifacts support the local-only prefetch-chain article.

## Accepted MLP8-cycle result

The fresh scalar ladder is retained under results/mlp8_cycle_v1. It has all
68 warm/cache-evicted, basic/branch cells accepted with at least 15 pinned,
fault-free 250-ms samples and exact PMU groups. At 512 MiB the warm/basic
median is 27.596 TSC ticks/node (12.573 ns), compared with 222.238
ticks/node in the separately retained single-chain arena control. This is an
MLP shape change, not a single-chain speedup.

The isolated raw supplement contains 224 timing rows and 448 exact-running
counter rows: seven landmarks, four Haswell raw cache/TLB profiles, eight
shuffled attempts each. The independent C/C++/Rust/Go, compiler-policy, and
one-versus-eight-lane controls in the sibling linear_scan_matrix results
directory contain 420, 420, and 210 accepted rows respectively. The direct
GCC 13 pair measures 219.974 cycles/node with one lane versus 27.694 with
eight at 512 MiB (7.94×).
