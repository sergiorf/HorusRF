# Slice 5 Implementation Plan — Complete Characterization

## Status and objective

**Status:** complete; implemented and accepted.

Slice 5 turns the device-bound point evaluator from Slice 4 into the complete
M0 characterization runtime. It generates the program's frequency sweep,
executes the typed IR at every point, accumulates samples and the named
calibration artifact, applies that artifact to the captured measurements for
verification, and calculates aggregate error metrics.

The pipeline after this slice will be:

```text
ir::Program -> validate once -> generate frequency points
                           -> execute typed dataflow at each point -> RfDevice
                           -> collect samples and calibration
                           -> apply correction for verification
                           -> calculate metrics
```

This slice completes the experiment represented by an already lowered
`ir::Program`. It does not add a source-file runner. Parsing, semantic analysis,
and lowering remain separate caller responsibilities until the Slice 7 CLI and
end-to-end integration boundary.

## Inputs inherited from Slice 4

The implementation builds on:

- `device::RfDevice`, whose three operations configure frequency, configure
  output power, and acquire the external power measurement;
- `runtime::execute_point`, which validates and evaluates all IR definitions at
  one caller-supplied frequency;
- `runtime::PointEvaluation`, whose dense slots are addressed by `ir::ValueId`;
- the invariant that a valid `MeasurePower` evaluation performs exactly
  `setFrequency`, `setOutputPower`, and `measurePower`, in that order;
- structured runtime diagnostics for malformed public IR; and
- the deterministic simulator, which exposes no hidden-error access.

For a valid program, the whole-characterization runtime may read the reference,
measurement, named-value, and calibration-correction slots from each point
evaluation. It must not reinterpret AST nodes, semantic expressions, or source
text.

The public `execute_point` contract remains supported. Slice 5 may refactor its
implementation so point and whole-program execution share one preflight
validator and one validated-point evaluator, but it must not change observable
point behavior.

## Scope

Implement:

- deterministic, index-based generation of the M0 frequency sweep;
- validation of sweep bounds, step, representable point count, and the M0
  calibration binding before any device access;
- whole-program execution through the existing typed IR evaluator;
- one owned `CharacterizationSample` per generated frequency;
- construction of the IR-declared named frequency-to-`PowerDelta` calibration
  artifact;
- verification-only correction application to each captured measurement;
- residual error calculation against the program reference power;
- uncorrected and corrected RMS errors;
- uncorrected and corrected maximum absolute errors;
- an explicit RMS improvement ratio;
- reusable result and metric types suitable for the independent Slice 6
  procedural implementation; and
- focused sweep, result, metric, orchestration, failure, and canonical-fixture
  tests.

## Non-goals

Do not implement:

- new DSL syntax or an `apply calibration` statement;
- interpolation, extrapolation, nearest-neighbor lookup, or multidimensional
  calibration tables;
- a second physical verification sweep or additional device measurements;
- the independent procedural C++ experiment;
- source loading, parsing, semantic analysis, or lowering inside the runtime;
- the command-line runner, CSV formatting, console reporting, or output files;
- the Slice 7 end-to-end executable test or procedural/runtime equivalence test;
- device retries, cancellation, progress callbacks, asynchronous execution, or
  partial-result recovery;
- random noise or access to simulator internals;
- changes to accepted quantity arithmetic; or
- third-party dependencies.

The verification performed here is mathematical application of the generated
artifact to the measurements from the same characterization run. A separate
acquisition pass would change the device-call contract and is outside M0.

## Architecture and dependency boundary

```text
                    +-------------------------------+
                    | reusable result/metric types  |
                    | horusrf/domain/results.hpp    |
                    +---------------+---------------+
                                    ^
                                    |
ir::Program -> runtime::execute_characterization -> CharacterizationResult
                                    |
                                    v
                         validated point evaluator
                                    |
                                    v
                             device::RfDevice
```

Place device-independent sample, artifact, result, and metric value types in
the domain layer so Slice 6 can reuse them without depending on IR or runtime.
The domain result implementation may depend only on domain quantities and the
C++ standard library. It must not include parser, semantic, IR, runtime, device,
or simulator headers.

The whole-characterization executor belongs to the runtime layer and depends
on IR, the abstract device API, and domain results. Runtime production code
must not depend on the simulator, parser, semantic analyzer, or concrete source
fixture.

## Repository and build deliverables

Add this minimum structure:

```text
include/horusrf/domain/results.hpp
include/horusrf/runtime/characterization.hpp
src/domain/results.cpp
src/runtime/characterization.cpp
tests/domain/results_tests.cpp
tests/runtime/characterization_tests.cpp
```

`src/domain/results.cpp` joins the existing `horusrf_domain` target.
`src/runtime/characterization.cpp` joins the existing `horusrf_runtime` target.
Add the new domain test source to `horusrf_domain_tests`. Add a dedicated
`horusrf_characterization_tests` executable and CTest entry linked to runtime,
simulator, parser, semantic, and IR so it can exercise both focused IR programs
and the real source pipeline. Define `HORUSRF_SOURCE_DIR` for that test target.

Reuse `tests/test_support.hpp` and the existing `build/` directory. Do not add a
test framework or another build tree.

## Public result model

Use strong quantities for every pointwise physical value:

```cpp
namespace horusrf::domain {

struct CharacterizationSample {
    Frequency frequency;
    Power reference_power;
    Power measured_power;
    PowerDelta error;
    PowerDelta correction;
    Power corrected_power;
    PowerDelta residual_error;
};

struct CalibrationArtifact {
    std::string name;
    std::vector<Frequency> dimensions;
    std::vector<PowerDelta> corrections;
};

struct CharacterizationMetrics {
    double uncorrected_rms_error_db{};
    double corrected_rms_error_db{};
    double maximum_absolute_error_db{};
    double corrected_maximum_absolute_error_db{};
    double rms_improvement_ratio{};
};

struct CharacterizationResult {
    std::vector<CharacterizationSample> samples;
    CalibrationArtifact calibration;
    CharacterizationMetrics metrics;
};

[[nodiscard]] CharacterizationMetrics calculate_metrics(
    std::span<const CharacterizationSample> samples);

} // namespace horusrf::domain
```

Equivalent project-consistent snake/camel naming is acceptable. Keep the
conceptual separation between sample rows, the reusable calibration artifact,
and aggregate metrics. Do not store raw frequency or power columns in parallel
vectors, and do not put runtime diagnostics or IR IDs in domain results.

`CalibrationArtifact::dimensions[i]` and `corrections[i]` form one table entry.
Both vectors have exactly the same nonzero size and the same ordering as
`CharacterizationResult::samples`. The name is copied from the IR calibration,
not hard-coded to `tx_power`.

The public structs own all values. They contain no references to the IR,
point-evaluation storage, or device.

## Whole-characterization API and failure contract

Add a distinct result name so the existing point `ExecutionResult` remains
unambiguous:

```cpp
namespace horusrf::runtime {

struct CharacterizationExecutionResult {
    std::optional<domain::CharacterizationResult> result;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] CharacterizationExecutionResult execute_characterization(
    const ir::Program& program,
    device::RfDevice& device);

} // namespace horusrf::runtime
```

`ok()` is true exactly when a result is present and diagnostics are empty.
Malformed IR, an invalid sweep, or an unsupported M0 calibration shape returns
no result and at least one structured diagnostic. Extend `DiagnosticCode` with
specific stable codes such as:

```cpp
InvalidSweep
SweepTooLarge
InvalidCalibrationBinding
```

Reuse the Slice 4 codes when an existing program invariant fails. Diagnostics
carry the most specific source span: the sweep span for range/count failures,
the calibration span for calibration-binding failures, and the existing value
or program span for ordinary IR failures.

All structural and sweep validation occurs before the first device call. The
executor may report the first deterministic failure. It does not return a
partially populated characterization.

Device exceptions and domain factory exceptions continue to propagate rather
than being mislabeled as malformed IR. If a device throws after earlier points
have executed, the executor cannot roll back those physical side effects, but
no partial result escapes. This is the same exception policy as
`execute_point`.

## Shared validation and evaluation internals

Refactor the Slice 4 implementation around two non-public operations:

```text
validate executable IR once
evaluate one already-validated point
```

The public `execute_point` performs validation and, on success, calls the
validated evaluator once. `execute_characterization` performs the same
validation once, validates its additional sweep/calibration constraints, and
calls the validated evaluator for every generated frequency.

This avoids revalidating all IR definitions at every point and, more
importantly, ensures malformed structure is discovered before any point causes
device side effects. The internal validated evaluator must not be exposed as a
way for callers to bypass checks.

Existing Slice 4 malformed-IR, call-order, and exception tests remain unchanged
and green after the refactor.

## M0 calibration selection

Complete characterization requires exactly one IR calibration descriptor. Its
accepted shape is:

- a nonempty name;
- correction type `PowerDelta`;
- exactly one index;
- index type `Frequency`;
- index ID equal to `program.sweep.frequency`; and
- an existing correction slot whose value type is `PowerDelta`.

The Slice 4 validator already checks the type and index invariants. Slice 5 adds
the exactly-one requirement because a single correction, corrected value, and
metric set is represented in each M0 sample. Zero or multiple calibrations
produce `InvalidCalibrationBinding` before device access. Supporting multiple
simultaneous verification artifacts requires a different result shape and is
deferred rather than selecting one by position or by the spelling `tx_power`.

At each point, append the evaluated sweep frequency to the artifact dimensions
and the evaluated calibration correction slot to its corrections. Never
recompute the correction as `reference - measured`; the artifact must reflect
the expression declared and lowered from the `.hrf` program.

## Frequency sweep generation policy

Generate points from the canonical `Frequency` values in `ir::Sweep`, not from
source literals or converted display units.

Preflight requires:

- finite start, end, and step values (normally guaranteed by domain factories);
- `step > 0 Hz`;
- `end >= start`; and
- a computed point count that is representable and no greater than the explicit
  M0 safety limit of `1'000'000` points.

The safety limit prevents malformed or impractical public IR from causing an
unbounded loop or allocation. Keep it as a named implementation constant and
cover over-limit rejection in tests; it is not new DSL syntax.

Use an integer point index and calculate each candidate independently:

```text
candidate(i) = start + i * step, for i = 0, 1, 2, ...
```

Perform count and candidate arithmetic in `long double`, then create each
public `Frequency` from the checked `double` value. Do not repeatedly add the
step to the preceding `double`, because accumulated rounding can omit a
mathematically reachable endpoint.

The range is inclusive on the step lattice:

- the start is always the first point;
- a point less than the end is included;
- a candidate within a documented floating-point tolerance of the end is
  snapped to the exact stored end value and included once;
- a candidate above that tolerance is not included; and
- when `(end - start)` is not an integral number of steps, do not invent a
  shorter final interval merely to append the end.

Use a scale-aware comparison tolerance, for example a small fixed multiple of
`std::numeric_limits<double>::epsilon()` times the largest magnitude involved.
Centralize it in the sweep generator rather than scattering approximate
comparisons through orchestration code.

Examples:

| Start | End | Step | Generated points |
|---:|---:|---:|---|
| `1 Hz` | `1 Hz` | `1 Hz` | `1` |
| `0 Hz` | `1 Hz` | `0.25 Hz` | `0, .25, .5, .75, 1` |
| `0 Hz` | `1 Hz` | `0.3 Hz` | `0, .3, .6, .9` |
| `2.40 GHz` | `2.50 GHz` | `1 MHz` | 101 points including both ends |

Keep sweep generation internal to the runtime in Slice 5. A separate public
range abstraction is unnecessary until another consumer needs it.

## Per-point orchestration

For each generated frequency, in ascending order:

1. evaluate every IR definition with the validated point evaluator;
2. read `program.reference` as `Power`;
3. read `program.measurement` as `Power`;
4. calculate the physical uncorrected error as
   `measured_power - reference_power`;
5. read the sole calibration descriptor's correction slot as `PowerDelta`;
6. calculate `corrected_power = measured_power + correction`;
7. calculate `residual_error = corrected_power - reference_power`;
8. append the sample; and
9. append the same frequency/correction pair to the calibration artifact.

The point evaluator continues to evaluate named derived values. For the
canonical program, tests must prove that the named `error` slot equals the
sample's physical error. The result model does not require a derived value to
be spelled `error`, however, and the runtime must not search source names to
perform the physical comparison.

Use the existing domain operators for all dBm/dB arithmetic. The orchestration
must not subtract or add raw doubles to reproduce quantity behavior.

There is exactly one device measurement per sample. Artifact creation,
correction application, residual calculation, and metrics are pure and cause
no further device calls.

## Metrics contract

Calculate metrics only after all samples have been accumulated:

```text
uncorrected_rms = sqrt(sum(error_db^2) / N)
corrected_rms   = sqrt(sum(residual_error_db^2) / N)
max_error       = max(abs(error_db))
corrected_max   = max(abs(residual_error_db))
improvement     = uncorrected_rms / corrected_rms
```

Accumulate squared values using `long double` and convert final public fields
to `double`. This is not a promise of arbitrary-precision results; it simply
avoids avoidable loss during aggregation.

`calculate_metrics` rejects an empty sample span with `std::invalid_argument`.
Normal runtime execution can never call it with an empty span because every
valid sweep includes its start point.

Define the improvement-ratio zero cases explicitly:

- if corrected RMS is nonzero, return the quotient;
- if corrected RMS is zero and uncorrected RMS is nonzero, return positive
  infinity; and
- if both RMS values are zero, return `1.0` because no improvement was needed.

The infinity applies only to a dimensionless reporting metric and is not
stored in a domain quantity. Slice 7 can choose how to render it.

Metrics operate only on the errors already stored in samples. They do not know
about IR, devices, calibrations, or simulator behavior. This permits Slice 6 to
use the identical formulas while retaining independent orchestration.

## Canonical characterization behavior

Load, parse, analyze, and lower
`examples/tx_path_characterization.hrf`, then execute it with a fresh
`SimulatedRfDevice`.

The result must contain:

- 101 samples from `2.40 GHz` through `2.50 GHz`, spaced by `1 MHz`;
- reference power `-10 dBm` at every point;
- one device measurement at every point;
- uncorrected error equal to measured minus reference;
- one artifact named `tx_power` with 101 ordered dimensions and corrections;
- correction equal to the IR-evaluated `reference.power - power` at each point;
- corrected power equal to measured plus correction;
- residual error equal to corrected minus reference; and
- aggregate metrics consistent with the stored samples.

For the deterministic model, the first, midpoint, and final uncorrected errors
are approximately `+0.35 dB`; the quarter point is approximately `+0.55 dB`;
and the three-quarter point is approximately `+0.15 dB`. Pointwise correction
from the same samples should reduce corrected RMS below both a small absolute
tolerance and `0.1 * uncorrected_rms_error`.

Assert numeric values with documented tolerances appropriate for `double`.
Do not assert hidden simulator state or duplicate its private function in
runtime production code.

## Required tests

### Result and metric tests

Cover:

- RMS and maximum calculations for a hand-checked sample set;
- sign independence of squared and absolute metrics;
- corrected metrics using residuals rather than corrections;
- nonzero finite improvement ratio;
- infinite improvement for a perfect correction of nonzero errors;
- ratio `1.0` when both error series are exactly zero;
- empty-input rejection; and
- ownership of sample and artifact vectors after source temporaries disappear.

### Sweep generation tests

Exercise sweep generation through `execute_characterization` with a recording
device. Cover:

- a one-point range;
- canonical 101-point generation;
- an exactly divisible small decimal range whose endpoint is susceptible to
  binary rounding;
- a non-divisible range that stops on the final lattice point below the end;
- positive start, zero start, and negative frequency values accepted by the
  current general `Frequency` quantity;
- nonpositive step rejection;
- reversed range rejection;
- over-limit or non-representable counts rejected before device calls.

Do not test sweep behavior by exposing a new public helper solely for tests.

### Orchestration and device-call tests

Use a recording fake and a small lowered program to assert:

- point order matches generated frequency order;
- each point performs exactly frequency, output-power, and measurement calls;
- the configured frequency and reference power match the result row;
- no additional calls occur for correction, residual, artifact, or metrics;
- sample and artifact ordering/cardinality remain aligned;
- correction comes from the IR correction slot, including a valid correction
  expression that is deliberately not simply `reference - measured`;
- corrected power and residual use strong domain operators; and
- repeated runs with fresh equivalent devices produce equal values.

### Validation and failure tests

Construct minimal malformed IR copies and verify no result and zero device calls
for:

- every Slice 4 structural failure category after validator refactoring;
- zero calibrations;
- multiple calibrations;
- invalid calibration index/correction metadata;
- zero or negative step;
- end before start;
- too many sweep points; and
- a point-count calculation that cannot be represented safely.

Verify that a device exception propagates. If it occurs after the first point,
assert that earlier calls happened but no partial `CharacterizationResult` was
returned.

### Canonical source-pipeline test

Read the actual canonical `.hrf` file and run parser, analyzer, lowering, whole
characterization, abstract device calls, and simulator. Assert the canonical
behavior listed above, including the named IR `error` value at representative
points and the calibration descriptor's `over frequency` identity.

This is a Slice 5 runtime acceptance test, not the Slice 7 executable/CSV
end-to-end test: it calls each compiler stage directly and produces no files.

### Regression tests

Keep all Slice 1–4 tests green. In particular, retain the public one-point
tests that prove:

- supplied frequency binding;
- complete operation evaluation;
- exact three-call measurement ordering;
- validation before point side effects; and
- device exception propagation.

## Implementation sequence

1. Add the reusable domain result structs and focused metric tests.
2. Implement `calculate_metrics`, including empty input and zero-ratio cases.
3. Define `CharacterizationExecutionResult`, the public whole-execution API,
   and new diagnostic codes.
4. Refactor Slice 4 validation and point evaluation into shared non-public
   operations without changing the public `execute_point` contract.
5. Add whole-runtime preflight for sweep and exactly-one-calibration rules.
6. Implement checked, index-based sweep count and point generation.
7. Execute validated points in order and build sample rows from typed slots.
8. Accumulate the named calibration artifact from its evaluated correction
   slot and active frequency index.
9. Apply each correction for verification and calculate residuals and metrics.
10. Add recording-device tests for ordering, cardinality, values, and failures.
11. Add the canonical source-pipeline characterization test through the real
    simulator.
12. Configure, build, and run all CTest tests from the repository root using
    only `build/`.

Each step keeps the existing parser, domain, semantic, IR, simulator, and point
runtime suites green. Do not introduce Slice 6 or Slice 7 scaffolding merely to
exercise the new reusable data types.

## Acceptance checklist

- [ ] Existing Slice 1–4 behavior remains green.
- [ ] CMake configure and C++20 build succeed using only `build/`.
- [ ] CTest discovers and passes result/metric and characterization tests with
      all earlier tests.
- [ ] Result and metric types depend only on the domain layer and standard
      library.
- [ ] `execute_point` retains its public behavior after shared-validator
      refactoring.
- [ ] Whole execution validates the complete program, sweep, and calibration
      before the first device call.
- [ ] Sweep generation is index-based, bounded, deterministic, and follows the
      documented endpoint policy.
- [ ] The canonical sweep contains exactly 101 ordered points including both
      endpoints.
- [ ] Each sample causes exactly one ordered device measurement sequence.
- [ ] Every sample stores strong typed reference, measurement, error,
      correction, corrected power, and residual values.
- [ ] The calibration artifact preserves the IR name and one ordered
      frequency/correction entry per sample.
- [ ] Corrections are read from evaluated IR slots, not reconstructed or read
      from simulator internals.
- [ ] Verification application causes no additional device measurements.
- [ ] RMS, maximum absolute error, and improvement metrics follow the specified
      formulas and edge-case behavior.
- [ ] The real canonical `.hrf` program completes through the simulator and
      materially reduces corrected RMS error.
- [ ] Malformed programs and invalid sweeps expose no partial result; preflight
      failures cause zero device calls.
- [ ] Device exceptions propagate without being converted to diagnostics.
- [ ] No procedural example, CLI, CSV, new syntax, interpolation, or
      third-party dependency is added.

## Slice boundary and handoff

Slice 5 hands Slice 6 reusable sample, artifact, result, and metric types plus a
fully working declarative characterization runtime. Slice 6 can independently
write the same sweep and device orchestration in procedural C++ while sharing
only quantities, the abstract device/simulator, result value types, and metric
calculation.

Slice 5 does not provide a convenience function that performs parsing through
execution, and the procedural example must not call
`execute_characterization`. Slice 7 will compare the two independent paths,
add the source-file CLI, serialize the common result to CSV, and establish the
final executable end-to-end proof.
