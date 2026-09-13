# Stride-16 retest

## MLP8-cycle retest

- [x] Audit and run a fresh Haswell MLP8 capacity ladder.
  Contract: eight live node indices advance through the same seeded random
  64-byte-node cycle; report per-node time and do not call it a faster
  one-chain pointer chase.
  Validation: warm/cache-evicted basic and branch profiles, at least 15
  accepted CPU-pinned 250-ms rows per reported cell, checksum equality, and
  exact PMU time.
- [x] Run isolated raw L1/L2/L3/DTLB profiles at seven MLP8 landmarks.
  Contract: raw profiles are separate from timing and branch profiles.
  Validation: retain source/binary hashes, disassembly, randomized order, and
  at least six accepted exact-running samples per event and footprint.
- [x] Add C/C++/Rust/Go and same-source compiler MLP8 controls.
  Contract: every front end holds eight independent successor registers; no
  control may replace it with a scalar chain or data-parallel gather.
  Validation: retained source, disassembly, flags, and 15 accepted samples
  per language/compiler footprint cell.
- [x] Write the local self-contained MLP8 article using the linear-scan
  template.
  Contract: distinguish MLP from prefetch, preserve complete code/data/PMU
  evidence, chart the latency overlap, animate actual Haswell behavior, and
  mark expectation verdicts.
  Validation: local Astro check, production build, asset links, and no remote
  publication.
  Status: the local article, evidence assets, code snapshots, exact PMU data,
  language/compiler/pair tables, and measured-behaviour SVG were staged at
  the MLP8 route. Astro check and static production build passed on
  2026-09-10; nothing was pushed or published remotely.

## Prefetch-chain retest

- [x] Audit and run the Haswell scalar capacity ladder for `prefetch_chain`.
  Contract: retain the 64-byte arena-node cycle used by pointer chase, then
  issue exactly `__builtin_prefetch(&values[next], 0, 1)` only after the
  current node reveals `next`; do not describe this as look-ahead or SIMD.
  Validation: retain warm/cache-evicted basic and branch rows, 15 accepted
  repetitions per cell, CPU pinning, 250-ms minimum duration, checksum, and
  exact-running PMU groups.
  Status: the first 15-attempt diagnostic artifact retained 15 timed-fault
  rows, leaving a few cells below 15 accepted rows; it is deliberately not
  used as a result. The rerun uses 18 attempts and an independent validator
  requiring at least 15 accepted rows in every cell.
- [x] Run isolated raw L1/L2/L3/DTLB profiles at the seven capacity landmarks.
  Contract: raw-event passes remain separate from timing/basic/branch rows.
  Validation: six accepted repetitions per event/footprint, exact running
  time, source/binary hashes, disassembly, order, and independent validation.
  Status: the first invocation stopped before its first sample because its
  shuffle formatter passed a malformed footprint; the formatter is corrected.
  Its following six-attempt run exposed four groups with one timed-fault row,
  so it is retained as diagnostic-only; the clean run uses eight attempts and
  requires at least six accepted rows in every group. An eight-attempt data
  collection then exposed a post-run `awk` option-order bug before finalization;
  its unfinalized artifact is retained separately and the corrected runner uses
  a new directory.
- [x] Add and run C/C++/Rust/Go plus same-source compiler controls before
  writing the article.
  Contract: every front end has the same random 64-byte node cycle and the
  same deliberately late one-hop prefetch; Go's implementation must make any
  assembly helper explicit rather than silently omitting the operation.
  Validation: retain sources, flags, disassembly, 15 accepted samples/cell,
  and separate language/compiler artifacts.
- [x] Write the self-contained local article in the linear-scan template.
  Contract: include complete code shapes, raw evidence, exhaustive tables,
  PMU/hotspot analysis, animation, expectation verdicts, and local assets.
  Validation: all claims derive from retained DUT data; local Astro check and
  production build pass. No remote publication is performed.
  Completed 2026-09-10: the accepted scalar and raw artifacts, two 420-row
  controls, and the 210-row same-source prefetch-on/off control are staged
  locally. Astro check and static production build passed; nothing was pushed.

## Dependent index-cycle retest

- [x] Run the Haswell GCC scalar capacity ladder for `dependent_index_cycle`,
  retaining warm/cache-evicted timing, basic/branch PMU rows, and a separately
  scheduled raw cache/TLB supplement.
  Contract: `index = next[index]` is one strict 4-byte-index load-to-use
  chain; do not conflate it with 64-byte arena nodes or direct random reads.
  Validation: accepted rows are pinned, at least 250 ms, fault-free, checksum
  correct, and exact-running in their individual PMU group.
- [x] Run four-language and same-source compiler controls at capacity
  landmarks.
  Contract: every control constructs the same deterministic closed cycle.
  Validation: 15 accepted repetitions per control cell, exact PMU running
  time, source/binary hashes, and independent local hash/matrix validation.
- [x] Publish the self-contained dependent-index article with sources, raw
  data, tables, animation, and hotspot/assembly explanation.
  Contract: use the corrected language `analysis/` summary and preserve every
  capacity, raw-PMU, and control artifact.
  Validation: all local public assets resolve; Astro check and production
  build pass (2026-09-10). The article includes the shared experiment
  template's code-shape matrix, exhaustive per-cell tables, follow-ups, and
  conclusion sections. No remote publication was performed.

## Arena pointer-chase retest

- [x] Audit and run the Haswell GCC scalar capacity ladder for
  `arena_pointer_chase`, retaining warm/cache-evicted timing, basic/branch PMU
  rows, raw cache/TLB passes, and rejected rows separately.
  Contract: each 64-byte node load must expose a true load-to-use `next`
  dependency; do not conflate it with random direct indexing or MLP8.
  Validation: analyser acceptance requires CPU pinning, at least 250 ms,
  fault-free samples, correct checksum, and exact PMU running time.
- [x] Run the C/C++/Rust/Go source-level control at seven capacity landmarks.
  Contract: each front end constructs the same seeded, 64-byte-aligned closed
  node cycle and times only `node = nodes[index]; index = node.next`.
  Validation: retain 15 randomized, exact-PMU, fault-free samples per
  language/footprint; build disassembly and source hashes alongside the CSV.
- [x] Run a same-source GCC/Clang scalar-versus-auto compilation control.
  Contract: compiler configurations may change surrounding code but cannot
  remove the strict pointer dependency or turn it into SIMD.
  Validation: retain the flags, binaries, disassembly, order and accepted
  sample matrix separately from language controls.
- [x] Publish the pointer-chase article with the established self-contained
  template: complete code paths, capacity/PMU charts, assembly/hotspot view,
  animated hierarchy explanation, and explicit expectation callouts.
  Validation: all reported values come from retained DUT evidence; Astro check
  and production build pass.

## Random-access retest

- [x] Add a self-contained C/C++/Rust/Go random-direct-load control matrix.
  Contract: all languages use the same deterministic permutation, data fill,
  checksum semantics, and in-scan `cycles/instructions/ref-cycles` group.
  Validation: every retained sample has a correct checksum and exact PMU run
  time; raw records, order, sources, and hashes are retained.
- [x] Run the Haswell random-access capacity ladder and its raw cache/TLB PMU
  passes, plus a separately labelled MLP control.
  Contract: do not conflate independent random reads with dependent pointer
  chains or the MLP control; retain cache states and rejected rows.
  Validation: the existing analyser accepts only non-migrated, 250-ms,
  fault-free, exact-counter samples.
- [x] Publish a separate self-contained random-access article.
  Contract: show every executed language path, complete tables/charts,
  assembly/hotspots, cache/TLB evidence, an animated hierarchy model, and
  explicit expectation-versus-result callouts.
  Validation: public raw assets and sources resolve locally; Astro check and
  production build pass.

- [x] Add a narrow, deterministic DUT invocation for `stride16_scan` that
  records only the requested mode and implementation.  Contract: retain every
  250-ms sample, exact-running PMU counter row, machine manifest, disassembly,
  and randomized order without changing DUT controls.  Validate with the
  source-contract check and a local smoke run.
- [x] Build and run GCC 13 and Clang 18 Haswell `-O3` scalar/vector-off and
  auto/vector-default variants on the DUT.  Contract: 15 accepted repetitions
  per footprint, state, and PMU profile; do not claim results for a rejected
  row.  Validate with the analyser and quality summaries.
- [x] Publish a self-contained stride-16 article.  Contract: explain the
  operation, expectation, hardware/run controls, code, raw-derived timing and
  PMU summaries, charts, and matched/not-matched conclusions.  Validate the
  blog build and preserve the older all-cases historical note.
- [x] Update the deterministic workflow and evidence documentation with the
  actual accepted run paths, method, and observed hotspots.  Validate links,
  commands, and generated files after the run.
