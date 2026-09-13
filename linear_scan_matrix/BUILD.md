# Deterministic commands

Run from this directory.

```bash
make build       # build every local v2 binary except optional Clang C++
make clangpp     # build the Clang C++ compiler-matrix binary
make test        # deterministic small-input and validator tests
make benchmark   # prints the guarded DUT command; does not run remotely
make run         # local smoke run, not a performance result
```

On the prepared DUT, use one unique result directory:

```bash
scripts/prepare_haswell_dut_v2.sh prepare
scripts/run_v2_on_haswell_dut.sh /home/d3v/linear-scan-matrix-run-UNIQUE
scripts/validate_results.sh /home/d3v/linear-scan-matrix-run-UNIQUE/raw
scripts/analyze_results.sh /home/d3v/linear-scan-matrix-run-UNIQUE/raw
scripts/prepare_haswell_dut_v2.sh restore
```

The DUT runner refuses an unlocked frequency, enabled turbo, unavailable PMU,
wrong CPU model, or an existing output directory.

For the compact stride-16 language control matrix:

```bash
make stride16_language
scripts/run_stride16_language_on_haswell_dut.sh /home/d3v/stride16-language-run-UNIQUE
scripts/validate_stride16_language_results.sh /home/d3v/stride16-language-run-UNIQUE/raw
scripts/analyze_stride16_language_results.sh /home/d3v/stride16-language-run-UNIQUE/raw
```

It is deterministic in case set and shuffle seed (`20260909` by default), but
uses a unique result directory and retains the actual shuffled order.

For the random-direct-load language and same-source C++ compiler controls:

```bash
make random_language
scripts/run_random_language_on_haswell_dut.sh /home/d3v/random-language-run-UNIQUE
scripts/run_random_cpp_compilers_on_haswell_dut.sh /home/d3v/random-cpp-compilers-run-UNIQUE
```

Both runners require CPU 4 at fixed frequency, turbo disabled, permitted PMU
access, and offline SMT sibling CPU 5. They retain 15 shuffled repetitions at
seven landmarks and reject a sample shorter than 250 ms, with a bad checksum,
or with multiplexed PMU time. The compiler runner expects the explicitly
hashed GCC/Clang products named in its source.

For the MLP8 language, compiler, and lane-count controls:

```bash
make mlp8_language
scripts/run_mlp8_language_on_haswell_dut.sh /home/d3v/mlp8-language-run-UNIQUE
scripts/validate_mlp8_language_results.sh /home/d3v/mlp8-language-run-UNIQUE/raw
scripts/analyze_mlp8_language_results.sh /home/d3v/mlp8-language-run-UNIQUE/raw
scripts/run_mlp8_cpp_compilers_on_haswell_dut.sh /home/d3v/mlp8-cpp-compilers-run-UNIQUE
scripts/validate_mlp8_cpp_compilers_results.sh /home/d3v/mlp8-cpp-compilers-run-UNIQUE/raw
scripts/run_mlp8_lane_pair_on_haswell_dut.sh /home/d3v/mlp8-lane-pair-run-UNIQUE
scripts/validate_mlp8_lane_pair_results.sh /home/d3v/mlp8-lane-pair-run-UNIQUE/raw
```

All three require Haswell CPU 4 fixed at frequency with turbo disabled,
permitted PMU access, and its CPU-5 SMT sibling offline. The language/compiler
matrices use eight live chains; the pair changes only MLP_LANES from one to
eight, with per-footprint operation targets chosen only to clear the 250 ms
acceptance floor.

For the dependent-index language and same-source compiler controls:

```bash
make index_language
scripts/run_index_language_on_haswell_dut.sh /home/d3v/index-language-run-UNIQUE
scripts/validate_index_language_results.sh /home/d3v/index-language-run-UNIQUE/raw
scripts/analyze_index_language_results.sh /home/d3v/index-language-run-UNIQUE/raw
scripts/run_index_cpp_compilers_on_haswell_dut.sh /home/d3v/index-cpp-compilers-run-UNIQUE
scripts/validate_index_cpp_compilers_results.sh /home/d3v/index-cpp-compilers-run-UNIQUE/raw
```

Both require CPU 4 at fixed frequency, turbo disabled, PMU access, and an
offline CPU-5 SMT sibling. At 512 MiB, one closed cycle already has
134,217,728 indices, so the runner retains that full traversal rather than
changing the operation to meet a smaller requested budget.
