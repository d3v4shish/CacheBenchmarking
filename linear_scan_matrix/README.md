# Linear-scan language and SIMD matrix

This project measures a contiguous, repeated scan on the retained Haswell DUT.
It compares equivalent scalar kernels across C, C++, Rust, and Go, and labels
explicit SIMD separately. It is not a general language benchmark or a
first-touch cache benchmark.

The immutable `results/dut-run-20260908-linear-v1/` directory is the original
run. New measurements use a distinct `v2` run directory and are never merged
with v1.

Read [BUILD.md](BUILD.md) for deterministic commands and
[BENCHMARKS.md](BENCHMARKS.md) for the measurement contract.

## MLP8-cycle controls

The four MLP8 sources use a seeded closed permutation of 64-byte nodes. Their
timed loop holds eight live successor indices: each lane has a strict
load-to-use dependency, while the eight separate loads can overlap. This is
memory-level parallelism, not a SIMD or prefetch benchmark.

```bash
make mlp8_language
scripts/run_mlp8_language_on_haswell_dut.sh /home/d3v/mlp8-language-run-UNIQUE
scripts/validate_mlp8_language_results.sh /home/d3v/mlp8-language-run-UNIQUE/raw
scripts/analyze_mlp8_language_results.sh /home/d3v/mlp8-language-run-UNIQUE/raw
scripts/run_mlp8_cpp_compilers_on_haswell_dut.sh /home/d3v/mlp8-cpp-compilers-run-UNIQUE
scripts/run_mlp8_lane_pair_on_haswell_dut.sh /home/d3v/mlp8-lane-pair-run-UNIQUE
```

The language and compiler matrices each retain 420 shuffled rows (four
products × seven landmarks × fifteen repetitions); the one-lane/eight-lane
control retains 210. All rows must run for at least 250 ms with a correct
checksum and an exact in-process cycles/instructions/reference-cycles group.

## Stride-16 language control matrix

`src/stride16_{c,cpp,rust,go}` are four deliberately small equivalents for
the stride-16 working-set article. Each reads `uint32_t[0], [16], [32] ...`:
one useful word per 64-byte cache line. Their shared in-process PMU interval
contains only that repeated loop.

```bash
make stride16_language
scripts/run_stride16_language_on_haswell_dut.sh /home/d3v/stride16-language-run-UNIQUE
scripts/validate_stride16_language_results.sh /home/d3v/stride16-language-run-UNIQUE/raw
scripts/analyze_stride16_language_results.sh /home/d3v/stride16-language-run-UNIQUE/raw
```

The runner fixes exactly 7 footprints × 4 languages × 15 shuffled repetitions
(420 rows) and refuses a non-Haswell, turbo-enabled, unlocked, sibling-online,
or multiplexed-PMU run. `results/stride16-language-run-20260909-b/` is the
retained accepted DUT artifact.

## Dependent-index-cycle controls

`src/index_{c,cpp,rust,go}` build the same seeded closed permutation over
`uint32_t` entries and time only `index = next[index]`. Each iteration has one
true load-to-use address dependency, so this is neither a direct random-load
benchmark nor a useful SIMD target. The accepted Haswell artifacts are in
`results/dependent_index_v1/`: `index-language-run-v5` (420 language rows) and
`index-cpp-compilers-run-v1` (420 same-source GCC/Clang rows). Each has seven
landmarks × four products × fifteen shuffled repetitions and exact in-process
cycles/instructions/reference-cycles.
