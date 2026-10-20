# HorusRF DSL evolution contract

Status: accepted Phase 1 design, 2026-10-17. This document completes M0 of the
[project roadmap](project-roadmap.md). It specifies work for M1-M6; unless a section
explicitly says otherwise, the described additions are not implemented yet.

Implementation status: M1 (typed frequency/temperature sweep compiler support), M2
(typed Cartesian runtime execution and environmental control), and M3 (multiple
named measurements) were completed on 2026-10-17. M4 (assertions and qualification)
and M5 (calibration surfaces and versioned structured results) were completed on
2026-10-18. M6 (full Studio JSON integration) was completed on 2026-10-18.

## Scope and audited baseline

The design was checked against the current grammar and parser, AST, semantic model,
IR and lowering, runtime validation/evaluation, device and simulator contracts,
domain results, CSV/CLI behavior, Studio's strict CSV model and process boundary,
and all CTest suites.

The compatibility baseline is the checked-in `tx_path_characterization.hrf` program:

- one frequency sweep, one `measure power`, and one frequency-indexed power
  calibration;
- the public `Frequency`, `Power`, and `PowerDelta` types and the existing
  `RfDevice`, `MeasurementDevice`, and execution overloads;
- frequency, output-power, then power-measurement equipment call order;
- the existing console summary, CLI exit codes 0 and 2-5, stable diagnostic codes,
  and silent successful `--check`;
- the seven-column legacy CSV, including header, row order, classic locale, and
  round-trip numeric precision;
- deterministic simulator observations without exposing its transfer function;
- strict Studio CSV loading, asynchronous `QProcess`, distinct output streams,
  source navigation, and last-good-result retention; and
- independent procedural/declarative implementations and their equivalence test.

Phase 1 changes are additive around that path. No Phase 1 feature moves Studio
inside the core dependency graph or lets runtime depend on a simulator class.

## Concrete syntax

The implemented Phase 1 syntax is now maintained only in the authoritative ANTLR
grammar [`grammar/HorusRf.g4`](../grammar/HorusRf.g4), with lexical conventions and the
syntax/semantic boundary documented in the
[`language reference`](language.md). It remains whitespace-delimited, line comments
begin with `//`, statement terminators are not present, and a source contains one
characterization.

`temperature` and `assert` are reserved words. `evm`, `frequency_error`, and `abs`
are ordinary identifier tokens resolved by semantic analysis; declaring `abs` as a
value is rejected semantically. `%`, `<`, `<=`, `>`, `>=`, `==`, and `!=` are
tokens, a lone `!` is invalid, and comparisons do not chain.

The legacy spelling `measure power` declares the value named `power`. The parser
also accepts `measure power = power` through its contextual-name compatibility
rule, but semantic analysis rejects that named form because `power` is a reserved
user value name. New source should use a descriptive name such as
`measure output_power = power`.

A Phase 1 characterization contains exactly one `reference power`, exactly one
frequency sweep, an optional temperature sweep, at least one measurement, and
exactly one calibration. The reference and every sweep must precede the first
measurement. All names share one
declaration namespace; sweep names and `reference.power` are reserved, and user
values must be unique. Measurements, derived values, and assertions otherwise use
written statement order and may refer only to earlier declarations. A calibration
may appear once all of its correction and index dependencies are available. The
JSON schema uses a calibration array for forward-compatible reading, but Phase 1
emits exactly one element.

### Representative valid program

```horusrf
characterize qualified_tx_path {
  reference power = -10 dBm
  sweep frequency 2.40 GHz .. 2.50 GHz step 5 MHz
  sweep temperature -20 degC .. 80 degC step 20 deltaC

  measure output_power = power
  measure evm_result = evm
  measure carrier_error = frequency_error
  derive power_error = output_power - reference.power

  assert abs(power_error) <= 1.5 dB
  assert evm_result <= 5 %
  assert abs(carrier_error) <= 20 kHz

  derive calibration tx_power {
    correction = reference.power - output_power
    over frequency, temperature
  }
}
```

This program has 21 frequency points, six temperature points, 126 operating
points, 378 observations, and 378 assertion outcomes.

### Representative invalid programs

Each fragment is invalid for the stated reason:

```horusrf
sweep temperature -20 degC .. 80 degC step 20 degC // absolute used as a delta
sweep frequency 2.5 GHz .. 2.4 GHz step 1 MHz       // reversed range
sweep temperature 0 K .. 20 K step -1 deltaK       // non-positive step
measure x = phase                                    // unknown operation
measure power_result = power
measure power_result = evm                           // duplicate name
derive early = output_power - reference.power        // use before declaration
assert output_power <= 1 dB                          // unlike dimensions
assert abs(reference.power) <= 10 dBm                // no abs(Power) overload
derive calibration c {
  correction = reference.power - output_power
  over temperature, frequency                        // not declaration order
}
```

## Types, units, and arithmetic

All source numbers must convert to finite canonical values. A converted absolute
temperature below 0 K is invalid. Strong types continue across public and layer
boundaries.

| Semantic type | Source units | Canonical unit | Conversion |
|---|---|---|---|
| `Frequency` | `Hz`, `kHz`, `MHz`, `GHz` | Hz | scale by 1, 1e3, 1e6, or 1e9 |
| `FrequencyDelta` | contextual `Hz`, `kHz`, `MHz`, `GHz` | Hz | same scales |
| `Power` | `dBm` | dBm | identity |
| `PowerDelta` | `dB` | dB | identity |
| `Ratio` | `%` | ratio | divide by 100 |
| `Temperature` | `K`, `degC` | K | K is identity; degC adds 273.15 |
| `TemperatureDelta` | `deltaK`, `deltaC` | K | both are identity scales |
| `Boolean` | none | none | produced by comparisons only |

Frequency and frequency-delta quantities intentionally share conventional RF
unit spellings. An uncontextualized frequency literal is `Frequency`. Where the
other operand has `FrequencyDelta` type, the semantic analyzer contextually types a
frequency literal as `FrequencyDelta`; thus `abs(carrier_error) <= 20 kHz` is legal.
This contextual rule applies only to a literal directly used with a known
`FrequencyDelta` operand and never converts a named `Frequency` value.

Absolute and delta temperature spellings are never contextual. In particular,
`20 degC` cannot become a step and `20 deltaC` cannot become an endpoint. Celsius
conversion is affine only for `Temperature`; temperature differences in Celsius
and Kelvin have equal magnitude.

The existing power rules remain unchanged. Phase 1 adds only these arithmetic
families:

| Expression | Result |
|---|---|
| `Temperature - Temperature` | `TemperatureDelta` |
| `Temperature + TemperatureDelta` | `Temperature` |
| `Temperature - TemperatureDelta` | `Temperature` |
| `TemperatureDelta + TemperatureDelta` | `TemperatureDelta` |
| `TemperatureDelta - TemperatureDelta` | `TemperatureDelta` |
| `-TemperatureDelta` | `TemperatureDelta` |
| `FrequencyDelta +/- FrequencyDelta` | `FrequencyDelta` |
| `-FrequencyDelta` | `FrequencyDelta` |
| `Ratio +/- Ratio` | `Ratio` |
| `-Ratio` | `Ratio` |

Operand order remains significant: delta-plus-absolute forms are rejected.
Arithmetic on absolute frequencies and adding or negating absolute temperatures
remain invalid. `abs()` has overloads for `PowerDelta`, `FrequencyDelta`,
`TemperatureDelta`, and `Ratio`, returning its argument type. It has no absolute
quantity or Boolean overload.

Ordered comparisons require two operands of the same non-Boolean semantic type
after the frequency-literal contextual rule. Equality and inequality have the same
rule; Phase 1 does not introduce Boolean literals or Boolean combinators. A
comparison returns `Boolean`, and an `assert` expression must be Boolean.

## Sweep contract

- Supported dimensions are exactly `frequency` and `temperature`, at most once
  each. These two dimension spellings are syntactic; duplicate dimensions and
  their type-specific constraints are semantic errors.
- Dimensions and coordinates retain source declaration order. The last declared
  dimension varies fastest. For frequency followed by temperature, point order is
  `(f0,t0), (f0,t1), ..., (f1,t0), ...`.
- An axis is generated by integer index as `start + index * step`, never by repeated
  floating-point addition. An endpoint on the lattice is included; an off-lattice
  range stops at the last value below the endpoint.
- Endpoint inclusion uses the current tolerance: 16 times double epsilon times the
  maximum absolute magnitude of start, end, step, and 1 in canonical units. A
  candidate within that tolerance is snapped to the declared endpoint.
- Bounds and steps must be finite, the step must be strictly positive, the end must
  not be below the start, temperature endpoints must be at least 0 K, and the step
  must have the dimension's delta type (`Frequency` is retained as the frequency
  step spelling for source compatibility).
- Axis counts and their Cartesian product use checked `size_t` arithmetic. Each
  axis and the whole experiment are limited to 1,000,000 points. Overflow and
  limit violations are rejected before allocation or equipment access.
- M1 accepts and lowers multiple dimensions. M2 executes their checked Cartesian
  product through typed coordinates and explicit device capabilities.

## Compiler representations

The following are shape contracts, not mandates for member spelling.

The AST stays syntactic. A sweep stores its written dimension and three literal
nodes; a measurement stores the optional written name and operation token; calls,
comparisons, and assertions retain their complete source spans. The AST does not
contain domain values, resolved symbols, device capabilities, or result objects.

Semantic analysis maintains declaration-ordered vectors of resolved sweeps,
measurements, derived quantities, assertions, and calibrations. Every symbol has a
stable dense `SymbolId`, name, semantic type, and declaration span. It resolves the
fixed sweep dimensions and measurement operations, performs unit conversion and
overload selection, checks use-before-declaration, and computes checked axis and
product counts. It does not select devices.

IR maintains declaration-ordered `Sweep` and `Measurement` records plus dense,
typed SSA-like values. Definitions use typed alternatives or enums such as
`MeasurePower`, `MeasureEvm`, and `MeasureFrequencyError`; operation names are not
carried as strings for runtime dispatch. Assertions reference a typed Boolean value,
their comparison operands/operator, ordinal, and source span. Calibrations reference
ordered sweep value IDs and a strongly typed correction value. IR remains independent
of AST layout, devices, simulator state, and presentation schemas.

Every layer validator checks dense IDs, definition order, type/definition agreement,
unique names, binding membership, and declared ordering before indexing a vector.

## Operating points and execution

The public cross-layer coordinate model is logically:

```cpp
using CoordinateValue = std::variant<Frequency, Temperature>;
struct Coordinate {
    std::string name;
    CoordinateType type; // stable enum: Frequency or Temperature
    CoordinateValue value;
};
struct OperatingPoint {
    std::vector<Coordinate> coordinates; // sweep declaration order
};
```

No coordinate is erased to `double`, and code must not use a variant index as a
serialized type identifier. Point results own their coordinate and value data.

### Devices and capabilities

`RfDevice` remains TX control and `MeasurementDevice::measurePower()` remains the
legacy observation contract. Phase 1 adds separate explicit interfaces:

- `TemperatureController::setTemperature(Temperature)`;
- `EvmMeasurementDevice::measureEvm() -> Ratio`; and
- `FrequencyErrorMeasurementDevice::measureFrequencyError() -> FrequencyDelta`.

The generalized execution overload accepts a typed device bundle containing the
required `RfDevice` reference and nullable pointers for power, EVM,
frequency-error, and temperature capabilities. Preflight makes a pointer mandatory
only when the program uses that capability. The existing overloads taking
`MeasurementDevice&` remain and delegate with its address in the power slot.
Simulator implementations may implement several interfaces, but the runtime
receives only public interfaces. No string-keyed capability registry, simulator
downcast, or simulator transfer function crosses the boundary.

After validating the complete IR and sweep product, runtime preflights all required
controllers and measurement interfaces. A recognized language operation with no
supplied capability produces `runtime.capability_unavailable` at the operation's
span before any equipment call. An unknown operation never reaches IR and produces
`semantic.unknown_measurement_operation`.

For each operating point runtime performs:

1. sweep controls in sweep declaration order (`setFrequency` or
   `setTemperature`);
2. `setOutputPower(reference.power)`;
3. measurements once each, in measurement declaration order; and
4. derived values, assertions, calibration correction, and result capture in IR
   dependency/source order without further equipment access.

This preserves frequency, output power, power measurement for the legacy program.
There is no retry. A device exception or evaluation error stops execution and
returns no partial public result, as today.

All device observations and computed numeric values are checked for finiteness at
the runtime boundary. A non-finite value is `runtime.non_finite_value`, aborts the
run, and is not converted to an assertion failure or emitted as JSON.

## Assertions and qualification

Assertions are observations, not control flow. Each is assigned a stable ID
`assertion-<n>` from one-based source order and retains its full expression span.
Every assertion is evaluated once for every successfully evaluated point, in source
order. False assertions do not stop later assertions or points.

An outcome owns its ID/span, operating-point coordinates, comparison operator,
typed left and right observed values, and `passed` flag. For each assertion the
summary contains total, passed, and failed counts. A point passes exactly when all
its assertions pass; an experiment passes exactly when every point passes. With no
assertions, point and experiment qualification status is `not_evaluated`, not a
vacuous pass. The only status strings are `pass`, `fail`, and `not_evaluated`.

Non-finite operands are execution failures under the runtime rule above. They are
never ordered, never counted, and never represented as JSON `null` assertion
values. Compiler-accepted literal thresholds are finite by construction.

## Calibration surfaces and metrics

A calibration `over` list must name every active sweep exactly once and in sweep
declaration order. Missing, duplicate, non-sweep, or reordered dimensions are
semantic errors. This makes every correction entry correspond one-to-one with an
operating point.

The artifact records the ordered dimension descriptors and then entries in runtime
point order. Each entry owns a coordinate tuple in `over` order and one strongly
typed correction. Correction type remains `PowerDelta` in Phase 1.

Metrics computed from the same observations used to derive a calibration are
`calibration_fit` metrics. Existing RMS and maximum pre/post-correction errors are
in-sample fit statistics and keep their formulas. Assertion counts and pass rates
are `qualification` metrics. Neither category is called validation. Independent
validation metrics are deferred until the language supplies an independent data
set. A perfect fit has a null JSON improvement ratio with reason `perfect_fit`,
never an IEEE infinity.

## JSON result schema version 1

M5 adds a deterministic UTF-8 JSON document. Object member order shown below is the
writer order; readers must ignore unknown members, but must reject an unknown schema
name or unsupported major version. Arrays preserve the orders defined above.
Numbers use classic-locale, round-trip precision and JSON finite-number syntax.

Every numeric result uses this object shape:

```json
{
  "type": "Frequency",
  "value": 2400000000,
  "canonical_unit": "Hz",
  "display_value": 2.4,
  "display_unit": "GHz"
}
```

Stable type names are `Frequency`, `FrequencyDelta`, `Power`, `PowerDelta`, `Ratio`,
`Temperature`, `TemperatureDelta`, and `Boolean`. Canonical units are respectively
`Hz`, `Hz`, `dBm`, `dB`, `ratio`, `K`, `K`, and `none`. Display units reflect the
declaration or operation default and are metadata, not a reinterpretation of
`value`. Boolean values use a JSON Boolean `value` and omit numeric/display units.

The complete top-level shape is shown below. Array contents are abbreviated to one
representative entry and are not intended to be the output of the full example
program above.

```json
{
  "schema": {"name": "horusrf.characterization-result", "version": 1},
  "characterization": "qualified_tx_path",
  "qualification": {
    "status": "pass",
    "points": {"total": 126, "passed": 126, "failed": 0},
    "assertions": [
      {"id": "assertion-1", "total": 126, "passed": 126, "failed": 0}
    ]
  },
  "points": [
    {
      "coordinates": [
        {"name": "frequency", "type": "Frequency", "value": 2400000000,
         "canonical_unit": "Hz", "display_value": 2.4, "display_unit": "GHz"}
      ],
      "measurements": [
        {"name": "output_power", "operation": "power", "type": "Power",
         "value": -9.65, "canonical_unit": "dBm",
         "display_value": -9.65, "display_unit": "dBm"}
      ],
      "derived": [],
      "assertions": [
        {"id": "assertion-1", "source": {
           "begin": {"line": 11, "column": 3, "offset": 310},
           "end": {"line": 11, "column": 40, "offset": 347}},
         "operator": "<=", "left": {"type": "PowerDelta", "value": 0.35,
           "canonical_unit": "dB", "display_value": 0.35, "display_unit": "dB"},
         "right": {"type": "PowerDelta", "value": 1.5,
           "canonical_unit": "dB", "display_value": 1.5, "display_unit": "dB"},
         "passed": true}
      ],
      "qualification": "pass"
    }
  ],
  "calibrations": [
    {"name": "tx_power",
     "dimensions": [{"name": "frequency", "type": "Frequency"}],
     "correction_type": "PowerDelta", "canonical_unit": "dB",
     "entries": [
       {"coordinates": [{"name": "frequency", "type": "Frequency",
          "value": 2400000000, "canonical_unit": "Hz",
          "display_value": 2.4, "display_unit": "GHz"}],
        "correction": {"type": "PowerDelta", "value": -0.35,
          "canonical_unit": "dB", "display_value": -0.35,
          "display_unit": "dB"}}
     ]}
  ],
  "metrics": {
    "calibration_fit": {
      "uncorrected_rms_error_db": 0.35,
      "corrected_rms_error_db": 0,
      "maximum_absolute_error_db": 0.4,
      "corrected_maximum_absolute_error_db": 0,
      "rms_improvement_ratio": null,
      "rms_improvement_reason": "perfect_fit"
    },
    "qualification": {"status": "pass", "pass_ratio": 1},
    "validation": null
  }
}
```

All declared measurements and derived values appear, in declaration order, even if
not referenced by an assertion or calibration. For `not_evaluated`, assertion
arrays are empty and qualification ratios are null.

## CLI and output behavior

M5 extends usage to:

```text
usage: horusrf-run <source.hrf> [--check | --csv <output.csv> | --json <output.json>]
```

The three modes are mutually exclusive. Source and output paths must differ.
Successful `--json` executes the experiment, writes schema v1, adds `json: <path>`
to the summary, and exits 0. `--check` remains silent and performs no capability
preflight or equipment access. A failed parse, analysis, lowering, runtime, or
serialization leaves no requested output file and follows the existing diagnostic
exit 4 contract; I/O failures remain exit 3.

The legacy CSV is available only when the compiled/result shape is precisely one
frequency sweep, legacy `measure power`, the standard report fields, one matching
calibration, and no assertions or additional values requiring representation. An
incompatible `--csv` request reports `output.csv_unrepresentable` at the
characterization span, exits 4 before equipment access, and creates no file. The
legacy header and values are never extended or flattened.

A completed experiment with one or more false assertions is not an execution
error: requested JSON is written and the complete summary is printed. The CLI then
prints source-positioned `qualification.assertion_failed` lines on standard error
and exits 6. Legacy programs have no qualification lines added to their console
summary, preserving its exact output. Extended programs add qualification totals.

## Compatibility verification matrix

Each later milestone must retain these fixtures in addition to its focused tests:

| Contract | Existing proof to preserve | Required additive proof |
|---|---|---|
| Grammar and diagnostics | lexer/parser/semantic invalid fixtures | new tokens, valid full program, every invalid class above |
| Legacy compiler shape | parser, semantic, and lowering tests | legacy-to-generalized representation equivalence |
| Public API and call order | executor/characterization tests | legacy overload delegation and preflight-with-zero-calls |
| Simulator opacity | simulator tests | new observations tested only through public interfaces |
| Result formulas | domain and end-to-end tests | typed coordinates, outcomes, surface alignment |
| Console/CLI exits | CLI acceptance | JSON, exit 6, incompatible CSV, no failed-output residue |
| CSV bytes | CSV and end-to-end header/rows | explicit legacy-shape gate |
| Procedural independence | equivalence suite | legacy result equivalence after generalized runtime |
| Studio behavior | headless Studio suite | JSON rejection, models, filters, source navigation, last-good result |

Tests remain deterministic, offline, independent of the wall clock, randomness,
and hardware. Syntax work updates `grammar/HorusRf.g4`, `docs/language.md`, syntax
highlighting, and tests in the same milestone. Cross-layer milestones finish with
the full unfiltered CTest suite.

The single language-change workflow is: update `grammar/HorusRf.g4`; update any
semantic definition or frontend metadata adapter affected by the syntax; update
the public language and architecture documentation; and add the lowest-layer
parser, frontend, semantic, and cross-layer tests needed to prove the behavior in
the same change. Studio highlighting continues to consume compiler-owned token
metadata rather than a parallel syntax list.
