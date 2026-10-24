# HorusRF M6Q.8 structure-aware RAG experiment

**Status:** Phase 0 design, ready for review. Slices 1-4 and all live model runs
are not yet implemented. The embedding and compiler-skeleton phases are gated
follow-up experiments, not part of the initial implementation.

**Design inspection revision:** `50b4d1bf4ed1b9f76df714221df4f99c252896fe`
(2026-10-23). The comparison run below was produced at
`604bcfac1b7fb337fc8607f56d65d32c9d0ee323`.

## 1. Question and decision rule

This experiment asks whether retrieving independently authored,
compiler-validated HorusRF examples by requested program structure improves
local Qwen 2.5 Coder 3B and 7B authoring quality over both the unchanged
`direct-dsl-v1` baseline and the existing `direct-dsl-relevant-v1` feature
selector.

Retrieval is an experimental prompt input. It does not define language
capabilities, bypass compiler validation, relax requirement verification, or
execute a generated program. It remains disabled by default unless controlled
measurements justify a separate promotion decision.

The primary outcome is requirement satisfaction on supported, previously unseen
requests. Compiler validity, strict normalized-IR equality, accepted-proposal
precision, coverage, repair incidence, and latency remain separate outcomes. A
result is not successful merely because valid syntax, a subset of easy cases, or
the few accepted proposals improve.

The initial experiment stops after Slice 4 if structural retrieval does not
reproducibly outperform the existing feature selector. Embeddings and validated
skeletons require explicit follow-up decisions based on the observed failure
categories.

## 2. Frozen comparison evidence

The October 23 dataset-2.0.0 baseline is archived as
`evaluation/history/20261023T125128Z-direct-dsl-v1-604bcfac1b7f.json`. It used
temperature 0, 2,048 maximum output tokens, a 120-second timeout, three shared
repair/correction calls, 30 cases in three repetitions, comparison
`semantic-ir-v2`, and requirement schema `golden-requirements-v2`.

| Metric | Qwen 3B | Qwen 7B |
|---|---:|---:|
| First-attempt compiler-valid | 45/90 | 50/90 |
| Final compiler-valid | 54/90 | 56/90 |
| Requirements satisfied | 36/84 (42.9%) | 44/84 (52.4%) |
| Strict semantic matches | 30/84 | 41/84 |
| Median / P90 latency | 12.0 / 25.7 s | 17.4 / 41.8 s |
| Repair rate | 53.3% | 41.1% |
| Requirement-correction rate | 6.7% | 6.7% |

The recorded model digests are:

- `qwen2.5-coder:3b`:
  `f72c60cabf6237b07f6e632b2c48d533cef25eda2efbd34bed21c5e9c01e6225`;
- `qwen2.5-coder:7b`:
  `dae161e27b0e90dd1856c8bb3209201fd6736d8eb66298e75ed87571486f4364`.

M6Q.2 remains an essential control. `direct-dsl-relevant-v1` improved 3B but
regressed 7B, while compacting all examples regressed 3B. Consequently, fewer
examples or keyword overlap alone is not evidence for RAG. The new system must
measure structural coverage and regressions independently for both model sizes.

Before every live M6Q.8 run, the existing model-list operation must confirm the
installed identifiers and capture the returned digests. A digest mismatch is an
experiment change and must be reported. The scripts must never pull a model or
alter the local Ollama installation.

## 3. Phase 0 implementation findings

### Prompt construction

`PromptBuilder` creates a provider-neutral `LlmRequest` containing a system
message, optional user/assistant few-shot pairs, and the final user request. The
system message contains the strict JSON proposal contract, the embedded
authoritative ANTLR grammar, compiler language capabilities, units, functions,
constraints, and additional mandatory semantic rules. Ollama receives the same
strict response schema through `LlmRequest::json_schema`.

`PromptConfiguration` currently has three stable identities:

- `direct-dsl-v1` includes every applicable curated example unchanged;
- `direct-dsl-compact-v1` includes every applicable example with comments
  removed and a compact explanation;
- `direct-dsl-relevant-v1` includes up to two compact examples selected by the
  current feature heuristic.

The baseline must remain the default and its messages must remain byte-for-byte
unchanged. Before prompt code is refactored, an exact-message regression fixture
will pin representative baseline generation, modification, repair, and
requirement-correction requests. The configuration ID remains
`direct-dsl-v1`.

The current `requested_features()` and `relevant_example_names()` functions are
private prompt implementation details. The former lowercases the request,
always adds frequency and calibration, and detects temperature, power, EVM,
frequency error, assertions, and modification through fixed phrases. The latter
greedily selects at most two examples by uncovered feature count, breaks useful
ties in favor of the example with fewer tags, and otherwise follows curated
example order. It does not model sweep order, assertion operators, derived
dependencies, numeric constraints, or calibration algebra.

### Orchestration and correction

`AgentSession` builds the initial request, performs the existing conservative
support assessment, and rejects wholly unsupported requests without a model
call. Each model response is parsed under the strict proposal schema and compiled
through the existing validation adapter. Invalid candidates receive the existing
diagnostic-focused repair prompt. Compiler-valid candidates are projected to
`ProgramFacts` and checked against conservatively extracted requirements;
high-confidence violations receive the existing targeted correction prompt.
The common repair/correction budget, repeated-candidate check, unchanged
diagnostic check, cancellation, and timeout behavior must not change in the
RAG-only experiment.

### Evaluation, dataset, and Studio

For every model/repetition/case, the evaluator currently constructs its own
`PromptBuilder`, wraps the provider to capture each request and response, and
runs the ordinary `AgentSession`. Schema-v2 reports retain full generated source,
attempt diagnostics, prompt UTF-8 sizes, compiler and verification timing,
provider timing and token telemetry, requirement outcomes, semantic differences,
run order, warm/cold classification, and provider identity. Compact history
snapshots retain aggregate and per-case trends and the full report hash.

Dataset 2.0.0 has 30 ordered cases: supported and partially supported cases use
compiler-valid semantic expectations and typed requirement selectors; unsupported
and uncertain cases carry explicit expected support classifications and are not
silently included in the applicable correctness denominator.

Studio constructs `PromptSettings` with only the selected model and timeout, so
it receives the default `direct-dsl-v1` configuration. The worker-thread service,
review/apply workflow, and execution separation need no change for the initial
evaluator-only experiment. Product exposure of RAG is a later promotion task.

## 4. Proposed extension shape

The new code remains C++20, Qt-independent, local, and deterministic outside the
existing opt-in generation calls. It reuses the compiler, support assessment,
requirement extraction, prompt, agent, evaluation, and CMake boundaries.

### 4.1 Corpus layout and identity

Use a physically separate corpus:

```text
ai/retrieval/
  manifest.json
  examples/
    <example-id>/
      request.txt
      program.hrf
      metadata.json
```

The manifest supplies a versioned schema, corpus ID/version, deterministic
example order, and content digest. Each metadata record supplies a unique ID,
category, typed operations, ordered structural steps, sweep dimensions and
order, dependency information, comparison strictness where applicable,
complexity, and provenance. Metadata describes the checked-in program; it is not
an alternative language definition.

Start with 20-30 structurally distinct examples covering basic and one-point
frequency characterization, named measurements, frequency/temperature order,
derived power error and dependency chains, strict and inclusive assertions,
multiple assertions, calibration sign, unit conversion, and combined operations.
Numerical clones do not count as coverage. Existing examples may be included only
when their independent provenance is recorded and they do not reproduce a held-out
answer.

The loader and validator will enforce exact JSON shape, required fields, unique
IDs, complete manifest/directory agreement, deterministic ordering, valid UTF-8,
and successful full compiler validation of every program. Corpus loading never
contacts a provider, runtime, simulator, or device.

### 4.2 Leakage policy

The current 30-case dataset is a **development benchmark** for M6Q.8. Its failure
patterns and categories have already influenced this experiment proposal, so it
cannot establish final unseen-case performance even if authors avoid opening its
expected scripts while writing the corpus.

Corpus validation will compare corpus entries both internally and against any
declared evaluation dataset using:

1. normalized exact request equality;
2. deterministic lexical similarity to flag likely prompt paraphrases;
3. exact source equality after whitespace/comment normalization;
4. compiler-derived normalized program structure and expressions to flag equal
   or near-equal solutions; and
5. a generated review report for human disposition of near matches.

Common required scaffolding is not leakage by itself. Automatic checks must
distinguish shared syntax from equality of requested values, operation sequence,
dependency graph, assertions, sweep order, and calibration expression. Suspected
paraphrases are review failures, not silently accepted or silently deleted.

Any case known to have influenced corpus authoring or rank tuning stays in the
development split. Final acceptance requires newly authored prompt families,
frozen before their expected scripts are exposed to corpus authors or tuning.
Related paraphrases remain in one family and cannot cross the development/test
boundary.

### 4.3 Query analysis and retrieval

Add a small retrieval module, initially represented by
`include/horusrf/ai/retrieval.hpp`, `src/ai/retrieval.cpp`, and an owning offline
test target. Prefer value types and a concrete `ExampleRetriever`; do not add a
framework or virtual strategy hierarchy before a second implementation exists.

The query analyzer must reuse `assess_instruction_support()` and
`extract_requirements()` for capabilities they already recognize. It may add a
typed retrieval projection for ordered sweeps, measurement operations, named
values, derivation chains, assertion operators, calibration, fixed-frequency
representation, units, and complexity. Unknown evidence remains unknown. It must
not parse HorusRF or invent a second language capability table.

The existing legacy prompt selector remains behaviorally frozen. Shared text
helpers may be extracted only after exact regression tests prove that all three
existing configurations are unchanged.

Ranking version `structural-rag-ranking-v1` will combine:

- ordered structural and dependency compatibility;
- operation and typed-feature coverage;
- penalties for conflicting or unsupported operations;
- conservative lexical similarity; and
- complexity compatibility.

Structure and operation coverage take precedence over lexical similarity. Initial
coefficients are hypotheses, recorded with the report, and may be tuned only on
development cases. Ties resolve deterministically by manifest order and example
ID. Selection supports K=1, 2, or 3, a UTF-8 byte or token-estimate budget,
near-duplicate suppression, and a preference for one complete match over several
fragments.

Each result records the corpus/ranking identity, selected IDs in order,
component scores, penalties, rationale, rejected-by-budget IDs, retrieval
duration, and inserted context size. Empty, missing, invalid, or corrupted data
produces a typed nonfatal diagnostic and a deterministic prompt fallback; it
never broadens supported operations.

### 4.4 Prompt integration and diagnostics flow

Add the opt-in configuration ID `structural-rag-v1`. An immutable, validated
corpus/retriever is loaded once by the evaluator and shared with per-case
`PromptBuilder` instances. `PromptBuilder` remains independently usable with no
retriever for all existing configurations.

The initial RAG prompt uses the same full compiler-derived language context and
the same non-baseline fidelity instructions as `direct-dsl-relevant-v1`; it
replaces only the few-shot selection mechanism. This isolates retrieval quality
from grammar compression. A compact-compiler-context variant, if useful, receives
a different configuration ID and a separate experiment.

Retrieved entries are delimited as untrusted validated examples, never as system
instructions. The original request remains the final user message. The proposal
JSON schema and provider call/response contract are unchanged. Compiler-derived mandatory
rules remain present even when no example is selected. Relevant semantic-rule
filtering is deferred until it can be derived from existing language capabilities
without duplicating authoritative grammar or semantics.

Retrieval occurs only for the initial generation request in the first experiment.
Repair and requirement-correction prompt construction remains exactly as it is,
preventing retrieval-guided correction from confounding generation-time RAG.

To make provenance observable without recomputing retrieval, attach local prompt
context diagnostics to the initial `LlmRequest`. Providers ignore this metadata;
the evaluator's existing observing wrapper copies it into the attempt record.
This is smaller and safer than mutable `PromptBuilder` state or a second evaluator
retrieval pass. Offline tests must prove that it does not enter the Ollama wire
payload.

### 4.5 Evaluation integration

Extend the existing executable and PowerShell runner with explicit corpus path,
K, context budget, and ranking identity inputs. The defaults continue to select
`direct-dsl-v1`; RAG settings are rejected or ignored with a clear diagnostic for
non-RAG configurations rather than changing their prompts.

The report format needs a versioned additive revision for:

- corpus schema/version/digest and ranking version;
- K and context budget;
- corpus load/fallback status;
- per-case selected IDs, scores, rationale, duration, and context size; and
- aggregate retrieval coverage, latency, and context-cost summaries.

The repository has no raw-report reader, but the archiver accepts old schemas.
It must remain compatible with schema-v1/v2 input while preserving the new
provenance in a versioned history snapshot. Old history files stay immutable.

## 5. Initial controlled benchmark

Run the following configurations separately against both recorded model
identifiers, initially with three repetitions:

| Configuration | K |
|---|---:|
| `direct-dsl-v1` | n/a |
| `direct-dsl-relevant-v1` | n/a |
| `structural-rag-v1` | 1 |
| `structural-rag-v1` | 2 |
| `structural-rag-v1` | 3 |

Pin temperature 0, 2,048 output tokens, 120-second timeout, the existing shared
repair/correction budget of 3, dataset/case order, corpus digest, code revision,
and actual model digests. Keep complete raw attempts under `build/ai-evaluation/`
and archive only reviewed complete gates.

Report, with explicit denominators:

- first-attempt and final compiler validity;
- requirement satisfaction and strict normalized-IR match;
- support-classification agreement and unsupported recognition;
- accepted-proposal precision and generation coverage;
- per-category and per-case persistence across repetitions;
- compiler repairs and requirement corrections separately;
- retrieval, prompt-evaluation, generation, and total median/P90 latency;
- input/output tokens and retrieval-context bytes/tokens;
- requested-operation and structural coverage of selected examples; and
- improvements, irrelevant selections, and regressions by failure class.

Token reduction and retrieval knowledge are possible confounders. The initial
RAG comparison therefore retains the full compiler context. Analysis will report
prompt size and latency alongside correctness; any later compact-context variant
is a separate factorial comparison, not folded into the RAG claim.

The 30-case run answers whether the implementation is promising on development
data. It does not answer whether the system exceeds 80% on unseen requests.

## 6. Acceptance and optional-stage gates

Structural RAG proceeds beyond the first benchmark only when the improvement is
repeatable across repetitions, materially exceeds `direct-dsl-relevant-v1`, has
acceptable retrieval and end-to-end cost, and does not introduce unacceptable
calibration, assertion-strictness, sweep-order, numeric-fidelity, or support
classification regressions. Results for 3B and 7B are judged separately.

Embedding retrieval is considered only when error review shows that structural
analysis often identifies the right requirements but ranks the wrong examples.
It must use an explicitly available local Ollama embedding model, persisted
corpus/model identities, an in-memory index, and deterministic fallback. No model
is downloaded without approval and no vector database is introduced at this
scale.

Compiler-validated skeletons are considered only after the RAG-only result is
reported and remaining errors demonstrate a need for deterministic construction.
They form a separate configuration and evaluation; they are not credited as RAG
improvements.

Final acceptance requires a newly curated, family-separated frozen dataset of at
least 100 supported cases where feasible, plus separately scored partial,
unsupported, and ambiguous requests. Report the point estimate and a 95%
confidence interval, category outcomes, proposal precision, and coverage. The
target is greater than 80% end-to-end requirement satisfaction on all supported
held-out cases, counting invalid output and generation failure as failures. If it
is missed, report the measured gap and limiting failure classes rather than
changing the denominator.

RAG remains non-default until that independent evaluation and a separate product
promotion review are complete.

## 7. Incremental implementation plan

### Slice 0 - analysis and design

- Complete this architecture and risk review.
- Preserve the archived comparison run and revisions above.
- Obtain review before implementation.

### Slice 1 - independent corpus

- Add the versioned corpus, loader, compiler validation, provenance, and leakage
  report.
- Add deterministic offline tests for schema, IDs, ordering, completeness,
  compilation, duplication, and declared evaluation separation.

### Slice 2 - deterministic structural retrieval

- Add conservative typed query analysis, versioned scoring, budgeted top-K
  selection, diagnostics, and safe fallback.
- Test exact and paraphrased features, dependency order, ties, unsupported and
  unknown features, empty/corrupt corpora, duplicate suppression, budgets, and
  repeated-run determinism.

### Slice 3 - prompt integration

- Pin exact legacy prompt messages.
- Add `structural-rag-v1`, shared immutable retriever injection, clearly delimited
  context, and local request diagnostics.
- Prove existing configurations, repair flow, provider payload, and Studio
  defaults are unchanged.

### Slice 4 - evaluation and first A/B report

- Add CLI/script settings and versioned report/history provenance.
- Run deterministic evaluator tests, then the full unfiltered CTest suite because
  the change crosses prompt, agent-observation, and evaluation layers.
- Verify installed model digests, run the controlled matrix, and publish aggregate
  plus per-case improvement/regression analysis before any optional work.

### Slices 5-7 - separately approved follow-ups

- Slice 5: local embedding and hybrid retrieval only if retrieval selection is
  the measured bottleneck.
- Slice 6: compiler-validated skeletons and deterministic semantic safeguards only
  after the RAG-only result.
- Slice 7: frozen independent acceptance dataset, full benchmark, and explicit
  promote/reject recommendation.

## 8. Principal risks and controls

| Risk | Control |
|---|---|
| Evaluation leakage | Separate corpus, provenance, deterministic similarity checks, manual disposition, family-level final split |
| Baseline drift | Stable ID/default plus exact-message fixtures before refactoring |
| A second language model | Compiler validation and compiler-owned capabilities remain authoritative; no DSL parser in retrieval |
| Retrieval prompt injection | Checked-in corpus treated as delimited untrusted data; system contract remains dominant |
| Confounded quality claim | Freeze repair behavior and full compiler context; evaluate compression and skeletons separately |
| Hidden retrieval failure | Typed fallback diagnostics and report provenance for every case |
| Misleading aggregate accuracy | Supported-case denominator, proposal precision/coverage, category and regression reporting |
| Nondeterministic tests | No live model, network, clock, randomness, runtime, simulator, or hardware in unit tests |
| Product behavior changes prematurely | Evaluator-only opt-in; Studio and `direct-dsl-v1` remain unchanged |
| Excess architecture | Concrete in-memory retriever, no framework, cloud service, vector database, MCP, or fine-tuning |

## 9. Deliverables and final questions

Slices 1-4 produce the validated corpus, reusable structural retriever, opt-in
prompt configuration, versioned evaluation provenance, raw and archived A/B
results, and per-case failure analysis. Later deliverables are conditional on
their gates.

The final report must answer with measured evidence whether RAG beats
`direct-dsl-v1`, whether structural retrieval beats
`direct-dsl-relevant-v1`, whether either model benefits more, which error classes
change, what happens to end-to-end latency, and whether the independent supported
holdout exceeds 80%. It must report embeddings and skeletons as not evaluated
unless their separate gated experiments actually run.
