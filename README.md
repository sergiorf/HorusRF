# HorusRF

HorusRF is a C++20 declarative DSL for reproducible RF characterization. Its
reference experiment configures a nominal tester TX output, sweeps 2.40–2.50 GHz in 1 MHz
steps, measures the resulting RF output with separate calibrated measurement
equipment, and derives a frequency-indexed power correction. A deterministic
simulator supplies both sides of that physical setup. The canonical program is
[`examples/tx_path_characterization.hrf`](examples/tx_path_characterization.hrf).
The repository is a focused, executable demonstration of how such a DSL could
look; it is not a staged product roadmap.

## Prerequisites and build

Use CMake 3.20 or newer, a C++20 compiler, and Java 11 or newer. Java runs the
pinned ANTLR generator for the source-private generated frontend; the generator
JAR and matching C++ runtime source are vendored, so configuration, compilation,
and tests do not download dependencies. Configure, build, and run all tests from the
repository root using the single `build/` directory:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To regenerate the authoritative HorusRF ANTLR parser explicitly, build its
generation target. Generated files stay under `build/generated/` and are never
committed:

```sh
cmake --build build --target horusrf_antlr_grammar_generated
```

With a multi-config generator, add `--config Debug` (or the configuration in use).
The generated frontend is the production HorusRF parser. Its ANTLR parse tree,
AST builder, and diagnostic adapter remain private to `horusrf_parser`; public
callers continue to use the existing HorusRF AST and diagnostic types.

Qt-free callers that need structured language services can include
`horusrf/frontend/language_services.hpp` and link `horusrf_frontend`.
`frontend::parse(source)` returns either an AST or syntax diagnostics;
`frontend::validate(source)` returns either an analyzed program or diagnostics
classified as syntax or semantic. Both preserve stable diagnostic codes,
messages, and source spans, and neither lowers nor executes the experiment. The
diagnostics-only `frontend::get_diagnostics(source)` convenience returns the same
ordered diagnostics. `frontend::get_language_spec()` exposes grammar-derived rule
and fixed-token metadata, while `frontend::get_language_capabilities()` describes
supported statements, measurements, sweep dimensions, units, functions, and
important semantic constraints. Both metadata objects are immutable and have
deterministic declaration order. The lower-level throwing `parser::parse()` API
remains available for compatibility.

Sources produced by automation are untrusted input. Discovering capabilities and
validating a source never lowers, executes, constructs a device, or accesses
hardware. Applications must validate every generated program and obtain explicit
user approval in a separate execution step before operating equipment. HorusRF
does not provide an LLM client, agent orchestrator, MCP server, or unrestricted
execution facility.

For a multi-config generator, choose one configuration consistently, for example:

```sh
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The executable is normally `build/horusrf-run` for a single-config generator and
`build/Debug/horusrf-run` (or the selected configuration) for a multi-config
generator. On Windows, append `.exe`.

The default build remains Qt-free. On Windows, the development script configures
the optional HorusRF Studio, builds it, runs the full test suite, and launches it:

```powershell
.\scripts\run-studio.cmd
```

The script defaults to a Debug build and the supported Qt installation at
`C:\Qt\6.12.0\msvc2022_64`. Override either value when necessary:

```powershell
.\scripts\run-studio.cmd -Configuration Release `
  -QtPath C:\path\to\Qt\6.x\msvc2022_64
```

Pass `-NoLaunch` to perform the configure, build, and test steps without opening
Studio. The `.cmd` entry point applies a PowerShell execution-policy bypass only
to the script process; it does not change the user's execution policy.

The equivalent manual commands are:

```powershell
cmake -S . -B build `
  -DHORUSRF_BUILD_STUDIO=ON `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.12.0\msvc2022_64
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
build\Debug\horusrf-studio.exe
```

On Linux, use the generator-neutral equivalent (omit `CMAKE_PREFIX_PATH` when
Qt with the Widgets and Charts development components is already discoverable):

```sh
cmake -S . -B build -DHORUSRF_BUILD_STUDIO=ON \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/horusrf-studio
```

For a multi-config generator, launch `build/<Configuration>/horusrf-studio`
instead. Studio provides a resizable editor workspace, HorusRF syntax highlighting,
line numbers with an emphasized current line, a live line-and-column status indicator,
standard file and undo/redo actions, and editor zoom controls in the View menu.
Use Ctrl++ and Ctrl+- to zoom, or Ctrl+0 to restore the default size.
Keyword, unit, and operator highlighting rules are built once from compiler-owned
fixed-token metadata derived from the generated grammar; Studio does not maintain
parallel keyword or unit lists and does not run a parser while highlighting.

A fresh window opens the commented
[`studio_default.hrf`](examples/studio_default.hrf) showcase as an untitled,
unmodified buffer without executing it. The experiment sweeps 21 frequencies by 6
temperatures for 126 points, captures named power, EVM, and frequency-error
measurements, evaluates three qualification assertions, and produces a two-dimensional
TX-power calibration surface. F5 validates the current editor buffer without operating
the simulator; F6 executes the current buffer, including unsaved changes, by invoking
the sibling `horusrf-run` executable.

The Examples menu progresses from Basic TX Calibration through TX Qualification,
Temperature Characterization, and Named RF Measurements to the Complete Studio
Showcase. Every example loads as an untitled, unmodified copy, after the normal prompt
for unsaved work, so the checked-in source is not overwritten accidentally.

Runs request the versioned JSON result from the CLI and present typed multidimensional
coordinates, measurements, derived values, qualification state, and calibration
entries without linking core libraries. The Points tab filters by coordinate and pass
state. Plot shows a scalar against one sweep axis while fixing the remaining
coordinates. Heatmap shows a scalar over two dimensions and its selectable cells link
back to Points. A new two-dimensional result initially opens Heatmap, preferring
`frequency` on X, `temperature` on Y, and `power_error`; a one-dimensional result opens
Plot. Compatible reruns preserve the selected tab, axes, scalar, and still-available
fixed coordinates instead of replacing deliberate choices.

Qualification failures are grouped by assertion and navigate back to the source.
Exit status 6 means execution completed and produced results but qualification failed;
it is distinct from a compiler, runtime, or process failure. The structured summary
therefore remains available for completed qualification failures. A later validation,
execution, or result-load failure also leaves the most recent valid result and summary
visible while reporting the new failure separately. The original strict CSV loader and
legacy frequency charts remain available for compatibility.

For deployment, keep `horusrf-run` beside `horusrf-studio`; Studio deliberately
finds and invokes that sibling process rather than linking the compiler or runtime.
On Windows, run the Qt installation's `windeployqt` on `horusrf-studio.exe` to
collect its dynamically linked Qt libraries and platform plugin. On Linux, package
the equivalent Qt Core, GUI, Widgets, and Charts shared libraries and platform
integration using the distribution's normal mechanism. Distributed open-source
bundles must include the Studio corresponding source and GPLv3 notice and satisfy
the Qt source, notice, relinking, and other obligations summarized in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Run the reference experiment

Using a multi-config Debug build:

```sh
build/Debug/horusrf-run examples/tx_path_characterization.hrf
build/Debug/horusrf-run examples/tx_path_characterization.hrf --check
build/Debug/horusrf-run examples/tx_path_characterization.hrf --csv output.csv
build/Debug/horusrf-run examples/named_measurements.hrf --json result.json
```

Substitute the generator-appropriate executable path described above. A successful
run reports the characterization and calibration names, 101 samples, RMS and maximum
absolute errors before and after correction, and the improvement ratio. CSV contains
one header plus 101 ordered data rows with this schema:

```text
frequency_hz,reference_power_dbm,measured_power_dbm,error_db,correction_db,corrected_power_dbm,residual_error_db
```

The correction comes from the program's calibration expression. The runner adds
standard report fields at every point: error is measured minus reference,
corrected power is measured plus correction, and residual error is corrected power
minus reference. These report fields are not implicit DSL declarations.

`--check` parses, semantically analyzes, and lowers the source without constructing
devices or executing the experiment. It is mutually exclusive with `--csv` and
`--json`, and those output modes are mutually exclusive with each other. It
produces no output when validation succeeds.

M1 through M5 support declaration-ordered frequency and temperature sweeps plus
named `power`, `evm`, and `frequency_error` measurements. Temperature
endpoints use `K` or `degC`; steps use `deltaK` or `deltaC`. For example,
[`multidimensional_check.hrf`](examples/multidimensional_check.hrf) has 21 × 6 =
126 points and succeeds both with `--check` and normal execution. Coordinates are
ordered by sweep declaration and the last dimension varies fastest. The legacy
single-frequency CSV schema is unchanged; requesting CSV for a multidimensional
program exits 4 with `output.csv_unrepresentable` before any equipment call.

[`qualified_measurements.hrf`](examples/qualified_measurements.hrf) demonstrates
typed comparisons, `abs()`, and per-point assertions. Assertions are all evaluated;
a false result does not stop the experiment. Qualified runs report point and
per-assertion totals. A completed experiment with any failed assertion emits
`qualification.assertion_failed` diagnostics and exits 6. Programs without
assertions retain the legacy summary exactly, and assertion results are not written
to the legacy CSV schema.

`--json` writes schema version 1, named
`horusrf.characterization-result`. It is the complete machine-readable format for
typed coordinates, measurements, derived values, assertion outcomes and source
spans, qualification summaries, ordered calibration-surface entries, and metrics.
JSON numbers are locale-independent and finite; a perfect calibration fit uses a
null improvement ratio with reason `perfect_fit`. Existing RMS and maximum-error
statistics are labeled `calibration_fit`, assertion statistics are labeled
`qualification`, and independent `validation` remains null because no independent
validation data set exists. Readers may accept unknown fields but must reject an
unknown schema name or unsupported major version.

Source failures are written without color as
`path:line:column: diagnostic.code: message`. Exit status is 0 for success, 2 for
usage, 3 for file I/O, 4 for source/compiler/runtime diagnostics, and 5 for an
unexpected failure. Status 6 means execution completed but qualification failed.
`horusrf-run --help` prints the accepted command forms.

## Semantic failure showcase

The files in [`examples/invalid`](examples/invalid) are deliberately invalid,
but syntactically well-formed, characterization programs. They demonstrate checks
that happen before the runtime touches a device or creates a requested CSV:

| Fixture | Rejected condition | Diagnostic |
|---|---|---|
| [`reference_uses_db.hrf`](examples/invalid/reference_uses_db.hrf) | A power delta (`dB`) is used where absolute power (`dBm`) is required | `semantic.unexpected_quantity_type` |
| [`reversed_sweep.hrf`](examples/invalid/reversed_sweep.hrf) | The sweep end precedes its start | `semantic.invalid_sweep_range` |
| [`power_plus_power.hrf`](examples/invalid/power_plus_power.hrf) | Two absolute powers are added | `semantic.invalid_binary_operands` |
| [`use_before_measurement.hrf`](examples/invalid/use_before_measurement.hrf) | Measured power is referenced before `measure power` | `semantic.reference_not_available` |
| [`calibration_over_power.hrf`](examples/invalid/calibration_over_power.hrf) | A calibration is indexed by a measurement instead of a sweep | `semantic.non_sweep_calibration_dimension` |

For example:

```sh
build/Debug/horusrf-run examples/invalid/power_plus_power.hrf
```

reports a source-localized error similar to:

```text
examples/invalid/power_plus_power.hrf:5:23: semantic.invalid_binary_operands: cannot add Power and Power
```

and exits with status 4. Supplying `--csv result.csv` still produces no CSV. The
CLI test suite runs every showcase fixture and verifies its diagnostic, exit status,
empty standard output, and lack of an output artifact.

These examples highlight a distinction from ad hoc procedural orchestration. The
C++ API also uses strong `Frequency`, `Temperature`, `TemperatureDelta`, `Power`,
and `PowerDelta` types, but the DSL
additionally validates the experiment as a whole: required declarations, statement
availability, sweep validity, expression dimensions, and calibration index shape.
Procedural code can implement the same checks, but must do so explicitly and invoke
them consistently before operating equipment. The procedural C++ example explicitly
coordinates the tester and measurement device; the HorusRF runtime performs that
coordination behind the unchanged `sweep frequency` and `measure power` intent.

## Reference documentation

- [Language reference](docs/language.md)
- [Architecture and execution model](docs/architecture.md)
- [Project roadmap](docs/project-roadmap.md)
- [Accepted DSL evolution contract](docs/dsl-evolution.md)
- [Test suite organization](tests/README.md)
- [Apache 2.0 core and CLI license](LICENSE)
- [GPLv3 Studio license](LICENSE-GPL-3.0)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

When changing the language, update the authoritative
[`grammar/HorusRf.g4`](grammar/HorusRf.g4), any semantic definition or frontend
metadata adapter affected by the change, the public language documentation, and
the lowest-layer tests in the same change. Generated ANTLR sources remain build
artifacts under `build/`; do not edit or commit them.

## Licensing

The HorusRF language, compiler, runtime, device, simulator, output, examples, and
`horusrf-run` CLI remain licensed under Apache License 2.0. The separate
`horusrf-studio` application and its Studio-specific tests are licensed under
GPL-3.0-only; their source files carry SPDX notices and the license text is in
[`LICENSE-GPL-3.0`](LICENSE-GPL-3.0). Qt remains under its own commercial or
open-source terms. See the [third-party notices](THIRD_PARTY_NOTICES.md) for
build and distribution details.

The current implementation is simulator-only. It does not provide real hardware support, general-purpose RF
automation, or persisted/imported calibration.
