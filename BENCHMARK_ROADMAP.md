# Cache, SIMD, DSA, CPU Pipeline, Instruction, Synchronization, Linux I/O, and Storage-Engine Benchmark Roadmap

Status: design plan, not yet executed  
Last updated: 2026-09-07  
Primary DUT: `lenovo`, Intel Core i7-4702MQ (Haswell), Linux `7.0.0-28-generic`

## 1. Purpose and scope

This roadmap defines the next pure benchmarking program for `slash-tmp.blog`. It deliberately excludes application-project benchmarks such as ClamAV and miniz. The unit of work is an explicit, falsifiable experiment covering one of:

- Cache and memory-access behavior.
- SIMD and scalar implementation choices.
- Data structures and problem-shaped DSA questions.
- Atomics, locks, futexes, semaphores, and concurrent queues.
- System-call, IPC, file-I/O, socket-I/O, `io_uring`, and copy-avoidance mechanisms.
- BLAS-like dense and sparse numerical kernels.
- Single-thread, multithread, and multiprocess execution.
- Compiler/code-shape costs: allocation, calls, dispatch, inlining, code layout, and instruction-front-end pressure.
- Instruction-level costs: latency, reciprocal throughput, decoded-uop count, execution-port pressure, operand-dependent timing, dependency breaking, register-domain crossings, decode/uop-cache behavior, and memory-operand effects.
- Database and ordered-storage mechanisms: B+ trees, LSM/SSTables, WALs, compaction, durability, and controlled MySQL/MongoDB comparisons.

Each experiment below contains an exact test, a hypothesis, and a test plan. Results should answer a decision question, not merely rank opaque implementations.

## 2. DUT facts and experimental limits

### 2.1 CPU and topology

- Intel Core i7-4702MQ, Haswell, x86-64, AVX2 and FMA.
- Four physical cores and eight logical CPUs system-wide.
- Isolated benchmark CPUs 4–7 represent only two physical cores:
  - CPUs 4 and 5 are SMT siblings.
  - CPUs 6 and 7 are SMT siblings.
- Per core: 32 KiB L1 data cache and 256 KiB L2.
- Shared LLC: 6 MiB.
- One NUMA node.
- Primary single-thread placement: CPU 4 with CPU 5 idle.
- Primary two-thread placement: CPUs 4 and 6.
- Explicit SMT comparison: CPUs 4 and 5.
- Four-logical-thread comparison: CPUs 4–7; label this as two cores/four SMT threads, never four-core scaling.

### 2.2 Current operating-system controls

- Boot parameters include `isolcpus=domain,managed_irq,4-7`, `nohz_full=4-7`, `rcu_nocbs=4-7`, and housekeeping IRQ affinity on CPUs 0–3.
- `perf_event_paranoid=0`; normal performance counters work.
- `irqbalance` and `tuned` are inactive.
- Transparent huge pages are in `madvise` mode.
- The governor is `performance`, but turbo remains enabled; actual frequency and thermal state must still be measured.

### 2.3 Storage and I/O facts measured on 2026-09-06

- Root filesystem: ext4 on `/dev/sda2`.
- Device: Crucial BX500 `CT240BX500SSD1`, approximately 240 GB SATA SSD.
- About 25 GB was free and the filesystem was 89% full.
- RAM: 15 GiB, with about 13 GiB available during inventory.
- Swap: 4 GiB, unused during inventory.
- Kernel configuration includes `CONFIG_IO_URING=y`, `CONFIG_IO_URING_ZCRX=y`, `CONFIG_FUTEX=y`, `CONFIG_FUTEX_PI=y`, and `CONFIG_USERFAULTFD=y`.
- `kernel.io_uring_disabled=0`.
- User memlock limit was approximately 1.94 GiB, sufficient for moderate registered-buffer tests.
- `perf`, `taskset`, and `numactl` are present.
- `fio` and a discoverable `liburing` development installation were absent.

### 2.4 Network facts measured on 2026-09-06

- Active DUT interface: `enx000010027e2d`.
- Adapter: ASIX AX88772B USB 2.0 Ethernet.
- Negotiated speed: 100 Mb/s full duplex.
- Built-in Realtek Ethernet and Atheros Wi-Fi interfaces were down.
- The active link can test API overhead and latency but is too slow to characterize high-throughput network copy avoidance.
- Linux loopback must not be used to claim `MSG_ZEROCOPY` gains because the kernel normally falls back to deferred copying on loopback.
- Modern io_uring zero-copy receive requires NIC features that this adapter is unlikely to provide. Probe and report unsupported rather than silently substituting another path.

## 3. Universal experiment contract

### 3.1 Execution profiles

Every applicable experiment uses these named profiles:

| Profile | Placement | Meaning |
|---|---|---|
| `solo_physical` | CPU 4; CPU 5 idle | Primary single-thread result |
| `two_physical` | CPUs 4 and 6 | Clean two-core scaling |
| `smt_pair` | CPUs 4 and 5 | Shared-core contention |
| `two_core_smt` | CPUs 4–7 | Maximum isolated logical CPUs |
| `two_process_physical` | one process on CPU 4 and one on CPU 6 | Thread/process comparison |
| `normal_system` | CPU 4 with normal background services | Sensitivity result only |

Primary publication runs require AC power, a stable cooling position, no unrelated benchmark-core work, verified IRQ placement, and a logged frequency profile.

### 3.2 Cache-footprint ladder

Size tests by bytes actually touched:

- L1 bracket: 8, 16, 24, 28, 32, 36, 48, 64 KiB.
- L2 bracket: 96, 128, 192, 224, 256, 288, 384, 512 KiB.
- LLC bracket: 1, 2, 4, 5, 6, 7, 8, 12 MiB.
- DRAM: 32, 128, 512 MiB, and optionally 2–8 GiB for file/page-cache experiments.

### 3.3 Standard data distributions

Use applicable subsets of:

- Sorted ascending, descending, and nearly sorted with 0.1%, 1%, and 10% swaps.
- Sequential and random permutation.
- Uniform random.
- Zipfian with exponents 0.8, 1.0, and 1.2.
- Explicit 80/20 and 95/5 hot sets.
- Clustered, phase-changing, and random-walk locality.
- Low cardinality: 2, 16, 256, and 65,536 distinct values.
- Adversarial low-bit, cache-set, collision, monotonic, alternating-extreme, and periodic patterns where relevant.
- Five fixed seeds per distribution for the core matrix.

### 3.4 Standard operation dimensions

- Hit rate: 0%, 10%, 50%, 90%, and 100%.
- Read/write mix: 100/0, 95/5, 80/20, and 50/50.
- Batch size: 1, 4, 8, 16, 32, 128, and 1,024 where meaningful.
- Payload: 0, 4, 16, 64, 128, and 256 bytes.
- Key: 32-bit integer, 64-bit integer, 16-byte key, short string, and long string.
- Lifecycle: newly built, warm, aged, fragmented, tombstone-heavy, immediately post-resize, and steady state.
- Reuse: 1, 8, 64, 1,024, and N operations per build/preprocessing step.

### 3.5 Compilation matrix

- Scalar reference: vectorization explicitly disabled and no intrinsics.
- Normal compiler-optimized C++: `-O3 -march=haswell -mtune=haswell`.
- Auto-vectorized compiler implementation.
- Explicit AVX2/FMA implementation where legal and relevant.
- GCC and Clang comparison for representative kernels.
- Debug/ASan/TSan binaries are correctness tools only and never publication timing binaries.

Record compiler version, complete flags, source commit, dirty-tree state, executable hash, dependency versions, and relevant disassembly. Save compiler vectorization reports for SIMD claims.

### 3.6 Timing protocol

- Generate input, allocate storage, and prefault pages outside the timed region unless those actions are the subject of the experiment.
- Verify output after the timed region with a reference implementation and checksum/materialized output.
- Warm up for at least 500 ms; extend warm-up if frequency or steady state has not settled.
- Each timed sample should normally last at least 250 ms.
- Use 15 randomly interleaved repetitions for publication rows and retain every raw repetition.
- Use batched timing for operations too small to measure individually.
- Report median, MAD, p05, p95, bootstrap confidence interval, `ns/op`, `cycles/op`, operations/s, and useful bytes/s.
- For latency-sensitive queues, locks, and I/O, retain histograms and report p50, p90, p99, p99.9, and maximum alongside throughput.

### 3.7 Counter protocol

Use separate, non-multiplexed passes where possible:

1. `cycles`, `instructions`, `branches`, `branch-misses`.
2. `TopdownL1` alone, then `TopdownL2` alone: Retiring, Bad Speculation, Front-End Bound, Back-End Bound, fetch latency/bandwidth, Memory Bound, and Core Bound.
3. Front-end drill-down using the DUT-supported metric groups `Frontend`, `FetchLat`, `FetchBW`, `IcMiss`, `DSB`, `DSBmiss`, `BrMispredicts`, `MachineClears`, and `MicroSeq`.
4. Back-end drill-down using `Backend`, `MemoryBound`, `MemoryLat`, `MemoryBW`, `MemoryTLB`, `PortsUtil`, and the relevant load/store split, store-forwarding, and cache-level events.
5. L1 data loads/misses, LLC loads/misses, dTLB loads/misses, page faults, context switches, and CPU migrations.
6. Task clock, system/user CPU time, voluntary/involuntary context switches for blocking and I/O tests.
7. Optional energy and effective frequency using RAPL/turbostat when permission is available.

The 2026-09-06 DUT probe successfully produced `TopdownL1` and `TopdownL2`, but a 30-ms smoke workload multiplexed individual events down to roughly 14–42% running time. Publication passes must therefore run at least 250 ms, collect small compatible event groups separately, record `time_enabled/time_running`, and reject materially multiplexed rows. Never add raw event percentages as if they were mutually exclusive; use the supported top-down formulas.

### 3.8 Result acceptance

A performance difference is material when:

- Correctness and semantic contracts are identical.
- Median difference is at least 5%.
- The confidence interval of the ratio excludes 1.0.
- The direction persists at two adjacent sizes or multiple seeds.
- No migration, thermal throttling, unexpected timed page fault, short I/O, or fallback path invalidated the sample.

Publish negative and unsupported results. Never silently replace an unavailable io_uring, zero-copy, IOPOLL, or hardware feature with a different mechanism.

### 3.9 Instruction-microbenchmark construction protocol

Instruction tests in Section 27 use Agner Fog's definitions and test-program structure, with the following publication rules:

- **Latency** means core cycles added along a true dependency chain. Construct a closed chain whose output is the next dynamic instruction's input, unroll enough copies to dominate loop overhead, subtract a structurally matched control, and divide by the number of dynamic test instructions. Do not call wall-clock time divided by unrelated independent instructions “latency.”
- **Reciprocal throughput** means average core cycles per instruction for mutually independent instances in steady state. Use enough independent register chains to hide the measured instruction's latency, sweep the number of chains until the result stops improving, subtract a matched loop/control cost, and divide by retired dynamic test instructions.
- **Cross-domain latency** is reported as a measured round trip when no instruction can close a dependency chain by itself. For example, measure A→B→A and report the combined latency; never assign the total arbitrarily to one edge.
- **Memory-read latency** uses a serialized shuffled pointer chase so the next address is unknown until the previous load completes. Arithmetic on known addresses measures issue throughput, memory-level parallelism, or prefetching—not isolated load-use latency.
- **Store forwarding** uses a store followed by a dependent overlapping load and a consumer, compared with an exact-width/alignment/addressing control. This measures the store-to-load forwarding path. It is not a physical “store latency,” which cannot be isolated this way.
- **Core cycles, not TSC ticks**, are the primary denominator. Record both. Validate the core-cycle event against effective frequency and reject throttling, migration, or materially multiplexed samples.
- Put at least 100 static test instructions in a generated body where code-size effects are not the subject; otherwise sweep body size explicitly. Measure an empty or neutral body with the same loop branches, counter updates, register pressure, and alignment.
- Use separate counter passes when required. Timing, instructions/uops, port utilization, front-end delivery, branches, and cache/TLB events need not fit into one counter group. Record `time_enabled/time_running` and never compare differently multiplexed groups as exact totals.
- Inspect and archive the final machine code. Record opcode bytes, prefixes, operand form, register class, vector/element width, immediate value, static alignment, and dynamic instruction count. Reject compiler-inserted spills, extra domain transitions, or reassociation unless they are the subject.
- Interleave the test and its control, randomize variant order, run at least 15 retained repetitions after warm-up, and repeat across sessions. A negative control must be capable of showing no penalty; a positive control must provoke the expected class of penalty.
- Operand-dependent instructions require explicit value classes rather than random-only data. For division, square root, subnormals, assists, and variable-count operations, publish a distribution or latency surface rather than a single “the latency.”
- Unsupported ISA rows are capability results, not zeros. Execute CPUID/XCR0 gating before emitting or calling a variant; on this Haswell DUT, AVX-512, scatter, AVX-512 mask, and FP16 scripts remain `unsupported_on_dut` rather than faulting or being emulated.

### 3.10 Executable-source readability and provenance contract

Every source file, script, query, build recipe, visualization generator, and analysis program that is executed for this roadmap must be written for auditability by a curious beginner, not merely for an experienced performance engineer. “Heavily commented” here means that a reader can connect the code to the experiment without trusting the author.

- Begin each executable source file with a plain-language header: batch/test IDs, decision question, semantic contract, hypothesis, measurement equation, expected failure modes, safety constraints, expected output files, and sources with stable URLs/section names.
- Before every non-obvious block, explain in simple language: what data enters, what it produces, why that construction isolates the intended effect, what would invalidate it, and which source or architectural rule motivates it. Assembly, compiler barriers, timing boundaries, affinity, PMU events, cache eviction, randomization, checksum, and statistical calculations require comments even when they look familiar to an expert.
- For tight timed loops, comment the loop immediately above it and comment each instruction/statement whose placement affects dependencies, register state, memory ordering, code shape, or dynamic count. Keep comments outside the timed region so explanation never changes the benchmark.
- Use numbered source references in code comments, with the full primary-source URLs in the file header. A source is evidence for a method or hardware claim, not a substitute for measured DUT evidence.
- Generate and retain an annotated disassembly sidecar. The source comment must state the intended opcode/dependency pattern; the article must show whether the final binary actually matches it.
- Explain every command in executed shell scripts, including environment variables, file paths, CPU placement, permissions, and cleanup behavior. Scripts must fail closed on missing tools, unsupported PMU/ISA, unsafe paths, wrong affinity, or incomplete result files.
- Separate `setup`, `timed work`, `verification`, and `reporting` in code and comments. Allocation, random generation, printing, checksums, and file I/O are outside timing unless they are explicitly the subject.
- No opaque generated file is accepted without its generator, inputs, version/hash, and a readable generated excerpt/disassembly. No notebook, chart, animation, or article may hide a transformation from raw result to plotted value.
- A review checklist accompanies each batch: a beginner can identify the question, input/output, comparison/control, expected result, source basis, measurement denominator, and invalidation conditions for every executable artifact. Failure of this checklist blocks execution/publication until corrected.

## 4. Measurement-validation experiments

### V01. Turbo versus fixed-frequency stability

**Test:** Compare arithmetic, L1 scan, L2 scan, LLC scan, and pointer chase under turbo-on and fixed/non-turbo profiles.

**Hypothesis:** Turbo improves headline latency but increases run-to-run variance and run-order bias.

**Test plan:** Run all five kernels for 15 randomized repetitions on CPU 4. Log frequency and temperature around every sample. Repeat after a ten-minute thermal soak. Establish a primary fixed-frequency profile and keep turbo-on as a separately labeled real-world profile.

### V02. Thermal-soak sensitivity

**Test:** Measure representative kernels cold, after five minutes of AVX2 work, and after fifteen minutes of memory plus AVX2 work.

**Hypothesis:** Laptop thermal state alters effective clock enough to change implementation rankings.

**Test plan:** Log frequency, temperature, cycles/op, and wall time every 30 seconds. Repeat in reverse order after cooling. Define maximum suite duration and cooldown trigger from the observed curve.

### V03. SMT and shared-resource interference

**Test:** Run a CPU-4 pointer chase while CPU 5 or CPU 6 independently executes arithmetic, streaming memory, pointer chasing, or AVX2.

**Hypothesis:** CPU-5 interference reveals SMT execution-resource contention; CPU-6 interference isolates shared LLC, memory-controller, and thermal effects.

**Test plan:** Sweep foreground footprint across every cache tier. Compare idle, sibling interferer, and other-core interferer while recording cycles and cache misses.

### V04. Background-system sensitivity

**Test:** Compare quiet benchmark conditions with normal GUI/X2go/Gitea activity.

**Hypothesis:** Isolation prevents migration but not LLC, memory, power, and thermal interference.

**Test plan:** Run L1 compute, LLC random lookup, and DRAM stream tests in both states. If the median moves over 3% or CV doubles, primary runs require the quiet profile.

### V05. Harness and timer overhead

**Test:** Measure empty loop, checksum, indirect dispatch, RNG/index generation, and clock-read paths.

**Hypothesis:** Small-N results can be dominated by framework work.

**Test plan:** Batch 1–1,024 logical operations per timing boundary. Require measured harness overhead below 1% or enlarge the batch. Keep random query arrays pre-generated.

### V06. PMU reproducibility and multiplexing

**Test:** Compare the same kernel with no counters, small counter groups, and a deliberately oversized counter set.

**Hypothesis:** Counter multiplexing and collection overhead distort short tests.

**Test plan:** Save `time_enabled/time_running`, timing overhead, and repeated values. Set the maximum safe event group and use multiple passes beyond it.

### V07. Warm, cache-evicted, and page-cold semantics

**Test:** Compare immediate repeat, repeat after a 24–32 MiB eviction walk, and repeat after page discard/reallocation.

**Hypothesis:** A single “cold” label incorrectly conflates cache, TLB, and fault cost.

**Test plan:** Publish `warm`, `cache_evicted`, and `page_cold` separately; verify page faults and dTLB counters.

### V08. Compiler and code-generation sensitivity

**Test:** Compile scan, search, hash probe, CAS loop, and gather kernels using GCC/Clang and scalar/auto-vector/AVX2 modes.

**Hypothesis:** Compiler transformations can create or erase the behavior supposedly being tested.

**Test plan:** Validate output and inspect disassembly. Record cycles/instructions. Fail rows containing unintended vectorization or optimized-away work.

## 5. Problem-shaped DSA experiments

### D01. Two Sum

**Test:** Determine whether any pair reaches a target.

**Hypothesis:** Hashing wins large one-shot unsorted inputs; sort plus two pointers wins with representation reuse; quadratic SIMD remains competitive only for small N.

**Test plan:** Compare scalar nested, blocked SIMD nested, hash build/query, sort+two-pointer, and pre-sorted query. Sweep N, duplicates, hit/miss, target position, and 1–1,024 queries per build.

### D02. Contains Duplicate

**Test:** Detect whether any duplicate exists.

**Hypothesis:** Bitmap wins bounded dense domains, sort wins compact reusable arrays, and hashing wins large sparse one-shot domains.

**Test plan:** Compare quadratic small-N, sort+adjacent, flat hash, node hash, byte bitmap, packed bitset, and radix detection. Control first-duplicate position and include no-duplicate worst cases.

### D03. Set intersection

**Test:** Materialize intersection of two integer sets.

**Hypothesis:** Merge wins similarly sized sorted inputs, galloping wins asymmetric inputs, hashing wins unsorted inputs, and bitmaps win past a density crossover.

**Test plan:** Compare hash, scalar merge, branchless merge, SIMD merge, galloping, dense bitmap, and blocked/compressed bitmap. Sweep size ratio 1:1–1:1,024, overlap, clustering, and cache footprint.

### D04. Set union and difference

**Test:** Materialize union and difference of sorted sets.

**Hypothesis:** Output traffic dominates high-output cases, reducing the advantage of branch/SIMD optimizations.

**Test plan:** Compare branchy, branchless, buffered SIMD, dense bitset, and compressed-bitset forms. Sweep overlap, output density, destination reservation, and in-place versus out-of-place contracts.

### D05. K-th largest and median selection

**Test:** Return one order statistic.

**Hypothesis:** `nth_element` wins broadly, heaps win extreme K, and complete sorting wins only with enough later ordered queries.

**Test plan:** Compare full sort, quickselect/`nth_element`, fixed heap, counting, and radix selection for `K={1,8,sqrt(N),N/2,N-1}` across standard distributions.

### D06. Top-K elements

**Test:** Return K largest elements under sorted-output and unordered-output contracts.

**Hypothesis:** Fixed heaps win tiny K; selection plus partial sort wins medium K; full sort wins near N.

**Test plan:** Compare binary/d-ary heap, fixed sorted buffer, `partial_sort`, `nth_element`+sort, radix candidates, and full sort. Sweep K/N and payload sizes.

### D07. Top-K frequent values

**Test:** Return the K most frequent keys.

**Hypothesis:** Cardinality, rather than N, selects the winning pipeline: dense histogram at low cardinality, hash+heap or hash+bucket at high cardinality.

**Test plan:** Compare sort+run count, flat hash+heap, hash+bucket, dense histogram, and radix histogram across cardinality, Zipf skew, K, and key width.

### D08. Deduplication

**Test:** Produce unique values with stable and order-free contracts.

**Hypothesis:** Hashing wins stable first-occurrence output, sort+unique wins order-free output, and bitmap/radix wins bounded domains.

**Test plan:** Treat stable and unordered results separately. Sweep duplicates, domain, order, payload, and memory budget. Record peak memory and allocation count.

### D09. Merge K sorted streams

**Test:** Merge K sorted arrays.

**Hypothesis:** Pairwise merge wins tiny K, heap wins moderate K, and tournament/loser trees improve locality at large K.

**Test plan:** Compare repeated pair merge, binary/d-ary heap, loser tree, and blocked merge for K=2–256, equal/unequal lengths, and overlapping/disjoint ranges.

### D10. Range sum with updates

**Test:** Answer range sums under point updates.

**Hypothesis:** Prefix sums dominate read-only workloads, Fenwick trees dominate mixed point updates, and segment trees justify their footprint only for richer aggregates.

**Test plan:** Compare scan, prefix, Fenwick, iterative segment tree, and blocked delta scheme across interval length, locality, and 100/0–50/50 query/update mixes.

### D11. Range minimum query

**Test:** Answer static and mutable range minima.

**Hypothesis:** Sparse tables win static high-query workloads, segment trees win mutable workloads, and direct scans win short ranges.

**Test plan:** Compare scan, blocked minima, sparse table, and segment tree. Sweep interval length, query reuse, clustered ranges, and update fraction.

### D12. Interval overlap and stabbing

**Test:** Count or return intervals intersecting a point/range.

**Hypothesis:** Sorted endpoints win static counts, interval trees win dynamic reporting, and SIMD scans win small N.

**Test plan:** Compare AoS scan, SoA endpoint scan, sorted endpoint arrays, interval tree, and augmented B-tree. Sweep overlap density, interval length, output cardinality, and update rate.

### D13. Streaming median

**Test:** Maintain a median under insertions.

**Hypothesis:** Two heaps win unbounded streams, order-statistic trees win with deletion, and histograms win bounded domains.

**Test plan:** Compare two heaps, ordered multiset/tree, Fenwick frequency structure, and bucket histogram on uniform, monotonic, and bursty input.

### D14. Sliding-window median

**Test:** Emit a median for every K-element window.

**Hypothesis:** Lazy-delete heaps give good average speed but unstable tail latency; indexed trees are steadier; histograms dominate low-cardinality data.

**Test plan:** Compare lazy heaps, multiset, Fenwick histogram, and sorted small buffer for K=8–65,536 and adversarial expiry patterns.

### D15. Longest increasing subsequence

**Test:** Compute LIS length.

**Hypothesis:** Tails-array `O(N log N)` wins medium/large N, while quadratic blocked/SIMD code can win very small N.

**Test plan:** Compare quadratic scalar, quadratic blocked/SIMD, and patience/tails search on random, sorted, reverse, sawtooth, low-cardinality, and nearly sorted input.

### D16. Inversion count

**Test:** Count inverted pairs.

**Hypothesis:** Merge counting has better locality than a Fenwick tree over compressed ranks despite similar asymptotic complexity.

**Test plan:** Compare quadratic small-N, merge counting, Fenwick, and blocked variants. Measure coordinate compression separately and end-to-end.

### D17. Edit distance

**Test:** Compute Levenshtein distance.

**Hypothesis:** Two-row DP reduces cache traffic; tiled/diagonal forms help large strings; bit-parallel methods dominate compatible length/alphabet regimes.

**Test plan:** Compare full matrix, two-row, tiled, diagonal wavefront, and bit-parallel methods across lengths, alphabet sizes, similarities, and unequal dimensions.

### D18. Knapsack and subset sum

**Test:** Solve 0/1 knapsack and subset feasibility.

**Hypothesis:** One-dimensional reverse DP improves locality; packed bitsets dominate feasibility; sparse-state methods win when reachable sums are sparse.

**Test plan:** Compare 2D DP, rolling 1D, packed-bitset shifts, and sparse sorted/hash states across capacity, item count, and state density.

### D19. Word frequency

**Test:** Count token occurrences.

**Hypothesis:** Allocation and string hashing dominate; string views or interned IDs plus flat storage reduce misses and allocations.

**Test plan:** Compare node hash, flat hash, sort+aggregate, radix trie, and intern-then-count. Measure tokenization-excluded and end-to-end forms across vocabulary, skew, and token length.

### D20. Group anagrams

**Test:** Group strings by character multiset.

**Hypothesis:** Fixed-alphabet count signatures beat sorting each long string; signature allocation can dominate grouping.

**Test plan:** Compare sorted-string signature, fixed count signature, rolling hash with verification, and radix grouping across string length, alphabet, group size, and storage strategy.

### D21. Prefix dictionary and autocomplete

**Test:** Return exact/prefix matches and top-K completions.

**Hypothesis:** Sorted strings win static compact data; radix/ART-style structures win repeated prefixes and dynamic updates.

**Test plan:** Compare sorted vector, pointer trie, compact radix trie, adaptive-radix-style nodes, and prefix hash. Sweep prefix length, shared-prefix depth, output count, and update mix.

### D22. Exact substring search

**Test:** Find one byte pattern.

**Hypothesis:** SIMD first-byte filtering wins short patterns, Boyer-Moore-Horspool wins long low-match patterns, and linear-time methods win adversarial repetitive text.

**Test plan:** Compare naive, SIMD-filter+verify, KMP, Boyer-Moore-Horspool, and Two-Way across pattern length, alphabet, match rate/location, repetition, and alignment.

### D23. Multi-pattern matching

**Test:** Search many patterns simultaneously.

**Hypothesis:** Aho-Corasick amortizes traversal for large pattern sets but becomes cache-bound as its automaton grows; grouped SIMD filtering wins small sets.

**Test plan:** Compare repeated single-pattern search, grouped prefix filter, dense/sparse-transition Aho-Corasick, and compact double-array forms across pattern count and automaton footprint.

### D24. Sparse vector intersection and dot product

**Test:** Intersect sorted indices and optionally multiply/accumulate values.

**Hypothesis:** Merge wins similar density, galloping wins asymmetric density, bitmaps win dense cases, and AVX2 gather helps only while latency is bounded.

**Test plan:** Compare merge, branchless merge, galloping, hash, bitmap, and AVX2 gather across density, size ratio, clustering, overlap, and cache tier.

## 6. Data-structure lifecycle experiments

### L01. Modern hash-table families

**Test:** Compare node chaining, arena chaining, linear probing, robin hood, SwissTable-style control-byte probing, cuckoo, hopscotch, and F14-style chunk filtering.

**Hypothesis:** Metadata filtering wins compact-key lookup, while node/indirect storage becomes competitive as values grow or stable addresses are required.

**Test plan:** Use the same hash/equality where possible. Sweep footprint, key/value size, load, and hit rate. Record memory/entry, probes, equality calls, cache misses, build, iteration, find, insert, and erase.

### L02. Hash load-factor cliff

**Test:** Hold capacity constant and sweep occupancy.

**Hypothesis:** Failed lookup and p99 probe length degrade earlier than successful lookup.

**Test plan:** Test every 5% occupancy, then every 1% above 75% up to each implementation limit. Separate hit/miss rows and preserve probe-length histograms.

### L03. Hash aging and tombstones

**Test:** Keep size constant while repeatedly deleting and inserting.

**Hypothesis:** Tombstones and displaced keys cause degradation absent from freshly built tables.

**Test plan:** Measure after 0, 0.25N, N, 4N, 16N, and 64N mutations using uniform, hot, and clustered deletion. Compare cleanup/rehash policies.

### L04. Hash growth and resize stalls

**Test:** Insert through every capacity transition.

**Hypothesis:** Amortized throughput hides large individual rehash stalls.

**Test plan:** Time 64/256-insert batches for no reserve, exact reserve, 1.25x, 1.5x, and 2x reserve/growth policies. Report p50/p99/max, allocation count, moved bytes, and unused capacity.

### L05. Hash-bit quality and adversarial keys

**Test:** Vary hash-bit entropy independently of semantic key distribution.

**Hypothesis:** Tables using low bits for placement fail on weak low-bit entropy; strong finalization costs cycles but prevents clustering.

**Test plan:** Use sequential, aligned/low-zero, high-zero, deliberately colliding, and well-mixed hashes. Record probes, equality calls, branch misses, and tail latency.

### L06. Heterogeneous string lookup

**Test:** Query owning-string maps using temporary strings, `string_view`, interned IDs, and prehashed keys.

**Hypothesis:** Temporary construction and allocation can dominate lookup.

**Test plan:** Sweep string length, hit rate, table family, and cache footprint; record allocations, bytes copied, hash time, and complete lookup time.

### L07. Ordered-container crossover

**Test:** Compare sorted vector/flat map, red-black tree, AVL tree, B-tree, skip list, and hash table.

**Hypothesis:** Sorted vectors dominate read-mostly sets well beyond tiny N; B-trees overtake pointer trees after L1/L2.

**Test plan:** Measure build, exact find, predecessor, lower bound, range scan, insert, and erase across payload, insertion order, query locality, and update mix.

### L08. B-tree node size and fanout

**Test:** Sweep node size independently of cache-line size.

**Hypothesis:** One-line nodes are not universally optimal: larger nodes reduce height but add intra-node work and split amplification.

**Test plan:** Test 64, 128, 256, 512, 1,024, and 4,096-byte nodes using scalar linear, binary, SIMD key search. Record height, nodes touched, splits, and bytes written.

### L09. Dynamic-array growth and mutation

**Test:** Measure append, middle insert, erase, compaction, and shrink under multiple growth policies.

**Hypothesis:** Geometric growth minimizes total copies but creates tail stalls; over-reservation may push a formerly cache-resident structure out of cache.

**Test plan:** Compare exact, 1.25x, 1.5x, and 2x growth; element sizes 4–256 bytes; trivial and nontrivial moves. Record every allocation/reallocation and moved byte.

### L10. Small-vector crossover

**Test:** Compare heap vectors with inline capacities 4, 8, 16, 32, and 64.

**Hypothesis:** Inline storage wins common small lengths but enlarged container objects hurt arrays of containers.

**Test plan:** Measure construct, append, iterate, copy/move, and destroy for isolated containers and arrays of 1,024 containers using lengths below/at/above capacity.

### L11. Heap and priority-queue layout

**Test:** Compare binary, 4-ary, 8-ary, pairing, radix/bucket, and blocked heaps.

**Hypothesis:** Higher arity reduces depth/cache misses but increases comparisons; radix/bucket heaps win bounded monotonic keys.

**Test plan:** Measure bulk build, push, pop, replace-min, and decrease-key across size, payload, priority distribution, and operation mix.

### L12. Union-find variants

**Test:** Compare naive chains, rank/size union, full compression, path splitting, and path halving.

**Hypothesis:** Splitting/halving can beat full compression by reducing stores while keeping shallow trees.

**Test plan:** Use random, adversarial-chain, union-then-find, and interleaved workloads. Record parent reads/writes, depth distribution, and cycles/op.

### L13. Bitmap and compressed-set representations

**Test:** Compare sorted integers, dense bitset, blocked bitmap, run-length encoding, and Roaring-like adaptive containers.

**Hypothesis:** Representation crossovers are predictable from density and clustering; adaptive containers approach the best region without one fixed format.

**Test plan:** Sweep universe, density 0.001%–90%, clustering, and overlap. Measure build, membership, iteration, rank, union, intersection, and conversion.

### L14. Allocator influence on node structures

**Test:** Run lists, trees, tries, and chained hashes with system heap, arena, slab/pool, and freelist reuse.

**Hypothesis:** Allocation/layout dominates pointer-prefetch micro-optimizations after mutation fragments placement.

**Test plan:** Build sequentially, churn randomly, then traverse/search/update. Record allocation latency, footprint, pages, spatial distance between linked nodes, and TLB/cache misses.

### L15. Intrusive, pointer, index, and offset links

**Test:** Compare owning pointers, intrusive pointers, 64-bit indices, 32-bit indices, and compressed 32-bit offsets.

**Hypothesis:** Indices/offsets reduce node footprint enough to offset address calculation whenever the arena fits their range.

**Test plan:** Sweep payload and structure footprint; measure traversal, insert/delete, relocation, bytes/node, pages touched, and TLB misses.

## 7. Sequential queue, cache-policy, and mutation experiments

### Q01. Queue representation

**Test:** Compare circular array, power-of-two ring, `deque`, linked queue, and growing-vector queue.

**Hypothesis:** Rings win bounded workloads; deque is a better latency/space tradeoff when capacity is unpredictable.

**Test plan:** Measure enqueue, dequeue, alternation, burst fill/drain, and wraparound across element size, capacity, and occupancy.

### Q02. Sliding-window maximum algorithm extension

**Test:** Compare monotonic deque, lazy-delete heap, ordered tree, and prefix/suffix block decomposition.

**Hypothesis:** Monotonic deque wins online streams; block decomposition benefits batched/offline processing.

**Test plan:** Sweep window length, random/monotonic/sawtooth input, batch versus online output, and payload. Record comparisons and branch misses.

### Q03. LRU/LFU/CLOCK policy mechanics

**Test:** Compare hash+intrusive list, hash+index list, CLOCK, segmented LRU, and frequency buckets.

**Hypothesis:** Indexed dense storage lowers CPU cost, while alternative policies may protect against scans at different bookkeeping cost.

**Test plan:** Replay uniform, Zipf, loop, scan-pollution, burst, and phase-changing traces. Report hit rate separately from CPU cost, footprint, and mutation traffic.

### Q04. Linked-list mutation and fragmentation

**Test:** Extend existing list traversal with random insert/delete churn.

**Hypothesis:** Arena advantage narrows as slot reuse becomes random but remains ahead of independent heap nodes.

**Test plan:** Apply 0, N, 4N, and 16N mutations, then measure traverse/search/update and consecutive-node address-distance distribution.

### Q05. Worklist discipline

**Test:** Compare stack, FIFO queue, deque, priority queue, and bucket queue for the same generated task graph.

**Hypothesis:** Discipline changes locality and duplicate work enough to dominate container-operation cost.

**Test plan:** Use tree-like, graph-like, skewed-priority, and bursty task DAGs. Measure task completion, working-set size, queue operations, and output order where semantically permitted.

## 8. Graph and spatial DSA experiments

### G01. Graph representation build and amortization

**Test:** Build edge list, pointer adjacency list, adjacency vectors, CSR, CSC, and blocked CSR.

**Hypothesis:** CSR traverses fastest but build/sort cost is not amortized for very few queries.

**Test plan:** Measure build separately and after 1, 2, 8, 64, and 1,024 traversals. Sweep graph shape, size, edge order, peak memory, and bytes/edge.

### G02. BFS algorithm and representation

**Test:** Compare top-down, bottom-up, direction-optimizing, bitmap-frontier, and queue-frontier BFS.

**Hypothesis:** Direction optimization wins broad power-law frontiers; simple top-down wins low-degree road/grid graphs.

**Test plan:** Run CSR, adjacency vectors, and pointer lists on uniform, power-law, grid, and community graphs from source-degree percentiles.

### G03. Frontier representation crossover

**Test:** Replay identical active sets using vector, queue, dense bitmap, sparse bitset, and adaptive hybrid frontier.

**Hypothesis:** Sparse vectors win below a density threshold and bitmaps above it.

**Test plan:** Sweep frontier density 0.01%–90%, clustering, and graph footprint independently of BFS, then validate end-to-end BFS.

### G04. PageRank push versus pull

**Test:** Compare push/atomic, pull/CSR-or-CSC, Jacobi double buffer, and in-place Gauss-Seidel styles.

**Hypothesis:** Pull avoids atomics and regularizes reads but needs incoming-edge layout; push can win sparse active updates.

**Test plan:** Fix iterations and convergence separately. Measure iteration cost, convergence, cache misses, synchronization, and numerical delta.

### G05. Connected components

**Test:** Compare BFS/DFS traversal, union-find, Afforest-style sampling, and parallel hooking/compression.

**Hypothesis:** Union-find wins offline edge streams, traversal wins when adjacency already exists, and sampling helps giant-component graphs.

**Test plan:** Use many-small-component, giant-component, grid, uniform, and power-law graphs. Include representation construction and algorithm-only results.

### G06. Single-source shortest paths

**Test:** Compare binary-heap Dijkstra, d-ary heap, radix heap, bucket queue, and delta stepping.

**Hypothesis:** Heap choice follows weight range/frontier width; delta stepping trades redundant work for parallelism.

**Test plan:** Sweep small/wide/clustered weights, low/high graph diameter, source degree, and one/two physical cores. Verify identical distances.

### G07. Triangle counting and neighbor intersection

**Test:** Compare scalar merge, branchless merge, galloping, hash, bitmap, and SIMD intersection.

**Hypothesis:** Degree-aware dispatch beats a single intersection algorithm.

**Test plan:** Save individual neighbor-list pairs, bin by degree ratio/overlap, benchmark independently, then run end-to-end triangle count.

### G08. Vertex reordering

**Test:** Compare original, random, degree-sorted, BFS-order, and locality/community order.

**Hypothesis:** Reordering reduces LLC/TLB misses but only amortizes across repeated graph kernels.

**Test plan:** Measure reorder/rebuild cost and 1–1,024 repetitions of BFS, PageRank, CC, and triangle count.

### G09. Betweenness centrality sampling

**Test:** Compare exact Brandes traversals with 1, 4, 16, 64, and all feasible sampled sources.

**Hypothesis:** Memory behavior per source remains similar while total cost grows with source count; layout effects persist.

**Test plan:** Measure accuracy separately from time and locality. Choose sampled vertices by random and degree-stratified selection.

### G10. Minimum spanning forest

**Test:** Compare Kruskal with comparison/radix sorting, Prim with heap variants, and Boruvka-style parallel contraction.

**Hypothesis:** Edge sorting dominates Kruskal; Prim benefits sparse graphs; Boruvka exposes scalable parallel phases at extra memory cost.

**Test plan:** Sweep graph density, weight range, presortedness, and component count; report preprocessing, algorithm, memory, and scalability separately.

### G11. K-nearest neighbors

**Test:** Compare brute-force scalar/SIMD, blocked brute force, kd-tree, uniform grid, and approximate search.

**Hypothesis:** SIMD blocked brute force wins small/high-dimensional data; spatial indexes win large low-dimensional clustered data.

**Test plan:** Sweep N, dimensions 2–128, K, clustering, build reuse, and accuracy. Report build, query, recall, and query-batch throughput.

### G12. Two-dimensional range query

**Test:** Compare scan, SoA SIMD scan, kd-tree, quadtree, uniform grid, and range tree.

**Hypothesis:** SIMD scans remain competitive for small/medium data and large output fractions; indexes win selective repeated queries.

**Test plan:** Sweep point distribution, rectangle area/selectivity, query reuse, updates, and output-count versus count-only contracts.

## 9. SIMD and irregular-memory experiments

### S01. Branchy versus branchless filtering

**Test:** Filter and compact values by a predicate.

**Hypothesis:** Branches win near 0%/100% selectivity; branchless/SIMD wins around unpredictable 50%.

**Test plan:** Sweep selectivity in 5% increments using random, clustered, periodic, and alternating masks. Compare branchy, branchless, auto-vectorized, and AVX2.

### S02. Stable versus unstable compaction

**Test:** Compact qualifying records with and without order preservation.

**Hypothesis:** Relaxing stability enables buffered/partitioned output with fewer dependencies.

**Test plan:** Compare scalar stable, SIMD stable, per-lane buffers, unstable partition, and index-only output across selectivity and payload.

### S03. Gather, scatter, and indirection depth

**Test:** Execute gather, scatter, gather+scatter, and two-level `a[index[index[i]]]` access.

**Hypothesis:** AVX2 gather helps modest dispersion in cache; unrolled scalar wins DRAM/dependency-heavy patterns.

**Test plan:** Use contiguous, strided, permutation, clustered, Zipf, page-random, and two-level indices. Compare scalar, unrolled, prefetched, and AVX2.

### S04. Alignment and boundary crossing

**Test:** Load/store vectors at every offset around cache-line and page boundaries.

**Hypothesis:** Unaligned access is inexpensive until a line or page split occurs.

**Test plan:** Sweep offsets 0–63 plus selected 4 KiB crossings for scalar, SSE, and AVX2 in L1 and DRAM conditions.

### S05. Vector tail handling

**Test:** Process lengths around vector/unroll boundaries.

**Hypothesis:** Cleanup overhead creates sawtooth behavior for small and irregular lengths.

**Test plan:** Test every length 0–128 and `2^k +/- {1,2,3,7,8,15}` using scalar tail, padded input, overlapping final vector, and mask emulation.

### S06. Reduction dependency structure

**Test:** Sum/min/max/hash-reduce using 1, 2, 4, 8, and 16 accumulators.

**Hypothesis:** Multiple accumulators hide dependency latency until port/load bandwidth saturates.

**Test plan:** Run integer/floating scalar and AVX2 across cache tiers. Record numerical error for reassociated floating reductions.

### S07. Byte classification

**Test:** Count/find delimiters, ASCII classes, zero bytes, and any-of-N bytes.

**Hypothesis:** SIMD compare+movemask is a clean win for contiguous input, but position output can become store-bound.

**Test plan:** Compare branchy scalar, table lookup, SWAR, SSE, and AVX2 across match density, alignment, and count-only versus materialized positions.

### S08. Integer delta and variable-byte coding

**Test:** Encode/decode sorted IDs using raw, delta, variable-byte, and stream-separated control/data formats.

**Hypothesis:** Compression wins beyond LLC by reducing bytes despite extra instructions.

**Test plan:** Sweep delta distributions and footprint. Measure decode-only, decode+sum, decode+intersection, and graph-neighbor traversal.

### S09. Store and write-allocation behavior

**Test:** Compare overwrite, copy, read-modify-write, zero, append, and non-temporal store.

**Hypothesis:** Ordinary stores win reused/cache-resident outputs; streaming stores win large write-only outputs but hurt immediate consumption.

**Test plan:** Sweep footprint, reuse distance, alignment, and producer-followed-by-consumer versus producer-only.

### S10. Sustained AVX2 frequency and thermal behavior

**Test:** Compare 10 ms–60 s scalar, SSE, and AVX2 compute bursts followed by a scalar probe.

**Hypothesis:** AVX2 may win the kernel but change thermal/turbo state enough to affect sustained and subsequent work.

**Test plan:** Record effective frequency, temperature, energy if available, kernel throughput, scalar-probe recovery, and run-order effects.

## 10. Atomics, locks, futexes, and synchronization experiments

All synchronization tests require a correctness stress phase before timing. Run ThreadSanitizer and invariant checks in separate non-performance builds. Publication binaries must use standards-correct atomics and memory orders; a faster data race is not a valid implementation.

For handoff latency, timestamp batches or sampled operations rather than adding two clock reads to every tiny operation. Report wall time, total CPU time, useful operations/s, unsuccessful retries, sleep/wake counts, voluntary/involuntary context switches, fairness, and tail latency.

### SY01. Atomic load and store baseline

**Test:** Measure aligned atomic load/store against non-atomic thread-local access using relaxed, acquire/release, and sequentially consistent orders.

**Hypothesis:** Relaxed and acquire/release loads/stores are often similar on Haswell, while sequentially consistent stores may require stronger instructions/order; cache ownership dominates cross-core results.

**Test plan:** Test uncontended same-thread, one writer/one reader on 4+6, and SMT 4+5. Sweep 32/64-bit values, one versus many cache lines, and cached versus evicted state. Inspect instructions and report load-to-observation latency separately from raw operation throughput.

### SY02. Atomic fetch-add scaling

**Test:** Increment one shared atomic, striped atomics, per-thread counters, and batched local counters.

**Hypothesis:** A single locked cache line collapses under contention; striping and local batching recover throughput.

**Test plan:** Run one thread, two physical, SMT pair, and four logical threads. Sweep local flush batch 1–65,536, reader snapshot frequency, padding, and relaxed versus stronger order. Record operations/s, cycles/op, fairness, and cache-line transfers.

### SY03. CAS success and failure cost

**Test:** Measure compare-and-swap when it always succeeds, always fails, and succeeds with controlled probability.

**Hypothesis:** Failed CAS still incurs substantial coherence/locked-instruction cost, and retry traffic dominates under contention.

**Test plan:** Use private and shared cache lines, success probabilities 0%, 1%, 10%, 50%, 90%, 100%, and physical/SMT placement. Record attempts per success and separate logical success throughput from instruction throughput.

### SY04. Weak versus strong CAS

**Test:** Compare `compare_exchange_weak` retry loops with `compare_exchange_strong`.

**Hypothesis:** On x86 Haswell their generated instruction and steady-state performance may be identical; any difference likely comes from surrounding loop/code generation rather than hardware spurious failure.

**Test plan:** Test uncontended and contended state machines with identical memory orders. Save disassembly and failed-attempt counts. Accept a null result if codegen and confidence intervals match.

### SY05. CAS backoff strategy

**Test:** Compare tight CAS retry, `pause`, fixed pause count, exponential backoff, randomized backoff, yield, and spin-then-park.

**Hypothesis:** Tight retry wins low contention; pause/backoff improves physical-core contention; parking wins long waits; SMT benefits from early `pause`.

**Test plan:** Sweep contenders, critical-section duration 0–100 microseconds, and topology. Report throughput, retries, CPU consumption, p99 acquisition, fairness, and scheduler activity.

### SY06. Atomic memory-order publication

**Test:** Transfer ownership of a payload through a ready flag using valid relaxed+fence, release/acquire, and sequentially consistent protocols.

**Hypothesis:** Release/acquire provides the best clear contract with little or no extra Haswell cost over relaxed, while sequential consistency may add cost in store-heavy forms.

**Test plan:** Pass payloads of 8 bytes–1 MiB between CPUs 4 and 6 and between 4 and 5. Verify sequence/checksum, inspect instructions, and measure handoff plus payload visibility. Do not include invalid all-relaxed publication as a performance candidate.

### SY07. Atomic false sharing

**Test:** Update separate atomics at byte distances 4–256.

**Hypothesis:** Independent atomics on the same line behave like one coherence hotspot; 64-byte separation removes most interference.

**Test plan:** Sweep read/write ratios, topology, and per-operation versus batched writes. Include ordinary non-atomic per-thread counters as the local baseline.

### SY08. Uncontended mutex fast path

**Test:** Measure lock/unlock for `pthread_mutex`, `std::mutex`, recursive mutex, error-checking mutex, PI mutex where permitted, and simple atomic spinlock.

**Hypothesis:** Normal mutex remains in userspace when uncontended and is close to a CAS-based lock; diagnostic and PI semantics cost more.

**Test plan:** Run repeated lock/unlock on one core with empty and tiny critical sections. Count syscalls using `perf trace`/tracepoints outside primary timing and inspect library implementation/version.

### SY09. Mutex contention versus hold time

**Test:** Contend for one mutex with controlled critical and noncritical work.

**Hypothesis:** Mutex parking wins long holds, spin strategies win very short holds, and SMT changes the crossover.

**Test plan:** Sweep critical work 0, 50 ns, 200 ns, 1 us, 10 us, 100 us and outside work at ratios 0–100x. Compare 4+6, 4+5, and 4–7. Report throughput, wait histogram, CPU time, and fairness.

### SY10. Adaptive mutex/spin-then-futex threshold

**Test:** Implement or configure spin counts before futex parking.

**Hypothesis:** There is a topology- and hold-time-specific optimal spin window; one global setting cannot optimize both SMT and physical cores.

**Test plan:** Sweep spin iterations 0, 16, 64, 256, 1,024, 4,096 and randomized critical durations. Record proportion acquired while spinning, futex calls, CPU consumption, and p99 latency.

### SY11. Spinlock algorithm comparison

**Test:** Compare test-and-set, test-test-and-set, exchange, CAS, ticket, and MCS-style locks.

**Hypothesis:** Test-test-and-set reduces coherence traffic; ticket improves fairness but broadcasts invalidations; MCS benefits higher contention but overhead may dominate with only two physical cores.

**Test plan:** Test empty to 10-us critical sections, one/two/four logical participants, physical/SMT placement, and preempted-owner injection. Report throughput, fairness, cache misses, retries, and worst wait.

### SY12. Reader-writer lock crossover

**Test:** Compare mutex, `pthread_rwlock`, shared mutex, seqlock-style read validation, immutable-copy publication, and sharded locks.

**Hypothesis:** Reader-writer locks help only sufficiently long/read-heavy critical sections; read-side bookkeeping can make a mutex faster for tiny operations.

**Test plan:** Sweep read shares 50%–100%, read/write duration, number of readers, and writer bursts. Report reader/writer p99 separately, starvation/fairness, and retry count for seqlock readers.

### SY13. Futex uncontended versus sleeping path

**Test:** Measure a userspace atomic fast path, `FUTEX_WAIT_PRIVATE` mismatch (`EAGAIN`), actual sleep/wake, and `FUTEX_WAKE_PRIVATE` with no waiter.

**Hypothesis:** The syscall path is orders more expensive than the userspace fast path; wake-without-waiter and mismatch expose kernel-entry overhead without scheduling.

**Test plan:** Batch direct futex syscalls, then measure two-thread ping-pong with verified waiter state. Record cycles, syscall duration, context switches, and wake-to-run latency for 4+6 and 4+5.

### SY14. Private versus shared futex

**Test:** Compare private futex between threads with shared futex between threads and processes.

**Hypothesis:** Private futex lookup is cheaper; process-shared futex adds kernel mapping/key overhead but may be small relative to scheduling.

**Test plan:** Use identical state machines in same-process anonymous memory and shared `mmap` across processes. Compare no-wait, wait/wake, and ping-pong paths on CPUs 4 and 6.

### SY15. Futex wake cardinality and thundering herd

**Test:** Wake one, K, or all waiters on one futex.

**Hypothesis:** Wake-all creates scheduler/coherence cost disproportionate to useful progress; targeted wake is better for one available unit.

**Test plan:** Use 1–4 waiters locally and optionally non-isolated housekeeping CPUs for a larger stress-only case. Record wake syscall time, runnable latency, useful winner, futile wakeups, and context switches.

### SY16. Futex requeue and condition-variable broadcast

**Test:** Compare wake-all with `FUTEX_REQUEUE`/`FUTEX_CMP_REQUEUE` to move waiters from a condition futex to a mutex futex.

**Hypothesis:** Requeue avoids waking threads that immediately block again and reduces herd cost.

**Test plan:** Create barrier/broadcast phases with one available lock. Sweep waiter count and work after wake. Measure syscalls, context switches, time until all useful work completes, and fairness.

### SY17. Futex bitset and wait-vector selection

**Test:** Compare one futex per condition, `FUTEX_WAIT_BITSET`, and `futex_waitv` for wait-any semantics.

**Hypothesis:** Wait vectors simplify multi-source waits but have setup/scanning overhead; bitset multiplexing worsens when many unrelated waiters share one futex.

**Test plan:** Wait on 1, 2, 4, 8, 16, 32, 64, and 128 conditions with uniform and hot wake targets. Record wake latency, setup cost, spurious/uninterested wake processing, and CPU time.

### SY18. Semaphore fast and blocking paths

**Test:** Compare POSIX unnamed semaphore, mutex+condition counter, futex-backed counting semaphore, and C++ counting semaphore if available.

**Hypothesis:** Atomic decrement handles available permits cheaply; exhausted permits converge on futex/scheduler cost; bulk release changes wake efficiency.

**Test plan:** Sweep permit count, waiter count, acquire/release batch, thread/process sharing, and burst patterns. Report permit throughput, blocking latency, fairness, and system CPU.

### SY19. Condition variable versus semaphore

**Test:** Implement the same producer-consumer condition using condition variable and predicate, semaphore, eventfd, and direct futex sequence counter.

**Hypothesis:** Semaphores fit counted events; condition variables pay mutex/predicate cost but express state transitions; eventfd costs a syscall but integrates with polling.

**Test plan:** Test one event at a time, bursts, accumulated events, timeouts, and shutdown. Verify no lost wakeups and report throughput, tail, and CPU sleep ratio.

### SY20. Barrier and latch algorithms

**Test:** Compare mutex/condition barrier, centralized atomic sense-reversing barrier, tree barrier, futex-assisted barrier, and process-shared barrier.

**Hypothesis:** Centralized atomics win two physical cores; tree structure has insufficient scale to amortize on this DUT but may reduce four-logical-thread contention.

**Test plan:** Sweep work between barriers 0–1 ms, participants 2/4 logical, physical versus SMT placement, and thread versus process. Report barrier latency, skew, and CPU time.

### SY21. Priority inversion and PI mutex/futex

**Test:** Block a high-priority task behind a low-priority owner with and without priority inheritance.

**Hypothesis:** PI bounds inversion when a medium-priority competitor preempts the owner, at higher uncontended/contended overhead.

**Test plan:** Run only if scheduling permissions permit. Use controlled priorities and CPU placement, measure high-priority blocking time, and keep this safety-sensitive test isolated from primary throughput runs.

### SY22. Cross-process notification

**Test:** Compare process-shared futex, POSIX semaphore, eventfd, pipe byte, Unix datagram, and signal for one-way wake and ping-pong.

**Hypothesis:** Shared-memory futex has the lowest state-transfer overhead; eventfd/pipe cost more but integrate cleanly with descriptor event loops; signals have high variance.

**Test plan:** Pin processes to 4+6 and 4+5. Test empty and payload-associated notifications, bursts, blocking/nonblocking modes, and p50–max latency.

### SY23. Process-shared robust mutex and owner death

**Test:** Compare normal process-shared mutex, robust mutex during normal operation, and recovery after owner termination.

**Hypothesis:** Robust tracking has modest steady overhead but uniquely enables detectable recovery; recovery latency is far larger and should not be blended into normal results.

**Test plan:** Measure uncontended/contended normal paths, then deliberately terminate an owner in a disposable child process and measure detection/repair. Validate consistency semantics.

## 11. Concurrent queue and work-distribution experiments

### CQ01. SPSC topology and handoff

**Test:** Compare bounded ring implementations between one producer and one consumer.

**Hypothesis:** Cache-line ownership transfer dominates tiny messages; 4+6 maximizes true-core throughput while 4+5 trades coherence distance for shared execution resources.

**Test plan:** Compare plain correctly ordered ring, sequence-number ring, and library baseline on 4+6, 4+5, and 4+0. Sweep message 0–4,096 bytes and measure throughput plus sampled end-to-end latency.

### CQ02. SPSC batching and capacity

**Test:** Sweep queue capacity and producer/consumer batch size.

**Hypothesis:** Batching amortizes atomic ownership traffic; undersized queues amplify backpressure; oversized queues increase cache footprint and latency.

**Test plan:** Capacities 2–1,048,576 entries and batches 1–1,024. Test balanced, producer-fast, and consumer-fast rates. Report occupancy distribution, stalls, cache misses, throughput, and p99.

### CQ03. SPSC wait strategy

**Test:** Compare busy spin, `pause`, yield, nanosleep, futex/eventfd park, and adaptive spin-then-park when empty/full.

**Hypothesis:** Spinning minimizes latency under continuous load; parking minimizes CPU under sparse arrivals; hybrid crossover depends on interarrival gap and topology.

**Test plan:** Generate fixed and exponential gaps from 0–10 ms. Measure latency, CPU utilization, wakeups, energy if available, and missed service deadlines.

### CQ04. MPSC queue algorithms

**Test:** Compare mutex queue, atomic-tail linked queue, bounded sequence ring, per-producer SPSC queues with consumer merge, and batched locked queue.

**Hypothesis:** Per-producer queues avoid producer contention and win skewed/bursty traffic at the cost of consumer scanning and ordering semantics.

**Test plan:** Use 2–3 producers across physical/SMT CPUs and one consumer. Sweep payload, batching, producer skew, global-order requirement, and queue saturation. Report producer fairness and consumer scan waste.

### CQ05. MPMC queue algorithms

**Test:** Compare mutex+condition queue, bounded sequence-number ring, linked CAS queue, sharded queues, and work-stealing deques.

**Hypothesis:** No single MPMC design wins: bounded rings lead steady traffic, sharding leads low-sharing work, and blocking queues lead sparse traffic.

**Test plan:** Test 1P1C, 2P1C, 1P2C, and 2P2C on CPUs 4–7, clearly noting only two physical cores. Sweep message cost, burst, capacity, and wait policy; report fairness and tails.

### CQ06. CAS versus fetch-add reservation

**Test:** Compare queue slot reservation by CAS loop, atomic fetch-add ticket, and per-producer ranges.

**Hypothesis:** Fetch-add avoids retry storms but may create head-of-line blocking when a producer is delayed after reservation.

**Test plan:** Inject controlled producer pauses after reservation, sweep contention, and record retries, holes, blocked consumer time, throughput, and tail latency.

### CQ07. Inline versus indirect payload

**Test:** Store payload directly in queue slots versus pointers/indices into pools.

**Hypothesis:** Inline wins small messages; indirection wins large messages by reducing coherence traffic but adds cache/TLB misses.

**Test plan:** Sweep payload 0–64 KiB, immutable/mutable consumer access, allocator/pool layout, and producer-consumer topology. Report bytes transferred across shared cache lines.

### CQ08. Backpressure semantics

**Test:** Compare blocking, spin, drop-new, drop-old, overwrite, and caller-runs policies when a bounded queue fills.

**Hypothesis:** Throughput rankings change with semantic policy; drop/overwrite reduces latency but cannot be compared as equivalent work without loss metrics.

**Test plan:** Generate consumer slowdowns and bursts. Report accepted/completed/dropped operations, queue delay, producer stall, and recovery time.

### CQ09. Queue latency coordination omission

**Test:** Measure latency under open-loop scheduled arrivals and closed-loop next-request-after-completion generation.

**Hypothesis:** Closed-loop tests conceal queue buildup and underestimate tail latency during overload.

**Test plan:** Run identical mean rates with both generators, including 70%, 90%, 99%, and 110% of measured capacity. Preserve intended-arrival timestamps and report corrected queue latency.

### CQ10. Memory reclamation

**Test:** Compare never-reclaim arena, reference counting, hazard pointers, epoch reclamation, quiescent-state reclamation, and mutex-protected reclamation for a linked concurrent structure.

**Hypothesis:** Epoch methods maximize steady throughput but delay memory recovery; hazard pointers add read-side stores/scans; reference counting creates contention.

**Test plan:** Use find/insert/delete mixes, stalled-reader injection, and bounded/unbounded churn. Report throughput, unreclaimed bytes, reclamation latency, and scan cost.

### CQ11. Work stealing versus central queue

**Test:** Execute identical task DAGs through a central MPMC queue, per-worker deque without stealing, and work-stealing deques.

**Hypothesis:** Work stealing wins irregular task durations but adds overhead to uniform fine-grained work.

**Test plan:** Use fixed, bimodal, heavy-tailed, recursive, and graph-frontier task sets. Sweep task granularity and report utilization, steals, failed steals, makespan, and fairness.

## 12. System-call, IPC, file-I/O, network-I/O, and io_uring experiments

### 12.1 I/O experiment rules

- Never benchmark a raw destructive device on the DUT. Use explicit test files only.
- Keep at least 10 GiB filesystem headroom. Given the measured 25 GiB free space, begin with an 8 GiB preallocated file and recheck free space immediately before creation.
- Separate warm page-cache, cache-evicted best effort, and `O_DIRECT` tests. They answer different questions.
- Separate submission cost, completion cost, device service time, CPU consumption, and end-to-end application latency.
- Verify every completion result, byte count, offset, and data checksum. Handle short I/O and `EINTR` correctly.
- Use monotonically increasing operation IDs and save latency histograms.
- For storage tests, record device model/firmware, filesystem, mount options, scheduler, read-ahead, queue parameters, file extent state, free-space percentage, and SSD temperature if exposed.
- For network tests, record NIC/driver/link speed/offloads/MTU/socket buffer sizes and both sender and receiver CPU utilization.
- Do not call io_uring itself “zero copy.” Its shared submission/completion rings reduce control-path syscall traffic; data-copy behavior depends on the operation, buffers, page cache, filesystem, and device.
- Runtime-probe every io_uring opcode and setup feature; record `supported`, `permission_denied`, or `hardware_unsupported` rather than dropping rows.

### IO01. Pure system-call entry baseline

**Test:** Compare a normal userspace function call, libc call that remains in userspace, raw `syscall()` for `getpid/gettid`, zero-byte read/write where valid, and an intentionally invalid cheap syscall.

**Hypothesis:** Kernel entry/exit establishes the fixed control-path floor that batching and io_uring must amortize.

**Test plan:** Batch 1–4,096 operations, pin CPU 4, warm code/data, and record cycles/instructions plus syscall trace count. Ensure the compiler cannot fold calls or reuse returned values.

### IO02. vDSO versus real syscall

**Test:** Compare libc/vDSO `clock_gettime`, direct vDSO symbol, and raw `SYS_clock_gettime`; likewise test `getcpu` where available.

**Hypothesis:** vDSO avoids kernel transition and is much faster; timing harnesses accidentally using raw syscalls can dominate microbenchmarks.

**Test plan:** Resolve and verify the vDSO path, batch calls, test supported clock IDs, record cycles and distribution, and use the result to choose the benchmark clock strategy.

### IO03. Thread/process creation and context switch

**Test:** Measure pthread create/join, process `fork`/wait, `fork`+`exec`, thread ping-pong, and process ping-pong.

**Hypothesis:** Creation costs dwarf reusable worker dispatch; process address-space setup and exec dominate small jobs.

**Test plan:** Separate cold first use and warmed repetition. Sweep child work 0–100 ms, existing address-space size, and thread/process pools. Record minor faults, context switches, and wall/CPU time.

### IO04. IPC notification primitive

**Test:** Compare shared-memory atomic polling, futex, eventfd, pipe, Unix stream socket, Unix datagram, and POSIX semaphore for notification-only traffic.

**Hypothesis:** Shared memory/futex wins low-level handoff; eventfd/pipe trade overhead for pollability; sockets add protocol and buffer cost.

**Test plan:** Run one-way and ping-pong, single and batched notifications, blocking and nonblocking, 4+6 and 4+5, with no payload. Record syscalls/message, CPU, throughput, p99, and wake latency.

### IO05. IPC payload transport

**Test:** Compare pipe, Unix stream/datagram socket, shared-memory ring+futex, POSIX shared memory+semaphore, and `memfd` shared mapping.

**Hypothesis:** Shared memory dominates large payloads by avoiding repeated kernel copies, while pipe/socket simplicity is competitive for tiny messages.

**Test plan:** Sweep payload 0 bytes–4 MiB, batch 1–128, one/two directions, and reuse versus allocate/map per transfer. Verify payload and report effective copies/bytes, throughput, latency, and CPU on both processes.

### IO06. Scalar versus vectored synchronous file read

**Test:** Compare `read`, `pread`, `readv`, and `preadv2` for equivalent file regions.

**Hypothesis:** Vectored I/O amortizes syscall overhead for fragmented application buffers; one contiguous buffer remains cheaper for the same bytes.

**Test plan:** Use 1–1,024 iovecs, total sizes 4 KiB–4 MiB, sequential/random offsets, warm buffered and `O_DIRECT` where supported. Include iovec construction both excluded and included.

### IO07. Scalar versus vectored synchronous write

**Test:** Compare `write`, `pwrite`, `writev`, and `pwritev2` without and with durability requirements.

**Hypothesis:** Vectored writes reduce syscall count but may not reduce device work; durability semantics dominate once sync is requested.

**Test plan:** Use a bounded reusable test file, verify written data, sweep iovecs and sizes, and run buffered-no-sync, final-fdatasync, per-batch-fdatasync, `O_DSYNC`, and per-write sync flags as distinct contracts.

### IO08. Buffered read versus mmap

**Test:** Compare buffered `read/pread`, `mmap` sequential access, `mmap` random access, and mapped access after prefault.

**Hypothesis:** mmap avoids an explicit userspace copy for warm reads but introduces page-fault and TLB behavior; pread gives steadier latency and explicit buffering.

**Test plan:** Sweep 4 KiB–8 GiB files, sequential/random access, one/many passes, prefault/no-prefault, `madvise` hints, and checksum consumption. Record faults, RSS, dTLB, throughput, and CPU.

### IO09. Warm page cache versus storage media

**Test:** Run identical reads warm, after `posix_fadvise(DONTNEED)` best-effort eviction, after controlled cache pressure, and through `O_DIRECT`.

**Hypothesis:** Warm-cache API rankings describe memory-copy/control cost, while direct/cold tests describe SATA device latency; mixing them creates meaningless averages.

**Test plan:** Verify residency where possible with `mincore`, record page faults and device I/O counters, and label eviction as best effort. Never use global cache dropping during normal system activity without explicit operational approval.

### IO10. Buffered versus direct I/O

**Test:** Compare buffered and `O_DIRECT` reads/writes.

**Hypothesis:** Buffered I/O wins reuse and small accesses; direct I/O gives predictable cache bypass and can benefit queued large transfers, but alignment and SATA latency constrain it.

**Test plan:** Sweep 4 KiB–4 MiB blocks, sequential/random access, queue depth, aligned offsets/buffers, one/multiple passes, and compute-after-read. Check actual `O_DIRECT` success and reject fallback/misalignment.

### IO11. Direct-I/O alignment

**Test:** Probe buffer, length, and offset alignment requirements and performance.

**Hypothesis:** Unsupported alignment fails rather than merely slowing; valid larger alignment may aid DMA mapping but offers diminishing returns.

**Test plan:** Use aligned allocations from 512 bytes to 2 MiB, lengths/offsets around block boundaries, capture exact errors, and time only semantically successful cases.

### IO12. Sequential-read size and readahead

**Test:** Sweep synchronous sequential block size and access stride with default, sequential, random, and no-reuse advice.

**Hypothesis:** Page-cache readahead hides device latency for regular reads; random advice prevents useless I/O; large calls eventually saturate SATA bandwidth.

**Test plan:** Read an 8 GiB file with blocks 512 bytes–8 MiB and strides 1–64 blocks. Record requested versus actual device bytes, major/minor faults, throughput, CPU, and latency.

### IO13. Random-read queue depth: synchronous threads versus io_uring

**Test:** Compare one blocking thread per outstanding read, thread pool, Linux AIO if available, and io_uring.

**Hypothesis:** io_uring amortizes submissions/completions as queue depth grows; at QD1 on a SATA SSD it may equal or lose to simple `pread`.

**Test plan:** Use `O_DIRECT` 4/16/64/256 KiB random reads, queue depth 1, 2, 4, 8, 16, 32, and 64. Pin submitter/workers explicitly and report IOPS, p99, CPU/IO, and context switches.

### IO14. Sequential I/O: sync versus io_uring

**Test:** Compare `read/pread` loops, batched vectored I/O, and io_uring at equivalent block size/depth.

**Hypothesis:** At large sequential blocks the SATA device dominates and io_uring has little throughput advantage; it may reduce CPU or improve overlap with compute.

**Test plan:** Sweep block 4 KiB–4 MiB, QD1–32, buffered/direct, and read-only/write-only. Report bandwidth, CPU%, syscalls/GiB, and latency.

### IO15. io_uring ring depth and submit/completion batching

**Test:** Vary SQ/CQ depth, number submitted per `io_uring_enter`, and completions reaped per loop.

**Hypothesis:** Moderate batching amortizes syscalls; oversized rings increase footprint/queueing latency without helping the SATA device.

**Test plan:** Ring depth 2–4,096 and submit/reap batches 1–256 for a memory-fast operation, warm buffered I/O, and direct device I/O. Record syscalls/request, queue wait, service latency, and CPU.

### IO16. Registered files

**Test:** Compare ordinary file descriptors with io_uring fixed-file registration.

**Hypothesis:** Registration reduces per-request file-reference overhead most for small fast operations and many descriptors; the gain shrinks when device latency dominates.

**Test plan:** Test one, 16, 256, and 4,096 files if filesystem headroom permits, warm buffered and direct reads, QD1–64. Include registration setup/amortization at 1–1,000,000 operations.

### IO17. Registered/fixed buffers

**Test:** Compare ordinary buffers, registered fixed buffers, and provided buffer rings for read/receive paths.

**Hypothesis:** Fixed buffers reduce repeated pin/map validation, benefiting small repeated I/O, but registration setup and pinned memory hurt one-shot work.

**Test plan:** Sweep buffer count, size, reuse, QD, and total pinned bytes up to a safe fraction of the 1.94-GiB memlock limit. Measure setup, steady state, teardown, and failure behavior separately.

### IO18. SQPOLL

**Test:** Compare normal io_uring submission with `IORING_SETUP_SQPOLL` using an explicitly pinned polling thread.

**Hypothesis:** SQPOLL reduces submission syscalls/latency at high rates but consumes an entire CPU; on this two-isolated-physical-core DUT its system efficiency may be worse.

**Test plan:** Pin application to CPU 4 and poller to CPU 6, then compare conventional two-core worker layouts. Sweep idle timeout, QD, block size, and offered rate. Report wall throughput and total system CPU, not application CPU alone.

### IO19. io_uring task-run flags

**Test:** Compare default setup with runtime-supported combinations such as single issuer, cooperative task running, deferred task running, and submit-all behavior.

**Hypothesis:** Single-issuer/deferred completion policies reduce coordination overhead in a compliant one-thread event loop but can worsen latency or violate an unsuitable usage model.

**Test plan:** Probe feature support, use identical single-issuer and multi-issuer workloads, sweep batch/QD, and record completion delivery latency, enter syscalls, CPU, and correctness. Do not compare flags under invalid usage.

### IO20. IOPOLL/hipri capability and value

**Test:** Compare interrupt-driven direct I/O with polled completion only if the SATA device/driver/filesystem accept it.

**Hypothesis:** The current SATA path may reject or gain little from IOPOLL; if supported, it trades CPU for lower high-IOPS latency.

**Test plan:** Begin with a capability probe. If supported, test QD1–32 4–64 KiB direct reads, record poll CPU and p99. If unsupported, publish that hardware result and do not emulate it.

### IO21. `RWF_NOWAIT` and nonblocking buffered read

**Test:** Attempt buffered reads with nowait semantics on resident and nonresident pages.

**Hypothesis:** Resident reads complete without blocking; cache misses return `EAGAIN` where the filesystem path supports it, enabling an event loop to choose fallback work.

**Test plan:** Probe support, create known resident/evicted ranges, compare success/EAGAIN/error rates and call cost, then integrate fallback to io_uring/blocking worker.

### IO22. Linked operations and dependency chains

**Test:** Compare userspace completion-driven sequencing with io_uring linked chains for read→write, read→timeout, and open→read→close.

**Hypothesis:** Kernel-visible chains reduce user/kernel round trips but limit scheduling flexibility and propagate failures differently.

**Test plan:** Sweep chain length 2–64, failure position, QD, and independent chains. Verify cancellation/error semantics and record enter calls, latency, and throughput.

### IO23. Cancellation and timeout

**Test:** Compare poll/epoll timer management, io_uring linked timeout, standalone timeout, and asynchronous cancel.

**Hypothesis:** io_uring integrates timeout/cancel efficiently for many outstanding operations, but cancellation races and late completions add state-machine cost.

**Test plan:** Create 1–16,384 pending timers/reads/socket waits, cancel 0–100%, and vary deadline distribution. Validate exactly-once completion handling and report cancel latency, stale completions, CPU, and memory.

### IO24. Event-loop readiness: poll versus epoll versus io_uring

**Test:** Wait for readiness across many pipes/Unix sockets/TCP sockets using `poll`, level/edge-triggered epoll, io_uring poll, and multishot poll if supported.

**Hypothesis:** `poll` scales with watched descriptors; epoll/io_uring scale with active events, while multishot reduces resubmission cost.

**Test plan:** Use 1–65,536 descriptors subject to limits, active fractions 0.01%–100%, burst/sparse events, and one/multiple event-loop threads. Record setup/update and steady state separately.

### IO25. Accept and receive event-loop path

**Test:** Compare blocking-per-connection, epoll+accept/recv, io_uring accept/recv, and multishot accept/recv with provided buffers when supported.

**Hypothesis:** Multishot operations reduce submission traffic for many short connections/messages; at low concurrency simple epoll remains competitive.

**Test plan:** Use a separate load-generator host, connection counts 1–10,000 within DUT limits, message sizes 1 byte–1 MiB, persistent and short-lived connections. Record connection/s, messages/s, p99, syscalls, and both-side CPU.

### IO26. send/receive batching

**Test:** Compare `send/recv`, `writev/readv`, `sendmmsg/recvmmsg`, and io_uring send/recv.

**Hypothesis:** Batching dominates tiny messages by amortizing kernel entry; at 100 Mb/s the current NIC masks high-throughput API differences.

**Test plan:** Test Unix sockets locally for control-path cost and a second physical host for network behavior. Sweep message 64 bytes–1 MiB and batch 1–128. Never use loopback as the sole network conclusion.

### IO27. `MSG_ZEROCOPY` send crossover

**Test:** Compare normal TCP/UDP send with `SO_ZEROCOPY`+`MSG_ZEROCOPY`, including completion processing.

**Hypothesis:** Copy avoidance loses on small messages and may help only beyond roughly 10 KiB with capable hardware; the DUT’s 100-Mb/s USB NIC may make it slower or inconclusive.

**Test plan:** Use a second host, message 1 KiB–4 MiB, multiple in-flight buffers, and notification batching. Track copied-fallback notifications, safe buffer-reuse time, throughput, latency, CPU, and pinned memory.

### IO28. io_uring zero-copy send

**Test:** Compare normal io_uring send, registered-buffer send, and zero-copy send opcodes where supported.

**Hypothesis:** Registered/zero-copy paths reduce CPU for large reusable buffers but add completion and ownership complexity; current link speed limits visible throughput gain.

**Test plan:** Runtime-probe opcodes, use a second host, sweep buffer size/in-flight count, verify notification CQEs and fallback flags, and report application plus kernel CPU.

### IO29. io_uring zero-copy receive capability

**Test:** Probe ZC Rx kernel, driver, NIC header/data split, queue steering, and memory-registration requirements.

**Hypothesis:** Kernel configuration alone is insufficient and the ASIX USB NIC will not satisfy the hardware requirements.

**Test plan:** Make this a capability test first. If unsupported, record the exact missing feature and stop. Execute receive benchmarks only on a suitable NIC; never label ordinary provided buffers as zero-copy Rx.

### IO30. File copy mechanisms

**Test:** Copy a file using read+write, buffered large blocks, mmap+write, `sendfile`, pipe+`splice`, `copy_file_range`, and io_uring read+write/splice where supported.

**Hypothesis:** In-kernel copy paths reduce userspace copies/CPU, but filesystem/device semantics and same-filesystem optimizations change what work actually occurs.

**Test plan:** Use 4 KiB–8 GiB files, warm/cold, same/different destination files on the same ext4 filesystem, and checksum verification. Record wall time, CPU, actual device bytes, cache state, and setup. Do not claim physical copying if reflink/offload semantics avoid it.

### IO31. File-to-socket transfer

**Test:** Compare read+send, mmap+send, `sendfile`, splice through a pipe, io_uring read+send, io_uring splice, and zero-copy send where supported.

**Hypothesis:** `sendfile`/splice reduce userspace copy and CPU for large files, but the 100-Mb/s link masks bandwidth differences and emphasizes CPU/latency.

**Test plan:** Use a second receiver host, file sizes 4 KiB–8 GiB, cold/warm cache, slow/fast receiver, checksum, and complete drain. Report sender CPU, receiver CPU, throughput, p99, and page-cache effects.

### IO32. Copy then compute versus mmap/direct consume

**Test:** Read data and immediately checksum, search, parse fixed records, or aggregate using read buffer, mmap, and direct-I/O buffers.

**Hypothesis:** Avoiding a copy helps only if the consumer’s access pattern does not introduce larger page/TLB/cache cost; end-to-end rankings differ from raw I/O.

**Test plan:** Pair each I/O method with light checksum, SIMD scan, random lookup, and heavy compute. Measure I/O-only, compute-only, and fused pipeline.

### IO33. Durability contract

**Test:** Compare buffered acknowledgment, `fdatasync`, `fsync`, `O_DSYNC`, `O_SYNC`, and grouped commit.

**Hypothesis:** Durability level and batch interval dominate API choice; group commit dramatically improves throughput while increasing acknowledgment latency.

**Test plan:** Append fixed records, sync every 1, 4, 16, 64, 256, 1,024 records, verify after controlled normal reopen, and report latency histogram. Do not induce unsafe power failure on this machine.

### IO34. Append contention

**Test:** Append from one/multiple threads or processes using shared fd, separate fds, mutex serialization, `pwrite` offset reservation, and io_uring.

**Hypothesis:** Shared append ordering and filesystem locking serialize writers; preallocated disjoint offsets scale better.

**Test plan:** Use CPUs 4 and 6 first, 4–7 as SMT stress. Sweep record 64 bytes–1 MiB, sync policy, and batch. Verify record completeness/order and report lock/syscall/device costs.

### IO35. Small-file metadata

**Test:** Compare open/stat/read/close, persistent fds, `openat`, directory fd reuse, registered io_uring files, and batched directory traversal.

**Hypothesis:** Path lookup and metadata dominate small files; fd reuse/registration helps repeated access, while storage bandwidth is irrelevant.

**Test plan:** Generate a bounded tree outside timing, warm/cold dentry/inode states, vary depth and file count, and record syscalls, faults, CPU, and operations/s. Clean up only the explicit test tree.

### IO36. Mixed I/O plus compute pipeline

**Test:** Alternate asynchronous reads with checksum, decompression-like transform, sort/hash aggregation, or GEMV work.

**Hypothesis:** io_uring’s chief benefit on SATA may be overlap, not faster individual I/O; excessive QD harms latency/cache locality.

**Test plan:** Sweep compute per byte, QD, buffer count, same-core event loop, separate I/O/computation threads, and two processes. Report end-to-end throughput, CPU utilization, queue latency, and cache counters.

### IO37. Submission architecture: one ring or per-worker rings

**Test:** Compare shared ring with multiple issuers, one submitter ring plus worker queue, and one ring per worker/process.

**Hypothesis:** Per-worker rings remove userspace contention but duplicate resources; one submitter centralizes batching but adds handoff latency.

**Test plan:** Use one/two physical workers and four logical stress mode, read and socket workloads, uniform/skewed demand, and registered resources. Record lock/CAS contention, memory, throughput, and p99.

### IO38. fio cross-check versus custom harness

**Test:** Reproduce core sync/io_uring buffered/direct QD curves in `fio` and the custom benchmark.

**Hypothesis:** Agreement validates device-level findings; disagreement exposes setup, timing, cache, or verification mistakes.

**Test plan:** After installing a pinned fio version, save complete job files and version. Match file, block, depth, direct mode, runtime, affinity, fixed buffers, and registered files. Explain residual differences rather than averaging them.

## 13. BLAS-like, SIMD-DSA, and parallel numerical experiments

### 13.1 Numerical correctness contract

- Test FP32, FP64, and integer variants only where semantics are meaningful.
- Compare floating results using a declared absolute/relative/ULP tolerance; also save a high-precision or trusted-library reference.
- Report both mathematical work and memory traffic: FLOP/s, useful bytes/s, instructions, cycles, cache misses, and arithmetic intensity.
- Separate time spent packing/transposing/converting from the microkernel, then publish end-to-end results at reuse counts 1, 2, 8, 64, and 1,024.
- Set BLAS/OpenMP thread counts explicitly. Record whether the library was built with pthread/OpenMP support and the selected Haswell kernel.
- Prevent nested/accidental oversubscription unless oversubscription is the test.
- For parallel reductions, distinguish deterministic fixed-order results from faster nondeterministic order.

### B01. BLAS-1 copy and scale

**Test:** Implement vector copy and `x = alpha*x` using scalar, auto-vectorized, explicit AVX2, and installed BLAS libraries.

**Hypothesis:** These operations are memory-bound beyond L1/L2; SIMD helps cache-resident sizes but cannot exceed DRAM bandwidth at scale.

**Test plan:** Sweep FP32/FP64, footprint ladder, alignment, in-place/out-of-place, and strides 1, 2, 4, 8, 16. Report useful and actual bytes/s, cycles/element, and cache misses.

### B02. AXPY and triad

**Test:** Compute `y = alpha*x + y` and `a = b + alpha*c`.

**Hypothesis:** FMA reduces instruction count but the large-size limit remains memory bandwidth; nonunit stride rapidly loses vector efficiency.

**Test plan:** Compare scalar, no-FMA AVX2, FMA AVX2, compiler, BLAS, and one/two physical threads. Sweep size, stride, alignment, and read/write aliasing.

### B03. Dot product and norm

**Test:** Compute dot, sum-of-squares, L1 norm, and max norm.

**Hypothesis:** Multiple SIMD accumulators remove dependency bottlenecks in cache; DRAM dominates large vectors; reproducible summation costs throughput.

**Test plan:** Compare scalar one/multi-accumulator, AVX2, FMA, pairwise/tree, Kahan/compensated, and BLAS. Report speed and numerical error across conditioned/adversarial inputs.

### B04. Nonunit-stride BLAS-1

**Test:** Repeat copy/AXPY/dot with positive, negative, power-of-two, odd, and irregular index strides.

**Hypothesis:** Regular nonunit access benefits hardware prefetch until line waste dominates; irregular gather loses much of AVX2’s advantage.

**Test plan:** Use strides 1–1,025 including 65/129/257, reverse order, and saved irregular patterns. Compare scalar, manual lane streams, gather, and packing-then-compute with reuse counts.

### B05. GEMV layout and transpose

**Test:** Compute dense matrix-vector multiply for row-major/column-major and transposed/nontransposed operands.

**Hypothesis:** Contiguous row access dominates single-use GEMV; packing/transposing wins only after reuse; threading saturates memory quickly.

**Test plan:** Sweep M,N from 16–8,192, square/tall/wide shapes, FP32/FP64, scalar/AVX2/BLAS, 1/2 physical threads and SMT. Separate packing and repeated multiply.

### B06. Batched small GEMV

**Test:** Apply many small matrices to vectors individually versus batched/layout-interleaved.

**Hypothesis:** Batching improves instruction and cache reuse; library-call overhead dominates tiny standalone GEMV.

**Test plan:** Matrix sizes 2–128, batch 1–4,096, same/different matrices, AoS/SoA/interleaved layout, scalar/AVX2/library. Include dispatch overhead.

### B07. GEMM loop-order baseline

**Test:** Compare all legal naive loop orders for `C=A*B`.

**Hypothesis:** Loop order alone changes cache behavior by orders of magnitude; layouts determine the best order.

**Test plan:** Test `ijk`, `ikj`, `jik`, `jki`, `kij`, `kji` for row/column-major A/B/C, FP32/FP64, N=8–1,024. Verify exact work and report cache/TLB counters.

### B08. GEMM blocking ladder

**Test:** Sweep MC/KC/NC outer blocking and MR/NR microkernel shapes.

**Hypothesis:** Blocks fitted to 32-KiB L1, 256-KiB L2, and 6-MiB LLC maximize reuse; one setting cannot optimize all shapes.

**Test plan:** Begin broad powers-of-two, then refine around maxima. Compare no-pack, pack-A, pack-B, pack-both. Publish microkernel-only and end-to-end including pack cost.

### B09. GEMM scalar, auto-vector, AVX2/FMA, and BLAS

**Test:** Compare reference triple loop, optimized scalar/blocking, compiler vectorization, explicit AVX2/FMA microkernel, OpenBLAS, and BLIS if installed.

**Hypothesis:** Library kernels approach Haswell compute limits for large reusable matrices; custom/no-pack forms may win very small or skinny cases.

**Test plan:** FP32/FP64 square sizes 1–2,048 and rectangular/skinny shapes. Force one thread first, then 2 physical and 4 logical. Save library version/core selection and GFLOP/s per physical core.

### B10. Small/skinny GEMM

**Test:** Sweep one dimension in {1,2,4,8,16,32} while others vary to 4,096.

**Hypothesis:** Conventional packing and general GEMM dispatch are excessive for skinny problems; specialized kernels win.

**Test plan:** Compare GEMV-like path, no-pack microkernels, packed GEMM, BLAS, and batched execution. Measure crossover by shape and reuse.

### B11. Batched small GEMM

**Test:** Multiply many 2x2–64x64 matrix pairs using independent calls, fused batches, and interleaved SoA data.

**Hypothesis:** Dispatch and packing dominate individual calls; batching exposes SIMD across matrices and improves throughput.

**Test plan:** Sweep size, batch, same-A/same-B reuse, layout, FP32/FP64, and one/two threads. Report latency/matrix and aggregate GFLOP/s.

### B12. Transpose and tiled transpose extension

**Test:** Extend existing transpose work across tile size, in-place/out-of-place, element size, and parallel partition.

**Hypothesis:** Optimal tile brackets cache geometry; SIMD helps within tiles; in-place cycle handling adds irregular accesses.

**Test plan:** Tiles 4–128, rectangular/square, aligned/misaligned leading dimensions, FP32/FP64/16-byte records, scalar/AVX2, 1/2 physical threads. Include edge tiles.

### B13. Symmetric rank-k and triangular kernels

**Test:** Benchmark SYRK, triangular matrix multiply, and triangular solve against GEMM-equivalent baselines.

**Hypothesis:** Triangular structure reduces mathematical work but complicates packing/vector utilization; dependency-limited solve scales poorly.

**Test plan:** Compare custom blocked and BLAS implementations across size, side/uplo/transpose variants, 1/2 threads, and include residual correctness checks.

### B14. Dense matrix reduction and softmax-like passes

**Test:** Compute row/column sums, maxima, normalization, and max-subtract-exp-sum normalization as separate/fused passes.

**Hypothesis:** Fusing passes reduces memory traffic, but transcendental approximation and reproducibility determine accuracy/performance.

**Test plan:** Compare row/column-major, scalar/AVX2/library primitives, fused/unfused, tall/wide shapes, 1/2 threads, and report error plus useful bytes.

### B15. 1D convolution

**Test:** Compare direct scalar, direct AVX2, unrolled fixed-kernel, im2col+GEMM, and FFT only for sufficiently long filters.

**Hypothesis:** Direct SIMD wins small kernels, GEMM conversion wins repeated/channel-rich work, and FFT only amortizes for large kernels/signals.

**Test plan:** Sweep signal 64–1M, kernel 3–65,536, boundary mode, batch/channels, reuse, and include transformation/allocation time.

### B16. 2D stencil and convolution

**Test:** Apply 3x3/5x5/variable stencils using naive, row-buffered, tiled, vectorized, and two-thread halo partitioning.

**Hypothesis:** Cache tiling and row reuse matter more than SIMD for large images; boundary handling harms small tiles.

**Test plan:** Sweep image dimensions, pitch/alignment, radius, tile, temporal steps, scalar/AVX2, and 1/2 physical threads. Measure halo and boundary separately.

### B17. Sparse matrix-vector formats

**Test:** Compare COO, CSR, CSC, ELLPACK, sliced ELL, and blocked CSR SpMV.

**Hypothesis:** CSR wins general CPU sparsity; ELL/vector forms win regular row lengths; BCSR wins block structure; gather latency caps SIMD.

**Test plan:** Use uniform, power-law, banded, block-structured, grid, and random matrices. Sweep density, row-length variance, block size, FP32/FP64, 1/2 threads, and conversion amortization.

### B18. Sparse matrix-multiple-vector/matrix

**Test:** Compare repeated SpMV with fused SpMM over K dense columns.

**Hypothesis:** SpMM amortizes sparse index loads and exposes vector reuse as K grows.

**Test plan:** K=1,2,4,8,16,32,64; row-major/column-major dense side; CSR/BCSR; scalar/AVX2; 1/2 threads. Include format conversion.

### B19. Histogram privatization

**Test:** Compare one shared atomic histogram, lock-striped bins, per-thread dense histograms plus reduction, sparse local hashes, and SIMD conflict-handling approaches.

**Hypothesis:** Private bins win when their combined footprint fits cache; shared atomics can win enormous sparse domains or tiny work; skew creates hot-bin collapse.

**Test plan:** Bins 2–16M, uniform/Zipf/single-hot, FP none/integer counts, 1/2/4 logical threads. Record update throughput, merge time, footprint, and cache-line contention.

### B20. Prefix scan

**Test:** Compare scalar sequential, SIMD intra-block, two-pass blocked, and parallel scan.

**Hypothesis:** SIMD block scans help cache-resident data; parallel scan wins only after synchronization/second-pass overhead amortizes.

**Test plan:** Sweep N, integer widths, inclusive/exclusive semantics, overflow policy, block size, 1/2 threads and two processes over shared memory.

### B21. Parallel reduction

**Test:** Sum/min/max using static contiguous partitions, cyclic partitions, dynamic scheduling, and tree reduction.

**Hypothesis:** Static contiguous partitioning has best locality; dynamic scheduling helps only irregular per-element work; SMT contributes little for bandwidth-bound reductions.

**Test plan:** Sweep footprint, compute intensity, partition grain, FP reproducibility mode, thread/process execution, and physical/SMT placement.

### B22. Radix sort decomposition

**Test:** Benchmark histogram, prefix, scatter, and complete LSD/MSD radix sort separately and together.

**Hypothesis:** Histogram/scatter memory traffic, not digit extraction, dominates; radix width has a cache-capacity optimum.

**Test plan:** Digits 4, 8, 11, 16 bits; 32/64-bit keys; key-only/key-value; uniform/skewed/low-entropy; scalar/SIMD; 1/2 threads with private histograms.

### B23. Parallel comparison sort and merge

**Test:** Compare serial sort, two-way partitioned sort+merge, sample/range partition, and parallel merge.

**Hypothesis:** Two physical cores help beyond a size/grain crossover; skew and duplicates unbalance naive range partitioning.

**Test plan:** Sweep N, distribution, payload, partition sampling, merge implementation, 1/2 physical and 4 logical threads. Include task creation and temporary memory.

### B24. SIMD sorting networks for small arrays

**Test:** Sort fixed arrays of 4–64 keys with insertion, std::sort, branchless network, SSE/AVX2 network, and radix micro-sort.

**Hypothesis:** Sorting networks win fixed small sizes and unpredictable order but perform unnecessary work on nearly sorted input.

**Test plan:** Test every N 1–64, key widths, duplicates, sorted/reverse/random input, inlining, and use as B-tree-node or block-sort primitive.

### B25. Batched ordered lookup

**Test:** Compare independent binary/Eytzinger/B-tree searches with sorted queries, bucketed queries, SIMD lane-parallel search, and breadth-first batched traversal.

**Hypothesis:** Reordering/batching queries converts random accesses into shared cache-line reuse; SIMD helps multiple independent searches more than one branchy search.

**Test plan:** Batch 1–1,024, random/sorted/clustered queries, hit rates, cache tiers, scalar/AVX2, and include query-reorder cost at different reuse counts.

### B26. SIMD search inside B-tree nodes

**Test:** Compare branchy linear, branchless linear, binary, and AVX2 compare+mask within nodes.

**Hypothesis:** SIMD search wins cacheline-sized nodes and medium fanout; binary search wins very large nodes unless batching/prefetch compensates.

**Test plan:** Node keys 4–512, key widths 4–16 bytes, hit position/miss, alignment, and warm/cold node. Integrate winning primitives into L08 end-to-end trees.

### B27. SIMD metadata-filtered hashing

**Test:** Compare scalar tag scanning, SSE 16-byte, AVX2 32-byte, and scalar probing without tags.

**Hypothesis:** Vector tag filtering reduces expensive key comparisons, especially misses and large keys; wider groups can overfetch and worsen insertion movement.

**Test plan:** Sweep tag bits, group width, load factor, hit rate, key/value size, good/adversarial hashes, and cache tier. Measure candidate matches and bytes touched.

### B28. Approximate membership filters

**Test:** Compare Bloom, blocked Bloom, counting Bloom, cuckoo filter, xor-style static filter, and direct hash/set lookup.

**Hypothesis:** Blocked filters improve cache locality; xor/static filters provide favorable lookup/space when updates are unnecessary; false positives determine end-to-end value.

**Test plan:** Sweep bits/key, hash count, target false-positive rate, footprint, positive rate, batch, build/update requirement, and filter+backing-lookup pipeline.

### B29. SIMD graph-neighbor intersection

**Test:** Compare scalar merge/galloping, SIMD block merge, SIMD galloping probes, and bitmap AND+popcount.

**Hypothesis:** SIMD block intersection wins comparable moderate degrees; galloping wins asymmetric degree; bitmap wins dense local universes.

**Test plan:** Sweep degrees, ratio, overlap, clustering, sorted-ID gap distribution, and cache tier. Integrate adaptive dispatch into G07.

### B30. SIMD/parallel edit-distance wavefront

**Test:** Compare row DP, tiled cache-aware DP, diagonal SIMD wavefront, independent-string batching, and two-thread tile wavefront.

**Hypothesis:** SIMD across independent strings is easier and more efficient than within one dependent DP matrix; parallel wavefront needs large tiles/problems.

**Test plan:** Sweep string length/similarity, batch, tile, scalar/AVX2, 1/2 physical threads, and exact correctness. Include scheduler/barrier cost.

### B31. Parallel BFS and frontier ownership

**Test:** Compare shared atomic visited/frontier, per-thread local frontier then merge, bitmap test-and-set, and owner-partitioned vertices.

**Hypothesis:** Local frontiers reduce synchronization; ownership improves writes but can increase remote/irregular edge traversal.

**Test plan:** Run G02 graphs with CPUs 4+6 and 4–7, sweep frontier density and graph shape, report duplicate discoveries, atomic retries, merge time, and speedup.

### B32. Parallel PageRank partitioning

**Test:** Compare edge partition, vertex partition, push with atomics/private accumulation, pull, static scheduling, and dynamic scheduling.

**Hypothesis:** Pull plus contiguous vertex partition gives best locality on two cores; dynamic scheduling helps degree skew but adds scheduling overhead.

**Test plan:** Use uniform/power-law/grid/community graphs, fixed iterations, 1/2 physical and SMT modes. Report imbalance, synchronization, bytes/edge, and numerical delta.

### B33. Thread versus process dense kernel

**Test:** Execute AXPY, GEMV, GEMM, and reduction using one process/two threads versus two pinned processes over shared memory.

**Hypothesis:** Threads have lower coordination/setup cost; persistent processes approach thread steady state for coarse partitions but complicate barriers and shared allocation.

**Test plan:** Use identical partitioning and CPUs 4+6, reusable workers, shared `mmap`/POSIX shm, and work sizes from microseconds to seconds. Report setup and steady state separately.

### B34. Thread versus process DSA kernel

**Test:** Run histogram, radix sort, merge sort, BFS, and hash build using threads versus shared-memory processes.

**Hypothesis:** Process isolation does not improve raw memory bandwidth on one NUMA node; differences arise from runtime, allocator, IPC, and ownership structure.

**Test plan:** Fix algorithm/partition, pin 4+6, compare initialization, shared-state synchronization, private local state+merge, peak RSS/PSS, and steady throughput.

### B35. Copy-on-write after fork

**Test:** Fork after constructing large arrays, graphs, and indexes, then perform read-only, sparse-write, and dense-write work.

**Hypothesis:** Read-only processes cheaply share pages; sparse/dense mutation triggers increasing COW faults and memory bandwidth/footprint.

**Test plan:** Sweep object 32 MiB–8 GiB, modified page fraction 0–100%, random/sequential writes, and two processes. Record faults, PSS/RSS, time, and cache behavior.

### B36. Parallel grain-size crossover

**Test:** Apply the same work through direct serial call, persistent thread pool, OpenMP, work-stealing pool, and persistent process pool.

**Hypothesis:** Scheduler/dispatch overhead defines a minimum profitable task size; irregular work shifts the crossover toward dynamic scheduling.

**Test plan:** Synthetic work 50 ns–100 ms plus real filter/sort/GEMM/graph tasks. Measure enqueue-to-finish latency, throughput, utilization, and p99.

### B37. Memory-bandwidth saturation

**Test:** Scale copy, triad, reduction, gather, SpMV, and PageRank over one physical, two physical, SMT pair, and four logical threads.

**Hypothesis:** Contiguous kernels saturate before all logical CPUs; SMT helps latency-bound irregular kernels more than bandwidth-bound streams.

**Test plan:** Use cache-resident and DRAM footprints, fixed work per thread and fixed total work, report aggregate/per-core bandwidth, efficiency, and memory/LLC counters.

### B38. Nested parallelism and oversubscription

**Test:** Call single/multithreaded BLAS from one/two application threads and processes.

**Hypothesis:** Uncontrolled nested BLAS creates 4–8 runnable threads on two physical benchmark cores, worsening latency and reproducibility.

**Test plan:** Explicitly test outer 1/2 multiplied by inner 1/2/4 threads, affinity on/off, small/large GEMM, and report actual thread count, migrations, throughput, and tails.

### B39. Pipeline fusion versus intermediate materialization

**Test:** Compare separate filter→transform→reduce, decode→intersect, read→checksum→aggregate, and fused blocked implementations.

**Hypothesis:** Fusion wins by removing intermediate traffic until code complexity, reduced vectorization, or cache pressure offsets it.

**Test plan:** Measure each stage, fully materialized pipeline, scalar fused, AVX2 fused, and producer/consumer parallel pipeline across selectivity, footprint, and downstream compute.

### B40. Roofline calibration for interpreting DSA kernels

**Test:** Establish sustainable scalar/AVX2 integer and FP compute, L1/L2/LLC/DRAM bandwidth, random-access latency, and atomic coherence ceilings.

**Hypothesis:** Normalizing each DSA kernel against the relevant ceiling explains whether it is compute, bandwidth, latency, branch, or synchronization bound.

**Test plan:** Use validated microkernels under fixed-frequency and turbo profiles. Calculate attained percentage of appropriate ceilings without pretending one aggregate score describes the CPU.

## 14. Result schema and artifacts

Every raw result row must include at least:

```text
schema_version
run_id
timestamp_utc
test_id
problem
implementation
semantic_contract
phase                 # build/query/update/end_to_end/etc.
dataset_id
seed
n
footprint_bytes
key_bytes
value_bytes
distribution
hit_rate
operation_mix
batch_size
queue_depth
thread_count
process_count
cpu_affinity
topology_mode
wait_strategy
memory_order
io_api
io_buffering_mode
io_durability_mode
compiler
compiler_version
flags
source_commit
binary_sha256
instruction_family
instruction_mnemonic
instruction_encoding_hex
operand_form
register_class
vector_width_bits
element_width_bits
immediate_value
dependency_pattern
independent_chain_count
static_test_instruction_count
dynamic_test_instruction_count
code_alignment
working_set_stride
operand_value_class
kernel
filesystem
device
frequency_profile
effective_frequency
temperature_before
temperature_after
warm_state
repetition
elapsed_ns
cpu_time_ns
tsc_ticks
core_cycles_raw
reference_core_cycles
net_test_core_cycles
latency_core_cycles
reciprocal_throughput_core_cycles
operations
useful_bytes
algorithm_work_class
measured_work_items
estimated_span_steps
algorithm_passes
preprocessing_ns
layout_conversion_ns
output_count
output_bytes
random_access_count
sequential_bytes_read
sequential_bytes_written
vector_lane_slots
vector_active_lanes
worker_busy_ns_min
worker_busy_ns_max
synchronization_count
atomic_retry_count
parallel_speedup
parallel_efficiency
result_checksum
cycles
instructions
retired_uops
issued_uops
decoded_uops
uops_port_0
uops_port_1
uops_port_2
uops_port_3
uops_port_4
uops_port_5
uops_port_6
uops_port_7
branches
branch_misses
tma_retiring_pct
tma_bad_speculation_pct
tma_frontend_bound_pct
tma_backend_bound_pct
tma_fetch_latency_pct
tma_fetch_bandwidth_pct
tma_memory_bound_pct
tma_core_bound_pct
perf_time_enabled
perf_time_running
text_bytes
hot_text_bytes
code_pages_touched
l1_loads
l1_load_misses
llc_loads
llc_load_misses
dtlb_loads
dtlb_load_misses
page_faults
context_switches
cpu_migrations
allocation_count
allocated_bytes
peak_rss_bytes
db_system
db_version
storage_engine
db_cache_bytes
record_count
logical_read_bytes
logical_write_bytes
wal_journal_bytes
engine_read_bytes
engine_write_bytes
device_read_bytes
device_write_bytes
compaction_bytes
checkpoint_or_flush_count
acknowledgment_contract
index_count
compression
status
status_reason
```

Store the following beside each run:

- Machine snapshot and boot command line.
- Full source and binary provenance.
- Dataset generator parameters and checksums.
- One raw timing file per repetition or an unaggregated combined file.
- PMU raw output including event scaling.
- Disassembly/vectorization report for representative kernels.
- I/O capability-probe output and explicit fallback flags.
- Summary tables generated from, never substituted for, raw rows.

## 15. Matrix-control strategy

The complete Cartesian product is intentionally not run. Use four levels:

### Level 0: correctness and capability smoke

- One L1-sized and one DRAM-sized input.
- Two distributions.
- One seed.
- Three repetitions.
- No publication conclusions.
- Validate checksums, feature support, affinity, counters, fallback detection, and cleanup.

### Level 1: core comparison

- All cache-boundary sizes relevant to the hypothesis.
- Three to five relevant distributions.
- Five seeds.
- Fifteen interleaved repetitions.
- Timing first, counter passes second.

### Level 2: crossover refinement

- Add dense parameter points around observed algorithm/cache/load/QD crossovers.
- Repeat on another day/session to measure reproducibility.
- Inspect code and counters before interpreting the curve.

### Level 3: stress and sensitivity

- Adversarial data, thermal soak, background noise, SMT, overload, long aging, cancellation, owner death, and failure cases.
- Label as sensitivity/stress, not normal operation.

## 16. Execution roadmap

### Phase 0. Harness and machine qualification

Implement V01–V08, result schema, correctness adapters, feature probes, topology validation, frequency/temperature logging, and randomized run order.

Exit criteria:

- Hot compute/cache kernels normally have CV below 2%; device/graph tests have a documented broader threshold.
- L1/L2/LLC transitions are visible at plausible sizes.
- No publication row has CPU migration, PMU multiplexing, unverified fallback, or invalid checksum.
- Fixed-frequency and turbo profiles are independently reproducible.

### Phase 1. Atomics and primitive synchronization

Implement SY01–SY20 in this order:

1. Atomic load/store, fetch-add, and CAS.
2. Backoff and false sharing.
3. Mutex and spinlock fast/contended paths.
4. Futex private/shared wait/wake.
5. Wake cardinality, requeue, wait-vector.
6. Semaphore, condition variable, and barrier.

Run SY21–SY23 after permissions and recovery isolation are verified.

### Phase 2. Concurrent queues

Implement CQ01–CQ11 after the atomic/futex primitives are validated. Begin with SPSC, then MPSC, then MPMC, then reclamation and work stealing. Do not infer MPMC scalability from only logical thread count; state the two-physical-core limit prominently.

### Phase 3. High-value problem-shaped DSA

First batch:

1. D01 Two Sum.
2. D03 set intersection.
3. D05/D06 selection and Top-K.
4. D07 Top-K frequent.
5. D08 deduplication.
6. D10/D11 range structures.
7. D13/D14 streaming/window median.
8. D19 word frequency.
9. D24 sparse intersection.

Then implement remaining DSA and lifecycle experiments L01–L15 and Q01–Q05.

### Phase 4. File-I/O control paths

Implement IO01–IO12 first. These establish syscall, IPC, page-cache, mmap, direct-I/O, alignment, and SATA baselines without assuming io_uring superiority.

### Phase 5. io_uring storage

After installing/pinning liburing and fio:

1. Capability probe.
2. IO13–IO17 basic sync-versus-ring and registered-resource tests.
3. IO18–IO23 polling/task-run/nowait/chain/cancel features.
4. IO38 fio cross-check.

### Phase 6. Network and copy avoidance

Implement IO24–IO32 only with an appropriate second host. The existing 100-Mb/s USB adapter is acceptable for control-path exploration but not a definitive zero-copy throughput study. For meaningful network zero-copy work, add a stable gigabit-or-faster NIC and peer.

### Phase 7. BLAS-like and SIMD-DSA kernels

Implement B01–B14 first to establish Level-1/2/3 dense behavior, then B15–B30 for convolution, sparse math, sorting, indexing, filtering, and DP. Compare custom kernels against pinned reference libraries rather than treating library results as ground truth without provenance.

### Phase 8. Parallel and multiprocess kernels

Implement B31–B39 and the applicable graph experiments after single-thread baselines stabilize. Always report:

- Speedup relative to the exact single-thread implementation.
- Total performance and performance per physical core.
- Physical-core versus SMT topology.
- Useful work, duplicate work, synchronization, merge, and setup cost.

### Phase 9. Integrated graph and spatial suite

Complete G01–G12 using scaled generators and correctness checkers. Avoid full GAP data sizes on this 15-GiB laptop. Preserve build/preprocessing costs and show amortization curves.

### Phase 10. Clean-Code and CPU-pipeline attribution

First stabilize selected published Clean-Code rows with CC01. Then implement CC02, CC04, CC05, CC10, CC11, CC15, CC17, CC20, and CC23 as the high-value set. Run the remaining CC experiments only after the generated-code, binary-size, and top-down sidecars are automated; otherwise source-level comparisons will outpace the evidence needed to explain them.

### Phase 11. Storage structures and databases

Do not start DB endurance/compaction work on the nearly full root SSD. Build DB01 in memory first, then use a safe disposable data directory/device for DB02, DB05, DB08–DB14, and DB20. Run DB21 only after both servers, clients, durability contracts, cache budgets, and indexes have been made equivalent. Treat DB03/DB04/DB06/DB07/DB15–DB19/DB22 as the second wave after amplification and maintenance telemetry is trustworthy.

### Phase 12. Algorithm-shape and higher-work crossovers

Add the work/span/traffic instrumentation required by AS01–AS18 before claiming that parallelizability compensates for extra work. Run HW01, HW03–HW05, HW07–HW08, HW10, HW13, HW17, HW19–HW20, and HW24 as the first crossover set. These cover worse-asymptotic streaming scans, sorting, preprocessing, multi-pass parallel algorithms, brute-force SIMD, graph direction switching, replicated state, FFT crossover, packing, and compression. Defer broad many-core conclusions until a host with at least 8 physical cores is available; on this DUT, publish only one-to-two-physical-core and SMT behavior.

### Phase 13. Agner-derived instruction and microarchitecture matrix

Implement IL01–IL10 first; they qualify the generator, dependency-chain method, independent-stream method, reference subtraction, core-cycle counter, and uop/port accounting. No instruction-table result is publishable until these controls reproduce stable, internally consistent values.

Then run the Haswell-supported architectural tests in this order:

1. IL11–IL22: decoders, prefixes, instruction boundaries, loop delivery, uop cache, and jump density.
2. IL23–IL27: conditional/indirect prediction, macro-fusion, return stack, and stack-engine synchronization.
3. IL28–IL33: move elimination, zero idioms, partial registers/flags, execution domains, out-of-order capacity, and SMT resource sharing.
4. IL34–IL48: cache latency/throughput, load/store ports, aliasing, alignment, forwarding, addressing, non-temporal traffic, prefetch, fences, and locked operations.
5. IL49–IL62: AVX/SSE state, floating-point warm-up and exceptional values, FMA, shuffles, gather, packs, conversions, division, strings, x87, and special integer/crypto instructions.
6. IL63–IL64: DAXPY and Taylor-series composite cross-checks tying isolated instruction results to useful kernels.

Run exact-source reproduction and the roadmap's controlled reimplementation as separate implementations. Pin and hash Agner Fog's `testp.zip`; archive every generated assembly/configuration and never merge measurements from different script/archive revisions. AVX-512, scatter, AVX-512 mask-register, and FP16 rows remain capability-gated on Haswell. Re-run the portable subset on a newer AVX-512-capable host later rather than changing the DUT's test semantics.

### Batch execution and publication gate

The roadmap is executed as small, closed batches. A batch begins only after its source, manifest schema, correctness oracle, safety limits, and capability probes are ready. It ends only after raw results, derived tables, disassembly, machine snapshot, and counter sidecars have been retained.

After every completed batch:

1. Create one self-contained article before starting another benchmark batch.
2. The article must state the question, semantic contract, expectation, reason for the expectation, exact DUT state, generated code, measurement construction, raw-data location, results, exceptions, and what counters/disassembly support or fail to support the interpretation.
3. Include static accessible visuals generated from raw data: result distributions, crossover/parameter curves, counter attribution, topology/cache-state comparison, and an annotated data/control-flow diagram where useful. Include source data/CSV behind every chart.
4. Add an optional HTML/SVG animation only when it materially explains a time sequence or structural difference—for example, a dependency chain versus independent streams, a cache-footprint walk, a branch-training timeline, or cache-line ownership ping-pong. Every animation needs an equivalent static figure and text description; never use animation as evidence by itself.
5. Publish negative, unsupported, and inconclusive outcomes with the same prominence as wins. “Expected” and “observed” must be separate fields; no visual or prose may imply an unmeasured explanation.
6. Stop after the article is written and request user permission before beginning the next batch. Corrections to the just-finished batch's article/data are allowed; expanding into a new test family is not.

Suggested article layout:

```text
Title and one-sentence decision
Question and semantic contract
Expectation and architectural rationale
DUT, software, topology, thermal/frequency, and capability manifest
Test construction and correctness checks
What the counters/disassembly can and cannot establish
Results: distributions, curves, and raw-data links
Observed exceptions, failed hypotheses, and unsupported cases
Interpretation boundaries and reproducibility instructions
What this changes in the next-batch priority
```

### Batch 1. Measurement qualification and first integer instruction table

**Purpose:** Prove that the harness can distinguish timing overhead, core cycles, a dependency-chain latency result, and an independent-stream throughput result before it measures caches, SIMD, locks, or DSA algorithms.

**Included tests:** V01 (only profiles that can be safely established), V05, V06, V07, IL01, IL02, IL03, IL04, and IL05. The IL03/IL04 population is deliberately small: register-register integer `add`, a non-zeroing logical/ALU control, `imul`, a zero-idiom control, and one supported shift form. IL05 applies only to these same rows. V02/V03/V04/V08, memory operands, SIMD, and all application/DSA work remain later batches.

**Why this is first:** `add` supplies a low-latency/simple-uop control; `imul` supplies a longer dependency that makes chain construction visible; the zero-idiom distinguishes a broken false dependency from a genuine chain; a variable or immediate shift tests a second operand form. Together they reveal optimized-away work, wrong dynamic counts, insufficient independent streams, counter multiplexing, unstable frequency, and an invalid matched reference with minimal implementation surface.

**Measurement deliverables:** A machine/capability snapshot; CPU-4 affinity proof and CPU-5-idle check; generated assembly and disassembly; empty/control/test timings; TSC/core/reference-cycle comparison; chain-count convergence curve; `cycles/op`, instructions/op, uops/op, and compatible port counters; `time_enabled/time_running`; warm/cache-evicted/page-cold labels; output/checksum and harness self-tests. No value becomes an instruction-table result until its control is near zero and the chain/stream convergence checks pass.

**Expected article:** `Batch 1 — Can this Haswell laptop measure an instruction honestly?` It should contain an animated-or-static two-panel dependency-chain versus independent-stream diagram, a timer-overhead waterfall, a chain-count saturation curve, a TSC-versus-core-cycle/frequency plot, PMU multiplexing side-by-side comparison, and a table that labels every expectation as confirmed, contradicted, or inconclusive. It must not claim a general Haswell instruction table from this calibration subset.

**Exit criteria:** Stable CPU affinity; no unintended spills/vectorization; retained raw repetitions; timing/control plateau at the chosen unroll; safe counter groups established; no material multiplexing; a documented frequency/temperature acceptance window; and validated output. If any fail, the article becomes a qualification-failure report and Batch 1 is corrected/repeated rather than proceeding.

### Batch 1 execution record — 2026-09-07

**Status:** Completed as a qualified narrow integer calibration; article written; next batch explicitly awaits user permission.

- A first preflight run was retained but rejected because it contained only 1–8 static target instructions per body, contrary to Section 3.9's 100+ instruction rule. The corrected accepted run uses 128 static target instructions per generated latency/throughput body; its opcode disassembly is retained with the result.
- Accepted raw evidence: 375 rows, 15 randomized rounds per case, all rows on CPU 4 before and after timing, raw CSV SHA-256 `f699ab0e75da9e86241f7f110a24eae51768f359880928f26223f1eb7b08727e`.
- Long matched dependency chains measured approximately 1 core cycle for immediate `ADD`/`SHL` and 3 core cycles for immediate `IMUL`. Four independent ADD chains reached the observed plateau; eight gave no material improvement under the roadmap's 5% threshold.
- The basic counter group did not multiplex. The deliberately oversized expanded group had a median cycle `time_running/time_enabled` of about 0.498, so its uop/port values are not used for attribution. A dedicated compatible uop/port pass remains required for IL05.
- The page-state probe validated the labels: `page_cold` had roughly 1,027 minor faults and about 2.22 million median TSC ticks versus 3 faults/27 thousand ticks for `warm`.
- Turbo remained enabled and temperature/energy telemetry were absent. Therefore V01 is only partially complete and V02 is not complete; accepted instruction values are core-cycle results for this documented performance profile, not a fixed-frequency generalization.
- Article, visual companion, derived CSVs, raw data, manifest, source, runner, and analyser are retained under `batch1/` in this workspace.

## 17. DUT additions required before execution

### Required for measurement quality

- One privileged setup/restore helper for frequency/turbo, benchmark IRQ verification, and turbostat/RAPL access.
- A quiet benchmark service profile or maintenance session without GUI/X2go and unrelated services for primary results.
- Automatic temperature/frequency logging and cooldown enforcement.
- Runtime verification that CPU 5 is idle during CPU-4 single-thread tests.
- Sufficient filesystem headroom before any I/O test-file allocation.

### Required for Agner-derived instruction tests

- The 2026-09-06 DUT inventory found GCC 13.3, Clang 18.1, `objdump`, `perf`, `/dev/cpu/0/msr`, and kernel build files. Keep those exact versions in the first manifest; add `llvm-objdump` only if its decoded operands are useful as a second disassembler.
- Install a pinned recent NASM release because neither `nasm` nor `yasm` was discoverable in PATH. Record its version and binary hash. This is required to reproduce the NASM/YASM test paths, though compiler-generated inline assembly variants can start earlier.
- Agner Fog's custom `/dev/MSRdrv` device was absent. Prefer Linux `perf_event_open`/`perf stat` for the roadmap implementation. If exact PMCTest counter programming is required, first audit and build the GPL driver from the pinned archive against the running kernel, obtain explicit permission before loading it, record module/source hashes, and unload it after the session.
- Validate every requested raw event against Haswell event encodings and the running kernel's PMU aliases. Agner's scripts span multiple microarchitectures; an accepted event or instruction on another CPU is not automatically meaningful on this DUT.
- Add a generator that emits a source file, exact opcode/disassembly sidecar, dynamic-instruction-count proof, and matched control for every instruction row. The generated binary must fail closed if CPUID and XCR0 do not permit the encoded ISA state.
- Reserve at least one benchmark session for counter-event calibration and positive/negative controls before filling the instruction table. Counter-derived port/uop claims are invalid if event scheduling, skid, or multiplexing has not been bounded.

### Required for io_uring/file-I/O work

- Install or vendor a pinned `liburing` source version and record its commit/release.
- Install a pinned `fio` version for cross-checking.
- Confirm headers match the kernel UAPI used at compile time.
- Build an opcode/setup-feature probe using `io_uring_get_probe` or equivalent.
- Optional observability: `trace-cmd`, relevant tracepoints, `blktrace` where supported, `iostat`, and BPF tools if available and permitted.
- Use an explicit 8-GiB test file initially; never fill the filesystem or benchmark the raw root device destructively.
- Precondition only the test file/range. Limit repeated write endurance tests on the consumer SATA SSD.

### Required for meaningful network zero-copy work

- A second physical host connected without loopback.
- Prefer a dedicated, stable 1-Gb/s or faster Ethernet path; 10 Gb/s would reveal copy cost much more clearly.
- A NIC/driver supporting the exact offloads/features being claimed.
- For io_uring ZC Rx, explicit verification of header/data split, flow steering, queue configuration, and kernel API support.
- Sender/receiver clock synchronization is optional for throughput, but one-way latency requires a validated timing method; otherwise use round-trip latency.

### Required for BLAS-like comparisons

- Pin exact OpenBLAS and/or BLIS versions and build configurations.
- Confirm Haswell dispatch rather than generic kernels.
- Record internal thread count and affinity.
- Set `OPENBLAS_NUM_THREADS`, `OMP_NUM_THREADS`, BLIS runtime configuration, and related variables explicitly per test.
- Keep a single-threaded build/configuration available to prevent hidden internal parallelism.

### Required for broad parallel-algorithm conclusions

- The current DUT can answer single-core versus two-physical-core and SMT questions, but cannot establish a general scaling curve. Add a second machine with at least 8 physical cores for AS01–AS07 and representative HW tests.
- Prefer a system that exposes at least two NUMA nodes for a separately labeled locality/communication extension; never mix its results into the single-node Haswell curves.
- Capture topology with `lscpu`/`hwloc`, pin workers and allocator arenas, calibrate thread-pool submission/barrier/steal cost, and log per-worker busy time.
- Obtain reliable memory-controller bandwidth and energy telemetry where supported. Without it, report process counters and calibrated STREAM-like ceilings but label bandwidth/energy attribution as incomplete.
- Run strong scaling at fixed N and weak scaling at fixed work per physical core. Never use an implementation change between the one-worker baseline and the parallel row unless both algorithm and worker-count effects are shown separately.

### Required for database and storage-engine comparisons

- As of the 2026-09-06 PATH inventory, the DUT had no discoverable `mysql`, `mariadb`, `mongod`, `mongosh`, `rocksdb_bench`/`db_bench`, or `sqlite3` executable. Install or build pinned versions before DB01–DB22; record server, client, storage-engine, libc, and compression-library versions.
- Prefer native services with explicit CPU/memory affinity. If containers are used, use the same container/network/filesystem layering for every engine and record cgroup limits, overlay/direct-volume choice, and host kernel.
- Add a disposable data device with ample free space. The current 240-GB root SSD had only about 25 GB free and was 89% full, which is insufficient for large compaction/write-amplification sweeps and makes destructive recovery tests inappropriate. A dedicated SSD with at least 100–200 GB free is the practical minimum for the core matrix.
- Put every engine on the same physical device for engine comparisons. Use a second device only in a separately labeled WAL/data-placement experiment.
- Capture model/firmware, logical and physical block sizes, scheduler, mount options, discard/TRIM policy, write-cache/FUA behavior, SMART health/temperature, and bytes written. Do not compare a warm/preconditioned drive with a fresh one.
- Use disposable database directories and process-crash tests first. Power-cut durability requires a sacrificial host/device and an externally controlled power method; it must not be attempted on the DUT root filesystem.
- Install pinned workload drivers: a direct structure harness, RocksDB `db_bench`, and one consistent client such as YCSB plus database-specific tools only for cross-checks. Pin driver CPU separately so client work is not attributed to the server.
- Reserve enough RAM to avoid swap and explicitly size InnoDB/WiredTiger/RocksDB caches. Report both cold, warm, and larger-than-cache datasets; never call a page-cache test a storage-media result.

## 18. Interpretation rules

- Big-O is context, not the conclusion; publish cache-tier and workload crossovers.
- A preprocessing-based algorithm must show both query-only and amortized end-to-end curves.
- Do not compare different durability, ordering, fairness, loss, consistency, stability, accuracy, or progress guarantees as if equivalent.
- A spinlock with lower latency but one full CPU consumed is not simply “faster” than a sleeping mutex.
- A dropped queue item is not a completed operation.
- A zero-copy request that reports a copied fallback is not a zero-copy sample.
- An io_uring completion is not necessarily device durability.
- Buffered warm-cache bandwidth is not storage bandwidth.
- SMT threads are not physical cores.
- Multiprocess execution on one NUMA node does not create additional memory bandwidth.
- SIMD speedup must include tails, setup, layout conversion, output materialization, and accuracy.
- Library performance must record its internal thread count and CPU dispatch.
- Publish confidence intervals and raw distributions, not only best-of-N values.

## 19. Research anchors

- [Agner Fog software optimization resources](https://www.agner.org/optimize/) — authoritative index for the five optimization manuals, instruction tables, and public test programs used to design Section 27.
- [Agner Fog instruction tables](https://www.agner.org/optimize/instruction_tables.pdf) — experimental latency, reciprocal-throughput, execution-unit, and instruction-domain definitions and caveats.
- [Agner Fog microarchitecture manual](https://www.agner.org/optimize/microarchitecture.pdf) — pipeline, out-of-order execution, register renaming, branch prediction, cache, decoder, loop/uop-cache, and architecture-specific explanations behind the tests.
- [Agner Fog test programs (`testp.zip`)](https://www.agner.org/optimize/testp.zip) — GPL test harness, list-driven instruction tests, and 37 architectural scripts audited in Section 27; archive inspected 2026-09-06, SHA-256 `af01ca667976653d9a742a3126cf054d00de7c7be837f354309e7f2674305c2d`.
- [Problem Based Benchmark Suite](https://cmuparlay.github.io/pbbsbench/) — problem specifications, generators, checkers, and timing structure.
- [PBBS benchmark list](https://cmuparlay.github.io/pbbsbench/benchmarks/) — sequence, graph, text, geometry, and related kernels.
- [GAP Benchmark Suite](https://github.com/sbeamer/gapbs) — standardized BFS, SSSP, PageRank, connected components, betweenness centrality, and triangle counting.
- [Google Benchmark user guide](https://google.github.io/benchmark/user_guide.html) — repetitions, warm-up, random interleaving, context, counters, and output formats.
- [uarch-bench](https://github.com/travisdowns/uarch-bench) — low-level microbenchmarking, code-generation pitfalls, counters, and frequency control.
- [Abseil containers](https://abseil.io/docs/cpp/guides/container) and [F14 design](https://github.com/facebook/folly/blob/main/folly/container/F14.md) — flat/node storage and SIMD metadata-filtered hash design.
- [Linux futex manual](https://man7.org/linux/man-pages/man2/futex.2.html), [futex_waitv](https://man7.org/linux/man-pages/man2/futex_waitv.2.html), and [Linux memory barriers](https://docs.kernel.org/core-api/wrappers/memory-barriers.html) — synchronization semantics and supported operations.
- [io_uring manual](https://man7.org/linux/man-pages/man7/io_uring.7.html), [io_uring setup](https://man7.org/linux/man-pages/man2/io_uring_setup.2.html), and [registered buffers](https://man7.org/linux/man-pages/man7/io_uring_registered_buffers.7.html) — ring/control and resource-registration semantics.
- [liburing](https://github.com/axboe/liburing) — official userspace helper library, tests, examples, and runtime feature probing.
- [fio documentation](https://fio.readthedocs.io/en/master/fio_doc.html) — io_uring engines, fixed buffers, registered files, SQPOLL, polling, and reproducible storage jobs.
- [Linux `MSG_ZEROCOPY` documentation](https://docs.kernel.org/networking/msg_zerocopy.html) — copy-avoidance threshold/caveats, notifications, fallback reporting, and loopback limitation.
- [io_uring zero-copy receive](https://docs.kernel.org/networking/iou-zcrx.html) — ZC Rx kernel API and NIC hardware requirements.
- [`sendfile`](https://man7.org/linux/man-pages/man2/sendfile.2.html), [`splice`](https://man7.org/linux/man-pages/man2/splice.2.html), and [`copy_file_range`](https://man7.org/linux/man-pages/man2/copy_file_range.2.html) — in-kernel transfer semantics.
- [vDSO manual](https://man7.org/linux/man-pages/man7/vdso.7.html) — userspace-assisted system services versus real syscalls.
- [BLIS performance methodology](https://github.com/flame/blis/blob/master/docs/Performance.md) and [BLIS multithreading](https://github.com/flame/blis/blob/master/docs/Multithreading.md) — Level-3 operation measurement, Haswell reference data, affinity, and parallel loop structure.
- [OpenBLAS runtime controls](https://www.openmathlib.org/OpenBLAS/docs/runtime_variables/) — explicit thread and Haswell-kernel selection.
- [Spatter](https://github.com/hpcgarage/spatter) — gather, scatter, concurrent gather/scatter, and multi-level irregular access patterns.
- [Intel Top-Down Microarchitecture Analysis](https://www.intel.com/content/www/us/en/docs/vtune-profiler/cookbook/2023-0/top-down-microarchitecture-analysis-method.html) and [CPU metrics reference](https://www.intel.com/content/www/us/en/docs/vtune-profiler/user-guide/2025-1/cpu-metrics-reference.html) — Retiring, Bad Speculation, Front-End/Back-End Bound, fetch, memory, core, and drill-down metric interpretation.
- [Intel instruction-cache miss recipe](https://www.intel.com/content/www/us/en/docs/vtune-profiler/cookbook/2024-0/instruction-cache-misses.html) — code-footprint, hot/cold layout, and instruction-front-end diagnosis.
- [MySQL InnoDB clustered and secondary indexes](https://dev.mysql.com/doc/refman/8.4/en/innodb-index-types.html) and [redo log](https://dev.mysql.com/doc/refman/8.4/en/innodb-redo-log.html) — physical index and durability semantics.
- [MongoDB WiredTiger](https://www.mongodb.com/docs/manual/core/wiredtiger/), [journaling](https://www.mongodb.com/docs/manual/core/journaling/), and [write performance](https://www.mongodb.com/docs/manual/core/write-performance/) — cache, checkpoint, WAL, compression, concurrency, and index-write costs.
- [WiredTiger B-tree architecture](https://source.wiredtiger.com/develop/arch-btree.html) — why MongoDB/WiredTiger is not a proxy for an SSTable-based LSM comparison.
- [LevelDB implementation notes](https://github.com/google/leveldb/blob/main/doc/impl.md), [RocksDB compaction](https://github.com/facebook/rocksdb/wiki/Compaction), and [RocksDB tuning guide](https://github.com/facebook/rocksdb/wiki/RocksDB-Tuning-Guide) — memtable/SSTable lifecycle and read, write, and space amplification.
- [CMU work/depth overview](https://www.cs.cmu.edu/~scandal/cacm.html) and [Blelloch's parallel-algorithms chapter](https://www.cs.cmu.edu/~blelloch/papers/BM10.pdf) — total work, dependency depth/span, and available parallelism as separate algorithm properties.
- [Berkeley Lab Roofline model](https://amcr.lbl.gov/departments/computer-science-department/ppan/roofline-performance-model/) — arithmetic intensity and memory-bandwidth/compute ceilings; extend the same accounting to comparisons, edges, probes, and useful output rather than FLOPs alone.
- [GraphBLAS](https://graphblas.org/) — expressing graph work as sparse linear-algebra primitives that expose batching, vectorization, and parallel execution.

## 20. Immediate first implementation milestone

The first mergeable benchmark milestone should contain only:

1. Run manifest/schema and machine snapshot.
2. V01–V08 validation.
3. SY01 atomic load/store.
4. SY02 shared/striped/per-thread counter.
5. SY03/SY05 CAS and backoff.
6. SY08/SY09 mutex paths.
7. SY13 futex paths.
8. CQ01–CQ03 SPSC.
9. IO01 syscall entry.
10. IO02 vDSO.
11. IO04 IPC notification.
12. B01–B03 BLAS-1 calibration.

This milestone establishes trustworthy timing, coherence, scheduling, syscall, and memory ceilings before the larger DSA and io_uring matrix is implemented.

## 21. Current slash-tmp.blog pure-benchmark baseline registry

This section distinguishes already-published empirical work from planned work. The roadmap originally assumed the existing cache suites rather than enumerating them, so this registry is normative.

Status tags used from this point onward:

- `BLOG-BASELINE`: already benchmarked and published; retain for regression and reproducibility.
- `EXTENSION`: the roadmap asks a materially new question using an existing baseline.
- `NEW`: no matching empirical suite was found on the blog during the 2026-09-06 audit.
- `EXPLANATORY-ONLY`: the blog discusses the topic but does not publish a corresponding controlled benchmark run.

### 21.1 Cache-search-lab raw-memory baselines

The following are `BLOG-BASELINE`:

```text
micro_associativity_conflict
micro_cold_warm_scan
micro_dependent_pointer_chase
micro_huge_page_compare
micro_linear_scan
micro_mlp_pointer_chase
micro_pointer_chase
micro_prefetch_distance_sweep
micro_prefetch_pointer_chase
micro_random_access
micro_stride_scan
micro_tlb_page_walk
micro_working_set_ladder
micro_write_traffic
```

Roadmap relationship:

- Retain these exact cases unchanged as historical regression rows.
- V01–V08 repair frequency, topology, cold-state, compiler, and PMU methodology.
- S03, S04, S08, S09, B04, B37, and B40 are extensions, not replacements.
- Add the finer 32-KiB, 256-KiB, and 6-MiB boundary ladder because the published ladder did not tightly bracket the DUT caches.

### 21.2 Cache-search-lab linked-list baselines

The following are `BLOG-BASELINE`:

```text
arena_list
bloom_chunked_list
chunked_list
chunked_prefetch_list
compare_all
footprint_mutation_hitpos
heap_list
hot_cold_list
indexed_list
multi_lane_list
mutation_costs
prefetch_list
random_arena_list
```

Roadmap relationship:

- Q04, L14, and L15 are `EXTENSION`: sustained churn, allocator fragmentation, intrusive/index/offset forms, and TLB-aware aging.
- CQ10 is a new concurrent-reclamation extension rather than a traversal variant.

### 21.3 Cache-search-lab ordered-search baselines

The following are `BLOG-BASELINE`:

```text
blocked_array_search
branchless_binary
cacheline_block_search
compare_all
duplicate_lower_bound
eytzinger_prefetch
eytzinger_search
implicit_btree_search
linear_vs_binary
power2_padding_search
query_ordering
```

Roadmap relationship:

- L07/L08 and B24–B26 are `EXTENSION`: mutable ordered structures, B-tree fanout, SIMD node search, batch scheduling, and preprocessing amortization.
- Exact lookup versus lower/upper-bound versus range-output semantics must remain separate.

### 21.4 Cache-search-lab hash baselines

The following are `BLOG-BASELINE`:

```text
batched_prefetch_lookup
bucketized_hash
bytes_touched_lookup
chained_arena
chained_heap
compare_all
dense_id_array
hot_cold_hash
linear_probing
research_sweeps
robin_hood
set_conflict_hash
tag_split_hash
tombstone_degradation
```

Roadmap relationship:

- L01–L06 and B27/B28 are `EXTENSION`: Swiss/F14-style metadata, cuckoo/hopscotch, full lifecycle aging, resize tails, heterogeneous keys, hash quality, and approximate-membership pipelines.
- Preserve the original tag/bucket/payload points so new implementations connect to published curves.

### 21.5 Cache-search-lab multithread baselines

The following are `BLOG-BASELINE`:

```text
batched_hash_scaling
chunked_list_scaling
counters_padding
false_sharing
read_only_binary_search_scaling
read_only_hash_scaling
read_only_hash_variant_scaling
read_only_stream_bandwidth
```

Roadmap relationship:

- SY01–SY23, CQ01–CQ11, B31–B38, and the parallel graph cases are primarily `NEW`.
- SY02/SY07, C-style read scaling, and B37 are `EXTENSION` where they share a primitive with these baselines.
- Reinterpret historical 4-thread rows as two physical cores with SMT, not four-core scaling.

### 21.6 Cache-SIMD-DSA baselines

The blog already benchmarks four mappings—cache-unfriendly scalar, cache-aware scalar, SIMD-oriented AVX2, and cache-aware+AVX2—for these ten problems:

```text
stock profit
product except self
maximum subarray
trapping rain water
K-window maximum
rotated-array search
sort 0/1/2
BFS
matrix transpose
matrix multiplication
```

These are `BLOG-BASELINE`, not new roadmap ideas.

Roadmap relationship:

- Q02 extends K-window maximum with genuinely different algorithms and online/offline semantics.
- G02/G03/B31 extend BFS across graph shapes, representations, frontier formats, and parallel ownership.
- B12 extends transpose across cache tiles, edge dimensions, in-place behavior, and thread placement.
- B07–B11 extend matrix multiplication into loop-order, packing, microkernel, shape, batching, library, and parallel studies.
- The other six exact problem cases should remain regression rows but should not be advertised as newly added DSA coverage.

### 21.7 Clean-Code Tax empirical baselines

`clean-code-tax-measured` is also a real empirical suite and must not be listed as explanatory-only. The published runner registers 84 `clean_tax_` benchmarks and produces 265 named contrast rows across five sizes. Its 24 benchmark families are:

```text
small functions
virtual dispatch
smart pointers
exceptions
std::function
RAII in a hot loop
defensive validation
builder pattern
DTO mapping
logging and tracing
reactive pipeline
service boundary
connection pooling
cache layer
string concatenation
map choice
iterator/index/raw-pointer loops
stream I/O
reflection-style access
JSON-shaped parsing
allocation policy
atomics
ORM-style lazy loading
serialization
```

Published named contrasts explicitly include `small_noinline_helper`, `virtual_vs_soa`, `shared_ptr_vs_ref`, `exception_no_throw_vs_error`, `exception_1pct_vs_error`, `exception_50pct_vs_error`, `std_function_vs_lambda`, `raii_string_vs_reuse`, `validation_recheck`, `builder_vs_template`, `dto_layered_vs_direct`, `logging_eager_vs_none`, `tracing_every_vs_boundary`, `reactive_pipeline_vs_fused`, `service_serialized_vs_direct`, `connection_new_vs_pool`, `variant_vs_tagged`, `unordered_map_vs_flat`, `linear_scan_vs_hash`, `reflection_string_vs_direct`, `json_dom_vs_stream`, `alloc_heap_vs_arena`, `alloc_heap_vs_pool`, `atomic_global_vs_local`, `orm_lazy_vs_raw`, and `serialization_json_vs_binary`.

Roadmap relationship:

- These exact registered rows are `BLOG-BASELINE`; do not present basic inline-versus-`noinline`, virtual-versus-SoA, lambda-versus-`std::function`, heap-versus-arena/pool, or global-atomic-versus-local-flush as new.
- Rerun selected noisy published contrasts under V01–V08 with fixed-frequency controls, longer samples, randomized order, and non-multiplexed PMU sidecars.
- CC01–CC24 below are `EXTENSION`: they factor confounded comparisons, sweep code size and target entropy, separate front-end from data-layout effects, and test compiler/linker boundaries rather than merely adding more repetitions.
- X24–X26 remain allocation extensions because they measure size/lifetime distributions, cross-thread ownership, fragmentation, page faults, and zeroing rather than only per-item heap-versus-pool policy.

### 21.8 Blog notes that are not empirical baselines

At the time of audit, these relevant notes were explanatory rather than equivalent controlled benchmark suites:

- `simd-mapping`.
- `mutex-internals-hardware-futex`.
- `linux-io-path-vfs-block-nvme`.
- `packet-flow-nic-dma-kernel-xdp-dpdk`.
- `dsa-state-space-exploration-superset-professional-playbook`.
- `memory-path-animation`.

Therefore the new synchronization, io_uring, no-copy, BLAS, backtracking, DP, and state-space experiments below are not duplicates merely because their concepts are discussed in an article.

### 21.9 Summary crosswalk

| Existing blog family | Already measured | Planned disposition |
|---|---|---|
| Raw memory/cache/TLB/prefetch | Yes | Preserve; rerun under corrected protocol; extend edge cases |
| Linked layouts | Yes | Preserve; add long-lived churn, allocation, reclamation |
| Binary/blocked/Eytzinger search | Yes | Preserve; add mutable trees, batching, range semantics |
| Chained/open-addressed/tagged hash | Yes | Preserve; add modern metadata tables and lifecycle workloads |
| Basic read-only scaling/false sharing | Yes | Preserve; add full atomics, locks, futexes, queues, processes |
| Ten Cache-SIMD DSA problems | Yes | Preserve; extend four of them; do not relabel as new |
| Clean-Code Tax (84 registered benchmarks; 265 contrast rows) | Yes | Preserve; stabilize noisy rows; add isolated CPU-front-end/compiler-boundary extensions |
| State-space/backtracking/DP guide | Explanation only | Add empirical state-representation and search tests |
| Mutex/futex internals | Explanation only | Add SY benchmark suite |
| Linux I/O/io_uring concepts | Explanation only | Add IO benchmark suite |
| Packet flow/zero-copy concepts | Explanation only | Add hardware-gated network suite |

## 22. Additional gaps found after the blog-to-roadmap audit

The 186-test roadmap was broad, but the cross-audit exposed several still-missing families. The following are additional experiments, not merely suggestions.

### X01. Subset and permutation state representation

**Test:** Enumerate/count subsets and permutations using copied vectors, in-place choose/undo, boolean-used arrays, and integer bitmasks.

**Hypothesis:** Bitmasks and in-place undo reduce state footprint/allocation, while copied state can remain competitive at very small N through simpler code.

**Test plan:** Separate enumeration from count-only semantics. Sweep N until feasible, output/no-output, payload, pruning, recursive/iterative traversal, and allocator. Report states/s, bytes/state, allocations, maximum stack, and output cost.

### X02. N-Queens constraint representation

**Test:** Solve/count N-Queens using board scans, boolean column/diagonal arrays, 64-bit bitboards, symmetry reduction, and parallel first-row splitting.

**Hypothesis:** O(1) bitboard legality dominates board scans; symmetry reduction reduces work; parallelism helps only once subtrees are sufficiently balanced.

**Test plan:** N=8 upward within runtime limits, count-only with verified known counts, scalar bit operations versus BMI-capability-gated variants, one/two threads and processes. Record nodes explored and time/node.

### X03. Sudoku/backtracking heuristic

**Test:** Compare naive cell order, minimum-remaining-values selection, bitmask candidates, constraint propagation, and exact-cover/DLX.

**Hypothesis:** State-space reduction dominates micro-optimization; compact bitmasks reduce per-node cost, while DLX wins hard sparse-constraint instances.

**Test plan:** Use easy/medium/hard/adversarial fixed corpora, validate unique/valid solutions, separate preprocessing, record nodes, backtracks, branch misses, footprint, and p99 puzzle latency.

### X04. Word Break engine and dictionary layout

**Test:** Decide/enumerate segmentations using naive recursion, memoized top-down, bottom-up DP, BFS over indices, sorted dictionary, hash set, and trie.

**Hypothesis:** Memoization removes exponential repetition; trie avoids substring hashing/copying; the best engine depends on dictionary prefix sharing and output semantics.

**Test plan:** Sweep string length, dictionary size, word length, prefix overlap, solvable/unsolvable/adversarial strings, decision/count/enumeration contracts, and cache footprint.

### X05. LCS state layout

**Test:** Compute longest common subsequence using recursive memoization, full bottom-up table, two rows, cache tiles, bit-parallel algorithms where applicable, and wavefront parallelism.

**Hypothesis:** Two-row/tiled DP reduces memory traffic; top-down wins sparse reachable states; bit-parallel wins bounded alphabets; parallel wavefront needs large matrices.

**Test plan:** Sweep two string lengths independently, alphabet, similarity, repeated characters, reconstruction versus length-only, one/two threads, and verify sequence/value.

### X06. Grid components and flood fill

**Test:** Count connected components using recursive DFS, iterative stack DFS, BFS queue, union-find, bitmap visited, in-place marking, and row-run union.

**Hypothesis:** Dense grids favor contiguous row/bitmap methods; sparse clustered grids favor frontier traversal; recursion risks stack limits and adds call overhead.

**Test plan:** Sweep dimensions, occupancy 1%–99%, cluster size, 4/8-neighbor semantics, mutable/immutable input, and one/two threads. Report cells visited, queue peak, memory, and components/s.

### X07. Grid/state shortest path

**Test:** Compare BFS, 0-1 BFS, Dijkstra, bucketed shortest path, and A* on grids/state spaces with different edge costs and heuristics.

**Hypothesis:** Specialized queues dominate when weights are bounded; A* reduces explored states only with informative heuristics and pays heap/heuristic overhead otherwise.

**Test plan:** Use obstacle densities, corridor/maze/open maps, unit/0-1/small/wide weights, near/far targets, and exact distance validation. Record states expanded, queue work, and time/state.

### X08. Bidirectional BFS

**Test:** Compare unidirectional and bidirectional BFS with alternating, smaller-frontier-first, and fixed-direction expansion.

**Hypothesis:** Bidirectional search reduces explored state exponentially for symmetric high-branching spaces but adds intersection and dual-visited overhead on shallow/low-branching cases.

**Test plan:** Use implicit word-ladder-like graphs, grids, and explicit random graphs; vary distance and branching; compare hash/bitmap visited and frontier intersection methods.

### X09. Binary-tree layout and traversal

**Test:** Traverse equivalent trees stored as heap nodes, arena nodes, indices, breadth-first arrays, preorder arrays, and van-Emde-Boas-like layouts.

**Hypothesis:** Contiguous preorder/BFS layouts improve full traversals; vEB-like layouts improve mixed subtree/path queries; pointer trees suffer TLB/cache misses.

**Test plan:** Balanced/skewed/random trees, DFS/BFS/search/path-sum, recursive/iterative/Morris where legal, payload 0–256 bytes, cache ladder, and allocation/build costs.

### X10. Tree construction and serialization

**Test:** Reconstruct trees from traversals and serialize/deserialize using copied subranges, index ranges+hash lookup, iterative stack, pointer nodes, and arena/index nodes.

**Hypothesis:** Range indices and dense arenas eliminate repeated scans/copies; serialization format and allocation dominate large trees.

**Test plan:** Sweep balanced/skewed shape, node count, key width, duplicate policy, text/binary formats, allocator, and one-shot versus reuse. Verify structural equality.

### X11. Suffix indexing and repeated-substring query

**Test:** Compare suffix array construction/search, suffix automaton, suffix tree/radix form, rolling-hash+bisection, and direct search.

**Hypothesis:** Suffix arrays give compact locality for repeated queries; automata provide linear scanning at larger memory cost; rolling hash is fast but needs collision verification.

**Test plan:** Use random, repetitive, natural-language-like, and low-alphabet data; sweep text/query length, query reuse, memory, construction, exact search, LCP, and longest duplicate substring.

### X12. Palindrome algorithms

**Test:** Compare center expansion, DP table, rolling-hash verification, and Manacher for longest/count/all palindrome tasks.

**Hypothesis:** Center expansion wins ordinary small inputs; Manacher wins worst-case repetitive strings; full DP has high memory cost but supports richer range queries.

**Test plan:** Sweep length, alphabet, repetition, all-same/adversarial strings, longest/count/materialize contracts, and cache footprint. Validate exact boundaries.

### X13. Hash join versus sort-merge join

**Test:** Join two key/payload relations using nested loop, hash join, sort-merge, radix-partitioned hash, and index nested-loop.

**Hypothesis:** Hash join wins unsorted equi-joins, sort-merge wins ordered/reused inputs, radix partitioning restores cache locality beyond LLC, and nested loop/SIMD wins tiny sides.

**Test plan:** Sweep side-size ratio, match multiplicity, skew, sortedness, payload, output cardinality, cache tier, and build reuse. Report build/probe/output separately.

### X14. Group-by aggregation

**Test:** Aggregate key/value pairs using dense array, flat hash, sort+runs, radix partitioning, tree map, and per-thread local aggregation.

**Hypothesis:** Cardinality/density and skew choose the winner; parallel shared hash collapses on hot keys while local aggregation+merge scales.

**Test plan:** Sweep N, cardinality, Zipf skew, aggregate width, stable output order, one/two threads/processes, and include final ordering/materialization.

### X15. Data partitioning and scatter

**Test:** Partition records into K buckets using atomic counters, histogram+prefix+scatter, per-thread buffers, software write combining, and radix passes.

**Hypothesis:** Two-pass histogram/prefix avoids atomic contention; buffered scatter reduces random write allocation; very large K overwhelms cache.

**Test plan:** K=2–65,536, uniform/skewed keys, records 4–256 bytes, stable/unstable contracts, scalar/AVX2, one/two threads. Record write amplification and temporary memory.

### X16. Cardinality and frequency sketches

**Test:** Compare exact hash/dense counts with HyperLogLog-like cardinality, Count-Min Sketch, and sampled estimators.

**Hypothesis:** Sketches trade bounded error for fixed cache-resident state and win once exact state leaves cache; skew affects error and update contention.

**Test plan:** Sweep distinct count, stream length, skew, sketch width/depth/registers, merge across threads/processes, and report update throughput, memory, relative error, and merge time.

### X17. Streaming quantiles

**Test:** Compare exact sort/tree/heaps with fixed histogram and representative quantile sketches.

**Hypothesis:** Approximate sketches offer cache-bounded updates and mergeability; exact structures win small/bounded domains.

**Test plan:** Uniform, normal, Zipf, multimodal, and phase-changing streams; query p50/p90/p99/p99.9; sweep error/memory budget, one/two workers, and report rank error plus latency.

### X18. Persistent vector/tree update

**Test:** Compare full copy, copy-on-write pages, path-copy persistent tree/vector, mutable structure with snapshots, and reference-counted structural sharing.

**Hypothesis:** Structural sharing wins sparse updates and many snapshots but adds indirection/refcount cost to reads; full copy can win tiny state.

**Test plan:** Sweep state size, update locality/fraction, snapshot lifetime/count, reader count, payload, and allocator. Report update/read time, copied bytes, retained memory, and destruction cost.

### X19. HAMT versus tree/hash map

**Test:** Compare hash-array-mapped trie with flat hash, node hash, B-tree, and copy-on-write map for immutable/versioned workloads.

**Hypothesis:** HAMT offers predictable structural sharing but loses steady mutable lookup to flat hash due to depth/indirection.

**Test plan:** Measure lookup, insert-version, delete-version, iteration, branching factor, hash quality, key/value size, versions alive, and memory/version.

### X20. Succinct rank/select

**Test:** Compare raw byte/bit scans, prefix-count blocks, multi-level rank directory, select samples, and compressed bitmap representation.

**Hypothesis:** Small auxiliary directories produce near-constant queries with good cache locality; over-indexing increases footprint enough to hurt large bitmaps.

**Test plan:** Sweep bitmap size/density/clustering, directory block size, query mix/locality, build reuse, scalar/POPCNT/AVX2 implementations, and memory overhead.

### X21. Cache-oblivious versus cache-aware layout

**Test:** Compare linear, explicitly blocked, recursive cache-oblivious, and van-Emde-Boas-like layouts for matrix traversal, tree search, and recursive divide/recombine.

**Hypothesis:** Cache-oblivious layouts are robust across levels but tuned blocking wins a known Haswell cache; recursion/cutoff overhead hurts small inputs.

**Test plan:** Sweep recursion cutoff, cache ladder, rectangular shapes/tree heights, cold/warm state, and compiler inlining. Run on a second cache geometry when available.

### X22. External merge sort

**Test:** Sort data larger than RAM using chunk sort+merge with buffered synchronous I/O, mmap, direct I/O, and io_uring pipeline.

**Hypothesis:** Run size/fan-in and overlap dominate; io_uring helps by overlapping reads/writes/merge rather than changing comparison cost.

**Test plan:** Run only with safe disk headroom or a separate device. Sweep run size, merge fan-in, block/QD, compression, one/two workers, and report CPU, read/write amplification, and end-to-end durability semantics.

### X23. File-backed ordered index

**Test:** Compare mmap sorted array, page-aligned B+ tree, append/log+index, and small LSM-like levels for point/range lookup and updates.

**Hypothesis:** Page-oriented B+ trees give predictable random I/O; mmap excels warm reads; LSM batching improves writes at compaction/read-amplification cost.

**Test plan:** Use a disposable test file, buffered/direct modes, point/range/read-update mixes, cache states, compaction included/excluded, and report amplification, latency tails, and recovery metadata.

### X24. General allocator size/lifetime matrix

**Test:** Compare system allocator and selected alternatives for allocate/free/realloc/aligned allocation under realistic size/lifetime distributions.

**Hypothesis:** Thread caches win small repeated allocations, fragmentation dominates mixed long-lived objects, and aligned/large allocations cross into mmap-backed behavior.

**Test plan:** Sizes 8 bytes–16 MiB, fixed/lognormal/bimodal sizes, LIFO/FIFO/random frees, short/long-lived mixes, one/two threads, and measure latency histogram, RSS, fragmentation, faults, and syscalls.

### X25. Cross-thread allocation and free

**Test:** Allocate on one CPU/thread and free on another versus same-thread ownership and pooled return-to-owner.

**Hypothesis:** Remote frees create allocator synchronization/cache transfers; ownership queues improve steady throughput at added reclamation delay.

**Test plan:** CPUs 4+6 and 4+5, object 16–4,096 bytes, batch 1–1,024, lifetime/skew, allocator variants, and report throughput, p99, retained bytes, and coherence counters.

### X26. First-touch page fault and zeroing

**Test:** Allocate with malloc/mmap and touch pages sequentially, randomly, read-first, and write-first.

**Hypothesis:** Anonymous write-first cost is dominated by page allocation/zeroing; read-first can map the shared zero page; prefaulting removes faults from timed algorithms.

**Test plan:** Map 4 KiB–8 GiB, touch 1 byte/page and full pages, `MAP_POPULATE` where available, one/two threads/processes. Record minor faults, cycles/page, bandwidth, and RSS/PSS.

### X27. Transparent and explicit huge pages

**Test:** Compare normal 4-KiB pages, THP `madvise`, THP-disabled mapping, and explicit huge pages if configured.

**Hypothesis:** Huge pages reduce TLB misses for large random/streaming structures but increase allocation/compaction cost and internal fragmentation.

**Test plan:** Use pointer chase, hash, B-tree, graph, GEMM packing, and large sequential scans. Verify actual page sizes through smaps/counters; report allocation plus steady-state separately.

### X28. TLB shootdown and mapping mutation

**Test:** Repeatedly `mprotect`, `madvise`, unmap/remap, or change shared mappings while another core reads them.

**Hypothesis:** Cross-core page-table invalidation creates latency spikes that ordinary TLB walk tests miss.

**Test plan:** Use disposable mappings, CPUs 4+6, page counts 1–65,536, batch changes, one/multiple readers, and record mutation time, reader stalls, IPIs/tracepoints if accessible, and p99.

### X29. Instruction-cache and code-footprint pressure

**Test:** Execute equivalent dispatch/algorithm bodies with increasing code size and alignment.

**Hypothesis:** Template/unrolled/SIMD specialization can cross instruction-cache/uop-front-end limits even while reducing data-side work.

**Test plan:** Generate 1–1,024 distinct operations/functions, sequential/random dispatch, direct/indirect calls, small/large bodies, and record cycles, instruction-cache/front-end events where supported.

### X30. Branch predictor and BTB capacity

**Test:** Measure conditional and indirect branch streams with controlled predictability and target count.

**Hypothesis:** Branchless transformations help unpredictable conditions; indirect-call performance collapses after target/BTB capacity thresholds; sorted/clustered data retrains prediction.

**Test plan:** Probabilities 0–100%, periodic/random/Markov patterns, 1–4,096 branch sites/targets, warmed/phase-changing sequences. Record misses and recovery after distribution changes.

### X31. Store-to-load forwarding

**Test:** Store then load identical, partially overlapping, differently sized, and misaligned regions at controlled distances.

**Hypothesis:** Exact compatible overlaps forward efficiently; partial/misaligned size mismatches stall or replay.

**Test plan:** Sweep store/load widths 1–32 bytes, offsets, dependency chain length, cache-line/page splits, scalar/SSE/AVX2, and compare dependent latency with independent throughput.

### X32. 4-KiB alias and memory disambiguation

**Test:** Interleave loads/stores to addresses sharing or not sharing low 12 address bits.

**Hypothesis:** Potential aliases can delay speculative loads even when physical addresses differ; spacing/pattern history changes prediction.

**Test plan:** Sweep number of streams, matching low bits, true alias/no alias, warm training state, and dependent/independent work. Record cycles and machine-clear/replay events where supported.

### X33. Hardware prefetch boundary behavior

**Test:** Continue regular streams across cache-line, 4-KiB page, 2-MiB huge-page, and allocation boundaries.

**Hypothesis:** Prefetch effectiveness changes at page/stream boundaries and with too many concurrent streams; software prefetch can bridge some cases.

**Test plan:** Sweep stride, forward/backward direction, stream count, boundary alignment, normal/huge pages, and software-prefetch distance. Report useful versus excess memory traffic.

### X34. Cache-coherence ownership latency

**Test:** Ping-pong one line between cores using ordinary publication, exchange, CAS, and batched ownership of K operations.

**Hypothesis:** Line transfer has a fixed inter-core cost; batching multiple updates per ownership handoff raises useful work per transfer; SMT has a distinct tradeoff.

**Test plan:** Compare 4+6, 4+5, 4+0 and lines local/adjacent/padded. Sweep batch 1–65,536, read-only sharing versus write ownership, and report cycles/handoff and useful ops.

### X35. RCU/immutable snapshot read-mostly structure

**Test:** Compare mutex, reader-writer lock, seqlock validation, atomic shared pointer/copy publication, and epoch/RCU-like reclamation for a lookup table.

**Hypothesis:** Immutable/RCU-like publication gives fastest reads under rare updates, at copy/reclamation memory cost; seqlock readers retry during long writes.

**Test plan:** Sweep read/write ratio, structure size, update fraction, reader duration, stalled reader, and one/two readers. Report read/write p99, retries, copied bytes, and unreclaimed memory.

### X36. Lock-free stack and ABA/reclamation

**Test:** Compare mutex stack, Treiber stack with unsafe no-reclaim test mode, tagged-pointer mitigation, hazard pointers, and epoch reclamation.

**Hypothesis:** CAS stack wins low contention only with suitable reclamation; safety mechanisms materially change throughput and memory retention.

**Test plan:** Correctness-stress first, then push/pop mixes, empty/nonempty, one/two/four logical threads, delayed-thread injection, and report CAS retries, latency, retained nodes, and ABA detection. Never publish unsafe mode as viable.

### X37. Hardware transactional memory capability

**Test:** Probe Intel RTM/HLE availability and, only if enabled, compare transactional critical sections with mutex and fallback lock.

**Hypothesis:** This Haswell CPU/microcode may expose no usable TSX; if available, transactions help small low-conflict sections but abort under conflicts, capacity, syscalls, or interrupts.

**Test plan:** Capability gate first. If supported, sweep read/write set, conflicts, cache footprint, interrupts, and fallback. Report abort reasons and never omit fallback time.

### X38. One-time initialization and thread-local access

**Test:** Compare static local initialization guard, `pthread_once`, `std::call_once`, manual atomic state, eager initialization, and thread-local access/first construction.

**Hypothesis:** Fast paths are cheap after initialization but first-use contention and TLS construction/destruction create spikes invisible in steady state.

**Test plan:** Measure cold first call, concurrent first call, warmed calls, many once objects/TLS variables, thread creation, and exception/failure paths where semantics permit.

### X39. File-locking primitives

**Test:** Compare `flock`, POSIX record locks, open-file-description locks, process-shared mutex, and lock-file create/rename for equivalent exclusion scopes.

**Hypothesis:** Kernel file locks cost more than shared-memory locks but provide crash/process/file semantics; contention and range granularity dominate.

**Test plan:** One/two processes, uncontended/contended, whole/range locks, lock hold 0–100 us, owner exit, local ext4 only. Validate semantics and report acquisition tails/system CPU.

### X40. `vmsplice` and `tee` pipe paths

**Test:** Compare write→pipe, `vmsplice`→pipe, splice file→pipe, `tee` pipe duplication, and read/write fan-out.

**Hypothesis:** Page-based pipe operations reduce copying for large aligned buffers but setup/page ownership overhead loses on small messages.

**Test plan:** Payload 1 KiB–4 MiB, aligned/unaligned, one/multiple consumers, mutation-after-submit safety, checksum, pipe capacity, and CPU/throughput. Detect copied/fallback behavior where possible.

### X41. TCP copy/batching option matrix

**Test:** Compare Nagle on/off, `TCP_CORK`, `MSG_MORE`, one send versus `writev/sendmmsg`, and socket-buffer sizes.

**Hypothesis:** Batching/corking dominates small writes; disabling Nagle helps request-response latency but can reduce efficiency; the current 100-Mb/s adapter limits throughput conclusions.

**Test plan:** Use a second host, request-response and streaming workloads, 1–64 KiB messages, burst sizes, RTT conditions, and report packet count, latency, goodput, CPU, and retransmissions.

### X42. UDP batching and loss behavior

**Test:** Compare send/recv, `sendmmsg/recvmmsg`, io_uring, and optional zero-copy send for datagrams.

**Hypothesis:** Batching reduces syscall cost for small datagrams; queue overflow/loss appears before CPU limits on the current link; zero-copy loses for small packets.

**Test plan:** Second host, payload 64–65,000 bytes, offered-rate sweep through overload, batch 1–128, socket buffer sizes, and report sent/received/lost/reordered, CPU, and p99.

### X43. PACKET_MMAP and AF_XDP capability

**Test:** Compare ordinary UDP/socket capture with PACKET_MMAP; probe AF_XDP copy and zero-copy modes only where driver/NIC permits.

**Hypothesis:** Kernel-bypass/shared-ring paths reduce per-packet overhead, but the ASIX USB NIC likely cannot support AF_XDP zero-copy and may bottleneck before software.

**Test plan:** Privilege and feature gate first, use an external traffic source, verify mode actually selected, sweep packet size/rate/batch, and record drops, CPU, queue ownership, and copy versus zero-copy status.

### X44. Dense factorizations

**Test:** Benchmark LU with/without pivoting, Cholesky, and QR using unblocked, blocked, and BLAS-backed implementations.

**Hypothesis:** Blocking converts much of factorization into efficient Level-3 BLAS; pivoting and panel dependencies limit scaling and introduce irregular memory traffic.

**Test plan:** FP32/FP64, N=8–4,096, conditioned/ill-conditioned inputs, 1/2 threads, custom versus LAPACK/BLIS/OpenBLAS stack, with residual and factorization error checks.

### X45. FFT layout and threading

**Test:** Compare naive DFT small-N, radix-2 iterative/recursive FFT, mixed-radix library FFT, in-place/out-of-place, and batched transforms.

**Hypothesis:** SIMD/cache blocking and plan reuse dominate; bit reversal hurts locality; threading only helps sufficiently large/batched transforms.

**Test plan:** Complex FP32/FP64, lengths power-of-two and awkward composite/prime, cold/warm plans, batch, alignment, 1/2 threads, and report transform-only plus plan-inclusive time and numerical error.

### X46. Tensor contraction and layout

**Test:** Compare direct nested loops, loop-reordered/tiled contraction, transpose/pack+GEMM, and batched GEMM lowering.

**Hypothesis:** GEMM lowering wins with reuse and regular dimensions but packing dominates small/irregular tensors; layout determines attainable locality.

**Test plan:** Sweep dimensions/order, contraction axes, contiguous/strided layouts, reuse count, FP32/FP64, 1/2 threads, and include packing/materialization.

### X47. Mixed precision and quantized integer kernels

**Test:** Compare FP64, FP32, FP16-storage-with-FP32-compute, int16, and int8 dot/GEMM-like kernels with scaling/dequantization.

**Hypothesis:** Haswell lacks newer dot-product instructions, so int8 widening/shuffle overhead may erase nominal lane advantage; reduced memory traffic still helps beyond cache.

**Test plan:** Capability-aware scalar/AVX2/library implementations, cache ladder, symmetric/asymmetric quantization, saturation/overflow policies, accuracy, conversion included/excluded, and 1/2 threads.

### X48. Cross-machine and cross-architecture replication

**Test:** Re-run a compact sentinel set on a modern x86 system and an ARM/NEON system if accessible.

**Hypothesis:** Haswell-specific cache, gather, atomic, syscall, storage, and NIC conclusions do not universally transfer; invariant workload crossovers are more valuable than absolute rankings.

**Test plan:** Freeze source/data/schema; select V cache calibration, D03, L01, SY02/SY09/SY13, CQ01, IO13/IO27 where hardware permits, and B01/B09/B17. Record complete machine/software provenance and compare normalized as well as absolute results.

## 23. Clean-Code Tax and CPU-pipeline extensions

These experiments extend the existing 84-run Clean-Code Tax suite. They are intended to answer *why* a row costs cycles, not just whether one version is faster. For each test, record wall time, cycles, instructions, IPC, retired branches/misses, executable and hot-function bytes, and a separately collected top-down classification. Preserve exact semantics and verify generated assembly.

### CC01. Top-down attribution of published Clean-Code contrasts

**Test:** Re-run representative stable and noisy blog contrasts under `TopdownL1`, `TopdownL2`, and targeted front-end/back-end metric passes.

**Hypothesis:** Forced calls and polymorphic dispatch will divide between front-end/bad-speculation cost, whereas allocation, DTO, JSON, and object-layout rows will be primarily back-end Memory Bound plus allocator/system cost.

**Test plan:** Select at least `small_noinline_helper`, `virtual_vs_soa`, `std_function_vs_lambda`, `dto_layered_vs_direct`, `json_dom_vs_stream`, `alloc_heap_vs_pool`, `atomic_global_vs_local`, and one near-tie. Run 15 interleaved repetitions at 4K–64M work sizes, use separate non-multiplexed metric passes, and correlate cycles per useful item with TMA fractions rather than comparing event counts from unequal work.

### CC02. Inline-versus-call body-size crossover

**Test:** Compare an expression, ordinary inline candidate, `always_inline`, ordinary out-of-line call, and `noinline` call while sweeping callee body size and call-site count.

**Hypothesis:** Inlining wins for tiny hot callees until duplicated code evicts useful instructions from the DSB/L1I; forced inlining then loses even though it eliminates call/return instructions.

**Test plan:** Generate bodies of 1, 4, 16, 64, 256, and 1,024 useful uops at 1–1,024 distinct call sites. Test hot sequential and randomized call-site order, keep data L1-resident, record code bytes, DSB/MITE/ICache/fetch metrics, and publish the body-size × call-site crossover surface.

### CC03. Hot/cold error-path outlining

**Test:** Compare an inlined function containing validation/error construction with a hot core plus `noinline` cold error helper, compiler `cold` annotation, and PGO layout.

**Hypothesis:** Outlining rare paths reduces hot code footprint and fetch bandwidth without affecting common-case semantics; it loses when the error rate becomes material.

**Test plan:** Error rates 0%, 0.01%, 0.1%, 1%, 10%, and 50%; small/large error formatting; cache-hot/cold code. Record common and error latency distributions, text-section/hot-block size, ICache/iTLB/DSB metrics, and branch-miss cost.

### CC04. Indirect-call target entropy

**Test:** Compare direct call, function pointer, virtual call, `std::variant`/switch, and `std::function` with 1, 2, 4, 8, 16, 64, 256, and 1,024 possible targets.

**Hypothesis:** A monomorphic indirect call is often well predicted; random polymorphic targets make branch prediction/BTB pressure dominate, while grouped-by-type execution recovers prediction and data locality.

**Test plan:** Use identical tiny and medium bodies, uniform/Zipf/grouped/Markov/phase-changing target sequences, separate object pointers from target IDs, and test AoS versus type-bucketed SoA. Report target entropy, cycles/call, branch MPKI, front-end resteers, and code footprint.

### CC05. Dispatch cost separated from object layout

**Test:** Factor virtual-versus-SoA into independent dispatch and layout axes: contiguous objects with direct/virtual dispatch, pointer-shuffled objects with both, and type-bucketed storage with switch/direct calls.

**Hypothesis:** The published virtual-versus-SoA gap combines indirect control flow and data locality; pointer chasing will explain much of large-N cost, while dispatch dominates only with tiny bodies/hot data.

**Test plan:** Sweep object size, target count, body work, shuffled fraction, working-set tier, and hit/type distribution. Collect branch, cache, TLB, and top-down metrics and use a 2×2 factorial analysis to report dispatch, layout, and interaction effects.

### CC06. Devirtualization and whole-program visibility

**Test:** Compile the same hierarchy with open classes, `final` class/method, known concrete type, separate translation units, LTO, and PGO.

**Hypothesis:** Compiler visibility can turn apparently virtual clean code into a direct/inlined path; cross-TU and plugin boundaries prevent that unless LTO or guarded devirtualization applies.

**Test plan:** GCC/Clang, no-LTO/thin-or-full-LTO where supported, monomorphic/polymorphic runtime data, and one/two/many derived types. Inspect call sites and optimization reports, record build/link time and binary size, then benchmark identical work.

### CC07. `std::function` storage and construction tax

**Test:** Compare direct lambda, templated callable, function pointer, custom function-ref, and `std::function` while separating invocation from construction/copy/destruction.

**Hypothesis:** Small-buffer-resident callables mainly pay type-erased indirect invocation; captures beyond the small-object threshold add allocation and ownership cost with sharp size crossovers.

**Test plan:** Capture sizes 0–256 bytes and alignments 1–64, trivial/nontrivial copy/destructor, construct once versus per item, monomorphic versus many targets. Count allocations/bytes and report invoke-only, lifecycle-only, and end-to-end cost.

### CC08. Tagged union, `variant`, and visitor shape

**Test:** Compare manual tag+union switch, `std::variant` with `visit`, virtual hierarchy, and type-bucketed passes for equal variants and equal data layout.

**Hypothesis:** `variant` can be free or win when the visitor becomes a compact switch, but many alternatives/large visitors inflate code and stress prediction/front-end capacity.

**Test plan:** Alternatives 2–64, skew/grouping, payload 8–256 bytes, one generic versus overloaded visitors, single and chained visits. Check assembly, text size, branch/ICache metrics, and include reordering/bucketing cost.

### CC09. Cross-translation-unit, PLT, and shared-library calls

**Test:** Compare same-TU direct calls, separate object calls, static library, hidden/default symbol visibility in a shared library, PLT/no-PLT, and LTO where legal.

**Hypothesis:** ABI and dynamic-link boundaries prevent inlining and add indirection; the fixed per-call tax matters only for small callees but can also change code placement and ICache behavior.

**Test plan:** Callee work 1–1,024 uops, 1–1,024 call sites, eager/lazy binding outside timed steady state, PIE/non-PIE, GCC/Clang. Verify relocation/disassembly and report cycles/call, branch misses, hot code pages, and startup separately.

### CC10. L1 instruction-cache footprint ladder

**Test:** Execute semantically equivalent hot loops whose reachable code footprint crosses the L1I and higher-cache boundaries.

**Hypothesis:** Throughput shows a knee when active code stops fitting the 32-KiB L1I; random interleaving of functions hurts earlier than sequential phases, while hot/cold layout delays the knee.

**Test plan:** Generate 1–512 KiB active text in fine steps around 16–64 KiB, fixed uops/function, sequential/random/Zipf calls, 4-KiB and large alignment variants. Measure `IcMiss`, FetchLat, L1I misses, cycles, and actual mapped/hot code bytes.

### CC11. Decoded-uop cache (DSB) and MITE-decode crossover

**Test:** Sweep loop uop footprint and control-flow layout while holding data and retired useful work constant.

**Hypothesis:** Tight loops served by the DSB outperform decoder-fed loops; footprint, taken branches, or alignment that cause DSB misses/switches expose a front-end bandwidth limit before L1I misses rise.

**Test plan:** 16–4,096 uop loops, straight-line/unrolled/multi-block layouts, macro-fusion-friendly and hostile comparisons, aligned/misaligned entries. Collect `DSB`, `DSBmiss`, `FetchBW`, MITE/DSB delivered-uop events, and LSD metrics supported by the DUT.

### CC12. Instruction-TLB capacity and code-page layout

**Test:** Spread the same hot code across increasing numbers of 4-KiB executable pages versus compact placement.

**Hypothesis:** Sparse randomized calls across code pages create iTLB/front-end-latency stalls even when each function is tiny and data remains hot.

**Test plan:** 1–4,096 executable pages, sequential/random/Zipf target order, page-aligned functions, warmed and explicitly cold runs. Record iTLB load misses/walks, FetchLat, ICache misses, and code RSS; do not use self-modifying code in the timed path.

### CC13. Code alignment and macro-fusion

**Test:** Shift hot loop/function entry and key compare/branch pairs across 16-, 32-, and 64-byte boundaries, and compare fusion-friendly versus unfriendly instruction forms.

**Hypothesis:** Boundary placement and lost macro-fusion can alter decode/uop demand enough to explain small benchmark deltas that source code does not reveal.

**Test plan:** Mechanically generate offsets, confirm identical semantic instruction count apart from padding/form, randomize run order, and inspect disassembly. Report cycles, uops, DSB/MITE delivery, and alignment sensitivity distribution rather than selecting the best offset.

### CC14. Unrolling and specialization code-size budget

**Test:** Compare rolled, compiler-unrolled, manually unrolled 2–32×, and template-specialized loops across one versus many instantiated kernels.

**Hypothesis:** Moderate unrolling exposes ILP and reduces branches; aggressive unrolling or many specializations become front-end/ICache bound and increase build/binary cost.

**Test plan:** Arithmetic, scan, and branchy kernels; L1/LLC/DRAM data; 1–256 specializations invoked sequentially/randomly. Record throughput, text size, compile time, Frontend/Backend split, and use the same tail semantics.

### CC15. Conditional-branch entropy and phase recovery

**Test:** Compare branch, conditional move, mask arithmetic, scalar branchless, and AVX2 mask paths under controlled outcome sequences.

**Hypothesis:** Branches win highly predictable data; branchless/SIMD win near-random outcomes; phase changes impose a transient recovery tax hidden by steady averaged distributions.

**Test plan:** Outcome rates 0–100%, periodic, random, Markov, sorted, alternating, and abrupt phase changes. Sweep body cost and vector compress/store requirements, plot cycles and branch misses over recovery windows, and include work done on rejected items.

### CC16. Call/return predictor and recursion depth

**Test:** Compare iterative loops, normal recursion, mutual recursion, manually managed stacks, and intentionally mismatched call/return depth.

**Hypothesis:** Ordinary shallow calls predict well; deep or irregular return chains overflow/mislead return prediction and add bad speculation, while manual stacks trade that for data-memory traffic.

**Test plan:** Depth 1–4,096, fixed/variable depth, tail-call-eligible/disabled, tiny/medium bodies, warmed/cold sequences. Record cycles/node, branch misses, stack faults, and code/data cache effects; verify tail-call transformations.

### CC17. Dependency latency versus instruction-level parallelism

**Test:** Perform identical arithmetic as one dependent chain versus 2, 4, 8, and 16 independent accumulators.

**Hypothesis:** A dependency chain measures operation latency, whereas independent chains approach execution throughput; source-level abstraction may accidentally serialize otherwise parallel work.

**Test plan:** Integer add/multiply, FP add/FMA, loads at each cache tier, and representative wrapper/accessor patterns. Keep instruction mix equal, inspect registers/spills, and report cycles/operation, IPC, Core Bound, and port utilization.

### CC18. Execution-port pressure and instruction substitution

**Test:** Compare semantically equivalent instruction mixes that concentrate on one execution port versus distribute work across ports.

**Hypothesis:** Compute kernels with hot data can be Back-End Core Bound even at high IPC; replacing or interleaving operations can improve port balance without changing Big-O or memory traffic.

**Test plan:** Use integer arithmetic, address-generation-heavy loads, shuffle-heavy SIMD, load+store, and mixed variants. Collect `PortsUtil`/`lpm_ports`, uops, cycles, and disassembly; separately measure latency chains and independent throughput.

### CC19. Division, microcode, and numerical assists

**Test:** Compare integer/FP division, reciprocal-multiply transformations where exact semantics allow, constant versus variable divisors, and normal versus subnormal FP data.

**Hypothesis:** Divider occupancy or microcode/assist paths can dominate a compute-hot loop while still appearing as Retiring/Core Bound; constant strength reduction and FTZ/DAZ can change the result but also semantics.

**Test plan:** Dependent/independent operations, divisor/value distributions including zero/error handling and denormals, scalar/AVX2, strict/fast-math separately. Record divider, MicroSeq/assist, Core Bound, accuracy, and exceptional-case behavior.

### CC20. Allocation-cost decomposition

**Test:** Decompose `new`/`malloc` latency into warmed allocator fast path, free-list refill, arena contention, `brk`/`mmap`, first-touch fault/zeroing, constructor, destructor, and reclamation.

**Hypothesis:** “Allocation cost” is not one constant: cache-hot small allocations can stay in user space, while refill, page provisioning, zeroing, remote free, and destruction produce distinct knees and latency tails.

**Test plan:** Reuse X24–X26 sizes/lifetimes; add syscall/fault/allocation counters or interposition outside timed code, touched versus untouched allocations, trivial/nontrivial objects, immediate/batched/deferred free. Publish `ns/allocation`, `ns/lifetime`, faults, syscalls, RSS/fragmentation, and p99 separately.

### CC21. RAII lifetime placement and destructor batching

**Test:** Compare per-iteration RAII object construction/destruction, scope-hoisted reuse, move-only ownership, explicit reset, and bulk destruction after the hot loop.

**Hypothesis:** RAII itself is not the tax; expensive resource/string/refcount lifecycle placed at hot-loop frequency is. Hoisting or batching helps when it preserves peak-memory and exception-safety contracts.

**Test plan:** Trivial, string/vector, unique/shared ownership, file-like mock resource, and throwing/nonthrowing cleanup; sizes and reserve policies swept. Record construction, useful work, destruction, peak retained memory, allocations, atomics, and exception semantics.

### CC22. Abstraction stacking and nonlinear interaction

**Test:** Compose helper calls, validation, tracing, DTO copies, type erasure, allocation, and serialization one layer at a time around the same tiny operation.

**Hypothesis:** Individual near-zero costs can become material at scale, and interactions are not necessarily additive because one layer changes inlining, code footprint, allocation, or cache state for another.

**Test plan:** Use a factorial or fractional-factorial design rather than only “clean versus raw”; 0–7 layers, request work 10 ns–100 us, enabled/disabled tracing, hot/cold payload. Report absolute budget consumed, marginal effects, interactions, and end-to-end Amdahl impact.

### CC23. Optimization-level, LTO, and PGO sensitivity

**Test:** Build representative Clean-Code, DSA, and dispatch kernels with `-O2`, `-O3`, `-Os`, no/full LTO, and train/test-separated PGO.

**Hypothesis:** `-O3` may win a single loop but lose an instruction-footprint-heavy suite; LTO erases some abstraction boundaries; valid PGO improves layout/prediction, while mismatched profiles can regress phase-changing workloads.

**Test plan:** Freeze source and ABI, use independent representative and shifted training distributions, record optimization reports, build time, binary/hot text size, cold start, steady state, and TMA. Never train and report on the identical random input only.

### CC24. Cold-start code path versus warm steady state

**Test:** Separate process startup, dynamic relocation, first instruction/data page faults, first allocation/TLS initialization, cold ICache, and warmed repeated invocation.

**Hypothesis:** Large abstraction-heavy binaries may have acceptable steady-state throughput but worse first-request latency due to code/data page working set and initialization.

**Test plan:** Fork/exec-per-operation, long-lived process first call, warmed call, and periodically idle/evicted variants; static/shared builds where comparable. Record startup-to-ready, first/p50/p99 invocation, major/minor faults, RSS/PSS, code pages touched, and loader time.

## 24. Database, B+ tree, LSM, SSTable, and write-path experiments

Terminology rule: MySQL versus MongoDB is an end-to-end system/data-model comparison, not “B-tree versus SSTable.” InnoDB stores row data in a clustered index and MongoDB's default WiredTiger engine represents row/column stores with B-trees. A true B+ tree versus LSM/SSTable experiment uses a controlled structure harness and/or comparable embedded engines such as a B-tree engine versus LevelDB/RocksDB.

### DB01. In-memory B+ tree versus LSM write-buffer isolation

**Test:** Compare a cache-aware B+ tree with an LSM memtable using the same key/value representation before persistence, WAL, and compaction.

**Hypothesis:** The memtable wins random write bursts through append/skiplist-like buffering, while the B+ tree offers immediately ordered reads with less read-path indirection; at small cache-resident sizes constants dominate.

**Test plan:** Integer and string keys, values 0–1,024 bytes, sequential/uniform/Zipf writes, point/range reads, 100K–100M records as capacity allows. Equalize allocator and memory budget, report operation latency, bytes touched, cache misses, and structural memory per live record.

### DB02. Durable B+ tree versus LSM/SSTable end-to-end

**Test:** Compare a pinned page-oriented B+ tree engine with LevelDB/RocksDB under identical WAL, fsync, cache, compression, and API contracts.

**Hypothesis:** LSM batching improves sustained random-write throughput but pays compaction, read, and space amplification; B+ trees provide steadier point/range latency but more random page updates.

**Test plan:** Use the same device and client, single writer first, durable and relaxed modes separately, identical key/value bytes, cache budget, and dataset. Include ingest, steady mixed load, drain-to-quiescence, DB size, device bytes, p99.9, and CPU per useful operation.

### DB03. Sequential, random, monotonic, and hot-key inserts

**Test:** Insert the same records with monotonic, random, reverse, Zipfian-hot, and adversarial prefix key order.

**Hypothesis:** B+ trees benefit from append-like locality but can create a hot right edge; LSM write ingestion is less order-sensitive initially, while key order changes compression and later compaction overlap.

**Test plan:** Fixed seed/key set, 1–64 client threads where hardware allows but report 1/2 primary, batch 1–10K, values 16–1,024 bytes. Record throughput, page splits, stalls, flush/compaction bytes, WAL bytes, and tails over time.

### DB04. Single-write versus batch/group insertion

**Test:** Sweep API batch and transaction size for B+ tree and LSM engines.

**Hypothesis:** Batching amortizes parsing, locking, WAL headers, syscalls, and fsync; excessive batches increase latency, memory, contention, and recovery work.

**Test plan:** Batch 1–65,536 and time-based group commit, one/two writers, durable/relaxed modes. Report per-record throughput plus per-batch p50/p99, fsync count/time, WAL bytes, and maximum unacknowledged data implied by each contract.

### DB05. Point-read hit, miss, and negative-lookup path

**Test:** Measure successful and unsuccessful point lookup in B+ tree and LSM variants at controlled cache states.

**Hypothesis:** LSM read amplification grows with overlapping files/levels and misses; Bloom filters cut negative reads at memory/CPU cost; B+ tree path depth is steadier but page misses are expensive.

**Test plan:** 0/50/100% hit, hot/warm/cold, uniform/Zipf queries, cache-resident/larger-than-cache datasets, Bloom bits/key 0–20, aged and post-compaction states. Record block reads, filter positives/false positives, levels/pages touched, and latency tails.

### DB06. Ordered range-scan length and selectivity

**Test:** Scan lower-bound plus 1–1M consecutive records across B+ tree and LSM engines.

**Hypothesis:** B+ tree leaf linkage gives predictable scans; LSM merging of multiple sorted runs adds CPU/read amplification until compaction, but large sequential reads can amortize it.

**Test plan:** Vary range length, start locality, projection width, tombstone density, level overlap, cache state, and result materialization. Report seek/startup separately from per-row scan, blocks/bytes read, decompression, and p99.

### DB07. Overwrite and partial-update behavior

**Test:** Repeatedly overwrite a fixed hot set and update fixed-size versus growing/shrinking values/documents.

**Hypothesis:** LSM turns overwrites into new versions then compaction; B+ tree may update in place or split/relocate; document growth and secondary indexes amplify MongoDB-level cost beyond engine structure.

**Test plan:** Hot set 0.1–100%, value growth/shrink patterns, indexed/nonindexed fields, uniform/Zipf keys, steady duration long enough for maintenance. Track version/tombstone bytes, dirty/evicted pages, compaction, WAL, CPU, and tails.

### DB08. Delete, tombstone, and reclamation lifecycle

**Test:** Delete 1–90% of records, then measure reads, iteration, space, and reclamation before/during/after maintenance.

**Hypothesis:** LSM deletes are initially cheap tombstones but degrade reads/space until compaction; B+ tree removal/rebalancing and free-page reuse have different immediate and delayed costs.

**Test plan:** Random/range/hot deletes, interleaved inserts, point miss/range scan after each phase, explicit/default maintenance separately. Report logical versus physical bytes, tombstones, reclaimed time, write amplification, and latency timeline.

### DB09. Write-amplification accounting

**Test:** Measure host/device and engine bytes written per acknowledged logical byte for insert, overwrite, delete, and mixed workloads.

**Hypothesis:** WAL plus page rewriting or compaction causes workload-dependent amplification; an ingestion-throughput winner may lose on endurance and background cost.

**Test plan:** Start from reproducible preconditioned states, record application/WAL/SST/data-file/device bytes, compaction/flush/checkpoint phases, and drain fully after load. Report amplification during load and including quiescence; never infer it only from final DB size.

### DB10. Read-amplification accounting

**Test:** Count blocks/pages, compressed and physical bytes, filters, and CPU touched per logical point/range result.

**Hypothesis:** LSM tuning trades Bloom/index/cache memory and compaction work for fewer read probes; B+ tree fanout/page size trades depth for bytes read.

**Test plan:** Hits/misses, range sizes, cache states, aged states, fanout/page/block size, Bloom/filter/index policies. Report logical result bytes, engine block reads, OS/device reads, decompression CPU, and amplification distribution.

### DB11. Space-amplification and temporary-space peaks

**Test:** Track allocated disk bytes relative to live logical data across ingest, updates, snapshots, deletes, and compaction/checkpoint.

**Hypothesis:** LSM obsolete versions and compaction temporarily multiply space; B+ tree fragmentation and snapshot history retain pages. Peak space, not final size, determines safe capacity.

**Test plan:** Fixed live dataset, update churn 1–20×, snapshots/readers held for controlled times, compression on/off. Sample disk usage and engine metrics over time, report peak/steady amplification and time to reclaim.

### DB12. Foreground latency during compaction/checkpoint/flush

**Test:** Measure a constant foreground read/write rate while triggering natural flush, compaction, B+ tree checkpoint, and cache eviction.

**Hypothesis:** Background maintenance creates p99/p99.9 spikes and throughput stalls invisible in run-level averages; rate limiting trades completion time for foreground stability.

**Test plan:** Open-loop offered-load sweep with coordinated-omission-safe histograms, default/tuned background threads/rate limits, single/two physical cores, I/O/CPU telemetry. Mark every maintenance interval on the latency time series and include post-load drain.

### DB13. Cache-budget and double-cache behavior

**Test:** Sweep engine cache plus filesystem page-cache budgets for InnoDB, WiredTiger, B+ tree, and LSM engines.

**Hypothesis:** Too-small engine caches increase reads/eviction, while double caching compressed on-disk and uncompressed engine representations can consume memory without proportional hits.

**Test plan:** Working set 0.25–8× total cache, fixed total RAM cap, engine cache 10–80%, warm/cold/restart states, swap prohibited. Record engine/OS cache, faults, device I/O, hit ratios, RSS/PSS, and point/range/write tails.

### DB14. WAL and durability contract

**Test:** Compare no-sync/periodic-sync/per-transaction sync/group commit with equivalent acknowledgment guarantees.

**Hypothesis:** Fsync latency dominates small durable writes; batching increases throughput while acknowledgment interval controls potential loss. Unequal durability can reverse rankings and must never share one leaderboard.

**Test plan:** One and multiple clients, payload/batch sweep, WAL-only and WAL+data placement, explicit flush semantics, power-safe device assumptions recorded. Count `fsync`/`fdatasync`, flush latency, acknowledged transactions, and recovery-visible commits after controlled process kill.

### DB15. Key/value width, representation, and compression

**Test:** Sweep 8-byte, UUID, short/long string, common-prefix, and random keys with fixed/compressible/incompressible values.

**Hypothesis:** Comparison cost, fanout, prefix/block compression, encoding, and decompression shift both CPU and I/O rankings; one fixed tiny key is not representative.

**Test plan:** Values 0–64 KiB, compression none/Snappy/Zstd where supported at matched levels, same logical records, point/range/write mixes. Report logical/physical bytes, CPU cycles, cache behavior, compression ratio, and latency.

### DB16. Secondary-index write and read tax

**Test:** Add 0, 1, 2, 4, 8, and 16 secondary indexes and update indexed versus nonindexed fields.

**Hypothesis:** Every index adds write, WAL, cache, and space cost; covering indexes can repay it on reads. Wide/random secondary keys magnify the penalty.

**Test plan:** Equivalent indexed fields and uniqueness, fixed document/row payload, insert/update/delete plus covered/noncovered queries. Record index bytes, entries modified, write amplification, throughput/tails, and query plan/results.

### DB17. Concurrent writers, readers, and hot-key contention

**Test:** Scale read/write clients over disjoint, uniform shared, and Zipfian hot keys.

**Hypothesis:** Storage is not always the bottleneck: latch/lock/MVCC/transaction conflicts and cache-line ownership cap scaling, especially on the DUT's two isolated physical cores.

**Test plan:** 1, 2 physical, SMT pair, and 4 logical clients; 100/0 through 50/50 mixes; short/long transactions; open/closed-loop drivers. Report completed/aborted/retried operations, CPU, context switches, lock metrics, fairness, and p99.9.

### DB18. Transaction size and isolation level

**Test:** Compare single-row and multirow transactions under supported isolation/snapshot modes with equivalent invariants.

**Hypothesis:** Larger transactions amortize commit but retain locks/versions and increase conflict/recovery work; stronger isolation changes both semantics and performance.

**Test plan:** 1–10K records/transaction, read-only/read-modify-write, overlapping/disjoint keys, short/stalled reader, supported isolation modes clearly separated. Validate a transactional invariant and report commits, aborts, version/history bytes, and tails.

### DB19. Bulk load and index-build strategy

**Test:** Compare row-at-a-time insert, sorted bulk load, unordered bulk load, build-index-after-load, and engine-native ingest/SST import where semantics match.

**Hypothesis:** Sorting and bulk construction reduce page splits/compaction and WAL overhead, but preprocessing and temporary storage matter at one-shot scale.

**Test plan:** Dataset 1–100% of safe capacity, one/two workers, sorted/random source, indexes 0–8, durability modes separate. Include source generation, sorting, load, index build, drain, final verification, peak space, and total bytes written.

### DB20. Crash recovery and restart-to-service

**Test:** Kill the database process at controlled points during WAL append, flush/checkpoint, and compaction, then verify committed data and measure restart.

**Hypothesis:** More frequent checkpoints/fsync reduce recovery replay but increase steady write cost; LSM manifest/compaction and B+ tree checkpoint paths expose different recovery work.

**Test plan:** Disposable data directories only; SIGKILL/process failure first, 30 randomized cut points, acknowledged/unacknowledged operation ledger, integrity scan after restart. Report lost acknowledged writes (must be zero under the claimed contract), recovery time, bytes replayed, and time to full throughput.

### DB21. MySQL versus MongoDB controlled write/read comparison

**Test:** Compare InnoDB and MongoDB/WiredTiger on an intentionally common key/value CRUD contract before testing model-specific advantages.

**Hypothesis:** With equal durability, indexes, cache budget, client transport, and result semantics, much of the headline difference will come from SQL/BSON/protocol/server layers rather than “B-tree versus SSTable”; workload shape will choose the winner.

**Test plan:** One table/collection, identical primary and secondary logical keys, fixed payload bytes, prepared/reused client operations, Unix-socket and loopback variants, durable and relaxed acknowledgments in separate panels. Sweep insert/upsert/read/range/delete, batch, 1/2 clients, cache state, and record server plus client CPU, WAL/journal, data bytes, plans, p50–p99.9, and recovery semantics.

### DB22. Mixed workload and maintenance-state matrix

**Test:** Run standardized read-heavy, update-heavy, scan-heavy, read-latest, and read-modify-write mixes from fresh load through aged steady state.

**Hypothesis:** Single-operation microbenchmarks do not predict mixed steady state because cache pollution, compaction/checkpoints, index maintenance, and contention interact; rankings can change after aging.

**Test plan:** Use pinned YCSB-like distributions but implement and validate identical semantics, run load/warm/steady/drain phases, at least five seeds, controlled offered load, and 30–60 minute steady windows where safe. Publish time series, throughput-versus-tail-latency curves, amplification, CPU/I/O, and both fresh and aged results.

## 25. Fifty DSA questions for scalar, cache, SIMD, and parallel comparison

This is an exact 50-question portfolio. “Normal” means the straightforward scalar/reference algorithm, not necessarily `O(N)`—several questions are inherently `O(log N)`, `O(N log N)`, `O(N²)`, dynamic programs, or graph algorithms. Never let SIMD/cache/threads silently change exactness, ordering, stability, overflow, or output-materialization semantics.

Levers are rated `H` (strong candidate), `M` (workload-dependent), `L` (usually secondary), or `—` (not a sensible primary optimization) for cache/layout (`C`), SIMD (`S`), and multithread/multiprocess (`P`). `BLOG` means an exact or close blog baseline exists; `EXT` means this roadmap already contains a narrower component; `NEW` identifies a newly explicit question. Each row remains a benchmark specification with a test, hypothesis, and test plan.

| ID | DSA question and explicit test | Coverage | C/S/P | Hypothesis | Test plan |
|---|---|---|---|---|---|
| P01 | **Linear membership:** scalar early-exit `O(N)` versus blocked/prefetched scalar, AVX2 compare-mask, and chunk-parallel scan. | EXT | H/H/M | SIMD wins medium/large contiguous scans; early scalar wins tiny or early-hit queries; two cores help only after launch cost and memory bandwidth permit. | Sweep cache ladder, element width, hit rate/position, single/batched queries, sorted/random data, 1/2 cores and SMT; report query latency, bytes examined, bandwidth, and stop-position distribution. |
| P02 | **Count a predicate:** scalar branch, scalar branchless, AVX2 mask+popcount, and per-thread reduction, all `O(N)`. | NEW | H/H/H | SIMD removes branch-entropy cost; threads scale until DRAM bandwidth, but low selectivity with expensive predicates can be compute-bound. | Sweep selectivity/pattern, predicate cost, types, cache tier, aligned/tails, 1/2 cores; validate count and collect branch, vector, bandwidth, and reduction overhead. |
| P03 | **Min/max/argmin/argmax:** scalar dependent reduction versus multiple accumulators, AVX2 lanes, tiled and parallel reduction, `O(N)`. | NEW | H/H/H | Independent/vector accumulators break dependency chains; index/tie semantics and final horizontal reduction reduce speedup; threads help beyond a size crossover. | Integer/FP, NaN and first/last-tie policies, random/sorted/adversarial extrema, cache ladder, 1/2 cores; report element/s, cycles/element, exact index, and reduction setup cost. |
| P04 | **Sum, dot product, norm, and cosine:** scalar reference versus unrolled, AVX2/FMA, blocked, and threaded reductions, `O(N)`. | EXT | H/H/H | SIMD is large while cache-resident; DRAM bandwidth limits large vectors; threading helps compute-rich cosine more than a single sum and changes FP rounding. | INT/FP32/FP64, overflow/accuracy policies, strides, cache ladder, 1/2 cores, compensated and fast variants; include normalization passes and error bounds. |
| P05 | **Prefix sum/scan:** scalar `O(N)` versus SIMD intra-vector scan, blocked two-pass scan, and parallel scan. | EXT | H/M/H | SIMD helps but lane dependencies limit it; two-pass parallel scan wins only when block totals/fixup amortize and extra traffic does not dominate. | Inclusive/exclusive, integer/FP, N around cache tiers, block sizes, 1/2 cores/processes, in/out-of-place; record traffic, setup, and exact/rounding correctness. |
| P06 | **Segmented scan:** scalar boundary-aware scan versus bitmask/SIMD block scan and thread-partitioned scan with boundary repair. | NEW | H/M/H | Long regular segments approach plain-scan speed; many tiny/irregular segments make boundary handling and repair dominate. | Segment length distributions 1–1M, periodic/random/Zipf boundaries, values/types, cache ladder, 1/2 cores; validate segment starts and report repair/metadata cost. |
| P07 | **Filter and stable compaction:** branchy scalar push, branchless scalar scatter, AVX2 mask-table/compress emulation, two-pass prefix+scatter, and parallel chunks. | EXT | H/H/H | SIMD helps medium selectivity, but AVX2 lacks native compress-store; stable output and allocation can dominate. Parallel two-pass wins large N. | Selectivity 0–100%, clustered/random masks, payload 4–256 bytes, stable/unstable, preallocated/growing output, cache ladder and 1/2 cores; include bytes written and materialization. |
| P08 | **Partition into two or K buckets:** scalar branch/swap, histogram+prefix+scatter, buffered cache-aware scatter, SIMD classify, and per-thread local buckets. | EXT | H/M/H | Two-pass buffered methods beat atomic/random scatter for large K; small K may favor simple in-place code; skew drives contention and cache residency. | K=2–65,536, stable/unstable, uniform/Zipf keys, payload sizes, 1/2 workers, buffer sizes; report traffic, temporary memory, imbalance, and output validation. |
| P09 | **Histogram/frequency count:** direct shared bins versus scalar private bins, AVX2-assisted classification, cache-blocked privatization, and parallel merge. | EXT | H/M/H | Small private histograms scale; shared hot bins serialize; SIMD gather/conflict limitations make lane privatization workload-specific on AVX2. | Bins 2–1M, uniform/Zipf/adversarial keys, counter widths/overflow, cache ladder, 1/2 cores/SMT; record contention, merge cost, misses, and exact totals. |
| P10 | **Deduplicate/run-length encode:** scalar adjacent scan, hash-based general dedup, sort+unique, SIMD adjacent compares, and parallel block RLE with boundary merge. | EXT | H/H/H | SIMD excels already-sorted RLE; hashing wins unsorted streaming; sort+unique becomes competitive when sorted output/reuse is required. | Sorted/nearly/random, run/cardinality distribution, item width, stable-first versus sorted output, one/many uses, 1/2 workers; include sort/build/output and boundary fixup. |
| P11 | **Batched lower/upper bound:** independent scalar binary searches versus sorted-query scheduling, Eytzinger/blocked layout, SIMD B-tree-node search, and parallel query batches. | EXT | H/M/H | Batch locality and node SIMD hide branch/cache latency; one scalar binary search remains best for small hot arrays. | Array/query sizes, hit/miss/duplicate semantics, random/sorted/clustered queries, batch 1–4K, cache ladder, 1/2 cores; include layout build amortization. |
| P12 | **Merge two or K sorted sequences:** scalar merge, branchless/galloping merge, SIMD merge networks, cache-blocked K-way heap/loser tree, and parallel partitions. | EXT | H/M/H | Galloping wins skewed runs, SIMD wins similarly sized dense runs, and parallel merge needs accurate splitters plus large output. | Length ratio, overlap/duplicates, item/payload width, K=2–1K, stable semantics, cache tier, 1/2 cores; report comparisons, traffic, partition and output time. |
| P13 | **Sorted-set intersection/difference/union:** scalar merge, galloping, SIMD compare blocks, bitmap, hash, and partition-parallel methods. | EXT | H/H/H | Density and size ratio decide: SIMD merge helps balanced sorted lists, galloping helps skew, bitmaps win dense bounded universes, hashes win unsorted reuse. | Size ratio 1–10K, overlap 0–100%, clustered/uniform values, universe, output count/materialization, cache tier and 1/2 cores; include conversion/build amortization. |
| P14 | **Two Sum:** nested scalar `O(N²)`, sort+two-pointer `O(N log N)`, hash `O(N)`, dense-bitset/direct table, and batched/parallel variants. | EXT | H/M/M | Hash/direct lookup wins large N, but sorted/cache-contiguous and SIMD-blocked nested scans can win tiny N; output-first versus all-pairs changes the result. | N/cache ladder, value range/cardinality, hit rate/multiplicity, one/all-pair semantics, one/reused targets, 1/2 workers; include sort/hash build and overflow. |
| P15 | **Three Sum:** sorted two-pointer `O(N²)` versus hash-assisted, cache-tiled pair sums, SIMD inner comparisons, and parallel outer-index ranges. | NEW | H/M/H | Parallel outer loops and contiguous two-pointer scans help; materializing `O(N²)` pair sums becomes memory-bound and only wins with reuse/bounded N. | N up to safe quadratic work, duplicates/range/skew, existence/count/unique-triplet contracts, 1/2 cores; measure work, traffic, load balance, and output canonicalization. |
| P16 | **Kth element/selection:** full sort, heap, scalar quickselect, branchless/block partition, SIMD partition, and parallel sample/select. | EXT | H/M/M | Quickselect wins one query but branch/cache behavior is variable; block/SIMD partition stabilizes writes; sort wins many ranks or ordered reuse. | N, k at tails/middle, distributions including duplicates/sorted/adversarial, one/many k, deterministic/random pivot, 1/2 cores; report tail latency and moved bytes. |
| P17 | **Top-K values:** full sort, size-K heap, quickselect+sort-K, SIMD block selection, cache-aware tournament, and parallel local-top-K merge. | EXT | H/M/H | Heap wins tiny K, selection wins moderate K, sort wins large K/reuse; per-thread local top-K scales while shared heap contends. | N, K=1–N, duplicates/tie order, payload/index return, distributions, cache ladder, 1/2 workers; include final ordering and merge. |
| P18 | **Top-K frequent:** dense count, hash count+heap, sort+runs, sketch+verify, radix partition, and per-thread local aggregation. | EXT | H/M/H | Dense domains and cache-resident local tables dominate; skew hurts shared counters but helps hot cache; approximate candidates need exact verification. | N/cardinality/domain/skew, K, exact/approx contract, memory budget, one/two workers; report count/build/select/verify separately and error if approximate. |
| P19 | **Counting/radix sort:** scalar counting and LSD/MSD radix versus cache-blocked histograms, buffered scatter, SIMD key extraction, and parallel passes. | EXT | H/M/H | Radix beats comparisons for fixed-width keys once histograms/buffers fit cache; too many radix bits make bins/scatter cache-hostile. | Key width, payload, radix 4–16 bits/pass, N/cache tier, uniform/skew/prefix keys, stable semantics, 1/2 workers; count full passes, traffic, and temp space. |
| P20 | **Comparison sort:** `std::sort`, stable merge, introsort, tiled merge, SIMD small-block networks, and parallel merge/sample sort. | EXT | H/M/H | SIMD networks accelerate tiny base cases; cache-aware merge wins large stable sorts; two cores help only after partition/merge overhead. | N=8–1B safe limit, element/payload width, sorted/reverse/nearly/random/duplicates, stable/unstable, 1/2 workers; include allocation and verify permutation/order. |
| P21 | **Inversion count:** scalar merge-count `O(N log N)`, Fenwick with compression, cache-blocked merge, SIMD merge primitive, and parallel divide/recombine. | NEW | H/L/M | Merge-count locality beats pointer-heavy trees; SIMD has limited value because counting depends on merge positions; parallelism helps large balanced halves. | N, sorted/reverse/nearly/random/duplicates, key range, cache ladder, 1/2 workers; report compression/build, comparisons, traffic, and 64-bit exact count. |
| P22 | **Range sum with updates:** naive `O(N)` scan, prefix array, Fenwick tree, iterative segment tree, blocked decomposition, SIMD queries, and parallel batch processing. | NEW | H/M/H | Prefix wins immutable reads, Fenwick wins mixed point updates, blocked layout can beat asymptotically similar trees via locality; SIMD helps long ranges, not log-depth dependency. | N, range length/locality, query/update mixes, batch/reuse, cache tiers, 1/2 workers on disjoint/shared data; include preprocessing and consistency semantics. |
| P23 | **Range minimum query:** linear scan, sparse table, segment tree, Cartesian/LCA method, blocked micro/macro table, SIMD scan, and parallel batches. | NEW | H/H/H | SIMD scan wins short ranges; preprocessing structures win reused long queries; cache-blocking is decisive when tables exceed LLC. | N, range length/query count, static/update cases, element width/ties, cache state, 1/2 workers; plot query-only and amortized time plus memory. |
| P24 | **Sliding-window maximum:** deque, heap/lazy delete, block prefix/suffix, SIMD blocks, and chunk-parallel windows with halos. | BLOG/EXT | H/M/H | Deque wins streaming; block method is SIMD/parallel friendly offline; halos and small K erase thread gains. | N, K=1–N, monotonic/random/duplicate/phase data, online/offline contracts, cache ladder, 1/2 workers; preserve tie/index semantics and include setup. |
| P25 | **Sliding median/quantile:** two heaps, multiset/tree, Fenwick on compressed domain, histogram, block recomputation with SIMD, and partitioned windows. | EXT | H/M/M | Bounded domains favor cache-resident histograms/Fenwick; general exact heaps pay indirection; overlapping windows make straightforward parallel chunks attractive only with halo/setup amortization. | N/K/domain/cardinality, exact and separately labeled approximate quantiles, uniform/Zipf/phase data, 1/2 workers; report update/query tails, memory, and error. |
| P26 | **Batch hash lookup/update:** chained/node, linear/Robin-Hood, Swiss/F14 metadata, dense direct array, SIMD tag probe, prefetched batches, and sharded parallel tables. | BLOG/EXT | H/H/H | Flat SIMD-metadata tables win read-heavy cache-friendly loads; lifecycle, full keys, tombstones, resize, hot writes, and skew determine sustained performance. | Use L01–L06 distributions, load factors, hit rates, keys/payloads, fresh/aged states, batch 1–1K, 1/2 workers; report probe bytes, tails, build/resize, and memory. |
| P27 | **Bloom/blocked Bloom/Xor-like membership:** scalar hashes, SIMD blocked probes, cache-resident filters, parallel build/query, and exact-table verification. | EXT | H/M/H | Blocked filters reduce cache misses; more hashes lower false positives but cost CPU; batch SIMD helps, while parallel shared bit setting can contend. | Bits/key, hash count, N/query batch, target false-positive rates, key distributions, cache tier, 1/2 workers; measure build, query, false positives, exact follow-up, and memory. |
| P28 | **B+ tree node search:** branchy/branchless binary, linear SIMD, interpolation, cache-line blocked nodes, and batched/threaded independent queries. | EXT | H/H/H | Wide cache-line-sized nodes plus SIMD reduce depth and misses; overly wide nodes waste comparison/data bandwidth; batch scheduling exposes MLP. | Fanout/node size, key width/prefix, hit/range semantics, mutable fill factor, cache tiers, batch 1–1K, 1/2 workers; include build/update and bytes/node touched. |
| P29 | **Equi-join:** nested-loop, SIMD tiny-side scan, hash join, radix-partitioned hash, sort-merge, index nested-loop, and parallel local partitions. | EXT | H/H/H | Tiny-side SIMD scan can beat setup; hash wins unsorted equality; radix restores cache locality beyond LLC; sort-merge wins ordered/reused inputs. | Side ratio, cardinality/skew, match multiplicity/output size, payload, sortedness, cache tier, 1/2 workers; separate build/partition/probe/output and include reuse. |
| P30 | **Group-by aggregation:** dense array, flat hash, sort+runs, radix partition, tree, SIMD local bins, and per-thread local-then-merge. | EXT | H/M/H | Domain density and skew select the structure; shared hot tables collapse under contention, while local state may overflow cache. | N/cardinality/domain/skew, aggregate width/function, ordered/unordered output, memory cap, 1/2 workers; include final merge/sort, overflow, and materialization. |
| P31 | **Byte classification/tokenization/UTF-8 validation:** scalar table/branch DFA versus SWAR/AVX2 classify masks, block-state carry, and parallel chunks with boundary repair. | EXT | H/H/H | Byte SIMD gives large speedups for ASCII/common cases; variable-length state and boundaries reduce gains; threads help only for large buffers and repairable chunk state. | ASCII/UTF-8 valid/invalid, token density, adversarial boundary bytes, 64 B–1 GiB, 1/2 workers; require exact error offset/tokens and report bytes/s plus repair. |
| P32 | **Exact substring search:** naive scalar, `memchr`+verify, KMP, Boyer-Moore/Horspool, Two-Way/library, AVX2 first/last-byte filters, and parallel chunks. | NEW | H/H/H | SIMD filters win short needles/common text; skip algorithms win selective long needles; preprocessing and overlap halos decide batch/thread crossovers. | Haystack/needle size, alphabet, periodic/repetitive/random/natural-like text, hit position/count/all matches, reuse, cache state, 1/2 workers; include preprocessing. |
| P33 | **Multiple-pattern search:** repeated single-pattern search, trie, Aho-Corasick, Wu-Manber-like filtering, SIMD prefix filters, and parallel chunks. | NEW | H/M/H | Automata amortize many patterns but large transition tables become cache-bound; compact sparse transitions can beat dense tables; SIMD prefix rejection helps selective sets. | Pattern count/length/shared prefixes, alphabet, match density, automaton layouts, build reuse, cache tier, 1/2 workers with halos; report build, memory, scan, and output. |
| P34 | **Longest repeated substring:** suffix array+LCP, suffix automaton, rolling-hash+bisection+verification, and cache/threaded construction variants. | EXT | H/M/H | Suffix arrays offer compact scans; automata trade memory for one-pass construction; rolling hash is fast only when collision verification and repeated passes are counted. | Length/alphabet/repetition, exact substring/index output, construction reuse, cache tier, 1/2 workers; include sort/hash build, verification, peak memory, and worst-case strings. |
| P35 | **Trie prefix lookup/autocomplete:** pointer trie, sorted vector+range, radix/Patricia trie, double-array/LOUDS-like layout, SIMD node labels, and parallel query batches. | NEW | H/M/H | Compact contiguous/radix layouts reduce pointer/TLB misses; sorted arrays win static data; SIMD helps wide node label tests, not deep single-child paths. | Key count/length/prefix sharing, hit/miss/prefix output K, mutable/static, cache tier, batch 1–1K, 1/2 workers; include build/update, memory, and output. |
| P36 | **Suffix-array construction and lookup:** prefix doubling, induced sorting/library baseline, cache-aware radix passes, SIMD LCP, and parallel sorting/scans. | EXT | H/H/H | Construction is dominated by sorting/data movement; radix/cache layouts and parallel passes help, while SIMD strongly accelerates LCP/verification on long common prefixes. | Random/repetitive/low-alphabet/natural-like text, size/cache tier, build versus query reuse, 1/2 workers; verify suffix order and report build, LCP, query, memory, and traffic. |
| P37 | **Longest/count palindromic substring:** center expansion, DP, Manacher, rolling hash+verify, SIMD equality blocks, and parallel centers. | EXT | H/M/H | Center expansion wins normal small inputs, Manacher protects repetitive worst cases, SIMD/parallel center expansion helps only when radii are long and output contract permits. | Length/alphabet/repetition/all-same/adversarial, longest/count/all-output, cache tier, 1/2 workers; verify exact boundaries and include table/hash construction. |
| P38 | **Longest common subsequence:** scalar full-table DP, rolling rows, cache-tiled/wavefront DP, bit-parallel bounded alphabet, SIMD cells, and parallel wavefront. | EXT | H/H/M | Rolling/tiled layouts cut memory traffic; bit-parallel methods dominate suitable alphabets; two-core wavefront gains are limited by dependencies and diagonal synchronization. | M×N shapes, alphabet/repetition, length-only versus sequence reconstruction, cache tiers, tile/vector sizes, 1/2 workers; report cells/s, memory, sync, and output correctness. |
| P39 | **Edit distance:** full/rolling DP, banded DP, Myers bit-vector, cache-tiled SIMD wavefront, and parallel diagonals. | EXT | H/H/M | Algorithmic pruning/bit-parallelism beats generic SIMD when edit distance/alphabet permits; cache tiling dominates large full matrices; parallelism needs wide diagonals. | Length/shape, error rate/band width, ASCII/DNA, distance-only/alignment reconstruction, cache tier, 1/2 workers; validate exact distance/path and count pruned cells. |
| P40 | **Word Break:** substring-copy DP, index-based DP, hash set, trie walk, Aho-style transitions, SIMD prefix filter, and parallel candidate batches. | EXT | H/M/M | Avoiding substring allocation and using compact trie/index state gives the main win; SIMD helps reject candidates, while dependency across positions limits simple threading. | String/dictionary size, word lengths/prefix overlap, positive/negative/adversarial cases, boolean/one/all segmentation, dictionary reuse; report allocations, states/bytes touched, build, and output. |
| P41 | **Subset sum/0-1 knapsack:** scalar 1D/2D DP, bitset shift-OR, cache-tiled value DP, meet-in-the-middle, SIMD lanes, and parallel halves. | EXT | H/H/M | Bit-parallel state updates dominate when capacity is moderate; meet-in-the-middle wins small N/huge values; threads help independent halves more than dependency-heavy DP. | N/capacity/value distributions, decision/count/reconstruct contracts, cache tier, bitset/vector sizes, 1/2 workers; record states/second, memory, combination cost, and exact result. |
| P42 | **N-Queens/backtracking:** copied board, boolean arrays, bitmask recursion, symmetry pruning, iterative stack, and parallel top-level subtrees. | EXT | H/L/H | Compact bitmasks and pruning dwarf instruction SIMD; parallel independent subtrees scale if work stealing handles imbalance, while allocations destroy locality. | N=8 upward, count/enumerate/first-solution, symmetry on/off, state layouts, fixed/dynamic scheduling, 1/2 cores/processes; validate known counts and report nodes, ns/node, imbalance, and memory. |
| P43 | **Grid flood fill/islands:** recursive DFS, explicit stack, queue BFS, scanline flood, bitset frontier, SIMD neighbor masks, and tiled parallel components. | EXT | H/M/H | Scanline/tiled layouts reduce queue traffic; SIMD helps regular bitmaps; parallelism depends on component size and merge/boundary overhead. | Grid dimensions, density/clustering/percolation, 4/8-neighbor, byte/bit storage, cache tier, 1/2 workers; verify labels/count and report frontier, stack, boundary-union, and traffic. |
| P44 | **Graph BFS:** pointer/CSR/blocked CSR, top-down/bottom-up/direction-optimizing, bitmap/SIMD frontier, and parallel ownership. | BLOG/EXT | H/M/H | Representation and frontier density dominate; bottom-up/bitmaps win dense frontiers, top-down wins sparse, and two cores help only with balanced low-contention discovery. | Graph families/scale/degree/locality, source, cold/warm, push/pull threshold, 1/2 cores/SMT; validate levels/parents and report TEPS, bytes/edge, frontier timeline, and atomics. |
| P45 | **Connected components/union-find:** DFS/BFS, union-find with compression/rank, label propagation, Shiloach-like parallel methods, cache-blocked edges, and SIMD label scans. | EXT | H/M/H | Union-find is best for streamed edges but has irregular writes; label propagation is SIMD/parallel friendly on some graphs yet needs more passes; layout/relabeling matters. | Graph families, edge order, static/incremental, component distributions, CSR/edge-list layout, 1/2 workers; verify partition equivalence and report passes, atomics, traffic, and build. |
| P46 | **Single-source shortest path:** binary/d-ary heap Dijkstra, radix/delta-stepping, Bellman-Ford frontier, blocked CSR, SIMD relax, and parallel buckets. | EXT | H/M/H | Weight range and frontier shape decide; cache-aware adjacency and buckets beat pointer heaps, while SIMD/parallel relaxation can be lost to irregular writes/atomics. | Graph shape/scale, nonnegative weight distributions, source, sparse/dense frontier, 1/2 workers; validate distances, report edges relaxed, queue operations, atomics, imbalance, and memory. |
| P47 | **PageRank:** scalar CSR pull/push, blocked/reordered graph, SIMD edge accumulation, partitioned local reductions, and threaded iterations. | EXT | H/M/H | Pull avoids atomics and benefits from cache-aware ordering; SIMD is constrained by gathers, while two cores scale until irregular bandwidth and reduction dominate. | Graph families/orderings, FP precision/tolerance, fixed/converged iterations, cache tier, 1/2 workers; report edges/s, iterations, residual/error, traffic, atomics, and preprocessing. |
| P48 | **Triangle counting:** node-iterator intersections using merge, galloping, SIMD set intersection, bitsets, degree orientation, and parallel edge partitions. | EXT | H/H/H | Degree orientation cuts work; SIMD merge wins balanced neighbor lists, galloping wins skew, bitsets win dense neighborhoods; load balance determines parallel gain. | Graph families/degree skew/clustering, sorted adjacency layouts, exact count, 1/2 workers; report intersections, comparisons/bytes, preprocessing, memory, imbalance, and count validation. |
| P49 | **Sparse dot product/SpMV:** scalar COO/CSR, ELL/SELL/block formats, cache reorder, AVX2 gather or padded SIMD, and row/partition parallelism. | EXT | H/M/H | Regular blocked/padded formats enable SIMD but waste work/space; CSR wins irregular sparse data; two cores become memory-bandwidth or imbalance limited. | Matrix dimensions, density, row-length distribution, locality, FP precision, one/many RHS, cache tier, 1/2 workers; include format conversion/reuse and residual/error. |
| P50 | **Exact/approximate k-nearest neighbors:** brute scalar distance, SoA+AVX2 blocked distance, KD/ball tree, product/LSH-style approximate index, and query/data parallelism. | EXT | H/H/H | SIMD brute force wins low dimension/small N and provides the exact baseline; trees win low-dimensional selective queries; high dimension favors blocked brute force or explicitly approximate methods. | N, dimension 2–4K, K, data clustering, FP/int vectors, batch/reuse, cache tier, 1/2 workers; report build/query, recall for approximate methods, bytes, and exact distance checks. |

### 25.1 Cross-question reporting rule

For every P01–P50 result, publish four panels where applicable: normal scalar end-to-end, cache/layout-only scalar, SIMD on the same layout, and cache+SIMD with 1/2 physical cores. Add multiprocess only where isolation or address-space ownership is itself the question. Report preprocessing/build twice—excluded for steady repeated-query cost and included on an amortization curve—so a clever layout or index cannot win by hiding its construction.

### 25.2 Highest-value first twelve

After the validation/primitive milestone, implement P01, P07, P09, P11, P13, P17, P26, P28, P29, P31, P39, and P48 first. Together they exercise sequential scans, output stores, contention-free reduction, irregular lookup, batch locality, SIMD metadata, tree nodes, join partitioning, byte SIMD, dependency-heavy DP, and graph intersection. They give broader architectural coverage than selecting fifty variants of the same array scan.

## 26. Algorithm selection beyond Big-O

Big-O remains a guardrail for growth, but it is not a performance model for a real input range or machine. The benchmark winner is the implementation with the lowest *complete time-to-correct-answer under the required semantics*. Use these accounting models:

```text
T_total(Q) = T_build + T_layout_conversion + Q * T_query
           + T_updates + T_synchronization + T_output + T_cleanup

T_parallel is bounded by some combination of:
  total_work / (workers * useful_rate)
  dependency_span
  bytes_moved / sustainable_bandwidth
  dependent_misses * effective_miss_latency / memory_level_parallelism
  imbalance + communication + synchronization + scheduling
```

The terms are measurements, not a formula to fit after seeing the winner. Count useful and redundant work, dependency depth, passes, bytes, misses, output, and scheduling separately. Work/depth analysis distinguishes total operations from the critical dependency path; Roofline-style analysis distinguishes compute capacity from data-movement ceilings.

### 26.1 Criteria that can matter more than asymptotic complexity

| Criterion | Question the benchmark must answer | Required evidence |
|---|---|---|
| Target N and crossover | Does the asymptotic advantage start inside the actual problem range? | Dense size sweep and confidence interval around every crossover |
| Total work | How many comparisons, probes, relaxed edges, DP cells, hashes, or candidate states execute? | Instrumented useful and redundant work counts |
| Span/dependency depth | How much work is inherently serial even with unlimited workers? | Algorithmic span estimate plus measured critical stages/barriers |
| Parallel slackness | Is there substantially more ready work than hardware contexts? | Ready tasks/frontier/chunks versus worker count |
| Grain size | Is each parallel unit large enough to amortize enqueue, wake, and merge? | Task-size sweep including serial and thread-pool baselines |
| Load balance | Does skew leave one worker on the critical path? | Per-worker work and busy-time min/median/max |
| Synchronization density | How many atomics, locks, barriers, queue operations, or cache-line handoffs occur per useful item? | Counts, retries, waiting time, and coherence traffic |
| Memory traffic | Does a lower-work algorithm move more bytes or make more passes? | Useful versus actual bytes at cache/DRAM/device levels |
| Access regularity | Are accesses streaming/prefetchable or dependent/random? | Stride/MLP, cache/TLB misses, and dependent latency |
| Working-set size | Which implementation fits L1, L2, LLC, DRAM, or disk cache? | Actual live and touched bytes, not container capacity alone |
| Arithmetic/operation intensity | Is performance capped by execution units or bandwidth? | Operations or useful work per byte and attained ceilings |
| SIMD occupancy | How many vector lanes do useful work after masks, tails, conflicts, and gathers? | Active lanes/vector slots, scalar cleanup, gather/scatter cost |
| Branch predictability | Does less theoretical work cause unpredictable control flow? | Branch entropy, MPKI, recovery cycles, and phase behavior |
| Code footprint | Does specialization/unrolling improve data work but overflow DSB/L1I/iTLB? | Hot text bytes and front-end metric passes |
| Preprocessing reuse | How many queries/updates amortize sorting, indexing, packing, reordering, or compilation? | Break-even Q and end-to-end amortization curve |
| Output sensitivity | Is runtime bounded by the number of answers rather than input inspection? | Output count/bytes and time with count-only versus materialization |
| Online versus offline | May the implementation reorder, batch, delay, or see future input? | Separate semantic contracts and maximum permitted latency |
| Mutation and aging | Does a fast static structure stay fast through updates, tombstones, and fragmentation? | Fresh/steady/aged lifecycle phases |
| Extra memory | Does speed require a second array, per-thread copies, padding, index, or frontier? | Peak RSS and bytes per input/useful output |
| Determinism/stability | Is result/order reproducible and is stable ordering required? | Exact semantic checks, run hashes, and tie ordering |
| Exactness/accuracy | Does an approximate or numerically relaxed path meet the error budget? | Error/recall distribution and speed-memory-accuracy frontier |
| Tail latency | Does maintenance, imbalance, allocation, or a worst-case pivot create rare stalls? | p50–p99.9/max and time series, not mean throughput alone |
| Energy and occupancy | Does using two cores save time but consume twice the core-time/energy? | Joules/useful result, CPU-time/wall-time, and idle/spin time |
| Portability | Is the result tied to AVX2 width, Haswell cache geometry, worker count, or one allocator? | Capability manifest and sentinel runs on another architecture |

### 26.2 Parallelism classification of the 50-question portfolio

| Parallel shape | P01–P50 candidates | Main trap |
|---|---|---|
| Contiguous data-parallel map/reduce | P02–P10, P19–P20, P31, P49–P50 | DRAM bandwidth saturates early; more threads can add no throughput |
| Parallel by independent query batch | P01, P11, P23, P25–P28, P35 | Single-query latency may not improve; batching changes the service contract |
| Partition plus local work plus merge | P07–P10, P12–P13, P15, P17–P20, P29–P30 | Extra passes, buffers, skew, and final merge may exceed useful work |
| Frontier or sparse bulk-synchronous | P43–P49 | Frontier size, irregular writes, atomics, and barriers vary by iteration |
| Wavefront/dependency limited | P05–P06, P22, P38–P41, P46 | Plenty of total work does not imply short span or useful two-core scaling |
| Irregular task/subtree parallel | P14, P16, P18, P34, P37, P42, P44–P46 | Static partitioning amplifies subtree/degree/pivot imbalance |
| Speculative or approximate | P16, P18, P27, P34, P39, P50 | Extra candidates and verification must be counted; quality cannot change silently |
| Primarily bandwidth-bound on this DUT | P01–P10, P19, P31, P47, P49 | Two physical cores may already reach the memory ceiling; SMT is not extra bandwidth |

DUT-specific screening rule: CPUs 4 and 6 provide only two isolated physical cores. Parallelism alone therefore cannot rescue an implementation that performs several times more work; its extra-work factor must also be offset by better SIMD occupancy, cache locality, branch behavior, or reduced synchronization. Treat this as a prioritization bound, not a substitute for measurement. Four logical CPUs remain two cores with SMT and must never be labeled 4-core scaling.

### AS01. Measured work and span

**Test:** Instrument representative scan, reduction, prefix, divide-and-conquer, DP-wavefront, graph-frontier, and backtracking implementations for total useful work and algorithmic dependency stages.

**Hypothesis:** Similar `O(N)` algorithms can have radically different span; low-span algorithms scale, while dependency-heavy algorithms remain serial despite abundant total work.

**Test plan:** Use P03, P05, P20, P38, P44, and P42; record work items, stage/barrier count, largest serial stage, T1, T2, and predicted parallelism `work/span`. Compare predictions with two-core speedup and explain scheduler/memory deviations.

### AS02. Strong-scaling efficiency and serial fraction

**Test:** Hold input fixed and run each parallel implementation on one worker, two physical cores, one SMT pair, and two cores/four logical threads.

**Hypothesis:** Compute-rich balanced algorithms approach two-core speedup; memory-bound scans saturate early; SMT helps latency stalls but hurts execution-port or cache pressure.

**Test plan:** Use identical parallel code with worker count one as the speedup baseline, not a different serial implementation. Report wall time, total CPU time, speedup, efficiency, bandwidth, frequency, per-worker work, and SMT topology for at least one scan, sort, join, DP, and graph kernel.

### AS03. Weak scaling and capacity effects

**Test:** Increase problem size in proportion to physical workers while holding work per worker approximately fixed.

**Hypothesis:** Algorithms that strong-scale at one cache tier can weak-scale poorly when the enlarged global structure leaves LLC, increases index depth, or expands merge/frontier state.

**Test plan:** Compare one worker with N against two physical workers with 2N, preserving distribution/output density. Report time, per-worker throughput, global footprint, cache tier, synchronization, and boundary/merge work; include a fixed-footprint control.

### AS04. Within-instance versus across-instance parallelism

**Test:** Compare parallelizing one large query/problem with running independent scalar instances or queries on separate cores.

**Hypothesis:** Batch/query parallelism usually avoids barriers and shared writes and can outperform a sophisticated within-instance algorithm, but it does not reduce latency for a lone request.

**Test plan:** Use binary search, hash lookup, kNN, edit distance, and Top-K; keep total inputs and completed answers equal. Sweep batch 1–1,024 and latency deadline, report per-query latency, batch makespan, throughput, queueing, and memory replication.

### AS05. Grain-size and scheduler crossover

**Test:** Sweep tasks per chunk and compare serial loop, static chunks, atomic index, central queue, and work-stealing scheduling.

**Hypothesis:** Fine tasks expose parallelism but drown in scheduling/cache-line traffic; coarse tasks reduce overhead but amplify skew and tail latency.

**Test plan:** Calibrate tasks from 10 ns to 10 ms with uniform and heavy-tailed work, then repeat on P15, P20, P42, and P48. Record enqueue/dequeue/steal counts, worker idle time, p99 task wait, useful CPU fraction, and end-to-end crossover.

### AS06. Load balance under data skew

**Test:** Compare static equal-index, equal-estimated-work, dynamic chunk, and work-stealing partitions for skewed keys, degrees, ranges, and search subtrees.

**Hypothesis:** Equal input counts do not mean equal work; dynamic scheduling recovers utilization but adds queue/coherence overhead and can damage locality.

**Test plan:** Uniform, Zipf 0.8/1.0/1.2, one giant component/high-degree vertex, variable match output, and adversarial subtree cases. Publish worker work/busy-time distributions, steals, locality counters, makespan, and fairness.

### AS07. Work-efficient versus extra-work parallel algorithm

**Test:** Compare a work-efficient serial/dependency-heavy algorithm with a lower-span algorithm that executes more operations.

**Hypothesis:** Extra work wins only when it unlocks enough parallel/vector throughput or regular memory access; on one/two cores the crossover may be much later than on many-core hardware.

**Test plan:** Use serial versus tree reduction, Dijkstra versus delta/Bellman-style relaxation, union-find versus label propagation, and top-down versus bottom-up BFS. Count extra work explicitly, sweep N/graph shape and 1/2/SMT workers, and plot time against work inflation.

### AS08. One irregular pass versus multiple streaming passes

**Test:** Compare one-pass random/in-place algorithms with two- or three-pass histogram, prefix, partition, or materialize-and-stream algorithms.

**Hypothesis:** Extra sequential passes can beat fewer dependent random accesses because prefetching, SIMD, write combining, and parallel ownership outweigh added bytes—until bandwidth or temporary memory becomes limiting.

**Test plan:** Use filter/compact, radix partition, hash join, group-by, graph relabeling, and strided-to-packed numerical kernels. Record passes, sequential/random bytes, allocation, cache/TLB misses, bandwidth, preprocessing break-even, and output.

### AS09. Recompute versus load/memoize

**Test:** Compare loading precomputed values from increasingly large tables with recomputing equivalent cheap, medium, and expensive functions.

**Hypothesis:** Recomputing cheap values can beat a cache/DRAM miss and reduce footprint, while memoization wins for expensive computation or high local reuse.

**Test plan:** Functions 1–1,024 arithmetic instructions, table L1–DRAM, sequential/random/Zipf access, scalar/AVX2 and 1/2 workers. Include table construction and report cycles/value, bytes, hit rate, code size, and break-even reuse.

### AS10. SIMD occupancy versus nominal vector width

**Test:** Compare scalar and AVX2 versions while instrumenting active lanes, masked/rejected lanes, gathers, conflict repair, and scalar tails.

**Hypothesis:** Nominal eight-lane AVX2 code can deliver little speedup on short, divergent, sparse, or gather-heavy work; regrouping/bucketing inputs may matter more than vector width.

**Test plan:** Use filter, histogram, B-tree nodes, sparse intersection, byte parsing, edit distance, and kNN. Sweep batch length, selectivity, sparsity, alignment, tails, and grouping cost; report useful lanes/vector, cycles/useful item, and conversion/materialization.

### AS11. SIMD-versus-threading interaction

**Test:** Measure scalar one-core, SIMD one-core, scalar two-core, and SIMD two-core using the identical layout and semantics.

**Hypothesis:** SIMD and threading multiply only for compute-bound kernels; for bandwidth-bound scans the first optimization consumes the shared bandwidth headroom and leaves little for the second.

**Test plan:** Apply to count, dot, filter, radix, substring, SpMV, and kNN across the cache ladder. Report absolute time and incremental speedup of each factor, memory bandwidth, frequency, and a two-factor interaction term; do not multiply separate headline ratios.

### AS12. Preprocessing and break-even query count

**Test:** Compare a no-build algorithm against sorted, indexed, packed, transposed, reordered, or compiled structures over query/update reuse Q.

**Hypothesis:** The best steady query implementation can be the worst one-shot choice; cache/SIMD-friendly layouts win only after a measurable reuse threshold that moves with mutation rate.

**Test plan:** Q=1, 2, 4, 8, 16, 64, 1K, and N; apply to search, B-tree, Bloom, trie/suffix, graph reorder, GEMM packing, and kNN index. Publish build, query, maintenance, memory, and the confidence interval for break-even Q.

### AS13. Output-sensitive performance

**Test:** Run existence-only, count-only, index-return, and full-materialization contracts for algorithms whose answer size varies.

**Hypothesis:** Join, intersection, match, enumeration, and traversal results become write/allocation bound at high output cardinality; count-only rankings do not predict materialized APIs.

**Test plan:** Use set operations, substring/multipattern search, joins, group-by, palindrome, N-Queens, triangle listing, and kNN. Sweep output from zero to superlinear where safe, preallocated/growing/streamed sinks, and report input work separately from bytes/objects emitted.

### AS14. Early exit, cancellation, and speculative wasted work

**Test:** Compare serial early-exit with parallel chunk search, ordered cancellation, unordered first-winner, and full-scan implementations.

**Hypothesis:** Parallel search wins late-hit/miss cases but may perform far more work for early hits; cancellation polling frequency trades wasted work for coordination overhead.

**Test plan:** Apply to membership, Two Sum existence, substring, tree search, and first-solution backtracking. Sweep hit position/probability, worker count, chunk/order, cancellation interval, deterministic-result requirement, and report time plus examined/wasted items.

### AS15. Online latency versus offline throughput

**Test:** Compare immediate per-item algorithms with batching, query sorting, buffering, microbatch SIMD, and deferred parallel processing under explicit deadlines.

**Hypothesis:** Batching raises throughput/cache reuse but introduces queueing delay and can violate an online service contract; the correct batch size is arrival-rate and deadline dependent.

**Test plan:** Use search, hash, parsing, graph queries, and DB writes with closed-loop and Poisson/open-loop arrivals. Sweep batch/time window and offered load, record queue delay, service time, p50–p99.9, throughput, and deadline misses with coordinated-omission-safe measurement.

### AS16. Extra-space versus locality and parallelism

**Test:** Compare in-place structures with out-of-place double buffers, per-thread private state, padded arrays, indexes, and materialized layouts under a fixed memory budget.

**Hypothesis:** Extra memory often buys streaming writes, conflict-free updates, and simple SIMD, but can push the working set across cache/DRAM or cause allocation/fault costs that reverse the result.

**Test plan:** Apply to partition, merge/radix sort, graph frontiers, DP rows, histograms, and joins; sweep memory cap and cache tier. Report peak/live/touched bytes, faults, traffic, speed, and failure/slow path when the budget is exceeded.

### AS17. Determinism, stability, and reproducible parallel results

**Test:** Compare deterministic stable algorithms with faster unstable/nondeterministic reductions, schedulers, hash iteration, and graph-parent selection.

**Hypothesis:** Relaxing order can improve throughput and balance, but reproducibility, floating-point variation, debugging, and downstream sorting impose measurable costs that must be included when required.

**Test plan:** Use sort, Top-K ties, FP reduction, group-by, BFS parents, and parallel search. Run 100 repetitions, hash complete outputs, quantify numerical/order variation, then include canonicalization cost and compare only contracts acceptable to the caller.

### AS18. Exactness, approximation, and Pareto frontiers

**Test:** Compare exact algorithms with sketches, approximate filters/indexes, bounded/banded DP, early termination, quantization, and ANN at multiple error budgets.

**Hypothesis:** Approximation can move a workload to a smaller cache tier or remove work, but one default setting hides the useful speed-memory-quality frontier and verification can erase gains.

**Test plan:** Use frequency/cardinality, Bloom/Xor filters, quantiles, edit distance, kNN, and mixed precision. Sweep error/recall/false-positive budgets, seeds and adversarial inputs; publish Pareto-optimal latency, throughput, memory, energy, and quality including exact verification.

### 26.3 Higher-work algorithms that can still win

The following experiments deliberately compare algorithms where the apparent work/asymptotic loser may be the machine-level winner. A win is valid only over the measured domain and stated semantic contract; the plan must still show where asymptotic growth eventually reverses it.

### HW01. SIMD scan versus hash/tree/index lookup

**Test:** Compare contiguous `O(N)` scan with average-`O(1)` hash and `O(log N)` binary/B+ tree lookup for one and batched queries.

**Hypothesis:** Scan wins small/medium N, high selectivity, cold indexes, or SIMD batches because it streams compact data; indexes win after their avoided bytes exceed pointer/probe/setup costs.

**Test plan:** Sweep N through cache tiers, Q, hit/selectivity, key width, sortedness, static/update state, scalar/AVX2 and 1/2 workers. Include index build/memory and publish the N×Q break-even surface.

### HW02. Query sorting before irregular lookup

**Test:** Compare queries in arrival order with an extra `O(Q log Q)` sort or radix partition followed by locality-ordered lookup and output-order restoration.

**Hypothesis:** Added ordering work wins when Q is large and table/tree probes otherwise miss caches/TLB; it loses for online/small batches or cheap hot lookups.

**Test plan:** Apply to binary/B-tree/hash lookup, graph adjacency requests, and KV reads; sweep Q, locality, cache tier, key distribution, stable restoration, and 1/2 workers. Report sort, lookup, reorder, misses, and latency deadline separately.

### HW03. Full parallel sort versus linear selection/heap Top-K

**Test:** Compare full sort `O(N log N)` with quickselect expected `O(N)`, size-K heap `O(N log K)`, and their parallel variants for Top-K/kth queries.

**Hypothesis:** Highly optimized parallel sort can win at large K, many requested ranks, or required sorted output despite more comparisons; selection wins isolated small-K queries.

**Test plan:** Sweep N, K, requested-rank count, distribution, stable/tie semantics, sorted-output requirement, 1/2 workers. Count comparisons/moves and include final K sort, allocation, partition imbalance, and repeated-query reuse.

### HW04. Sort-and-unique versus expected-linear hash deduplication

**Test:** Compare `O(N log N)` sort+unique with expected-`O(N)` hash dedup, dense bitmap/direct addressing, and streaming approximate+verify variants.

**Hypothesis:** Sort+unique can win through contiguous memory, low overhead, parallel sort, compact output, and required ordering; hash wins stable-first or one-pass streaming without ordered output.

**Test plan:** N, cardinality/skew, item/key width, already/nearly/random order, stable-first/sorted output, memory cap, 1/2 workers. Include allocation, hash resizing, sorting, output and peak memory.

### HW05. Sort-merge join versus expected-linear hash join

**Test:** Compare sort+merge `O(N log N)` construction with expected-linear hash join, radix-partitioned hash, nested SIMD tiny-side scan, and index nested loop.

**Hypothesis:** Sort-merge wins when inputs are preordered, reused, range output is useful, or streaming/parallel merge offsets sorting; hash wins one-shot unsorted equality joins with suitable memory.

**Test plan:** Sweep side ratio, skew, match multiplicity, presorted fraction, build reuse, payload/output, cache tier, and 1/2 workers. Report build/sort/partition/probe/merge/output and spill behavior.

### HW06. Dense direct table/bitmap versus asymptotically compact hash/tree

**Test:** Compare `O(N+U)` initialization/direct-address methods over universe U with `O(N)` expected hash or `O(N log N)` tree methods.

**Hypothesis:** A dense array/bitmap wins when U fits cache or can be lazily generation-tagged; clearing a huge sparse universe makes the formally simple method lose.

**Test plan:** Use membership, histogram, Two Sum, Top-K frequency, visited sets, and bounded-domain median; sweep N/U density, clear strategy, generations, key skew, cache tier, and workers. Report initialization, lookup/update, bytes, and full lifecycle.

### HW07. Linear query versus preprocessing-heavy range structure

**Test:** Compare per-query `O(N)` scan with prefix, sparse-table, Fenwick, segment-tree, and blocked structures whose construction adds work and memory.

**Hypothesis:** A scan wins one/few or short ranges and mutable data; precomputation wins high reuse, while SIMD short scans push the break-even query count upward.

**Test plan:** Range sum/min, N, Q, range length/locality, update rate, scalar/AVX2 scan, 1/2 query workers. Publish build/update/query/memory and Q break-even for each cache tier.

### HW08. Multi-pass block sliding window versus one-pass deque

**Test:** Compare online one-pass deque with offline block prefix/suffix and chunked parallel sliding-window maximum that read/write more data.

**Hypothesis:** The extra-pass block algorithm wins large offline arrays through branch-free SIMD/parallel work; deque wins streaming, small N, and tight memory.

**Test plan:** Sweep N, K, monotonic/random/duplicates, batch deadline, block/chunk size, 1/2 workers and memory cap. Include halos, temporary arrays, output, traffic, and online latency.

### HW09. Repeated direct string search versus constructed automaton/index

**Test:** Compare repeated naive/Two-Way/KMP scans with trie/Aho-Corasick, suffix array/automaton, and rolling index across pattern/query reuse.

**Hypothesis:** Repeated streaming scans can beat construction for few queries and exploit SIMD; indexes/automata win enough patterns/reuse but may become cache-bound as state grows.

**Test plan:** Text/pattern size, count, alphabet/repetition, matches/output, Q, online additions, cache tier, 1/2 workers. Report construction, memory, scan/query, verification, and break-even Q.

### HW10. Brute-force SIMD kNN versus nominally sublinear spatial index

**Test:** Compare exact `O(Nd)` blocked SoA distance scan with KD/ball tree and separately labeled approximate indexes.

**Hypothesis:** Brute force wins small N, large dimension, large query batch, or low-pruning data because it vectorizes/parallelizes; trees win low dimensions and selective isolated queries.

**Test plan:** Sweep N, d=2–4,096, K, clusters/intrinsic dimension, batch, cache tier, exact/recall contract, build reuse, and 1/2 workers. Count candidates/distances and include build/layout/output.

### HW11. Blocked Floyd–Warshall versus repeated sparse SSSP

**Test:** Compare `O(V³)` blocked Floyd–Warshall with repeated Dijkstra/delta-stepping and matrix/GraphBLAS-style all-pairs formulations.

**Hypothesis:** On dense graphs or many sources, regular blocked/vectorized Floyd–Warshall can beat lower-work irregular heap traversals; sparse few-source workloads favor SSSP.

**Test plan:** V within safe memory, density 0.01–100%, weight distributions, source count 1–V, scalar/blocked/AVX2 and 1/2 workers. Validate distances and report relaxations, bytes, preprocessing, memory, and source-count break-even.

### HW12. Bellman-style bulk relaxation versus Dijkstra

**Test:** Compare work-efficient priority-queue Dijkstra with Bellman-Ford/frontier relaxation and delta-stepping that may relax more edges but exposes parallel buckets.

**Hypothesis:** Extra relaxations win on low-diameter graphs and suitable weight ranges through contiguous/parallel edge processing; Dijkstra wins sparse high-diameter or single-core cases with effective queues.

**Test plan:** Graph shape/diameter/degree, edge order, weight range, source, delta width, 1/2 workers/SMT. Count successful/attempted relaxations, queue/bucket operations, atomics, passes, imbalance, and verify exact distances.

### HW13. Bottom-up/direction-optimizing BFS versus work-efficient top-down BFS

**Test:** Compare top-down edge traversal with bottom-up vertex scans and dynamic direction switching.

**Hypothesis:** Bottom-up may inspect redundant neighbor candidates but wins when the frontier is dense by reducing contested discovery and exposing bitmap/parallel operations; it loses on sparse frontiers.

**Test plan:** Road, mesh, RMAT, social-like, high-diameter, and adversarial graphs; source/frontier evolution, switch thresholds, bitmap/list formats, 1/2 workers. Count edges/candidates examined, frontier bytes, atomics, barriers, and validated levels.

### HW14. Label propagation versus union-find/DFS components

**Test:** Compare multi-pass label propagation with work-efficient union-find and DFS/BFS connected components.

**Hypothesis:** Label propagation performs more edge passes but is regular and parallel; it wins low-diameter well-clustered graphs, while union-find/DFS wins long-diameter or few-core cases.

**Test plan:** Graph families, component sizes/diameter, edge ordering/relabeling, convergence rule, SIMD label scans, 1/2 workers. Report passes, edges processed, atomics, label changes, cache traffic, and exact partition equivalence.

### HW15. Dense bitset graph operations versus sparse adjacency lists

**Test:** Compare scanning/AND-popcount over padded bitsets with merge/gallop intersection over sparse neighbor lists for reachability, triangles, and common-neighbor queries.

**Hypothesis:** Bitsets execute work proportional to the vertex universe, yet win past a density/batch threshold through contiguous SIMD/POPCNT and simple parallel partitioning.

**Test plan:** V, density, degree skew/clustering, query batch, bitset compression, cache tier, 1/2 workers. Report zero words inspected, useful intersections, bytes, preprocessing/memory, and exact outputs.

### HW16. Padded regular sparse formats versus minimal-work CSR

**Test:** Compare CSR/COO with ELL, SELL-C-sigma, blocked sparse, and dense fallback formats that store/compute padding or reorder rows.

**Hypothesis:** Extra padded operations can win through unit-stride SIMD and balanced parallel rows; highly irregular matrices make padding, conversion, and bandwidth prohibitive.

**Test plan:** SpMV and sparse dot/batch RHS across row-length distributions, density/locality, slice/block widths, FP type, cache tier, 1/2 workers. Count real/padded nonzeros, lane occupancy, conversion reuse, bytes, balance, and numerical error.

### HW17. Private replicated state plus merge versus shared atomic state

**Test:** Compare one-pass atomic shared updates with per-thread private histograms/hashes/frontiers followed by reduction or merge.

**Hypothesis:** Replication deliberately adds initialization and merge work but wins when it removes hot-line contention; it loses when private state exceeds cache or keys are mostly disjoint.

**Test plan:** Histogram, group-by, graph frontier, visited bitmap, and frequency count; state size L1–LLC, skew, update density, 1/2/SMT workers. Report atomics/retries, ownership traffic, private bytes, initialization/merge, and end-to-end time.

### HW18. Classical blocked GEMM versus asymptotically faster multiplication

**Test:** Compare classical `O(N³)` packed/blocked GEMM with Strassen-like recursion and a reference library for square and non-square matrices.

**Hypothesis:** Classical GEMM wins practical small/medium N through tuned cache/FMA kernels, lower memory, and numerical behavior; Strassen crosses over only when reduced multiplications repay additions/copies/recursion.

**Test plan:** FP32/FP64, N and awkward shapes, cutoff, workspace cap, 1/2 threads, hot/cold allocation. Include packing/workspace, FLOPs/bytes, frequency, forward/residual error, and crossover confidence.

### HW19. Direct convolution versus FFT convolution

**Test:** Compare asymptotically worse direct `O(NK)` scalar/SIMD/cache-blocked convolution with FFT-based `O(N log N)` convolution.

**Hypothesis:** Direct SIMD wins small kernels, short signals, and one-shot use; FFT wins long kernels/signals or repeated filters after plan/transform amortization.

**Test plan:** 1D/2D sizes, K, batch/reuse, real/complex, padding/boundaries, cache tier, 1/2 threads. Include planning, transforms, workspace, output, numerical error, and break-even surfaces.

### HW20. Materialize/pack/transpose before compute versus direct strided access

**Test:** Compare direct strided/gather computation with an extra `O(N)` layout conversion followed by contiguous SIMD/parallel kernels.

**Hypothesis:** Copying first wins when compute/query reuse or poor strides repay conversion and extra traffic; direct access wins one-shot small work or constrained memory.

**Test plan:** GEMM/GEMV, tensor contraction, column aggregation, hash payload split, and graph relabeling; stride/layout, reuse, cache tier, payload, 1/2 workers. Report conversion, kernel, output, peak memory, and break-even reuse.

### HW21. Comparison sort versus fixed-key radix/counting sort

**Test:** Compare comparison `O(N log N)` sort with nominally linear multi-pass radix/counting variants under full semantic constraints.

**Hypothesis:** Radix wins large fixed-width keys when passes/histograms remain cache-efficient; comparison sort wins small N, expensive payload movement, variable strings, custom comparators, or in-place/stable constraints.

**Test plan:** N, key/payload width, entropy/common prefixes, distributions, stable/in-place/order semantics, radix width, cache tier, 1/2 workers. Count passes, comparisons, bytes, temporary memory, and output verification.

### HW22. More-node parallel search versus stronger serial pruning

**Test:** Compare highly pruned depth-first backtracking with parallel breadth/top-subtree search that may copy state and visit more nodes.

**Hypothesis:** Parallel search can beat a lower-work serial heuristic on hard balanced instances, but loses early-solution and heavily imbalanced cases through duplicated/speculative work.

**Test plan:** N-Queens, Sudoku, subset/branch-and-bound, and path search; count/all/first-solution contracts, heuristic strength, cutoff depth, work stealing, deterministic order, 1/2 cores/processes. Report nodes, copies, steals, cancellation waste, time, and tails.

### HW23. Recompute/naive recursion versus memoized dynamic programming

**Test:** Compare allocation-free recursive or repeated computation with memo/table DP, rolling DP, and bit-parallel DP across the small-N crossover.

**Hypothesis:** The asymptotically worse method can win tiny inputs because table initialization, hashing, allocation, and output reconstruction dominate, but must lose sharply as repeated subproblems grow.

**Test plan:** Fibonacci-like calibration only as validation, then Word Break, LCS variants, subset sum, and interval DP; N near crossover, positive/negative/adversarial instances, stack/table layouts, one/many queries. Count states and include initialization/memory.

### HW24. Compressed representation plus decode versus uncompressed access

**Test:** Compare uncompressed arrays/indexes/adjacency with delta, variable-byte, bit-packed, dictionary, or block-compressed representations decoded scalar/SIMD.

**Hypothesis:** Added decode instructions win when reduced footprint moves data into a nearer cache tier or saves DRAM/I/O bandwidth; random updates and tiny hot data favor uncompressed form.

**Test plan:** Integer postings/adjacency, keys and numerical arrays; compression ratio, block size, sequential/random queries, update/static state, cache tier, 1/2 workers. Report encode/decode, bytes, lane utilization, query/output, memory, and break-even reuse.

### 26.4 Decision rule for publication

For each algorithm family, publish a Pareto table rather than one global winner. A candidate is dominated only if another implementation is no worse in correctness/accuracy, latency, throughput, memory, preprocessing, and required hardware, and is strictly better in at least one. Highlight these distinct winners when they differ:

- Lowest one-shot latency.
- Highest warmed single-core throughput.
- Lowest p99 under the stated arrival rate.
- Best two-physical-core throughput and efficiency.
- Lowest memory and bytes moved.
- Lowest energy per correct result.
- Earliest break-even with preprocessing included.
- Best deterministic/exact implementation.
- Best approximate implementation at each declared quality budget.

Never write “algorithm A is faster” without the N range, data distribution, semantic contract, cache state, reuse count, worker topology, and whether build/output costs were included.

## 27. Agner Fog-derived low-level and instruction-level experiments

This section converts the instruction-table methodology and every architectural script in the inspected `testp.zip` archive into explicit experiments. “Agner-derived” means that the source program motivated the question or construction; the published implementation must still follow this roadmap's controls, archive the generated machine code, and distinguish an exact-source reproduction from an independently generated extension.

### 27.1 Source-coverage audit

Every `.sh2` architectural script in the inspected archive is assigned below. No script is silently omitted; unsupported features become explicit capability rows.

| Agner script | Roadmap experiment(s) | What is extracted |
|---|---|---|
| `AVX_states.sh2` | IL49 | AVX upper-state transitions and `vzeroupper` |
| `branch.sh2` | IL23–IL24 | conditional patterns and indirect-target prediction |
| `cache_banks.sh2` | IL37 | address-dependent L1 conflicts/false dependencies |
| `cache_latency.sh2` | IL34 | serialized cache/memory load latency |
| `cache_throughput.sh2` | IL35, IL45–IL46 | cache bandwidth, non-temporal traffic, prefetching |
| `daxpy.sh2` | IL63 | instruction results versus a useful streaming kernel |
| `decode_double.sh2` | IL12 | complex/multi-uop decoder supply |
| `decode_nops.sh2` | IL13 | instruction length and NOP decoding |
| `decode_prefix.sh2` | IL14 | redundant/meaningful prefix cost |
| `decoder_throughput.sh2` | IL11 | front-end decode/delivery ceiling |
| `exec_domain.sh2` | IL31 | integer/vector/FP domain round trips |
| `fused_branch.sh2` | IL25 | compare/test-plus-branch macro-fusion |
| `instruct_boundaries.sh2` | IL16 | alternate entry and cached instruction boundaries |
| `jmp.sh2` | IL22 | taken-jump density and branch-delivery limits |
| `length_chg_prefix.sh2` | IL15 | length-changing prefix and boundary interaction |
| `loop_buffer.sh2` | IL17 | loop-stream delivery behavior |
| `loop_size.sh2` | IL21 | bytes, uops, body size, and loop-delivery knees |
| `memcpy.sh2` | IL43 | copy implementation and size crossovers |
| `memory_mirror.sh2` | IL40 | same-address/address-mode alias portability probe |
| `mixed_throughput.sh2` | IL10 | mixed port/delivery bottlenecks |
| `mov_rename.sh2` | IL28 | move elimination/rename limits |
| `out_of_order.sh2` | IL32 | independent-chain overlap and scheduler/window proxy |
| `partial_register_stall.sh2` | IL30 | partial GPR, flags, XMM, and YMM dependencies |
| `read_write_bandwidth.sh2` | IL36 | L1 load/store port ceiling |
| `resource_sharing.sh2` | IL33 | SMT-shared front-end/execution resources |
| `returnstack.sh2` | IL26 | return-address-stack capacity and recovery |
| `stack_sync_uops.sh2` | IL27 | stack engine synchronization/uops |
| `store_forwarding.sh2` | IL39 | store-to-load forwarding compatibility matrix |
| `subnormal.sh2` | IL51–IL52 | exceptional FP values and FP control modes |
| `taylor.sh2` | IL64 | serial FP dependency/FMA composite kernel |
| `testmemcpyalign.sh2` | IL44 | source/destination alignment matrix |
| `ucache_double_entries.sh2` | IL19 | instructions consuming extra uop-cache entries |
| `ucache_misprediction.sh2` | IL20 | branch recovery with different delivery sources |
| `ucache_size.sh2` | IL18 | decoded-uop-cache capacity/associativity knees |
| `unaligned_mem.sh2` | IL38 | split-line/page loads, stores, and forwarding |
| `warmup_fp.sh2` | IL50 | first-use/wake-up behavior of vector/FP units |
| `xor.sh2` | IL29 | zero-idiom recognition and dependency breaking |

The list-driven CSV suites are covered as generated rows rather than one hand-written headline value:

| Source family | Roadmap experiment(s) | DUT treatment |
|---|---|---|
| `int`, `int_misc` | IL03–IL09, IL58, IL62 | Run all legal integer operand forms and value classes |
| `ivec1`–`ivec5` | IL03–IL09, IL54, IL56–IL57 | Run MMX/SSE/AVX2 forms; separate widths and lane topology |
| `fvec1`–`fvec6` | IL03–IL09, IL49, IL51–IL54, IL57, IL59 | Run supported x87/SSE/AVX/FMA forms and exceptional-value cases |
| `gather` | IL55 | Run AVX2 gathers; mark scatter/AVX-512 forms unsupported |
| `stringvec` | IL60 | Run scalar/string/vector-string forms with explicit lengths |
| `maskinstr`, `misc512`, `fp16` | IL09 | CPUID/XCR0 gate; preserve as `unsupported_on_dut` on Haswell |
| legacy `.sh1` suites | IL03–IL09 and IL49–IL62 | Use as a second source/reproduction path, not duplicate evidence |

The older hand-written `.sh1` suite is also explicitly routed:

| Agner script | Roadmap experiment(s) | Treatment |
|---|---|---|
| `32bitinstr.sh1` | IL09 | Separate 32-bit ABI/mode build; never mix with x86-64 rows |
| `amd.sh1` | IL09, IL40 | Portability probe; execute only cases legal and meaningful on Intel Haswell |
| `avx2.sh1` | IL49, IL53–IL59 | AVX2 arithmetic, shuffle, conversion, gather, and state rows |
| `convers.sh1`, `convers2.sh1` | IL07, IL31, IL57 | conversion/domain chains and combined round trips |
| `div.sh1` | IL08, IL58–IL59 | integer/FP divide, square root, value-class surfaces |
| `fma.sh1` | IL53 | FMA forms, dependent operand, and throughput |
| `fvec.sh1` | IL03–IL08, IL51–IL54, IL57, IL59 | supported floating-vector rows |
| `gather.sh1` | IL55 | AVX2 gather; unsupported scatter forms remain gated |
| `int1.sh1` | IL03–IL08, IL58, IL62 | base integer latency/throughput/special rows |
| `ivec.sh1` | IL03–IL08, IL54, IL56–IL57 | integer-vector operation families |
| `misc_int.sh1`, `new_int_instr.sh1` | IL62 | special/extension integer instructions with CPUID gating |
| `misc_vect.sh1` | IL54, IL56–IL57, IL62 | miscellaneous supported vector forms |
| `mul.sh1` | IL08, IL58 | multiply forms, result dependencies, operand cases |
| `pushpop.sh1` | IL27 | push/pop and stack-engine controls |
| `shift.sh1` | IL08, IL58 | immediate/variable shifts, rotates, and counts |
| `strings.sh1` | IL60 | string instructions by length and match location |
| `x87.sh1` | IL51–IL52, IL57, IL59, IL61 | x87 arithmetic, control state, conversion, and comparison |

### 27.2 Harness and instruction-table methodology

#### IL01. Timer, serialization, and matched-reference overhead

**Test:** Measure empty generated bodies with the same entry/exit, loop, register saves, dependency seed, branch count, alignment, and timing instructions used by every instruction test. Compare `RDTSCP`-bounded timing, `perf_event_open` core-cycle counting, and whole-process `perf stat`.

**Hypothesis:** Short bodies are dominated by timing and loop machinery; a repeated 100+ instruction body plus matched subtraction makes the net estimate stable, while naive one-instruction timing materially overstates cost.

**Measurement construction:** Generate bodies containing 0, 1, 10, 100, and 1,000 neutral operations. Use a serializing preamble appropriate to Haswell, prevent compiler movement with assembly boundaries, and put checksum use after the timed region. Interleave each body with its structurally identical reference.

**Derived measurement:** `net_core_cycles = test_core_cycles - reference_core_cycles`; divide only by the known dynamic test-instruction count. Plot net estimate and confidence interval versus body length. A valid harness approaches a plateau and produces approximately zero for a neutral-versus-identical control.

**Test plan:** Run on CPU 4, fixed-frequency and turbo profiles, 15 interleaved repetitions, four code alignments, and GCC/NASM generators. Reject migration, interrupts/outliers by the common policy, not by selecting the minimum. Archive raw and subtracted values so negative noisy deltas remain visible.

#### IL02. TSC ticks versus actual core cycles

**Test:** Compare invariant-TSC elapsed ticks, unhalted core cycles, reference cycles, APERF/MPERF-derived effective frequency, and wall time for the same integer, FP, L1, and DRAM bodies.

**Hypothesis:** TSC is stable wall-time instrumentation but is not an instruction latency denominator when turbo or throttling changes core frequency; unhalted core cycles remain the correct primary measure.

**Measurement construction:** Run identical fixed-work bodies while sweeping turbo on/off, warm/cool state, and an intentionally bounded thermal soak. Collect each counter in compatible, minimally multiplexed groups and log frequency before/during/after.

**Derived measurement:** Report `TSC_ticks/op`, `core_cycles/op`, `GHz_effective = core_cycles/elapsed_seconds`, and their ratios. The instruction result is accepted only when cycles/op is stable and changes in TSC/op are explained by frequency.

**Test plan:** At least 1 s per sample, 15 repetitions per profile, randomized profiles across sessions. Establish allowable APERF/MPERF and temperature drift and reuse it as an automatic acceptance check for IL03–IL64.

#### IL03. Same-domain instruction latency

**Test:** Measure latency of every supported instruction/operand form that can feed its result directly into the next instance: integer ALU, shift, multiply, vector integer/FP, shuffle, convert, and selected memory forms.

**Hypothesis:** The steady dependency-chain result matches the true critical-path delay, but varies by operand form, vector width, and sometimes operand value.

**Measurement construction:** Emit a closed chain such as `r = op(r, invariant)` or alternate two registers when architectural constraints require it. Unroll at least 100 static instances without independent work; use a neutral dependency-preserving control with identical loop structure and register pressure.

**Derived measurement:** `latency = (cycles_chain - cycles_control) / dynamic_op_count`. Also report instructions, uops, and assist events per op. A chain must show no spill and exactly the intended data dependency in disassembly.

**Test plan:** Generate rows by mnemonic, opcode bytes, register/memory/immediate form, width, element type, and relevant value class. Repeat chain lengths 32/100/256 as a convergence test; run both GCC-inline-assembly and NASM exact encodings for a validation subset.

#### IL04. Reciprocal throughput with independent streams

**Test:** Measure steady-state reciprocal throughput for the same supported forms as IL03 using mutually independent destinations.

**Hypothesis:** Throughput improves as independent streams hide latency, then plateaus at an execution-port, decoder, or issue-width ceiling; using too few streams falsely reports latency-limited throughput.

**Measurement construction:** Generate 1, 2, 4, 6, 8, 12, and 16 independent register chains, round-robin the instruction across them, and keep source operands resident. Match total dynamic instructions and loop structure in a neutral control. Keep code inside/outside the uop cache as an explicit dimension rather than an accident.

**Derived measurement:** `reciprocal_throughput = net_core_cycles / dynamic_op_count`; choose the best stable plateau across sufficient chain counts, not a cherry-picked repetition. Report ops/cycle as its reciprocal and the stream count at saturation.

**Test plan:** Run L1-resident register forms first, then memory forms separately. Collect port/uop and front-end counters after timing. Flag a row `front_end_limited` when enlarging the body changes throughput without changing execution dependencies.

#### IL05. Decoded-uop and execution-port decomposition

**Test:** For IL03/IL04 rows, count retired/issued uops and Haswell port utilization in separate compatible passes; test the instruction alone and in targeted port-contention mixtures.

**Hypothesis:** Port occupancy and uop count explain most throughput ceilings, while discrepancies reveal front-end limits, microcode, assists, or counter-model limitations.

**Measurement construction:** Use a long independent-stream body. First measure the candidate alone; then pair it 1:1 and 2:1 with instructions known to prefer candidate ports or complementary ports. Preserve dependency independence and dynamic counts.

**Derived measurement:** Report uops/instruction, per-port uops/instruction, cycles/instruction, and mixture throughput. Infer a port set only when the isolated and mixture results agree; label any inference rather than presenting PMU counts as architectural truth.

**Test plan:** Calibrate with Haswell-known single-uop integer adds, loads, stores, and branches as positive controls. Use non-multiplexed groups, subtract the control body's uops, and record skid/scaling. Repeat counter rows if the timing body changes.

#### IL06. Register form versus fused memory-operand form

**Test:** Compare register-register instructions with architecturally equivalent memory-source forms, and with an explicit load plus register-register instruction.

**Hypothesis:** A memory operand may decode to an extra load uop yet fuse in the back end; its performance depends on cache tier, addressing mode, load-port pressure, and whether the explicit load enables better scheduling.

**Measurement construction:** For latency, serialize address/load/use with L1-hot data and subtract pointer-update controls. For throughput, use independent addresses and registers with 1–16 streams. Compare `op reg,reg`, `op reg,[mem]`, and `load tmp,[mem]; op reg,tmp` with identical useful semantics.

**Derived measurement:** Report latency delta, reciprocal-throughput delta, uops/op, load-port occupancy, bytes/op, and code bytes/op. The fused-memory result includes the load; never subtract a separately measured load and call the remainder exact.

**Test plan:** Base+offset, indexed, scaled-index, and RIP-relative addressing; aligned/cross-line data; L1/L2/LLC/DRAM footprints. Use integer, scalar FP, vector FP, and representative RMW forms.

#### IL07. Cross-register-file/domain round-trip latency

**Test:** Measure pairs that cross GPR↔XMM, integer-vector↔FP-vector, x87↔integer/SSE, and mask/state domains where supported.

**Hypothesis:** Domain crossings can add latency that an isolated same-domain table omits; a closed round trip is measurable even when neither one-way edge can be separately chained.

**Measurement construction:** Build `A -> conversion/move X -> B -> inverse Y -> A`, repeat the pair as one dependency chain, and create same-domain/control pairs with equal instruction count where possible. Prevent memory spills.

**Derived measurement:** `round_trip_latency = net_cycles / pair_count`. Publish the pair and combined value. If an independently measurable inverse exists, a one-edge estimate may be shown as `round_trip - measured_inverse` with propagated uncertainty and an explicit “derived” label.

**Test plan:** Sweep scalar/vector width and exact opcodes (`movd`, `movq`, conversions, extracts/inserts). Collect uops/ports. Do not publish arbitrary half-round-trip numbers.

#### IL08. Operand-value sensitivity and assists

**Test:** For instructions suspected to be data-dependent, measure controlled operands: zero, one, powers of two, dense bit patterns, smallest/largest normal, subnormal, infinity, NaN, exact/non-exact division, and divisor/magnitude grids.

**Hypothesis:** Division, square root, exceptional FP, and some microcoded operations have distributions of cost rather than a single constant; random values can hide both best and worst cases.

**Measurement construction:** Pre-generate operand rings for each named class, keep class cardinality/address behavior equal, and run latency chains plus independent throughput streams. Add a normal-value control and count FP assists/microcode-delivery events where available.

**Derived measurement:** Publish median and tails of cycles/op by operand class, assist probability, uops/op, and the maximum/minimum class ratio. Separate deterministic class cost from a mixture's weighted mean.

**Test plan:** Validate operands bit-for-bit, include FTZ/DAZ state in the manifest, avoid exceptions changing control flow unless explicitly tested, and cover IL51, IL58, and IL59 with shared machinery.

#### IL09. Complete supported-instruction census and capability matrix

**Test:** Expand every row in Agner's list-driven integer, vector-integer, vector-FP, gather, string-vector, mask, AVX-512, and FP16 sources into a manifest of requested encodings and observed support.

**Hypothesis:** A generated census prevents attractive instructions from being cherry-picked and makes unsupported ISA families visible; only a subset is performance-valid on Haswell.

**Measurement construction:** Parse/pin the source lists, normalize each row to mnemonic/encoding/operands/test modes, run CPUID+XCR0+assembler capability checks before execution, then dispatch legal rows through IL03–IL08. Illegal rows never enter executable paths.

**Derived measurement:** Produce counts by `measured`, `unsupported_cpuid`, `unsupported_os_state`, `assembler_unavailable`, `unconstructable_dependency`, and `failed_validation`. For measured rows emit latency, throughput, memory-form, uop/port, and value-class results as applicable.

**Test plan:** On Haswell, expect AVX2/FMA and AVX2 gather coverage but no AVX-512, scatter, mask-register, or FP16 execution. Hash the source archive and generated manifest, diff revisions, and require every source row to end in an explicit status.

#### IL10. Mixed-instruction throughput and bottleneck composition

**Test:** Mix instructions from different and overlapping execution resources: integer ALU/multiply, branches, loads, stores, FP add/multiply/FMA, vector integer, and NOP/decode traffic.

**Hypothesis:** Isolated reciprocal throughput does not predict a real mixture by simple addition; complementary ports overlap, while shared ports or front-end bandwidth create nonlinear slowdowns.

**Measurement construction:** Generate fixed ratios 1:1, 2:1, 3:1, and representative kernel ratios with independent operands. Keep total instructions and code bytes controlled; compare ordered, alternating, and shuffled static schedules. Include single-family baselines.

**Derived measurement:** Compare observed cycles/iteration with a resource-bound prediction derived from isolated uops/ports. Report throughput, IPC, uops/cycle, port occupancy, front-end-bound fraction, and residual prediction error. Independently compute useful bytes—do not trust unverified script byte counts.

**Test plan:** L1 data only for core tests, then explicit L2/DRAM variants. Sweep body below/above uop-cache capacity and run solo, SMT sibling, and separate-core profiles to connect IL10 with IL33.

### 27.3 Instruction front end, decoders, loops, and uop cache

#### IL11. Legacy-decoder throughput ceiling

**Test:** Measure sustained delivery of simple single-uop instructions from the legacy decode path while keeping the back end non-limiting.

**Hypothesis:** Haswell reaches a finite MITE decode width, but instruction mix, alignment, and the complex-decoder slot change delivered uops/cycle; a tiny hot loop may instead come from the uop cache or loop mechanism.

**Measurement construction:** Generate straight-line bodies of independent register moves/ALU-neutral operations with one to four instructions per 16-byte region. Make bodies larger than the decoded-uop cache for the primary MITE condition, then execute a matched small-body control. Use branches only at the outer repetition boundary.

**Derived measurement:** `decoded_instructions_per_core_cycle = dynamic_instructions/net_cycles` and `delivered_uops/core_cycle`; corroborate source with MITE/DSB/LSD delivery events. A decoder ceiling is the plateau reached while backend-bound and bad-speculation fractions remain low.

**Test plan:** Sweep 1–8-byte encodings, 16/32/64-byte entry alignment, 1–6 instructions per decode block, and integer versus vector single-uop forms. Inspect all boundaries in disassembly and repeat with DSB deliberately warmed/evicted.

#### IL12. Complex-decoder and multi-uop instruction supply

**Test:** Compare simple instructions with instructions that decode to multiple uops or invoke the microcode sequencer, at controlled ratios per decode block.

**Hypothesis:** Throughput falls when multiple complex instructions compete for the decoder capable of handling them, and falls differently when the microcode sequencer rather than normal decode supplies uops.

**Measurement construction:** Place 0, 1, 2, and 4 complex instructions in repeated aligned decode regions, padding remaining bytes with known NOPs. Use independent operands and a back-end-complementary control. Test normal multi-uop and clearly microcoded representatives separately.

**Derived measurement:** Report instructions/cycle, delivered uops/cycle, MITE versus microcode-sequencer uops, and incremental cycles per complex instruction. The knee across complex-instruction density estimates the front-end constraint rather than latency.

**Test plan:** Use supported integer, stack/string, and vector candidates selected after IL05 uop validation. Sweep body size below/above DSB capacity so a cached body cannot disguise legacy decoder behavior.

#### IL13. Instruction length and NOP decoding

**Test:** Measure architecturally valid NOP encodings of 1–15 bytes and useful instructions padded to the same bytes/uops.

**Hypothesis:** Equal-uop bodies with longer encodings consume fetch bandwidth and interact with decode boundaries; multi-byte NOPs may be cheaper than many one-byte NOPs for alignment padding.

**Measurement construction:** Generate equal total byte counts and separately equal instruction counts for each NOP length. Repeat bodies from L1I-small to larger than DSB/L1I, align entry at every offset 0–15, and include a useful single-uop body with the same layout.

**Derived measurement:** Report bytes/cycle, instructions/cycle, uops/cycle, and cycles per padding byte under MITE-, DSB-, and capacity-stressed conditions. Equal-byte and equal-instruction views identify fetch versus decode limits.

**Test plan:** Verify exact bytes, prohibit assembler NOP rewriting by emitting encodings explicitly, and collect I-cache, ITLB, MITE, DSB, and fetch-bandwidth metrics in separate passes.

#### IL14. Prefix decode cost and prefix-budget limits

**Test:** Add redundant legacy prefixes and compare meaningful legacy, REX, VEX encodings of otherwise equivalent operations.

**Hypothesis:** Prefix count/combination can reduce decode throughput or trigger special handling even when execution semantics and uops are unchanged; VEX may avoid some legacy-state issues while increasing byte length.

**Measurement construction:** Emit exact bytes for 0–multiple legal prefixes, keep semantic output identical, use independent single-uop work, and control total code bytes with NOP padding in a second comparison. Separate redundant prefixes from operand-size/address-size changes.

**Derived measurement:** Report cycles/instruction versus prefix count, bytes/cycle, delivered uops/cycle, and front-end event deltas. The equal-byte control separates prefix recognition from raw fetch volume.

**Test plan:** Sweep instruction address modulo 16/32, hot DSB versus forced MITE, scalar/vector encodings, and supported VEX forms. EVEX rows are `unsupported_on_dut`; they are not replaced by VEX.

#### IL15. Length-changing-prefix stalls at decode boundaries

**Test:** Place instructions with length-changing prefixes at and across candidate 16-byte fetch/decode boundaries, compared with same-length no-LCP encodings and shifted placement.

**Hypothesis:** Haswell may pay a front-end penalty for length-changing prefixes, especially near boundaries, even when retired instruction/uop counts match.

**Measurement construction:** Construct identical loops differing only in LCP presence and start offset. Use repeated prefixed instructions separated enough to test isolated versus dense cases; use `.byte` emission and fixed padding to prevent assembler layout changes.

**Derived measurement:** `LCP_penalty = cycles_LCP - cycles_same_layout_control`, normalized per prefixed instruction. Confirm with front-end stall/LCP-related events where exposed and unchanged back-end port counts.

**Test plan:** Sweep offsets 0–31, prefix density, body size, DSB warm/cold source, and immediate/displacement lengths. Require the penalty pattern to follow placement across at least two regenerated alignments.

#### IL16. Instruction-boundary metadata and alternate-entry behavior

**Test:** Execute a byte region from one entry point, then jump into a different valid instruction boundary within overlapping bytes, versus a separately encoded non-overlapping control.

**Hypothesis:** Cached decode/boundary metadata can make alternate-entry execution slower or force re-decode/clear behavior; results may depend on whether the region resides in the DSB.

**Measurement construction:** Emit deliberately overlapping but architecturally valid instruction streams with two callable entries and verified expected checksums. Alternate entries in controlled sequences, flush/evict between phases when required, and compare with streams having the same decoded instructions but no overlapping bytes.

**Derived measurement:** Report cycles/entry, machine clears or recovery events, DSB misses, MITE delivery, and branch misses. The delta to the non-overlap control measures boundary-metadata/redecode cost, not useful operation cost.

**Test plan:** Entry offset, first-entry history, alternation frequency, alignment, and loop size are dimensions. Keep this test isolated because self-modifying or accidental invalid decoding would confound it; reject any checksum/fault discrepancy.

#### IL17. Loop-stream delivery eligibility and capacity

**Test:** Sweep tiny counted loops by uop count, instruction count, branch count, and instruction form to detect when loop delivery changes source or throughput.

**Hypothesis:** Eligible loops can avoid repeated legacy decoding, but a capacity/eligibility boundary causes a throughput knee; Haswell-specific LSD/DSB behavior must be observed rather than inferred from another CPU.

**Measurement construction:** Generate independent one-uop bodies of 1–128 uops with one backward branch, then variants with extra branches, multi-uop instructions, and alignment changes. Keep back-end resource demand deliberately below delivery capacity using complementary simple operations.

**Derived measurement:** Plot cycles/iteration and delivered uops by LSD/DSB/MITE source against loop uops. Define capacity as a repeatable change point supported by both timing and delivery-source counters, not the fastest isolated row.

**Test plan:** 16/32/64-byte alignment, warm and post-eviction state, solo and SMT sibling. Verify actual uop count with PMU and disassembly because instruction count is not uop count.

#### IL18. Decoded-uop-cache capacity and associativity

**Test:** Sweep hot code footprint and mapping pattern while holding dynamic useful uops per iteration constant to locate DSB capacity and conflict knees.

**Hypothesis:** Throughput stays on the decoded-uop delivery plateau until total or per-set pressure exceeds capacity/associativity, then MITE delivery and front-end stalls rise.

**Measurement construction:** Call or jump among generated blocks containing equal independent uops. Sweep number of blocks, block address separation, and placement bits; compare contiguous capacity pressure with conflict-focused layouts. Warm all blocks before the timed phase.

**Derived measurement:** Record cycles/uop, DSB-delivered fraction, DSB misses/switches, MITE fraction, and I-cache misses. A capacity/conflict threshold requires a timing knee plus delivery-source change without an L1I miss surge.

**Test plan:** Repeat code-layout randomizations and link addresses, archive section maps, and test one versus two SMT threads to reveal per-core sharing. Keep branch target predictability constant.

#### IL19. Instructions consuming extra decoded-uop-cache entries

**Test:** Compare loops with equal retired/decoded uop counts but different instruction types suspected of occupying more than one DSB entry.

**Hypothesis:** Some encodings exhaust decoded-uop-cache storage earlier than their uop count predicts, causing an earlier delivery-source knee.

**Measurement construction:** Build paired code families matched for total uops, bytes, branches, and back-end ports; vary only candidate instruction type. Sweep repeated blocks through the region around IL18's measured capacity.

**Derived measurement:** Estimate `effective_DSB_entries_per_instruction` from the ratio of capacity knee locations, with confidence across layouts. Support it with DSB miss/delivery changes and show raw bytes/uops to rule out simpler explanations.

**Test plan:** Validate candidates using the source script and IL05 uop counts, repeat at multiple alignments/set mappings, and label the entry ratio an empirical Haswell result rather than a universal encoding property.

#### IL20. Branch-misprediction recovery by front-end source

**Test:** Measure an otherwise identical predictable versus deliberately mispredicted branch when its target stream is resident in DSB, supplied by MITE, or stressed beyond front-end capacity.

**Hypothesis:** Recovery cost depends on how quickly the correct target can resume uop delivery, so one global “mispredict penalty” hides DSB/MITE and code-footprint effects.

**Measurement construction:** Use a data-independent precomputed outcome stream with known 0% and near-50% misprediction controls. Match taken rate and target work. Create small-hot, large-hot, and L1I/ITLB-stressed target bodies without changing branch semantics.

**Derived measurement:** `incremental_cycles_per_miss = (cycles_bad - cycles_good) / (branch_misses_bad - branch_misses_good)`, with propagated confidence. Also report recovery-related, DSB/MITE, and I-cache/ITLB events.

**Test plan:** Direct conditional branches first, then indirect IL24. Sweep target alignment/distance and ensure the denominator contains enough misses for a stable ratio; do not infer penalty from one event.

#### IL21. Loop size: instruction bytes versus uops versus branches

**Test:** Independently vary loop byte footprint, decoded uops, instruction count, and internal branch count using short/long encodings, immediates, displacements, NOP padding, and multi-uop instructions.

**Hypothesis:** Separate knees correspond to loop delivery, DSB storage, L1I capacity, ITLB reach, and branch-predictor resources; a single “loop size” axis conflates them.

**Measurement construction:** Use a factorial generator: equal-uop/different-byte, equal-byte/different-uop, and equal-both/different-branch bodies. Keep back-end work independent and matched. Sweep from 16 bytes through several MiB, with page placement recorded.

**Derived measurement:** Report cycles/useful-uop and delivery/cache/TLB counters against all four independent size axes. Fit change points only after showing raw curves; classify each knee by the counter that changes with it.

**Test plan:** Multiple link/layout seeds, 4-KiB and huge-page code mappings where permitted, hot and cold instruction state, and solo/SMT. This becomes the calibration for CC code-footprint tests.

#### IL22. Unconditional and taken-branch density

**Test:** Execute 0–many unconditional jumps or always-taken conditional branches per 16-byte/32-byte region, compared with equivalent fall-through/padded code.

**Hypothesis:** Dense taken branches hit branch-delivery/target-prediction limits before simple instruction decode capacity, while not-taken fall-through behaves differently.

**Measurement construction:** Generate chains of uniquely targeted jumps with controlled distance and alignment; equalize total code bytes and useful destination work. Include not-taken, always-taken, and alternating conditional variants with matched branch counts.

**Derived measurement:** Report branches/core-cycle, cycles/branch, branch misses, front-end-bound share, and delivered uops. The maximum stable correctly predicted taken branches/cycle is the delivery ceiling; exclude misprediction rows from that estimate.

**Test plan:** Sweep 1–8 branches per cache line, target distance, same/cross page, forward/backward, small DSB-resident and large MITE/I-cache bodies. Verify every target address from the linked binary.

### 27.4 Branch prediction, call/return, renaming, and out-of-order execution

#### IL23. Conditional-branch predictor pattern matrix

**Test:** Measure conditional branches driven by constant, alternating, periodic, nested-loop, correlated, pseudo-random, and adversarial bit sequences while holding taken ratio constant where possible.

**Hypothesis:** Predictor accuracy depends on learnable history/correlation, not taken percentage alone; two 50%-taken streams can have near-zero versus near-random miss rates.

**Measurement construction:** Store or generate a cyclic outcome trace outside the timed branch body, warm it for multiple periods, then execute one branch per outcome with identical work on both arms. Include branchless selection and a branch with randomly permuted trace as controls. Ensure data loads are L1-hot and not the bottleneck.

**Derived measurement:** Report branch misses/branch, cycles/element, and `incremental_cycles_per_miss` relative to the same-pattern branchless/control path. For periodic sequences, report miss rate versus period/history length; for nested loops, report misses per loop invocation.

**Test plan:** Taken ratios 0/10/50/90/100%, periods 2–4096, one versus many static branch PCs, consecutive versus nested loops, and cold-to-trained trajectories. Repeat with code-layout seeds to detect aliasing.

#### IL24. Indirect-branch target prediction and target-set size

**Test:** Call/jump indirectly among 1–256 targets using constant, alternating, round-robin, skewed, correlated, and random target sequences.

**Hypothesis:** Indirect prediction degrades with target entropy and target-set/PC aliasing; virtual dispatch cost therefore depends strongly on call-site and type distributions.

**Measurement construction:** Emit targets with equal useful work and return path, preload the target-index trace, and compare direct-call, monomorphic indirect, switch/jump-table, and function-pointer dispatch. Keep target addresses/layout explicit and randomize layouts across builds.

**Derived measurement:** Report indirect branch misses/call, cycles/call, p99 batch cost, and incremental cycles/miss over direct or correctly predicted indirect control. Plot against target entropy and count, not only count.

**Test plan:** One call site/many targets, many call sites/shared target set, alternating two-way, three-way, Zipfian, phase change, and random; small/large target bodies. Link findings to CC05–CC08 without merging semantic implementation costs into the predictor-only row.

#### IL25. Compare/test plus branch macro-fusion matrix

**Test:** Compare adjacent `cmp`/`test`/supported ALU-plus-conditional-branch pairs with unfused controls created by inserting a neutral instruction, separating blocks, reversing operand forms, or crossing boundaries.

**Hypothesis:** Fused pairs consume fewer front-end/retirement resources and improve branch-dense throughput, but fusion eligibility depends on opcode, condition, adjacency, and placement.

**Measurement construction:** Generate repeated pairs with predictable taken and not-taken outcomes, independent flags/data across pairs, and equal code-byte controls. Test `cmp`, `test`, `add`, `sub`, `and`, `or`, `xor` candidates only as architecturally valid; disassembly proves adjacency.

**Derived measurement:** Fusion is inferred from lower uops/pair plus higher pairs/cycle, not timing alone. Report cycles/pair, retired/issued uops/pair, branch delivery, and boundary-specific delta.

**Test plan:** Register/register, register/immediate, and memory compare forms; conditions; taken state; offsets across 16/32-byte boundaries; DSB/MITE conditions. Positive known-fused and known-unfused controls are mandatory.

#### IL26. Return-address-stack capacity and recovery

**Test:** Traverse generated nested call chains of depth 1–128 and compare matched direct-jump/manual-stack controls, then perturb returns with non-LIFO paths where safe.

**Hypothesis:** Returns are accurately predicted up to an implementation-dependent nesting capacity; beyond it, misses and cycles/return rise. Non-LIFO behavior disrupts prediction even at shallow depth.

**Measurement construction:** Generate distinct non-inlined functions with one call at each level and equal leaf work. Repeat the whole chain enough times, subtract loop/call setup using a shallow calibrated control, and prevent tail-call optimization. Record actual call/return addresses.

**Derived measurement:** Report cycles/call-return pair and return/indirect branch misses by depth. Define the RSB knee as the smallest repeatable depth with a miss-rate and timing increase across layouts.

**Test plan:** Sequential depth, recursive same-PC depth, alternating depths, exception/signal-free primary runs, and controlled LIFO violations. Run solo first; SMT and context-switch perturbation are sensitivity tests.

#### IL27. Stack-engine synchronization and stack instruction cost

**Test:** Compare `push`/`pop`, direct `[rsp]` loads/stores, `mov rsp` copies, `add/sub rsp`, `call`/`ret`, and `ret imm` sequences with equal data movement.

**Hypothesis:** Hardware stack tracking eliminates some address-update work, while instructions that explicitly read/modify `rsp` force synchronization uops or dependencies.

**Measurement construction:** Build balanced sequences that restore the exact initial stack pointer and never touch guard/red-zone-invalid memory. Create latency chains through the loaded value and throughput bodies with independent stack slots. Match memory bytes and architectural results using explicit load/store/add controls.

**Derived measurement:** Report cycles/operation, uops/operation, load/store-port occupancy, and incremental synchronization uops/cycles when an explicit `rsp` consumer is inserted. Never treat unsafe unmatched stack motion as a benchmark result.

**Test plan:** Single and burst push/pop, interleaved direct `[rsp]`, copy-to-GPR addressing, call/return, and alignment/cross-line stack locations. Verify stack restoration after every invocation.

#### IL28. Move elimination and rename-capacity limits

**Test:** Insert 0–256 register moves into a true dependency chain and compare eliminable GPR/XMM moves, same-register moves, non-eliminable forms, and known-zero sources.

**Hypothesis:** Renamed/eliminated moves add little or no execution latency/uops until rename resources or elimination eligibility are exhausted; unsupported forms consume ports and extend the chain.

**Measurement construction:** Anchor a repeated arithmetic dependency chain, place moves between producer and consumer, and ensure moved values are semantically required. In a separate throughput body, issue independent moves at increasing density. Match instruction count with neutral known-eliminated controls.

**Derived measurement:** Plot incremental cycles and issued/retired uops per inserted move. A move is empirically eliminated when the consumer chain latency does not grow and execution-port uops stay near control; the density knee estimates elimination/rename capacity.

**Test plan:** GPR 32/64-bit, XMM/AVX moves, same/different registers, chains/cycles, known-zero, and mixed moves. Sweep dependent chain length and DSB-resident/large bodies to separate rename from front-end limits.

#### IL29. Zero-idiom recognition and dependency breaking

**Test:** Compare `xor reg,reg`, `sub reg,reg`, vector XOR variants, and non-zeroing same/different-register controls before a consumer that otherwise depends on an old long-latency producer.

**Hypothesis:** Recognized zero idioms break the false input dependency and may be eliminated, whereas look-alike forms or partial-width uses retain a dependency and delay the consumer.

**Measurement construction:** Start a long dependent producer, insert the candidate zeroing instruction, then consume its result. Compare same-register zero idiom, different-register XOR/SUB, `mov 0`, and an explicit dependency-preserving operation. Repeat as a closed chain and as independent throughput streams.

**Derived measurement:** Dependency breaking is the reduction in producer-to-consumer critical-path cycles relative to the dependency-preserving control; elimination is additionally supported by uop/port counts. Report both separately.

**Test plan:** 32/64-bit GPR, legacy SSE and VEX XMM/YMM forms, consumer widths, flag consumers, and zero-known rename state. Include exact opcode bytes because encoding changes semantics and upper-lane state.

#### IL30. Partial-register, partial-flags, and vector-lane dependency penalties

**Test:** Measure narrow writes followed by wider reads (`AL/AH/AX/EAX/RAX`), partial flag producers followed by consumers of preserved flags, and scalar XMM writes followed by full-vector reads; include XMM↔YMM upper-lane cases.

**Hypothesis:** Some partial updates require merge uops or preserve dependencies on the prior full value, increasing latency and uops; 32-bit GPR writes and appropriate VEX zeroing forms act as controls that define the full destination.

**Measurement construction:** Produce a long dependency on the old full register, perform the partial write, then consume the full register/flags/vector. Pair with a full-defining write of equal semantic result and with no-old-dependency control. Serialize repetitions through the consumer.

**Derived measurement:** `merge_penalty = cycles_partial_chain - cycles_full_definition_chain` per iteration; also report incremental uops, ports, and stalls. A dependence is established only if removing the old producer removes the delay.

**Test plan:** Low/high byte, 16/32/64-bit, `inc/dec` versus `add/sub` flag consumers, `movss/movsd` plus packed operations, legacy/VEX XMM/YMM transitions, and boundary-independent code layouts.

#### IL31. Execution-domain crossing

**Test:** Compare operations whose bitwise semantics can execute in integer-vector versus FP-vector domains, plus explicit conversions/moves between GPR, vector integer, scalar FP, and x87.

**Hypothesis:** A consumer in a different execution domain can add bypass latency or port pressure even when registers and bit patterns are unchanged; Haswell behavior is instruction-pair-specific.

**Measurement construction:** Form closed alternating chains `A-domain producer -> B-domain consumer -> A-domain producer`, plus same-domain A/A and B/B chains with matched work. For explicit register-file transfers use IL07 round-trip reporting.

**Derived measurement:** Report combined cycles/pair, uops/ports, and `crossing_delta = alternating_pair - matched_same_domain_pair`. Do not divide the extra equally between directions unless each direction is independently identified.

**Test plan:** Logical XOR/AND, moves, shuffles, conversions, scalar/packed widths, and memory intermediates. Inspect for compiler-inserted moves and separate legacy SSE/AVX-state penalties into IL49.

#### IL32. Out-of-order overlap/window-capacity proxy

**Test:** Interleave two or more independent long-latency dependency chains and sweep the number of in-flight chain operations/instructions separating producer and consumer.

**Hypothesis:** The core overlaps independent chains until scheduler, reorder, physical-register, load-buffer, or another finite resource fills; beyond that point overlap collapses or plateaus.

**Measurement construction:** Use calibrated integer multiply, FP multiply/add, and L1-load chains separately. Compare one chain, two interleaved equal chains, and N chains; lengthen each chain without changing per-op dependencies. Create a front-end-light body and verify no cache misses for core-only rows.

**Derived measurement:** `overlap_efficiency = single_chain_cycles / (N * interleaved_cycles_per_chain_work)` under an explicitly stated normalization. Plot total cycles versus in-flight dependent operations; the knee is an empirical resource-capacity proxy, not automatically “ROB size.”

**Test plan:** Integer, FP, mixed, L1 loads, and store-buffer extensions; uop counts 16–512; 1–16 independent chains. Use PMU backend/core-bound, stalls, and relevant buffer-full events to refine—but not overclaim—the limiting structure.

#### IL33. SMT shared-resource contention fingerprint

**Test:** Run a victim microkernel on CPU 4 alone and concurrently with controlled stressors on sibling CPU 5; compare an identical stressor on separate physical CPU 6.

**Hypothesis:** Sibling-only slowdown identifies shared-core resources: fetch/decode/uop delivery, execution ports, branch machinery, and L1/load-store structures; separate-core slowdown primarily reflects shared LLC/memory/system effects.

**Measurement construction:** Victims and stressors cover long NOP/fetch, short NOP/decode, integer add/multiply/divide, branches, x87, 128/256-bit integer and FP add/multiply/divide, loads, stores, and read-modify-write. Synchronize start/end, pin both tasks, and give each independent data unless testing cache sharing.

**Derived measurement:** `sibling_slowdown = T_victim+sibling / T_victim_solo`; `core_shared_component = sibling_slowdown / separate_core_slowdown` with confidence. Report both tasks' throughput, effective frequency, port/front-end metrics, and total work so throttling is not mistaken for contention.

**Test plan:** Full victim×stressor matrix after a sparse screen, both thread assignments, equal-duration epochs, fixed-frequency and turbo sensitivity. Label CPUs 4+5 one physical core and CPUs 4+6 two physical cores.

### 27.5 Cache, addressing, load/store, and memory-order instructions

#### IL34. Serialized load-use latency by cache/TLB tier

**Test:** Pointer-chase a randomized single-cycle permutation over working sets bracketing L1, L2, LLC, DRAM, dTLB reach, and page-walk regimes.

**Hypothesis:** A true dependent chase exposes load-use latency and discrete cache/TLB knees; sequential or multiple independent loads instead measure prefetching and memory-level parallelism.

**Measurement construction:** Store exactly one pointer/index per selected cache line, shuffle into one closed cycle, prefault it, and update the next address solely from the just-loaded value. Use an equal arithmetic/loop dependency control without memory. For TLB isolation, keep lines-per-page fixed while sweeping pages.

**Derived measurement:** `load_use_latency = (chase_cycles - control_cycles) / dereferences`; report distribution over separately generated permutations, cache/TLB miss rates, page walks, and effective frequency. Use change points only as measured tier transitions.

**Test plan:** Working-set ladder from 4 KiB to 512 MiB, strides 64 B/page/huge-page, 4-KiB versus THP-madvise, warm versus explicit eviction, five permutations. Disable software prefetch in the latency body and verify one retired load per dereference.

#### IL35. Cache read/write throughput by footprint and stride

**Test:** Measure sustained read, write, read+write, two-read+write, three-read+write, and copy/update streams across the cache ladder.

**Hypothesis:** L1 is limited by load/store ports, outer tiers by transfer bandwidth and misses; writes differ under write-allocate, eviction, and dirty-line pressure. Strides alter prefetch coverage and effective traffic.

**Measurement construction:** Use many independent addresses, enough unrolling to expose memory-level parallelism, fixed useful bytes, and separate accumulator/checksum. Prefault and initialize outside timing. For stores, distinguish overwrite, read-modify-write, and streaming allocation semantics.

**Derived measurement:** Report useful bytes/s and operations/cycle plus estimated/requested traffic, cache misses, memory-bound fraction, load/store port usage, and writeback/device-independent status. Do not call useful-store bytes actual bus traffic.

**Test plan:** Scalar/128/256-bit widths; 64-B sequential, powers-of-two, prime, and random strides; L1/L2/LLC/DRAM sizes; read/write ratios; 1/2/SMT threads. Randomize arrays/addresses between sessions to expose set conflicts.

#### IL36. L1 load-port, store-data, and address-generation ceilings

**Test:** Run independent L1-hot operations with ratios R, W, RW, RRW, RRRW, and RWW using simple and complex addressing.

**Hypothesis:** Haswell sustains different maxima for load data, store data/address, and AGU work; read/write mixes expose which resource saturates more clearly than isolated loads or stores.

**Measurement construction:** Keep a small array L1-resident, use independent cache lines/offsets and enough registers, unroll beyond latency, and wrap safely. Generate base+displacement, base+index, scaled-index, and pointer-increment versions with identical bytes. Accumulate loads and verify stores afterward.

**Derived measurement:** Report loads/cycle, stores/cycle, total memory uops/cycle, useful B/cycle, per-port uops, and cycles/iteration. The ceiling is the stable maximum before front-end or dependency metrics rise; mixture equations identify load/store/AGU constraints.

**Test plan:** 4/8/16/32-byte operands, aligned and contained, 1–16 independent streams, below/above DSB size. Include a register-only loop to subtract pointer/control work.

#### IL37. L1 bank/address conflict and false-dependency probe

**Test:** Access two independent L1-hot addresses at separations covering low address bits, cache sets, and 4-KiB aliases; compare a minimally perturbed second address.

**Hypothesis:** Particular address-bit relationships may reduce parallel load/store issue or create false dependencies; a one-byte/cache-line perturbation breaks the relationship without changing working-set size.

**Measurement construction:** Issue paired independent reads, read/write, and store/load operations repeatedly with fixed base addresses. Sweep `delta1`; for each, compare `delta1` with `delta1 + delta2` controls. Allocate page-aligned buffers and record virtual and physical information only where safely observable.

**Derived measurement:** `conflict_penalty(delta) = cycles_pair(delta) - cycles_pair(perturbed_control)`, corroborated by port/stall/alias events. Report a heat map over address-bit separations; no penalty is a valid Haswell result.

**Test plan:** Byte through multi-page separations, both operand orderings, load/load, load/store, store/load, and different widths. Keep both lines L1-hot and avoid actual cache-set eviction unless it is a separately labeled subtest.

#### IL38. Unaligned, split-line, and split-page memory operations

**Test:** Sweep each load/store/round-trip operation's byte offset across 64-byte cache-line and 4-KiB page boundaries for scalar, 128-bit, and 256-bit widths.

**Hypothesis:** Contained unaligned access is often cheap, while split-line and especially split-page operations add uops/latency; store-to-load overlap has an additional forwarding compatibility cost.

**Measurement construction:** For throughput use independent L1 addresses at every offset. For load latency use a dependency chain through loaded indices/values. For store→load use IL39's dependent consumer. Compare each offset with an aligned address in the same allocation and cache state.

**Derived measurement:** Report cycles/op, uops/op, ports, split-load/store events, and `boundary_penalty = offset_result - aligned_control`. Separate contained-unaligned, split-line, and split-page classifications.

**Test plan:** Widths 1–32 bytes; offsets around 16/32/64-byte and page boundaries; L1 and outer-cache states; reads, stores, and store→load. Map both pages before timing and reject page faults.

#### IL39. Store-to-load forwarding compatibility matrix

**Test:** Cross product store width, load width, relative offset, boundary crossing, address expression, and number/order of stores before the dependent load.

**Hypothesis:** Exact compatible contained overlaps forward quickly; partial overlap, mismatched boundaries, or reconstructed wide loads can fail/slow forwarding and add a large dependency-chain penalty.

**Measurement construction:** In each iteration store a value whose next iteration/address or accumulator depends on the subsequent load, forcing store→load→consumer serialization. Pair every case with an exact-width/alignment same-address forwarding control and a no-overlap load control. Keep all data L1-hot.

**Derived measurement:** Report combined store→load→consumer cycles, `forwarding_penalty` versus compatible control, uops, and store-forward-block events. Call it forwarding latency/penalty, never standalone store latency.

**Test plan:** Store/load widths 1/2/4/8/16/32 bytes, every meaningful offset, split-line cases, narrow-after-wide, wide-after-multiple-narrow stores, two split reads, base/index versus changed address computation, and both vector/integer loads.

#### IL40. Same-address “memory mirror” portability probe

**Test:** Reproduce same-address and closely aliased read/write patterns from the source's architecture-specific memory-mirroring test, including alternate addressing modes, stack forms, and read-modify-write cases.

**Hypothesis:** The originally targeted effect may be absent on Haswell; any observed benefit or penalty must survive matched forwarding, cache, and address-generation controls before being generalized.

**Measurement construction:** Generate repeated same-address store/load/RMW sequences and controls using distinct addresses with identical cache-set/offset placement. Keep dependency direction explicit, compare address expressions, and run with all data L1-hot.

**Derived measurement:** Report cycles/op, forwarding blocks, uops/ports, and same-minus-distinct-address delta. Classify `effect_not_detected` when confidence includes the materiality bound; do not reinterpret a null result as failed infrastructure.

**Test plan:** Reproduce legal source cases, then isolate store forwarding, 4-KiB alias, stack engine, and AGU explanations with IL27/IL37/IL39 controls. Mark the result CPU-specific.

#### IL41. Address-generation mode and AGU cost

**Test:** Compare base, base+displacement, base+index, base+scaled-index+displacement, pointer bump, LEA-precomputed address, and RIP-relative memory operands under matched loads/stores.

**Hypothesis:** Complex addressing can shift work among Haswell AGUs/ALU ports and reduce load/store throughput even though Big-O and bytes are identical; precomputation may help only when its extra ALU uops fit spare ports.

**Measurement construction:** Use L1-resident independent streams and equal address sequences. Generate both fused memory operations and explicit LEA+simple-address forms. For latency, make the next address depend on the computed address; for throughput, use independent bases.

**Derived measurement:** Report cycles/memory-op, uops, AGU/load/store/ALU port occupancy, and code bytes. Compare explicit-address and fused-address variants at equal useful bytes and include pointer-update work.

**Test plan:** Loads, stores, RMW, scalar/vector widths, 1–3 memory operands per iteration, and increasing stream counts. Repeat below/above DSB capacity to ensure larger encodings are not misdiagnosed as AGU cost.

#### IL42. Load-op fusion versus split scheduling under pressure

**Test:** Compare memory-source arithmetic/logical instructions with explicit load+operation while independently stressing load ports, candidate execution ports, or register pressure.

**Hypothesis:** The fused source form saves code/front-end resources, while split form can schedule/load earlier; the winner changes with load-port, ALU/FP-port, and register constraints.

**Measurement construction:** Use equal computations on independent array elements. Add calibrated stress instructions targeting load ports or the arithmetic instruction's ports without changing memory bytes. Spill-free split variants are required; a separate forced-register-pressure arm measures spill crossover explicitly.

**Derived measurement:** Report cycles/element, decoded/issued/retired uops, ports, code bytes, and spill loads/stores. Attribute a difference only after confirming equal cache misses and outputs.

**Test plan:** Integer add/compare, scalar FP, AVX2 add/multiply/FMA memory forms, simple/complex addresses, L1/L2 footprints, and 1–16 independent streams.

#### IL43. Copy-loop implementation and size crossover

**Test:** Compare libc `memcpy`, compiler builtins, REP MOVSB/string forms, scalar unrolled, SSE/AVX2 temporal, and AVX2 non-temporal copies over a complete size ladder.

**Hypothesis:** Startup, overlap rules, alignment, cache residency, ERMS/string behavior, vector width, write allocation, and destination reuse create multiple crossovers; one bandwidth number is not a copy characterization.

**Measurement construction:** Use non-overlapping buffers for `memcpy` semantics, randomize source/destination pairs, initialize/check outside timing, and select hot-source/hot-destination, cold-source, and cold-destination states deliberately. Include function-call overhead only in an end-to-end row and subtract it in kernel rows.

**Derived measurement:** Report ns/call, cycles/byte, useful GiB/s, cache-line traffic proxies, uops/byte, and break-even sizes. For very small sizes report exact length, not rounded bandwidth only.

**Test plan:** Length 0–256 every byte, powers/boundaries through 1 GiB, fixed and runtime-known lengths, source/destination reuse, 1/2 threads. Record libc/compiler versions and resolved implementation/IFUNC.

#### IL44. Source/destination alignment matrix for copy

**Test:** For each IL43 implementation, sweep source and destination offsets independently across at least 0–63 bytes, including equal and unequal misalignment.

**Hypothesis:** Relative alignment can matter more than either absolute alignment because loads/stores split at different boundaries and forwarding/vector prologues change.

**Measurement construction:** Allocate page-aligned buffers with guards, run every `(src_offset,dst_offset)` pair for representative lengths, and keep non-overlap/cache state identical. Randomize matrix order and use multiple base pages to avoid fixed cache-set artifacts.

**Derived measurement:** Produce cycles/byte and bandwidth heat maps, plus worst/best/median ratios. Explain cells using split-load/store events and prologue/epilogue disassembly; do not average offsets before inspecting the surface.

**Test plan:** Lengths below vector width, 32/64/128/4,096-byte boundaries, L1/L2/LLC/DRAM sizes, temporal and non-temporal methods. Verify exact copied bytes and red zones after every invocation.

#### IL45. Temporal versus non-temporal load/store crossover

**Test:** Compare ordinary cached stores/loads/copies with non-temporal stores and supported streaming-load forms across size, reuse, read-for-ownership, and fencing policy.

**Hypothesis:** Non-temporal stores win large one-pass writes by avoiding cache pollution/write allocation, but lose for small or soon-reused data and require completion fences whose cost must be included when visibility is required.

**Measurement construction:** Use identical useful data transformations; distinguish enqueue/issue completion from `sfence`-completed visibility. After each run, probe a fixed hot victim to measure pollution and optionally reread destination after controlled delay. Do not mix cold-state preparation into timed work.

**Derived measurement:** Report issue and fenced cycles/byte, useful bandwidth, destination reread latency, victim-cache degradation, and store/memory traffic proxies. Define crossover separately for no-reuse, immediate-reuse, and durability-inapplicable memory semantics.

**Test plan:** 4 KiB–1 GiB, alignment, write-only/copy/update, one/two cores, fence per chunk/end, reuse distances, and cache-footprint tiers. Confirm emitted NT opcodes.

#### IL46. Software/hardware prefetch effectiveness and pollution

**Test:** Compare no software prefetch with `prefetchnta`, `prefetcht0/t1/t2`, and compiler intrinsics across streams, strides, random chains with lookahead, and compute per element.

**Hypothesis:** Prefetch helps only when lead distance covers memory latency and the access is predictable without exhausting bandwidth/cache/MSHR resources; it can slow hot/small/random work through instruction and cache pollution.

**Measurement construction:** Sweep prefetch distance in cache lines/iterations while keeping demand accesses identical. Include a hardware-prefetch-friendly sequential control, a pointer chain whose future addresses cannot be known, and a batched pointer structure where lookahead is legal. Probe unrelated victim data for pollution.

**Derived measurement:** Report cycles/element, useful bandwidth, demand miss latency/count, prefetch request/use proxies where reliable, front-end uops, and victim slowdown. The winning distance must persist across adjacent sizes and include prefetch instruction overhead.

**Test plan:** L2/LLC/DRAM footprints, 1–16 streams, sequential/fixed/prime/random strides, read/write, scalar/AVX2 consumers, one/two threads. Do not call a warmed-cache effect successful DRAM prefetching.

#### IL47. Fence, serialization, and cache-control instruction cost

**Test:** Measure `lfence`, `sfence`, `mfence`, locked-serializing idioms, `cpuid`, `rdtsc/rdtscp`, and supported cache-line flush/control instructions under empty and outstanding-memory conditions.

**Hypothesis:** Fence cost is workload-state dependent: an empty-pipeline reciprocal cost differs from draining preceding loads/stores/cache flushes; serialization strength and semantics cannot be ranked by cycles alone.

**Measurement construction:** For empty cost, repeat the instruction with a matched loop. For drain cost, precede each fence with N independent loads/stores/NT stores/flushes to distinct lines, then make a post-fence timestamp or dependent observation. Use exact architectural ordering semantics and avoid substituting weaker fences.

**Derived measurement:** Report base cycles/fence and marginal drain cycles versus outstanding operations/bytes, plus uops and memory stalls. Separate issue latency, completion latency, and ordered-visibility contract.

**Test plan:** N=0–store-buffer/cache-scale, L1/LLC/DRAM lines, hot/cold, fence per op/batch, solo/SMT. Verify available flush opcodes by CPUID and never execute unsupported encodings.

#### IL48. Locked instruction and atomic memory-operation cost by placement

**Test:** Measure `lock`-prefixed add/exchange/CAS and implicit-lock exchange on private, shared-read, contended, same-line, false-shared, cross-line, and page-boundary operands.

**Hypothesis:** Uncontended locked instructions have a coherence/ordering floor; ownership transfer, retries, line split, and SMT topology dominate contended cases. A split-lock case may trap or be prohibitively slow and must be capability/safety-gated.

**Measurement construction:** Single-thread dependency and independent-address bodies establish local latency/throughput. Ping-pong and all-writer bodies on CPUs 4+6 versus 4+5 establish ownership/contention cost. Use matched plain load/store and non-atomic RMW controls only for attribution, never semantic ranking.

**Derived measurement:** Report cycles/atomic, successful operations/s, CAS failure/retry count, p50–p99.9 latency, core/system CPU, and coherence/cache metrics. Normalize completed semantic operations, not attempted instructions.

**Test plan:** Operand sizes, memory orders at language layer, aligned/false-shared/separate lines, private/sibling/separate-core. Probe split-lock policy without first executing a dangerous case; run only if explicitly safe and separately authorized.

### 27.6 SIMD, floating point, division, strings, and special instructions

#### IL49. Legacy SSE/AVX upper-state transition and `vzeroupper`

**Test:** Transition among clean upper state, 256-bit AVX-modified state, legacy SSE code, 128-bit VEX code, and explicit `vzeroupper` at controlled boundaries.

**Hypothesis:** Legacy SSE after dirty AVX upper lanes can incur a transition penalty; placing `vzeroupper` at the interface removes it at its own small cost, while all-VEX 128-bit code avoids the legacy transition.

**Measurement construction:** Generate repeated phase pairs: SSE→AVX, AVX→legacy SSE, AVX→`vzeroupper`→SSE, AVX→VEX.128, and read-only 256-bit cases. Use equal arithmetic and dependencies, vary operations per phase, and keep function boundaries/inlining explicit.

**Derived measurement:** Report cycles/transition, calculated as excess phase-pair cycles over same-domain controls divided by actual transitions; record uops, assists/machine clears if exposed, and frequency. Separately report steady-state cycles/op.

**Test plan:** Phase length 1–1,024 operations, call boundary versus inline, GCC/Clang/assembly, integer/FP vector bodies, and ABI-facing leaf calls. Verify exact legacy versus VEX encodings in disassembly.

#### IL50. FP/SIMD first-use warm-up and idle-gap response

**Test:** Measure the first burst of x87, scalar SSE, 128-bit vector, and 256-bit AVX arithmetic after controlled idle gaps, preceded by integer-only work.

**Hypothesis:** Some processors show execution-unit power/wake or frequency-state transients; the Haswell DUT may show no material first-use effect, which is an expected-negative outcome.

**Measurement construction:** After idle gaps of 0 µs–1 s, timestamp short consecutive blocks of 1, 2, 4, …, 1,024 operations without averaging the first block into steady state. Compare integer-only, no-idle, and already-warm controls; log effective frequency at the finest reliable granularity.

**Derived measurement:** Report per-block cycles/op and `first_block_excess = first_block - steady_state_block`, with confidence across trials. Separate wake-up-like transients from AVX frequency or scheduler interruptions using frequency/counter logs.

**Test plan:** FP add/multiply/divide, x87, scalar, SSE, AVX2, and FMA; multiple idle mechanisms outside timed blocks; solo only initially. Publish `not_detected_within_resolution` if appropriate.

#### IL51. Normal, subnormal, zero, infinity, NaN, underflow, and overflow matrix

**Test:** Measure scalar/vector add, multiply, divide, square root, conversion, and FMA for every relevant input/result class: normal, signed zero, subnormal input, subnormal result, underflow, overflow, infinity, quiet NaN, and signaling NaN where safely controlled.

**Hypothesis:** Exceptional values can trigger assists or different pipelines and produce large latency/throughput penalties; FTZ/DAZ changes both semantics and cost.

**Measurement construction:** Create bit-exact operands outside timing and verify output bits/exception flags afterward. Use dependency chains for latency and independent lanes/streams for throughput. Compare MXCSR default, FTZ, DAZ, and FTZ+DAZ while keeping exception masks explicit.

**Derived measurement:** Report cycles/op, reciprocal throughput, assist events/op, exception flags, and result classification. `exception_penalty = class_cycles - matched_normal_cycles`; semantic result changes under FTZ/DAZ are displayed beside performance.

**Test plan:** FP32/FP64, scalar/128/256-bit, operation×input-class×result-class, exact boundary values and multiple payloads. Clear/read flags outside timed bodies and reject unintended traps.

#### IL52. Rounding mode, exception-mask, FTZ, and DAZ state cost

**Test:** Compare arithmetic/conversions under all IEEE rounding modes and FTZ/DAZ settings, and measure the direct cost of changing MXCSR/x87 control state at different batch sizes.

**Hypothesis:** Steady normal arithmetic is often insensitive to rounding mode, but conversions/boundaries differ; frequent control-state changes serialize or add overhead that batching amortizes.

**Measurement construction:** Run identical bit-exact operand sets under round-to-nearest, down, up, and toward-zero. Separately time save/modify/restore around batches of 1–1,024 operations, with a no-change save/restore control. Verify both numeric result and flags.

**Derived measurement:** Report cycles/op, result error/bit pattern, and `state_change_overhead = changed_batch - unchanged_batch`; compute break-even batch size. Never compare different numeric semantics without labeling them.

**Test plan:** Scalar/vector add/multiply/FMA, float↔int conversion, values near halfway/overflow/subnormal boundaries, MXCSR and x87 control paths. Archive control words per run.

#### IL53. FMA latency, throughput, dependency form, and contraction

**Test:** Compare explicit FMA forms and operand-dependency choices with separate multiply+add, and compiler-contracted versus contraction-disabled source.

**Hypothesis:** FMA reduces instruction count and rounding while using specific ports; latency depends on which architectural operand is the accumulator, and throughput gains may vanish under port or dependency pressure.

**Measurement construction:** Build chains with each legal FMA operand position carrying the dependency, plus independent streams for throughput. Compare equal mathematical expressions using FMA and MUL+ADD; retain a bit-exact/accuracy comparison because results may round differently.

**Derived measurement:** Report cycles/result, latency by dependent operand, reciprocal throughput, uops/ports, FLOPs/cycle under the stated counting convention, code bytes, and numerical error. Compiler rows record whether contraction actually occurred.

**Test plan:** Scalar/128/256-bit FP32/FP64, all supported FMA3 operand orders, register and L1 memory source, 1–16 chains, GCC/Clang/intrinsics/assembly. Test mixed FMA+load and FMA+shuffle ratios.

#### IL54. Shuffle, permute, cross-lane, broadcast, extract, and insert matrix

**Test:** Measure all supported SSE/AVX2 rearrangement classes by latency/throughput: within-lane shuffle, cross-128-bit-lane permute, variable permute, unpack, blend, broadcast, insert, extract, and scalar↔vector transfers.

**Hypothesis:** Equal-width “shuffle” operations differ sharply by lane topology, control source, and destination domain; cross-lane and variable-index forms often cost more or occupy scarcer ports.

**Measurement construction:** For latency, feed permuted output into the next permutation using a pattern that preserves a nontrivial dependency; for throughput, use independent vectors and controls. Validate the permutation after timing and keep index vectors resident for variable forms.

**Derived measurement:** Report cycles/instruction, uops/ports, useful elements rearranged/cycle, and lane-crossing delta relative to within-lane control. For extract/insert pairs use combined round-trip values when needed.

**Test plan:** 128/256-bit, byte/word/dword/qword/FP elements, immediate and variable controls, identity/reverse/rotate/random patterns, register/memory forms. Separate data movement from later algorithm speedup.

#### IL55. AVX2 gather latency, throughput, mask, and locality surface

**Test:** Measure AVX2 gather for contiguous, fixed-stride, random, same-address, partially overlapping, and cache-tier-varied index vectors; compare scalar loads and manual lane assembly.

**Hypothesis:** Gather cost is governed by active lanes, cache-line/page count, cache tier, and dependency; contiguous data may favor ordinary vector loads, while sparse batched accesses may amortize gather setup.

**Measurement construction:** For latency, make gathered data generate the next index vector or pair gather with a dependent store/load round trip. For throughput, use independent index/data vectors. Sweep mask active-lane count and compare the same exact addresses in scalar code.

**Derived measurement:** Report cycles/gather, cycles/active element, useful bytes/cycle, cache lines/pages touched, uops/ports, cache/TLB misses, and speedup over scalar. Do not count masked-off lanes as completed work.

**Test plan:** 1–all active lanes, FP32/FP64/dword/qword legal forms, L1/L2/LLC/DRAM, same/contiguous/stride/random indices, sorted versus unsorted batch. Scatter and AVX-512 gather rows remain unsupported on Haswell.

#### IL56. Integer-vector pack, unpack, saturation, min/max, compare, and horizontal reduction

**Test:** Measure supported SSE/AVX2 packing/unpacking, narrowing/saturating arithmetic, min/max, comparisons, horizontal operations, and lane-reduction trees against scalar and alternate-vector sequences.

**Hypothesis:** Packed primitives give high lane throughput, but cross-lane reduction, packing, saturation semantics, and tail handling can dominate an end-to-end algorithm.

**Measurement construction:** Use dependency chains for primitive latency, independent vectors for throughput, and complete kernels that load, transform, handle tails, and materialize output. Feed adversarial values at saturation and signedness boundaries.

**Derived measurement:** Report cycles/vector, valid elements/cycle, uops/ports, lane utilization, tail fraction, and complete-kernel cycles/element. Verify exact saturation/comparison semantics for every lane.

**Test plan:** 8/16/32/64-bit elements where supported, signed/unsigned, 128/256-bit, full/partial blocks, aligned/unaligned memory, reduction sizes. Include shuffle-tree alternatives from IL54.

#### IL57. Integer/FP conversion, rounding, and register-transfer round trips

**Test:** Measure signed/unsigned integer↔FP conversions, FP-width conversions, scalar/vector forms, truncating/rounding variants, and GPR↔XMM transfers.

**Hypothesis:** Conversion latency and throughput depend on direction, width, signedness, rounding, and register domain; unsupported wide unsigned conversions may need multi-instruction algorithms whose full cost matters.

**Measurement construction:** Where possible, chain conversions back to the original type and report a round trip; otherwise pair with a separately calibrated inverse or validate throughput only. Use exactly representable values plus boundary/overflow/NaN cases and prevent constant folding.

**Derived measurement:** Report combined round-trip cycles, derived one-way value only when justified, reciprocal throughput, uops/ports, and numeric result/exception behavior. Multi-instruction emulation reports whole-sequence cost.

**Test plan:** Scalar/packed FP32/FP64, 32/64-bit integers, 128/256-bit, all rounding modes, register/memory forms, in-range/boundary/out-of-range. Distinguish instruction primitive from language cast semantics.

#### IL58. Integer multiply/divide and variable-count operation surface

**Test:** Measure integer multiply variants, signed/unsigned divide/remainder, high-half multiply, variable shifts/rotates, bit extract/deposit, and count-dependent instructions over controlled operands.

**Hypothesis:** Multiply is usually fixed enough for a table value; divide and selected complex/variable operations may depend on operand magnitude/pattern and monopolize scarce resources.

**Measurement construction:** Chain result into the next legal dividend/count for latency without degenerating to constants; use precomputed independent operand rings for throughput. Include trivial divisors, powers of two, near-equal, maximum quotient, non-round values, and signed edge cases without divide traps.

**Derived measurement:** Publish cycles/op distributions and surfaces by operand class, throughput at 1–N streams, uops/microcode/ports, and compiler strength-reduction comparison. Trapping overflow/divide-by-zero is a separate correctness path, not timed in-process.

**Test plan:** 8/16/32/64-bit legal forms, `imul` forms, `mulx`, `div/idiv`, BMI/BMI2 shifts/extract/deposit supported by Haswell, immediate versus variable counts. Verify quotient/remainder and exact dynamic instructions.

#### IL59. FP divide, square root, reciprocal, and refinement surface

**Test:** Compare precise divide/square root with approximate reciprocal/reciprocal-square-root plus zero/one/two refinement steps, across value classes and vector widths.

**Hypothesis:** Precise operations have higher latency/lower throughput and possible operand sensitivity; approximate+Newton methods can win packed throughput at a declared error budget but lose scalar/small or exceptional cases.

**Measurement construction:** Use closed dependency chains and independent streams. Generate normal operands spanning exponent/mantissa, exact and non-exact ratios, near-one, extremes, subnormals, zero, infinity, and NaN. Keep iteration/refinement count explicit and prevent reassociation.

**Derived measurement:** Report latency/throughput distributions, uops/ports/assists, relative/ULP error, exceptional-result behavior, and correct-results-per-cycle at each quality contract. Never rank approximate and precise results without the accuracy column.

**Test plan:** Scalar/128/256 FP32/FP64 supported forms, default versus FTZ/DAZ, divisor grids, mixed-operation kernels, and compiler reciprocal transformations under explicit flags.

#### IL60. String-instruction setup, startup, and length crossover

**Test:** Measure REP MOVS/STOS/CMPS/SCAS and applicable string/vector-string operations against scalar, libc, and AVX2 implementations over length, alignment, match position, and direction.

**Hypothesis:** String instructions have setup/startup costs and hardware fast paths that cross over with unrolled/vector code; compare/scan behavior depends on early-exit position and data distribution.

**Measurement construction:** Set registers outside or inside timing as separate kernel/end-to-end rows. Use exact byte counts and explicit direction flag state. For compare/scan, generate match at first/middle/last/absent and materialize the result.

**Derived measurement:** Report fixed startup intercept, cycles/byte or element after the slope stabilizes, branch/assist/uop behavior, and crossover lengths. For early exit normalize both per call and per examined byte.

**Test plan:** Length 0–256 bytewise then powers to DRAM, alignments, forward/backward where legal, hot/cold states, runtime versus compile-time sizes. Connect REP MOVS copy results with IL43 rather than double-counting them.

#### IL61. x87 versus scalar SSE/AVX arithmetic and stack effects

**Test:** Compare semantically matched x87 and scalar SSE/AVX add, multiply, divide, square root, conversion, and short expression chains.

**Hypothesis:** x87's stack/register model, precision, and instruction behavior differ from SSE/AVX; apparent speed or accuracy differences can result from extended precision and spill/control semantics, not merely opcode cost.

**Measurement construction:** Use pure-register chains within each domain, then force equivalent store-rounding when numerical semantics must match. Include x87 stack depth/rotation and spill-free controls. Do not mix domains except in IL07/IL31 rows.

**Derived measurement:** Report cycles/op, uops/ports, code bytes, spill count, and numeric error/bit pattern under both native and forced-equivalent precision. State the precision contract beside timing.

**Test plan:** FP32/FP64 inputs, x87 extended intermediates, chain depth, normal/exceptional values, and representative polynomial expressions. Treat this primarily as legacy/compiler and numerical-semantics characterization.

#### IL62. Bit-manipulation, population-count, CRC, AES, carry-less multiply, and state instructions

**Test:** Measure Haswell-supported POPCNT, LZCNT/TZCNT, BMI1/BMI2, CRC32, AES-NI, PCLMULQDQ, byte swap, flag-transfer, and selected save/restore/state-management instructions.

**Hypothesis:** Specialized instructions greatly reduce instruction count but differ in latency, port use, dependency behavior, setup, and value sensitivity; state save/restore has memory and microcode costs unlike register ALU primitives.

**Measurement construction:** Use IL03/IL04 chains/streams for pure operations, IL07 round trips for flag/register transfers, and properly sized/aligned buffers plus required fencing for state operations. Compare equivalent portable bit-twiddling or table/software sequences with identical outputs.

**Derived measurement:** Report latency, reciprocal throughput, uops/ports, bytes/cycle for state/memory forms, code size, and end-to-end useful units such as bytes checksummed/encrypted. Separate primitive cost from key schedule/table/setup.

**Test plan:** Operand widths and immediate variants, representative and adversarial bit patterns, 1–16 independent streams, L1/outer memory for state forms. Gate every feature by CPUID/XCR0 and test cryptographic correctness vectors outside timing.

### 27.7 Composite validation kernels

#### IL63. DAXPY roofline and instruction-table cross-check

**Test:** Run `y[i] = a*x[i] + y[i]` using scalar MUL+ADD, scalar FMA, SSE, AVX2 MUL+ADD, AVX2 FMA, compiler auto-vectorization, and pinned BLAS AXPY where semantics match.

**Hypothesis:** L1 DAXPY reflects load/store/FMA port limits predicted by IL05/IL36/IL53; outer-cache/DRAM sizes become bandwidth-bound, so isolated arithmetic throughput stops predicting performance.

**Measurement construction:** Allocate aligned/non-aligned x/y, initialize and checksum outside timing, and run enough iterations without changing the mathematical contract. Count exact loads, stores, FLOPs, and useful/estimated traffic; prevent hidden BLAS threading in single-thread rows.

**Derived measurement:** Report cycles/element, GFLOP/s, useful GiB/s, arithmetic intensity, uops/ports, and top-down memory/core bound. Compare measured cycles with `max(front_end, port, L1 traffic, measured-tier bandwidth)` prediction and publish residual error.

**Test plan:** FP32/FP64, N around every cache tier, alias/no-alias contracts, alignment/tails, 1/2/SMT threads, hot/cold. This is validation of the instruction model, not a replacement for B01–B03.

#### IL64. Taylor/polynomial dependency, unrolling, FMA, and accuracy cross-check

**Test:** Evaluate a fixed polynomial/Taylor approximation using Horner, Estrin, scalar, SSE/AVX2, FMA/non-FMA, and multiple independent input streams.

**Hypothesis:** Horner exposes FMA/add-multiply latency through a serial chain; Estrin and multiple inputs add parallelism at extra operations/register pressure, moving performance toward throughput limits. FMA also changes rounding error.

**Measurement construction:** Fix polynomial coefficients, approximation interval, degree, and output contract. Horner is one dependency chain; Estrin exposes a tree; independent-input variants use 1–16 streams. Include coefficient loads and final output in complete rows, with register-resident kernel rows separate.

**Derived measurement:** Report cycles/value, cycles/degree, latency-chain prediction error, reciprocal-throughput prediction error, uops/ports, spills, code bytes, maximum/median ULP or relative error, and vector-lane utilization.

**Test plan:** FP32/FP64, degrees 3–63, scalar/128/256-bit, FMA contraction choices, input distributions including range edges, 1–16 independent values, hot/streamed coefficient states. Determine when Estrin's extra work pays for shorter span and when register spills reverse it.

### 27.8 Publication outputs and stop conditions

The instruction campaign produces five artifacts rather than one oversized leaderboard:

1. A capability manifest containing every requested source-list row and its explicit status.
2. A latency table containing only valid dependency-chain or labeled round-trip results.
3. A reciprocal-throughput/uop/port table containing the saturation stream count and front-end state.
4. Architectural curves/heat maps for boundaries, code size, branches, cache/TLB, forwarding, operand values, and SMT contention.
5. Composite-model checks showing where isolated instruction numbers do and do not predict DAXPY/polynomial performance.

Stop or relabel a row when the generated disassembly differs, dynamic count is unproved, the body spills unintentionally, CPUID/XCR0 disallows it, the counter is materially multiplexed, the task migrates, frequency/temperature leaves its acceptance window, a fault/assist changes semantics unexpectedly, or test and control do not have the same useful contract. Never copy a number from Agner Fog's table into the DUT results: the table is a source of hypotheses and cross-checks, while every published DUT value must come from retained raw runs on the named machine.
