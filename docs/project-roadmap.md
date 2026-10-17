# HorusRF project roadmap and implementation plan

Status: approved direction, reviewed against the repository on 2026-10-17.

This is the active forward-looking roadmap. Files under `docs/archive/` are historical
records and are not changed by this plan. Milestone details may be refined by the
preceding design work, but phase order and architectural constraints are gates, not
suggestions.

## Baseline

HorusRF is a C++20 declarative DSL for RF characterization, calibration, and
automated testing. The repository currently has a lexer and parser, AST, semantic
analysis with strongly typed RF quantities, typed IR, runtime, simulated RF devices,
CLI, and a Qt Studio application.

The implemented language intentionally has one frequency sweep, one power
measurement, and one frequency-indexed power calibration. The runtime controls TX
through `RfDevice`, obtains observations through `MeasurementDevice`, and never
uses simulator internals. Studio is a separate GPL application that invokes the CLI
through `QProcess`; it does not link the compiler, runtime, simulator, or output
libraries.

At the time of this review, the existing Debug CTest suite contains 19 tests and
passes in full.

## Goal and phase gates

The long-term goal is a more expressive RF experimentation platform that preserves
the declarative language, dimensional type safety, deterministic execution, and
existing layer boundaries.

Work proceeds in order:

1. Phase 1: DSL extensions and Studio integration (M0-M6).
2. Phase 2: optional LLM assistance (M7-M9), only after Phase 1 formats and language
   contracts are stable.
3. Phase 3: advanced RF measurements and optional agentic workflows (M10-M13).

The current priority is exclusively Phase 1. Designing an extension point for later
AI use is allowed; building AI infrastructure during Phase 1 is not.

## Non-negotiable compatibility and architecture rules

- Keep grammar, lexer/parser behavior, `docs/language.md`, syntax highlighting, and
  tests synchronized for every syntax change.
- Preserve AST, semantic, IR, runtime, device, simulator, output, CLI, and Studio
  boundaries documented in `docs/architecture.md`.
- Use strong domain values at public and cross-layer boundaries. Do not replace
  `Frequency`, `Power`, or `PowerDelta` with untyped numbers; apply the same rule to
  new temperature, ratio, frequency-delta, boolean, and complex-domain values.
- Runtime accesses equipment through public device abstractions only. It must never
  depend on simulator classes or their private transfer functions.
- Keep RF stimulus control and observation separate. If environmental control is
  added, introduce an explicit device contract such as `TemperatureController`
  rather than adding simulator-specific behavior to the runtime or overloading
  `MeasurementDevice`.
- Preserve independent declarative and procedural orchestration and their existing
  equivalence coverage.
- Keep Studio behind the CLI process boundary and preserve asynchronous `QProcess`
  execution, separate output streams, diagnostic navigation, and last-good results.
- Preserve existing source behavior, public execution overloads where practical,
  diagnostic stability, CLI exit behavior, and the exact legacy CSV schema for the
  original experiment shape. New result shapes use JSON; the fixed legacy CSV is
  not stretched into a second implicit schema.
- Tests remain deterministic and independent of network, wall clock, randomness,
  and real hardware.
- Avoid speculative frameworks, dependencies, and extension scaffolding.

## Phase 1 - DSL extensions and Studio

### M0 - Architecture and language design

Status: complete on 2026-10-17. The accepted contracts are recorded in
[`docs/dsl-evolution.md`](dsl-evolution.md) and the approved boundary changes are
summarized in [`docs/architecture.md`](architecture.md).

Inspect the current grammar, compiler layers, runtime validation and evaluation,
device contracts, simulator, result types, CSV/CLI behavior, Studio CSV model, and
test suites. Record the accepted design in `docs/dsl-evolution.md` and update
`docs/architecture.md` for approved boundary changes.

The design must settle these contracts before implementation:

- Exact source grammar for generic sweep declarations, named measurements,
  assertions, calls, comparisons, and multidimensional calibration.
- Unambiguous absolute-temperature and temperature-delta units. For example,
  `-20 degC` is an absolute temperature while `20 deltaC` is a step. The final
  spellings are an M0 decision; a Celsius absolute unit must never be silently used
  as a delta merely because it appears after `step`.
- Affine Celsius/Kelvin conversion semantics and legal arithmetic involving
  `Temperature` and `TemperatureDelta`.
- Sweep rules: declaration-order dimension order, which dimension varies fastest,
  endpoint inclusion, step/range validation, duplicate dimensions, checked
  Cartesian-product sizing, and a whole-experiment point limit.
- General AST, semantic, and IR shapes. AST remains syntactic, semantic analysis
  resolves names and dimensions, and IR remains typed and device-independent.
- An operating-point coordinate model whose entries retain dimension name, strong
  type, and value without reducing all coordinates to `double`.
- Measurement operation names, result types, device capability/preflight behavior,
  and equipment-call ordering. Unknown language operations are compiler errors;
  a valid operation unavailable on the supplied device is a runtime diagnostic
  produced before any equipment call.
- Assertion evaluation and aggregation rules, including NaN/non-finite policy,
  source spans, and the definition of point and experiment pass/fail.
- Version 1 JSON result schema, CLI option and failure behavior. The schema must
  carry type and canonical/display unit metadata and use stable names rather than
  C++ variant indexes.
- Calibration surface ordering and the distinction between calibration-fit metrics,
  qualification metrics, and genuinely independent validation metrics. Fit data
  must not be presented as validation data.
- Backward-compatibility fixtures for the current example, API, CLI summary, CSV,
  call order, diagnostics, Studio behavior, and procedural/declarative equivalence.

Acceptance:

- `docs/dsl-evolution.md` contains concrete contracts and representative valid and
  invalid programs, not only class-name proposals.
- Existing tests pass unchanged except for deliberate additive test coverage.
- No production refactor is included unless needed to prove a design assumption.

### M1 - Typed sweep model and compiler support

Add multiple typed sweep declarations through grammar, lexer, AST, semantic
analysis, and IR. Add `Temperature` and `TemperatureDelta` domain types with the
absolute/delta conversions selected in M0. Support signed endpoints where valid,
reject duplicate dimensions and invalid ranges, and compute individual and total
point counts using checked arithmetic.

An illustrative program, subject to the exact M0 spellings, is:

```text
sweep frequency 2.40 GHz .. 2.50 GHz step 5 MHz
sweep temperature -20 degC .. 80 degC step 20 deltaC
```

This milestone deliberately changes compiler representation before general runtime
execution. `--check` accepts a valid multidimensional program, existing
single-frequency programs still execute unchanged, and attempting to execute a new
multidimensional program must return an explicit stable runtime diagnostic without
touching equipment until M2 lands.

Acceptance:

- Multiple typed sweeps parse, analyze, and lower in declaration order.
- Invalid units, duplicate dimensions, non-positive steps, reversed ranges,
  non-finite values, count overflow, and excessive products are rejected at the
  lowest layer that has enough information.
- Grammar, language reference, syntax highlighting, and focused tests agree.
- Original programs, CLI output, CSV, and device call order remain unchanged.

### M2 - Multidimensional runtime execution

Execute the deterministic Cartesian product of the IR sweep dimensions. Introduce
typed operating-point coordinates and an explicit environmental-control device
contract with a deterministic simulator implementation. Retain the existing
`RfDevice` and `MeasurementDevice` responsibilities and a source-compatible path
for legacy callers.

The M0 design fixes ordering; the recommended default is declaration-order
coordinates with the last declared dimension varying fastest. Generate each axis
by integer index using the existing endpoint-tolerance discipline. Check the full
product before reserving memory or calling equipment.

Acceptance:

- A 21 by 6 experiment executes exactly 126 unique points in the documented order.
- Stimulus/environment setup precedes measurements at every point in a documented,
  tested call order.
- Simulator results are reproducible without tests depending on its private
  transfer function.
- Device failures preserve current no-retry/no-partial-result behavior unless M0
  explicitly documents a compatible replacement.
- Legacy execution and procedural/declarative equivalence remain unchanged.

### M3 - Multiple named measurements

Add named measurement declarations while retaining `measure power` as a legacy
spelling with its existing binding. For example:

```text
measure output_power = power
measure evm = evm
measure frequency_error = frequency_error
```

Add strong `Ratio` and `FrequencyDelta` values, define their units and arithmetic,
generalize semantic symbols and IR measurement operations, and let derived
expressions refer to earlier named measurements. Extend public measurement-device
contracts through the smallest explicit capability model justified by power, EVM,
and frequency-error observations; do not expose simulator state or use stringly
typed runtime dispatch.

Acceptance:

- Each declared measurement executes once per operating point in source order.
- A 126-point, three-measurement experiment yields 378 named observations.
- Results retain measurement name, semantic type, canonical value, and unit.
- Duplicate names, use-before-declaration, unknown operations, illegal arithmetic,
  and unavailable device capabilities produce stable diagnostics at the specified
  compiler/runtime boundary.
- Legacy `measure power`, output, and simulator observables remain compatible.

### M4 - Assertions and qualification

Add comparison operators, boolean semantic and IR types, typed function calls with
an initially small `abs()` overload set, and assertion statements evaluated at each
operating point.

```text
derive power_error = output_power - reference.power

assert abs(power_error) <= 1.5 dB
assert evm <= 5 %
assert abs(frequency_error) <= 20 kHz
```

Assertions are specification results, not runtime control flow. A false assertion
does not stop the experiment. Each outcome records the assertion identity/span,
coordinates, observed operands, threshold, and pass/fail state. Equipment or
evaluation failures remain execution failures and are not converted into failed
assertions.

Acceptance:

- Invalid comparisons and non-boolean assertion expressions fail semantic analysis.
- Every assertion is evaluated once per completed point in source order.
- Point and overall qualification status and per-assertion counts follow the M0
  aggregation contract.
- CLI reporting and source-positioned failures are covered by positive, negative,
  end-to-end, and process tests.

### M5 - Calibration surfaces and versioned structured results

Generalize calibration indexes and artifacts from one frequency vector to ordered,
typed multidimensional coordinate/value entries. Ensure correction entries align
with the operating points and retain a strong correction type.

```text
derive calibration tx_power {
  correction = reference.power - output_power
  over frequency, temperature
}
```

Introduce a versioned JSON result serializer and an additive CLI option such as
`--json <path>`. JSON becomes the complete machine-readable format for coordinates,
measurements, derived values, assertions, qualification summaries, calibration
surfaces, and explicitly scoped metrics. JSON output is deterministic and
locale-independent; schema versioning and unknown-field expectations are
documented.

The original `--csv` header and values remain unchanged for the legacy experiment
shape. If `--csv` is requested for a result that cannot be represented by that
schema, fail before creating the file with a stable diagnostic or usage contract
chosen in M0. Do not silently flatten multidimensional data into the legacy CSV.

Acceptance:

- Calibration coordinate tuples and corrections align exactly with runtime order.
- JSON round-trip/contract tests verify names, types, units, coordinates, source
  metadata where promised, and schema version.
- Calibration-fit and qualification metrics are labeled accurately. Independent
  validation metrics appear only if the language has an independent validation
  data set; otherwise they are explicitly deferred.
- Existing CSV and CLI acceptance fixtures remain byte-for-byte compatible where
  their current tests require it.

### M6 - Full Studio integration

Keep `QProcess` integration and switch rich-result ingestion to the versioned CLI
JSON contract. Studio owns its presentation model and does not link core libraries
or repeat domain calculations.

Deliver:

- A multidimensional results table with typed coordinates, all observations and
  derived values, assertion state, and coordinate/pass filters.
- Selectable plots for power, EVM, frequency error, and other scalar values against
  one sweep axis while other coordinates are fixed.
- Heatmaps for two selected dimensions and a selected scalar, with deterministic
  axis ordering, units, missing-cell behavior, and point selection.
- A qualification dashboard with point totals, pass percentage, overall status,
  and failures grouped by assertion.
- Failure inspection showing values, thresholds, coordinates, and navigation to
  the assertion source span.
- Continued support for the legacy chart and strict legacy CSV loading where it is
  already public Studio behavior.

Acceptance:

- A checked-in qualification example runs from Studio and exposes all observations,
  calibration data, and failures.
- Model and widget tests cover table filtering, plot selection, heatmap mapping,
  qualification aggregation display, process arguments, JSON rejection, and source
  navigation without pixel-fragile screenshots.
- Failed runs preserve the last valid displayed result and keep stdout/stderr
  behavior compatible.
- The complete unfiltered CTest suite passes.

## Phase 2 - LLM integration (deferred)

Phase 2 starts only after Phase 1 language and JSON contracts are stable. AI remains
an application-layer assistant: compiler validation is authoritative, generated
source is separate from approved source, and execution requires user approval.

### M7 - Provider integration

Add a small provider-neutral application interface with an Ollama adapter first.
Keep compiler/runtime targets AI-free. Configuration and an assistant panel belong
to Studio. Tests use injected deterministic fakes and never require a network or a
running model service.

### M8 - Natural language to HorusRF

Supply the versioned language specification and curated examples to the provider,
validate generated source through the CLI/compiler, return structured diagnostics,
and permit a bounded number of repairs. Studio displays a proposal and diff for
review; it never automatically replaces approved source or starts execution.

### M9 - Assisted result analysis

Ground explanations in versioned structured results. The assistant may summarize
measurements, locate failed regions, explain calibration surfaces, and propose
source edits or follow-up measurements. It must distinguish observations from
inference and must not invent unavailable measurements.

## Phase 3 - Advanced RF engineering (deferred)

### M10 - Complex measurements and S-parameters

Design strong complex-domain quantities such as complex ratio/reflection
coefficient, impedance, phase, VSWR, and S-parameters before adding syntax. Define
port validation and exact dimensions for `abs`, `phase`, `real`, `imag`, `log10`,
and `vswr`. Add explicit device capabilities and a deterministic simulated two-port
network without exposing its private transfer function.

### M11 - Smith chart visualization

Add a reusable Studio component for S11/S22 traces, normalized impedance,
frequency markers, selected-point values, and comparative traces. It consumes the
generic structured result model rather than a special simulator API.

### M12 - Advanced examples

Add realistic filter, amplifier gain/compression, antenna matching,
temperature-calibration, frequency-response, and passband/stopband examples.
Language additions require a concrete example and must pass through the normal
grammar-to-documentation workflow.

### M13 - Agentic experimentation (optional)

After the safety and approval model is designed, an assistant may propose a refined
experiment based on completed results. Every generated revision is validated and
shown to the user, and every execution requires explicit approval. Real hardware
also remains subject to independent limits and authorization. MCP operations such
as language-spec retrieval, validation, planning, simulation, and result retrieval
are considered only when an external-client use case justifies them.

## Milestone execution protocol

For each milestone:

1. Re-inspect the affected contracts and record any approved refinement.
2. Add tests at the lowest layer that proves each behavior, while retaining
   separate end-to-end, equivalence, CLI-process, and Studio suites.
3. Implement only that milestone.
4. Run focused tests during development.
5. Run the full unfiltered CTest suite after cross-layer changes.
6. Update grammar, language/architecture documentation, examples, and public CLI
   documentation in the same change.
7. Review the complete diff and compatibility fixtures.
8. Commit the milestone separately only when explicitly asked to create commits.
9. Stop and report results before beginning the next milestone.

Do not implement milestones concurrently without explicit approval. A milestone is
not complete merely because its happy path works; its diagnostics, compatibility
fixtures, documentation, and full-suite acceptance must also pass.
