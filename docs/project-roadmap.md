# HorusRF project roadmap

Status: Phase 1 completed on 2026-10-18. Later phases are deferred ideas, not
approved implementation commitments.

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

## Deferred directions

The following are possible directions only. Each requires a separate design and
explicit approval before implementation.

### Assisted authoring and analysis

Potential work includes a provider-neutral Studio integration, natural-language
source proposals, bounded diagnostic-driven repair, and explanations grounded in
versioned result JSON. Compiler validation remains authoritative, generated source
must be reviewed, and execution requires a separate explicit user action. Core
compiler and runtime targets remain AI-free; tests use deterministic fakes and no
network.

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
