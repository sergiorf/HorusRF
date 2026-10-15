# HorusRF Studio v0.1 implementation plan

> **Archived as complete on 2026-10-16.** All three milestones were implemented
> and accepted in commits `a5e7221`, `3e4e3a1`, and `8758a57`. The Qt-disabled
> suite passed 17/17 tests and the Qt-enabled suite passed 18/18 tests at final
> acceptance. The instructions below are retained as the historical plan.

## Purpose and execution boundary

This document is the handoff plan for adding the optional HorusRF Studio desktop
application. The application is a lightweight C++20 and Qt 6 Widgets workbench
for editing, validating, running, and visualizing HorusRF experiments.

Implementation is divided into three sequential milestones. The next execution
task is **Milestone 1 only**. Complete, verify, and commit Milestone 1, then stop.
Do not begin Milestone 2 or 3 without a new instruction. Do not push commits.

Before making changes, reread `AGENTS.md`, `README.md`,
`docs/architecture.md`, `docs/language.md`, the root `CMakeLists.txt`, the CLI,
and CSV implementation. Use the existing `build/` directory exclusively.

## Repository baseline and environment

The repository was inspected before this plan was written:

- The Studio fits as an application/composition layer without changing the AST,
  semantic, IR, runtime, device, simulator, or output boundaries.
- Studio must communicate with `horusrf-run` through a process boundary. It must
  not link compiler, runtime, simulator, or output targets.
- The CLI currently accepts `horusrf-run <source.hrf> [--csv <output.csv>]`.
- Diagnostics use `path:line:column: code: message` and exit status 4.
- CSV output has seven fixed numeric columns documented in `README.md`.
- After rebuilding stale Debug binaries, the unfiltered baseline passed 17/17
  tests with:

  ```powershell
  cmake --build build --config Debug --parallel
  ctest --test-dir build -C Debug --output-on-failure
  ```

- The existing `build/` directory uses `Visual Studio 18 2026`.
- Qt 6.12.0 MSVC 2022 64-bit is installed at
  `C:\Qt\6.12.0\msvc2022_64` with Widgets, Charts, Test, debug libraries, and
  the Windows/offscreen platform plugins.
- Configure Qt discovery with
  `-DCMAKE_PREFIX_PATH=C:\Qt\6.12.0\msvc2022_64` when Studio is enabled.

The licensing documentation was committed separately as `8ad896e` (`added
license`). Preserve that commit, but update `README.md` and
`THIRD_PARTY_NOTICES.md` in Milestone 1 to describe the new mixed-license design
accurately. At the time this plan was persisted, only this plan file was
uncommitted.

## Architectural decisions

### Process boundary

Studio is a UI and process-orchestration application. It invokes the sibling
`horusrf-run` executable with `QProcess`, fixed program and argument strings, and
temporary files. It does not duplicate or directly call compiler/runtime logic.

### Plotting dependency

Use the Qt Charts Widgets API in Milestone 3. Add the Charts component only when
the plot is implemented; Milestones 1 and 2 must not acquire an unused plotting
dependency. Build the graph from `QChartView`, line series, numeric axes, and
the built-in legend for the measured, corrected, and optional reference data.

Qt Charts is deprecated as of Qt 6.10 and is available to open-source users
under GPLv3 rather than LGPLv3. Those tradeoffs are accepted for this bounded
prototype and article project. Do not add a speculative Qt Graphs abstraction
or migration layer.

### Qt licensing

Keep the HorusRF language, compiler, runtime, device, simulator, output, and CLI
source under Apache-2.0. License the separate `horusrf-studio` application and
its Studio-specific tests under GPL-3.0-only, preserving applicable
Apache-2.0 notices for any Apache-licensed material it incorporates. Add the
GPLv3 license text and clear file- or directory-level license notices so the
boundary is unambiguous in the mixed-license repository.

Link Studio dynamically to Qt Core, GUI, Widgets, Charts, and Test under their
applicable open-source terms. Keep `THIRD_PARTY_NOTICES.md` and the README
licensing section accurate. If Studio binaries are distributed, provide the
corresponding Studio source and build scripts, the required license notices,
and access to the corresponding Qt source as required by the applicable Qt and
GPLv3 terms. The independent `horusrf-run` process remains Apache-2.0.

### Canonical example

Bundle `examples/tx_path_characterization.hrf` into the Studio executable from
the existing file using Qt resources. Do not create a second source copy. At
startup, open it as an untitled, initially unmodified template. The first save
must use Save As so Studio cannot overwrite the repository example accidentally.

### Incremental user interface

Milestone 1 supplies working File/Edit actions and establishes the splitter
layout. Add Experiment actions and F5/F6 only in Milestone 2, when they work.
Results and diagnostics areas may be clear placeholders in Milestone 1, but must
not contain fake execution behavior.

## Milestone 1: application shell and DSL editor

Target commit:

```text
feat(studio): add Qt application and DSL editor
```

### Build integration

1. Add `HORUSRF_BUILD_STUDIO`, default `OFF`, to the root `CMakeLists.txt`.
2. Call `find_package(Qt6 REQUIRED COMPONENTS Widgets)` only inside the enabled
   branch. Discover `Qt6::Test` only when both Studio and tests are enabled.
3. Add `horusrf-studio` as a C++20 target. Use target-local automatic MOC and
   resource compilation rather than global Qt settings where practical.
4. Apply the existing warning policy.
5. Keep the default Qt-free configuration, build, and tests unchanged.
6. Add Studio tests to CTest only when Studio and `BUILD_TESTING` are enabled.
7. Add the GPL-3.0-only license text and mark Studio production and test sources
   with an unambiguous GPL-3.0-only notice. Do not change the license of the
   Apache-2.0 core or CLI.

Expected production files:

```text
src/studio/main.cpp
src/studio/main_window.hpp
src/studio/main_window.cpp
src/studio/dsl_editor.hpp
src/studio/dsl_editor.cpp
src/studio/syntax_highlighter.hpp
src/studio/syntax_highlighter.cpp
```

Add a `.qrc` file or use `qt_add_resources` to embed the canonical example from
its existing path. Add only the test files needed to prove nontrivial editor and
startup behavior; avoid creating a speculative application framework.

### Editor

Implement `DslEditor` as a `QPlainTextEdit` subclass with:

- a line-number gutter sized from the current block count;
- line-number painting synchronized with scrolling and resizing;
- current-line highlighting when the editor is writable;
- the system fixed-width font and readable tab stops;
- built-in Qt undo/redo behavior;
- normal selection, clipboard, and keyboard behavior.

Implement a `QSyntaxHighlighter` whose rules mirror the current grammar:

- keywords such as `characterize`, `reference`, `power`, `sweep`, `frequency`,
  `step`, `measure`, `derive`, `calibration`, `correction`, and `over`;
- decimal numbers, including the grammar-supported signed form through separate
  operator and number tokens;
- exact units: `Hz`, `kHz`, `MHz`, `GHz`, `dBm`, and `dB`;
- identifiers;
- `//` comments through the end of the line.

Apply the generic identifier style before keyword/unit styles, and apply comment
formatting last so comments take precedence. Do not change the HorusRF grammar.

### Main window and document lifecycle

Create a `QMainWindow` with nested resizable splitters:

- left: DSL editor;
- right: a clear results placeholder;
- bottom: a clear diagnostics placeholder.

Implement:

- File menu and toolbar: New, Open, Save, Save As, Exit;
- Edit menu: Undo and Redo;
- shortcuts: Ctrl+N, Ctrl+O, Ctrl+S, plus platform-standard undo/redo;
- `.hrf` open/save filters;
- UTF-8 file reading;
- atomic UTF-8 writing with `QSaveFile`;
- current file path and window-title updates;
- a modified marker using the document modification state;
- Save/Discard/Cancel prompts before New, Open, and close;
- clear file-I/O error dialogs;
- the embedded canonical template at startup.

An untouched startup template should close without prompting. Once edited, it
must prompt, and Save must route to Save As because it has no writable file path.

### Milestone 1 tests

Use Qt Test where it materially helps. Keep tests deterministic, offline, and
headless. Set `QT_QPA_PLATFORM=offscreen` for UI test execution. Cover at least:

- editor/highlighter behavior that can be asserted without visual snapshots;
- initial canonical template loading and initial unmodified state;
- document modification/title behavior where it can be tested without modal
  native dialogs.

Do not add fragile screenshot, timing, or native-dialog automation.

### Milestone 1 documentation

Update README with:

- the default Qt-free build remaining unchanged;
- the Windows Qt-enabled configure/build command using the installed MSVC path;
- a generator-neutral Linux example using `CMAKE_PREFIX_PATH` if needed;
- the generator-appropriate `horusrf-studio` launch path;
- a brief description of the editor-only Milestone 1 behavior.
- the GPL-3.0-only Studio and Apache-2.0 core/CLI license boundary.

Update `THIRD_PARTY_NOTICES.md` so it no longer says that Studio avoids GPL-only
Qt modules. Ensure all licensing text describes both the current milestone's
actual dependencies and the planned Qt Charts dependency without implying that
the Apache-2.0 core has been relicensed.

### Milestone 1 verification sequence

Use only `build/`; reconfigure it as needed.

1. Verify the default configuration remains Qt-independent:

   ```powershell
   cmake -S . -B build -DHORUSRF_BUILD_STUDIO=OFF
   cmake --build build --config Debug --parallel
   ctest --test-dir build -C Debug --output-on-failure
   ```

2. Configure and build Studio:

   ```powershell
   cmake -S . -B build `
     -DHORUSRF_BUILD_STUDIO=ON `
     -DCMAKE_PREFIX_PATH=C:\Qt\6.12.0\msvc2022_64
   cmake --build build --config Debug --parallel
   ctest --test-dir build -C Debug --output-on-failure
   ```

3. Launch `build\Debug\horusrf-studio.exe` and manually smoke-test:
   startup template, typing, highlighting, line numbers, undo/redo, New/Open,
   Save/Save As, modified prompts, splitter resizing, and clean exit.
4. Run `git diff --check` and inspect `git status --short`.
5. Commit exactly the completed Milestone 1 scope with the required commit message.
6. Report changed files, architectural decisions, actual test/build results,
   known limitations, and the commit hash. Then stop.

## Milestone 2: validation and execution

Do not start until explicitly instructed.

Target commit:

```text
feat(studio): integrate compiler and simulator
```

1. Extend the CLI with `horusrf-run <source.hrf> --check`.
   - Parse, semantically analyze, and lower without constructing devices or
     executing the simulator.
   - Make `--check` and `--csv` mutually exclusive.
   - Preserve all established forms, streams, side effects, and exit codes.
   - Update help, README, and CLI acceptance coverage.
2. Add `ExperimentRunner` using one asynchronous `QProcess`.
   - Resolve `horusrf-run` beside `horusrf-studio`.
   - Use `QTemporaryDir` for the current editor buffer and result CSV.
   - Always execute the current buffer, including unsaved edits.
   - Use a fixed executable and argument list, never a shell command.
3. Add Experiment/toolbar actions and F5/F6 shortcuts.
4. Reject or disable repeated requests while a process is active.
5. Keep stdout, stderr, and structured diagnostics distinct.
6. Parse `path:line:column: code: message` robustly, including Windows drive
   letters, and navigate clicked diagnostics to the editor location.
7. Handle missing executable, process startup errors, abnormal termination,
   invalid source, unexpected failure, and application close while active.
8. Test valid/invalid validation, current-buffer execution, diagnostic parsing,
   navigation, process failures, and preservation of the CLI contract.
9. Run the full CTest suite, commit, report, and stop.

## Milestone 3: CSV results and visualization

Do not start until explicitly instructed.

Target commit:

```text
feat(studio): add experiment visualization
```

1. Add a dedicated results model and strict parser for:

   ```text
   frequency_hz
   reference_power_dbm
   measured_power_dbm
   error_db
   correction_db
   corrected_power_dbm
   residual_error_db
   ```

2. Require the exact ordered header and exactly seven fields per data row.
3. Parse finite numeric values with the C locale. Reject malformed headers,
   fields, missing values, non-finite values, and empty result sets gracefully.
4. Do not add quoting/general CSV machinery, interpolation, or fabricated data.
5. Preserve the latest successful result when a later run fails.
6. Extend Studio's enabled CMake branch to discover the Qt Charts component and
   link `horusrf-studio` to `Qt6::Charts`. Keep Qt Charts absent from the default
   Qt-free build and from Studio builds before this milestone.
7. Implement the results view with the Qt Charts Widgets API:
   - frequency shown in GHz;
   - power shown in dBm;
   - measured and corrected curves;
   - toggleable reference curve;
   - axes, units, ticks, and legend;
   - correct handling of variable sample counts and degenerate ranges.
8. Display sample count and the CLI-reported error metrics separately from
   compiler diagnostics.
9. Test CSV parsing, successful process/result loading, malformed and empty CSV,
   retained prior results after failure, and nonvisual series/axis configuration.
   Do not add screenshot assertions.
10. Finish Windows/Linux documentation, deployment/licensing instructions, and
    GUI polish.
11. Run Qt-disabled and Qt-enabled builds and the full CTest suite, commit,
    report, and stop.

## Non-goals for v0.1

Do not add AI or LLM integration, agents, MCP, hardware backends, multi-document
projects, autocomplete, IntelliSense, debugging, plugins, cloud services,
persistent databases, or general-purpose graph editing. Avoid settings systems,
service locators, dependency-injection frameworks, and future-facing extension
points that are not required by the vertical slice.

## Final acceptance checks

After all three milestones:

- the default build works without Qt;
- the optional Studio target builds with Qt 6 Widgets and Charts;
- the canonical example opens, edits, validates, and runs from the current buffer;
- diagnostics are visible and navigable;
- actual CSV results produce measured/corrected/reference curves and metrics;
- failures do not replace the latest successful result;
- the repository and distributed artifacts clearly preserve the
  GPL-3.0-only Studio and Apache-2.0 core/CLI boundary;
- existing and new tests pass unfiltered;
- README contains accurate Windows/Linux build and launch instructions;
- the three milestone commits remain separate and no commits are pushed.
