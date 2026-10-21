# HorusRF AI generation quality improvement plan

**Status:** Proposed after repository review  
**Scope:** Local natural-language-to-HorusRF generation, validation, correction,
evaluation, and performance  
**Reviewed baseline artifact:** `build/ai-evaluation/results-20261021-193036.json`
from dataset 1.0.0 at revision `66e692f5d76db7e91273ca59fa8fdfa963e97c1d`

## 1. Outcome

Improve how reliably the existing provider-neutral C++20 authoring agent satisfies
explicit user requirements. Keep the current prompt builder, `LlmProvider`,
`AgentSession`, compiler validation adapter, Ollama adapter, Studio review/apply
workflow, golden dataset, and opt-in evaluator. Do not add an orchestration
framework, MCP, fine-tuning, or automatic execution.

The first changes must improve measurement and diagnosis. Prompt behavior must not
change until the reviewed baseline has a case-level failure record and the
evaluation can distinguish requirement violations from exact-golden comparison
differences.

## 2. Repository review

### 2.1 Existing components to reuse

| Existing component | Reuse |
|---|---|
| `frontend::get_language_spec()` and `frontend::get_language_capabilities()` | Authoritative grammar and supported compiler capabilities in prompts and support checks |
| `frontend::validate()` and `ir::lower()` | The only validity path; no AI-owned syntax or type rules |
| `semantic::AnalyzedProgram` and `ir::Program` | Typed, normalized source for program facts and requirement checks |
| `ai::compare_script_semantics()` | Exact structural comparison and detailed mismatch diagnostics, not the sole requirement-satisfaction oracle |
| `ai::numeric_fidelity_warnings()` | Seed for conservative extraction; replace raw spelling comparison with typed canonical quantities rather than adding a parallel extractor |
| `PromptBuilder` and curated examples | Deterministic prompt variants and capability-based example selection |
| `AgentSession` | Bounded calls, cancellation, validation, failure retention, and unchanged-invalid detection |
| `ProviderMetrics` and Ollama telemetry | Model load, prompt evaluation, generation duration, and token counts |
| `GoldenDataset` and evaluator | Versioned fixtures, offline validation, repeated live runs, and reports |
| Studio assistant service and panel | Explicit review/apply boundary; display new support and verification diagnostics here |

### 2.2 Corrections to the proposed design

1. **Calibration cannot generally be omitted.** The compiler requires exactly one
   calibration. Prompt experiments may remove copied comments and unnecessary
   intermediate derives, but every valid proposal still needs the required
   calibration structure.
2. **A fixed frequency is represented by a one-point frequency sweep.** The DSL
   requires a frequency sweep and a positive step even when start equals end.
   When the user did not specify the step, a verifier must not turn the arbitrary
   representational step into an explicit user requirement.
3. **Compiler capabilities do not provide a natural-language classifier.** They
   enumerate supported dimensions and operations. A small AI-layer recognizer may
   identify a phrase such as “sweep transmit power,” but support is decided by
   checking the recognized dimension against compiler metadata. Do not copy a
   list of supported dimensions into the AI layer.
4. **Exact script equivalence is not the same as requirement satisfaction.** The
   current comparator intentionally compares the full normalized IR. It can reject
   an extra harmless derived value or algebraically equivalent correction. It also
   ignores names that a prompt may explicitly require. Keep it as a useful strict
   signal, and add a requirement-oriented comparison.
5. **The response contract has no first-class unsupported result.** It accepts only
   `status: "proposal"`; evaluation searches explanation and warning text for
   words such as “cannot.” Known unsupported requests need a typed agent outcome
   or pre-generation support assessment rather than keyword scoring.
6. **The existing milestone name is already occupied.** Repository architecture
   documentation describes the shipped golden dataset and evaluator as M6. To
   avoid rewriting history, label this work “M6 quality hardening” (M6Q) or assign
   it M7. The sequence below uses M6Q identifiers while preserving the requested
   M6 scope.

### 2.3 Missing interfaces

- A compiler-backed `ProgramFacts` projection containing canonical reference
  power, ordered sweeps, measurements (including requested names), normalized
  derived expressions, assertions, and calibration facts.
- Typed requirement clauses with provenance and confidence:
  `SweepRequirement`, `ReferencePowerRequirement`, `MeasurementRequirement`,
  `AssertionRequirement`, and `CalibrationRequirement`.
- Per-clause outcomes: `satisfied`, `violated`, `unknown`, and `unsupported`, with
  a stable code and explanation.
- A request-level support assessment distinguishing supported, partially
  supported, unsupported, and uncertain requests.
- Attempt history in evaluation, including candidate, structured-response status,
  compiler diagnostics, semantic differences, and provider metrics for every
  call.
- Prompt-configuration identity and stable prompt-size measurements in reports.
- A first-class unsupported agent result. This should be added only after the
  preflight behavior is proven offline; it is evidence for a small contract
  extension, not a reason to redesign the agent.

## 3. Baseline audit

### 3.1 Artifact discrepancy

The saved report does not exactly match the numbers in the incoming proposal.
It reports median latency of 10.876 s for 3B and 19.634 s for 7B, and aggregate
output throughput of 36.315 and 15.732 tokens/s. The proposal states 13.7 s,
17.7 s, 35.8 tokens/s, and 17.3 tokens/s. First-valid, final-valid, and semantic
counts agree.

Do not combine these measurements. M6Q.0 must either locate the artifact that
produced the proposed latency figures or designate the saved report as the
reproducible baseline. Future reports must record a prompt-configuration ID,
dataset/comparison version, model digest when available, run order, repetition,
and warm/cold classification.

### 3.2 Failure classification from the saved report

The table classifies every non-success in the saved single-repetition run. A
“comparator limitation” means the generated source appears to satisfy the stated
request but fails full-program structural equality; it must be confirmed by the
new requirement oracle before changing any score.

| Model | Case | Observed cause | Classification |
|---|---|---|---|
| 3B | `frequency_sweep_named_power` | Uses reserved `correction` where the grammar expects a calibration identifier; repair later repeats it | Unsupported syntax; repair stagnation |
| 3B | `frequency_sweep_multiple_measurements` | Adds an intermediate error and negates it; correction is algebraically equivalent to expected | Likely comparator limitation |
| 3B | `frequency_sweep_derived_error` | Omits requested measurement name and reverses the requested `measured - reference` formula | Missing named operation; incorrect formula/sign |
| 3B | `frequency_sweep_power_assertion` | Omits the requested power-error assertion | Missing required assertion |
| 3B | `measurement_assertions` | Assertions are present, but calibration uses `output - reference` instead of its negation | Incorrect formula/sign |
| 3B | `temperature_sweep` | Emits an incomplete fixed-frequency sweep without range and step | Unsupported syntax; repair stagnation |
| 3B | `multiple_constraints` | Applies `abs()` and unary negation directly to absolute power | Invalid semantic type; repair stagnation |
| 3B | `unit_conversions` | Converts `-10000 mdBm` to `-10000 dBm` instead of `-10 dBm` | Incorrect numerical value/unit conversion |
| 3B | `single_frequency_point` | Omits the compiler-required frequency sweep | Missing required DSL scaffolding; repair stagnation |
| 3B | `derived_measurement_chain` | Uses `measure output_power` without `= power`, then refers to it | Unsupported measurement syntax/operation; repair stagnation |
| 3B | `temperature_first_multidimensional` | Uses absolute measured power as a calibration correction | Invalid semantic type/formula; repair stagnation |
| 3B | `fixed_frequency_power_sweep_unsupported` | Invents `sweep power` and never reports the limitation | Unsupported feature hallucination; repair stagnation |
| 7B | `frequency_sweep_power_assertion` | Omits the requested power-error assertion | Missing required assertion |
| 7B | `temperature_sweep` | Uses a different positive step for a one-point frequency sweep; user did not specify that step | Comparator/golden limitation |
| 7B | `multidimensional_sweep` | Uses `deltaC` for a temperature endpoint | Invalid semantic unit/type; repair stagnation |
| 7B | `calibration_surface` | Omits `= power` from the named measurement and later emits a malformed derive block | Unsupported syntax; repair stagnation |
| 7B | `multiple_constraints` | Changes strict “below” comparisons to `<=` for power and EVM | Incorrect comparison result |
| 7B | `single_frequency_point` | Repeatedly produces incomplete sweep or invalid measurement syntax | Unsupported syntax; ineffective repair |
| 7B | `derived_measurement_chain` | Omits `= power` and later omits `derive` before calibration | Unsupported syntax; repair stagnation |
| 7B | `fixed_frequency_power_sweep_unsupported` | Invents `sweep power` and never reports the limitation | Unsupported feature hallucination; repair stagnation |

This evidence supports the copying, omission, repair, unsupported-operation, and
token hypotheses, but it does not yet prove their relative contribution.

## 4. Requirement verification design

Use one compiler-backed fact projection and two independent sources of
requirements:

1. **Golden evaluation:** manually curated typed requirements stored with each
   case. This is the authoritative scoring oracle and avoids pretending that a
   deterministic extractor understands arbitrary prose.
2. **Interactive authoring:** conservative high-confidence extraction from the
   user's text. Unrecognized or ambiguous clauses remain `unknown` and are shown
   to the user; they do not become invented defaults.

Both sources feed the same verifier:

```text
natural-language request ----> conservative extractor ----\
                                                        requirement clauses
golden metadata -------------> curated requirements -----/
                                                               |
validated source -> frontend validation -> IR -> ProgramFacts  |
                                               |               |
                                               +---- verifier -+
```

`ProgramFacts` should be an AI/evaluation-owned read-only projection built from
`ir::Program`; it must not be a second semantic model or compiler. Canonical
domain quantities provide unit equivalence. Expression facts should reuse the
normalization already implemented by semantic comparison.

Initial interactive extraction should cover only:

- explicit frequency/temperature ranges and steps;
- explicit fixed frequency;
- explicit reference power;
- explicit power, EVM, and frequency-error measurements and requested names;
- simple comparison assertions with explicit thresholds;
- explicitly requested sweep order and calibration indexing.

Do not initially extract arbitrary derived formulas. Golden metadata may express
them because it is curated. Extend interactive extraction only with independently
tested, unambiguous patterns.

Verification policy:

- compiler validity remains necessary for every proposal;
- a violated or unsupported explicit clause is never reported as verified;
- `unknown` is not counted as satisfied in evaluation;
- during the first interactive release, outcomes augment diagnostics and warnings;
- Studio apply remains an explicit source-edit action, not execution authority;
- whether known violations disable Apply is a later product decision based on
  false-positive measurements. Unknown alone must not disable Apply initially.

## 5. Actionable M6 quality-hardening roadmap

### M6Q.0 — Reproducible baseline and diagnostic report

**Changes**

- Extend evaluation schema to version 2 while retaining a version-1 reader if a
  reader exists or documenting that reports are write-only artifacts.
- Record every candidate, parse result, compiler diagnostics, semantic
  differences, call purpose (generation/compiler repair/requirement correction),
  provider metrics, and final warnings.
- Record prompt configuration ID, prompt UTF-8 size by section, provider-reported
  input tokens, model digest when Ollama supplies it, run order, and warm/cold
  status.
- Add `docs/ai-generation-quality-baseline.md` with the confirmed artifact and
  case-level diagnosis. Do not change prompts in this milestone.

**Affected components**

`include/horusrf/ai/evaluation.hpp`, `src/ai/evaluation.cpp`, agent/evaluator
observation seams, `src/ai_eval/main.cpp`, evaluation tests, and documentation.

**Tests**

Fake-provider attempt history, invalid diagnostics, semantic-difference
serialization, schema fields, cancellation, missing telemetry, and deterministic
clock tests. Run focused AI tests and the full unfiltered CTest suite.

**Benefit / risk**

Provides trustworthy evidence and explains repair behavior. Main risk is report
size; keep human tables concise and detailed data in JSON.

**Exit**

All 17 saved cases have an evidence-backed classification, and the latency
discrepancy is resolved or explicitly preserved as two separate runs.

### M6Q.1 — Requirement oracle for evaluation

**Changes**

- Extract shared `ProgramFacts` from validated/lowered IR.
- Add versioned, manually curated requirement metadata to the existing 17 cases.
- Implement per-clause verification with canonical quantities and stable paths.
- Retain exact semantic comparison as a secondary strict metric.
- Reclassify the algebraically equivalent correction and unspecified one-point
  sweep step cases using the requirement oracle.

**Affected components**

New small AI fact/verification headers and sources, semantic comparison internals,
golden metadata loader/schema, evaluator/report, golden fixtures, CMake, and tests.

**Tests**

Required detection tests: missing assertion, missing measurement, wrong bound,
wrong step, wrong reference power, known unit conversion, strict versus inclusive
comparison, requested measurement name, algebraically equivalent calibration, and
unspecified one-point step. Use `unit_conversions` and
`frequency_sweep_power_assertion` as regressions.

**Benefit / risk**

Makes the score correspond to explicit requirements rather than one complete
reference implementation. Risk is accidentally building another compiler;
mitigate by projecting only compiler-normalized facts and keeping curated clauses
small.

**Exit**

Every supported case reports clause outcomes and a requirement-satisfaction
status. Exact comparison and requirement satisfaction are separate fields.

### M6Q.2 — Measured prompt experiment

**Changes**

- Introduce named prompt configurations without changing the default initially:
  A current prompt, B current prompt with compact examples/comments, and C
  deterministic capability-relevant examples.
- Select examples with explicit feature tags; no model-based selector.
- Keep mandatory calibration syntax in each usable proposal while avoiding
  unrelated intermediate values and copied comments.
- Add direct instructions to preserve explicit values, operations, assertion
  strictness, names, and sweep order.
- Measure prompt sections by UTF-8 bytes/characters offline and total input tokens
  from provider telemetry live. Do not add a tokenizer dependency merely to
  estimate tokens.

**Affected components**

`include/horusrf/ai/prompt.hpp`, `src/ai/prompt.cpp`, embedded example metadata,
prompt tests, evaluator settings/report, and CLI options for experiments.

**Tests**

Deterministic selection for frequency, temperature, measurements, assertions,
calibration, and modification; no irrelevant example; all examples compile; same
input/configuration produces identical messages.

**Benefit / risk**

Directly tests the copying and prompt-size hypotheses. The risk is overfitting 17
cases; choose the default only after repeated results and retain all configurations
for comparison.

**Exit**

Run A/B/C on the unchanged suite with identical settings and 3–5 repetitions.
Document compiler validity, requirement satisfaction, exact comparison, repairs,
input/output tokens, median/P90 latency, and variability. Adopt a configuration
only if it has no material correctness regression.

### M6Q.3 — Conservative interactive requirement extraction

**Changes**

- Replace spelling-only numeric fidelity checks with typed high-confidence clauses
  and canonical domain quantities.
- Preserve source spans/text, confidence, ambiguity, and uncheckable clauses.
- Verify clauses against `ProgramFacts` after successful compilation.
- Add the results to `ScriptProposal` diagnostics without treating `unknown` as
  satisfied or as a compiler error.

**Affected components**

AI contracts, new requirement extraction/verification component, agent,
Studio presentation, and offline unit tests.

**Tests**

Equivalent units, signs, repeated values, en/em-dash punctuation, ambiguous
phrases, unsupported unit spelling such as `mdBm`, fixed-frequency representation,
named measurements, thresholds, and false-positive controls.

**Benefit / risk**

Catches valid-but-wrong proposals before they are presented as trustworthy. Regex
overreach is the main risk; prefer `unknown` and narrow patterns.

**Exit**

The six required mismatch classes are detected with no known false positive in
the original suite, and Studio distinguishes compiler validity from requirement
verification.

### M6Q.4 — Targeted correction and repair observability

**Changes**

- Map stable compiler diagnostic codes to compact compiler-owned syntax/semantic
  context already present in language metadata; avoid a second grammar handbook.
- Include the exact diagnostic span and nearby candidate lines, original request,
  and an instruction to preserve unaffected statements.
- Track exact repeated candidates plus stable hashes and diagnostic-set changes.
- Stop when the candidate and diagnostics repeat; report “no progress” separately
  from call-limit exhaustion.
- Add one distinguishable requirement-correction path for high-confidence
  violations, within a single shared total-call budget. Do not call it compiler
  repair.

**Affected components**

Prompt repair request, `AgentSession`, attempt-history contracts, evaluator, and
fake-provider tests.

**Tests**

Targeted prompts for parser and semantic codes seen in the baseline; preservation
of valid statements; repeated candidate; changed text with unchanged diagnostics;
requirement correction; total budget; cancellation; malformed response; and final
diagnostic retention.

**Benefit / risk**

Avoids known wasted calls and makes improvement measurable. Over-specific repair
templates may bias models; begin with the few recurring stable diagnostics.

**Exit**

No unbounded loop is possible, all calls have a recorded purpose, and the saved
repeated-invalid cases stop or improve for an explainable reason.

### M6Q.5 — Unsupported and partial-support handling

**Changes**

- Add a narrow preflight recognizer for explicit sweep dimensions and other
  high-confidence operations.
- Resolve recognized operations against `get_language_capabilities()`.
- Return a typed unsupported outcome for an entirely known-unsupported request
  before provider inference.
- For partial support, enumerate supported, unsupported, and uncertain clauses.
  Do not silently produce a “successful” partial script.
- Replace evaluation's free-text keyword heuristic with the typed outcome.

**Affected components**

AI contracts, preflight component, agent, evaluator, Studio messaging, and tests.

**Tests**

Power sweep, frequency sweep, temperature sweep, fixed frequency, mixed
frequency-plus-power request, ambiguous “power range,” and capability metadata
changes. Verify that a known unsupported request makes zero provider calls.

**Benefit / risk**

Improves safety and removes futile repairs. False unsupported classifications are
the key risk; only high-confidence matches may bypass generation.

**Exit**

The existing unsupported power-sweep case is recognized structurally, makes no
repair calls, and Studio gives an understandable limitation.

### M6Q.6 — Performance tuning after correctness gates

**Changes**

- Report cold and warm latency separately, and per-call model load, prompt
  evaluation, generation, compiler, and verification durations.
- Evaluate compact examples, comment-free output, smaller structured explanations,
  and lower output limits using the chosen prompt configuration.
- Preserve both model choices; do not add automatic fallback or mid-request model
  switching.

**Affected components**

Prompt settings, evaluator timing/report, CLI documentation, and possibly Ollama
request settings already behind the provider adapter.

**Tests**

Offline serialization and settings bounds; opt-in live A/B runs for performance.
Provider error and cancellation tests remain mandatory.

**Benefit / risk**

Reduces interactive time without obscuring quality. Truncation is the principal
risk; output-limit experiments must report output-limit errors distinctly.

**Exit**

Warm median and P90 are reproducible, and no accepted optimization materially
reduces supported-case requirement satisfaction.

### M6Q.7 — Dataset expansion and trend reporting

**Changes**

- Expand only after requirement metadata and reporting stabilize.
- Add 13–23 independently curated cases for units, negatives, strictness, names,
  sign conventions, paraphrases, partial support, ambiguity, and combined clauses.
- Keep dataset 1.0.0 cases available; version metadata and scoring logic
  independently.
- Run 3–5 repetitions and report per-case persistence as well as aggregate median
  success, variability, median/P90 latency, repair/correction rate, and typed
  unsupported recognition.

**Affected components**

Golden fixtures/manifest/README, loader validation, evaluator summaries, and
documentation.

**Tests**

Schema migration/version rejection, complete discovery, curated expected-source
validation, clause completeness, repetitions, denominator correctness, and
trend-table serialization.

**Benefit / risk**

Reduces overfitting and makes trends credible. Dataset drift is the risk; require
human review and never promote model output automatically.

**Exit**

The suite contains roughly 30–40 reviewed cases, the original cases remain
addressable, and reports compare revisions without conflating changes in dataset,
oracle, prompt, or model.

## 6. Dependencies and review boundaries

```text
M6Q.0 evidence
   |
   v
M6Q.1 requirement oracle ---> M6Q.2 prompt experiment
   |                              |
   v                              v
M6Q.3 interactive verification -> M6Q.4 targeted correction
   |
   +---------------------------> M6Q.5 unsupported handling
                                      |
                                      v
                              M6Q.6 performance
                                      |
                                      v
                              M6Q.7 dataset expansion
```

M6Q.0 and M6Q.1 are correctness prerequisites for selecting a prompt. M6Q.4
depends on verification only for requirement-directed correction; compiler repair
can be reviewed independently within the same milestone. Performance claims wait
until the quality oracle is stable. Dataset expansion waits until its metadata
would not immediately require migration.

Each milestone is a separate reviewable change. No milestone may modify runtime,
device, simulator, or output behavior. Normal tests remain offline and
deterministic; live Ollama evaluation is explicit only.

## 7. Evaluation rules and targets

- Report compiler validity over applicable generated proposals, not unsupported
  requests.
- Report requirement satisfaction over supported cases and count `unknown`
  separately; never count it as success.
- Report exact semantic comparison as a diagnostic metric, not as the only
  semantic-success definition.
- Keep infrastructure/provider failures outside model-quality numerators while
  reporting them visibly.
- Separate unsupported, partially supported, ambiguous, and supported cases.
- Never infer repeatability from temperature zero or a one-repetition run.
- Separate cold latency from warm latency and preserve per-attempt totals.
- Pin dataset version, requirement-schema version, comparison version, prompt
  configuration, model tag/digest when available, settings, revision, and machine
  context for every live report.

Provisional targets remain:

| Metric | Target |
|---|---:|
| Compiler validity of applicable final proposals | 100% |
| Satisfied explicit requirements on supported golden cases | at least 90% |
| Known unsupported recognition | at least 95% |
| Median warm 3B latency | under 10 s if measurement supports it |
| Unbounded calls | 0 |

Do not use the 90% target to weaken the oracle or curate away difficult cases.

## 8. Simplifications deliberately chosen

- Curated golden requirements come before general natural-language extraction.
- `ProgramFacts` is a projection over existing IR, not a new intermediate
  language.
- Exact comparison remains available instead of being rewritten prematurely.
- Example selection is tagged and deterministic, not embedding-based or
  model-selected.
- Unsupported preflight starts with a few explicit patterns whose support is
  resolved from compiler metadata.
- Prompt size uses provider telemetry and simple stable byte/character counts;
  no tokenizer library is added.
- Requirement correction shares one bounded call budget with compiler repair.
- Structured intent generation remains a future experiment only if repeated
  requirement-oriented results show direct DSL generation remains inadequate.

## 9. First review slice

Start with M6Q.0 only. It changes reporting and documentation, not model behavior.
The review should include:

1. evaluation schema v2 and deterministic fake-provider tests;
2. the 17-case baseline diagnosis with actual compiler diagnostics and semantic
   differences;
3. reconciliation of the two latency/throughput result sets;
4. a design note for `ProgramFacts` and curated requirement metadata sufficient
   to begin M6Q.1.

Do not begin prompt, repair, or Studio behavior changes in this slice.
