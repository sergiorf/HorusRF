# Slice 4 Implementation Plan — Device-Bound Point Execution

## Status and objective

**Status:** defined; implementation not started.

Slice 4 binds the typed, device-independent IR from Slice 3 to the smallest
physical execution boundary needed by M0. It adds the RF device abstraction, a
deterministic simulated implementation, and a runtime evaluator that executes
the IR dataflow for one caller-supplied frequency point.

The pipeline after this slice will be:

```text
source -> parse -> analyze -> lower -> execute one point -> device
                                                        -> simulator
```

Point execution is deliberate. Slice 4 proves that every IR operation can be
evaluated and that the abstract `MeasurePower` operation causes correctly
ordered device calls. Slice 5 remains responsible for generating the inclusive
frequency sweep, accumulating samples and calibration entries, applying the
calibration for verification, and calculating metrics.

## Inputs inherited from Slice 3

The runtime consumes `ir::Program` from `include/horusrf/ir/ir.hpp`. It may rely
on accepted Slice 3 output having these properties:

- `ValueId`s are contiguous, stable, and begin at zero;
- `Program::values` is in dependency order and operands point backwards;
- reference, sweep, and measurement IDs identify `Power`, `Frequency`, and
  `Power` values respectively;
- constants contain strong canonical domain values;
- each definition has the type required by its concrete operation;
- calibration indexes identify the active sweep frequency value; and
- no device, callback, or executable function is embedded in the IR.

The runtime still validates the complete program before causing device side
effects. Hand-assembled malformed IR must fail safely rather than indexing an
invalid slot, extracting the wrong variant alternative, or partially
configuring a device.

## Scope

Implement:

- the minimal `device::RfDevice` interface for frequency configuration, output
  power configuration, and external power measurement;
- `device::SimulatedRfDevice`, with deterministic private TX-path error state;
- a runtime value representation using the existing strong domain types;
- validation of executable IR invariants before device access;
- single-point interpretation of every Slice 3 IR definition;
- binding of the IR sweep value to a supplied frequency;
- binding of `MeasurePower` to ordered `RfDevice` calls;
- an owning point-evaluation result addressable by `ValueId`;
- structured execution diagnostics for malformed IR; and
- device, simulator, and runtime unit tests, including the canonical source
  pipeline through one selected point.

## Non-goals

Do not implement:

- inclusive sweep generation, point counting, or range divisibility policy;
- a whole-experiment `execute` loop or `CharacterizationResult`;
- characterization sample rows or calibration table population;
- calibration lookup, interpolation, or application;
- corrected power, residual error, RMS error, maximum error, or improvement
  metrics;
- the independent procedural C++ characterization;
- the command-line runner, CSV output, or final end-to-end integration tests;
- device discovery, transport selection, retries, timeouts, concurrency,
  cancellation, or asynchronous I/O;
- a programmable external instrument hierarchy;
- random noise, public simulator error inspection, or test-only simulator
  back doors;
- new DSL syntax, semantic rules, IR operations, or optimization; or
- third-party dependencies.

## Architecture boundary

```text
ir::Program
    |
    | runtime::execute_point(program, frequency, device)
    v
runtime evaluator ---- pure definitions ----> typed value slots
    |
    | MeasurePower
    v
device::RfDevice
    |
    +---- device::SimulatedRfDevice
    +---- future physical adapter
```

The runtime depends on the IR, domain quantities, and abstract device API. It
must not depend on parser, semantic-model, or simulator headers. The simulator
depends on the abstract device API and domain quantities, but never on IR or
runtime. The abstract device header likewise has no compiler or runtime
dependency.

Tests may compose all layers, but production dependency direction must remain
one-way.

## Repository and build deliverables

Add this minimum structure:

```text
include/horusrf/device/rf_device.hpp
include/horusrf/device/simulated_rf_device.hpp
include/horusrf/runtime/diagnostics.hpp
include/horusrf/runtime/executor.hpp
src/device/simulated_rf_device.cpp
src/runtime/executor.cpp
tests/device/simulated_rf_device_tests.cpp
tests/runtime/executor_tests.cpp
```

Required CMake targets:

| Target | Kind | Responsibility |
|---|---|---|
| `horusrf_device` | interface library | Abstract RF device contract |
| `horusrf_simulator` | static library | Deterministic simulated device |
| `horusrf_runtime` | static library | IR validation and point execution |
| `horusrf_simulator_tests` | executable/CTest test | Simulator contract and determinism |
| `horusrf_runtime_tests` | executable/CTest test | Interpreter, device binding, failures |

`horusrf_simulator` links to `horusrf_device` and `horusrf_domain`.
`horusrf_runtime` links to `horusrf_ir`, `horusrf_device`, and
`horusrf_domain`; it does not link to the simulator. Runtime tests may link to
parser, semantic, IR, runtime, and simulator to exercise the real pipeline.
Define `HORUSRF_SOURCE_DIR` for the runtime test target so it can load the
canonical fixture.

Because the interface header exposes domain quantities, `horusrf_device`
propagates `horusrf_domain` as an interface dependency.

Reuse `tests/test_support.hpp` and the existing `build/` directory. Do not add
a test framework or an alternate build tree.

## Device contract

The M0 device boundary is exactly:

```cpp
namespace horusrf::device {

class RfDevice {
public:
    virtual ~RfDevice() = default;

    virtual void setFrequency(domain::Frequency frequency) = 0;
    virtual void setOutputPower(domain::Power power) = 0;
    [[nodiscard]] virtual domain::Power measurePower() = 0;
};

} // namespace horusrf::device
```

Equivalent project-consistent naming is acceptable, but do not add getters,
an error-function API, calibration access, or generic string commands. The
device represents the complete M0 physical setup: the RF tester produces the
configured output and calibrated external equipment observes actual power.

For one `MeasurePower` evaluation, the runtime call order is exactly:

```text
setFrequency(current point)
setOutputPower(reference power)
measurePower()
```

Both setters occur before every measurement. This makes each point execution
self-contained and avoids relying on state left by an earlier point. Pure IR
operations cause no device calls. The runtime makes exactly one measurement
call for the single `MeasurePower` definition in a valid M0 program.

The M0 interface has no status channel. Exceptions raised by a device adapter
propagate through `execute_point`; the runtime must not relabel an unknown
hardware failure as malformed IR. A richer device error protocol is deferred
until real adapter requirements exist.

## Deterministic simulator

`SimulatedRfDevice` is `final` and stores configured frequency and output power
privately. Each setting is absent until its corresponding setter is called.
Calling `measurePower()` before both settings exist throws `std::logic_error`
and does not invent defaults.

Once configured, it returns:

```text
normalized(f) = (f - 2.40e9 Hz) / 100.0e6 Hz
tx_error(f)   = 0.35 dB + 0.20 dB * sin(2*pi*normalized(f))
measured(f)   = configured_output_power + tx_error(f)
```

Use a local high-precision constant for pi; do not depend on platform-specific
math macros. The model has no random component, hidden clock, static mutable
state, or history dependence. Reconfiguration replaces the corresponding
setting, and repeated measurements with unchanged settings return equal power.

The hidden error is an implementation detail. Do not expose an accessor,
inject the error function, or make tests friends of the simulator. Tests
observe behavior only through `RfDevice` operations and documented numeric
outputs. Frequencies outside the canonical range still use the same formula;
range policy belongs to the experiment runtime, not the device.

## Runtime values and point result

Use strong quantities for all runtime storage:

```cpp
using RuntimeValue = std::variant<
    domain::Frequency,
    domain::Power,
    domain::PowerDelta>;

struct PointEvaluation {
    domain::Frequency frequency;
    std::vector<RuntimeValue> values;

    [[nodiscard]] const RuntimeValue& at(ir::ValueId id) const;
};
```

`values[id.value]` corresponds to the IR value with that ID. `PointEvaluation`
owns every value and contains no reference to the IR or device. `at` performs a
bounds check and throws `std::out_of_range` for a caller-supplied invalid ID;
typed convenience getters are unnecessary in this slice.

Do not store raw doubles, source unit strings, `std::any`, pointers to variant
members, or one map per value. Dense slots match the accepted contiguous IR
identity contract and make dependency evaluation explicit.

Calibration descriptors are not copied into `PointEvaluation`. Their
correction expressions are already among the evaluated slots; Slice 5 will
pair those slot values with calibration metadata while accumulating points.

## Execution API and diagnostics

Use a non-throwing result for malformed-program failures:

```cpp
enum class DiagnosticCode {
    InvalidValueId,
    InvalidDefinitionOrder,
    TypeMismatch,
    InvalidProgramBinding,
    InvalidOperation,
};

struct ExecutionResult {
    std::optional<PointEvaluation> evaluation;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] ExecutionResult execute_point(
    const ir::Program& program,
    domain::Frequency frequency,
    device::RfDevice& device);
```

`ok()` is true exactly when one evaluation is present and diagnostics are
empty. Diagnostics contain a stable code, explanatory message, and the most
specific available source span. Tests assert code and span before message
fragments.

Malformed IR is an internal pipeline/integration error, not a source-language
error. Any validation failure returns no evaluation and causes zero device
calls. Validate the full structure before evaluating any definition. It is
acceptable to report the first deterministic failure; do not continue into
unsafe dependent checks merely to collect more diagnostics.

Domain factory exceptions and device exceptions are not converted to execution
diagnostics. Accepted lowered IR already contains finite quantities, the input
`Frequency` is already a valid domain value, and the abstract device API does
not provide enough information to classify arbitrary exceptions.

## Preflight validation

Before touching the device, verify at minimum:

1. value IDs are contiguous, unique, and match their vector positions;
2. every definition alternative agrees with the declared `ValueType`;
3. every operand ID exists, precedes its consumer, and has the required type;
4. `Constant` variant alternatives agree with the declared type;
5. `Alias` preserves its operand type;
6. unary and binary definitions follow the closed Slice 3 type table;
7. program reference, sweep-frequency, and measurement IDs exist and point to
   `ReferencePower`, `SweepFrequency`, and `MeasurePower` definitions;
8. those three bindings have types `Power`, `Frequency`, and `Power`;
9. there is exactly one definition for each M0 base operation;
10. named values reference existing values with matching declared types; and
11. calibrations have an existing `PowerDelta` correction and exactly the
    active `Frequency` sweep index, with matching metadata.

Do not duplicate parsing or semantic analysis. Validation concerns executable
IR structure only. The supplied point need not equal a generated member of the
sweep; Slice 5 owns range generation and calls this API with each chosen point.

## Definition evaluation

Evaluate `Program::values` once in stored order and append one `RuntimeValue`
per definition:

| IR definition | Runtime behavior |
|---|---|
| `Constant` | copy its strong domain value |
| `ReferencePower` | copy the embedded `Power` |
| `SweepFrequency` | bind the caller-supplied frequency |
| `MeasurePower` | configure frequency and reference power, then measure |
| `Alias` | copy the operand value |
| `NegatePowerDelta` | apply domain unary `-` |
| `AddPowerDelta` | apply `PowerDelta + PowerDelta` |
| `ApplyPowerDelta` | apply `Power + PowerDelta` |
| `PowerDifference` | apply `Power - Power` |

Use the existing domain operators. Do not reproduce dBm/dB arithmetic with raw
doubles. Operation dispatch is exhaustive over `ir::Definition`; adding a new
IR alternative must cause a compile-time implementation decision rather than
silently falling through.

When `MeasurePower` is reached, read the already evaluated program sweep and
reference slots. Do not assume their numeric IDs or search by names. Store the
returned `Power` in the measurement slot, after which later pure expressions
may consume it normally.

## Canonical point behavior

Lower the real canonical fixture and execute it at `2.425 GHz`. The logical
evaluation is:

```text
v0 = ReferencePower(-10 dBm)
v1 = SweepFrequency(2.425 GHz)
device.setFrequency(v1)
device.setOutputPower(v0)
v2 = MeasurePower()                         # -9.45 dBm
v3 = PowerDifference(v2, v0)               # +0.55 dB, name error
v4 = PowerDifference(v0, v2)               # -0.55 dB, calibration correction
```

At `2.425 GHz`, the sine term is `+1`; use tolerances appropriate to `double`
rather than exact equality for the calculated values. This test proves the
complete compiler-to-device path for a point but is not the Slice 7
whole-experiment integration test.

## Required tests

### Device interface and spy tests

Cover:

- destruction through `RfDevice*`;
- a recording fake that captures exact call order and arguments;
- one point causes frequency, output power, and measurement calls in order;
- all pure definitions are evaluated without additional device activity; and
- a throwing fake's exception propagates from `execute_point`.

### Simulator tests

Cover:

- measurement before either setting fails;
- measurement with only frequency or only output power fails;
- setter order does not change the final measurement;
- canonical start, quarter, midpoint, three-quarter, and end frequencies match
  the documented sinusoidal model;
- changing output power shifts measured power by the same `PowerDelta`;
- repeated measurements and two fresh identically configured instances agree;
- reconfiguration replaces prior state; and
- behavior is observable only through the abstract interface.

### Runtime operation matrix

Use lowered small valid programs, not manually assembled IR, to exercise:

- all three constant alternatives;
- reference and supplied sweep bindings;
- measurement;
- aliases for `Frequency`, `Power`, and `PowerDelta`;
- delta negation and addition;
- applying a delta to power;
- subtracting two powers;
- nested dependencies and named values; and
- access to evaluated calibration-correction slots.

Assert runtime variant types and domain values for every result. At least one
test uses a recording fake so simulator arithmetic cannot conceal incorrect
runtime ordering or arguments.

### Defensive failure tests

Construct minimally malformed IR copies to verify no evaluation and zero
device calls for:

- non-contiguous, duplicated, or mismatched value IDs;
- missing, forward, or out-of-range operands;
- definition/type and constant/type mismatches;
- invalid reference, sweep, or measurement bindings;
- duplicate or missing M0 base definitions;
- invalid operation operand types;
- inconsistent named-value metadata; and
- invalid calibration correction or index metadata.

Do not repeat the full semantic or lowering rejection suites. These tests
protect the executor from unsafe public IR construction.

### Canonical pipeline point test

Read `examples/tx_path_characterization.hrf`, parse, analyze, lower, and execute
one point through `SimulatedRfDevice`. Assert successful prior stages, five
runtime slots, the supplied frequency, reference and measured powers, derived
error, correction, calibration index identity, and expected numeric values.

## Implementation sequence

1. Add the abstract device header and CMake interface target.
2. Implement the simulator state machine and deterministic model with focused
   tests through the abstract interface.
3. Define runtime values, point evaluation, diagnostics, and the public
   `execute_point` result contract.
4. Implement side-effect-free preflight validation for base bindings, IDs,
   types, operands, named values, and calibration metadata.
5. Implement dense slot evaluation for constants, reference, supplied sweep,
   aliases, and pure arithmetic definitions.
6. Bind `MeasurePower` to the three ordered device calls and its result slot.
7. Add recording-fake tests for call order, arguments, cardinality, and the
   complete operation matrix.
8. Add malformed-IR tests proving validation occurs before side effects.
9. Add the canonical fixture point test through the real simulator.
10. Configure, build, and run all CTest tests from the repository root using
    only `build/`.

Each step keeps parser, domain, semantic, and IR tests green. Do not pull Slice
5 result aggregation into this work merely to simulate whole-experiment
completion.

## Acceptance checklist

- [ ] Existing Slice 1–3 behavior remains green.
- [ ] CMake configure and C++20 build succeed using only `build/`.
- [ ] CTest discovers and passes simulator and runtime tests with all earlier
      tests.
- [ ] `RfDevice` exposes only the three required M0 operations and has a
      virtual destructor.
- [ ] Runtime depends on the abstract device interface, never the simulator.
- [ ] Simulator depends on neither IR nor runtime.
- [ ] Simulator requires both settings, is deterministic, and exposes no
      hidden-error accessor.
- [ ] The documented private TX-error model produces expected measurements.
- [ ] All IR definition alternatives evaluate through strong domain types.
- [ ] One point binds the supplied frequency and embedded reference correctly.
- [ ] `MeasurePower` makes exactly three device calls in the required order.
- [ ] Point evaluation owns a dense value for every IR `ValueId`.
- [ ] The canonical fixture executes at one selected frequency and produces
      the expected error and correction.
- [ ] Malformed IR returns structured diagnostics and causes no device calls.
- [ ] Device exceptions propagate without being misclassified.
- [ ] No sweep iteration, characterization aggregation, calibration
      application, metrics, procedural path, CSV, or CLI is added.

## Slice boundary and handoff

Slice 4 hands Slice 5 a deterministic `RfDevice`, a production-independent
simulator, and a point evaluator that materializes every typed IR value for a
given frequency. Slice 5 can generate the inclusive sweep, call
`execute_point` for each frequency, read the program's measurement, named
error, and calibration-correction slots, and build characterization results.

Slice 4 intentionally does not decide how many points exist, whether an end
point is included under floating-point rounding, how samples are represented,
or how corrections are accumulated and applied. Those decisions remain the
complete-characterization work of Slice 5.
