# HorusRF DSL Frontend: Remaining Plan

Status: active implementation plan.

This document contains only unfinished work. Completed grammar formalization,
ANTLR adoption, parser parity, production cutover, cross-platform acceptance,
reusable language services, and Studio language-definition sharing are recorded
in the architecture documentation and ADRs rather than repeated here.

## Objective

Expose deterministic compiler capabilities suitable for future automation.

## Constraints

- Keep `grammar/HorusRf.g4` as the only authoritative concrete syntax.
- Preserve the existing AST, semantic, IR, runtime, device, simulator, and output
  boundaries.
- Keep the frontend independent of Qt, CLI execution, and hardware.
- Preserve existing DSL behavior, diagnostic codes, and source spans.
- Keep tests deterministic and offline.
- Do not expose ANTLR types through public HorusRF APIs.

## Milestone 3 - Deterministic tooling for future automation

Expose or design stable, deterministic capabilities equivalent to:

```text
get_language_spec()
get_language_capabilities()
validate(source)
get_diagnostics(source)
```

The capability description should cover supported statements, measurements, sweep
dimensions, units, functions, and important semantic constraints. Derive metadata
from authoritative compiler definitions where possible; do not create another
manually maintained grammar. Treat formatting as a separate follow-up unless it can
be implemented safely within this milestone.

Document the future automation boundary:

- generated DSL is untrusted input;
- every program is validated before execution;
- hardware execution remains separate from validation; and
- execution requires explicit user approval.

Do not implement an LLM client, agent orchestration, MCP server, or unrestricted
execution.

Acceptance criteria:

- Tools can discover supported language capabilities and obtain structured
  validation results.
- Capability data does not duplicate concrete syntax manually.

## Final verification and documentation

After the milestone above:

1. Run the full unfiltered CTest suite on supported Windows and Linux toolchains.
2. Run every checked-in DSL example through the compiler.
3. Verify parser, semantic, IR, CLI, runtime, and Studio behavior at their existing
   test boundaries.
4. Update public API, architecture, language-development, and Studio documentation.
5. Document the single workflow for adding syntax: update the grammar, frontend
   adapters or metadata, public documentation, and tests in the same change.

The refactoring is complete when the reusable frontend and Studio consume shared
language definitions, deterministic tooling is available, and all cross-platform
verification passes.

## Execution policy

Implement one milestone at a time. Add tests at the lowest layer that proves each
behavior, run the relevant focused suites, then run the full unfiltered suite after
cross-layer changes. Stop and report results before beginning the next milestone.
