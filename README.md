# HorusRF

HorusRF is a C++20 declarative DSL demonstration for reproducible RF
characterization. It provides a typed compiler pipeline, deterministic simulated
equipment, a command-line runner, and an optional Qt Studio application. The
canonical experiment is
[`examples/tx_path_characterization.hrf`](examples/tx_path_characterization.hrf).

The implementation is simulator-only. It does not provide a hardware backend,
general-purpose RF automation, or calibration persistence/import.

## Build and test

Requirements:

- CMake 3.20 or newer;
- a C++20 compiler; and
- Java 11 or newer for the vendored ANTLR 4.13.2 generator.

The generator JAR and matching C++ runtime are vendored, so the build and tests do
not download dependencies. Use the repository's single `build/` directory:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

With a multi-config generator, use one configuration consistently:

```sh
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The runner is normally `build/horusrf-run` for a single-config generator and
`build/Debug/horusrf-run` for a multi-config Debug build. Append `.exe` on
Windows. Generated parser sources remain under `build/generated/`; to regenerate
them explicitly, build the `horusrf_antlr_grammar_generated` target.

## Run an experiment

For a multi-config Debug build:

```sh
build/Debug/horusrf-run examples/tx_path_characterization.hrf
build/Debug/horusrf-run examples/tx_path_characterization.hrf --check
build/Debug/horusrf-run examples/tx_path_characterization.hrf --csv output.csv
build/Debug/horusrf-run examples/named_measurements.hrf --json result.json
```

Use the generator-appropriate executable path described above. `--check` parses,
analyzes, and lowers a source without constructing devices or running the
experiment. A successful check is silent. `--check`, `--csv`, and `--json` are
mutually exclusive.

JSON schema version 1 (`horusrf.characterization-result`) is the complete result
format. It represents typed multidimensional coordinates, measurements, derived
values, qualification outcomes, calibration surfaces, and metrics. The fixed
seven-column CSV format is retained only for the original one-frequency,
`measure power` experiment shape.

CLI exit statuses are:

| Status | Meaning |
|---:|---|
| 0 | success |
| 2 | invalid command-line usage |
| 3 | file I/O failure |
| 4 | source, compiler, runtime, or output diagnostic |
| 5 | unexpected failure |
| 6 | execution completed, but an assertion failed |

Source diagnostics are written to standard error as
`path:line:column: diagnostic.code: message`. Run `horusrf-run --help` for the
accepted command forms. The [language reference](docs/language.md) defines the DSL
and result behavior in detail.

## Optional Studio application

The default build is Qt-free. Studio requires Qt 6 Widgets and Charts:

```sh
cmake -S . -B build -DHORUSRF_BUILD_STUDIO=ON \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/horusrf-studio
```

Omit `CMAKE_PREFIX_PATH` when CMake can already find Qt. With a multi-config
generator, add the selected configuration to the build and test commands and
launch `build/<Configuration>/horusrf-studio`.

On Windows, the development script configures, builds, tests, and launches Studio:

```powershell
.\scripts\run-studio.cmd
```

It defaults to a Debug build and `C:\Qt\6.12.0\msvc2022_64`. Use `-QtPath` and
`-Configuration` to override those values, or `-NoLaunch` to stop after testing.

Studio edits and validates HorusRF source, invokes the sibling `horusrf-run`
process for execution, and presents JSON results as tables, plots, heatmaps,
qualification summaries, and calibration entries. It links the parser only for
compiler-owned highlighting metadata; validation and execution remain behind the
CLI process boundary. It does not link semantic, IR, runtime, device, simulator,
or output targets. Keep both executables together when deploying it. See
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for Qt deployment and license
obligations.

## Library-facing frontend

Qt-free callers can include `horusrf/frontend/language_services.hpp` and link
`horusrf_frontend`:

- `frontend::parse(source)` returns an AST or syntax diagnostics;
- `frontend::validate(source)` returns an analyzed program or syntax/semantic
  diagnostics;
- `frontend::get_diagnostics(source)` returns the validation diagnostics; and
- `frontend::get_language_spec()` and `get_language_capabilities()` expose
  deterministic compiler-owned language metadata.

These operations do not lower or execute a program, construct a device, or access
hardware. Applications must treat generated source as untrusted, validate it, and
keep execution behind a separate explicit approval. HorusRF contains no LLM
client, agent orchestrator, MCP server, or unrestricted execution facility.

## Documentation

- [Language reference](docs/language.md) — implemented syntax and behavior
- [Architecture](docs/architecture.md) — layers, dependencies, and ownership
- [DSL evolution contract](docs/dsl-evolution.md) — compatibility decisions behind
  the generalized language
- [Project roadmap](docs/project-roadmap.md) — completed work and deferred ideas
- [Grammar audit](docs/dsl-grammar-audit.md) — syntax/semantic boundary and
  conformance coverage
- [Test organization](tests/README.md) — suites, labels, and commands

When changing syntax, update
[`grammar/HorusRf.g4`](grammar/HorusRf.g4), frontend behavior, the language
reference, and the lowest-layer tests together. Generated ANTLR sources are build
artifacts and must not be committed.

## Licensing

The language, compiler, runtime, devices, simulator, output libraries, examples,
and `horusrf-run` are licensed under Apache License 2.0. The separate
`horusrf-studio` application and its Studio-specific tests are GPL-3.0-only. Qt
remains under its own terms. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
for details.
