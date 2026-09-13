# v2 TODO

## MLP8-cycle language and compiler controls

- [x] Add four native C/C++/Rust/Go MLP8 front ends and run the locked-Haswell
  language matrix.
  Contract: every timed loop maintains eight independent live successor
  indices on the same seeded 64-byte-node cycle; no source may substitute a
  single chain, a direct random load, prefetch, or SIMD gather.
  Validation: seven landmarks × four languages × 15 shuffled rows, each at
  least 250 ms with matching checksum and exact cycles/instructions/ref-cycles
  PMU running time; retain source hashes and disassemblies.
- [x] Run same-source GCC 13/Clang 18 scalar/auto compiler controls and a
  GCC-13 lane-count pair (one lane versus eight lanes).
  Contract: policies may change code generation only; the pair changes only
  the number of independently live chains.
  Validation: retain 420 compiler and 210 pair rows, flags, hashes,
  disassemblies, and validators that reject incomplete or multiplexed PMU
  rows.
  Status: the first compiler artifact stopped before its third row because
  Clang's 32 KiB loop took 219.3 ms; it is retained as diagnostic-only. The
  clean rerun raised just the two L1 targets and accepted all 420 compiler
  rows. The 420 language rows and 210 lane-pair rows also passed their
  independent validators on 2026-09-10.

## Prefetch-chain language controls

- [x] Add and run the four native C/C++/Rust/Go late-prefetch front ends.
  Contract: each uses the same seeded random 64-byte-node cycle and issues
  exactly one software-prefetch request only after the current `next` field
  has loaded; Go's explicit assembly helper must retain `PREFETCHT2`.
  Validation: all 420 rows are CPU-pinned, at least 250 ms, checksum-correct,
  exact-running in their PMU group, with sources and disassembly retained.
- [x] Run the same-source GCC/Clang compiler-policy control.
  Contract: compiler policies may change loop control but cannot turn the
  late one-hop prefetch into independent look-ahead or SIMD.
  Validation: retain 420 valid rows, flags, hashes, and disassembly.

- [x] Run a same-source GCC 13 scalar prefetch-on/off paired control.
  Contract: only PREFETCH_ENABLED changes; the off binary must contain no
  PREFETCHT2 and the on binary must retain it.
  Validation: 210 accepted rows (2 variants × 7 footprints × 15), exact
  in-scan PMU groups, hashes, and both disassemblies.

## Dependent-index-cycle controls

- [x] Add and run four deterministic `uint32_t` closed-cycle language front
  ends plus a same-source GCC 13/Clang 18 compiler-policy control.
  Contract: one `index = next[index]` load-to-use dependency per timed
  operation; no direct-index, arena-node, or MLP substitution.
  Validation: the retained Haswell artifacts each have 420 accepted rows (28
  cells × 15), exact PMU running time, correct checksums, hashes,
  disassemblies, randomized order, and independent local validators.
- [x] Publish the dependent-index article locally after owner review.
  Contract: use all retained ladder/raw-PMU/language/compiler evidence and the
  corrected language `analysis/` reference-cycle derivation.
  Validation: local public artifact links, Astro check, and production build
  pass (2026-09-10). No remote publication was performed.

## Stride-16 language comparison for the working-set article

- [x] Add independently readable C, C++, Rust, and Go scalar stride-16
  kernels.
  Contract: every kernel touches `uint32_t[0], [16], [32]...`, checks the
  same modulo-32-bit result, and places allocation/fill outside its timing and
  PMU scope.
  Validation: all four binaries emit identical operation counts and valid
  in-scan `cycles/instructions/ref-cycles` groups on a small local input.
- [x] Run a selected seven-footprint, fifteen-repetition matrix on the locked
  Haswell DUT.
  Contract: order is deterministic but shuffled; every retained counter group
  has `time_running == time_enabled`, a valid checksum, and a recorded
  source/binary hash.
  Validation: the dedicated validator rejects an incomplete, duplicate,
  multiplexed, or checksum-mismatched matrix.
- [x] Replace the article's single-language code presentation with all four
  executed kernels and the retained language comparison.
  Contract: each code block has a direct expectation and an explicit matched
  or not-matched conclusion backed by the new table; existing capacity and
  raw-PMU evidence stays published.
  Validation: article links, CSV assets, syntax highlighting, animated
  hierarchy SVG, and the Astro production build are checked locally.

- [x] Add a shared in-scan PMU scope for C, C++, Rust, and Go.
  Contract: counters begin after allocation/fill and cover the scan only.
  Validation: one non-multiplexed `cycles/instructions/ref-cycles` group and
  matching timer/PMU fields are emitted by every binary.
- [x] Normalize v2 input semantics.
  Contract: FP inputs are deterministic and balanced; all languages scan
  `string16` records rather than mixing records and flat byte slices.
  Validation: every case emits its known checksum.
- [x] Implement deterministic build, run, validation, analysis, and selected
  profile commands.
  Contract: a unique v2 directory contains raw samples, PMU records,
  manifests, and hashes; every primary group has 15 reps.
  Validation: `scripts/validate_results.sh` rejects missing, duplicate, or
  multiplexed records.
- [x] Add Rust explicit AVX2, C++ native-vector, Clang C++, and C++/Rust
  unroll diagnostics.
  Contract: each is separately named and its executed disassembly is retained.
  Validation: the requested SIMD or unroll shape is visible in the artifact.
- [x] Run the v2 primary matrix and selected profile/event passes on the DUT.
  Contract: CPU/frequency/PMU checks pass, ordering is fully shuffled, and
  telemetry is retained.
  Validation: build, validator, analysis, and article staging all pass.
- [x] Update the public blog article from retained v2 artifacts.
  Contract: replace v1 at the existing route; explain the test, expectations,
  code shapes, executed assembly, v2 results, charts, PMU boundary, and
  qualified diagnostics without mixing v1 and v2 values.
  Validation: the Astro production build passed and the local preview rendered
  the v2 article, both inline SVGs, syntax-highlighted code, and the complete
  64 MiB case matrix on 2026-09-09.
- [x] Enrich the v2 article without dropping any measured material.
  Contract: add fully annotated C, native C++, Rust, and Go code shapes,
  contextual coloured evidence boxes, and a clearly labelled same-experiment
  follow-up matrix that does not present unrun work as a result.
  Validation: Astro check, content audit, and static generation passed on
  2026-09-09; the local preview returned HTTP 200 with every prior
  section/table/chart, explicit green matched/red non-matched callouts, both
  animations, and 5 highlighted code blocks. The final site-wide audit remains blocked by an unrelated
  generated working-set-ladder route missing canonical/PNG social metadata.
- [ ] Publish the complete retained raw-artifact bundle as public blog assets.
  Contract: expose individual samples, normalized PMU records, order, machine
  manifest, source, disassembly, and diagnostic text only after the owner
  explicitly selects and approves the exact public payload.
  Validation: published files must hash-match the retained DUT artifacts and
  every public link must be checked from the local preview.
