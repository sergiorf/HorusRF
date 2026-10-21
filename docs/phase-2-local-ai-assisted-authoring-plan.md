# HorusRF — Phase 2: Local AI-Assisted DSL Authoring

Status: Approved implementation plan. M0 through M5 were completed and verified
on 2026-10-20; M6 was completed and verified on 2026-10-21. M7 has not started.
Milestones must be implemented sequentially and marked complete only after
verification.

## Mission

Implement a local, AI-assisted authoring feature for HorusRF that allows users
to generate and modify RF characterization scripts using natural language.

The initial implementation will use Qwen through Ollama, while maintaining a
provider-neutral architecture.

The AI assistant must use the existing HorusRF language definition, compiler
diagnostics, and validation infrastructure to generate valid scripts and
automatically correct compilation errors.

The final experience should allow a user to:

1. Open HorusRF Studio.
2. Describe an RF characterization experiment in natural language.
3. Have a local LLM generate a HorusRF DSL script.
4. Automatically validate the script using the existing compiler.
5. Automatically repair compilation errors through a bounded feedback loop.
6. Review the proposed script and its changes.
7. Accept or reject the generated script.
8. Execute the accepted script through Studio's existing execution workflow.

The assistant must never execute an experiment automatically.

The priority is a reliable, maintainable implementation rather than an
elaborate agent framework.

## 1. Repository inspection and constraints

Before making modifications, inspect the current repository and read:

- `AGENTS.md`
- `README.md`
- `docs/architecture.md`
- `docs/language.md`
- `docs/project-roadmap.md`
- `docs/dsl-grammar-audit.md`
- `grammar/HorusRf.g4`
- `include/horusrf/frontend/language_services.hpp`
- `src/frontend/language_services.cpp`
- `src/cli/main.cpp`
- existing Studio sources and tests
- existing CLI and frontend tests
- relevant CMake configuration

Respect the existing architectural decisions.

HorusRF currently provides:

- a C++20 DSL compiler;
- an ANTLR-generated lexer/parser;
- AST and semantic analysis;
- strong domain quantity types;
- typed intermediate representation;
- runtime and simulator;
- command-line runner;
- Qt 6 Studio;
- a Qt-independent language frontend;
- structured compiler diagnostics; and
- language discovery and capability APIs.

Avoid duplicating existing functionality.

### Mandatory architectural constraints

- Do not modify the DSL grammar unless absolutely necessary.
- Do not embed LLM functionality into the parser, semantic analyzer, IR,
  runtime, or simulator.
- Do not introduce Qt dependencies into compiler components.
- Preserve existing public APIs and compatibility behavior.
- Preserve existing CLI behavior unless extending it through backward-compatible
  options.
- Preserve the Studio-to-runner process boundary.
- Preserve all existing test expectations.
- Use the repository's existing `build/` directory.
- Keep tests deterministic and independent of network services.
- Do not introduce speculative abstractions.

The previous roadmap treated AI-assisted authoring as deferred. This plan
authorizes planning and implementation of that feature as Phase 2. The roadmap
must reflect the new scope.

## 2. Explicit scope

### In scope

- Natural-language generation of complete HorusRF scripts
- Natural-language modification of existing scripts
- Ollama-based local LLM inference
- Provider-neutral LLM interface
- Compiler-driven script validation
- Structured JSON diagnostics
- Bounded automatic correction loop
- Language-aware prompt construction
- Script preview and user approval
- Qt Studio assistant panel
- Deterministic offline tests
- Documentation and benchmark scenarios

### Out of scope

Do not implement:

- MCP integration or MCP servers
- Multi-agent orchestration
- Autonomous experiment execution
- Autonomous hardware control
- Cloud authentication or subscriptions
- RAG frameworks or vector databases
- Model fine-tuning
- Persistent conversation databases
- Automatic modification of project files
- New RF measurement functionality
- General-purpose code execution
- Arbitrary shell or filesystem tools for the agent
- Automatic analysis and planning of follow-up experiments

The initial release is an AI-assisted DSL authoring application, not an
autonomous RF laboratory agent.

## 3. Target architecture

Implement the following logical workflow:

```text
              HorusRF Studio
                    |
             AI Assistant UI
                    |
              Agent Service
                    |
          +---------+---------+
          |                   |
     Prompt Builder      LLM Provider
          |                   |
  Language Capabilities   Ollama/Qwen
  Grammar Information         |
  Curated Examples            |
          |                   |
          +---------+---------+
                    |
               DSL Proposal
                    |
            Validation Service
                    |
           Existing HorusRF
         Parser / Semantic / IR
                    |
             +------+------+
             |             |
           Valid         Invalid
             |             |
       Script Preview  Diagnostics
             |             |
       User Approval   Repair Prompt
             |             |
       Apply to Editor <---+
             |
       Existing Run Action
```

Maintain separation between:

1. LLM transport and providers.
2. Prompt construction.
3. Agent orchestration.
4. Compiler validation.
5. Studio UI integration.

Avoid circular dependencies. The compiler must remain independent of the AI
system.

## 4. Milestone M0 — Architecture and contracts

Status: Completed on 2026-10-20. The implementation and decisions are recorded
in the [AI-assisted authoring architecture](ai-assisted-authoring-architecture.md).

### Objective

Establish a minimal architecture before implementing provider integrations or
modifying Studio.

### Tasks

#### M0.1 — Inspect existing contracts

Determine the correct reuse points for:

- `frontend::validate()`
- `frontend::get_diagnostics()`
- `frontend::get_language_spec()`
- `frontend::get_language_capabilities()`
- `ir::lower()`
- CLI `--check` functionality
- Studio's asynchronous process handling

Identify which existing components can be reused directly. Do not create
redundant compiler validation paths.

#### M0.2 — Define the agent components

Proposed logical modules:

```text
horusrf_ai
    LLM provider interface
    Ollama provider
    Prompt builder
    Agent session
    Repair loop
    Script proposal result

horusrf_agent_validation
    Compiler validation adapter
    Structured diagnostics
    Validation result serialization

horusrf_studio_ui
    Existing Studio components
    AI assistant panel
    Script preview and approval
```

These are conceptual boundaries, not mandatory target or directory names.
Prefer fewer targets if separation can be maintained cleanly.

Do not introduce an independent validation library unless it provides a real
dependency or reuse advantage.

#### M0.3 — Define stable contracts

Specify types for:

- generation request;
- script modification request;
- provider response;
- validation result;
- repair attempt;
- final script proposal;
- agent error; and
- cancellation.

Use strong C++ types and avoid unnecessary stringly typed internal APIs.

A script proposal should include:

- candidate source;
- validation status;
- diagnostics;
- human-readable explanation;
- attempt count;
- provider/model information; and
- whether the candidate is eligible to be applied.

The final proposal must be immutable from the agent's perspective once presented
for acceptance.

### Deliverables

- [x] Short architecture document
- [x] Component dependency diagram
- [x] Public interface proposal and implemented contract values
- [x] Updated Phase 2 roadmap
- [x] Initial deterministic unit tests for contracts

### Acceptance criteria

- [x] Compiler remains AI-free.
- [x] Dependencies follow existing project conventions.
- [x] Studio retains its existing process boundary.
- [x] No network dependency is introduced into the default CLI or compiler build.

Verification: `horusrf_ai_contract_tests` and the full unfiltered 25-test CTest
suite passed in the existing `build/` directory with the Release configuration
on 2026-10-20.

## 5. Milestone M1 — Machine-readable compiler validation

### Objective

Expose existing compiler diagnostics in a format suitable for an AI repair loop.
The repository already provides diagnostic categories, codes, messages, and
source spans. Do not redesign the diagnostic system unnecessarily.

Status: Completed and verified on 2026-10-20.

### Tasks

#### M1.1 — Introduce structured diagnostic serialization

Add a backward-compatible CLI validation option. Implemented interface:

```bash
horusrf-run experiment.hrf --check --diagnostics=json
```

This syntax extends the existing check form without changing its default behavior.

Response contract:

```json
{
  "schema": "horusrf.validation-result",
  "version": 1,
  "valid": false,
  "diagnostics": [
    {
      "category": "semantic",
      "code": "semantic.unknown_identifier",
      "message": "Unknown identifier",
      "range": {
        "start": {
          "line": 8,
          "column": 12,
          "offset": 143
        },
        "end": {
          "line": 8,
          "column": 20,
          "offset": 151
        }
      }
    }
  ]
}
```

Lines and columns are one-based; byte offsets are zero-based.

Requirements:

- Use existing diagnostic codes.
- Preserve existing source positions.
- Include parser and semantic diagnostics.
- Include IR lowering diagnostics.
- Preserve normal `--check` behavior.
- Do not execute experiments.
- Produce deterministic JSON.
- Specify output and exit-code behavior clearly.
- Ensure no diagnostic can corrupt the JSON stream.

Structured JSON uses stdout; unexpected process-level failures use stderr. Compiler
diagnostic generation is reused rather than duplicated.

#### M1.2 — Implement a validation adapter

The AI service should be able to request validation and obtain a structured
result.

For Studio, reuse the existing asynchronous runner process boundary.

If in-process validation is useful for standalone agent tests, evaluate whether
it can be implemented without introducing a second, divergent validation
definition.

The authoritative validation result must agree with the CLI's complete `--check`
behavior, including IR lowering.

#### M1.3 — Tests

Cover:

- valid scripts;
- syntax errors;
- unknown identifiers;
- invalid quantity types;
- invalid sweep steps;
- missing declarations;
- invalid calibration dimensions;
- invalid calibration correction;
- multiple semantic diagnostics;
- correct source ranges;
- JSON escaping; and
- backward compatibility with existing CLI output.

### Acceptance criteria

- [x] The agent can obtain machine-readable diagnostics.
- [x] Existing CLI consumers remain unaffected.
- [x] Validation cannot trigger simulator or hardware execution.
- [x] All existing compiler and CLI tests pass.

Implementation: the Qt-free `horusrf_ai_validation` target exposes complete
frontend-plus-lowering validation through `ai::validate_script()` and deterministic
schema-versioned serialization through `ai::write_validation_json()`. The CLI form
uses stdout for validation JSON, exits 0 for valid source and 4 for compiler-invalid
source, and reserves stderr for usage, I/O, and unexpected process failures.

Verification: `horusrf_ai_validation_tests`, structured and legacy CLI acceptance
coverage, and the full unfiltered 26-test CTest suite passed in the existing
`build/` directory with the Release configuration on 2026-10-20.

## 6. Milestone M2 — Provider-neutral LLM interface

### Objective

Introduce a small LLM abstraction, initially implemented through Ollama.

Status: Completed on 2026-10-20. The implementation uses a Qt-free blocking
provider call designed for invocation on a worker thread. Every request carries
an explicit timeout and the shared `std::stop_token`; Studio scheduling remains
M5 work.

### Tasks

#### M2.1 — Define the provider interface

Use an interface conceptually similar to:

```cpp
class LlmProvider {
public:
    virtual ~LlmProvider() = default;

    virtual LlmResponse generate(
        const LlmRequest& request
    ) = 0;
};
```

The final API must account for asynchronous invocation, cancellation, and
timeouts. Avoid blocking the Qt UI thread.

Do not prematurely introduce streaming, provider discovery frameworks, or
complex orchestration abstractions.

#### M2.2 — Ollama implementation

Support a configurable local Ollama endpoint.

Default:

```text
http://127.0.0.1:11434
```

Allow the user to select the installed model. Do not hardcode a particular Qwen
model name.

Use Ollama's supported chat API and structured output facilities where
practical.

Suggested configuration:

```text
Provider: Ollama
Endpoint: http://127.0.0.1:11434
Model: User-selected Qwen
Temperature: Low
Max output: Configurable
Timeout: Configurable
```

Keep provider-specific request and response formats inside the Ollama adapter.
Use existing HTTP/JSON facilities where suitable and avoid unnecessary
dependencies.

#### M2.3 — Provider errors

Handle:

- Ollama not running;
- invalid endpoint;
- model not installed;
- connection failure;
- request timeout;
- user cancellation;
- malformed JSON;
- empty model response;
- unexpected provider response; and
- model context or output limits.

Return structured errors to the agent service and understandable messages to
Studio. Never silently replace a failed local request with a cloud request.

#### M2.4 — Deterministic fake provider

Implement a fake LLM provider for tests. It must support scripted responses and
failure injection.

Tests must not require Ollama, Qwen, internet access, or a GPU.

### Acceptance criteria

- [x] Ollama can be used locally through its non-streaming `/api/chat` endpoint.
- [x] Model selection is configurable per request.
- [x] Provider failures are returned as values and cannot unwind into Studio.
- [x] Requests can be cancelled.
- [x] Tests are fully offline.
- [x] No LLM dependency enters compiler/runtime targets.

Implementation notes:

- `horusrf_ai` owns provider-neutral messages, requests, results, errors, and the
  `LlmProvider` interface.
- `horusrf_ai_ollama` owns the Ollama JSON protocol and a narrow native HTTP/1.x
  transport for local `http://` endpoints. It adds no third-party or Qt
  dependency and does not support cloud/TLS fallback.
- Ollama requests set `stream` to false, map maximum output to `num_predict`,
  and optionally carry a JSON-schema `format` value.
- `tests/ai/fake_provider.hpp` provides ordered responses, request capture, and
  failure injection for later prompt and state-machine tests.
- `horusrf_ai_provider_tests` verifies serialization, Unicode decoding, strict
  response handling, configuration validation, cancellation, provider error
  mapping, and fake-provider behavior without sockets or a model.

Verification: the focused AI contract, validation, and provider suites and the
full unfiltered 27-test CTest suite passed in the existing `build/` directory
with the Release configuration on 2026-10-20. A live-model smoke test was not
run because Ollama is not installed in the verification environment; it is not
part of deterministic CTest.

## 7. Milestone M3 — Language-aware prompt construction

Status: Completed and verified on 2026-10-20.

### Objective

Give the LLM accurate knowledge of the implemented HorusRF language. The model
must never assume unsupported RF capabilities are available.

### Tasks

#### M3.1 — Build language context

Reuse:

```cpp
frontend::get_language_spec()
frontend::get_language_capabilities()
```

Include relevant information about:

- available statements;
- supported measurements;
- sweep dimensions;
- units and quantity types;
- built-in functions;
- required declarations;
- statement ordering; and
- calibration constraints.

Supplement the metadata with the semantic rules that cannot be reconstructed
from token and capability lists alone. For example:

- Power and PowerDelta arithmetic;
- absolute versus relative temperature;
- allowed comparisons;
- calibration correction typing;
- required sweep ordering; and
- named measurement behavior.

Use the authoritative language documentation as a source during implementation.
Keep the final prompt concise enough for local models.

#### M3.2 — Curated examples

Create a small collection of known-valid examples demonstrating:

- basic frequency characterization;
- frequency and temperature sweeps;
- named power measurements;
- EVM measurements;
- frequency error;
- assertions;
- calibration surfaces; and
- existing-script modification.

Reuse repository examples where possible. Ensure examples are tested against the
real compiler. Do not introduce syntactically invalid examples as positive
demonstrations.

#### M3.3 — System prompt contract

The model must:

1. Generate only supported HorusRF syntax.
2. Preserve the user's explicitly requested RF values.
3. Respect dimensions and units.
4. Preserve unrelated sections when editing existing scripts.
5. Avoid inventing new language constructs.
6. Avoid silently changing calibration behavior.
7. Explain unsupported requirements.
8. Avoid claiming that a script has been validated before actual compiler
   validation.

Request a structured response containing:

```json
{
  "status": "proposal",
  "script": "...",
  "explanation": "...",
  "warnings": []
}
```

Validate this response against a defined schema before accepting it. A
successful provider response does not imply a valid HorusRF script.

#### M3.4 — Context management

For generation, provide:

- user request;
- relevant language capabilities;
- required language constraints; and
- selected examples.

For editing, additionally provide:

- current script;
- user's modification request; and
- instructions to preserve unrelated behavior.

For repair, additionally provide:

- previous candidate;
- compiler diagnostics; and
- original user requirements.

Do not resend unnecessarily large histories or unrelated documentation.

### Acceptance criteria

- Prompt construction is deterministic.
- Context derives from actual compiler capabilities.
- Example scripts compile successfully.
- Unsupported capabilities are explicitly handled.
- The model receives sufficient information to correct common errors.

## 8. Milestone M4 — Agent generation and repair loop

Status: Completed and verified on 2026-10-20.

### Objective

Implement a bounded agent loop that generates, validates, and repairs scripts.
This is the central feature of Phase 2.

### Tasks

#### M4.1 — Initial generation

Workflow:

```text
User request
    |
Build language context
    |
Call LLM
    |
Parse structured response
    |
Extract candidate DSL
    |
Validate with HorusRF
```

Reject empty or malformed responses. Never execute generated scripts during
validation.

#### M4.2 — Diagnostic-driven repair

If the candidate is invalid:

1. Collect structured compiler diagnostics.
2. Construct a repair request.
3. Include the current candidate source.
4. Include the original user requirements.
5. Ask the model to fix the reported errors without unnecessary changes.
6. Validate the new candidate.
7. Repeat within a fixed limit.

Default maximum: three repair attempts after initial generation. Make the limit
configurable internally.

Stop if:

- a valid script is obtained;
- the maximum number of attempts is reached;
- the user cancels;
- the provider fails;
- the model repeatedly returns an unchanged invalid script; or
- the model response is structurally unusable.

Retain the last candidate and its diagnostics for review.

#### M4.3 — Explicit agent states

Use a small state machine:

```text
Idle
Generating
Validating
Repairing
Completed
Failed
Cancelled
```

Avoid building a generic workflow engine.

#### M4.4 — Result contract

Return:

- generated source;
- compiler validation status;
- diagnostics;
- number of attempts;
- explanation;
- warnings; and
- error state where applicable.

Only valid scripts can be applied through the normal acceptance action. Invalid
candidates may remain visible for inspection but must be clearly identified.

#### M4.5 — Semantic fidelity

Compiler validity is necessary but insufficient. For example, these two
frequency sweeps may both compile:

```text
sweep frequency 2.4 GHz .. 2.5 GHz step 5 MHz
```

```text
sweep frequency 2.4 GHz .. 2.5 GHz step 10 MHz
```

But only one satisfies a request for 5 MHz steps.

For the first version:

- preserve original requirements throughout repair;
- avoid allowing the model to silently remove requirements to achieve
  compilation;
- check clearly extractable numeric requirements where practical;
- surface assumptions and omissions; and
- require user review.

Do not attempt to implement a universal natural-language semantic verifier.

### Acceptance criteria

- Valid scripts terminate successfully.
- Invalid scripts trigger repair.
- Every repaired candidate is revalidated.
- Retry limits are enforced.
- Cancellation is handled.
- No script executes automatically.
- Original user requirements remain available during every repair attempt.
- Tests cover success, failure, repeated errors, and cancellation.

Implemented result:

- `horusrf_ai_agent` owns the synchronous, Qt-free `AgentSession` and its
  explicit states. UI callers are required to invoke it on a worker thread.
- Generation and modification use the M3 prompt builder, the provider-neutral
  M2 interface, and complete M1 frontend-plus-IR validation.
- The default permits three repair attempts after initial generation. Provider
  failure, malformed or empty output, cancellation, unchanged invalid output,
  success, and exhaustion all terminate deterministically.
- `AgentResult` returns either an immutable valid `ScriptProposal` or an
  `AgentFailure`. Failures retain the last invalid proposal and compiler
  diagnostics whenever a candidate was already validated.
- Proposal warnings retain provider warnings and add conservative review notices
  when an explicit number-and-unit requirement is absent from the candidate.
- `horusrf_ai_agent_tests` uses only the scripted fake provider and real in-memory
  compiler validation. It covers first-pass success, repair success, exhaustion,
  unchanged output, provider and response failures, cancellation, invalid input,
  modification context preservation, and numeric fidelity warnings.

Verification: the focused AI suites and the full unfiltered 29-test CTest suite
passed in the existing `build/` directory with the Release configuration on
2026-10-20. No live model, network service, runtime, simulator, or hardware was
used.

## 9. Milestone M5 — Qt Studio integration

Status: Completed and verified on 2026-10-20.

### Objective

Expose the assistant through a usable, responsive desktop interface.

### Tasks

#### M5.1 — Assistant panel

Implement a dockable or resizable Qt panel. Suggested elements:

```text
AI Assistant
----------------------------------
Provider: Ollama
Model:    <installed model>

Request:
[ Describe your RF experiment... ]

[ Generate ] [ Cancel ]

Status: Validating...

Proposal:
[ Generated source preview ]

Diagnostics:
[ Compiler validation results ]

[ Apply to Editor ] [ Discard ]
----------------------------------
```

Reuse existing Studio styling and widget conventions. Avoid adding a new UI
framework.

#### M5.2 — Generation mode

Allow generation of a complete script from natural language. The generated
script must appear as a proposal, not replace the editor immediately.

#### M5.3 — Modification mode

Allow a user to request changes to the script currently open in Studio.

Example:

```text
Add a temperature sweep from -20°C
to 80°C using 20°C increments.
```

The assistant should receive the current source and produce a complete updated
candidate.

Show a diff or equivalent clear change preview. Ensure the user can distinguish
additions, removals, and modifications.

#### M5.4 — Approval behavior

Provide explicit actions to:

- apply proposal;
- discard proposal; and
- retry request.

Applying must be an intentional user action. Preserve the editor's undo/redo
behavior where possible. Do not overwrite saved files automatically.

If the editor changes while the agent is working, detect that the proposal is
based on an outdated document version. Require the user to review or regenerate
rather than silently overwriting newer edits.

#### M5.5 — Non-blocking UI

Use asynchronous operations for:

- Ollama requests;
- compiler validation subprocesses; and
- repair iterations.

The UI must remain responsive. Provide cancellation and clear state transitions.
Ensure callbacks cannot access destroyed widgets. Avoid introducing concurrent
agent sessions unless necessary.

#### M5.6 — Configuration

Provide a minimal configuration interface:

- Ollama endpoint;
- model selection; and
- request timeout.

Check provider availability and installed models through supported Ollama APIs.
Do not download models automatically. Provide clear instructions when Ollama or
a selected model is unavailable.

### Acceptance criteria

- Studio remains responsive during inference.
- Users can generate and edit scripts conversationally.
- Proposals are validated.
- Proposals can be reviewed before application.
- Cancellation works.
- Editor content is never replaced without approval.
- The existing script execution workflow is unchanged.

Implemented result:

- Studio owns a dockable Qt Widgets assistant with generate and current-document
  modification modes, provider configuration, status, complete proposal source,
  a line-oriented change view, compiler details, and explicit apply, discard,
  retry, and cancel controls.
- `AssistantService` runs the Qt-free `AgentSession` on one owned worker thread,
  forwards cooperative cancellation, prevents concurrent sessions, and returns
  completion to the GUI thread without callbacks into destroyed widgets.
- A request snapshots both source and document revision. Any subsequent editor
  change marks the proposal stale and disables Apply until the user retries.
- Applying a compiler-valid proposal is a single undoable editor edit. It neither
  saves the document nor invokes validation or execution actions.
- Endpoint, editable model selection, and bounded timeout are configurable. The
  Ollama adapter now discovers installed models through `/api/tags`; Studio gives
  explicit startup and `ollama pull` guidance without downloading models.
- Headless Studio tests use an injected fake backend to cover generation,
  modification context, preview, application and undo, stale protection,
  discard, cancellation, and model discovery. Provider tests cover deterministic
  `/api/tags` parsing through fake transport.

Verification: the focused provider and headless Studio suites and the full
unfiltered CTest suite passed in the existing `build/` directory with the Release
configuration on 2026-10-20. No live model, network service, runtime, simulator,
hardware, or wall-clock-dependent test was used.

## 10. Milestone M6 — Automated testing and quality evaluation

### Objective

Status: Completed and verified on 2026-10-21.

Demonstrate that the architecture works reliably, independently of the quality
of any individual LLM. Separate deterministic software tests from empirical
model evaluation.

### M6.1 — Unit tests

Test:

- request construction;
- response parsing;
- Ollama adapter behavior using fake transport;
- prompt generation;
- language capability serialization;
- agent state transitions;
- retry limits;
- cancellation;
- provider failures; and
- diagnostic handling.

### M6.2 — Compiler integration tests

Test the complete validation pipeline using known-valid and known-invalid
HorusRF scripts.

Test representative diagnostics for:

- syntax errors;
- unknown identifiers;
- invalid dimensions;
- incorrect units;
- invalid expressions;
- invalid calibrations; and
- missing declarations.

Verify that IR lowering is included in acceptance validation.

### M6.3 — Agent workflow tests

Use scripted fake model responses. Scenarios:

1. Valid script on first generation.
2. Invalid first candidate followed by valid repair.
3. Multiple failures followed by success.
4. Exhausted repair limit.
5. Malformed LLM output.
6. Provider unavailable.
7. Cancellation during generation.
8. Cancellation during repair.
9. Unsupported user request.
10. Valid candidate with an explicitly reported limitation.

All tests must run without network access or local inference.

### M6.4 — Studio tests

Test:

- generation request submission;
- preview presentation;
- apply and discard;
- existing-document editing;
- cancellation;
- stale document detection;
- provider failure messages; and
- preservation of existing editor behavior.

### M6.5 — Real-model benchmark

Create a benchmark suite containing approximately 40 natural-language tasks.

Suggested distribution:

| Category | Cases |
|---|---:|
| Basic characterization | 8 |
| Multidimensional sweeps | 6 |
| Named measurements | 6 |
| Assertions and qualification | 6 |
| Calibration | 5 |
| Existing-script modifications | 6 |
| Unsupported requests | 3 |

Run the benchmark manually or through an opt-in tool against a configured local
model. Do not include live-model execution in CTest.

Measure:

- first-attempt compilation success;
- compilation success after repair;
- average repair attempts;
- semantic requirement satisfaction;
- unsupported-request recognition;
- failure rate; and
- generation latency.

Record model identity, model version, inference settings, and machine
specifications.

Compilation success alone is not a sufficient quality metric. For the first
evaluation, compare a lightweight Qwen model with a larger Qwen coding model if
hardware permits. Do not hardcode benchmark expectations around a particular
model.

### Acceptance criteria

- Deterministic automated tests pass.
- Real-model benchmark results are reproducible.
- Model failures are reported transparently.
- No live-model dependency enters the normal test suite.
- No regression in existing compiler, CLI, or Studio functionality.

Implemented result:

- The later approved golden-dataset and multi-model evaluation plan refined the
  initial approximate 40-task suggestion into a versioned 17-case suite: 16
  supported requests and one explicit unsupported request. This smaller initial
  suite meets that plan's 15-case minimum and retains representative coverage.
- Offline tests validate the dataset, compiler integration, semantic comparison,
  provider behavior, bounded agent workflow, report serialization, model
  selection, persistence, and Studio behavior without Ollama or network access.
- The opt-in `horusrf-ai-eval` runner records compiler validity, semantic
  equivalence, unsupported recognition, repair behavior, failures, latency, and
  provider telemetry without executing generated experiments.
- A clean 3B/7B live comparison at Git revision `66e692f` produced a version-1
  report. The 3B model reached 10/17 final-valid and 5/16 semantic-match results;
  the 7B model reached 12/17 and 9/16 respectively. Neither model explicitly
  recognized the one unsupported request, and those limitations are retained
  transparently in the report.

Verification: the full unfiltered 32-test CTest suite passed in the existing
`build/` directory with the Release configuration on 2026-10-21. The clean live
report is `build/ai-evaluation/results-20261021-193036.json`; its revision,
dataset version, inference bounds, exact model identifiers, per-case outcomes,
and timing telemetry make the run repeatable. Live evaluation remains opt-in and
outside CTest.

## 11. Milestone M7 — Documentation and demonstration

### Objective

Deliver an understandable, demonstrable feature suitable for users and future
technical publication.

### Tasks

#### M7.1 — User documentation

Document:

- installing Ollama;
- selecting a compatible local model;
- configuring HorusRF Studio;
- generating a script;
- editing a script;
- interpreting validation errors;
- understanding model limitations; and
- reviewing proposals before execution.

#### M7.2 — Architecture documentation

Update `docs/architecture.md` to explain:

- LLM provider boundary;
- agent orchestration;
- validation feedback loop;
- Studio interaction;
- execution approval boundary; and
- dependency separation.

Update `docs/project-roadmap.md` with Phase 2 milestones and actual completion
states. Do not mark milestones as complete before implementation and
verification.

#### M7.3 — Demonstration scenarios

Provide three compelling demonstrations.

**Demo A — Generate an experiment**

User prompt:

```text
Characterize transmitter power between
2.4 GHz and 2.5 GHz using 5 MHz steps,
with a reference power of -10 dBm.
Generate the calibration correction.
```

Expected:

- complete HorusRF script;
- compiler validation;
- user approval; and
- successful simulation.

**Demo B — Modify an experiment**

User prompt:

```text
Add temperature characterization
from -20°C to 80°C in 20°C steps.
Also measure EVM and require it
to remain below 2%.
```

Expected:

- updated script;
- existing behavior preserved;
- correct calibration dimensions;
- valid qualification assertion; and
- preview of changes.

**Demo C — Repair an invalid script**

Supply a deliberately invalid HorusRF program.

Expected:

- initial compiler failure;
- structured diagnostic feedback;
- Qwen repair proposal;
- successful validation; and
- user-visible explanation.

#### M7.4 — Technical article material

Preserve enough engineering detail to support a future technical article
covering:

- DSLs as a reliable LLM target;
- local language models in desktop applications;
- compiler-assisted generation;
- diagnostic-driven repair;
- strong domain typing;
- deterministic validation;
- human approval boundaries; and
- empirical model benchmarks.

Do not introduce MCP into this phase.

## 12. Implementation priorities and sequencing

Use this order:

| Milestone | Description | Dependency |
|---|---|---|
| M0 | Architecture and contracts | None |
| M1 | Structured validation | M0 |
| M2 | Ollama integration | M0 |
| M3 | Prompt construction | M0, M2 |
| M4 | Agent repair loop | M1, M2, M3 |
| M5 | Studio integration | M4 |
| M6 | Tests and benchmarks | All relevant milestones |
| M7 | Documentation and demos | Verified implementation |

Testing should happen incrementally at every milestone rather than being
postponed until M6.

Keep milestones independently reviewable and avoid large, unrelated
modifications.

## 13. Definition of done

Phase 2 is complete when:

- [ ] A user can generate HorusRF DSL from natural language.
- [ ] Qwen runs locally through Ollama.
- [ ] No cloud account is required.
- [ ] The compiler provides structured validation feedback.
- [ ] Invalid scripts trigger a bounded repair loop.
- [ ] Final accepted proposals have passed full compiler validation.
- [ ] The user can modify existing scripts through natural language.
- [ ] Studio presents proposed modifications before applying them.
- [ ] The UI remains responsive during inference.
- [ ] Cancellation is supported.
- [ ] No experiment executes without explicit user action.
- [ ] Existing compiler and runtime boundaries are preserved.
- [ ] All automated tests are deterministic and offline.
- [ ] Existing test suites pass.
- [ ] Documentation reflects the implemented architecture.
- [ ] At least three end-to-end demonstration scenarios work.
- [ ] A reproducible local-model benchmark is available.

## 14. Working instructions for Codex

Implement incrementally and follow these rules:

1. Inspect existing code before making changes.
2. Prefer extending existing contracts over replacing them.
3. Avoid unnecessary abstractions.
4. Introduce no general-purpose agent framework.
5. Do not add MCP.
6. Do not modify the language grammar merely to simplify LLM generation.
7. Do not weaken compiler validation.
8. Do not hide model or validation failures.
9. Do not execute scripts automatically.
10. Keep compiler and runtime code completely independent of AI providers.
11. Preserve cross-platform compatibility, especially Windows and Linux.
12. Keep tests offline and deterministic.
13. Run focused tests after each meaningful change.
14. Run the full unfiltered CTest suite after cross-layer changes.
15. Update documentation together with behavior changes.
16. Summarize each milestone with files changed, architectural decisions, test
    results, and remaining limitations.

Where a design choice is unclear, choose the simplest solution consistent with
the existing architecture and document the decision.

Do not introduce dependencies or new application layers merely for possible
future extensibility.

### First execution task

Begin with **Milestone M0**.

Inspect the current repository and produce a concrete implementation proposal
identifying:

- existing components to reuse;
- necessary new interfaces and files;
- proposed target dependencies;
- CLI compatibility decisions;
- validation and LLM service boundaries;
- testing strategy; and
- potential architectural risks.

Then implement the smallest coherent first milestone, with tests and
documentation.

Proceed sequentially through later milestones, keeping each milestone
independently verifiable. Do not make speculative changes beyond the approved
Phase 2 scope.

**Primary engineering principle:**

The LLM interprets the user's intent and proposes HorusRF source. The existing
compiler remains authoritative on language correctness. The user remains
authoritative on applying changes and executing experiments.
