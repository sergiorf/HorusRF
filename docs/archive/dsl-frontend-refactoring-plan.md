# HorusRF DSL Frontend Refactoring Plan

> **Archived as complete on 2026-10-20.** Milestone 3 and its final verification
> were completed, and this document is retained as the historical refactoring plan.

This record described the final unfinished work after grammar formalization,
ANTLR adoption, parser parity, production cutover, reusable language services,
and Studio language-definition sharing. Milestone 3 and its final verification are
now complete; the resulting boundaries are recorded in the architecture and public
language documentation.

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

## Completion record

Milestone 3 added deterministic grammar/specification discovery, compiler-owned
capability metadata, and a diagnostics-only validation view. Metadata is derived
from generated ANTLR rule/token data, domain unit definitions, and semantic tables
shared with analysis. The automation safety and language-change workflows are
documented without adding execution or orchestration facilities.

The final Release verification passed the complete configured suites on Windows
(24 tests, including Studio) and Linux with GCC (23 tests, Qt-free), including all
parser, semantic, IR, CLI, runtime, integration, equivalence, and checked-in DSL
example boundaries.
