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
