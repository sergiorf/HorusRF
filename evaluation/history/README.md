# AI evaluation history

This directory contains durable, reviewable snapshots of selected live AI
evaluation runs. It exists outside `build/` so quality changes can be compared
across revisions even after local build artifacts are cleaned.

Current snapshots use `horusrf.ai-evaluation-history` schema version 2. Retained
legacy snapshots remain schema version 1. Snapshots retain:

- the dataset, oracle, comparison, prompt, model digest, settings, revision, and
  run timestamp;
- the evaluator's aggregate summaries; and
- per-model, per-case persistence counts across repetitions.

They deliberately omit generated scripts, prompts, explanations, and individual
repair payloads. Those details remain in the ignored full report under
`build/ai-evaluation/`. Each snapshot records that report's SHA-256 so a retained
raw artifact can be matched to its committed summary.

## Adding a run

Archive a completed schema-v1, schema-v2, schema-v3, or schema-v4 report with:

```powershell
.\scripts\archive-ai-evaluation.cmd `
  -ReportPath build\ai-evaluation\results-YYYYMMDD-HHMMSS.json
```

Alternatively, archive as part of a live run:

```powershell
.\scripts\run-ai-evaluation.ps1 -Repeat 3 -Archive
```

Review the new snapshot before committing it. Snapshot files are immutable: the
archiver refuses to overwrite an existing path. Commit gate, milestone, and
release-candidate runs; do not commit smoke tests, interrupted runs, or ad hoc
prompt exploration.

When comparing snapshots, match `dataset_version`, `comparison_version`,
`requirement_schema_version`, model identity, and settings. Differences in these
fields are experiment changes, not pipeline improvements.

Legacy schema-v1 reports predate prompt-configuration identity and the
requirement oracle. Their snapshots preserve those fields as `null`; do not
interpret unavailable requirement results as failures or zero successes.
Schema-v1 through schema-v3 reports also predate the interactive acceptance
decision. Their per-case acceptance and interactive-verification counts remain
`null`; schema-v4 snapshots retain allow/review/reject counts and the accepted
curated-requirement-success numerator.

## Retained runs

The initial history includes the two single-repetition M6Q.0 baseline runs, the
three-configuration M6Q.2 gate, and the two-limit M6Q.6 performance gate. M6Q.2
and M6Q.6 results are based on 17 cases repeated three times per model.
Requirement and semantic denominators exclude the unsupported case.

| Snapshot | Model | Final valid | Requirements | Semantic match | Median / P90 latency |
|---|---|---:|---:|---:|---:|
| `m6q0-20261021-190949-schema-v1.json` | 3B | 10/17 | unavailable | 5/16 | 13.662 / 24.180 s |
| `m6q0-20261021-190949-schema-v1.json` | 7B | 12/17 | unavailable | 9/16 | 17.672 / 52.372 s |
| `m6q0-20261021-193036-schema-v1.json` | 3B | 10/17 | unavailable | 5/16 | 10.876 / 22.762 s |
| `m6q0-20261021-193036-schema-v1.json` | 7B | 12/17 | unavailable | 9/16 | 19.634 / 58.726 s |
| `m6q2-direct-dsl-v1-r3.json` | 3B | 30/51 | 18/48 | 15/48 | 13.298 / 25.186 s |
| `m6q2-direct-dsl-v1-r3.json` | 7B | 36/51 | 27/48 | 27/48 | 17.470 / 54.522 s |
| `m6q2-direct-dsl-compact-v1-r3.json` | 3B | 27/51 | 6/48 | 6/48 | 5.994 / 14.053 s |
| `m6q2-direct-dsl-compact-v1-r3.json` | 7B | 36/51 | 27/48 | 21/48 | 9.212 / 33.332 s |
| `m6q2-direct-dsl-relevant-v1-r3.json` | 3B | 33/51 | 24/48 | 21/48 | 3.870 / 12.069 s |
| `m6q2-direct-dsl-relevant-v1-r3.json` | 7B | 27/51 | 21/48 | 15/48 | 20.695 / 36.388 s |
| `m6q6-direct-dsl-v1-t2048-r3.json` | 3B | 30/51 | 21/48 | 18/48 | 13.544 / 28.940 s |
| `m6q6-direct-dsl-v1-t2048-r3.json` | 7B | 36/51 | 30/48 | 30/48 | 18.807 / 53.328 s |
| `m6q6-direct-dsl-v1-t512-r3.json` | 3B | 30/51 | 21/48 | 18/48 | 16.038 / 32.646 s |
| `m6q6-direct-dsl-v1-t512-r3.json` | 7B | 36/51 | 30/48 | 30/48 | 18.197 / 53.266 s |

The baseline prompt is `direct-dsl-v1`. The compact and relevant configurations
are experimental comparisons, not successive improvements.

M6Q.8 adds the controlled dataset-2.0.0 structural-retrieval development matrix.
It uses 30 cases repeated three times per model. Requirement and semantic
denominators are 84 applicable runs per model; support outcomes retain their own
denominators. These are development results, not an independent holdout claim.

| Snapshot | Model | Final valid | Requirements | Semantic match | Median / P90 latency |
|---|---|---:|---:|---:|---:|
| `m6q8-controlled-direct-dsl-v1-r3.json` | 3B | 54/90 | 36/84 | 30/84 | 12.656 / 25.955 s |
| `m6q8-controlled-direct-dsl-v1-r3.json` | 7B | 56/90 | 44/84 | 41/84 | 18.590 / 43.002 s |
| `m6q8-controlled-direct-dsl-relevant-v1-r3.json` | 3B | 42/90 | 33/84 | 30/84 | 8.810 / 19.291 s |
| `m6q8-controlled-direct-dsl-relevant-v1-r3.json` | 7B | 54/90 | 36/84 | 30/84 | 17.993 / 32.076 s |
| `m6q8-controlled-structural-rag-k1-r3.json` | 3B | 48/90 | 42/84 | 30/84 | 5.044 / 11.584 s |
| `m6q8-controlled-structural-rag-k1-r3.json` | 7B | 69/90 | 66/84 | 54/84 | 10.541 / 21.237 s |
| `m6q8-controlled-structural-rag-k2-r3.json` | 3B | 48/90 | 33/84 | 27/84 | 5.021 / 13.845 s |
| `m6q8-controlled-structural-rag-k2-r3.json` | 7B | 75/90 | 63/84 | 54/84 | 8.214 / 18.414 s |
| `m6q8-controlled-structural-rag-k3-r3.json` | 3B | 42/90 | 30/84 | 24/84 | 8.166 / 14.360 s |
| `m6q8-controlled-structural-rag-k3-r3.json` | 7B | 75/90 | 69/84 | 60/84 | 9.448 / 22.591 s |
