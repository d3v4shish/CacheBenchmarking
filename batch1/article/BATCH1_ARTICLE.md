# Batch 1 — Can this Haswell laptop measure an instruction honestly?

Decision: yes for long, pinned, non-multiplexed basic-counter runs of this narrow integer subset; no for short timing rows or the oversized PMU pass. The batch establishes a trustworthy starting point for future instruction tests, but it is not a general Haswell instruction table and it does not authorize the next benchmark batch.

Open the [visual companion](batch1_visuals.html) for static charts and an optional explanatory animation. The animation explains construction only; every numerical result below comes from retained raw data.

## Question and expectation

The first question was deliberately small: can one machine distinguish three different things?

1. A loop/timestamp cost.
2. The time between dependent instructions (latency).
3. The rate of independent instructions (reciprocal throughput).

The expectation came from Agner Fog's definitions: a true dependency chain measures latency, whereas enough independent instances reveal average cycles per instruction in steady state. On Haswell, the working hypothesis was roughly one core cycle for immediate `ADD` and immediate `SHL`, about three core cycles for immediate `IMUL`, and an ADD-throughput plateau after several independent register chains. [Agner Fog’s instruction tables](https://www.agner.org/optimize/instruction_tables.pdf) are the methodological and architectural hypothesis source; they are not copied as DUT results.

## What exactly ran

The accepted run used a generated body containing 128 static target instructions. `add_latency`, `imul_latency`, and `shift_latency` repeatedly read and overwrite the same register. Their paired empty loop has the same loop counter, decrement, branch, call boundary, and alignment, but no target instruction. For a target row and its same-round empty row:

```text
net core cycles per target instruction
  = (target PMU core cycles − empty-loop PMU core cycles)
    / generated dynamic target-instruction count
```

The primary rows execute 12,000,000 loop turns × 128 target instructions = 1.536 billion target instructions per retained sample. The runner retained 15 randomized rounds. It checked the checksum, observed CPU before/after the timed region, retained `time_enabled`/`time_running`, and saved source/binary hashes plus opcode disassembly.

For throughput, `add_throughput_1/2/4/8` always contains 128 ADD instructions per generated body, distributed across 1, 2, 4, or 8 independent registers. This section reports raw steady-state core cycles per target instruction. The loop contribution is bounded and visible: the median retired-instruction count was 1.015625 per target instruction, meaning the loop contributes approximately two instructions per 128-target body. It is not silently subtracted.

The source, runner, and analyser are heavily annotated so their reason, assumptions, failure modes, measurement denominator, and source links are visible to a beginner:

- [Measurement binary](/home/d3v/Workspace/Benchmark/batch1/src/batch1.cpp)
- [DUT runner](/home/d3v/Workspace/Benchmark/batch1/scripts/run_batch1_on_dut.sh)
- [Raw-data analyser](/home/d3v/Workspace/Benchmark/batch1/src/analyze_batch1.cpp)
- [Source-readability gate](/home/d3v/Workspace/Benchmark/batch1/scripts/check_source_contract.sh)

## DUT and evidence identity

| Item | Accepted-run value |
|---|---|
| CPU | Intel Core i7-4702MQ, Haswell, family 6 model 60 stepping 3 |
| Kernel | Linux 7.0.0-28-generic |
| Placement | CPU 4; all 375 retained rows observed CPU 4 before and after timing |
| SMT sibling | CPU 5; only kernel housekeeping/worker threads were visible in before/after snapshots, not a proof of absolute idleness |
| Compiler | GCC 13.3.0, `-O3 -march=haswell -mtune=haswell -fno-omit-frame-pointer` |
| Governor/profile | `performance`; sysfs reported 3.2 GHz min/max; `intel_pstate/no_turbo=0` |
| PMU policy | `perf_event_paranoid=0` |
| Raw result file | [samples.csv](/home/d3v/Workspace/Benchmark/batch1/results/dut-run2/raw/samples.csv) |
| Raw CSV SHA-256 | `f699ab0e75da9e86241f7f110a24eae51768f359880928f26223f1eb7b08727e` |
| Source SHA-256 | `b3bb29d8bc1476f0bb822f1d993228799b145e0f9aae13cb00abd4f2a0ba23b4` |
| Binary SHA-256 | `9043cd43acfe9ca5175fc00f347edfe1a67f0d0f2b5c1903f8a1c87be1b3e036` |

The full machine snapshot, PMU aliases, run order, source/binary hashes, and disassembly are retained under [the accepted DUT result directory](/home/d3v/Workspace/Benchmark/batch1/results/dut-run2). The accepted session ran from 02:05:37 to 02:09:54 IST. The per-run frequency snapshot and core/reference-cycle ratio show the core was operating above the 2.2 GHz nominal/TSC reference rate; core cycles, rather than TSC ticks, are therefore the latency denominator.

## Results

### 1. The long matched chains landed exactly where expected

| Target chain | Net core cycles / target instruction, median | p05–p95 | Result |
|---|---:|---:|---|
| `addq $3, reg` | 0.9955 | 0.9951–0.9961 | Consistent with 1-cycle latency |
| `shlq $1, reg` | 0.9953 | 0.9934–0.9956 | Consistent with 1-cycle latency |
| `imulq $3, reg, reg` | 3.0019 | 3.0002–3.0030 | Consistent with 3-cycle latency |

These are net values from the primary 1.536-billion-target-instruction rows. The closely packed intervals are evidence that the body is long enough to suppress timer/loop noise—not evidence that the CPU will always behave this way under cache misses, SMT contention, different operands, or another frequency state.

TSC ticks per target were about 0.704 for ADD/shift and 2.123 for IMUL, while core cycles were about 1 and 3. This is expected under the observed performance/turbo profile: TSC/reference time is not the same unit as actual unhalted core cycles. That is why the article never calls “0.704 TSC ticks” ADD latency.

### 2. Short rows showed why the long body was necessary

The first ladder point has only 1,024 generated ADDs. Its net core-cycle p05–p95 interval is 0.827–1.125, much wider than the primary row’s 0.9951–0.9961. The raw timer-only median is 68 TSC ticks (p05 50, p95 86). A tiny body can therefore produce a plausible-looking but unstable quotient.

| Generated ADDs per sample | Median net core cycles / ADD | p05–p95 |
|---:|---:|---:|
| 1,024 | 0.9912 | 0.8271–1.1250 |
| 102,400 | 0.9914 | 0.9885–0.9937 |
| 10.24 million | 0.9961 | 0.9941–1.0004 |
| 1.536 billion | 0.9955 | 0.9951–0.9961 |

This is the batch’s first useful negative lesson: a correct-looking average at very small N is not enough. Batch 1’s accepted instruction values use the longest row only.

### 3. ADD throughput plateaued at four independent chains

| Independent ADD chains | Raw median core cycles / ADD | Approximate ADDs / core cycle | Interpretation |
|---:|---:|---:|---|
| 1 | 1.0033 | 1.00 | Latency-limited chain |
| 2 | 0.5116 | 1.95 | Two chains overlap |
| 4 | 0.2592 | 3.86 | Near the observed steady-state plateau |
| 8 | 0.2587 | 3.87 | No material improvement over four chains |

This confirms the construction difference: one chain does not answer the same question as four. Eight streams were about 0.2% lower than four in the retained medians, but that is far below the roadmap’s 5% materiality threshold. The honest conclusion is “four is sufficient for this body,” not “eight is worse.”

Two related raw steady-state checks were useful but intentionally narrower than a full instruction table:

| Body | Raw median core cycles / target instruction | Observation |
|---|---:|---|
| Eight independent immediate IMUL chains | 1.0033 | About one IMUL issued per core cycle |
| Eight independent immediate shifts | 0.5055 | About two shifts per core cycle |
| Eight independent XOR-zero idioms | 0.2587 | Similar 4-per-cycle delivery ceiling; not proof of a particular backend port allocation |

No uop-port allocation claim is made from these rows. The accepted basic profile did not request those events; the expanded profile did, and multiplexed.

### 4. The zero-idiom probe showed a real dependency difference

Both sequences execute `IMUL; zero-to-value; ADD` 128 times per body and end with the same checksum. In one body the zero is `xor reg,reg`, which can break the prior value dependency. In the control it is `and $0,reg`, which still must read the IMUL result.

`and_zero_control − xor_break` was 4.0131 net core cycles per three-instruction triplet (p05–p95: 4.0020–4.0149). That is strong evidence that the two source-identical-looking ways to produce zero do not create the same dependency graph on this CPU. It is not a license to assign exactly four cycles to one individual instruction: the measured unit is the whole triplet.

### 5. PMU multiplexing was observed, not guessed

The normal `basic` PMU profile had 0/retained rows with `cycles time_running != time_enabled`. Its core-cycle values are retained for the tables above.

The deliberately oversized `expanded` profile had 15/15 rows with cycle multiplexing; its median `time_running / time_enabled` was 0.4976. Its timing remained a useful sensitivity result—median TSC ticks per ADD were 91.25 versus 90.88 for the basic profile—but its uop/port values are not used for an exact attribution. This validates the roadmap rule to use separate compatible PMU passes.

### 6. “Warm,” “cache evicted,” and “page cold” were not treated as synonyms

The page probe touches one volatile byte in each of 1,024 pages. It is a state-label validation, not an ALU benchmark.

| State | Median TSC ticks | p05–p95 | Median minor faults | What it means |
|---|---:|---:|---:|---|
| Warm | 27,074 | 17,736–49,368 | 3 | Pages were initialized before timing |
| Cache-evicted | 37,044 | 35,358–60,082 | 3 | A 32 MiB best-effort eviction walk preceded timing |
| Page-cold | 2,223,114 | 2,209,902–2,270,112 | 1,027 | `MADV_DONTNEED` made first touches fault/repopulate |

The page-cold row is roughly two orders of magnitude slower and has the expected fault signature. Future articles can now use these three labels precisely rather than calling every non-immediate repeat “cold.”

## What differed from the expectation or needs more work

- The original preflight run used only 1–8 static target instructions per body. That violated the roadmap’s 100+ instruction rule. It is retained at [dut-run1](/home/d3v/Workspace/Benchmark/batch1/results/dut-run1) as a rejected preflight, not mixed into any result above. The corrected run generated 128 static ADDs in the latency body; [its opcode disassembly](/home/d3v/Workspace/Benchmark/batch1/results/dut-run2/manifest/disassembly_with_opcodes.txt) verifies this.
- Turbo was enabled (`no_turbo=0`). Sysfs reported a 3.2 GHz performance profile, but this batch did not gain permission or tooling to force and compare a non-turbo profile. Therefore V01 is partially, not completely, satisfied.
- There is no temperature/energy trace. The four-minute session’s cycle/reference ratio was stable enough for this narrow result, but V02 remains incomplete.
- CPU 5 snapshots show only kernel threads, but snapshotting cannot prove that the sibling had zero activity during every nanosecond. SMT interference is intentionally deferred to its dedicated batch.
- Counter boundary operations are outside the target body but PMU enable/disable is not perfectly simultaneous with TSC timestamps. This is negligible for the accepted long rows but is another reason not to interpret the 1,024-instruction ladder as a table value.
- The current throughput bodies show a clear plateau with a small loop fraction. A later IL04 batch should still add a matched independent-stream reference and dedicated uop/port counter passes before publishing a formal comprehensive reciprocal-throughput table.

## Reproducibility

1. Run the source-readability gate.
2. Deploy the exact source and runner to an empty DUT directory.
3. Execute the runner with a new result directory; it compiles, records machine state, hashes source/binary, saves disassembly, randomizes case order, and refuses overwrites.
4. Copy the entire result directory, verify the raw CSV SHA-256, then run `analyze_batch1.cpp` against that raw file.
5. Regenerate charts from the derived CSVs; do not manually change a plotted number.

The retained derived outputs are [group summary](/home/d3v/Workspace/Benchmark/batch1/article/data/group_summary.csv), [paired summary](/home/d3v/Workspace/Benchmark/batch1/article/data/paired_summary.csv), and [quality summary](/home/d3v/Workspace/Benchmark/batch1/article/data/quality_summary.csv).

## Batch decision and next-step gate

Batch 1 passes its central qualification question for long-body integer chains and a basic non-multiplexed PMU profile. It also produced two guardrails that later batches must keep: use core cycles for instruction latency under turbo, and reject/partition multiplexed PMU event sets.

The next batch is not started. Per the roadmap’s batch gate, it requires user permission after review of this article. A sensible candidate is a dedicated front-end/PMU calibration batch: IL05 plus IL11–IL21, using separated compatible event groups and the same comment/provenance contract.
