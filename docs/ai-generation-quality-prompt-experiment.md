# AI generation M6Q.2 prompt experiment

**Date:** 2026-10-22  
**Dataset:** 1.0.0 (17 cases)  
**Models:** `qwen2.5-coder:3b`, `qwen2.5-coder:7b`  
**Repetitions:** three per model, configuration, and case  
**Decision:** retain `direct-dsl-v1` as the default

This records the live A/B/C gate for M6Q.2. Configuration A is the unchanged
baseline prompt, B compacts every applicable example, and C uses compact,
deterministically feature-selected examples. B and C remain available for
explicit evaluation experiments, but neither qualifies for promotion because
each has a material correctness regression for at least one supported model.

## Reproducibility

All reports use evaluation schema 2, dataset 1.0.0, temperature 0, a 2,048-token
output limit, a 120-second per-provider-call timeout, and at most three compiler
repair attempts. The model digests were identical across all three reports:

- `qwen2.5-coder:3b`:
  `f72c60cabf6237b07f6e632b2c48d533cef25eda2efbd34bed21c5e9c01e6225`
- `qwen2.5-coder:7b`:
  `dae161e27b0e90dd1856c8bb3209201fd6736d8eb66298e75ed87571486f4364`

The local, ignored report artifacts are:

| Configuration | Stable ID | Report | Revision recorded by report |
|---|---|---|---|
| A | `direct-dsl-v1` | `build/ai-evaluation/m6q2-abc-direct-dsl-v1-r3.json` | `fff58b2b3e42da0785701c8a090a95ab1037c292` |
| B | `direct-dsl-compact-v1` | `build/ai-evaluation/m6q2-abc-direct-dsl-compact-v1-r3.json` | `64d38b715ca19137488743624a66204ae84621d0` |
| C | `direct-dsl-relevant-v1` | `build/ai-evaluation/m6q2-abc-direct-dsl-relevant-v1-r3.json` | `64d38b715ca19137488743624a66204ae84621d0` |

The A revision contains the prompt-experiment implementation. The later B/C
revision adds the local Ollama launcher and evaluator progress reporting; it does
not change prompt construction, model settings, the dataset, or the requirement
oracle.

## Correctness results

The supported-case denominator is 48: 16 supported cases times three
repetitions. Final-valid uses all 51 runs, including the unsupported case.

| Configuration | Model | First valid | Final valid | Requirements satisfied | Strict semantic matches | Unsupported recognized |
|---|---|---:|---:|---:|---:|---:|
| A | 3B | 24/51 | 30/51 | 18/48 | 15/48 | 0/3 |
| A | 7B | 30/51 | 36/51 | 27/48 | 27/48 | 0/3 |
| B | 3B | 24/51 | 27/51 | 6/48 | 6/48 | 0/3 |
| B | 7B | 33/51 | 36/51 | 27/48 | 21/48 | 0/3 |
| C | 3B | 30/51 | 33/51 | 24/48 | 21/48 | 0/3 |
| C | 7B | 24/51 | 27/51 | 21/48 | 15/48 | 0/3 |

B preserves the 7B requirement total but lowers its strict semantic matches and
reduces 3B requirement satisfaction from 37.5% to 12.5%. C improves 3B
requirement satisfaction from 37.5% to 50.0%, but lowers the 7B result from
56.3% to 43.8% and its strict semantic match rate from 56.3% to 31.3%.

For compiler validity, requirement satisfaction, and strict semantic match, each
model/configuration/case combination produced the same classification in all
three repetitions: every count was either zero or three. C's third 7B
`derived_measurement_chain` run had one Ollama timeout. That case did not compile
successfully in either of C's other repetitions or any A repetition. B produced
valid candidates for it but did not satisfy its requirements or strict semantic
comparison. The timeout therefore does not alter the prompt-promotion decision;
it remains recorded as environmental variability.

None of the prompts recognized the unsupported fixed-frequency power-sweep
request. Typed unsupported handling remains assigned to M6Q.5 rather than being
treated as a prompt-selection criterion that one of these variants passed.

## Prompt size and performance

Token figures are averages per provider call with telemetry, including repair
calls. Prompt bytes are averages for the first generation call. Repairs are
averages per case.

| Configuration | Model | Prompt bytes | Measured calls | Input tokens | Output tokens | Repairs | Median | P90 | Output throughput |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| A | 3B | 11,423 | 96 | 2,376.5 | 224.9 | 0.882 | 13.298 s | 25.186 s | 31.93 tokens/s |
| A | 7B | 11,423 | 96 | 2,381.9 | 236.8 | 0.882 | 17.470 s | 54.522 s | 16.15 tokens/s |
| B | 3B | 10,282 | 99 | 2,233.7 | 126.4 | 0.941 | 5.994 s | 14.053 s | 35.35 tokens/s |
| B | 7B | 10,282 | 84 | 2,270.1 | 148.7 | 0.647 | 9.212 s | 33.332 s | 17.64 tokens/s |
| C | 3B | 9,049 | 81 | 2,065.9 | 130.5 | 0.588 | 3.870 s | 12.069 s | 37.64 tokens/s |
| C | 7B | 9,049 | 102 | 2,064.0 | 154.6 | 1.020 | 20.695 s | 36.388 s | 17.50 tokens/s |

B demonstrates that compact examples and shorter responses can materially reduce
latency, particularly for 7B. C demonstrates the strongest 3B result with the
smallest prompts and lowest 3B latency. Those benefits are not portable across
both supported model sizes, so they are evidence for later targeted experiments
rather than a safe global default.

## Promotion decision

Keep A, `direct-dsl-v1`, as the product default. B and C fail the M6Q.2 exit rule
that a promoted configuration have no material correctness regression. Do not
combine or dynamically select these prompts by model without a separately
approved experiment; the current evidence covers only 17 cases and two model
digests.

M6Q.2 is complete. The next milestone is M6Q.3, conservative extraction of
high-confidence requirements from interactive user instructions and verification
of compiled proposals before Studio presents them as trustworthy.
