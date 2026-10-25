# Structural retrieval corpus

This directory is the independent, checked-in example corpus for the M6Q.8
structure-aware RAG experiment. It is separate from the development evaluation
dataset under `tests/ai/golden` and is not used by any default product path.

`manifest.json` fixes schema version 1, corpus identity and version, deterministic
example order, and an FNV-1a content digest over the corpus identity plus every
ordered example ID, request, program, and metadata record. Each example has this
layout:

```text
examples/<id>/request.txt
examples/<id>/program.hrf
examples/<id>/metadata.json
```

Example metadata records typed operations, ordered structural steps, ordered
sweep dimensions, data dependencies, comparison strictness, complexity, and
authoring provenance. The provenance explicitly records that the development
benchmark categories influenced corpus coverage; this corpus must therefore not
be presented as an unseen final-acceptance set.

The loader validates the exact JSON shapes, UTF-8, safe and unique IDs,
manifest/directory agreement, digest, and each program through the full compiler
pipeline. It performs no provider, network, runtime, simulator, or hardware work.

`ExampleRetriever` implements the deterministic `structural-rag-ranking-v1`
ranking. Its conservative query projection reuses the authoring support and
requirement extractors, then scores ordered sweep/dependency compatibility,
operation and typed-feature coverage, lexical similarity, complexity, conflicts,
and complete-match preference. Selection accepts only K=1, 2, or 3, applies a
UTF-8 byte or explicit four-bytes-per-token estimate budget, suppresses structural
near-duplicates, and resolves score ties in manifest order. Results include the
corpus and ranking identities, component scores, rationale, budget rejections,
context size, duration, and typed fallback diagnostics. This retriever is not yet
used by any default product path.

The opt-in `structural-rag-v1` prompt configuration connects this retriever only
to initial generation prompts. Selected entries are inserted in ranking order as
clearly delimited, untrusted user/assistant reference pairs; the original request
remains the final user message. Repair, requirement-correction, modification,
the three legacy prompt configurations, and Studio defaults do not retrieve.
Each structural-RAG request retains an immutable local `RetrievalResult` for
later evaluation provenance, and provider serializers ignore that metadata.
The live evaluator validates and loads the corpus before model discovery, records
schema/version/digest and ranking/budget identity in schema-v3 reports, and
retains each initial request's selections, scores, rationale, duration, and
context cost outside the provider payload. Missing retrievers and selection failures fall back to the full compiler context
with no retrieved examples and an explicit typed diagnostic.

`leakage-report.json` is the deterministic review artifact produced by comparing
requests and compiler-valid programs internally and against golden dataset 2.0.0.
An empty `findings` array means no normalized exact request, likely request
paraphrase, normalized exact program, or value-erased structural clone crossed
the configured thresholds. It is a review aid, not evidence of final unseen-case
performance.
