# HorusRF

HorusRF is a C++20 declarative DSL for reproducible RF characterization. Its
reference experiment configures a nominal tester TX output, sweeps 2.40–2.50 GHz in 1 MHz
steps, observes an imperfect path through a deterministic simulator, and derives a
frequency-indexed power correction. The canonical program is
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

## Run the reference experiment

Using a multi-config Debug build:

```sh
build/Debug/horusrf-run examples/tx_path_characterization.hrf
build/Debug/horusrf-run examples/tx_path_characterization.hrf --csv output.csv
```

Substitute the generator-appropriate executable path described above. A successful
run reports the characterization and calibration names, 101 samples, RMS and maximum
absolute errors before and after correction, and the improvement ratio. CSV contains
one header plus 101 ordered data rows with this schema:

```text
frequency_hz,reference_power_dbm,measured_power_dbm,error_db,correction_db,corrected_power_dbm,residual_error_db
```

Source failures are written without color as
`path:line:column: diagnostic.code: message`. Exit status is 0 for success, 2 for
usage, 3 for file I/O, 4 for source/compiler/runtime diagnostics, and 5 for an
unexpected failure. `horusrf-run --help` prints the accepted command form.

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
them consistently before operating a device.

## Reference documentation

- [Language reference](docs/language.md)
- [Architecture and execution model](docs/architecture.md)
- [Test suite organization](tests/README.md)
- [Apache 2.0 license](LICENSE)

The current implementation is simulator-only. It does not provide real hardware support, general-purpose RF
automation, or persisted/imported calibration.
