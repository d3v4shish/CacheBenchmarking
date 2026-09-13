# Hotspots and controls

- Scalar float/double reductions have a loop-carried accumulator dependency.
  v2 unroll diagnostics isolate loop overhead from that dependency.
- Large footprints may be limited by cache capacity or memory bandwidth.
  v2 records size order randomly and retains selected cache/TLB diagnostics.
- CPU 4's SMT sibling, interrupts, temperature, and scheduler activity can
  perturb a pinned task. v2 retains telemetry and scheduler counters.
- Allocation/fill can distort outer process PMU counts. v2 starts counters only
  after initialization.
- A repeated `1.0f` sum can cease to change at high magnitude. v2 uses a
  deterministic balanced FP pattern and validates checksums.

## Stride-16 language control hotspots

- The useful payload is 4 B but the architectural line is 64 B. Above L1,
  expected L1 miss pressure is approximately one line per useful load; source
  language cannot recover the 15 unused words.
- Near 256 KiB and 6–8 MiB, p10–p90 timing spread is material. Retain it and
  do not call small median differences a language victory.
- Go retires roughly seven instructions/load while C/C++/Rust retire four in
  the selected scalar loop. That is visible while cache-resident; at 512 MiB
  line movement dominates and the cycle difference falls within 2.1%.

## v2 observations

- At 64 MiB, C++ and Rust scalar float are tied within the retained PMU spread:
  3.129840 and 3.129640 median cycles/element respectively, each with 4.0
  instructions/element. The v1 “extra Rust FP work” conclusion does not hold
  under the normalized v2 implementation and in-scan measurement boundary.
- Explicit Rust AVX2 is close to C++ AVX2 at 64 MiB: float is 0.677382 versus
  0.670716 cycles/element, and int is 0.673668 versus 0.664914.
- Scalar FP remains dependency-bound near 3 cycles/element. Manual unroll
  changes instruction count but leaves the same one-accumulator limit visible.
- The selected L1D and dTLB external probes multiplexed (49–50% running), so
  they are retained as failed probes and are not interpreted. LLC, branch, and
  scheduler probes ran at 100%; their outer-process boundaries include setup.

## Dependent-index-cycle control hotspots

- `index = next[index]` is a loop-carried address dependency: no iteration can
  discover its next address until the preceding 4-byte index load completes.
  Vectorization flags are therefore a code-generation negative control, not a
  source of memory-level parallelism.
- C/C++ retire roughly four instructions/index, Rust and Go about six. Rust
  is about 7% slower than C at 32 KiB (5.353 versus 5.001 cycles/index), but
  all languages converge within 0.3% at 512 MiB where serialized memory
  response dominates the extra instructions.
- Clang's same-source loop retires 1.375 instructions/index versus GCC's four,
  but both remain about 215 cycles/index at 512 MiB. Fewer instructions do not
  remove a DRAM-scale load-to-use dependency.

## Late-prefetch-chain control hotspots

- C, C++, and Rust all retain one current-node demand load, then emit a
  late PREFETCHT2 after the returned successor creates its address. Go calls
  an explicit one-instruction assembly helper; its roughly 47 instructions
  per node expose the runtime/call cost while cached.
- The paired GCC 13 on/off build isolates the key effect. The on loop retires
  12 instead of 9 instructions/node and is slower at 36 KiB, 256 KiB, 6 MiB,
  8 MiB, and 512 MiB. Its L1-resident 32 KiB inversion is not a general
  latency conclusion.
- Compiler vector-default policy cannot create SIMD or MLP: the successor
  address is loop-carried. At 512 MiB GCC and Clang policies remain within
  the same roughly 235-238 cycle/node serial-memory regime.
