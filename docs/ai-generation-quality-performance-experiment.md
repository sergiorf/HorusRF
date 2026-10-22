# AI generation M6Q.6 performance experiment

**Date:** 2026-10-23  
**Dataset:** 1.0.0 (17 cases)  
**Models:** `qwen2.5-coder:3b`, `qwen2.5-coder:7b`  
**Repetitions:** three per model, limit, and case  
**Decision:** retain the 2,048-token default

M6Q.6 added separate cold and warm latency summaries, per-attempt compiler and
requirement-verification timing, and typed provider output-limit reporting. The
live gate compared the unchanged `direct-dsl-v1` prompt at 2,048 and 512 output
tokens. Both reports were produced at revision
`7f51c656428480546b872107b840c198a781f2bf` with temperature zero, a 120-second
timeout, and at most three repairs.

## Results

The supported-case denominator is 48: 16 supported cases times three
repetitions. Final-valid includes the three preflight-rejected unsupported runs.

| Limit | Model | Final valid | Requirements | Semantic match | Warm median | Warm P90 | Output-limit errors |
|---:|---|---:|---:|---:|---:|---:|---:|
| 2,048 | 3B | 30/51 | 21/48 | 18/48 | 13.819 s | 28.940 s | 0 |
| 512 | 3B | 30/51 | 21/48 | 18/48 | 16.155 s | 32.646 s | 0 |
| 2,048 | 7B | 36/51 | 30/48 | 30/48 | 18.703 s | 53.328 s | 0 |
| 512 | 7B | 36/51 | 30/48 | 30/48 | 17.838 s | 53.266 s | 0 |

Every correctness count is identical. The 512-token limit increased 3B warm
median latency by 16.9% and warm P90 by 12.8%. It reduced 7B warm median by 4.6%
and warm P90 by 0.1%, which is not a consistent cross-model improvement. Cold
latency has one sample per model and is retained in the reports without using it
for the decision.

The largest measured response was 303 tokens for 3B and 333 tokens for 7B, so
the 512-token setting never constrained generation. Average compiler and
requirement-verification durations were below one millisecond; model generation
dominates latency and those local stages are not useful optimization targets.

## Decision

Keep the 2,048-token setting. A 512-token cap appears safe for this dataset but
does not deliver a reproducible performance benefit. A lower cap would introduce
truncation risk without evidence of faster generation because providers stop
when the response is complete rather than consuming the entire allowance.
The provisional under-10-second warm median target remains unmet for both model
sizes; this gate closes the tested optimization, not that broader target.

The compact and feature-selected prompt variants remain opt-in experiments. As
recorded by M6Q.2, their latency gains are coupled to material correctness
regressions for at least one supported model, so they cannot be promoted as
performance optimizations.

Compact history snapshots are retained as
`evaluation/history/m6q6-direct-dsl-v1-t2048-r3.json` and
`evaluation/history/m6q6-direct-dsl-v1-t512-r3.json`. The full attempt-level
reports remain ignored build artifacts under `build/ai-evaluation/`.
