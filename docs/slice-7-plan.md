# Slice 7 Implementation Plan — End-to-End Proof and CSV

## Status and objective

**Status:** planned; not yet implemented.

Slice 7 turns the completed M0 compiler, runtime, simulator, and procedural
reference into one user-visible proof. It adds a `horusrf-run` executable that
reads a real HorusRF source file, runs the complete declarative pipeline against
a fresh `SimulatedRfDevice`, reports the resulting metrics, and optionally
writes all characterization samples as CSV.

The slice also closes the comparison promised by the roadmap. One integration
test starts from the canonical `.hrf` file and crosses every production layer.
A second integration test runs that declarative program and the independent
procedural C++ example against separate fresh simulators, then compares their
complete `domain::CharacterizationResult` values.

At the end of this slice, these documented commands work from the repository
root after building:

```text
build/bin/horusrf-run examples/tx_path_characterization.hrf
build/bin/horusrf-run examples/tx_path_characterization.hrf --csv output.csv
```

The exact executable location is generator-dependent; documentation may use
`build/bin/Debug/horusrf-run.exe` or `build/bin/Release/horusrf-run.exe` where a
multi-config generator requires it. The command-line contract itself remains:

```text
horusrf-run <source.hrf> [--csv <output.csv>]
```

## Scope

### In scope

- a `horusrf-run` command-line executable;
- loading a user-supplied HorusRF source file;
- the complete parse, semantic-analysis, IR-lowering, and characterization
  execution pipeline;
- a fresh deterministic `device::SimulatedRfDevice` per CLI invocation;
- concise console output derived from the completed result;
- reusable CSV serialization of `domain::CharacterizationResult`;
- optional CSV file output selected by `--csv`;
- stable usage, diagnostic, stream, and process-exit behavior;
- a real-file declarative end-to-end integration test;
- a full procedural/declarative equivalence integration test;
- focused CSV tests and a process-level CLI acceptance test;
- CMake targets and CTest registrations for all new code; and
- continued success of every Slice 1–6 test from the existing `build/` tree.

### Out of scope

- changes to the language grammar or canonical `.hrf` program;
- changes to domain quantities, result fields, simulator behavior, or the
  procedural experiment;
- hardware-device selection, discovery, or plugin infrastructure;
- configurable sweep parameters or overrides on the command line;
- reading calibration artifacts back from CSV;
- a general table/dataframe abstraction;
- alternate output formats such as JSON;
- CSV schema versioning or metadata preambles;
- progress bars, color, logging frameworks, or interactive prompts;
- batch execution of multiple source files;
- installation or packaging rules;
- third-party argument-parsing, CSV, or test dependencies; and
- broad documentation and test reorganization reserved for Slice 8.

If implementation reveals a defect in an earlier layer, fix it in that layer
with focused regression coverage. Do not expand the M0 language or device model
to solve a CLI or serialization concern.

## Existing contracts to reuse

The declarative path is composed from the completed public APIs:

```text
source text
  -> parser::parse
  -> semantic::analyze
  -> ir::lower
  -> runtime::execute_characterization
  -> domain::CharacterizationResult
```

The CLI must call those APIs in that order. It must not manually construct AST,
semantic, or IR nodes, and it must not duplicate parsing, validation, lowering,
sweep generation, expression evaluation, result construction, or metric
calculation.

The equivalence test also calls the completed procedural entry point:

```cpp
domain::CharacterizationResult
run_tx_path_characterization(device::RfDevice& device);
```

The two paths may share domain value/result types, the public device contract,
the deterministic simulator implementation, and metric formulas. They must
continue to have independent sweep and per-point orchestration.

## Files and CMake targets

Add:

```text
include/horusrf/output/csv.hpp
src/output/csv.cpp
src/cli/main.cpp
tests/output/csv_tests.cpp
tests/integration/end_to_end_tests.cpp
tests/integration/equivalence_tests.cpp
tests/integration/cli_acceptance.cmake
```

Add a static `horusrf_output` target containing the CSV implementation. It
exposes the normal public include directory, requires C++20, uses the repository
warning policy, and links publicly only to `horusrf_domain`.

Add the `horusrf-run` executable. It links to `horusrf_output`,
`horusrf_runtime`, `horusrf_simulator`, `horusrf_parser`, `horusrf_semantic`, and
`horusrf_ir`. Keep `main.cpp` responsible for application orchestration and
presentation only; lower layers remain unaware of files, arguments, and CSV.

Add these test targets and CTest registrations:

| Target/test | Purpose | Principal dependencies |
|---|---|---|
| `horusrf_csv_tests` | CSV schema, values, precision, and stream failures | `horusrf_output`, `horusrf_domain` |
| `horusrf_end_to_end_tests` | real `.hrf` file through declarative execution | parser through runtime, simulator |
| `horusrf_equivalence_tests` | complete two-path result comparison | declarative stack, simulator, procedural example |
| `horusrf_cli_acceptance` | actual process, console output, and CSV file | built `horusrf-run` executable |

Pass `HORUSRF_SOURCE_DIR` only to C++ integration targets that need the canonical
fixture. Pass the executable and fixture paths to the CMake acceptance script
with `-D` arguments using `$<TARGET_FILE:horusrf-run>` and
`${CMAKE_CURRENT_SOURCE_DIR}`. Write acceptance-test output only below the
existing build tree; do not write generated CSV into the source tree.

Do not create another build directory. Configure, build, and test only through
`build/`.

## CLI contract

### Accepted forms

The executable accepts exactly:

```text
horusrf-run <source.hrf>
horusrf-run <source.hrf> --csv <output.csv>
horusrf-run --help
```

`--help` writes usage text to standard output and exits successfully without
opening a source file or constructing a device. With no arguments, an unknown
option, a missing CSV path, a repeated `--csv`, or extra positional arguments,
the executable writes the same usage line plus a short error to standard error
and exits with the usage-error code.

Reject a CSV destination that names the source file itself so a successful
experiment cannot overwrite its own input. Compare normalized absolute paths
and, when both paths already exist, filesystem identity. Report this as invalid
usage before opening either file.

For M0, keep parsing intentionally small and explicit. Do not introduce a
general argument parser. Treat paths as opaque argument strings; a path ending
in `.hrf` or `.csv` is conventional but not required.

### Exit codes

Use these stable process outcomes:

| Code | Meaning |
|---:|---|
| `0` | execution and all requested output completed successfully |
| `2` | invalid command-line usage |
| `3` | source or CSV file I/O failure |
| `4` | parse, semantic, lowering, or runtime diagnostic failure |
| `5` | device or other unexpected execution exception |

Do not emit success metrics or claim a CSV path on any nonzero exit. A failed
run must not be converted into an apparently successful empty result.

### Source-file behavior

Open the input in binary mode and read it completely into an owning
`std::string`; this preserves parser offsets and avoids platform newline
translation. An empty file is passed to the parser and reported as a parse
failure rather than relabeled as an I/O failure.

File open/read failures include the source path and return exit code 3. No
device is constructed and no CSV is opened before source loading, parsing,
semantic analysis, and lowering have succeeded.

After lowering, construct a fresh `SimulatedRfDevice` and call
`runtime::execute_characterization`. Device exceptions propagate to the CLI's
top-level exception boundary and produce exit code 5. Structured runtime
diagnostics produce exit code 4.

### Diagnostics

Write all errors to standard error. Structured compiler/runtime diagnostics use
one line per diagnostic with this shape:

```text
<source-path>:<line>:<column>: <stage>: <message>
```

The stage is one of `parse`, `semantic`, `lowering`, or `runtime`. Use the
diagnostic's beginning source position. `parser::ParseError` contributes its
single structured diagnostic; analysis, lowering, and runtime results print all
diagnostics in their existing order.

I/O, usage, and unexpected exception messages do not invent a source position.
They use a concise `horusrf-run: <message>` prefix. Do not add formatting or
diagnostic concerns to compiler/runtime libraries in this slice.

### Console success output

On success, print a compact, deterministic summary to standard output from the
lowered program and returned result:

```text
characterization: tx_path
samples: 101
calibration: tx_power
uncorrected_rms_error_db: <value>
corrected_rms_error_db: <value>
maximum_absolute_error_db: <value>
corrected_maximum_absolute_error_db: <value>
rms_improvement_ratio: <value>
csv: <output-path>              # only when --csv was requested
```

Use the classic locale and enough significant digits to make values stable and
useful; exact decorative spacing is not an API. The labels, sample/artifact
identity, and numeric source are the contract. Metrics must come directly from
`CharacterizationResult::metrics`, not be recalculated in the CLI.

## CSV serialization contract

Expose a narrow stream-based API:

```cpp
namespace horusrf::output {

void write_csv(std::ostream& output,
               const domain::CharacterizationResult& result);

} // namespace horusrf::output
```

The serializer owns formatting only. It does not open files, print console
metrics, validate compiler state, run an experiment, or mutate the result. The
CLI owns the `std::ofstream`, calls `write_csv`, flushes/closes it, and checks
the stream before reporting success.

Emit this exact header as one line:

```text
frequency_hz,reference_power_dbm,measured_power_dbm,error_db,correction_db,corrected_power_dbm,residual_error_db
```

Emit one subsequent row for each sample in vector order, with fields in the
same order as the header. Write canonical numeric values obtained only through
the strong quantity accessors:

| CSV column | Value source |
|---|---|
| `frequency_hz` | `sample.frequency.hertz()` |
| `reference_power_dbm` | `sample.reference_power.dbm()` |
| `measured_power_dbm` | `sample.measured_power.dbm()` |
| `error_db` | `sample.error.db()` |
| `correction_db` | `sample.correction.db()` |
| `corrected_power_dbm` | `sample.corrected_power.dbm()` |
| `residual_error_db` | `sample.residual_error.db()` |

Use `std::locale::classic()` and `std::numeric_limits<double>::max_digits10` so
serialized values round-trip without locale-dependent decimal commas or
avoidable precision loss. Use `\n` line endings and end the final row with a
newline. All M0 fields are numeric, so quoting and escaping are unnecessary.

Preserve the caller's stream formatting state (flags, precision, and locale)
after the function returns. If the stream cannot accept the complete document,
throw `std::ios_base::failure`; do not silently return partial-success status.
An empty result still produces the header and no data rows.

The CSV deliberately contains samples only. The artifact and aggregate metrics
remain in the console summary and in the in-memory result; adding metadata rows
would make the promised rectangular schema ambiguous.

## Declarative end-to-end integration test

`horusrf_end_to_end_tests` must begin by opening the actual
`examples/tx_path_characterization.hrf` file. It then calls the public parser,
semantic analyzer, lowerer, runtime, and simulator APIs in production order. It
must not use a source string copied into the test, construct AST/IR manually,
or inspect simulator-private state.

Assert the intermediate meaning that could otherwise be lost between layers:

- the semantic program is named `tx_path`;
- the reference is `-10 dBm`;
- the sweep is `2.40e9` through `2.50e9 Hz` in `1.0e6 Hz` steps;
- the calibration is named `tx_power`;
- its sole index is the active frequency sweep; and
- the lowered calibration preserves that binding.

Then execute against a fresh simulator and assert:

- exactly 101 samples in ascending inclusive frequency order;
- every adjacent frequency differs by `1.0e6 Hz` within the established
  frequency tolerance;
- every reference power is `-10 dBm`;
- representative errors at indexes 0, 25, 50, 75, and 100 are approximately
  `+0.35`, `+0.55`, `+0.35`, `+0.15`, and `+0.35 dB`;
- every error equals measured minus reference;
- every correction equals reference minus measured;
- every corrected power equals measured plus correction;
- every residual equals corrected minus reference;
- artifact dimensions and corrections align one-to-one with samples; and
- corrected RMS and maximum absolute error are below a small absolute tolerance
  and materially below their uncorrected counterparts.

This test overlaps some lower-layer acceptance assertions intentionally: its
purpose is to prove the assembled production path from the real file, not to
replace focused unit/regression tests.

## Procedural/declarative equivalence integration test

`horusrf_equivalence_tests` loads and compiles the same real fixture, then uses
two distinct fresh simulators:

```text
fresh simulator A -> examples::run_tx_path_characterization
fresh simulator B -> parsed/lowered HorusRF program -> runtime
```

Compare the returned results field by field. First require equal sample counts,
artifact names, and artifact vector sizes. For every index, compare:

- frequency;
- reference power;
- measured power;
- error;
- correction;
- corrected power;
- residual error;
- artifact dimension; and
- artifact correction.

Also compare all five metrics:

- uncorrected RMS error;
- corrected RMS error;
- maximum absolute error;
- corrected maximum absolute error; and
- RMS improvement ratio.

Use named tolerances in the test rather than exact floating-point equality.
Use `1e-3 Hz` for frequency values and `1e-12` for power, power-delta, and
metric values unless implementation evidence requires a documented adjustment.
Mismatch messages must identify the path pair, field, and sample index so a
failure is actionable.

Do not make the production implementations compare themselves, call each
other, share an orchestration helper, or consume a result from the same
simulator instance. Equivalence is an integration-test responsibility.

## CSV tests

Build a small hand-authored `CharacterizationResult` with at least two samples,
including negative, positive, fractional, and large frequency values. Verify:

- the header is exact and appears once;
- there is one row per sample in original order;
- every column maps to the correct result field;
- all rows have seven comma-separated fields;
- output uses `.` as the decimal separator under a stream imbued with a locale
  that would otherwise use a decimal comma;
- values parse back to the original doubles within round-trip expectations;
- the final row ends in `\n`;
- an empty result emits only the header;
- caller flags, precision, and locale are restored; and
- a deliberately failing stream causes `std::ios_base::failure`.

Also serialize the canonical declarative result in an integration assertion and
check 102 lines, the exact header, first/last frequencies, and representative
data values. Do not compare against a checked-in 101-row golden CSV; field-level
assertions provide clearer failures and avoid maintaining generated data.

## CLI process acceptance test

The `horusrf_cli_acceptance` CMake script invokes the built executable with the
canonical source and a CSV path below the current binary directory. It must:

1. remove only its own known prior output file if present;
2. run the no-CSV command and require exit code 0;
3. verify the console summary contains the characterization, 101 samples,
   `tx_power`, and all metric labels;
4. run the `--csv` command and require exit code 0;
5. verify the announced CSV path and that the file exists;
6. read the generated file, require the exact header and 101 data rows, and
   check representative first and last row fields; and
7. invoke one invalid-usage case and one missing-source case, requiring exit
   codes 2 and 3 with nonempty standard-error messages.

The test must pass executable and fixture paths as separate arguments, quote
paths correctly, and work when either path contains spaces. It may clean up its
own generated CSV after verification, but it must not delete or scan unrelated
build outputs.

## Failure and side-effect contract

- Argument validation happens before any file or device activity.
- The CSV destination is proven distinct from the source before either is
  opened.
- Source loading and compilation finish before device execution begins.
- CSV creation happens only after execution has produced a complete result.
- Compiler/runtime diagnostic failures never create a CSV file.
- A CSV open failure returns code 3 and reports the requested path.
- A write/flush/close failure returns code 3 and is never reported as success.
- The implementation does not retry device measurements or output writes.
- Device and unexpected standard exceptions are caught only at the CLI boundary
  to prevent process termination without context; library behavior remains
  unchanged.
- If output fails after a file has been created, the CLI makes a best-effort
  removal of that exact requested file and reports the failure. It must never
  broaden cleanup to a directory or wildcard.

## Implementation sequence

1. Add the stream-based CSV public header and `horusrf_output` target.
2. Implement exact headers, canonical units, classic-locale formatting,
   round-trip precision, stream-state restoration, and write-failure detection.
3. Add focused CSV unit tests, including locale and failing-stream cases.
4. Add `horusrf-run` with explicit M0 argument parsing and stable usage exits.
5. Implement binary source loading and the parse/analyze/lower/execute sequence.
6. Add stage-aware structured diagnostic rendering and exception handling.
7. Add deterministic console summary output from the completed program/result.
8. Add `--csv` file ownership, serialization, stream checks, and exact-file
   failure cleanup.
9. Add the real-source declarative end-to-end integration target.
10. Add the fresh-simulator procedural/declarative equivalence target with
    indexed mismatch reporting.
11. Add the process-level CMake acceptance test for documented CLI forms,
    generated CSV, invalid usage, and missing input.
12. Update the README with build-tree-aware commands and a short CSV schema
    description; do not perform Slice 8's broader documentation rewrite.
13. Configure and build in the existing `build/` directory.
14. Run the two integration tests and CLI acceptance test directly for focused
    feedback, then run the complete CTest suite.
15. Inspect the source tree and confirm no generated CSV or alternate build
    directory was left behind.

## Acceptance checklist

- [ ] `horusrf-run <source.hrf>` executes the complete production pipeline.
- [ ] `horusrf-run <source.hrf> --csv <path>` writes the documented CSV.
- [ ] `--help`, usage errors, I/O failures, structured diagnostics, and
      unexpected exceptions follow the stated stream and exit-code contract.
- [ ] The CLI uses a fresh simulator and does not inspect hidden simulator state.
- [ ] The CLI constructs no AST, semantic, or IR objects by hand.
- [ ] Console metrics come from the returned `CharacterizationResult`.
- [ ] CSV serialization is reusable, stream-based, and domain-only.
- [ ] The CSV header and seven-column ordering exactly match the contract.
- [ ] CSV contains canonical units, classic-locale numbers, round-trip
      precision, ordered rows, and a final newline.
- [ ] Stream formatting state is restored and write failures are observable.
- [ ] The end-to-end test starts from the actual canonical `.hrf` file.
- [ ] It proves semantic/IR calibration indexing and all 101 runtime samples.
- [ ] The equivalence test uses two separate fresh simulator instances.
- [ ] It compares every sample, artifact entry, and metric with named tolerances.
- [ ] Production declarative and procedural orchestration remain independent.
- [ ] The actual CLI process is tested with and without CSV output.
- [ ] The process test verifies 101 real data rows and failure exit codes.
- [ ] No generated CSV is committed or left in the source tree.
- [ ] No new grammar, domain, device, simulator, or procedural behavior is added.
- [ ] No third-party dependency or alternate build directory is introduced.
- [ ] Existing Slice 1–6 tests remain green.
- [ ] CMake configuration, compilation, and the full CTest suite pass using only
      the repository's existing `build/` directory.

## Slice boundary and handoff

Slice 7 delivers the runnable M0 proof:

```text
canonical .hrf -> compiler -> runtime -> simulator -> result -> console/CSV
                                   compared with
procedural C++ ---------------> simulator -> result
```

Slice 8 may reorganize tests, expand architecture and language documentation,
add diagrams, improve diagnostics consistently across layers, and harden the
clean-build presentation. It must not be required to make the Slice 7 commands,
CSV contract, or integration proofs work.
