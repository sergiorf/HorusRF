# HorusRF M6Q.9 Slice 1 guardrail evidence

**Status:** Completed on 2026-10-26 from the controlled M6Q.8 development
reports. This is an offline replay and failure review; it makes no new live-model
or product-quality claim.

## 1. Inputs and reproducibility

All five complete M6Q.8 reports are preserved as immutable schema-v2 snapshots:

- `evaluation/history/m6q8-controlled-direct-dsl-v1-r3.json`;
- `evaluation/history/m6q8-controlled-direct-dsl-relevant-v1-r3.json`;
- `evaluation/history/m6q8-controlled-structural-rag-k1-r3.json`;
- `evaluation/history/m6q8-controlled-structural-rag-k2-r3.json`; and
- `evaluation/history/m6q8-controlled-structural-rag-k3-r3.json`.

Each snapshot records the raw report's SHA-256, model digests, settings, dataset,
oracle, comparison, revision, summaries, and per-case persistence. The reports
use dataset `2.0.0`, comparison `semantic-ir-v2`, requirement schema
`golden-requirements-v2`, and revision
`c15089f98289b085901c7e27cd30914f1bcbfcb1`.

Reproduce the offline replay without Ollama or network access:

```powershell
.\scripts\analyze-ai-guardrail-replay.cmd
```

The detailed result is written to the ignored diagnostic artifact
`build/ai-evaluation/m6q9-slice1-guardrail-replay.json`. The script verifies
schema, K values, dataset/comparison/oracle identities, and shared evaluator
settings before pairing runs.

## 2. Apply-gate proxy replay

Schema-v3 retains the independent curated requirement verdict but does not retain
the final interactive requirement-verification verdict that would drive the
proposed Apply gate. The replay therefore uses **agent completion** as a proxy:
allow only a completed, compiler-valid proposal with a `supported` support
assessment; reject failures; send other valid support states to review.

This proxy is deliberately not labeled `g1-acceptance`. Its precision is scored
against the independent curated oracle.

| Policy proxy | Applicable | Allowed | Correct allowed | Incorrect allowed | Precision | Verified completion | Coverage |
|---|---:|---:|---:|---:|---:|---:|---:|
| 3B K=1 | 84 | 48 | 42 | 6 | 87.5% | 50.0% | 57.1% |
| 7B K=3 | 84 | 72 | 69 | 3 | 95.8% | 82.1% | 85.7% |

The current completion signal is useful but not a sufficient safety gate. For
7B it blocks all 12 terminal failures, yet allows three repetitions of
`combined_limits_paraphrase` whose requested measurement name is wrong. The
interactive extractor did not turn "Record output as actual" into a name-fidelity
requirement, so compiler validity and agent completion cannot detect the error.

## 3. 7B K=3 failure taxonomy

All 15 unsuccessful applicable runs are five case families repeated identically
three times. No failure reaches the repair limit; every attempted correction or
repair stops after one follow-up because the diagnostic or verified violation
does not change.

| Case | Runs | First failing boundary | Stable evidence | Guardrail implication |
|---|---:|---|---|---|
| `combined_limits_paraphrase` | 3 | Interactive acceptance gap | Valid program; `measurement_missing_or_misnamed`; generated `actual_power` instead of requested `actual` | Extend conservative measurement-name extraction and serialize the interactive verdict before enabling an Apply gate |
| `derived_measurement_chain` | 3 | Requirement correction no progress | Valid program; `measurement_missing_or_misnamed`; correction repeats the violation | More budget will not help; a fresh targeted correction must change its evidence or stop |
| `calibration_sign_paraphrase` | 3 | Compiler repair no progress | `parse.expected_identifier`; model declares reserved identifier `correction` | Improve compiler-owned repair context or use a fresh start; the directly relevant `negated_error_calibration` corpus example was not selected |
| `inclusive_frequency_error` | 3 | Compiler repair no progress | Duplicate declaration, unavailable reference, and missing measurement after naming a measurement `power` | Improve identifier-collision repair context; relevant inclusive examples were already selected, so more examples alone are unlikely to help |
| `mixed_supported_unsupported_sweeps` | 3 | Partial-support generation failure | `parse.expected_identifier` after emitting unsupported `sweep power` | Turn the existing partial-support assessment into explicit generation constraints; do not treat this as ordinary syntax repair |

The selected K=3 examples are recorded, but schema-v3 does not retain ranked
candidates beyond K. Automatic replay therefore cannot prove that a distinct
unused rescue example exists. Manual corpus review finds a plausible unused
calibration-sign example and a derived-error example, while the inclusive-error
failure already received directly relevant examples. Alternate retrieval remains
a hypothesis, not a justified general rescue policy.

## 4. Cascade replay

The replay pairs each 3B K=1 case/repetition with its 7B K=3 counterpart. It
escalates only when the 3B completion proxy does not allow the result.

| Applicable | Accepted | Correct accepted | Incorrect accepted | Escalated | Precision | Verified completion | Coverage |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 84 | 75 | 66 | 9 | 36 (42.9%) | 88.0% | 78.6% | 89.3% |

This cascade is worse than using 7B K=3 directly. Six incorrect 3B proposals are
allowed and therefore never escalated; some replace requests that 7B handles
correctly. Latency and model-cache behavior are not measured by this replay, but
the quality result alone blocks implementation of `g4-cascade` with the current
gate.

## 5. Decisions

1. **Proceed with acceptance observability, not product gating yet.** Add the
   final interactive requirement-verification result and a deterministic
   allow/review/reject reason to evaluation reports. Re-run this replay using the
   real verdict rather than completion as a proxy.
2. **Close the measurement-name extraction gap.** Add conservative tests for
   "record output as actual" and related explicit naming forms, with false-positive
   controls. This directly addresses the three observed unsafe acceptances.
3. **Do not implement `g2-reserved-correction` from current evidence.** No K=3
   failure exhausted the shared budget; extra unchanged calls would add latency
   without addressing stagnation.
4. **Keep `g3-alternate-rescue` gated.** First expose or replay ranked candidates
   beyond K and prove that a different validated example addresses a recurring
   failure. A failure-specific fresh start may be justified; a generic retry is
   not.
5. **Do not implement `g4-cascade` yet.** Reconsider it only after the gate's
   accepted precision is materially improved and measured with the real
   interactive verdict.

## 6. Slice 2A implementation

**Completed on 2026-10-26 without a live-model run.** Slice 2A:

- retains the proposal's final interactive requirement verification in
  `horusrf.ai-evaluation` schema version 4;
- computes evaluation-only `allow`, `review_required`, or `reject` decisions from
  generation, compiler, support, and interactive-verification evidence, with
  stable reason codes;
- recognizes explicit power-output naming forms such as "record output as
  actual" and "name the power reading output", while excluding CSV/JSON and
  other output-format words as measurement names;
- exposes accepted precision and acceptance coverage with explicit denominators,
  and preserves the new per-case counts in schema-v2 history snapshots; and
- adds deterministic extractor, agent, evaluator, serializer, and schema-v3/v4
  archive-compatibility tests.

The Release build and full unfiltered CTest suite passed 36/36 tests.

Studio eligibility and default prompts remain unchanged until the real gate is
measured against the curated oracle and separately approved.

The next task is a controlled schema-v4 replay of the unchanged 3B K=1 and 7B
K=3 configurations. It must compare the real acceptance decision with the
curated oracle and report accepted precision, coverage, verified completion,
and the per-case reasons before any product-gating work begins.
