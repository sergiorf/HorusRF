# ADR 0001: Retain the recursive-descent parser

- Status: Superseded by [ADR 0002](0002-adopt-antlr-generated-parser.md); retained as an evaluation record
- Date: 2026-10-19
- Plan: DSL frontend refactoring, Milestone 2

The sequential
[ANTLR migration plan](../dsl-frontend-refactoring-plan.md#antlr-migration-plan)
subsequently passed its regression and parity gates. ADR 0002 records the completed
production cutover and supersedes this decision.

## Context

HorusRF had a compact concrete syntax, an authoritative EBNF specification, a
handwritten lexer and recursive-descent parser, and an AST whose source spans and
structured diagnostics are public compiler behavior. Milestone 2 evaluated
whether a grammar-generated implementation would reduce long-term maintenance.

The principal alternative was ANTLR 4 with its C++ runtime. The evaluation used
the official ANTLR 4.13.2 tool and matching C++ runtime with Java 17. A disposable
combined grammar covered the complete HorusRF syntax. Its generated C++ parser
was compiled on Windows and exercised against all 12 checked-in DSL examples plus
the focused accepted and rejected grammar-conformance cases: 39 cases in total.
All cases agreed with the handwritten parser. The two sampled error locations also
agreed: an unfinished block reported line 1, column 17, and a missing quantity unit
reported line 1, column 39.

The proof of concept established that ANTLR can recognize the language, but did
not show a maintenance advantage for this frontend:

- The generated lexer/parser/visitor artifacts comprised 12 files and about
  120 KB before adding an AST builder. The C++ runtime static library produced by
  this toolchain was about 22.7 MB; these sizes are measurements, not shipped-size
  guarantees.
- The generated visitor does not construct the existing HorusRF AST. A custom
  visitor of roughly the same conceptual shape as the current parser would still
  be required to preserve AST structure and byte-offset spans.
- ANTLR's default recovery and messages do not provide HorusRF's stable diagnostic
  codes. A custom error listener and recovery policy would be required to preserve
  first-error behavior and the existing diagnostic taxonomy.
- Reproducible generation adds a Java tool plus a version-matched C++ runtime and
  requires a policy for generated sources. ANTLR recommends regenerating for each
  minor release because minor versions may be incompatible.
- The Windows proof of concept required build integration work for C-runtime
  selection and a local missing-`<chrono>` compatibility fix with the installed
  Visual Studio 2026 toolchain. ANTLR supports Windows and Linux, but this adds a
  dependency compatibility surface absent from the current parser.

PEG and parser-combinator alternatives were not prototyped. For this grammar they
would add a dependency or framework while retaining the same AST and diagnostic
adaptation work, so they offer no clear advantage over either evaluated approach.

## Decision

Retain the handwritten lexer and recursive-descent parser.

At the time of this decision, the EBNF remained the single authoritative syntax
specification, with no checked-in ANTLR grammar or generated parser. The implementation uses
one internal word-token table for keyword/unit classification, display spelling,
and unit recognition, and one parser predicate for contextual expression names.
This removes the practical metadata duplication identified during the evaluation
without introducing a new public language-definition layer.

The synchronization strategy is:

1. Every syntax change updates the authoritative grammar, lexer/parser behavior,
   `docs/language.md`, and parser tests in the same change.
2. `grammar_conformance_tests.cpp` parses every checked-in `.hrf` file, including
   examples that are intentionally invalid only at the semantic layer.
3. Focused accepted and rejected cases cover lexical boundaries, every unit,
   contextual names, statement forms, precedence, associativity, calls, and
   comparison cardinality.
4. Lexer tests verify that every reserved word and word unit maps to its canonical
   token spelling; parser tests separately verify AST shape, source spans, and all
   stable diagnostic codes.
5. Cross-layer syntax changes run the full unfiltered CTest suite.

## Consequences

The parser target remains dependency-free, C++20/CMake builds do not require Java
or generated-source maintenance, public parser interfaces remain unchanged, and
Studio/runtime boundaries are unaffected. The cost is continued manual grammar
implementation. The conformance suite and centralized metadata are the controls
against specification drift.

Reconsider a generator if the grammar grows enough that predictive parsing or
precedence maintenance becomes error-prone, if multiple independent consumers
need machine-readable syntax, or if conformance failures repeatedly escape the
existing tests. Any future evaluation must still preserve the AST boundary,
source spans, diagnostic codes, offline reproducibility, and a single
authoritative grammar.
