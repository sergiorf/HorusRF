# HorusRF — DSL Frontend Refactoring and Grammar-Driven Architecture

Status: proposed implementation plan.

## Objective

Refactor the HorusRF language frontend to establish a single, authoritative grammar
and a maintainable compiler architecture suitable for future DSL extensions, Studio
integration, and AI-agent-assisted RF experiment generation.

The current project already has a formal EBNF grammar (`grammar/horusrf.ebnf`), a
handwritten C++ lexer and recursive-descent parser, an AST, semantic analysis, IR
lowering, and runtime execution.

However, the grammar is not currently the source of truth. Language syntax is
implemented independently in the lexer, parser, and Studio syntax highlighter.

The goal is to eliminate this duplication and establish a clean, extensible language
frontend.

**Important:** This is an architectural refactoring, not an opportunity to redesign
the DSL or implement the AI agent. Existing functionality and language compatibility
must be preserved.

## Architectural principles

1. Maintain a single authoritative language grammar.
2. Separate lexical analysis, parsing, AST construction, semantic validation, IR
   generation, and execution.
3. Preserve the existing AST and downstream compiler interfaces wherever practical.
4. Keep the compiler frontend independent of Qt, Studio, CLI, and hardware devices.
5. Provide stable, structured diagnostics with error codes, messages, and source
   locations.
6. Make the frontend reusable by Studio, CLI, tests, and future AI agents.
7. Avoid introducing unnecessary dependencies or complexity.
8. Maintain backward compatibility with existing valid HorusRF scripts.
9. Prefer incremental changes with tests and independently verifiable milestones.
10. Do not introduce AI-specific logic into the parser or semantic analyzer.

## Milestone 1 — Audit and formalize the grammar

Inspect:

- `grammar/horusrf.ebnf`
- `src/parser/lexer.cpp`
- `src/parser/parser.cpp`
- `include/horusrf/parser/`
- `include/horusrf/ast/`
- `src/semantic/`
- `src/studio/syntax_highlighter.cpp`
- Existing DSL examples and compiler tests

Tasks:

1. Compare the formal EBNF grammar against the actual accepted syntax.
2. Identify inconsistencies, undocumented constructs, implicit syntax rules, and
   keyword handling differences.
3. Document operator precedence and associativity.
4. Define lexical rules for identifiers, keywords, numbers, comments, units, and
   operators.
5. Clarify distinctions between reserved keywords and valid identifiers in
   expressions.
6. Clarify which constraints are syntactic and which belong to semantic analysis.
7. Update the grammar to accurately represent the intended language.
8. Establish a suite of valid and invalid grammar conformance examples.

Deliverables:

- Authoritative grammar specification.
- Documented lexical conventions.
- Grammar conformance tests.
- Short explanation of discrepancies identified and resolved.

Acceptance criteria:

- All existing valid DSL examples remain accepted.
- Previously supported language behavior is preserved.
- The grammar accurately documents the accepted syntax.
- No intentional language extensions are introduced.

## Milestone 2 — Evaluate grammar-driven parser implementation

Evaluate whether replacing the handwritten parser with a grammar-generated parser
would improve maintainability.

Consider primarily:

- ANTLR4 with C++ runtime.
- Retaining recursive descent with improved grammar conformance.
- Other lightweight C++-compatible alternatives only if they offer a clear
  advantage.

Evaluation criteria:

- Grammar maintained independently from parser implementation.
- C++20 and CMake integration.
- Windows and Linux compatibility.
- AST construction complexity.
- Error reporting and source positions.
- Build reproducibility.
- Dependencies and generated code maintenance.
- Long-term language extensibility.
- Overall implementation complexity.

Create a small proof of concept if useful.

Choose the simplest maintainable approach, not necessarily the most sophisticated.

If adopting a parser generator:

1. Make its grammar the authoritative syntax definition.
2. Generate lexer and parser components reproducibly.
3. Introduce an AST builder or visitor to construct the existing HorusRF AST.
4. Preserve existing source-span information.
5. Adapt diagnostics to preserve or improve current behavior.
6. Avoid leaking generated-parser types into public compiler interfaces.

If keeping recursive descent:

1. Maintain a canonical EBNF specification.
2. Establish automated conformance tests to detect implementation drift.
3. Centralize token and keyword metadata where practical.
4. Document the synchronization strategy.

Do not introduce multiple competing authoritative grammars.

Deliverables:

- Brief architecture decision record explaining the choice.
- Integrated parser implementation or verified existing parser.
- Passing parser tests.
- Updated CMake configuration where necessary.

Acceptance criteria:

- No regression in accepted DSL scripts.
- No regression in AST structure or semantic meaning.
- Invalid programs produce useful structured diagnostics.
- The parser remains independent of Studio and runtime execution.

## Milestone 3 — Introduce reusable language services

Create a reusable frontend API accessible independently of the CLI or Studio.

Conceptual services:

```cpp
ParseResult parse(std::string_view source);

ValidationResult validate(std::string_view source);

std::vector<Diagnostic> get_diagnostics(
    std::string_view source);
```

Design appropriate result types based on the existing code rather than blindly
using these signatures.

Requirements:

1. Separate parsing errors from semantic errors.
2. Use stable diagnostic codes.
3. Include source ranges with line and column information.
4. Allow consumers to distinguish syntax errors from semantic errors.
5. Avoid presenting exceptions as the primary public diagnostic interface.
6. Preserve compatibility with existing APIs where reasonable.
7. Ensure error results are suitable for machine consumption.
8. Avoid Qt dependencies.

Where practical, support multiple diagnostics per input, using conservative error
recovery that avoids misleading cascading errors.

Do not rewrite the semantic analyzer unless necessary for the API integration.

Deliverables:

- Reusable language frontend library/API.
- Structured diagnostic types.
- Unit tests for validation and error reporting.

Acceptance criteria:

- A caller can parse and validate a DSL script without running an experiment.
- Diagnostics have stable codes and precise source locations.
- Existing CLI and runtime behavior remains functional.

## Milestone 4 — Integrate shared language definitions into Studio

Refactor Studio to rely on the shared language frontend wherever appropriate.

Tasks:

1. Review independently maintained syntax highlighting rules.
2. Reuse shared keyword, token, and unit definitions where practical.
3. Integrate parser and semantic diagnostics into the editor.
4. Support highlighting of invalid syntax.
5. Preserve existing Studio editing and execution workflows.
6. Ensure editor functionality remains responsive.

Do not necessarily replace Qt's syntax highlighter with a full parser on every
keystroke.

Use lightweight lexical highlighting where appropriate and perform full validation
separately.

Architect the implementation so future features can include:

- Context-sensitive completion.
- Hover information.
- Syntax-aware navigation.
- Quick fixes.
- Documentation lookup.

These advanced features are not required in this milestone.

Acceptance criteria:

- Studio continues to load, edit, and execute DSL examples.
- Syntax definitions do not drift independently from the compiler.
- Errors can be displayed with meaningful source locations.
- No significant regression in editor responsiveness.

## Milestone 5 — Prepare interfaces for AI-agent integration

Prepare the language frontend for future AI-assisted generation of RF experiments.

Do not implement the LLM client, agent orchestration, or model provider integration
yet.

The objective is to make the existing compiler accessible through deterministic
tools.

Design or expose capabilities equivalent to:

```text
get_language_spec()
get_language_capabilities()
validate(source)
get_diagnostics(source)
format(source)
```

Formatting should be implemented only if it can be done safely and within reasonable
scope; otherwise document it as a follow-up.

Consider a machine-readable language capability description containing:

- Supported statements.
- Supported measurement operations.
- Supported sweep dimensions.
- Supported units.
- Supported functions.
- Relevant semantic constraints.

Avoid duplicating manually maintained grammar information in an AI-specific format.

Where possible, derive metadata from authoritative compiler definitions.

Define a conceptual future workflow:

```text
User natural-language request
            |
            v
       LLM agent
            |
            v
    Generated HorusRF DSL
            |
            v
      Parse + Validate
            |
       +----+----+
       |         |
     Valid     Invalid
       |         |
       v         v
   Preview    Diagnostics
       |         |
       |         v
       |      Agent repair
       |         |
       |         +----> Revalidate
       v
 User approval
       |
       v
     Execute
```

Requirements:

1. Treat AI-generated DSL as untrusted input.
2. Validate every generated program before execution.
3. Keep parsing and validation deterministic.
4. Keep hardware execution separate from language validation.
5. Require explicit user approval before executing AI-generated experiments.
6. Prepare for bounded repair iterations using structured diagnostics.
7. Do not grant the agent unrestricted execution or filesystem access.

Deliverables:

- Reusable deterministic validation interfaces.
- Machine-readable capability design.
- Short document describing the future agent integration architecture.

## Milestone 6 — Regression testing and documentation

Before declaring the refactoring complete:

1. Run the full existing test suite.
2. Run every supported DSL example through the compiler.
3. Validate existing IR generation and execution behavior.
4. Test malformed syntax and semantic errors.
5. Verify source ranges and diagnostic codes.
6. Test Studio when Qt dependencies are available.
7. Verify supported Windows and Linux build configurations.
8. Update architecture documentation.
9. Document how future DSL constructs should be introduced.
10. Verify that future language changes do not require manually updating multiple
    unrelated syntax definitions.

## Non-goals

Do not implement the following as part of this refactoring:

- New RF measurement operations.
- Additional sweep types.
- Heatmap or Smith chart visualization.
- Changes to the RF simulator.
- New hardware drivers.
- New DSL execution semantics.
- LLM integration.
- Agent orchestration.
- MCP server.
- Large-scale AST or IR redesign.
- Unrelated Studio UI changes.

These developments should build on the resulting language architecture later.

## Execution instructions for Codex

1. Inspect the repository thoroughly before modifying code.
2. Start with a short technical assessment of the current language frontend.
3. Identify concrete architectural weaknesses and inconsistencies.
4. Propose the implementation choice for Milestone 2 with clear tradeoffs.
5. Implement the milestones incrementally.
6. Preserve functionality and backward compatibility.
7. Add tests alongside implementation changes.
8. Run relevant tests after each milestone.
9. Avoid broad rewrites where smaller refactorings achieve the objective.
10. Document architectural decisions and remaining limitations.

At the end of each milestone, report:

- Files changed.
- Architectural decisions.
- Tests executed and results.
- Compatibility considerations.
- Remaining issues.

**Overall success criterion:** HorusRF has a well-defined, authoritative language
specification and a clean, reusable frontend that supports future DSL evolution,
sophisticated Studio tooling, and reliable AI-agent-assisted experiment generation
without duplicating parsing rules or compromising existing functionality.
