# HorusRF golden authoring dataset

This directory contains model-independent, manually reviewed natural-language to
HorusRF cases. `manifest.json` defines the ordered dataset. Each directory under
`cases/` contains:

- `prompt.txt`: the user request;
- `expected.hrf`: the curated expected source, or an explanatory non-program for
  an explicitly unsupported request; and
- `metadata.json`: the case ID, category, difficulty, tags, validity expectation,
  comparison mode, and required language capabilities.

Metadata uses schema version 1. `comparison` is `semantic` for supported requests
and `unsupported` for requests the current language cannot express. Semantic cases
must set `expected_valid` to `true`; unsupported cases must set it to `false`.

The `horusrf_ai_golden_dataset_tests` CTest target validates the manifest and
metadata, checks unique IDs and complete discovery, and passes every supported
expected script through parsing, semantic analysis, and IR lowering. It is fully
offline and never contacts Ollama or executes an RF experiment.

Golden outputs are curated independently of model predictions. Do not replace an
expected script automatically with generated output.

The `horusrf_ai_semantic_comparison_tests` target uses the compiler's typed IR to
compare supported expected scripts with generated candidates. Formatting,
source locations, characterization names, harmless identifier renaming, and
display-unit spelling are ignored after canonical conversion. Sweep structure,
measurements, derived expressions, assertions, and calibration semantics are
compared. Invalid or structurally unnormalizable input is `NotEvaluated`, never
treated as a semantic match.

## Live evaluation

Live evaluation is opt-in and requires a running local Ollama service plus every
requested installed model. It never executes generated experiments. From the
repository root on Windows, run both comparison models with:

```powershell
.\scripts\run-ai-evaluation.ps1 -Configuration Release `
  -Models "qwen2.5-coder:3b,qwen2.5-coder:7b"
```

For a fast single-case run:

```powershell
.\scripts\run-ai-evaluation.ps1 -Models "qwen2.5-coder:3b" `
  -CaseIds "frequency_sweep_basic"
```

The script configures and builds only in the repository's `build/` directory,
then invokes `horusrf-ai-eval`. Reports default to timestamped JSON files under
`build/ai-evaluation/`. Use `-OutputPath`, `-Repeat`, `-Temperature`,
`-TimeoutSeconds`, `-MaxOutputTokens`, `-MaxRepairs`, or `-Endpoint` to override
the corresponding evaluation setting. The script checks installed models but
does not download them or start Ollama.

Evaluation reports use `horusrf.ai-evaluation` schema version 2. They are
write-only artifacts; the repository has no report reader, so no schema-v1
reader is required. Version 2 retains every provider attempt, structured-response
status, candidate, compiler diagnostics, strict semantic differences, prompt
UTF-8 sizes, provider telemetry, run order, cold/warm classification, and model
digest when Ollama reports one. The first evaluated case for each model is
classified cold and subsequent cases warm; per-call model-load telemetry remains
available to identify later reloads.
