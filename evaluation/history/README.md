# AI evaluation history

This directory contains durable, reviewable snapshots of selected live AI
evaluation runs. It exists outside `build/` so quality changes can be compared
across revisions even after local build artifacts are cleaned.

Snapshots use `horusrf.ai-evaluation-history` schema version 1. They retain:

- the dataset, oracle, comparison, prompt, model digest, settings, revision, and
  run timestamp;
- the evaluator's aggregate summaries; and
- per-model, per-case persistence counts across repetitions.

They deliberately omit generated scripts, prompts, explanations, and individual
repair payloads. Those details remain in the ignored full report under
`build/ai-evaluation/`. Each snapshot records that report's SHA-256 so a retained
raw artifact can be matched to its committed summary.

## Adding a run

Archive a completed schema-v1 or schema-v2 report with:

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

## Retained runs

The initial history includes the two single-repetition M6Q.0 baseline runs and
the three-configuration M6Q.2 gate. M6Q.2 results are based on 17 cases repeated
three times per model. Requirement and semantic denominators exclude the
unsupported case.

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

The baseline prompt is `direct-dsl-v1`. The compact and relevant configurations
are experimental comparisons, not successive improvements.
