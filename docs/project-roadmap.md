# HorusRF project roadmap

Status: Phase 1 completed on 2026-10-18. Phase 2 local AI-assisted DSL
authoring is in progress; M0 through M2 are complete.

HorusRF is a focused C++20 DSL demonstration. Current behavior belongs in the
[language reference](language.md), and dependency/ownership rules belong in the
[architecture](architecture.md). The accepted design and compatibility decisions
that guided Phase 1 are retained in the
[DSL evolution contract](dsl-evolution.md). Historical execution plans are under
[`archive/`](archive/) and remain unchanged.

## Completed foundation

Phase 1 generalized the original single-frequency power-characterization path
without changing its compatibility contract:

| Milestone | Completed | Result |
|---|---|---|
| M0 | 2026-10-17 | Language, runtime, result, and compatibility contracts |
| M1 | 2026-10-17 | Typed frequency and temperature sweep compilation |
| M2 | 2026-10-17 | Deterministic multidimensional runtime and environmental control |
| M3 | 2026-10-17 | Named power, EVM, and frequency-error measurements |
| M4 | 2026-10-18 | Typed assertions and qualification reporting |
| M5 | 2026-10-18 | Calibration surfaces and versioned JSON results |
| M6 | 2026-10-18 | Studio JSON ingestion and multidimensional presentation |

The implementation now includes the generated ANTLR frontend, AST, semantic
analysis, typed IR, runtime, explicit device contracts, deterministic simulator,
CLI, output serializers, and optional Qt Studio. Studio remains behind a process
boundary and invokes the CLI asynchronously.

## Continuing constraints

Future work must:

- keep `grammar/HorusRf.g4`, frontend behavior, `language.md`, and tests aligned;
- preserve the AST, semantic, IR, runtime, device, simulator, output, CLI, and
  Studio boundaries in `architecture.md`;
- retain strong domain quantities at public and cross-layer boundaries;
- keep runtime dependent on public device contracts, never simulator internals;
- preserve independent procedural and declarative orchestration;
- keep Studio behind the CLI process boundary;
- preserve stable diagnostics, CLI behavior, and the exact legacy CSV schema for
  the original experiment shape;
- keep tests deterministic, offline, and independent of real hardware; and
- avoid speculative frameworks, dependencies, and extension scaffolding.

## Approved Phase 2

### Local AI-assisted DSL authoring

Phase 2 adds local, provider-neutral AI-assisted authoring to Studio, initially
using Qwen through Ollama. The compiler remains authoritative, generated or
modified source is presented as a proposal, and applying or executing it always
requires an explicit user action. The implementation is divided into milestones
M0 through M7. M0, architecture and contracts, and M1, machine-readable compiler
validation, and M2, the provider-neutral Ollama interface, were completed on
2026-10-20; M3 through M7 have not started.

M0 introduced the dependency-free `horusrf_ai` contract target, deterministic
contract tests, and the documented component, validation, CLI compatibility, and
safety boundaries. See the
[AI-assisted authoring architecture](ai-assisted-authoring-architecture.md) for
the milestone decisions and public contract.

M1 introduced the Qt-free `horusrf_ai_validation` adapter, deterministic
`horusrf.validation-result` version 1 serialization, and the backward-compatible
`horusrf-run <source> --check --diagnostics=json` process interface. Validation
reuses the frontend and IR lowerer and has no runtime or device dependency.

M2 introduced the Qt-free `LlmProvider` interface, cancellable and bounded
requests, structured provider errors, a configurable local HTTP Ollama adapter,
and deterministic fake-provider and fake-transport coverage. Provider-specific
HTTP and JSON behavior remains isolated from compiler, runtime, and Studio
targets.

The complete approved scope, sequencing, acceptance criteria, and safety
boundaries are defined in the
[Phase 2 local AI-assisted authoring plan](phase-2-local-ai-assisted-authoring-plan.md).

## Deferred directions

The following are possible directions only. Each requires a separate design and
explicit approval before implementation.

### Advanced RF measurements

Potential additions include strong complex-domain quantities, S-parameters, port
validation, and Smith-chart presentation. Device capabilities and deterministic
simulation must remain explicit, and tests must not depend on a simulator's private
transfer function.

### Additional examples

Possible examples include filter response, amplifier gain/compression, antenna
matching, temperature calibration, and passband/stopband qualification. Language
extensions require a concrete use case and the normal grammar-to-documentation
workflow.

### Agent-assisted experimentation

An assistant might eventually propose follow-up experiments from completed
results. Such a workflow requires a separately designed safety and approval model:
every revision must be validated and shown to the user, and every execution must
be explicitly approved. Real hardware would require independent limits and
authorization.

## Change protocol

For an approved increment:

1. Re-inspect the affected contracts and record any design decision.
2. Add deterministic tests at the lowest useful layer.
3. Implement the smallest coherent change.
4. Run focused tests, then the full unfiltered CTest suite for cross-layer work.
5. Update grammar, language/architecture documentation, examples, and CLI
   documentation in the same change.
6. Review compatibility fixtures and the complete diff.

A change is complete only when its diagnostics, compatibility behavior,
documentation, and acceptance tests agree.
