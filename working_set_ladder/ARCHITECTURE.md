# Architecture

`src/working_set_ladder.cpp` creates deterministic data, warms setup outside
the timed region, runs one access pattern, checks its checksum, and appends one
timing row plus long-form PMU rows. `stride16_scan` reads `values[0]`,
`values[16]`, and so on: one four-byte value per 64-byte cache line.
`random_access` instead builds a deterministic query permutation and executes
`values[query]`; its retained physical footprint includes both payload and
query arrays. `dependent_index_cycle` stores the next random position as a
4-byte entry and executes `index = next[index]`, exposing exactly one
load-to-use address dependency. `mlp8_cycle` is a distinct eight-chain
64-byte-node workload.

`scripts/generate_stride16_selected_matrix.sh` identifies the four selected
compiler products; `scripts/generate_build_matrix.sh` retains the broader
research matrix;
`scripts/build_selected.sh` materializes one row and its build manifest.
`scripts/run_working_set_on_dut.sh` is the non-mutating DUT gate and runner.
`src/analyze_working_set.cpp` rejects short, migrated, faulting, or multiplexed
rows before computing median/p05/p95 summaries. The blog article consumes only
accepted summaries; raw CSV and manifests remain retained benchmark evidence.
