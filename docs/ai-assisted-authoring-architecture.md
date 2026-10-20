# HorusRF AI-assisted authoring architecture

Status: Milestone M0 through M3 architecture, provider, and prompt contracts, implemented
on 2026-10-20.

## Purpose and authority

The AI-assisted authoring subsystem turns a natural-language instruction into a
HorusRF source proposal. It does not extend the language, decide that source is
correct, apply source to an editor, or execute an experiment.

Authority remains deliberately split:

1. The local language model interprets intent and proposes source.
2. The existing compiler decides whether the source is valid.
3. The user decides whether to apply the proposal and separately whether to run
   the resulting source.

Generated source is untrusted input. No successful model response may bypass
compiler validation, and no validation result authorizes execution.

## Existing components reused

| Existing contract | AI-assisted use |
|---|---|
| `frontend::validate()` | Authoritative syntax and semantic validation |
| `frontend::get_diagnostics()` | Diagnostics-only convenience API when no analyzed program is needed |
| `frontend::get_language_spec()` | Grammar-rule and fixed-token prompt context |
| `frontend::get_language_capabilities()` | Compiler-owned statements, operations, types, units, functions, and constraints |
| `ir::lower()` | Final lowering validation after successful frontend validation |
| `horusrf-run --check` | Process-level validation behavior retained by Studio |
| `studio::ExperimentRunner` | Existing asynchronous process and cancellation boundary for validation and execution |

Full compiler validation means `frontend::validate()` followed, on success, by
`ir::lower()`. This matches `horusrf-run --check`. A new parser, semantic path,
or validation rule set is not permitted in the AI subsystem.

## Components and dependency direction

```mermaid
flowchart TB
    Studio[HorusRF Studio]

    subgraph Authoring[AI-assisted authoring]
        Session[Agent session and bounded repair loop]
        Prompt[Prompt builder]
        Provider[Provider-neutral LLM interface]
        Ollama[Ollama provider]
        Contracts[horusrf_ai contracts]
        Adapter[Compiler validation adapter]
    end

    Frontend[horusrf_frontend]
    IR[horusrf_ir]
    Runner[horusrf-run]

    Studio --> Session
    Studio -. validation and execution process boundary .-> Runner
    Session --> Prompt
    Session --> Provider
    Session --> Adapter
    Session --> Contracts
    Provider --> Ollama
    Prompt --> Frontend
    Adapter --> Frontend
    Adapter --> IR
    Runner --> Frontend
    Runner --> IR
```

The diagram is the approved dependency direction for later milestones; M0 adds
only the dependency-free `horusrf_ai` contract target. Provider transport,
validation adaptation, orchestration, and Studio integration are introduced by
their respective later milestones.

The compiler never depends on `horusrf_ai`, an LLM provider, or Qt. The authoring
core never depends on Qt. Provider-specific request and JSON types stay behind
the provider implementation. Studio remains an application layer and continues
to use `horusrf-run` as a child process for its existing Validate and Run actions.

## Public contract

`include/horusrf/ai/contracts.hpp` defines the values shared by the later
provider, validation, orchestration, and UI layers:

- `GenerationRequest` carries a natural-language instruction.
- `ModificationRequest` keeps the current source distinct from the instruction.
- `ProviderResponse` carries provider text and provider/model identity without
  exposing transport-specific data.
- `ValidationDiagnostic` preserves category, stable code, message, and source
  span. Its categories cover syntax, semantic, and IR-lowering failures.
- `ValidationResult` is either valid with no diagnostics or invalid with at
  least one diagnostic.
- `RepairAttempt` records a positive attempt number, candidate source,
  validation result, and provider identity.
- `ScriptProposal` records the final candidate, validation result, explanation,
  positive attempt count, and provider identity. It exposes no mutators and is
  eligible to apply only when validation succeeded.
- `AgentError` distinguishes invalid requests, provider and validation failures,
  repair exhaustion, cancellation, and internal failures.
- `CancellationToken` uses the standard C++20 `std::stop_token`, avoiding a
  framework-specific cancellation contract.

`ScriptSource` and `NaturalLanguageInstruction` prevent source and instruction
text from being exchanged accidentally at interface boundaries. Provider and
model names remain strings because they are external identifiers.

M0 intentionally does not define the provider virtual interface or agent result
variant. Those interfaces require the error and transport behavior developed in
M2 and the state machine developed in M4. The M0 values are sufficient to make
those later APIs strongly typed without pre-empting their design.

## Provider contract and Ollama boundary

M2 adds `include/horusrf/ai/provider.hpp`. `LlmProvider` accepts a sequence of
typed system, user, and assistant messages plus a user-selected model,
temperature, output-token bound, timeout, and optional structured-output JSON
schema. It returns either the existing `ProviderResponse` or a structured
`ProviderError`. The operation is Qt-free and may block only up to its request
timeout; UI callers must schedule it on a worker thread. Cancellation uses the
M0 `std::stop_token` contract.

`horusrf_ai_ollama` implements that interface with Ollama's non-streaming local
`/api/chat` endpoint. Its HTTP request/response seam exists to permit protocol
tests without sockets. Native transport supports local `http://` endpoints,
bounded responses, connection/request/response timeouts, cancellation, content
length, and chunked responses. HTTPS and cloud fallback are deliberately absent.
Ollama JSON, HTTP types, and transport errors do not enter the provider-neutral
interface.

The implemented target dependencies are:

```text
horusrf_ai_ollama --> horusrf_ai
horusrf_ai_prompt --> horusrf_ai + horusrf_frontend
horusrf_ai_validation --> horusrf_ai + horusrf_frontend + horusrf_ir
```

None of these edges is reversed. Compiler, IR, runtime, device, simulator, CLI, and
Studio UI targets do not depend on `horusrf_ai_ollama`.

## Prompt construction and proposal response

M3 adds `include/horusrf/ai/prompt.hpp` and the `horusrf_ai_prompt` target. The
Qt-free `PromptBuilder` constructs provider-neutral `LlmRequest` values for
generation, modification, and compiler-directed repair. Model selection and
request bounds remain caller-configurable.

The system context is rebuilt deterministically from the authoritative
`grammar/HorusRf.g4`, `frontend::get_language_spec()`, and
`frontend::get_language_capabilities()`, and adds the semantic rules that cannot
be reconstructed from grammar and metadata alone. The grammar is embedded at
configure time, so prompt construction has no source-tree runtime dependency. A
small mode-selected set of few-shot examples is embedded from the checked-in
`examples/` sources and covers frequency and temperature
sweeps, all current measurement operations, assertions, calibration surfaces,
and preservation during edits. Every positive proposal is compiled in the M3
test suite.

The provider is asked for exactly one object containing `status`, `script`,
`explanation`, and `warnings`. `proposal_response_schema()` supplies the strict
provider schema, and `parse_proposal_response()` independently rejects malformed
JSON, missing or incorrectly typed fields, duplicate fields, and extra fields.
Successful parsing establishes only the response shape: the candidate remains
untrusted until `validate_script()` succeeds.

## CLI compatibility

M0 made no CLI changes. M1 added the explicitly selected
`--check --diagnostics=json` form while preserving existing `--check` text
diagnostics, streams, exit codes, and command syntax by default. Structured
validation emits schema `horusrf.validation-result` version 1 on stdout and uses
exit 0 for valid source or exit 4 for compiler-invalid source. The default CLI and
compiler build have no network, Ollama, or Qt dependency.

## Validation and service boundaries

The `horusrf_ai_validation` adapter depends on `horusrf_ai`, `horusrf_frontend`,
and `horusrf_ir`. It returns only the agent-facing validation values and does not
expose an AST, semantic model, or IR program to the provider. It validates in
memory for the agent repair loop and neither constructs devices nor calls runtime
code.

Studio's existing validation and run actions remain process based. The future
agent service may use the adapter for its bounded repair loop, but accepting a
proposal only changes editor text. Running remains an independent explicit user
action through `ExperimentRunner`.

## Test strategy

`horusrf_ai_contract_tests` is an offline unit suite at the contract layer. It
checks request separation, provider identity, validation invariants, repair
attempt numbering, proposal apply eligibility and immutability, typed errors,
and standard cancellation behavior. It does not start a model, access a network,
read the clock, or cross a process boundary.

M1 added adapter/compiler tests at their lowest owning layer, M2 added fake-provider
transport tests, and M3 added deterministic prompt, curated-example, and response
schema tests. Later milestones add deterministic state-machine tests in M4 and
headless Studio tests in M5. Real-model quality
measurement remains a separately invoked benchmark, never a deterministic CTest
dependency.

## Recorded risks and controls

| Risk | M0 control |
|---|---|
| Frontend-only checks disagree with CLI acceptance | Full validation explicitly includes IR lowering |
| A second parser or rule set drifts from the language | All validation and language discovery reuse compiler contracts |
| Provider details spread through the application | Provider response exposes only text and provider/model identity |
| Qt leaks into reusable authoring code | `horusrf_ai` uses only the C++ standard library |
| Invalid source is offered for application | Apply eligibility is derived from successful validation |
| A presented proposal is mutated by orchestration | Proposal state is private, const, and exposed read-only |
| Cancellation becomes provider-specific | The shared token is `std::stop_token` |
| AI authoring triggers hardware | Authoring has no runtime or device dependency; execution remains a separate action |

## Milestone boundaries

M0 contains shared contracts, architecture, dependency decisions, and
deterministic contract tests. M1 contains the compiler validation adapter and
structured diagnostic JSON. M2 contains provider contracts, Ollama access, and
deterministic provider tests. M3 contains prompt assembly, curated examples, the
proposal response schema, and deterministic parsing. These milestones do not
contain generation or repair orchestration, UI controls, editor changes, or
experiment execution. Those remain M4 through M7 work in the approved plan.
