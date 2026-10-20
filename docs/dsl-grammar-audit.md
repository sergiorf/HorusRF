# DSL grammar audit

Status: completed on 2026-10-19; updated after the generated-parser cutover.

At the time of this audit, a manually maintained EBNF specification was the
authoritative concrete syntax. Migration milestone 3 replaced it with
[`grammar/HorusRf.g4`](../grammar/HorusRf.g4). This audit compared the syntax with the
handwritten lexer and parser, AST, semantic analyzer, Studio highlighter, shipped
DSL examples, and compiler tests. Milestone 1 changes only the language
specification, documentation, and conformance coverage; the compiler pipeline and
accepted language are unchanged.

## Resolved discrepancies

- The old grammar named `end_of_file` but did not define lexical conventions. The
  grammar now records identifier characters, exact/case-sensitive keywords and
  units, decimal-number forms, ignored whitespace, line comments, token priority,
  and the required number/unit token boundary.
- The old grammar used `identifier` for expression references even though the
  parser has always admitted the keyword tokens `power`, `frequency`, and
  `temperature` in expression-name positions. The `expression_name` production
  now documents that compatibility rule while retaining strict identifiers for
  characterization, derived-value, and calibration declarations.
- The old grammar restricted named measurement operations to `power`, `evm`, and
  `frequency_error`. The parser intentionally accepts a name there and semantic
  analysis reports `semantic.unknown_measurement_operation`. The grammar now
  reflects that boundary. The three supported operations remain unchanged.
- Calibration indexes were described as generic identifiers (and an earlier
  design document described only sweep dimensions), while the parser accepts an
  `expression_name` and semantic analysis checks that each name is an active
  sweep. The grammar now matches the parser.
- Operator structure was present but its contract was implicit. The language
  reference now states precedence, left associativity of `+`/`-`, the single
  prefix-minus rule, and non-associative comparisons.
- The accepted evolution document duplicated a proposed grammar and called
  `evm`, `frequency_error`, and `assert` keywords. Only `assert` is a lexer keyword;
  operation and function spellings are ordinary identifiers resolved semantically.
  That document now links to the single authoritative grammar.

Studio does not treat supported operation names as lexer keywords: they remain
ordinary identifiers. Its keyword, unit, operator, and punctuation rules now come
from compiler-owned fixed-token metadata rather than a parallel syntax list.

## Syntax and semantic boundary

The parser owns token formation, statement shapes, the two fixed sweep-dimension
spellings, quantity structure, and expression precedence. Semantic analysis owns:

- required and duplicate declarations and declaration order;
- supported measurement operations and the `abs` built-in;
- name resolution, reserved user names, and use-before-declaration;
- unit/type compatibility, numeric conversion, arithmetic overloads, and assertion
  Boolean requirements;
- sweep range, step, size, and absolute-temperature constraints; and
- calibration correction type and exact active-sweep index membership/order.

Accordingly, an empty characterization, an unknown named measurement operation, or
an `over power` calibration can parse successfully and then fail semantic analysis.
No semantic restriction is encoded as syntax merely to make the grammar narrower.

## Conformance coverage

`tests/parser/grammar_conformance_tests.cpp` supplies the shared syntax corpus and
example inventory. The generated-grammar recognition suite parses every checked-in
`.hrf` example, including programs deliberately invalid only at the semantic layer,
and exercises focused accepted and rejected sources for lexical forms, contextual
names, units, comments, precedence, calls, sweep dimensions, quantity boundaries,
and statement termination. Parser regression tests separately lock normalized
tokens, ASTs, spans, and diagnostics. Semantic, end-to-end, equivalence, and CLI
suites retain their own architectural coverage.
