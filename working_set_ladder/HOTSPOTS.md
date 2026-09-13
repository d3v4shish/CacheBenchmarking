# Hotspots

For `stride16_scan`, each logical operation loads one 64-byte cache line but
uses only its first 4-byte `uint32_t`.  Thus spatial locality is intentionally
poor (1/16 of a line payload is useful), while the next address remains
predictable to hardware prefetchers.  Expected transitions are primarily
capacity/memory-bandwidth effects rather than branch or dependency latency.

The accepted DUT data will determine whether the compiler loop, L1/L2/LLC
capacity, DRAM traffic, branch behavior, or TLB activity dominates. Generic
cache/TLB aliases are not interpreted if they do not run; the retest therefore
uses one Haswell raw miss/walk event per profile. No performance improvement is
claimed in advance of that measurement.

## Measured v3 hotspots

Above 36 KiB, raw L1 misses are about one per useful operation because every
operation begins a new cache line. Raw L2 misses climb to about 0.8/op beyond
L2; raw L3 misses rise from near zero at 4 MiB to about 0.445/op at 32 MiB and
larger. Completed DTLB walks stabilize near 0.0157/op through the LLC-sized
range. Sequential prefetch explains why neither L2 nor L3 miss rate reaches
one.

Clang scalar's small-cache advantage is an instruction/control hotspot, not a
memory hierarchy change: it retires about 2.375 instructions/op at 512 MiB,
versus GCC scalar's 5.000, but all products converge near 10 TSC ticks/op in
DRAM. Continuous `turbostat` is a PMU scheduling hotspot on this Haswell: it
prevented the constrained raw load-miss event from running and was omitted from
the accepted raw-cache supplement.

## Measured random-access hotspots

For the direct-index ladder, a query-array load selects each payload address.
The indexed payload load is the hotspot: at 512 MiB the scalar ladder measures
57.58 TSC ticks/load while raw L1/L2/L3 misses are 1.060/1.060/1.003 per load
and completed DTLB walks are 0.995/load. The loop branch is not the cause:
branch misses are negligible and the C++ language-control assembly is a
regular query-load, indexed-payload-load, loop-branch body.

Language runtime is a secondary small-cache hotspot. The retained C/C++/Rust
controls retire about 5/5/7 instructions per load; Go retires about 8. At
DRAM scale all remain close because the random payload miss dominates.
Compiler unrolling is a separate core-side effect: the GCC/Clang compiler
control reduces retired instructions to about 2.3–2.9/load in several products
and changes cache-resident time, but all variants converge within 1% at 512
MiB. Finally, the MLP8 control reaches 27.462 TSC ticks/node at 512 MiB versus
57.423 for scalar direct index; that is latency overlap after changing the
dependency structure, not a direct-index speedup.

## Measured arena pointer-chase hotspots

The pointer chase has one true load-to-use dependency per 64-byte node. Its
GCC scalar ladder rises from 7.32 TSC ticks/node at 24 KiB to 21.11 at 36 KiB,
113.27 at 6 MiB and 222.24 at 512 MiB. Separate exact raw PMU passes reach
about one L1 miss/node above L1, one L2 miss/node above L2, and at 512 MiB one
L3 miss/node plus 0.992 completed DTLB walks/node. The branch profile has only
0.0000032 branch misses/node there, so branch prediction is not the limiter.

Rust and Clang have a core-side loop-shape advantage while data is cached:
Rust reaches 6.01 cycles/node at 32 KiB and Clang scalar 6.31, versus GCC C++
8.02/8.04. Retained assembly shows serial unrolling and different loop-control
and address-generation sequences, not SIMD or concurrent pointer misses. At
512 MiB C/C++/Rust converge within roughly 3% of 220 cycles; Go retires about
24 rather than 9 instructions/node and measures 227.90 cycles/node.

## Measured dependent-index-cycle hotspots

The strict `index = next[index]` recurrence rises from 5.05 TSC ticks/index
at 32 KiB to 5.95 at 36 KiB, 14.39 at 256 KiB, 35.83 at 1 MiB, 71.09 at 6 MiB,
101.81 at 8 MiB, and 214.66 at 512 MiB. Exact raw events show L1 misses/index
climbing from 0.006 at 32 KiB to 0.971 at 1 MiB and effectively one at 512
MiB; DTLB walks reach 0.992/index at 512 MiB. This is serialized latency, not
cache-line bandwidth or MLP.

At 512 MiB, C/C++/Rust/Go converge at 214.50/215.09/215.14/215.13
cycles/index. C/C++ retire about four instructions/index and Rust/Go about
six. The same-source Clang loop retires 1.375 instructions/index versus GCC's
four, yet both remain about 215 cycles/index at 512 MiB: less loop work cannot
remove the DRAM-scale load-to-use dependency.

## Measured late-prefetch-chain hotspot

This chain has one random 64-byte node per line. The current node's next
field must be demand-loaded before its successor address exists, so the
software prefetch itself is downstream of the dominant dependency. The
accepted warm/basic ladder rises from 7.26 TSC ticks/node at 24 KiB to 18.16
at 36 KiB, 46.10 at 1 MiB, 114.34 at 6 MiB, and 235.29 at 512 MiB. Branch
misses are only about 0.0000031/node at 512 MiB.

The direct GCC 13 same-source paired control is the causal check: its
PREFETCHT2-on binary is 19.210 versus 15.860 cycles/node at 36 KiB and
236.789 versus 219.778 at 512 MiB. The 32 KiB inversion (7.466 on versus
8.072 off, despite 12 versus 9 instructions) is retained as a cache-resident
code-shape anomaly, not claimed as memory latency hiding. The raw DTLB walk
profile rises to 0.992 walks/node at 512 MiB; raw cache-event values are
retained but not interpreted as demand-miss probabilities because their tiny,
non-monotonic rates fail that semantic sanity check.

## Measured MLP8-cycle hotspot

MLP8 preserves a load-to-use address dependency within every lane but holds
eight successor addresses live concurrently. At 512 MiB the scalar warm/basic
ladder reaches 27.596 ticks/node rather than the arena pointer chase's 222.238.
The direct same-source GCC 13 lane control confirms causality: 219.974
cycles/node for one lane versus 27.694 for eight. This is latency overlap; it
is neither SIMD nor a hardware/software prefetch result.

Raw Haswell profiles rise from almost no raw L1 misses at 32 KiB to roughly
0.125 raw L1/L2/L3 event counts per logical node at 512 MiB; DTLB completed
walks reach 0.1242/node. These raw event encodings are retained diagnostics,
not demand-miss probabilities: the one-eighth normalization is a documented
semantic warning for this multi-lane workload. The accepted language controls
converge at DRAM scale (27.16 C, 27.12 Rust, 27.64 C++, 28.17 Go cycles/node);
small-cache differences follow their 6.25/7.875/7.75/22.63 retired
instructions per node instead.
