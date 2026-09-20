# Slice 3 Implementation Plan — Typed IR Lowering

## Status and objective

**Status:** planned; not yet implemented.

Slice 3 converts the validated, owning semantic model from Slice 2 into a
minimal, typed, device-independent intermediate representation (IR). The IR
must state what values an experiment produces, how derived values are
calculated, which value is swept, and which values form a calibration artifact.
It must not know how a concrete RF device or simulator performs those actions.

The completed pipeline for this slice will be:

```text
source -> lexer -> parser -> syntax AST -> semantic analyzer -> typed IR lowering
```

The active lowering boundary is `semantic::AnalyzedProgram -> ir::Program`.
“AST-to-IR” describes the complete compiler path, not a direct dependency from
the IR layer to the syntax AST. Parsing and semantic analysis remain mandatory
before lowering.

## Inputs inherited from Slice 2

Slice 3 consumes the public semantic model in
`include/horusrf/semantic/model.hpp`. A successful `AnalyzedProgram` provides:

- canonical domain values for reference power and sweep bounds;
- stable `semantic::SymbolId` identities for every declaration;
- a typed, owning expression tree with resolved symbol references;
- exactly one reference power, frequency sweep, and power measurement;
- source-ordered derived quantities and calibrations;
- calibration corrections typed as `PowerDelta`;
- calibration indexes resolved to active sweep symbols; and
- source spans on programs, declarations, and expression nodes.

Normal callers lower only a successful `AnalysisResult::program`. The lowerer
may rely on the semantic type rules, but it must still detect inconsistent
manually constructed semantic models and return a lowering diagnostic instead
of dereferencing missing values or producing malformed IR.

The semantic model remains unchanged by lowering. The resulting IR owns its
names, values, operations, and source metadata; it contains no pointers or
references into the semantic model.

## Scope

Implement:

- a closed set of IR value types and stable IR value identities;
- a device-independent representation of reference power, the frequency
  sweep, the power measurement, typed calculations, and calibration capture;
- deterministic semantic-to-IR lowering;
- a symbol-to-value mapping used only while lowering;
- flattening of recursive semantic expressions into ordered typed IR values;
- preservation of calibration names, ordered indexes, and correction values;
- source spans sufficient to relate IR values back to source constructs;
- structured lowering diagnostics for violated semantic-model invariants; and
- unit tests, including full parsing, analysis, and lowering of the canonical
  `.hrf` fixture.

## Non-goals

Do not implement:

- direct lowering from syntax AST or any parser behavior changes;
- sweep point generation, inclusive-range iteration, or range divisibility
  policy;
- `RfDevice`, a simulator, device selection, transport, or hardware handles;
- calls that set frequency, set output power, or measure through a device;
- runtime storage slots, bytecode, scheduling, interpretation, or execution;
- calibration table population, interpolation, lookup, or application;
- result rows, metrics, CSV output, or a command-line runner;
- optimizations such as constant folding, common-subexpression elimination,
  dead-value removal, or expression reassociation;
- control-flow graphs, branches, loops, SSA phi nodes, or LLVM integration;
- serialization or a stable on-disk IR format; or
- new syntax, semantic types, units, arithmetic rules, or third-party
  dependencies.

## Architecture boundary

The layers after this slice are:

```text
syntax AST
    |
    v
semantic::AnalyzedProgram
    |
    | ir::lower(...)
    v
ir::Program                   no AST/parser/device dependency
    |
    v
future runtime                interprets abstract IR operations
    |
    v
future RfDevice              performs physical or simulated I/O
```

The IR describes an abstract `MeasurePower` value source. This is not a device
call and contains no device object. Slice 4 will decide how that operation is
executed through `RfDevice`.

IR model headers may depend on the domain quantity headers and the shared
source-span definition. They must not include AST, parser implementation,
semantic-model, runtime, device, or simulator headers. The lowering API may
forward-declare or include `semantic::AnalyzedProgram`; the implementation is
the only component that traverses semantic model internals.

## Repository and build deliverables

Add this minimum structure:

```text
include/horusrf/ir/ir.hpp
include/horusrf/ir/lowering.hpp
include/horusrf/ir/diagnostics.hpp
src/ir/lowering.cpp
tests/ir/lowering_tests.cpp
```

Files may be split further when that improves clarity, but do not introduce a
generic compiler framework or an IR class hierarchy for the closed M0 model.

Required CMake targets:

| Target | Kind | Responsibility |
|---|---|---|
| `horusrf_ir` | static library | IR data model and semantic-to-IR lowering |
| `horusrf_ir_tests` | executable and CTest test | IR structure, lowering, and defensive failures |

`horusrf_ir` links to `horusrf_domain` and, for its lowering implementation,
`horusrf_semantic`. The IR test target also links to `horusrf_parser` and
`horusrf_semantic` so it can exercise the actual front end. Define
`HORUSRF_SOURCE_DIR` for the test target to load the real fixture.

Reuse `tests/test_support.hpp`; do not add a test framework. Continue to use
`build/` as the single build directory:

```text
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## IR design principles

The M0 IR is a small, immutable-by-convention data model rather than an
execution engine.

It has these invariants:

1. Every produced value has one stable `ValueId`, one `ValueType`, and one
   definition.
2. Definitions appear in dependency order. Every operand refers to an earlier
   value, except that program-level sweep and artifact metadata may refer to
   values defined in the body.
3. Operand and result types satisfy the same closed arithmetic table as the
   semantic model.
4. The sweep value is `Frequency`, the reference value is `Power`, and the
   measurement value is `Power`.
5. Every calibration correction is `PowerDelta`, and every calibration index
   identifies the value produced by a declared sweep.
6. Names are metadata, not identity. `ValueId` is used for all dataflow edges.
7. The representation owns all of its data and is deterministic for the same
   analyzed input.
8. The representation contains no concrete device, callback, or executable
   function object.

The lowerer does not optimize. Separate occurrences of the same literal or
expression produce separate IR values. This keeps source mapping and lowering
behavior obvious; a later optimization slice may change that policy.

## Types and identities

Use a closed IR type enum rather than exposing semantic types in the IR:

```cpp
enum class ValueType {
    Frequency,
    Power,
    PowerDelta,
};

struct ValueId {
    std::size_t value;
    friend bool operator==(ValueId, ValueId) = default;
};
```

Provide a total, non-throwing type-name helper for diagnostics and tests.
`ValueId` values are allocated monotonically beginning at zero during one
lowering operation. Callers must not infer domain meaning from the numeric
value, but deterministic allocation is required for repeatable tests and
future diagnostics.

Use the existing domain value types for constants:

```cpp
using ConstantValue =
    std::variant<domain::Frequency, domain::Power, domain::PowerDelta>;
```

Do not add untyped `double` values to the IR. Canonical unit conversion has
already happened and must not be repeated during lowering or execution.

## Value definitions

Represent every data-producing operation as a typed definition. The logical
closed alternatives are:

```text
Constant            canonical Frequency, Power, or PowerDelta
ReferencePower      configured reference Power
SweepFrequency      current point supplied by the enclosing frequency sweep
MeasurePower        abstract measured Power at the current point
Alias               another value of the identical type
NegatePowerDelta    PowerDelta -> PowerDelta
AddPowerDelta       PowerDelta, PowerDelta -> PowerDelta
ApplyPowerDelta     Power, PowerDelta -> Power
PowerDifference     Power, Power -> PowerDelta
```

`ReferencePower` is distinct from a general `Constant`: it marks the declared
experiment reference for future runtime setup and inspection. `SweepFrequency`
marks the current point yielded by the sweep. `MeasurePower` is an abstract
effectful source, so a future executor must not fold, duplicate, or reorder it.

`Alias` is required because a legal semantic declaration may directly name an
existing value, for example `derive observed = power`. Keeping the alias
preserves the declaration's own `ValueId`, name, type, and span without
pretending that it performs arithmetic.

A concrete representation may use `std::variant`:

```cpp
using Definition = std::variant<
    Constant,
    ReferencePower,
    SweepFrequency,
    MeasurePower,
    Alias,
    NegatePowerDelta,
    AddPowerDelta,
    ApplyPowerDelta,
    PowerDifference>;

struct Value {
    ValueId id;
    ValueType type;
    Definition definition;
    parser::SourceSpan span;
};
```

Equivalent names are acceptable, but the alternatives and their type
distinctions must remain inspectable without decoding strings. Operation
payloads contain operand `ValueId`s, not nested expressions or pointers.

## Program, sweep, and calibration structure

The IR program logically contains:

```text
Program
  characterization name and source span
  reference ValueId
  Sweep {
    current-frequency ValueId
    canonical start Frequency
    canonical end Frequency
    canonical step Frequency
    source span
  }
  measurement ValueId
  ordered Value[]
  NamedValue[]
  Calibration[]
```

`NamedValue` records each source-level derived declaration separately from its
definition:

```text
NamedValue { name, value ValueId, ValueType, source span }
```

This lets runtime consumers ignore source names while diagnostics, tests, and
future output selection retain them. Each derived declaration gets a fresh
value. If its expression is a symbol/reference leaf, emit an `Alias`; if its
root already creates an operation or constant value, that root value may carry
the declaration as its `NamedValue` without an additional alias. Whichever
policy is chosen must be uniform and tested. The preferred policy is always to
give a declaration a distinct root value via `Alias` only for leaf roots.

A calibration logically contains:

```text
Calibration {
  name
  correction ValueId
  ordered index ValueId[]
  ordered index ValueType[]
  correction ValueType
  source span
}
```

The correction and indexes are separate fields. Do not encode `over frequency`
as an arithmetic operand or infer it later from a name. Preserve index order
even though M0 currently has only one frequency dimension.

The program's ordered values form the per-sweep-point dataflow plan. The sweep
bounds are metadata on the enclosing sweep and are not expanded into point
values in this slice. Slice 4 can therefore iterate points and evaluate the
same ordered definitions once per point without the IR naming a device.

## Expression lowering

Lower expressions recursively, visiting operands left to right. Each visit
returns a typed `ValueId` and appends any newly created definitions in
dependency order.

The complete mapping is:

| Semantic expression | IR definition | Result type |
|---|---|---|
| quantity literal | `Constant` | literal type |
| symbol or reference leaf | existing mapped value | mapped type |
| unary `-PowerDelta` | `NegatePowerDelta` | `PowerDelta` |
| `Power + PowerDelta` | `ApplyPowerDelta` | `Power` |
| `PowerDelta + PowerDelta` | `AddPowerDelta` | `PowerDelta` |
| `Power - Power` | `PowerDifference` | `PowerDelta` |

No generic add/subtract opcode is permitted in M0. Distinct operations keep
physical meaning visible and prevent a future executor from accepting an
invalid operand combination accidentally.

Semantic `SymbolValue` and `ReferenceValue` nodes both resolve through the
temporary symbol map. Their stored type must agree with the mapped IR value.
Their name/object/member strings are retained only in the semantic model and
diagnostics; identity in the IR comes from `ValueId`.

Although analysis already rejects unsupported operations, the lowerer must
handle an inconsistent expression defensively by returning one diagnostic for
the smallest offending expression and no IR program.

## Symbol mapping and lowering order

Maintain a lowering-local map:

```text
semantic::SymbolId -> { ir::ValueId, ir::ValueType, declaration kind }
```

Populate and consume it in this deterministic order:

1. Emit `ReferencePower`; map the reference symbol.
2. Emit `SweepFrequency`; map the sweep symbol and record the canonical sweep
   bounds.
3. Emit `MeasurePower`; map the measurement symbol.
4. For each derived quantity in source order, lower its expression, record its
   named value, then map its semantic symbol to the root value.
5. For each calibration in source order, lower its correction, resolve its
   indexes through the map, and append the artifact descriptor.

Calibration symbols are artifact identities, not expression values, so they
are not inserted as value-producing definitions. If a future language version
allows a calibration artifact in an expression, it will require an explicit
IR operation rather than treating the artifact as a scalar.

When a semantic expression references a symbol absent from the map, report an
invariant diagnostic. Do not manufacture a placeholder value. Duplicate
semantic symbols, duplicate IR definitions, references to later values, type
mismatches, and a calibration index that does not map to the program sweep are
also lowering failures.

## Canonical fixture shape

Lowering `examples/tx_path_characterization.hrf` must produce the following
logical shape (numeric `ValueId`s are illustrative but should follow the
deterministic allocation policy):

```text
Program "tx_path"
  v0 : Power       = ReferencePower(-10 dBm)
  sweep frequency v1 : Frequency = 2.40 GHz .. 2.50 GHz step 1 MHz
  v2 : Power       = MeasurePower
  v3 : PowerDelta  = PowerDifference(v2, v0)     name "error"
  v4 : PowerDelta  = PowerDifference(v0, v2)
  calibration "tx_power" {
    correction = v4
    over = [v1]
    type = Calibration<[Frequency], PowerDelta>
  }
```

The IR stores canonical hertz/dBm/dB domain values even if this explanatory
display uses source-friendly units. There is one abstract measurement source;
the correction expression reuses it through its mapped `ValueId` and must not
emit a second `MeasurePower`.

## Source metadata

Retain the semantic source span on:

- the IR program;
- the sweep descriptor;
- every value definition;
- every named derived value; and
- every calibration descriptor.

Values that directly represent declarations use the declaration or expression
span appropriate to that semantic node. Arithmetic and literal definitions use
their expression spans. Leaf symbol references do not need separate persistent
IR nodes unless an `Alias` is emitted for a declaration, because the consuming
operation already retains the enclosing expression span.

Source spans are diagnostic/debug metadata. Runtime behavior must not depend
on source offsets, and lowering must not retain the original source text.

## Lowering result and diagnostics

Use a non-throwing public entry point for input-model failures:

```cpp
struct LoweringResult {
    std::optional<ir::Program> program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] LoweringResult lower(
    const semantic::AnalyzedProgram& program);
```

`ok()` is true exactly when a program is present and diagnostics are empty.
Any failure returns no partial IR program. Allocation failure and impossible
internal implementation bugs are not converted into user diagnostics.

The minimum diagnostic distinctions are:

```cpp
enum class DiagnosticCode {
    DuplicateSymbol,
    UnknownSymbol,
    TypeMismatch,
    InvalidDefinitionOrder,
    InvalidCalibrationIndex,
    UnsupportedExpression,
};
```

Equivalent naming or a more precise split is acceptable. Every diagnostic has
a stable code, explanatory message, and `SourceSpan`. These diagnostics report
broken lowering preconditions, not ordinary user semantic errors; valid parser
output that passes `semantic::analyze` must never produce them.

Diagnostics are deterministic and source ordered where multiple independent
issues can safely be identified. Tests assert codes and spans first and only
stable message fragments. Do not throw for a missing symbol, bad variant/type
pair, or malformed calibration index in a manually assembled semantic model.

## Required tests

### IR model tests

Cover:

- distinct and comparable `ValueId`s;
- total `ValueType` names;
- canonical domain values retained without conversion to raw `double`;
- operation payloads exposing typed operand IDs;
- ordered value definitions and backwards-only dependencies;
- an IR header usable without including AST or device headers; and
- owning behavior after the source AST and semantic program leave scope.

### Canonical pipeline test

Read the actual `examples/tx_path_characterization.hrf`, then call `parse`,
`analyze`, and `lower`. Assert:

- parsing and analysis succeed before lowering;
- the characterization name and source span are retained;
- the reference is `-10 dBm` and has type `Power`;
- sweep bounds normalize to `2.40e9`, `2.50e9`, and `1.0e6` hertz;
- the sweep current value has type `Frequency`;
- exactly one `MeasurePower` definition exists and has type `Power`;
- `error` is a named `PowerDifference(measurement, reference)` with type
  `PowerDelta`;
- the calibration correction is
  `PowerDifference(reference, measurement)` with type `PowerDelta`;
- calibration correction and indexes are stored separately;
- the sole index is the current-frequency value from the active sweep;
- the artifact type is `Calibration<[Frequency], PowerDelta>`; and
- all operands refer to existing, earlier definitions.

The test must not manually construct the canonical AST or semantic program.

### Focused successful lowering

Parse and analyze small valid programs that exercise:

- all three literal types as derived expression leaves;
- a bare measurement alias and a bare current-frequency alias;
- unary `PowerDelta` negation;
- `Power + PowerDelta`;
- `PowerDelta + PowerDelta`;
- `Power - Power`;
- a later derived quantity referencing an earlier derived quantity;
- nested expressions, verifying left-to-right dependency order;
- a calibration correction that references a derived value; and
- deterministic equality of two IR programs lowered from equivalent analyzed
  inputs, or field-by-field deterministic structure if full equality is not
  provided.

### Defensive failure tests

Use minimally modified or manually assembled semantic models only for states
that successful analysis cannot produce. Assert no partial program and the
appropriate diagnostic for:

- a symbol/reference expression whose `SymbolId` is not mapped;
- two declarations with the same semantic symbol;
- an expression's stored type disagreeing with its definition or operand;
- an invalid unary or binary semantic variant/type combination;
- a derived declaration that depends on a later or absent declaration;
- a calibration correction that is not `PowerDelta`;
- a calibration index that is missing, non-frequency, duplicated, or not the
  program's active sweep value; and
- malformed base declaration types for reference, sweep, or measurement where
  the public semantic representation permits construction of the state.

Do not repeat the semantic analyzer's complete rejection matrix in IR tests.
One end-to-end assertion should confirm that a semantic failure provides no
program to lower; ordinary unit/dimension/name errors remain Slice 2 tests.

## Implementation sequence

1. Add `horusrf_ir` and an empty `horusrf_ir_tests` target while keeping all
   existing parser, domain, and semantic tests green.
2. Define `ValueType`, `ValueId`, constant storage, typed operation payloads,
   `Value`, program metadata, named values, and calibration descriptors.
3. Add model-level tests for ownership, type inspection, and dependency order.
4. Define the lowering result and invariant-diagnostic contract.
5. Implement deterministic ID allocation plus reference, sweep, and
   measurement emission and symbol mapping.
6. Implement recursive left-to-right expression flattening for literals,
   resolved leaves, unary negation, power application, delta addition, and
   power difference.
7. Lower derived declarations in source order and preserve names/types/spans.
8. Lower calibration corrections and resolve ordered indexes to sweep
   `ValueId`s.
9. Add defensive invariant checks and ensure every failed lowering exposes no
   partial IR program.
10. Add the real canonical fixture test and the focused operation matrix.
11. Configure, build, and run all CTest tests from the repository root using
    only `build/`.

Each step must leave Slice 1 and Slice 2 behavior passing. Do not add runtime,
device, simulator, or CLI scaffolding to make the tests appear end-to-end.

## Acceptance checklist

- [ ] Existing parser, domain, and semantic behavior remains green.
- [ ] CMake configure and C++20 build succeed using only `build/`.
- [ ] CTest discovers and passes `horusrf_ir_tests` with all earlier tests.
- [ ] IR model headers contain no AST, runtime, device, or simulator dependency.
- [ ] Lowering accepts `semantic::AnalyzedProgram`, not raw syntax AST.
- [ ] The produced program owns all values, names, operations, and spans.
- [ ] Every IR value has one stable ID, one type, and one definition.
- [ ] Definitions are deterministic and ordered after their operands.
- [ ] Canonical quantities remain strong domain values, never untyped doubles.
- [ ] Every supported semantic expression maps to the explicit typed IR
      operation.
- [ ] The canonical fixture lowers without special-case names or values.
- [ ] Its IR contains reference, sweep, one measurement, derived error, named
      calibration, correction, and `over frequency` meaning.
- [ ] Calibration correction and ordered indexes remain distinct.
- [ ] The IR has no concrete device, simulator, execution, or point-generation
      dependency.
- [ ] Broken semantic-model invariants produce structured source-located
      lowering diagnostics and no partial program.
- [ ] No optimization, runtime, calibration application, CLI, or third-party
      dependency is added.

## Slice boundary and handoff

Slice 3 hands Slice 4 a self-contained typed experiment plan. Slice 4 may rely
on stable dataflow IDs, canonical values, backwards-only operand references,
one frequency sweep, one abstract power measurement, and typed calibration
metadata. It will bind `SweepFrequency` and `MeasurePower` to the small
`RfDevice` execution boundary while interpreting pure arithmetic operations.

Slice 3 does not decide how many sweep points exist, when a physical device is
configured, how measurement failures are represented, or where calibration
samples are stored. Those are runtime and later characterization concerns.
