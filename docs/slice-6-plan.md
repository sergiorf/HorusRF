# Slice 6 Implementation Plan — Procedural Reference

## Status and objective

Slice 6 is the active planned slice. Slice 5 completed the declarative path
from source text through whole-characterization execution and left reusable
domain result and metric types. Slice 6 adds the independent procedural C++
version of the same canonical TX-path experiment.

The procedural implementation must visibly perform the experiment itself. It
configures an `RfDevice`, measures every frequency, derives each result field,
builds the calibration artifact, and invokes the shared domain metric helper.
It must not parse HorusRF source, construct or lower an AST, consume IR, or call
the HorusRF runtime. This independence is the reason the example exists: the
two paths share physical and result primitives but not experiment
orchestration.

At the end of this slice, a fresh `SimulatedRfDevice` run through the procedural
path produces the same canonical 101-point result contract already established
for the declarative path. Slice 7 remains responsible for a user-facing CLI,
CSV serialization, and an integration comparison that invokes both paths.

## Scope

### In scope

- one procedural example module for the canonical TX-path characterization;
- fixed, explicit canonical experiment parameters in that module;
- direct ordered calls through the public `device::RfDevice` interface;
- construction of the existing `domain::CharacterizationResult` model;
- construction of the existing named `tx_power` calibration artifact;
- reuse of `domain::calculate_metrics` after sample collection;
- focused tests with a recording fake device;
- a canonical run with `device::SimulatedRfDevice`;
- CMake targets that make the example reusable by Slice 7; and
- continued success of every Slice 1–5 test from the single `build/` tree.

### Out of scope

- changes to the HorusRF grammar or canonical `.hrf` source;
- lexer, parser, AST, semantic, IR, or runtime changes;
- calling `runtime::execute_characterization` or `runtime::execute_point`;
- extracting orchestration shared by the declarative and procedural paths;
- a general public sweep/range abstraction;
- user-configurable experiment parameters;
- interpolation or later application of a stored calibration artifact;
- command-line argument processing or a `main` executable;
- CSV or other file output;
- a direct two-run comparison harness; and
- third-party dependencies.

If implementing Slice 6 exposes a defect in a lower layer, fix and test that
defect in its owning layer, but do not use the opportunity to broaden the
procedural API or begin Slice 7 work.

## Existing contracts to reuse

The implementation reuses the completed `device::RfDevice` API and the domain
result model unchanged. In particular, it returns:

```cpp
struct CharacterizationResult {
    std::vector<CharacterizationSample> samples;
    CalibrationArtifact calibration;
    CharacterizationMetrics metrics;
};
```

It uses the existing strong quantity factories and operators:

```text
measured - reference   -> PowerDelta
reference - measured   -> PowerDelta
measured + correction  -> Power
corrected - reference  -> PowerDelta
```

It must not reproduce those operations with arithmetic on raw `double` values.
Raw numeric values are permitted only when declaring the fixed canonical
frequency and power constants and when tests inspect quantities.

## Files and CMake targets

Add:

```text
examples/cpp/tx_path_characterization.hpp
examples/cpp/tx_path_characterization.cpp
tests/examples/tx_path_characterization_tests.cpp
```

The header exposes only the procedural entry point. A project-consistent API is:

```cpp
namespace horusrf::examples {

[[nodiscard]] domain::CharacterizationResult
run_tx_path_characterization(device::RfDevice& device);

} // namespace horusrf::examples
```

The function name may be adjusted for established naming conventions, but it
must remain specific to this canonical procedural example. Do not present it as
a generic characterization runtime.

Add a static CMake target such as `horusrf_procedural_example` containing the
example `.cpp`. Give consumers access to the example header and link the target
publicly only to `horusrf_device` and `horusrf_domain`. Enable C++20 and the
repository warning policy.

Add `horusrf_procedural_example_tests` as a separate test executable containing
the focused test source. It links to `horusrf_procedural_example` and
`horusrf_simulator`, includes `tests/` for `test_support.hpp`, uses the standard
warning policy, and has its own CTest registration.

Neither the example target nor its test target may link to `horusrf_parser`,
`horusrf_semantic`, `horusrf_ir`, or `horusrf_runtime`. This link boundary is a
build-level statement of orchestration independence, not merely a source-code
convention. The example must likewise include no headers from those layers.

Do not add an executable with `main` in this slice. The static example target
lets tests exercise the implementation and gives Slice 7 a reusable entry point
without prematurely choosing CLI behavior.

## Canonical experiment constants

The procedural module owns the canonical values directly:

| Parameter | Canonical value |
|---|---:|
| Reference/output power | `-10 dBm` |
| First frequency | `2.40 GHz` (`2.40e9 Hz`) |
| Last frequency | `2.50 GHz` (`2.50e9 Hz`) |
| Frequency step | `1 MHz` (`1.0e6 Hz`) |
| Point count | `101` |
| Artifact name | `tx_power` |

Keep these constants private to the implementation unless a concrete Slice 7
consumer proves that a public declaration is needed. Do not read or parse
`examples/tx_path_characterization.hrf`; duplicating the experiment parameters
is intentional because the procedural path is an independent reference.

Make the point count explicit or derive it once from fixed integral-Hz constants
with a compile-time assertion. The implementation must make it obvious that
both endpoints are included and that the expected count is 101. There is no
need to duplicate Slice 5's general malformed-sweep validation for constants
controlled by the program.

## Independent orchestration

Initialize an owning `domain::CharacterizationResult`, set its calibration name
to `tx_power`, and reserve 101 elements in the sample, dimension, and correction
vectors.

For point index `i` from 0 through 100, calculate the frequency independently:

```text
frequency(i) = 2.40e9 Hz + i * 1.0e6 Hz
```

Index-based generation avoids accumulated floating-point drift and exactly
matches the canonical endpoints. At each frequency, perform this sequence:

1. Call `device.setFrequency(frequency)`.
2. Call `device.setOutputPower(reference_power)`.
3. Call `device.measurePower()` exactly once.
4. Calculate `error = measured_power - reference_power`.
5. Calculate `correction = reference_power - measured_power`.
6. Calculate `corrected_power = measured_power + correction`.
7. Calculate `residual_error = corrected_power - reference_power`.
8. Append one `CharacterizationSample` containing those typed values.
9. Append the same frequency and correction to the artifact vectors.

After all points have been collected, calculate metrics exactly once with
`domain::calculate_metrics(result.samples)`, store the returned metrics, and
return the completed owning result.

The implementation must not call a helper from the runtime to generate the
sweep or evaluate a point. Small private helpers local to the procedural source
are acceptable only when they improve clarity without hiding the ordered device
interaction that this example is intended to demonstrate.

## Result invariants

On successful return:

- `samples.size()` is exactly 101;
- sample frequencies are strictly ascending from `2.40e9` through `2.50e9 Hz`;
- adjacent sample frequencies differ by `1.0e6 Hz`;
- every sample reference power is `-10 dBm`;
- one and only one device measurement produced each sample;
- `error` is the measured power minus the reference power;
- `correction` is the reference power minus the measured power;
- `corrected_power` is the measured power plus that correction;
- `residual_error` is corrected power minus reference power;
- the artifact name is `tx_power`;
- artifact dimensions and corrections each contain 101 entries;
- artifact entry `i` equals sample `i`'s frequency and correction; and
- metrics are computed from the finished sample vector using the shared helper.

The result owns all of its values and contains no references to the device or
to temporary local storage.

Because the canonical correction is derived from the same measurement, the
ideal arithmetic result has zero residual at every point. Tests should use the
same tight `double` tolerance already used by the Slice 5 canonical test rather
than depend on bitwise equality for derived floating-point values.

## Equivalence contract

For the same fresh deterministic simulator, the procedural result must satisfy
the same observable canonical contract as the declarative result:

- the same 101 frequencies, reference powers, and measured powers;
- the same pointwise errors and `tx_power` corrections;
- the same corrected powers and residual errors; and
- the same aggregate metric values within the established tolerances.

Slice 6 proves this without importing compiler/runtime layers into the
procedural test. The canonical simulator test asserts the known representative
values and full result invariants already asserted for the declarative path:

| Point | Frequency | Expected uncorrected error |
|---:|---:|---:|
| 0 | `2.40 GHz` | approximately `+0.35 dB` |
| 25 | `2.425 GHz` | approximately `+0.55 dB` |
| 50 | `2.45 GHz` | approximately `+0.35 dB` |
| 75 | `2.475 GHz` | approximately `+0.15 dB` |
| 100 | `2.50 GHz` | approximately `+0.35 dB` |

It also checks that corrected RMS is below a small absolute tolerance and below
`0.1 * uncorrected_rms_error_db`. A direct field-by-field invocation of both
orchestration paths belongs to Slice 7's integration proof.

## Failure and side-effect contract

The procedural function has no diagnostic result wrapper because its inputs are
fixed valid constants and `RfDevice` exposes failures as exceptions. Device and
domain exceptions propagate unchanged.

If a device throws:

- do not catch and relabel the exception;
- do not retry the measurement;
- do not issue calls for later points; and
- do not expose a partial `CharacterizationResult`, because the function has not
  returned.

Physical calls completed before the exception cannot be rolled back. This
matches the existing device/runtime exception policy.

## Required tests

### Recording-device orchestration test

Use a fake `RfDevice` that records every call and returns deterministic measured
powers. Assert:

- exactly 303 calls: frequency, output power, measurement for each of 101 points;
- exactly 101 measurements;
- strict three-call ordering at every point;
- the first, second, and final configured frequencies are `2.40e9`, `2.401e9`,
  and `2.50e9 Hz`;
- every configured output power is `-10 dBm`;
- each returned sample uses the fake's measured value for that point;
- all seven sample fields satisfy the typed arithmetic contract;
- sample order matches call order;
- artifact ordering and cardinality match the samples; and
- metrics equal a fresh `calculate_metrics(result.samples)` calculation.

Choose fake measurements that vary by point so an accidental reuse of a prior
measurement or a reordered sample is observable.

### Simulator acceptance test

Run the procedural entry point with a fresh `SimulatedRfDevice`. Assert all
canonical result and representative-value expectations in the equivalence
contract. Check all 101 rows for the reference, correction, corrected power,
residual, and artifact alignment invariants.

Run a second fresh simulator and verify corresponding physical and derived
values are deterministic. Do not inspect simulator-private state or reproduce
its sine formula in production or test code.

### Exception propagation test

Configure a fake to throw on a measurement after at least one successful point.
Assert that:

- the original exception type and message reach the caller;
- calls stop at the throwing measurement;
- there is no retry; and
- no completed result is available to the caller.

An additional first-measurement throw may be covered if useful, but is not a
substitute for proving behavior after earlier physical side effects.

### Dependency-boundary check

Keep dependency independence mechanically visible in CMake and includes. The
procedural example and its test must build and run without any parser, semantic,
IR, or runtime target on their link lines. Source review or a small textual CMake
assertion is sufficient; do not add a dependency-analysis framework.

### Regression tests

All existing Slice 1–5 tests remain green. In particular, Slice 5's canonical
source-pipeline test remains unchanged and continues proving the declarative
path independently.

## Implementation sequence

1. Add the procedural example header with the experiment-specific entry point.
2. Add a domain/device-only static target for the example.
3. Declare private canonical constants and reserve the complete result storage.
4. Implement index-based generation of the 101 frequencies.
5. Implement the explicit three-call device sequence for every point.
6. Derive each sample and artifact field with strong quantity operators.
7. Calculate metrics once from the completed sample vector and return the result.
8. Add the recording-device orchestration and result-invariant test.
9. Add exception-propagation coverage.
10. Add the fresh-simulator canonical and deterministic acceptance test.
11. Confirm the example/test dependency graph contains no compiler or runtime
    target.
12. Configure, build, and run the complete CTest suite using only `build/`.

Keep the implementation readable as an example throughout this sequence. Avoid
generic abstractions that make the physical procedure harder to see.

## Acceptance checklist

- [ ] The procedural example has an experiment-specific reusable entry point.
- [ ] Its target links only to domain/device infrastructure.
- [ ] Its source includes no parser, AST, semantic, IR, or runtime headers.
- [ ] It does not read or parse the `.hrf` example.
- [ ] It does not call declarative runtime execution or share orchestration with
      that path.
- [ ] Canonical parameters are explicit and produce 101 inclusive points.
- [ ] Frequency generation is index-based and has no accumulated step drift.
- [ ] Every point makes exactly one frequency call, one output-power call, and
      one measurement call in that order.
- [ ] All sample calculations use strong domain quantity operators.
- [ ] The result contains 101 complete, ordered, owning samples.
- [ ] The `tx_power` artifact contains the same 101 ordered frequency/correction
      pairs as the samples.
- [ ] Shared metrics are calculated once from the completed sample vector.
- [ ] The recording fake proves call order, values, cardinality, and alignment.
- [ ] A fresh simulator produces the established representative error values.
- [ ] Correction materially reduces RMS error and yields near-zero residuals.
- [ ] Equivalent fresh simulator runs are deterministic.
- [ ] Device exceptions propagate and stop later calls without returning a
      partial result.
- [ ] No CLI, CSV writer, general sweep API, new DSL syntax, or third-party
      dependency is introduced.
- [ ] CMake configuration, compilation, and all CTest tests pass using only the
      repository's existing `build/` directory.

## Slice boundary and handoff

Slice 6 hands Slice 7 two independently orchestrated implementations of the
same experiment:

```text
HorusRF source -> parser -> semantic -> IR -> runtime -> CharacterizationResult
Procedural C++ ---------------------------> device -> CharacterizationResult
```

They share strong quantities, `RfDevice`, `SimulatedRfDevice`, result value
types, and metric formulas. They do not share sweep or per-point orchestration.

Slice 7 can call the procedural entry point and the declarative pipeline in an
integration comparison, add the source-file CLI, and serialize their common
`CharacterizationResult` shape to CSV. Slice 6 deliberately does not choose CLI
arguments, output paths, CSV formatting, or mismatch-reporting policy.
