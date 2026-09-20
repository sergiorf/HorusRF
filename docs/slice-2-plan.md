# Slice 2 Implementation Plan — Domain Semantics

## Status and objective

**Status:** implemented and accepted.

Slice 2 turns the syntax AST into a validated, typed semantic model. It adds
canonical RF quantities, resolves the names introduced by the M0 language,
checks the explicit dBm/dB arithmetic rules, validates frequency sweeps and
calibration indexes, and reports structured source-located diagnostics.

The completed pipeline for this slice is:

```text
source -> lexer -> parser -> syntax AST -> semantic analyzer -> typed model
```

The typed model is the input to Slice 3. Slice 2 does not lower to executable
IR and does not execute or simulate an experiment.

## Inputs inherited from Slice 1

Slice 2 consumes the existing public syntax types in
`include/horusrf/ast/ast.hpp`:

- one `Program` containing one `Characterization`;
- the five statement alternatives;
- quantity spellings and units exactly as written;
- identifier, reference, unary, and binary expression alternatives;
- half-open source spans on syntax nodes.

The grammar and parser remain syntax-only. Semantic rejection must not be
added to the lexer or parser.

The current AST stores declaration and calibration-dimension names as strings
rather than separately spanned name nodes. Slice 2 will not reshape the
completed Slice 1 API solely for diagnostics. A diagnostic uses the smallest
available span: expression and quantity spans where available, otherwise the
enclosing statement span.

## Scope

Implement:

- dependency-free strong domain types for frequency, absolute logarithmic
  power, and logarithmic power delta;
- conversion of `Hz`, `kHz`, `MHz`, and `GHz` into canonical hertz;
- preservation of `dBm` as canonical absolute power and `dB` as canonical
  relative power;
- only the explicitly supported `Power`/`PowerDelta` C++ operations;
- a typed, owning semantic expression tree with resolved symbol identities;
- an analyzed characterization model suitable for Slice 3 lowering;
- declaration, name, reference, expression, sweep, and calibration checks;
- structured semantic diagnostics with source spans;
- domain and semantic-analysis tests, including analysis of the real canonical
  fixture.

## Non-goals

Do not implement:

- AST-to-IR lowering or any IR types;
- runtime execution, sweep point generation, or device calls;
- `RfDevice`, simulator behavior, calibration application, or result metrics;
- CSV or a command-line runner;
- new language syntax or grammar changes;
- general dimensional-analysis algebra, multiplication, division, compound
  units, user-defined units, or implicit unit conversion outside the six M0
  units;
- calibration table values or interpolation;
- error recovery that manufactures a partially valid semantic program;
- third-party unit, parser, or test libraries.

## Repository and build deliverables

Add this minimum structure:

```text
include/horusrf/domain/quantity.hpp
include/horusrf/domain/units.hpp
include/horusrf/semantic/analyzer.hpp
include/horusrf/semantic/diagnostics.hpp
include/horusrf/semantic/model.hpp
src/domain/quantity.cpp
src/domain/units.cpp
src/semantic/analyzer.cpp
tests/domain/quantity_tests.cpp
tests/semantic/analyzer_tests.cpp
```

Files may be combined when that makes an interface clearer, but domain code
must not depend on AST, parser, semantic, IR, runtime, or device headers.

Required CMake targets:

| Target | Kind | Responsibility |
|---|---|---|
| `horusrf_domain` | static library | canonical units and strong RF quantities |
| `horusrf_semantic` | static library | AST analysis, typed model, diagnostics |
| `horusrf_domain_tests` | executable and CTest test | unit conversion and legal arithmetic |
| `horusrf_semantic_tests` | executable and CTest test | resolution, typing, validation, diagnostics |

`horusrf_semantic` links to `horusrf_domain` and the existing parser/front-end
target only as needed to consume public AST types. `horusrf_domain` remains
independent. Reuse `tests/test_support.hpp`; do not add a test framework.

Continue to use `build/` as the only build directory. The acceptance commands
remain:

```text
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Domain model

### Dimensions and units

Use explicit closed enums:

```cpp
enum class Dimension {
    Frequency,
    AbsolutePower,
    RelativePower,
};

enum class Unit {
    Hz,
    KHz,
    MHz,
    GHz,
    DBm,
    DB,
};
```

Provide total mappings for unit spelling, dimension, and scale. Unknown unit
text must be reported as an error by the conversion boundary even though a
well-formed Slice 1 AST cannot normally contain it.

Canonical representation:

| Source unit | Dimension | Canonical storage | Scale |
|---|---|---|---:|
| `Hz` | Frequency | hertz | `1` |
| `kHz` | Frequency | hertz | `1'000` |
| `MHz` | Frequency | hertz | `1'000'000` |
| `GHz` | Frequency | hertz | `1'000'000'000` |
| `dBm` | AbsolutePower | dBm | `1` |
| `dB` | RelativePower | dB | `1` |

### Strong quantities

The public types are intentionally small value types:

```cpp
class Frequency {
public:
    static Frequency from_hertz(double value);
    [[nodiscard]] double hertz() const noexcept;
    friend bool operator==(Frequency, Frequency) = default;
    friend auto operator<=>(Frequency, Frequency) = default;
};

class Power {
public:
    static Power from_dbm(double value);
    [[nodiscard]] double dbm() const noexcept;
    friend bool operator==(Power, Power) = default;
};

class PowerDelta {
public:
    static PowerDelta from_db(double value);
    [[nodiscard]] double db() const noexcept;
    friend bool operator==(PowerDelta, PowerDelta) = default;
};
```

Constructors/factories accept canonical values; parsing source text and
choosing a type are separate operations. Canonical factories throw
`std::invalid_argument` if a caller supplies a non-finite value, which protects
the value-type invariant. The source conversion boundary validates before it
calls those factories, so malformed source remains a diagnostic rather than an
exception.

Use a domain-owned conversion result that does not depend on AST or semantic
diagnostic types:

```cpp
using CanonicalQuantity = std::variant<Frequency, Power, PowerDelta>;

enum class QuantityConversionError {
    InvalidNumber,
    NumberOutOfRange,
    UnknownUnit,
};

struct QuantityConversionResult {
    std::optional<CanonicalQuantity> value;
    std::optional<QuantityConversionError> error;
};

[[nodiscard]] QuantityConversionResult convert_quantity(
    std::string_view number,
    std::string_view unit);
```

The decimal conversion must consume the complete number spelling and reject
range errors or trailing characters defensively. Exactly one of `value` and
`error` is present. The semantic layer maps conversion errors to its own
source-located diagnostics.

Do not use a generic public `Quantity<double, Tag>` API in M0. Named RF types
make invalid operations harder to express and keep later device interfaces
readable.

### Legal C++ arithmetic

Expose exactly the operations required by the M0 roadmap:

```cpp
PowerDelta operator-(Power lhs, Power rhs);
Power operator+(Power lhs, PowerDelta rhs);
PowerDelta operator+(PowerDelta lhs, PowerDelta rhs);
PowerDelta operator-(PowerDelta value);
```

Do not add permissive operators for `Power + Power`, `PowerDelta - Power`,
`Power - PowerDelta`, unary `-Power`, or frequency arithmetic. Semantic
checking uses the same table explicitly; it must not infer validity by trying
C++ overload resolution.

## Semantic types and symbol model

Use the closed M0 value type set:

```cpp
enum class SemanticType {
    Frequency,
    Power,
    PowerDelta,
};

struct SymbolId {
    std::size_t value;
    friend bool operator==(SymbolId, SymbolId) = default;
};
```

Each successfully declared semantic object receives a stable `SymbolId`.
Resolved expression nodes and calibration dimensions retain IDs rather than
unresolved strings. Names are also retained for diagnostics and inspection.

M0 uses one characterization-wide namespace for bare names. It contains:

- `frequency`, introduced by `sweep frequency` with type `Frequency`;
- `power`, introduced by `measure power` with type `Power`;
- each successfully analyzed `derive` name;
- calibration artifact names for duplicate detection, although artifacts are
  not value expressions in M0.

`reference.power` is resolved through a separate reference namespace and has
type `Power`. Bare `reference` is not a value. Any other reference object or
member is invalid.

Declarations become visible only after their statement validates. This gives
deterministic source-order behavior, rejects self-reference, and prevents a
failed declaration from becoming available to later expressions. Forward
references are not supported in M0.

## Typed semantic model

The analyzer returns an owning model; it must not contain pointers into an AST
whose lifetime is controlled by the caller.

### Expressions

Define a semantic expression variant with these logical alternatives:

```text
QuantityValue    { Frequency | Power | PowerDelta, span }
SymbolValue      { SymbolId, name, type, span }
ReferenceValue   { SymbolId, object, member, type, span }
UnaryValue       { operator, operand, result type, span }
BinaryValue      { operator, left, right, result type, span }
```

Every expression node retains its inferred `SemanticType` and syntax span.
Recursive ownership may use `std::unique_ptr` or `std::shared_ptr`, but the
public model must be safely movable and have unambiguous ownership.

### Characterization objects

The successful model contains these logical objects:

```text
AnalyzedProgram
  characterization name and span
  ReferencePower { SymbolId, Power, span }
  FrequencySweep { SymbolId, start, end, step, span }
  PowerMeasurement { SymbolId, span }
  DerivedQuantity[] { SymbolId, name, typed expression, type, span }
  Calibration[] { SymbolId, name, correction, index IDs, type, span }
```

A calibration type records ordered index types and correction type. For the
canonical program it is equivalent to:

```text
tx_power : Calibration<[Frequency], PowerDelta>
```

The model retains declaration order within the derived-quantity and
calibration collections. It does not contain device operations or executable
sweep points.

## Semantic rules

### Required experiment declarations

A valid M0 characterization contains exactly one of each built-in declaration:

- `reference power`;
- `sweep frequency`;
- `measure power`.

The analyzer reports duplicates at the duplicate statement. At the end of the
characterization it reports each missing required declaration at the
characterization span. Multiple derived quantities and calibrations are
allowed when their names are unique.

All bare declared names share one duplicate-checking namespace. A derived or
calibration name must not collide with `frequency`, `power`, another derived
name, or another calibration name. The current lexer/parser prevents reserved
`frequency` and `power` spellings from being declaration names, but the
analyzer still defends this invariant for programmatically constructed ASTs.

### Quantity conversion

- A reference quantity must normalize to `Power` (`dBm`).
- All three sweep quantities must normalize to `Frequency`.
- Quantity expressions infer their type from their unit.
- The analyzer never rewrites the source AST spelling; canonical values live
  only in the semantic model.
- A sweep step must be strictly positive.
- A sweep end must be greater than or equal to its start.
- Frequency values otherwise remain policy-free in Slice 2; point generation
  and divisibility of the range by the step belong to later slices.

### Expression typing

The complete M0 operator table is:

| Syntax | Operand types | Result | Status |
|---|---|---|---|
| unary `-x` | `PowerDelta` | `PowerDelta` | allowed |
| `a + b` | `Power`, `PowerDelta` | `Power` | allowed |
| `a + b` | `PowerDelta`, `PowerDelta` | `PowerDelta` | allowed |
| `a - b` | `Power`, `Power` | `PowerDelta` | allowed |
| any other unary/binary combination | any | none | rejected |

The table is intentionally directional: `PowerDelta + Power` is not added by
commutativity in M0. Frequency arithmetic is rejected. Parentheses need no
semantic node beyond the span already retained by the syntax expression.

On an invalid operator, issue one diagnostic for that operator expression and
mark the internal result erroneous so parent expressions do not emit cascaded
dimension errors.

### References and derived names

- `reference.power` is valid only after `reference power` has been declared.
- `power` is valid only after `measure power` has been declared.
- `frequency` is valid only after `sweep frequency` has been declared.
- A derived name is visible to later expressions after its initializer passes.
- Calibration artifact names are not valid expression operands in M0.
- Unknown identifiers and unknown reference members are distinct diagnostics.

### Calibration validation

For each calibration declaration:

- analyze its correction expression using the symbols visible at that point;
- require the correction result to be `PowerDelta`;
- resolve each `over` name to an active sweep variable;
- reject unknown names, non-sweep names, and repeated dimensions;
- retain index `SymbolId`s in source order, separately from the correction;
- add the calibration name to the namespace only after the declaration passes.

M0 has one active frequency sweep, so the only valid index is the symbol
introduced by `sweep frequency`. The representation remains a vector so Slice
3 does not need an interface change when multidimensional syntax is expanded.

## Analyzer and result contract

Use a non-throwing semantic entry point unless an internal invariant is broken:

```cpp
struct AnalysisResult {
    std::optional<AnalyzedProgram> program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] AnalysisResult analyze(const ast::Program& program);
```

Contract:

- `ok()` is true exactly when `program` is present and diagnostics are empty;
- any semantic error makes `program` empty;
- diagnostics are deterministic and ordered by source occurrence;
- independent statement errors may be accumulated;
- follow-on diagnostics caused only by an already-invalid operand or missing
  declaration are suppressed where practical;
- the input AST is never mutated.

The analyzer may build a provisional model internally, but a partial model is
never exposed as success.

## Diagnostic contract

Keep semantic diagnostics separate from parser diagnostics. A minimum public
diagnostic contains:

```cpp
enum class DiagnosticCode {
    InvalidNumericLiteral,
    UnexpectedQuantityType,
    DuplicateDeclaration,
    MissingReference,
    MissingSweep,
    MissingMeasurement,
    UnknownIdentifier,
    UnknownReferenceMember,
    ReferenceNotAvailable,
    InvalidUnaryOperand,
    InvalidBinaryOperands,
    InvalidSweepRange,
    InvalidSweepStep,
    UnknownCalibrationDimension,
    NonSweepCalibrationDimension,
    DuplicateCalibrationDimension,
    InvalidCalibrationCorrection,
};

struct Diagnostic {
    DiagnosticCode code;
    std::string message;
    parser::SourceSpan span;
};
```

Equivalent naming is acceptable, but codes must preserve these distinctions.
Messages include the relevant name or operand types and use stable type/unit
spellings. For example:

```text
semantic.invalid_binary_operands:
cannot add Frequency and Power
```

Tests assert code and span first and only stable message fragments. Do not
make tests depend on entire prose messages.

## Required tests

### Domain tests

Cover:

- all four frequency-unit scales, including decimal spellings;
- `dBm` to `Power` and `dB` to `PowerDelta` without unintended scaling;
- negative reference power such as `-10 dBm`;
- rejection of unknown units, malformed numeric text, trailing characters,
  and non-finite/out-of-range values at the defensive conversion API;
- `Power - Power -> PowerDelta`;
- `Power + PowerDelta -> Power`;
- `PowerDelta + PowerDelta -> PowerDelta`;
- unary `-PowerDelta`;
- compile-time checks, through small concepts or traits, that rejected C++
  operations are unavailable;
- comparison/accessor behavior without relying on exact equality for values
  produced by scaled decimal conversion.

Use explicit tolerances for floating-point assertions and add a reusable
near-equality helper to `tests/test_support.hpp` if needed.

### Successful semantic analysis

Parse and analyze the actual
`examples/tx_path_characterization.hrf` fixture. Structurally assert:

- reference power is canonical `-10 dBm`;
- sweep endpoints are `2.40e9 Hz` and `2.50e9 Hz` and step is `1e6 Hz`;
- `frequency`, `power`, and `reference.power` resolve to their intended,
  distinct symbols;
- `power - reference.power` has type `PowerDelta`;
- calibration `tx_power` has a `PowerDelta` correction;
- its correction is retained separately from its index list;
- its sole index resolves to the active frequency sweep `SymbolId`;
- its public type is `Calibration<[Frequency], PowerDelta>`;
- semantic node spans correspond to the source constructs.

Also add focused valid programs for:

- every supported expression operation;
- unary negation of a previously derived `PowerDelta`;
- a later derived expression referring to an earlier derived value;
- equivalent frequency ranges expressed in each supported frequency unit.

### Rejected semantic analysis

At minimum, assert diagnostic codes and source locations for:

- wrong unit on reference power;
- a non-frequency sweep endpoint or step;
- zero or negative sweep step and reversed range;
- `Power + Power`, `PowerDelta - Power`, `Power - PowerDelta`, unary
  `-Power`, and frequency arithmetic;
- use of `power`, `frequency`, a derived name, or `reference.power` before it
  is available;
- unknown bare identifier and unknown reference member;
- duplicate reference, sweep, measurement, derived, and calibration names;
- collisions between user names and built-in `power`/`frequency` names in a
  defensively constructed AST (the parser prevents these source forms);
- missing reference, sweep, and measurement declarations;
- calibration correction with `Power` or `Frequency` type;
- unknown, non-sweep, and duplicate calibration indexes;
- a failed analysis returning no partial `AnalyzedProgram`.

Parser failures remain parser tests. Semantic tests should construct source and
call `parse` followed by `analyze`; manually constructed ASTs are reserved for
defensive states that valid parser output cannot express.

## Implementation sequence

1. Add the domain and semantic targets and empty test executables while keeping
   all existing parser tests green.
2. Implement unit spelling/dimension mappings, finite decimal conversion, and
   strong quantity types.
3. Implement only the approved C++ quantity operations and domain tests.
4. Define semantic symbols, the owning typed expression tree, analyzed program
   objects, diagnostics, and `AnalysisResult`.
5. Implement quantity normalization and sequential declaration/symbol-table
   handling for reference, sweep, measurement, and derived values.
6. Implement expression resolution and the explicit unary/binary type table,
   including cascade suppression.
7. Implement calibration correction typing and ordered sweep-index resolution.
8. Add required-declaration, duplicate, sweep-domain, and failure-result checks.
9. Add the canonical fixture test and the complete positive/negative matrix.
10. Run configure, build, and all CTest tests from the repository root using
    only `build/`.

Each step must leave existing Slice 1 behavior passing. Avoid bundling IR or
runtime scaffolding into this slice.

## Acceptance checklist

- [x] Existing lexer/parser behavior and tests remain green.
- [x] Clean CMake configure and C++20 build succeeds using `build/`.
- [x] CTest discovers and passes parser, domain, and semantic tests.
- [x] Domain types have no AST/parser/semantic/runtime/device dependencies.
- [x] All six units normalize to the specified canonical representations.
- [x] Only the explicit M0 `Power`/`PowerDelta` operations are exposed.
- [x] The canonical fixture analyzes successfully without special-case code.
- [x] Every semantic expression has an inferred type and resolved symbol IDs.
- [x] Canonical error and correction expressions infer `PowerDelta`.
- [x] Calibration correction and ordered indexes remain distinct.
- [x] `over frequency` resolves to the active sweep symbol.
- [x] Invalid dimensions, references, declarations, sweeps, and indexes produce
      structured source-located diagnostics.
- [x] Failed analysis exposes no partial successful model.
- [x] No IR, runtime, device, simulator, CLI, or third-party dependency is added.

## Slice boundary and handoff

Slice 2 hands Slice 3 an owned, validated semantic program containing canonical
domain values, resolved symbols, typed expressions, and typed calibration
declarations. Slice 3 may rely on the invariant that an `AnalyzedProgram` has
exactly one valid reference, frequency sweep, and power measurement and that
all derived and calibration expressions are well typed.

No executable sequencing is implied by the semantic model beyond declaration
visibility. Turning the validated model into the minimal device-independent
execution plan remains Slice 3 work.
