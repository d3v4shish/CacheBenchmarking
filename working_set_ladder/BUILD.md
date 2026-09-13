# Build

Generate the selected C++ compiler matrix, then build exactly one identified
row.  A build directory must be new and absolute.

```bash
scripts/generate_stride16_selected_matrix.sh /tmp/stride16-matrix.tsv
scripts/build_selected.sh /tmp/stride16-matrix.tsv stride16-gcc13-scalar /tmp/wsl-build
```

The selected run uses four Haswell `-O3`, non-PGO, no-LTO, system-allocator
rows: GCC 13 scalar/vector-off, GCC 13 auto/vector-default, Clang 18
scalar/vector-off, and Clang 18 auto/vector-default.  The manifest records the
exact command, compiler version, macros, source/binary hashes, and disassembly.

For a code-only smoke check:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror src/working_set_ladder.cpp -o /tmp/working_set_ladder
taskset --cpu-list 0 /tmp/working_set_ladder --mode stride16_scan --implementation scalar --state warm --perf-profile none --build-id smoke --footprint-bytes 65536 --round 0 --expected-cpu 0 --min-duration-ms 5 --seed 7 --output /tmp/samples.csv --counter-output /tmp/counters.csv
```

This smoke result is never benchmark evidence.

After accepted DUT evidence has been copied to `results/stride16_v3`, derive
the publication tables without changing raw CSVs:

```bash
scripts/export_stride16_article_data.sh "$(pwd)/results/stride16_v3"
```

The random-access MLP8 control is intentionally a separate invocation: it
compares eight dependent node chains with direct random indexing and must not
mix its rows into the direct-index ladder.

```bash
scripts/run_mlp8_control_on_haswell_dut.sh /home/d3v/mlp8-control-run-UNIQUE /absolute/identified-build-dir
```

It uses the same CPU-4/fixed-frequency/PMU gate, requires CPU 5 offline, runs
15 shuffled samples at seven landmarks for at least 250 ms each, and writes
its own raw samples, counters, summary, hashes, and disassembly.

The dependent-index raw-PMU supplement is a separate exact-event pass. First
create the identified GCC scalar build, then run:

```bash
scripts/run_index_selected_raw_on_haswell_dut.sh \
  /home/d3v/working-set-ladder-index-raw-UNIQUE /absolute/identified-build-dir
```

It emits six shuffled repetitions of seven landmarks under each of `l1_miss`,
`l2_miss`, `l3_miss`, and `dtlb_walk`; each profile carries cycles plus only
its own raw Haswell diagnostic event.
