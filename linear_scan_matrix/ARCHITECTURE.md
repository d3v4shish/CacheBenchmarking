# Architecture

`src/` contains per-language scan kernels and a small C PMU library. All
binaries take `type`, `implementation`, `bytes`, and `passes`, allocate/fill
before measurement, then print one machine-readable record.

`scripts/run_on_haswell_dut.sh` builds retained binaries, records environment
and disassembly, generates and shuffles the case list, and writes raw records.
`scripts/validate_results.sh` enforces the raw-data contract.
`scripts/analyze_results.sh` derives tables without mutating raw evidence.

No network input or mutable benchmark data is used. The only external boundary
is the explicit DUT execution and its unique retained result directory.

## Stride-16 language control

`stride16_c.c`, `stride16_cpp.cpp`, `stride16_rust.rs`, and `stride16_go.go`
are separate language front ends over the same kernel contract: a `uint32_t`
load at a 16-word/64-byte interval. They call the same C `pmu_scope` boundary
but do not call one another. The dedicated runner retains raw rows, order,
machine state, disassembly, and hashes; its analyser derives medians without
changing raw evidence.

## Dependent-index controls

`index_{c,cpp,rust,go}` are independent front ends over one contract: build a
seeded closed `uint32_t` cycle and time only the dependent `next[index]`
recurrence. The language runner records four front ends; the compiler runner
compiles only `index_cpp.cpp` with GCC/Clang scalar/default policies. Both
retain raw rows, shuffled order, machine state, disassembly, and hashes
separately from the working-set capacity ladder.
