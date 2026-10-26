# HorusRF M6Q.9 guardrail evaluation plan

**Status:** Slice 1 offline evidence replay and Slice 2A acceptance observability
plus name-fidelity coverage completed on 2026-10-26. A controlled schema-v4
replay is next. No product Apply-gate or default-prompt change is approved by
this plan. See the
[Slice 1 evidence report](ai-generation-guardrail-evidence.md).

## 1. Decision to make

This experiment asks whether a bounded, compiler-backed guardrail policy can turn
the promising M6Q.8 structural-retrieval result into a safer and more useful
authoring workflow without hiding failures or weakening the requirement oracle.

The primary question is not whether HorusRF can reject bad text. It is whether the
complete policy can increase **verified completion** while maintaining high
**accepted-proposal precision**, useful coverage, bounded provider calls, and
acceptable latency.

Guardrails remain inside the AI authoring and evaluation layers. They may inspect
support assessments, provider responses, compiler diagnostics, `ProgramFacts`,
requirement outcomes, retrieval provenance, and prior attempts. They must not
execute generated programs, construct runtime devices, depend on simulator
internals, add a second HorusRF parser, or bypass the normal frontend and IR path.

## 2. Evidence entering the experiment

The controlled dataset-2.0.0 M6Q.8 run used temperature 0, 2,048 output tokens,
a 120-second provider timeout, three repetitions, and the existing shared budget
of three repair/correction calls. The local model digests, corpus digest, ranking
identity, revision, and complete attempts are retained in the schema-v3 reports.

| Model and policy | Requirements | Strict semantic match | Final compiler-valid | Median latency |
|---|---:|---:|---:|---:|
| 3B, structural RAG K=1 | 42/84 (50.0%) | 30/84 (35.7%) | 48/90 (53.3%) | 5.0 s |
| 7B, direct DSL baseline | 44/84 (52.4%) | 41/84 (48.8%) | 56/90 (62.2%) | 18.6 s |
| 7B, structural RAG K=3 | 69/84 (82.1%) | 60/84 (71.4%) | 75/90 (83.3%) | 9.4 s |

The K=3 7B result is the quality control for M6Q.9. K=1 remains the 3B control.
The 30-case suite is a development benchmark influenced by prior failure analysis;
it supports engineering decisions but not a final unseen-request accuracy claim.

The remaining 7B K=3 failures are persistent across repetitions and cluster in
derived-measurement correction, calibration sign, partial/ambiguous requests,
inclusive frequency-error handling, and combined limits. This makes unchanged
blind retries an invalid intervention: a rescue must change actionable evidence,
retrieval context, or model routing.

## 3. Outcomes and denominators

The evaluator must report these outcomes separately:

- **Verified completion:** a surfaced proposal that passes the compiler and the
  independent curated requirements, divided by every supported evaluation run.
  Invalid output, rejected output, and exhausted rescue are failures here.
- **Accepted-proposal precision:** curated-requirement success among proposals the
  interactive guardrail policy would allow a user to apply.
- **Coverage:** surfaced proposals divided by supported evaluation runs.
- **Safe rejection:** a typed, explained refusal or blocked proposal. This is a
  safety success but never a generation-quality success.
- **Support handling:** supported, partial, unsupported, and uncertain outcomes
  retain separate denominators.
- **Operational success:** verified completion including provider and infrastructure
  failures in the denominator. Model-quality tables may additionally exclude
  infrastructure failures, but both views must be visible.

The acceptance decision must use the ordinary interactive extraction and
verification path. Its precision is scored against independently curated golden
requirements. It must not use the golden verdict to decide whether to accept;
that would make the measurement tautological.

Every result also reports strict semantic comparison, first/final validity,
compiler-repair and requirement-correction yield, no-progress stops, rescue yield,
unnecessary changes to already-correct clauses, provider-call count, input/output
tokens, retrieval context, median/P90 latency, and cold/warm status.

## 4. Guardrail policies to compare

Each policy has a stable ID and is evaluated as a cumulative step only after its
individual effect is understood.

| ID | Policy | Maximum behavior |
|---|---|---|
| `g0-current` | Existing support preflight, strict response schema, compiler validation, requirement verification, shared repair/correction budget, and no-progress stop | At most 4 provider calls: initial generation plus 3 repair/correction calls |
| `g1-acceptance` | `g0` plus an explicit three-state Apply gate: allow, review required, or reject | No extra provider call; at most 4 total |
| `g2-reserved-correction` | `g1` plus one reserved requirement-correction call when compiler repair consumed the shared budget but a novel, high-confidence violation remains | At most 5 provider calls total |
| `g3-alternate-rescue` | `g2` plus one fresh generation from the original instruction after `no_progress`, excluding the first retrieval selection and using the next non-conflicting ranked context | One fresh-start rescue; at most 6 provider calls total |
| `g4-cascade` | Try 3B K=1, apply the same gate, and escalate a rejected supported request to 7B K=3 | At most one model escalation and 10 provider calls across both bounded sessions; never merge candidates |

`g3` must not feed an invalid candidate back as an authoritative example. The
fresh attempt receives the original request, stable compiler-owned language
context, the failed outcome summary, and different validated retrieval evidence.
If no distinct suitable example exists, it stops rather than performing the same
call again.

`g1` allows Apply only for compiler-valid proposals whose extracted
high-confidence requirements are all satisfied and whose support assessment has
no silently unsupported clause. A violated or high-confidence unknown outcome is
rejected. Low-confidence, ambiguous, partial, or uncheckable clauses produce
`review_required`, not `allow`; only `allow` counts in accepted-proposal precision.

The initial experiment deliberately excludes automatic source rewriting,
compiler-generated program skeletons, embeddings, unconstrained retries, and
execution-based validation. Each would confound the guardrail result or cross an
architectural boundary and requires a separate approved experiment.

## 5. Evaluation sequence

### Phase 0 - freeze and replay existing evidence

1. Archive and review all five completed M6Q.8 reports before changing behavior.
2. Classify every unsuccessful 7B K=3 run by first failing boundary, repeated
   diagnostic/violation, retrieved example, and whether an unused ranked example
   could provide materially different evidence.
3. Replay `g1-acceptance` against recorded attempts without provider calls.
4. Simulate `g4-cascade` by pairing the recorded 3B K=1 and 7B K=3 outcomes by
   case and repetition. Label latency and cache effects as simulated, not measured.

Phase 0 establishes the achievable precision/coverage trade-off and prevents code
changes that cannot address an observed failure.

### Phase 1 - deterministic policy and reporting support

- Add value-type policy settings and stable policy IDs; do not introduce a
  strategy framework.
- Keep one total bounded provider-call counter with the limits above and
  additionally report calls permitted by the reserved-correction and fresh-rescue
  rules.
- Record the interactive acceptance verdict, reason codes, rescue trigger,
  previous and replacement retrieval IDs, model escalation, and final source
  provenance in an additive report schema revision.
- Add scripted-provider tests for every transition, cancellation point, exhausted
  budget, repeated candidate, absent alternate example, provider failure, and
  deterministic serialization.
- Confirm that `g0-current` produces byte-for-byte identical requests and the same
  outcomes as the existing path.

### Phase 2 - development ablation

Run `g0`, `g1`, `g2`, and `g3` separately with 7B K=3 on the unchanged 30-case
development suite. Run the cascade separately with 3B K=1 and 7B K=3. Use three
repetitions initially and five for any apparent improvement near the promotion
threshold.

Pin dataset and case order, expected programs, model tags and digests, corpus and
ranking digests, revision, temperature, output limit, timeout, and GPU execution.
The runner must fail before the benchmark if either model is not fully GPU-backed,
and the report must retain processor/backend identity. Do not mix CPU and GPU
measurements or silently resume a partial run under a different backend.

Use paired per-case comparisons. Report gains and regressions by case family, not
only aggregate totals. A variant that improves the mean by repeatedly fixing one
family while regressing numeric fidelity, assertion strictness, calibration sign,
or support classification does not pass.

### Phase 3 - independent acceptance benchmark

Freeze a family-separated holdout before expected programs are visible to corpus,
ranking, prompt, or policy authors. Target at least 100 supported cases where
feasible, plus separately scored partial, unsupported, and ambiguous requests.
Paraphrases from one request family remain in one split.

Run the selected control and guardrail policy for at least three repetitions.
Publish point estimates and 95% confidence intervals for verified completion,
accepted precision, coverage, support handling, calls, and latency. The development
suite and holdout must remain separate in every table and claim.

## 6. Promotion gates

A guardrail policy is eligible for a product review only if all applicable gates
pass:

| Gate | Required result |
|---|---:|
| Verified completion on supported holdout runs | at least 90%, with no denominator exclusions for invalid or rejected model output |
| Improvement over `g0-current` | at least 5 percentage points and positive paired evidence across more than one case family |
| Accepted-proposal precision | 100% point estimate and at least 95% lower confidence bound |
| Known unsupported recognition | at least 95% |
| Silent acceptance of a known unsupported clause | 0 |
| Unbounded calls | 0 |
| Fresh-start rescues | at most 1 per request |
| P90 latency regression | no more than 50% unless separately approved for a quality-first mode |

The 90% target must not be reached by weakening requirements, removing difficult
cases, treating rejection as generation success, or counting partial support as
fully satisfied. If no policy passes, retain the current behavior and publish the
measured gap and limiting failure families.

The cascade has an additional gate: it must materially reduce 7B invocations or
latency without reducing verified completion by more than one percentage point
relative to the selected 7B-only policy. Quality-first and fast modes may receive
different explicit policy IDs; there is no hidden automatic default switch.

## 7. Review slices

1. **Evidence report:** archive M6Q.8, produce failure taxonomy, offline gate and
   cascade replay, and approve the intervention hypotheses. Completed on
   2026-10-26; the replay rejects reserved correction and cascade as immediate
   work, and keeps alternate rescue gated pending ranking observability.
2. **Acceptance gate:** implement `g1`, report provenance, and deterministic tests;
   no generation behavior change.
3. **Reserved correction:** implement and evaluate `g2` independently.
4. **Alternate rescue:** implement and evaluate `g3` only if Phase 0 shows a
   materially different validated retrieval candidate for recurring failures.
5. **Cascade:** implement and evaluate `g4` only after the per-model policies are
   stable.
6. **Independent holdout:** freeze fixtures, run the final comparison, and make an
   explicit promote/reject decision.

Every implementation slice keeps normal CTest offline, deterministic, and free of
hardware or network dependence. Cross-layer changes require the full unfiltered
CTest suite. Live Ollama runs remain explicit and write only under `build/` until
a complete report has been reviewed and archived.

## 8. Claims boundary

The current defensible statement is that structural retrieval with K=3 achieved
82.1% requirement satisfaction for the local 7B model on the controlled
development benchmark. It is not yet an unseen-request or product reliability
claim.

After M6Q.9, report three numbers rather than one: verified completion, accepted
precision, and coverage. A useful final statement would distinguish direct model
quality from system behavior, for example: "X% completed with verified
requirements, Y% of surfaced proposals were correct, and Z% were safely blocked."
Use that form only with the measured holdout values.
