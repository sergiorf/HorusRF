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

Use CMake 3.20 or newer and a C++20 compiler. Configure, build, and run all tests
from the repository root using the single `build/` directory:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

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
standard file and undo/redo actions, and the canonical example as an initially
unmodified, untitled template. F5 validates the current editor buffer and
F6 runs it, including unsaved changes, by invoking the sibling `horusrf-run`
executable. Compiler diagnostics are clickable, and raw standard output and standard
error remain available in separate panes. Successful runs plot measured, reference,
and corrected power against frequency and show the engine-produced calibration
correction in a second frequency plot. Both charts use padded bounds and GHz/dBm/dB
units; the reference curve remains toggleable. Studio formats the CLI-reported values
as calibration-fit metrics and explicitly notes that they are not independent
validation metrics. A later failure leaves the most recent successful graphs visible
and reports the new failure separately.

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
produces no output when validation succeeds.

Source failures are written without color as
`path:line:column: diagnostic.code: message`. Exit status is 0 for success, 2 for
usage, 3 for file I/O, 4 for source/compiler/runtime diagnostics, and 5 for an
unexpected failure. `horusrf-run --help` prints the accepted command forms.

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
C++ API also uses strong `Frequency`, `Power`, and `PowerDelta` types, but the DSL
additionally validates the experiment as a whole: required declarations, statement
availability, sweep validity, expression dimensions, and calibration index shape.
Procedural code can implement the same checks, but must do so explicitly and invoke
them consistently before operating equipment. The procedural C++ example explicitly
coordinates the tester and measurement device; the HorusRF runtime performs that
coordination behind the unchanged `sweep frequency` and `measure power` intent.

## Reference documentation

- [Language reference](docs/language.md)
- [Architecture and execution model](docs/architecture.md)
- [Test suite organization](tests/README.md)
- [Apache 2.0 core and CLI license](LICENSE)
- [GPLv3 Studio license](LICENSE-GPL-3.0)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

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
