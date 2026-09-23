# Slice 8 Implementation Plan — Documentation and Hardening

## Status and objective

**Status:** planned; implementation has not started.

Slice 8 closes M0 by making the implemented system understandable, diagnosable,
and reproducible from a clean checkout. It documents the language and architecture
as they exist after Slice 7, makes implicit engineering constraints explicit,
stabilizes CLI diagnostic presentation, and organizes the deterministic CTest
suite by architectural responsibility.

This is a hardening slice. It must not add language features, change RF behavior,
replace the handwritten compiler, or weaken the independent procedural versus
declarative comparison completed in Slice 7.

A new user must be able to build and run the canonical experiment, export its 101
samples, understand the supported language and execution path, interpret failures,
and run either the full suite or a documented test group without reading the
implementation first.

## Scope

### In scope

- expand `README.md` into the concise M0 entry point;
- add `docs/architecture.md` for implemented boundaries and dependency direction;
- add `docs/language.md`, synchronized with `grammar/horusrf.ebnf`;
- expand `AGENTS.md` with durable repository and architecture constraints;
- add architecture and execution diagrams;
- give every emitted diagnostic a stable textual code;
- normalize CLI diagnostics to path, one-based location, code, and message;
- add focused diagnostic and CLI failure-path tests;
- label and document CTest suites without merging independent tests;
- audit commands, links, names, units, limitations, and public headers against the
  implementation; and
- verify configure, build, tests, CLI, and CSV using only `build/`.

### Out of scope

- new syntax, expressions, units, dimensions, or grammar productions;
- calibration import/application syntax or persisted artifacts;
- simulator-model changes, real hardware, or programmable external instruments;
- LLVM, code generation, optimization, LSP, or editor tooling;
- packaging, installation, or binary distribution;
- third-party documentation or test frameworks;
- CLI, exit-code, CSV-schema, or numeric-precision changes;
- shared sweep/per-point orchestration between procedural and declarative paths;
- speculative production refactoring; and
- alternate local build directories such as `build-debug/`.

## Existing contracts to preserve

The canonical fixture remains `examples/tx_path_characterization.hrf`: a `-10 dBm`
reference and inclusive `2.40 GHz` through `2.50 GHz` sweep in `1 MHz` steps,
producing 101 ordered samples and `tx_power : Frequency -> PowerDelta`.

The declarative path remains:

```text
source -> lexer/parser -> AST -> semantic model -> typed IR
       -> runtime -> RfDevice -> CharacterizationResult -> console/CSV
```

The procedural path continues to depend only on domain/device infrastructure. The
paths may share quantities, result types, `RfDevice`, simulator, and metric formulas,
but not parsing, lowering, sweep orchestration, or per-point orchestration.

Accepted CLI forms remain:

```text
horusrf-run <source.hrf>
horusrf-run <source.hrf> --csv <output.csv>
horusrf-run --help
```

Exit codes remain `0` success, `2` usage, `3` I/O, `4` source/compiler/runtime
diagnostic, and `5` unexpected failure. CSV retains this exact header:

```text
frequency_hz,reference_power_dbm,measured_power_dbm,error_db,correction_db,corrected_power_dbm,residual_error_db
```

The simulator remains deterministic and private. Documentation may describe
observable representative values but must not expose or depend on its private
transfer-function implementation.

## Documentation ownership

| Document | Responsibility |
|---|---|
| `README.md` | First-run guide and high-level status; link instead of duplicating details. |
| `docs/language.md` | User-facing implemented M0 language and diagnostic behavior. |
| `grammar/horusrf.ebnf` | Authoritative concrete syntax. |
| `docs/architecture.md` | Components, dependencies, ownership, and execution model. |
| `docs/m0-roadmap.md` | M0 rationale, slice sequence, and milestone record. |
| `docs/archive/slice-*.md` | Accepted records for completed slices. |
| `AGENTS.md` | Operational constraints for future changes. |

Links must be relative, correctly capitalized, and resolvable from their containing
file. Proposed features must be visibly separated from implemented behavior.

## README implementation

Use this order:

1. project purpose and M0 experiment;
2. slice status with archive/active-plan links;
3. prerequisites: CMake 3.20+ and a C++20 compiler;
4. configure/build/test commands using only `build/`;
5. generator-aware executable locations;
6. canonical run and CSV examples;
7. compact success-output and CSV descriptions;
8. diagnostic shape and exit-code summary; and
9. language, architecture, roadmap, and license links.

The root commands must be directly copyable:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

For multi-config generators, show one consistent configuration in build, test, and
executable paths. Do not imply that a `Debug/` or `Release/` subdirectory exists
for single-config generators. Do not claim hardware support, general-purpose RF
coverage, calibration persistence, or future syntax.

## Architecture document

Create `docs/architecture.md` with these sections.

### Context and physical model

Explain that M0 configures nominal tester output and frequency through `RfDevice`,
observes external power, derives error/correction, and returns owning result data.
External measurement equipment belongs to the simulated physical setup and is not
a separately programmable DSL instrument.

### Compile and execution flow

Add a Mermaid flowchart containing source, lexer/parser, AST, semantic analysis, IR,
runtime, device API, simulator, result, console, and CSV. Show procedural C++ joining
only at shared domain/device/result boundaries. Map nodes to actual targets:
`horusrf_parser`, `horusrf_domain`, `horusrf_semantic`, `horusrf_ir`,
`horusrf_device`, `horusrf_runtime`, `horusrf_simulator`,
`horusrf_procedural_example`, `horusrf_output`, and `horusrf-run`.

### Representation and ownership boundaries

Document that AST preserves written structure/spans; semantic analysis resolves
names and dimensions; IR is a typed device-independent plan; runtime consumes IR
through `RfDevice`; domain quantities/results cross both orchestration paths; and
CSV is a terminal presentation layer. Results own samples/artifact vectors, devices
are caller-owned references, and returned data aliases neither source nor simulator.

### Runtime sequence and failure behavior

Add a one-point Mermaid sequence: `setFrequency`, `setOutputPower`, `measurePower`,
then error/correction/corrected/residual computation. Describe index-based inclusive
sweep generation, one measurement per point, artifact/sample alignment, final metric
calculation, and no retry or partial result after device exceptions.

### Dependencies, determinism, and extension points

Give each target's allowed dependencies. Explicitly forbid domain-to-compiler,
AST-to-semantic/runtime/device, runtime-to-simulator-internals, and procedural-to-
compiler/runtime dependencies. Explain deterministic fresh-simulator equivalence,
strong dBm/dB types, and locale-independent output. Put hardware devices, richer
artifacts, application syntax, and compiler backends in a clearly marked future
section.

## Language reference

Create `docs/language.md` from the canonical fixture and small negative examples.
It must cover:

1. whitespace, comments, identifiers, numbers, keywords, punctuation, units, and
   source positions;
2. exactly one `characterize` block;
3. reference declarations and `reference.power`;
4. inclusive frequency sweeps, positive step, ordering, and runtime size limits;
5. `measure power` and its active-sweep association;
6. identifiers, quantities, references, parentheses, unary minus, `+`, `-`, and
   associativity;
7. calibration correction type, explicit `over`, and artifact interpretation;
8. canonical Hz, dBm, and dB conversion;
9. complete supported and rejected dBm/dB arithmetic;
10. declaration order, duplicate/missing/unknown names, and calibration indexes;
11. diagnostic examples with stable codes and one-based locations; and
12. current limitations versus future features.

Link or embed the EBNF while naming `grammar/horusrf.ebnf` authoritative. Compare
every production with lexer tokens, parser entry points, and prose. Resolve any
disagreement deliberately; do not document a parser accident as intended behavior.

At minimum document these implemented arithmetic rules:

| Expression | Result |
|---|---|
| `Power - Power` | `PowerDelta` |
| `Power + PowerDelta` | `Power` |
| `PowerDelta + PowerDelta` | `PowerDelta` |
| unary `-PowerDelta` | `PowerDelta` |

Audit rejected operations—`Power + Power`, `PowerDelta + Power`,
`PowerDelta - PowerDelta`, `PowerDelta - Power`, `Power - PowerDelta`, and unary
`-Power`—against the analyzer. Tests and accepted implementation win over stale
prose; correct roadmap inconsistencies explicitly.

State that M0 has no conditionals, general loops, functions, arrays, interpolation,
phase/gain/temperature dimensions, instrument declarations, calibration import or
application syntax, procedural DSL statements, or multiple characterizations.
Runtime applies correction internally only for verification metrics.

## Diagnostic hardening

Add beside each parser, semantic, IR, and runtime diagnostic enum:

```cpp
[[nodiscard]] std::string_view diagnostic_code_name(DiagnosticCode code) noexcept;
```

Implement names in the owning layer as lowercase dotted values:

```text
parse.unexpected_token
semantic.invalid_binary_operands
lowering.invalid_calibration_index
runtime.sweep_too_large
```

Use `lowering` for IR diagnostics because it is the visible stage. Every enumerator
gets exactly one stable name. Unknown numeric values may map to `<stage>.unknown`.
Conversion must not throw or allocate. Do not add a cross-layer diagnostic library.

Change source diagnostic output to:

```text
<path>:<line>:<column>: <diagnostic-code>: <message>
```

Usage/I/O errors remain `horusrf-run: <message>`. Do not emit ANSI color by
default. Extend tests to:

- exhaustively check every enum mapping;
- retain representative code/span/message checks;
- run one parse-invalid and one semantic-invalid CLI fixture;
- assert exit `4`, empty stdout, and exact path/location/code prefix;
- assert diagnostic failure creates no requested CSV; and
- retain distinct usage (`2`) and I/O (`3`) behavior.

Avoid full-message golden files where wording is not contractual. Codes, locations,
exit behavior, and side effects are contractual.

## Test organization

Keep sources in existing layer directories and keep end-to-end and equivalence tests
separate. Add CTest labels:

| Label | Suites |
|---|---|
| `unit` | parser, domain, semantic, IR, device, executor, characterization, procedural example, CSV |
| `integration` | declarative end-to-end and procedural/declarative equivalence |
| `cli` | CLI process acceptance |

Each `ctest -L <label>` must select at least one test; unfiltered CTest remains the
acceptance authority. Add `tests/README.md` mapping suites to layers, explaining
unit/integration/equivalence/process roles, listing commands, requiring deterministic
no-network/no-hardware tests, and guiding new tests to the lowest useful layer.

Do not hide explicit target link lines behind a generic registration abstraction;
they are useful dependency documentation.

## AGENTS.md implementation

Retain the single-`build/` rules and add durable constraints to:

- synchronize EBNF, parser, language reference, and tests for syntax changes;
- preserve AST/semantic/IR/runtime/device/simulator/output boundaries;
- use `Frequency`, `Power`, and `PowerDelta` at public/cross-layer boundaries;
- keep dimensional conversion in domain/semantic code;
- make runtime depend on `RfDevice`, never simulator internals;
- test simulator observables without exposing its private model;
- preserve independent procedural/declarative orchestration;
- keep tests free of network, clock, randomness, and real hardware;
- update public documentation with behavior changes;
- avoid speculative frameworks/dependencies;
- keep archive records unchanged except factual link repairs; and
- run full CTest after cross-layer changes.

## File plan

### New files

- `docs/architecture.md`;
- `docs/language.md`;
- `tests/README.md`; and
- layer-local diagnostic-name sources only if existing implementation files would
  become less clear.

### Modified files

- `README.md`, `AGENTS.md`, and `docs/m0-roadmap.md`;
- parser, semantic, IR, and runtime diagnostic headers and owning sources;
- `src/cli/main.cpp`;
- corresponding layer tests;
- `tests/integration/cli_acceptance.cmake`; and
- `CMakeLists.txt` for any sources and CTest labels.

Audit `grammar/horusrf.ebnf`; change it only for a demonstrated mismatch with
accepted behavior. Production RF computation, simulator constants, CSV serialization,
and the canonical fixture should not need changes.

## Implementation sequence

1. Capture full-suite, CLI, and CSV baselines from existing `build/`.
2. Audit grammar, tokens, parser, semantics, IR, runtime, CLI, CSV, targets, and tests.
3. Add diagnostic-name mappings and exhaustive layer tests.
4. Update CLI presentation and process failure coverage.
5. Add CTest labels and `tests/README.md`; verify label selection.
6. Write `docs/language.md` from accepted grammar/analyzer behavior.
7. Write `docs/architecture.md` from actual targets and data flow.
8. Expand `AGENTS.md` with durable constraints.
9. Revise README last so all links and commands exist.
10. Repair factual roadmap inconsistencies without rewriting archive history.
11. Configure/build, run each label, then run unfiltered CTest.
12. Run canonical CLI with and without CSV; verify summary, header, 101 rows, and
    locale-independent parseable numbers.
13. inspect links and Mermaid syntax without adding a dependency solely for it.
14. Ensure no generated CSV, alternate build, or temporary file remains.
15. On acceptance, check the list, move this plan to
    `docs/archive/slice-8-plan.md`, and update status links.

## Verification commands

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build -L unit --output-on-failure
ctest --test-dir build -L integration --output-on-failure
ctest --test-dir build -L cli --output-on-failure
ctest --test-dir build --output-on-failure
```

For multi-config generators, use the same `--config`/`-C` value throughout. CLI
verification uses the generator-appropriate executable path. Remove its disposable
CSV afterward and never replace the source fixture.

## Acceptance checklist

- [ ] README prerequisites and build/test/run/CSV commands are accurate and copyable.
- [ ] Executable guidance distinguishes single- and multi-config generators.
- [ ] README links resolve to all active reference/status documents.
- [ ] Architecture prose and diagrams match actual targets, dependencies,
      ownership, runtime ordering, and failure behavior.
- [ ] Procedural orchestration remains compiler/runtime independent.
- [ ] Language reference agrees with EBNF, parser, semantics, and canonical units.
- [ ] All supported/rejected arithmetic, ordering, indexing, and limitations are
      documented; future features are clearly marked.
- [ ] Every diagnostic enumerator has one tested, stable, lowercase textual code.
- [ ] Conversion is exhaustive, non-throwing, and allocation-free.
- [ ] CLI diagnostics use `path:line:column: code: message`.
- [ ] Parse/semantic failures return `4`, emit no success output, and leave no CSV.
- [ ] Usage and I/O retain exit codes `2` and `3`.
- [ ] AGENTS retains build rules and records all durable architecture/test rules.
- [ ] Every CTest suite has an appropriate `unit`, `integration`, or `cli` label.
- [ ] Each labeled command and the unfiltered suite pass.
- [ ] `tests/README.md` accurately describes responsibilities and commands.
- [ ] Tests remain deterministic, offline, and hardware-independent.
- [ ] Canonical execution still produces 101 ordered deterministic samples and
      established metrics.
- [ ] CSV retains seven columns, 101 rows, classic locale, and round-trip precision.
- [ ] No language feature, hardware backend, dependency, or shared orchestration is
      introduced.
- [ ] All validation uses only `build/`, with no generated artifact left behind.

## M0 handoff

Slice 8 is complete only when checked-in documentation describes checked-in
behavior and checked-in tests prove the documented commands and diagnostics. After
acceptance, archive this record as the final M0 slice. Later milestones must treat
strong quantities, typed compiler boundaries, device abstraction, deterministic
reference behavior, and the independent equivalence proof as explicit compatibility
decisions.
