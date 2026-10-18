# HorusRF Studio default experiment and showcase implementation plan

> **Archived as complete on 2026-10-18.** All five milestones were implemented.
> The final Studio-enabled suite passed 20/20 tests, the Studio-disabled suite
> passed 19/19 tests, and the instructions below are retained as the historical
> implementation plan.

## Purpose

Improve the Studio first-run experience using HorusRF capabilities that already
exist. A fresh Studio window should contain a realistic multidimensional
qualification experiment, and the first completed run should open a useful
visualization without requiring manual selector setup.

This is a Studio, examples, tests, and documentation change. It does not add DSL
syntax, change the versioned JSON contract, alter simulator behavior, or move
execution into the Qt process.

## Confirmed repository baseline

The repository already supplies the underlying showcase capabilities:

- `src/studio/main_window.cpp` initializes Qt resources, reads
  `:/examples/tx_path_characterization.hrf`, clears the document-modified flag,
  and leaves the template untitled. The same file builds the Examples menu and
  calls `confirmSaveIfModified()` before replacing the editor contents.
- `CMakeLists.txt` embeds `examples/tx_path_characterization.hrf` and
  `examples/studio_qualification.hrf` into the `horusrf_studio_ui` target with
  `qt_add_resources`, prefix `/examples`, and the source `examples/` directory as
  the resource base.
- `ExperimentRunner` invokes the sibling `horusrf-run` process. Run requests use
  `--json`, and an exit code 6 can still have a valid result through
  `producedResults()`.
- `ResultsModel` already parses schema version 1 structured results containing
  the characterization name, typed coordinates, named measurements, derived
  values, per-point assertions, qualification summaries, and calibration
  surfaces.
- `ResultsView` already has Points, Plot, Heatmap, Qualification, Calibration,
  and legacy-chart tabs. For structured results it currently clears and
  repopulates all selectors, chooses coordinate indexes 0 and 1 for a heatmap,
  chooses the first measurement as the scalar, and leaves the active tab at its
  previous numeric index. It does not preserve selections by name or explicitly
  choose the best tab for the result shape.
- `examples/multidimensional_check.hrf` and `examples/named_measurements.hrf`
  already execute 21 frequency values by 6 temperature values, or 126 points.
  `examples/qualified_measurements.hrf` demonstrates one-dimensional
  qualification and intentionally returns exit code 6 with the deterministic
  simulator.
- `examples/studio_qualification.hrf` already drives the complete structured
  Studio view at a smaller 5 by 6 grid. It remains a useful regression fixture,
  but it is close enough to the requested default that another family of nearly
  identical examples is unnecessary.
- `tests/studio/studio_tests.cpp` covers startup state, CLI process integration,
  strict JSON parsing, structured tables and visualizations, qualification source
  navigation, legacy CSV charts, and retention of the last valid result after a
  failed execution.
- `tests/integration/cli_acceptance.cmake` is the existing process-level location
  for `--check`, exit-code, JSON-shape, point-count, observation, qualification,
  and calibration assertions.
- The inspected `build/` cache is Studio-enabled with Qt 6.12 and the Visual
  Studio 18 2026 generator. It contains 20 registered tests. Implementation and
  verification must continue to reuse this directory.

The requested source is consistent with the implemented grammar and semantic
model: named `power`, `evm`, and `frequency_error` measurements, `abs()`, typed
comparisons, temperature sweeps, and multidimensional calibration are all
already supported. The inclusive ranges produce 21 frequency values and 6
temperature values. No grammar, parser, semantic, IR, runtime, device, simulator,
or JSON-schema change is planned.

## Architectural decisions

### Default example identity

Add `examples/studio_default.hrf` as a separate Studio-oriented example and keep
`examples/tx_path_characterization.hrf` byte-for-byte unchanged as the canonical
introductory and procedural-equivalence fixture. Also leave the other existing
examples unchanged; the new file carries the explanatory comments required for
the new showcase, while menu labels and status tips explain the reused fixtures.

Use the declarations from the task, including:

- characterization `tx_qualification`;
- frequency 2.40 GHz through 2.50 GHz in 5 MHz steps;
- temperature -20 degC through 80 degC in 20 deltaC steps;
- `output_power`, `modulation_evm`, and `carrier_error` measurements;
- derived `power_error`;
- three typed assertions;
- `tx_power` calibration over `frequency, temperature`.

Keep the supplied limits. Current simulator behavior is allowed to yield either a
passing qualification or a completed qualification failure. Tests must validate
the outcome structure rather than weaken limits to force one status.

### Result-shape and selection policy

Put one small, explicit policy in `ResultsView`; do not special-case the
`tx_qualification` characterization name. Represent selector choices by stable
dimension/scalar names and tabs by a semantic page identity, never by transient
combo-box or tab indexes.

Define a structured-result shape from:

- ordered coordinate names and value types;
- available numeric measurement names and types;
- available numeric derived-value names and types;
- whether assertions and calibrations are present.

Before replacing a structured model, snapshot the active tab and the plot axis,
plot scalar, fixed-coordinate selections, heatmap axes, and heatmap scalar. Treat
two runs as compatible when their dimension and numeric-scalar identities match;
point values and qualification outcomes may change without making the shape
incompatible.

After repopulating the widgets:

1. For a compatible repeated run, restore every still-valid selection by stable
   key and restore the user's active tab if that page remains usable.
2. If one prior selection disappeared, keep the other compatible choices and use
   the normal fallback only for the missing choice.
3. For a new or incompatible result with at least two dimensions and a numeric
   scalar, enable and select Heatmap. Prefer dimensions named `frequency` and
   `temperature`; otherwise use the first two distinct dimensions.
4. For a new or incompatible one-dimensional result with a numeric scalar,
   enable and select Plot and use its only dimension as the axis.
5. Prefer scalar `power_error` when present. Otherwise choose the first numeric
   derived value, then the first numeric measurement. This is based on result
   contents, not the characterization name.
6. If no useful plot or heatmap exists, select Points. Never make an empty or
   unsupported visualization the primary page.

Disable Heatmap when fewer than two distinct dimensions or no numeric scalar is
available. Disable Plot when no dimension or numeric scalar is available. Keep
legacy CSV behavior unchanged: legacy input continues to select the legacy chart
and does not participate in the structured-result policy.

Signals emitted while selector lists are being rebuilt should not trigger partial
rebuilds. Use `QSignalBlocker` or an equivalent small scoped guard, then rebuild
each dependent view once after the policy has been applied.

### Compact result summary

Use the existing area above the result tabs rather than adding a dashboard. Add a
compact structured-result summary containing only data already present in
`ResultsModel`:

- characterization name and point count;
- ordered sweep dimension names;
- available measurement operations/names;
- qualification state, with `not evaluated (no assertions)` for assertion-free
  programs;
- available calibration surface names.

Keep execution status separate from this summary. `setStatusMessage()` may report
a later validation or execution failure while the summary and tabs continue to
describe the last valid result. Use the existing wording distinction between
runner failure and `Experiment completed; qualification failed.` Do not label
calibration-fit metrics as validation, and do not compute additional statistics.

### Examples menu

Use existing files wherever they present a distinct progression. Register all
menu resources in the existing `qt_add_resources` block and order them as:

1. **Basic TX Calibration** — `tx_path_characterization.hrf`.
2. **TX Qualification** — `qualified_measurements.hrf`.
3. **Temperature Characterization** — `multidimensional_check.hrf`.
4. **Named RF Measurements** — `named_measurements.hrf` (power, EVM, and
   frequency error without qualification).
5. **Complete Studio Showcase** — `studio_default.hrf`.

Keep `studio_qualification.hrf` checked in and registered for compatibility and
existing regression use, but avoid presenting it as a sixth near-duplicate menu
choice. All menu actions continue through the existing unsaved-change
confirmation path and load an untitled, unmodified copy. Add stable object names
or resource data to the actions so widget tests can identify them without relying
on translated labels or menu position.

## Implementation milestones

### Milestone 1: add and validate the default experiment

1. Add `examples/studio_default.hrf` with concise comments describing the two
   sweeps, named observations, qualification, and calibration surface.
2. Add the file as an explicit process-level acceptance fixture in
   `CMakeLists.txt` and `tests/integration/cli_acceptance.cmake`.
3. Run `horusrf-run examples/studio_default.hrf --check`; require exit 0 and no
   diagnostics.
4. Run it with `--json`. Accept the CLI's established completed-result statuses
   (0 for qualified or 6 for qualification failure), require the JSON artifact,
   and inspect it rather than inferring success from the process code alone.
5. Assert the JSON contract has:
   - schema name `horusrf.characterization-result`, version 1;
   - characterization `tx_qualification`;
   - exactly 126 points in deterministic sweep order;
   - `frequency` and `temperature` coordinates on every point;
   - exactly three measurements per point with names/operations
     `output_power/power`, `modulation_evm/evm`, and
     `carrier_error/frequency_error`;
   - derived `power_error` on every point;
   - exactly three assertion outcomes on every point and three aggregate
     assertion summaries, each totaling 126 evaluations;
   - one `tx_power` calibration with dimensions ordered as `frequency`, then
     `temperature`, and exactly 126 entries.
6. Do not add CSV expectations for this fixture; the existing legacy CSV contract
   deliberately cannot represent multidimensional structured results.

Milestone gate: the example validates and produces the complete 21 by 6 JSON
shape through the real CLI before it is made the Studio default.

### Milestone 2: make the example the startup template and expand discovery

1. Add the new and reused menu fixtures to the existing resource list in
   `CMakeLists.txt`; retain both currently registered resources.
2. Change the constructor resource path in `MainWindow` to
   `:/examples/studio_default.hrf` without changing the surrounding document
   lifecycle: set editor text, clear modified state, and set an empty current file
   path.
3. Refactor the existing menu lambda only as far as needed to declare the ordered
   entries and attach stable test identifiers. Continue using
   `confirmSaveIfModified()`, `QFile`, `setModified(false)`, and
   `setCurrentFile({})`.
4. Do not run or validate on startup. F5 and F6 continue to operate on the current
   editor buffer through `ExperimentRunner`.

Milestone gate: a new window shows the new source as an untitled, unmodified
document; every menu resource loads; New/Open/Save/Save As behavior remains
unchanged.

### Milestone 3: implement visualization initialization and preservation

1. Add private value types/helpers in `results_view.hpp/.cpp` for result shape,
   saved selection, option lookup by item data, tab usability, and default scalar
   selection. Keep them local to Studio; no core model or JSON change is needed.
2. Capture selection state before replacing `model_`, rebuild selector contents
   with signals blocked, compare shapes, and apply the policy above.
3. Preserve fixed-coordinate choices for a compatible plot when the selected
   coordinate values still exist. If a value no longer exists, select the first
   available value for that fixed dimension.
4. Assign stable page identities/object names to tab pages as needed. Do not rely
   on tab indexes because the legacy page is conditionally enabled.
5. Enable/disable structured visualization pages according to their usability and
   select the policy result only for a first or incompatible structured shape.
6. Rebuild the points table, selected plot, heatmap, qualification tree, and
   calibration table once after selectors are final.
7. Preserve the existing behavior where selecting a heatmap cell navigates to the
   corresponding point; this deliberate interaction may still switch to Points.

Milestone gate: the default result opens a populated frequency-by-temperature
`power_error` heatmap, a one-dimensional qualified result opens a usable plot,
and compatible reruns do not override deliberate user choices.

### Milestone 4: add the compact result summary

1. Add one wrapping `QLabel` with a stable object name to the existing summary
   area above the tabs.
2. Build its structured text from the model schema, not from CLI stdout. Include
   no calculated statistics.
3. Leave the summary and latest valid model visible when a later run fails; update
   only the execution-status label for that failure.
4. Retain the current legacy sample-count and metrics presentation for CSV models.

Milestone gate: the summary distinguishes completed qualification failure from
execution failure and describes assertion-free experiments accurately.

### Milestone 5: tests and documentation

Extend `tests/studio/studio_tests.cpp` at the existing ResultsModel/ResultsView
boundary. Prefer small source/model fixtures and widget state assertions over
screenshots, geometry, colors, or arbitrary waits. Cover:

- startup contains `characterize tx_qualification`, has no current file, and is
  unmodified;
- the initial buffer validates through `ExperimentRunner`;
- the new default run is accepted even when its process status is 6, provided it
  produced valid results;
- the model exposes 126 points, three measurements per point, three assertion
  outcomes per point, and 126 calibration entries;
- the first default result selects Heatmap with `frequency` X,
  `temperature` Y, and `power_error` as the scalar;
- the heatmap is populated as 21 columns by 6 rows;
- a one-dimensional structured result selects Plot and does not select an
  unusable Heatmap;
- after deliberate changes to active tab, plot choices, fixed coordinates, and
  heatmap choices, a compatible rerun preserves all still-valid choices;
- an incompatible result falls back by the documented policy, and a removed
  scalar or coordinate value falls back without an empty view;
- assertion-free, passing, failing, and execution-failure summary/status text is
  unambiguous;
- each Examples action resolves a registered resource and loads it as an
  untitled, unmodified document;
- the existing retained-result-after-failure, qualification navigation, and
  legacy CSV tests continue to pass.

Keep process-level details in `tests/integration/cli_acceptance.cmake`; do not
duplicate the full JSON contract in fragile UI assertions.

Update the Studio section of `README.md` to document:

- the purpose and 126-point shape of the new startup experiment;
- F5 validation versus F6 execution;
- how to move from introductory to advanced examples with the Examples menu;
- the initial visualization policy and compatible-rerun preservation behavior;
- Heatmap as a two-dimensional scalar view;
- exit code 6 as completed execution with failed qualification, distinct from a
  compiler/runtime/process failure;
- retention of the last valid displayed result after a later failure.

Review `docs/language.md` for consistency, but do not edit the language reference
unless a factual correction or example link is needed. This work adds no language
feature. Do not modify archived plans.

## Expected file changes

Required:

- add `examples/studio_default.hrf`;
- modify `CMakeLists.txt`;
- modify `src/studio/main_window.cpp`;
- modify `src/studio/results_view.hpp`;
- modify `src/studio/results_view.cpp`;
- modify `tests/integration/cli_acceptance.cmake`;
- modify `tests/studio/studio_tests.cpp`;
- modify `README.md`.

Only if implementation proves necessary:

- modify `src/studio/main_window.hpp` for a small example-loading helper exposed
  to the implementation, not a new framework;
- modify `src/studio/results_model.hpp/.cpp` only if a read-only schema helper
  materially removes duplication. The parsed data needed for this plan is already
  available, so no model change is expected.

Explicitly unchanged:

- `examples/tx_path_characterization.hrf` and all existing example contents;
- `grammar/horusrf.ebnf`, lexer/parser, semantic analysis, IR, runtime, device,
  simulator, and output serialization;
- JSON schema version and fields;
- Studio-to-CLI process boundary.

## Verification sequence

Use only the existing `build/` directory. First build and test the Studio-enabled
configuration already represented by the inspected cache:

```powershell
cmake -S . -B build `
  -DHORUSRF_BUILD_STUDIO=ON `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.12.0\msvc2022_64
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

Run the focused process and Studio suites while iterating:

```powershell
ctest --test-dir build -C Debug --output-on-failure `
  -R "horusrf_cli_acceptance|horusrf_studio_tests"
```

Then verify that the optional UI has not leaked into the default build contract by
reconfiguring the same directory and running the full suite without Studio:

```powershell
cmake -S . -B build -DHORUSRF_BUILD_STUDIO=OFF
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

Reconfigure back to Studio-enabled if a manual smoke test is performed. Launch
the generator-appropriate `horusrf-studio` executable and check startup text,
F5, F6, first heatmap, each Examples entry, compatible rerun preservation,
qualification failure presentation, and save prompts. If Qt is unavailable in a
different implementation environment, run the Qt-disabled suite and report the
Studio build/test limitation explicitly.

Finish with:

```powershell
git diff --check
git status --short
```

## Definition of done

- A fresh Studio window contains the commented `studio_default.hrf` showcase as
  an untitled, unmodified buffer and does not execute it automatically.
- The checked-in example passes `--check` and produces a valid structured JSON
  result with 126 points, three named measurements per point, three qualification
  outcomes per point, and a 126-entry frequency/temperature calibration surface.
- Its first completed run, including a completed qualification failure, displays
  a populated frequency-by-temperature `power_error` heatmap.
- One-dimensional and legacy experiments still open useful plots.
- Compatible reruns preserve deliberate visualization and active-tab choices;
  incompatible results fall back predictably without selecting an empty view.
- The Examples menu provides a clear introductory-to-advanced progression and
  retains the existing unsaved-change guard.
- The compact summary describes the last valid result and does not confuse
  qualification, calibration fit, validation, or runner failure.
- The canonical TX example and all compiler/runtime/JSON contracts remain
  unchanged.
- Both applicable full CTest configurations pass, or any unavailable Qt
  configuration is reported accurately.

At implementation handoff, report the changed files, confirmed 21 by 6 point
count and measurement names, visualization policy, exact commands and results,
and any remaining limitations.
