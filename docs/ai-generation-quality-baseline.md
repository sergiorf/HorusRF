# AI generation quality baseline

**Baseline:** `build/ai-evaluation/results-20261021-193036.json`  
**Dataset:** 1.0.0 (17 cases)  
**Revision:** `66e692f5d76db7e91273ca59fa8fdfa963e97c1d`  
**Models:** `qwen2.5-coder:3b`, `qwen2.5-coder:7b`  
**Repetitions:** one

This is the reproducible M6Q baseline. It is a schema-v1 artifact, so it retains
the final candidate and aggregate per-call telemetry, but not intermediate
candidates, compiler diagnostics, or semantic-difference paths. Schema v2 now
records that evidence for every call. The classifications below were reviewed
against the retained final source, the case request, and the compiler and strict
semantic-comparison behavior. They must not be treated as requirement-oracle
results before M6Q.1.

The later schema-v2 A/B/C comparison and the decision to retain the baseline
prompt are documented in the
[M6Q.2 prompt experiment](ai-generation-quality-prompt-experiment.md).

## Aggregate results

| Model | First valid | Final valid | Strict semantic matches | Median latency | P90 latency | Output throughput |
|---|---:|---:|---:|---:|---:|---:|
| 3B | 8/17 | 10/17 | 5/16 supported | 10.876 s | 22.762 s | 36.315 tokens/s |
| 7B | 10/17 | 12/17 | 9/16 supported | 19.634 s | 58.726 s | 15.732 tokens/s |

## Case-level diagnosis

“Match” means strict normalized-IR equality. “Comparator limitation” is a
reviewed hypothesis which M6Q.1 must confirm with curated requirements.

| Case | 3B classification | 7B classification |
|---|---|---|
| `frequency_sweep_basic` | Match | Match |
| `frequency_sweep_natural_language_variant` | Match | Match |
| `frequency_sweep_named_power` | Invalid after repair: reserved `correction` used as a calibration identifier; syntax diagnostics persist | Match |
| `frequency_sweep_multiple_measurements` | Valid; likely comparator limitation from an extra derived error and algebraically equivalent negation | Match |
| `frequency_sweep_derived_error` | Valid; requested measurement name omitted and requested subtraction direction reversed | Match |
| `frequency_sweep_power_assertion` | Valid; requested power-error assertion absent | Valid; requested power-error assertion absent |
| `measurement_assertions` | Valid; calibration correction has the opposite sign | Match |
| `temperature_sweep` | Invalid after repair: fixed-frequency sweep lacks required range and step syntax | Valid; comparator differs only on an unspecified positive one-point frequency step |
| `multidimensional_sweep` | Match | Invalid after repair: temperature endpoint uses delta unit `deltaC` where an absolute temperature is required |
| `calibration_surface` | Match | Invalid after repair: named measurement omits `= power`, followed by malformed derive syntax |
| `multiple_constraints` | Invalid after repair: `abs()` and unary negation are applied to absolute power rather than `PowerDelta` | Valid; strict “below” requirements changed to inclusive `<=` comparisons |
| `unit_conversions` | Valid; `-10000 mdBm` is emitted as `-10000 dBm`, producing a reference mismatch | Match |
| `single_frequency_point` | Invalid after repair: compiler-required frequency sweep omitted | Invalid after repair: incomplete sweep and invalid measurement syntax persist |
| `derived_measurement_chain` | Invalid after repair: named measurement omits `= power` and is then referenced | Invalid after repair: named measurement omits `= power`; later repair also omits `derive` before calibration |
| `qualification_boolean_alias` | Match | Match |
| `temperature_first_multidimensional` | Invalid after repair: absolute measured power is used as a calibration correction | Match |
| `fixed_frequency_power_sweep_unsupported` | Invalid and unsupported request not recognized; invents `sweep power` | Invalid and unsupported request not recognized; invents `sweep power` |

For valid mismatches, the strict comparator evidence falls into these stable
paths: `reference`, `derived_values`, `assertions`, `calibration.correction`, and
one-point sweep `step`. Schema v2 serializes the exact path, expected value, and
actual value per attempt. For invalid candidates it serializes diagnostic
category, code, message, and source span per attempt. The old artifact cannot be
retroactively upgraded because it did not retain intermediate response text.

## Performance-result reconciliation

The apparently conflicting performance figures came from two separate saved
runs, not one artifact reported two ways:

| Artifact | Revision | 3B median / throughput | 7B median / throughput |
|---|---|---:|---:|
| `results-20261021-190949.json` | `0515423a1d28f1773a9eaabf0bc705af49dec848` | 13.662 s / 35.787 tokens/s | 17.672 s / 17.320 tokens/s |
| `results-20261021-193036.json` | `66e692f5d76db7e91273ca59fa8fdfa963e97c1d` | 10.876 s / 36.315 tokens/s | 19.634 s / 15.732 tokens/s |

The incoming rounded figures (13.7 s, 17.7 s, 35.8 tokens/s, and 17.3
tokens/s) describe the earlier `190949` run. The later `193036` run is the
reviewed baseline. Results remain separate because their revisions and run
times differ.

## Schema-v2 measurement convention

- `run_order` is the global case execution order in the report.
- The first evaluated case for each model is classified `cold`; later cases for
  that model are classified `warm`. Provider load duration remains the
  authoritative per-call telemetry and can reveal model reloads.
- Prompt configuration is `direct-dsl-v1`; comparison is `semantic-ir-v1`.
- Prompt size is UTF-8 byte count for each message and response schema, plus the
  corresponding total. Provider input-token counts are retained independently.
- Ollama model digest is copied from `/api/tags` when present.
