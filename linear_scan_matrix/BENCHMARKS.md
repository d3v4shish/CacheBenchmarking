# Benchmark contract

The primary v2 matrix holds byte footprint constant at 32 KiB, 256 KiB, 1 MiB,
8 MiB, and 64 MiB. `passes = ceil(2 GiB / footprint)`, so every retained
sample scans at least 2 GiB after allocation and fill.

Primary counters are in-process, thread-scoped `cycles`, `instructions`, and
`ref-cycles`, enabled only around the scan. The primary matrix has 15 fully
shuffled repetitions per case. Event/profile passes are selected diagnostics,
not substituted for primary data.

The v1 results used outer `perf stat`; do not combine its PMU values with v2.

## Retained v2 result

`results/dut-run-v2-20260909-c/` passed validation with 4,275 samples and
12,825 PMU records: 285 variant/type/footprint groups, 15 repetitions each.
The matrix includes C, shared-source C++, native-vector C++, Clang C++, Rust,
and Go scalar scans; C/C++/Rust explicit AVX2; and C++/Rust FP unroll
diagnostics. The selected external event and `perf record` passes are retained
under `diagnostics/` and are labelled separately from in-scan primary PMU data.

## Stride-16 C/C++/Rust/Go control, 2026-09-09

Retained artifact: `results/stride16-language-run-20260909-b/`. Intel
i7-4702MQ (Haswell model 60), CPU 4 pinned at 2.2 GHz, CPU 5 offline, turbo
disabled; GCC/G++ 13.3, Rust 1.75, Go 1.22.2. Each of the 28 named groups has
15 accepted shuffled samples, or 420 total. Each sample executes at least
33,554,432 useful loads (rounding passes upward where needed) and has a
non-multiplexed `cycles/instructions/ref-cycles` group.

At 32 KiB C/C++/Rust are 1.320/1.335/1.327 cycles per useful load at about
4.01 instructions/load; Go is 1.726 cycles at 7.016 instructions/load. At
512 MiB they converge to 10.331/10.317/10.325/10.120 cycles/load respectively.
This is a language control run, not a replacement for the article's distinct
27-footprint raw-cache-counter capacity ladder.

## Dependent-index controls, 2026-09-10

`results/dependent_index_v1/index-language-run-v5/` contains 420 accepted
C/C++/Rust/Go rows and `index-cpp-compilers-run-v1/` contains 420 accepted
same-source C++ compiler-policy rows. Both are 28 cells × 15 shuffled
repetitions on the locked Haswell DUT; every retained row is at least 250 ms,
checksum-correct, and has exact grouped cycles/instructions/reference-cycles.

At 32 KiB C/C++/Rust/Go measure 5.001/5.009/5.353/5.006 cycles per dependent
index load; at 512 MiB they converge at 214.495/215.091/215.142/215.131.
GCC scalar is 5.010/215.231 cycles at those endpoints, versus Clang scalar's
5.135/215.119. The language artifact's corrected reference-cycle derivation
is in `index-language-run-v5/analysis/`; raw DUT output is preserved unchanged.

## Late-prefetch controls, 2026-09-10

results/prefetch_chain_v1/prefetch-language-run-v1 contains 420 accepted
C/C++/Rust/Go rows, and prefetch-cpp-compilers-run-v2 contains 420 accepted
same-source GCC 13/Clang 18 scalar/default-policy rows. Every 28-cell matrix
has fifteen shuffled, checksum-correct rows of at least 250 ms with exact
cycles/instructions/reference-cycles PMU time. Every retained language and
compiler disassembly contains PREFETCHT2.

The causal paired control in prefetch-paired-control-run-v1 contains 210
accepted rows: GCC 13 builds the same C++ source with PREFETCH_ENABLED=1 or
0. The on binary retains PREFETCHT2; the off binary has none. It measures
236.789 versus 219.778 median cycles/node at 512 MiB, so the late one-hop
request did not hide the dependent DRAM-scale wait.
