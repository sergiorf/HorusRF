# Small-model DSL guardrail demo

## Purpose and claim

This demonstration shows how a small local language model can generate HorusRF
DSL safely enough for an assisted-authoring workflow when the model is surrounded
by deterministic guardrails. It is an engineering demonstration, not a claim of
general production reliability or unseen-request accuracy.

The clean reviewed controlled development run at revision `4237de3` used Qwen
2.5 Coder 7B through Ollama with structural retrieval K=3. It achieved 72
verified completions from 84 supported or partially supported runs (85.7%). All
72 proposals allowed by that revision's evaluation gate satisfied the
independently curated requirements (100% accepted precision in this development
suite). The retained evidence and limitations are documented in
[the M6Q.9 evidence report](ai-generation-guardrail-evidence.md).

The current revision adds a stricter calibration-direction policy. When a request
asks for calibration without saying whether correction is reference-minus-
measured or measured-minus-reference, an otherwise correct proposal is now
`review_required` rather than automatically `allow`. Treat this as a deliberate
safety/automation trade-off when presenting results; do not combine metrics from
the two policy revisions.

The result to emphasize is not that the model is always right. It is that model
output is treated as untrusted input and only surfaced when independent checks
agree.

## Guardrail flow

```text
Natural-language request
        |
        v
Support preflight --------> unsupported: stop without a model call
        |
        v
Validated structural examples (bounded K=3 context)
        |
        v
Local 7B model + strict response schema
        |
        v
HorusRF compiler ----------> invalid: bounded diagnostic repair
        |
        v
Requirement verification --> violated: bounded correction
        |
        v
allow / review required / reject
        |
        v
Explicit human Apply; execution remains a separate action
```

The safety properties worth showing are:

- checked-in retrieval examples are compiler validated and inserted only into
  the initial generation request;
- provider responses must match a strict structured schema;
- every candidate passes through the real lexer, parser, semantic analysis, and
  IR lowering path;
- independently extracted numeric, unit, sweep, measurement-name, assertion,
  and calibration requirements are checked against compiler-normalized facts;
- repair and correction calls share a fixed budget, and repeated candidates or
  unchanged evidence stop as `no_progress`;
- unsupported requests, ambiguous semantics, invalid candidates, and verified
  violations are not counted as successful generation;
- generated source is never executed automatically.

Studio uses the packaged, compiler-validated structural corpus with K=3 for
initial generation. It does not silently fall back when that corpus is missing or
invalid. Compiler repairs receive diagnostic-specific quantity types, valid units,
the failing source span, and a small validated syntax form; the compiler still
decides whether the returned script is valid. A repair that merely claims to make
a change while leaving the script or diagnostics unchanged stops as
`no_progress`.

## Reproduce the controlled run

The normal test suite is offline and does not require Ollama. The live benchmark
is explicit and writes only below `build/` until its report has been reviewed.

On Windows, start Ollama in GPU-required mode and load the model:

```powershell
.\scripts\start-ollama.cmd -Model qwen2.5-coder:7b -Processor Gpu
```

Run the 30-case suite three times with the controlled settings:

```powershell
.\scripts\run-ai-evaluation.cmd `
  -Configuration Release `
  -Models qwen2.5-coder:7b `
  -Dataset tests/ai/golden `
  -OutputPath build/ai-evaluation/m6q9-demo-head-7b-k3-r3.json `
  -Repeat 3 `
  -Temperature 0 `
  -TimeoutSeconds 120 `
  -MaxOutputTokens 2048 `
  -MaxRepairs 3 `
  -PromptConfiguration structural-rag-v1 `
  -RetrievalCorpus ai/retrieval `
  -RetrievalTopK 3 `
  -RetrievalContextBudget 4096 `
  -RetrievalBudgetUnit utf8-bytes `
  -RetrievalRankingId structural-rag-ranking-v1
```

Before publishing numbers, confirm that the report records the expected model
digest, dataset, prompt configuration, retrieval corpus and ranking identities,
settings, Git revision, all 90 runs, and no provider or infrastructure failure.
Archive only a complete reviewed report.

## Suggested recording sequence

Keep the video centered on behavior rather than aggregate JSON.

1. Open with the question: "Can a local 7B model safely generate a typed RF
   DSL?"
2. Show a straightforward supported request such as
   `frequency_sweep_basic`: retrieval supplies relevant valid syntax, the first
   candidate compiles, and the curated requirements pass. On the current policy,
   its unstated calibration direction produces `review_required`; this is the
   human-in-the-loop boundary rather than a generation failure.
3. Show `combined_limits_paraphrase`: a plausible candidate can miss a requested
   output name, requirement verification identifies the mismatch, and a bounded
   correction produces a verified proposal. Calibration ambiguity may still keep
   the final verdict at `review_required`.
4. Show `fixed_frequency_power_sweep_unsupported`: capability preflight rejects
   the unsupported power sweep without spending a model call.
5. Show a persistent difficult case such as `inclusive_frequency_error`: bounded
   repair stops when evidence does not improve, and the result is rejected rather
   than offered as correct.
6. End on the clean retained result: 85.7% verified completion and 100%
   precision among allowed proposals on the controlled development suite at
   revision `4237de3`. Then show that the current stricter policy sacrifices
   automatic coverage by sending unspecified calibration direction to human
   review.

For Studio footage, be precise about the boundary: Studio already shows a
compiler-valid proposal for explicit human review and Apply, but the three-state
acceptance decision is currently evaluation evidence rather than a product Apply
gate. Applying source changes only the editor; running the experiment remains a
separate user action.

When repair fails, Studio labels the source as a **Rejected candidate**, opens the
Validation tab, identifies explanation and warnings as untrusted model output,
and keeps Apply disabled. This rejected source is retained for diagnosis; it is
not a successful proposal.

## Short-form narrative

> I used a local 7B model to generate a typed RF DSL. The model is not trusted:
> requests are capability-checked, retrieval examples are validated, every
> candidate is compiled, explicit requirements are independently verified, and
> repair is bounded. On a controlled 30-case development suite repeated three
> times, the reviewed system revision verified 85.7% of applicable requests and
> every proposal its evaluation gate allowed satisfied the curated requirements.
> A later, stricter calibration policy routes unspecified correction direction to
> human review. Failures remain visible as review or rejection outcomes, and
> generated code is never executed automatically.
