# HorusRF DSL Frontend: Remaining Plan

Status: active implementation plan.

This document contains only unfinished work. Completed grammar formalization,
ANTLR adoption, parser parity, production cutover, and Windows verification are
recorded in the architecture documentation and ADRs rather than repeated here.

## Objective

Finish the reusable language-service boundary, remove the remaining duplication in
Studio syntax definitions, and expose deterministic compiler capabilities suitable
for future automation.

## Constraints

- Keep `grammar/HorusRf.g4` as the only authoritative concrete syntax.
- Preserve the existing AST, semantic, IR, runtime, device, simulator, and output
  boundaries.
- Keep the frontend independent of Qt, CLI execution, and hardware.
- Preserve existing DSL behavior, diagnostic codes, and source spans.
- Keep tests deterministic and offline.
- Do not expose ANTLR types through public HorusRF APIs.

## Pending ANTLR acceptance gate

Run the full unfiltered CTest suite on Linux using the existing `build/` directory.
Fix only genuine portability issues and record the result. The ANTLR migration is
complete when both the existing Windows Release result and this Linux result pass.

## Milestone 1 - Reusable language services

Add a Qt-free public API that parses and validates source without executing an
experiment.

Requirements:

1. Return structured results instead of requiring callers to catch parse
   exceptions.
2. Distinguish syntax diagnostics from semantic diagnostics.
3. Preserve stable diagnostic codes, messages, and precise source spans.
4. Preserve existing parser and semantic APIs as compatibility wrappers where
   practical.
5. Support multiple diagnostics only where recovery is conservative and does not
   produce misleading cascades.

Add focused tests for successful parsing and validation, syntax failures, semantic
failures, diagnostic classification, and source spans.

Acceptance criteria:

- A non-Qt caller can parse or validate a script without running it.
- Failure is available as machine-readable data.
- Existing CLI, Studio, and runtime behavior remains unchanged.

## Milestone 2 - Share language definitions with Studio

Remove the independently maintained keyword and unit lists from Studio's syntax
highlighter. Reuse compiler-owned token metadata without coupling Studio to ANTLR
or running a full parse for every highlighted block.

Requirements:

1. Preserve lightweight, responsive highlighting.
2. Keep the existing validation and navigable-diagnostic workflow working.
3. Add tests covering every grammar keyword and unit plus representative comments,
   numbers, operators, and ordinary identifiers.

Acceptance criteria:

- A syntax change no longer requires a separate Studio keyword or unit edit.
- Existing Studio editing, validation, and execution workflows still pass.

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

After the milestones above:

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
