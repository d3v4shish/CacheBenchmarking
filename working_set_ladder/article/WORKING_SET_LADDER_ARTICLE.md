---
title: "Working-set ladder on Haswell: all eight recorded cases"
description: "The complete preserved timing matrix, a loop-by-loop reconstruction, expectations, evidence, mismatches, and the missing perf data."
date: 2026-09-08
category: "Performance Notes"
subcategory: "Cache/Search"
tags: ["cache", "haswell", "benchmark", "prefetch", "tlb", "mlp", "perf"]
status: "experiment-draft"
confidence: "medium"
last_reviewed: 2026-09-08
featured: false
kind: "note"
listed: true
note_type: "experiment-draft"
code_available: true
last_tested: 2026-06-02
highlights:
  - "All 40 preserved timing medians and five size-specific comparison charts are in this article."
  - "Each of the eight recorded cases has its expected behavior, reconstructed runnable kernel, observed values, and verdict."
  - "No raw repetitions or per-variant perf data were retained for the historical ladder."
---

> **What data exists?** The recorded Haswell experiment preserved 40 timing
> medians: 8 cases × 5 footprints, each reported after 5 repetitions. It also
> preserved an aggregate chart. It did **not** preserve the raw repetitions,
> per-variant perf output, the benchmark binary, or the original runnable
> source. The v2 C++ below is a documented reconstruction; it has not run on
> the Haswell DUT and is not presented as the historical source.

## What are we trying to run?

Run the same five working-set sizes through eight read patterns. Measure
nanoseconds per logical operation. Keep the access pattern fixed while changing
the reachable footprint.

| Recorded run | Value |
| --- | --- |
| Machine | Intel Core i7-4702MQ, Haswell; 4 cores / 8 logical CPUs |
| Cache model used to choose sizes | 32 KiB L1D and 256 KiB L2 per core; 6 MiB shared LLC |
| Footprints | 32 KiB, 256 KiB, 1 MiB, 8 MiB, 64 MiB |
| Compiler | g++ 13.3.0, O3, march=native, C++20 |
| Timing | five repetitions; single-thread rows pinned to one logical CPU |
| Frequency | not fixed; governor was powersave |
| Perf data for this ladder | not retained |

The complete preserved timing input is also available as a CSV download, but every one of its values appears in this article. The reconstruction creates data and verifies a deterministic checksum outside the timer.

## What actually happened: every preserved value

| Case, ns/op | 32 KiB | 256 KiB | 1 MiB | 8 MiB | 64 MiB |
| --- | ---: | ---: | ---: | ---: | ---: |
| Linear scan | 0.600 | 0.527 | 0.534 | 0.827 | 0.867 |
| Stride-16 scan | 2.201 | 4.948 | 6.114 | 9.258 | 10.034 |
| Random direct read | 3.911 | 5.000 | 8.298 | 18.924 | 45.038 |
| Random 64-byte pointer node | 9.365 | 29.733 | 53.748 | 109.177 | 170.508 |
| Dependent index cycle | 6.396 | 19.608 | 45.818 | 116.110 | 160.778 |
| Pointer cycle plus prefetch | 9.847 | 24.481 | 50.856 | 103.324 | 187.059 |
| Eight independent pointer cycles | 1.261 | 3.788 | 6.932 | 17.450 | 42.083 |
| Shuffled one-touch-per-page walk | 19.375 | 7.828 | 7.070 | 28.464 | 38.044 |

## Size and loop comparison charts

<style>
.wsl-size-charts { display:grid; grid-template-columns:repeat(auto-fit,minmax(300px,1fr)); gap:16px; margin:20px 0; }
.wsl-size-chart { border:1px solid currentColor; border-radius:8px; padding:12px; }
.wsl-size-chart h3 { margin:0 0 10px; }
.wsl-bar-row { display:grid; grid-template-columns:92px 1fr 58px; align-items:center; gap:8px; margin:6px 0; font-size:.82rem; }
.wsl-bar-track { height:12px; background:rgba(120,140,160,.22); border-radius:3px; overflow:hidden; }
.wsl-bar-fill { display:block; height:100%; min-width:2px; background:#72b7ff; border-radius:3px; }
.wsl-bar-row code { text-align:right; }
</style>

<figure>
  <figcaption><strong>Each footprint, all eight cases.</strong> Bar length is normalized within its own panel; use the printed ns/op values for cross-panel comparison. These are medians only, so the chart deliberately has no invented error bars.</figcaption>
  <div class="wsl-size-charts" aria-label="Historical median timing comparisons at each footprint">
    <section class="wsl-size-chart"><h3>32 KiB</h3>
      <div class="wsl-bar-row"><span>linear</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:3.1%"></span></span><code>0.600</code></div>
      <div class="wsl-bar-row"><span>stride-16</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:11.4%"></span></span><code>2.201</code></div>
      <div class="wsl-bar-row"><span>random</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:20.2%"></span></span><code>3.911</code></div>
      <div class="wsl-bar-row"><span>pointer</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:48.3%"></span></span><code>9.365</code></div>
      <div class="wsl-bar-row"><span>dependent</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:33.0%"></span></span><code>6.396</code></div>
      <div class="wsl-bar-row"><span>prefetch</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:50.8%"></span></span><code>9.847</code></div>
      <div class="wsl-bar-row"><span>MLP8</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:6.5%"></span></span><code>1.261</code></div>
      <div class="wsl-bar-row"><span>TLB walk</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:100%"></span></span><code>19.375</code></div>
    </section>
    <section class="wsl-size-chart"><h3>256 KiB</h3>
      <div class="wsl-bar-row"><span>linear</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:1.8%"></span></span><code>0.527</code></div>
      <div class="wsl-bar-row"><span>stride-16</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:16.6%"></span></span><code>4.948</code></div>
      <div class="wsl-bar-row"><span>random</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:16.8%"></span></span><code>5.000</code></div>
      <div class="wsl-bar-row"><span>pointer</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:100%"></span></span><code>29.733</code></div>
      <div class="wsl-bar-row"><span>dependent</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:65.9%"></span></span><code>19.608</code></div>
      <div class="wsl-bar-row"><span>prefetch</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:82.3%"></span></span><code>24.481</code></div>
      <div class="wsl-bar-row"><span>MLP8</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:12.7%"></span></span><code>3.788</code></div>
      <div class="wsl-bar-row"><span>TLB walk</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:26.3%"></span></span><code>7.828</code></div>
    </section>
    <section class="wsl-size-chart"><h3>1 MiB</h3>
      <div class="wsl-bar-row"><span>linear</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:1.0%"></span></span><code>0.534</code></div>
      <div class="wsl-bar-row"><span>stride-16</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:11.4%"></span></span><code>6.114</code></div>
      <div class="wsl-bar-row"><span>random</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:15.4%"></span></span><code>8.298</code></div>
      <div class="wsl-bar-row"><span>pointer</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:100%"></span></span><code>53.748</code></div>
      <div class="wsl-bar-row"><span>dependent</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:85.2%"></span></span><code>45.818</code></div>
      <div class="wsl-bar-row"><span>prefetch</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:94.6%"></span></span><code>50.856</code></div>
      <div class="wsl-bar-row"><span>MLP8</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:12.9%"></span></span><code>6.932</code></div>
      <div class="wsl-bar-row"><span>TLB walk</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:13.2%"></span></span><code>7.070</code></div>
    </section>
    <section class="wsl-size-chart"><h3>8 MiB</h3>
      <div class="wsl-bar-row"><span>linear</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:0.7%"></span></span><code>0.827</code></div>
      <div class="wsl-bar-row"><span>stride-16</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:8.0%"></span></span><code>9.258</code></div>
      <div class="wsl-bar-row"><span>random</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:16.3%"></span></span><code>18.924</code></div>
      <div class="wsl-bar-row"><span>pointer</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:94.0%"></span></span><code>109.177</code></div>
      <div class="wsl-bar-row"><span>dependent</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:100%"></span></span><code>116.110</code></div>
      <div class="wsl-bar-row"><span>prefetch</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:89.0%"></span></span><code>103.324</code></div>
      <div class="wsl-bar-row"><span>MLP8</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:15.0%"></span></span><code>17.450</code></div>
      <div class="wsl-bar-row"><span>TLB walk</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:24.5%"></span></span><code>28.464</code></div>
    </section>
    <section class="wsl-size-chart"><h3>64 MiB</h3>
      <div class="wsl-bar-row"><span>linear</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:0.5%"></span></span><code>0.867</code></div>
      <div class="wsl-bar-row"><span>stride-16</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:5.4%"></span></span><code>10.034</code></div>
      <div class="wsl-bar-row"><span>random</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:24.1%"></span></span><code>45.038</code></div>
      <div class="wsl-bar-row"><span>pointer</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:91.1%"></span></span><code>170.508</code></div>
      <div class="wsl-bar-row"><span>dependent</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:86.0%"></span></span><code>160.778</code></div>
      <div class="wsl-bar-row"><span>prefetch</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:100%"></span></span><code>187.059</code></div>
      <div class="wsl-bar-row"><span>MLP8</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:22.5%"></span></span><code>42.083</code></div>
      <div class="wsl-bar-row"><span>TLB walk</span><span class="wsl-bar-track"><span class="wsl-bar-fill" style="width:20.3%"></span></span><code>38.044</code></div>
    </section>
  </div>
</figure>

The case sections below repeat their own five values so the reader never has to
leave the article to compare the code, expectation, evidence, and verdict.

## How to read the colored code

Every block below is C++20 and receives syntax highlighting from the article
renderer. It is the current v2 reconstruction, not the lost historical source.
The setup is outside the timer: it allocates the mapping, fills values, builds
the fixed permutation or closed cycle, and calculates an expected checksum.

Inside the timed body, <code>logical_ops_per_pass_</code> is the operation
denominator. A returned sum or index is checked after timing so the compiler
cannot erase the loads. <code>direct_data_</code>, <code>arena_</code>,
<code>index_cycle_</code>, and <code>tlb_</code> are prepared anonymous
mappings; <code>ArenaNode</code> is exactly one 64-byte cache line.

## Case 1: linear scan

### What this case means

**Linear** means reading the array in increasing-address order: element 0,
then 1, then 2, and so on. This is the useful baseline. A 64-byte cache line
holds sixteen 32-bit elements, so after one line arrives the next fifteen data
loads are already sitting in that line.

### Timed body — C++20

~~~cpp
// direct_data_ is a contiguous uint32_t mapping, filled before timing starts.
// This loop walks it in address order: 0, 1, 2, ... .
uint64_t run_linear() const {
  const uint32_t* values = direct_data_->as<uint32_t>();
  uint64_t sum = 0;
  for (size_t index = 0; index < direct_words_; ++index) {
    // One 4-byte load. Sixteen such values share one 64-byte cache line.
    sum += values[index];
  }
  return sum; // checked after timing; makes every load observable to the compiler
}
~~~

**Read the code.**

- <code>direct_data_</code> points to the prepared contiguous 32-bit mapping.
- The loop increments by one, so one iteration equals one loaded word.
- <code>sum</code> consumes every loaded value and becomes the checksum; it is
  not formatting or setup work inside the timed interval.

### What do we expect, and why?

It should be the cheapest case. A 64-byte line contains sixteen adjacent
32-bit words, so each line fill supplies many future loads. The hardware can
also recognise the stream.

### What happened?

| 32 KiB | 256 KiB | 1 MiB | 8 MiB | 64 MiB |
| ---: | ---: | ---: | ---: | ---: |
| 0.600 | 0.527 | 0.534 | 0.827 | 0.867 |

**Verdict: matched.** It remained below 0.9 ns/op at every recorded size.
**Perf evidence: absent.** The historical run did not retain cycles, cache
misses, bandwidth, or prefetch counters for this row.

## Case 2: stride-16 scan

### What this case means

**Stride-16** means add 16 to the element index after every load. Because an
element is 4 bytes, that advances exactly 64 bytes: each operation samples one
word from a different cache line and discards the other fifteen words fetched
with it. It isolates cache-line underuse while retaining a predictable forward
address stream.

### Timed body — C++20

~~~cpp
// Advance by sixteen uint32_t values: 16 * 4 B = one 64-byte cache line.
uint64_t run_stride16() const {
  const uint32_t* values = direct_data_->as<uint32_t>();
  uint64_t sum = 0;
  for (size_t index = 0; index < direct_words_; index += 16) {
    // Sample one word, then intentionally skip the remaining 15 words in this line.
    sum += values[index];
  }
  return sum;
}
~~~

**Read the code.**

- Incrementing <code>index</code> by 16 skips exactly 16 32-bit words, or one
  64-byte cache line.
- The operation denominator is the number of executed loop iterations, not the
  number of bytes allocated.
- The sum keeps the sampled load observable while the other fifteen words in
  each fetched line remain unused.

### What do we expect, and why?

It should cost more than the stream and rise earlier. It uses one 32-bit word
from a fetched 64-byte line and leaves fifteen words unused.

### What happened?

| 32 KiB | 256 KiB | 1 MiB | 8 MiB | 64 MiB |
| ---: | ---: | ---: | ---: | ---: |
| 2.201 | 4.948 | 6.114 | 9.258 | 10.034 |

**Verdict: matched.** The cost rose from 2.201 to 10.034 ns/op and exceeded the
stream at every size. **Perf evidence: absent.** The timing cannot separate
payload waste, cache misses, and any cache-set effects.

## Case 3: random direct read

### What this case means

**Random direct** means data locations are visited in a shuffled order, but
the shuffled index list itself is already available. The processor may know
several future data addresses at once, so this loses spatial locality without
creating a strict load-to-use chain.

### Timed body — C++20

~~~cpp
// random_queries_ is a fixed shuffled list built before timing.
// Its next query index is independent of the value loaded here.
uint64_t run_random() const {
  const uint32_t* values = direct_data_->as<uint32_t>();
  uint64_t sum = 0;
  for (uint32_t query : random_queries_) {
    // The data address is random, but later query addresses are already known.
    sum += values[query];
  }
  return sum;
}
~~~

**Read the code.**

- <code>random_queries_</code> is built and deterministically shuffled before
  the timer starts.
- Each <code>values[query]</code> is a direct data read; the following query is
  already available from the query stream and does not wait for this value.
- The sum is the semantic checksum for the entire shuffled traversal.

### What do we expect, and why?

It should lose the stream's locality, especially beyond cache capacity. Unlike
a pointer chain, later query addresses are already known, so the core may have
several independent loads in flight.

### What happened?

| 32 KiB | 256 KiB | 1 MiB | 8 MiB | 64 MiB |
| ---: | ---: | ---: | ---: | ---: |
| 3.911 | 5.000 | 8.298 | 18.924 | 45.038 |

**Verdict: matched.** It rose sharply with footprint but remained much cheaper
than one dependent random chain at 64 MiB. **Perf evidence: absent.** No
historical LLC-miss or outstanding-load count exists for this case.

## Case 4: random 64-byte pointer node

### What this case means

This is a shuffled linked list of nodes padded to one 64-byte cache line each.
The current node contains the index of the next node. Until the current node
arrives, the processor cannot know the next data address; that couples random
placement with one-cache-line-per-step and a serial dependency.

### Timed body — C++20

~~~cpp
// Every ArenaNode is padded to exactly one 64-byte cache line.
// arena_start_ selects the first node in a closed shuffled list.
uint64_t run_arena_pointer() const {
  const ArenaNode* values = arena_->as<ArenaNode>();
  uint32_t index = arena_start_;
  uint64_t sum = 0;
  for (uint64_t step = 0; step < logical_ops_per_pass_; ++step) {
    // The current node must arrive before its next index can be read.
    const ArenaNode& node = values[index];
    // Consume payload so this is a real node load, not a link-only traversal.
    sum += node.value;
    // This loaded link is the address for the next iteration: the dependency.
    index = node.next;
  }
  return sum ^ index;
}
~~~

**Read the code.**

- <code>index</code> selects one prepared 64-byte node.
- Reading <code>node.next</code> reveals the address needed by the next
  iteration, creating a load-to-use dependency.
- <code>node.value</code> is added only to make the node payload part of the
  checksum; <code>sum ^ index</code> validates both payload and final link.

### What do we expect, and why?

It should become expensive. Each random node consumes a whole cache line and
the next address is not available until the current node arrives.

### What happened?

| 32 KiB | 256 KiB | 1 MiB | 8 MiB | 64 MiB |
| ---: | ---: | ---: | ---: | ---: |
| 9.365 | 29.733 | 53.748 | 109.177 | 170.508 |

**Verdict: matched.** This was the slowest non-prefetch case at 64 MiB.
**Perf evidence: absent.** The timing supports a locality/dependency story but
does not prove an individual node missed a particular cache level.

## Case 5: dependent index cycle

### What this case means

This case is also a shuffled closed chain, but each link is only a 32-bit index
in a compact array rather than a padded 64-byte node. It keeps the essential
question—"does the next load wait for this load?"—while changing how much
useful link data fits in each cache line.

### Timed body — C++20

~~~cpp
// index_cycle_ is a compact uint32_t array containing a shuffled closed cycle.
// There is no independent next address in this strict load-to-use chain.
uint64_t run_index_cycle() const {
  const uint32_t* next = index_cycle_->as<uint32_t>();
  uint32_t index = index_start_;
  for (uint64_t step = 0; step < logical_ops_per_pass_; ++step) {
    // The value just loaded becomes the address for the next load.
    index = next[index];
  }
  return index;
}
~~~

**Read the code.**

- <code>next</code> is a compact array of 32-bit indices arranged as a closed
  shuffled cycle before timing.
- The assignment <code>index = next[index]</code> is the timed dependency:
  this iteration's loaded value is the next iteration's address.
- There is no independent data address in the loop body to overlap with it.

### What do we expect, and why?

It should expose memory latency as the footprint grows. The core cannot issue
the next data request until the current request returns an index.

### What happened?

| 32 KiB | 256 KiB | 1 MiB | 8 MiB | 64 MiB |
| ---: | ---: | ---: | ---: | ---: |
| 6.396 | 19.608 | 45.818 | 116.110 | 160.778 |

**Verdict: matched.** It rose to 160.778 ns/op at 64 MiB. It was slightly
cheaper than the padded pointer node there because the index payload is compact
rather than one full node per cache line. **Perf evidence: absent.**

## Case 6: pointer cycle plus next-node prefetch

### What this case means

This is the same serial random 64-byte-node chain as Case 4 with one addition:
after reading a node's <code>next</code> field, the code asks the processor to
prefetch that successor. It tests whether a request issued only one dependent
step ahead has enough time to help.

### Timed body — C++20

~~~cpp
// The current node reveals next; only then can software request its successor.
// That gives the prefetch just one dependent step of lead time.
uint64_t run_prefetch_chain() const {
  const ArenaNode* values = arena_->as<ArenaNode>();
  uint32_t index = arena_start_;
  uint64_t sum = 0;
  for (uint64_t step = 0; step < logical_ops_per_pass_; ++step) {
    // Fetch the current random node, then discover its successor.
    const ArenaNode& node = values[index];
    const uint32_t next = node.next;
    // Hint only: it neither waits for the fill nor removes the dependency.
    __builtin_prefetch(&values[next], 0, 1);
    sum += node.value;
    index = next;
  }
  return sum ^ index;
}
~~~

**Read the code.**

- The current node first supplies <code>next</code>; only then can the program
  issue <code>__builtin_prefetch</code> for the successor.
- The prefetch is only a request. It does not prove a cache fill or make the
  following dereference independent.
- Assigning <code>index = next</code> preserves the same pointer-chain
  semantics as the no-prefetch case.

### What do we expect, and why?

It might help if the next node can arrive before use. It might not: the
dependency delays address discovery, and the extra request can consume cache or
memory resources.

### What happened?

| 32 KiB | 256 KiB | 1 MiB | 8 MiB | 64 MiB |
| ---: | ---: | ---: | ---: | ---: |
| 9.847 | 24.481 | 50.856 | 103.324 | 187.059 |

**Verdict: did not match a simple “prefetch helps” expectation.** It was lower
than the dependent index cycle at 8 MiB, 103.324 versus 116.110 ns/op, but was
worse at 64 MiB, 187.059 versus 160.778 ns/op. The historical timing cannot
tell whether lead time, cache pollution, or traffic caused that reversal:
there are no per-case cache or memory counters.

## Case 7: eight independent pointer cycles

### What this case means

This runs eight separate linked lists side by side. Within any one lane the
next address still depends on the prior node, but lane 0 does not depend on
lane 1 through lane 7. It tests memory-level parallelism: whether several
independent cache misses can overlap.

### Timed body — C++20

~~~cpp
// Each lane is a dependent pointer chain, but the eight lanes are independent.
// Seeds are chosen before timing so setup is not measured.
uint64_t run_mlp8() const {
  const ArenaNode* values = arena_->as<ArenaNode>();
  std::array<uint32_t, 8> indexes{};
  uint32_t cursor = arena_start_;
  for (size_t lane = 0; lane < indexes.size(); ++lane) {
    indexes[lane] = cursor;
    cursor = values[cursor].next;
  }
  uint64_t sum = 0;
  for (uint64_t step = 0; step < logical_ops_per_pass_ / indexes.size(); ++step) {
    for (size_t lane = 0; lane < indexes.size(); ++lane) {
      // This lane's next address depends only on this lane's current node.
      const ArenaNode& node = values[indexes[lane]];
      sum += node.value;
      indexes[lane] = node.next;
    }
  }
  for (uint32_t index : indexes) sum ^= index;
  return sum;
}
~~~

**Read the code.**

- The initial eight indices are selected before the timed inner loop.
- In each inner iteration, every lane reads its own node and updates only its
  own successor index.
- A lane still has a dependency on itself, but the eight lanes do not depend on
  each other; this is the source of possible memory-level parallelism.
- The final XOR makes every ending index part of the checksum.

### What do we expect, and why?

It should beat one dependent chain beyond cache because independent misses can
overlap. It should not become eight times cheaper: issue bandwidth, miss
buffers, cache capacity, and DRAM are finite.

### What happened?

| 32 KiB | 256 KiB | 1 MiB | 8 MiB | 64 MiB |
| ---: | ---: | ---: | ---: | ---: |
| 1.261 | 3.788 | 6.932 | 17.450 | 42.083 |

**Verdict: matched.** At 64 MiB it was about 3.8 times cheaper than the one
dependent index cycle, 42.083 versus 160.778 ns/op. **Perf evidence: absent.**
The result is consistent with overlap but has no historical counter proof of
the number of concurrent misses.

## Case 8: shuffled one-touch-per-page walk

### What this case means

This is a random dependent chain over pages, not words: one load is made from
the first bytes of each selected 4 KiB page. Each result names the next page.
It puts pressure on address translation as well as data caching, because the
processor repeatedly needs a translation for an unrelated page.

### Timed body — C++20

~~~cpp
// Each page stores its successor page number in its first four bytes.
// The 4 KiB-page mapping and shuffled closed cycle are built before timing.
uint64_t run_tlb_walk() const {
  const uint8_t* bytes = tlb_->as<uint8_t>();
  uint32_t page = tlb_start_page_;
  for (uint64_t step = 0; step < logical_ops_per_pass_; ++step) {
    // page * 4096 chooses one page; the loaded successor selects the next page.
    // memcpy avoids assuming that the stored uint32_t is naturally aligned.
    std::memcpy(&page, bytes + static_cast<size_t>(page) * kPageBytes, sizeof(page));
  }
  return page;
}
~~~

**Read the code.**

- Each page stores the index of its shuffled successor in the mapping's first
  bytes; the cycle is built before timing.
- The address uses <code>page * kPageBytes</code>, so each logical operation
  touches one selected 4 KiB page.
- <code>memcpy</code> reads the stored next-page index without assuming
  alignment. That loaded index names the next page and creates the dependency.

### What do we expect, and why?

It should become expensive after translation reach is exceeded. One load per
page provides little data-cache reuse and also asks the processor to translate
many shuffled pages.

### What happened?

| 32 KiB | 256 KiB | 1 MiB | 8 MiB | 64 MiB |
| ---: | ---: | ---: | ---: | ---: |
| 19.375 | 7.828 | 7.070 | 28.464 | 38.044 |

**Verdict: partial match.** The two large points rose as expected. The first
three points are not monotonic, so the data does not support a simple
“more pages always costs more” claim. No dTLB event count was retained; it is
not possible to divide this timing into translation, data-cache, and
measurement-overhead components.

## What the next run must add

The existing v2 runner has not been executed on the Haswell DUT. When it is,
each of the eight reconstructed kernels will retain raw repetitions and
separate cycles/instructions, branches, cache, and dTLB observer passes with
time-enabled/time-running fields. Until then, the 40 medians above are all the
historical ladder evidence available locally; no perf table can honestly be
added.



