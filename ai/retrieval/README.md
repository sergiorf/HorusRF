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

`leakage-report.json` is the deterministic review artifact produced by comparing
requests and compiler-valid programs internally and against golden dataset 2.0.0.
An empty `findings` array means no normalized exact request, likely request
paraphrase, normalized exact program, or value-erased structural clone crossed
the configured thresholds. It is a review aid, not evidence of final unseen-case
performance.
