# HorusRF

HorusRF is a C++20 declarative DSL for reproducible RF characterization. Its
reference experiment configures a nominal tester TX output, sweeps 2.40–2.50 GHz in 1 MHz
steps, observes an imperfect path through a deterministic simulator, and derives a
frequency-indexed power correction. The canonical program is
[`examples/tx_path_characterization.hrf`](examples/tx_path_characterization.hrf).

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

## Reference documentation

- [Language reference](docs/language.md)
- [Architecture and execution model](docs/architecture.md)
- [Test suite organization](tests/README.md)
- [Apache 2.0 license](LICENSE)

The current implementation is simulator-only. It does not provide real hardware support, general-purpose RF
automation, or persisted/imported calibration.
