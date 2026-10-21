# HorusRF — Golden Dataset and Multi-Model Evaluation

Status: Approved implementation plan. Phases A through C completed and verified
on 2026-10-21; Phases D and E have not started. Implement phases incrementally
and mark them complete only after verification.

## Objective

Extend the existing HorusRF AI-assisted authoring functionality with:

1. A version-controlled golden dataset of natural-language RF characterization
   requests and expected HorusRF DSL scripts.
2. An automated evaluation framework to test natural-language-to-DSL
   generation.
3. Support for selecting between Qwen 2.5 Coder 3B and 7B in HorusRF Studio.
4. Performance and correctness metrics to compare models objectively.

Implement these capabilities in the existing architecture, reusing the
compiler, AI agent, Ollama provider, and Studio components.

Do not introduce a parallel AI implementation or unnecessary external
dependencies.

## 1. Inspect the existing architecture

Before changing code, review:

- `AGENTS.md`
- `docs/architecture.md`
- `docs/language.md`
- `docs/ai-assisted-authoring-architecture.md`
- `docs/phase-2-local-ai-assisted-authoring-plan.md`
- `include/horusrf/ai/`
- existing Ollama integration
- existing Studio assistant panel
- existing AI tests
- existing DSL examples
- existing CMake and CTest structure

Determine the appropriate extension points.

Preserve the existing dependency boundaries and safety guarantees.

The compiler remains the authoritative source of DSL correctness.

## 2. Create a golden dataset

Introduce a version-controlled dataset under:

`tests/ai/golden/`

Use a structure similar to:

```text
tests/ai/golden/
    README.md
    manifest.json
    cases/
        frequency_sweep_basic/
            prompt.txt
            expected.hrf
            metadata.json
        frequency_sweep_gain/
            prompt.txt
            expected.hrf
            metadata.json
        power_sweep/
            prompt.txt
            expected.hrf
            metadata.json
        temperature_sweep/
            prompt.txt
            expected.hrf
            metadata.json
        multidimensional_sweep/
            prompt.txt
            expected.hrf
            metadata.json
        measurement_assertions/
            prompt.txt
            expected.hrf
            metadata.json
```

The final organization may differ if an existing project convention is more
appropriate.

### Golden test case format

Each case must contain:

**`prompt.txt`**

The natural-language request made by the user.

**`expected.hrf`**

A manually reviewed, canonical HorusRF DSL script satisfying the request.

**`metadata.json`**

Machine-readable metadata describing the test.

Example:

```json
{
  "id": "frequency_sweep_basic",
  "description": "Basic RF frequency sweep",
  "category": "frequency_sweep",
  "difficulty": "basic",
  "tags": [
    "frequency",
    "sweep",
    "power"
  ],
  "expected_valid": true,
  "comparison": "semantic",
  "required_capabilities": [
    "frequency_sweep",
    "power_measurement"
  ]
}
```

Define and validate a consistent metadata schema.

### Initial dataset

Create approximately 15–20 representative cases, including:

- basic frequency sweeps
- frequency sweeps with multiple measurements
- fixed-frequency power sweeps
- temperature sweeps
- multidimensional sweeps
- named measurements
- derived measurements
- measurement assertions
- calibration surfaces
- qualification conditions
- requests containing multiple constraints
- natural-language variations of equivalent requests

Use only functionality genuinely supported by the current HorusRF DSL.

Take inspiration from the existing examples and compiler capabilities.

Do not invent new DSL syntax to satisfy a golden case.

Every expected script must successfully pass the full compiler validation
pipeline, including semantic analysis and IR lowering.

For unsupported user requests, add a separate explicitly marked category
testing graceful rejection or warnings, rather than inventing valid DSL.

Never silently remove a requested operation simply to obtain a compilable
result.

### Golden dataset requirements

The dataset must be:

- deterministic
- human-readable
- easy to extend
- independent of specific LLM models
- version controlled
- suitable for regression testing
- representative of real RF characterization requests

Golden outputs must be curated independently of model predictions.

Do not automatically replace expected scripts with generated results.

## 3. Golden dataset validation

Implement a deterministic, offline test that:

1. Discovers all golden cases.
2. Validates the manifest and metadata.
3. Ensures unique case identifiers.
4. Loads each expected DSL script.
5. Runs existing full compiler validation.
6. Reports invalid or malformed golden cases.
7. Fails when a supposedly valid golden script does not compile.

Register this test in CTest.

It must run without Ollama, networking, Qt, or model downloads.

Follow the repository's existing test organization.

## 4. Semantic comparison

For a generated DSL script, distinguish three outcomes:

1. The script is syntactically correct.
2. The script is semantically valid according to HorusRF.
3. The script actually implements the user's requested characterization.

These are different properties.

Implement an initial deterministic comparison mechanism using the existing AST,
analyzed program, or IR.

Do not compare scripts exclusively as raw text.

Where appropriate, compare:

- sweep dimensions
- sweep boundaries
- step sizes or point counts
- measurement types
- units
- requested derived values
- assertions and thresholds
- calibration definitions
- relevant experiment parameters

Ignore irrelevant formatting, comments, and harmless identifier differences
where equivalence can be established safely.

Do not assume two valid scripts are equivalent merely because both compile.

Avoid creating a second compiler or duplicating semantic rules.

Where reliable equivalence cannot yet be established, report the result as
`not_evaluated` or `unknown`, not as a successful match.

Design the comparison layer for incremental extension.

## 5. Automated model evaluation

Introduce a separate, opt-in evaluation executable or command using the
existing AI agent infrastructure.

The evaluation must:

1. Load the golden dataset.
2. Select an installed Ollama model.
3. Submit each natural-language request to the existing HorusRF agent.
4. Capture the generated DSL.
5. Record whether generation completed.
6. Record initial validation success.
7. Record repair attempts.
8. Run final compiler validation.
9. Compare the result against golden expectations.
10. Record performance metrics.
11. Generate a structured report.

Do not run actual RF experiments or simulated hardware during evaluation.

### Required metrics

Capture per test case:

```text
Case ID
Model
Generation status
First-attempt compilation success
Final compilation success
Semantic comparison status
Number of repair attempts
Total wall-clock latency
Model loading duration, if available
Prompt evaluation duration, if available
Generation duration, if available
Input token count, if available
Output token count, if available
Output tokens per second, if available
Error category
```

Ollama timing and token metrics should be captured through the existing
provider adapter where possible.

Extend the provider-neutral response contract only as needed, using optional
provider-independent metadata.

Do not leak Ollama-specific JSON structures into the rest of the application.

Report unavailable metrics explicitly rather than substituting estimates.

### Aggregate metrics

Produce a summary including:

- total cases
- first-pass compilation success rate
- final compilation success rate
- semantically verified success rate
- unknown semantic comparison count
- average repair attempts
- median latency
- P90 latency
- average output generation throughput
- failure categories

Success-rate denominators must be explicit.

Distinguish model failures from infrastructure failures and unsupported
requests.

### Repeatability

Support repeated runs of the same dataset.

Use a configurable temperature, defaulting to a low value.

Record:

- model name
- actual model identifier or digest where available
- generation parameters
- golden dataset version
- Git revision where available
- evaluation timestamp
- hardware/provider details where available

Do not assume temperature zero guarantees deterministic inference.

Store evaluation reports separately from the curated golden dataset.

## 6. Introduce 3B and 7B model selection

Extend the existing Studio AI Assistant model selector.

The user must be able to choose between locally installed models, including:

- `qwen2.5-coder:3b`
- `qwen2.5-coder:7b`

Use Ollama's existing `/api/tags` model-discovery endpoint.

Do not hardcode these as the only supported models.

Both should appear when installed, alongside any other compatible models
already discovered.

### Model selection behavior

- Display friendly model names.
- Retain the actual Ollama model identifier.
- Clearly indicate which model is selected.
- Use the selected model for generation, modification, and repair.
- Preserve selection during the session.
- Persist the selection across application restarts using existing Studio
  settings conventions, if available.
- Fall back safely if a previously selected model is no longer installed.
- Show useful feedback when Ollama is unavailable or a requested model is
  missing.

Do not silently switch models during an active generation request.

Do not automatically download models.

Include documentation for installing the two models:

```powershell
ollama pull qwen2.5-coder:3b
ollama pull qwen2.5-coder:7b
```

## 7. Model comparison

Add an evaluation command capable of running the same golden dataset against
multiple models sequentially.

For example, a command equivalent to:

```text
horusrf-ai-eval
    --models qwen2.5-coder:3b,qwen2.5-coder:7b
    --dataset tests/ai/golden
    --output results.json
```

The exact CLI syntax is an implementation decision.

Produce:

1. A machine-readable JSON report.
2. A concise human-readable comparison table.

For example:

```text
Metric                    3B         7B
------------------------------------------------
Cases                     20         20
First-pass valid          ...        ...
Final valid               ...        ...
Semantic matches          ...        ...
Median latency            ...        ...
P90 latency               ...        ...
Average repair attempts   ...        ...
```

Do not fabricate benchmark results.

The report must clearly distinguish compilation success from semantic
correctness.

Allow selecting a subset of test cases for fast iteration.

Support evaluation of individual cases by ID.

## 8. Performance considerations

The motivation for comparing models is that the current 7B configuration takes
approximately 40 seconds for some interactive generations on the development
machine.

Investigate safe, measurable optimizations without changing the DSL's
semantics.

Consider:

- output token limits
- prompt context size
- redundant prompt information
- number of few-shot examples
- model keep-alive configuration
- model loading overhead
- repair-loop overhead
- model-specific inference parameters

Reuse the existing compiler-derived prompt construction.

Do not remove essential grammar or semantic constraints merely to reduce
latency.

Avoid imposing token limits that routinely truncate valid scripts or structured
responses.

Measure performance before claiming improvements.

Do not introduce GPU-specific assumptions into the application architecture.

## 9. Offline versus live tests

Maintain two separate kinds of testing.

**Offline regression tests**

- Always deterministic.
- No Ollama dependency.
- No network access.
- Validate golden dataset integrity.
- Validate semantic comparison behavior.
- Exercise agent behavior with fake providers.
- Run in normal CTest.

**Live model evaluations**

- Explicitly invoked by developers.
- Require a running Ollama instance.
- Require installed models.
- Measure actual generation performance.
- Must not run as part of the default CTest suite.

Never let model availability make normal project tests fail.

## 10. Studio integration scope

The mandatory Studio change is model selection.

Do not build an elaborate benchmark dashboard inside Studio in this iteration.

Keep evaluation as a developer-facing CLI capability.

Preserve the existing proposal review workflow:

1. Generate.
2. Validate.
3. Repair if needed.
4. Present proposal.
5. User explicitly applies to editor.
6. User separately chooses whether to execute.

Model generation must never automatically modify, save, or execute
experiments.

## 11. Tests and documentation

Add appropriate tests for:

- golden dataset discovery
- metadata validation
- invalid golden scripts
- duplicate IDs
- semantic comparisons
- model enumeration
- model selection
- missing model handling
- model selection persistence
- correct model propagation to the provider
- evaluation report serialization
- fake-provider evaluation
- cancellation and errors

Update:

- AI-assisted authoring architecture documentation
- test documentation
- Studio usage instructions
- model installation instructions
- golden dataset README
- relevant build and CMake documentation

Follow all existing repository rules in `AGENTS.md`.

Use the existing `build/` directory.

Run the full applicable test suite.

## 12. Acceptance criteria

The implementation is complete when:

1. At least 15 representative golden cases exist.
2. Every positive expected script passes full compiler validation.
3. The golden dataset validation works offline.
4. Semantic comparison can detect at least basic parameter or measurement
   mismatches.
5. Studio lists installed 3B and 7B models.
6. The user's model selection reaches the actual Ollama request.
7. The existing generate/validate/repair workflow works with either model.
8. A developer can evaluate one model against the golden dataset.
9. A developer can compare 3B and 7B results.
10. Results include correctness and latency metrics.
11. Normal CTest execution requires no Ollama instance.
12. Existing compiler, CLI, Studio, and AI tests remain passing.

## Implementation approach

Proceed incrementally:

### Phase A — Golden dataset

Create metadata, fixtures, validation, and offline tests.

Completed on 2026-10-21 with 17 curated cases (16 supported and one explicit
unsupported power-sweep request), a version-1 manifest and strict metadata
schema, reusable offline loading/compiler validation, and deterministic CTest
coverage.

### Phase B — Semantic comparison

Implement a conservative structural comparison using existing compiler
representations.

Completed on 2026-10-21 with the Qt-free
`horusrf_ai_semantic_comparison` target. Both scripts pass through the existing
frontend and IR lowering path, after which canonical typed IR structure is
compared. The comparison covers reference power, ordered sweep dimensions and
parameters, measurement operations, normalized derived-expression graphs,
assertions, and calibration indexes and correction expressions. It ignores
formatting, source spans, display-unit spelling, characterization names,
internal value IDs, and harmless identifier renaming. Results distinguish
`Match`, structured `Mismatch`, and `NotEvaluated` for invalid or
unnormalizable input.

Deterministic offline tests cover canonical unit equivalence, mismatch classes,
invalid candidates, and self-equivalence for all 16 supported golden cases. The
focused suite and full unfiltered 31-test CTest suite passed in the existing
`build/` directory with the Release configuration. No model, network, runtime,
simulator, or hardware was used.

### Phase C — Model selection

Extend Studio's existing Ollama model selection and persistence.

Completed on 2026-10-21. Studio discovery retains every installed Ollama model,
shows friendly labels for `qwen2.5-coder:3b` and `qwen2.5-coder:7b`, and keeps the
exact external identifier as item data and in every assistant request. The
selected identifier is persisted with `QSettings`. Discovery preserves an
installed selection or explicitly reports and persists a deterministic fallback
when the saved model is missing. Active requests retain their copied selection
while the configuration controls are disabled.

Headless fake-backend tests cover 3B/7B enumeration, friendly presentation,
exact provider propagation, restart persistence in an isolated settings store,
and missing-model fallback. The focused Studio suite and the full unfiltered
31-test CTest suite passed in the existing `build/` directory with the Release
configuration. No Ollama service, model, download, network, runtime, simulator,
or hardware was used.

### Phase D — Evaluation runner

Implement the live benchmarking executable and reports.

### Phase E — Verification

Run tests, update documentation, and review architectural boundaries.

Before implementing each phase, inspect the corresponding existing code and
reuse established patterns.

Avoid unrelated refactoring.

At completion, summarize:

- files changed
- main architectural decisions
- number of golden cases
- commands for running offline and live tests
- commands for comparing 3B and 7B
- tests actually executed
- known limitations

Do not claim live model tests passed unless Ollama and the relevant models were
actually available and tested.
