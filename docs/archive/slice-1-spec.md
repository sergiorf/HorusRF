# Slice 1 Specification — Build and Parsing

> **Archived as complete on 2026-09-20.** The required CMake configure and
> build succeeded, and CTest passed `horusrf_parser_tests` (1/1). This document
> is retained as the acceptance record for the implemented parser front end.

## Purpose

Slice 1 establishes HorusRF's syntax-only front end. A clean checkout must
build, lex and parse the canonical M0 program, expose a testable abstract
syntax tree (AST), and report malformed syntax with source locations.

This slice proves that the language surface can be represented reliably
before units, dimensional rules, execution, or hardware interfaces exist.

## Scope

Implement:

- a C++20 CMake project with CTest integration;
- a dependency-free token model and handwritten lexer;
- source positions and spans on tokens and AST nodes;
- `grammar/horusrf.ebnf` as the syntax contract;
- AST types for the complete M0 syntax surface;
- a recursive-descent parser for one `characterize` block;
- `examples/tx_path_characterization.hrf`;
- lexer, parser, source-location, and syntax-error tests.

Completion means the canonical example parses end to end, including
`derive calibration ... over frequency`, and tests verify the resulting AST
without constructing or invoking runtime, device, simulator, semantic, or IR
objects.

## Non-goals

Do not implement unit conversion, dBm/dB dimensional checking, identifier
resolution, duplicate checks, AST-to-IR lowering, runtime execution, device
interfaces, simulator behavior, calibration application, result metrics, CSV,
an execution CLI, or third-party parser/test-framework dependencies.

The parser may preserve numeric and unit text in a quantity literal, but must
not decide whether an expression is physically valid. That belongs to Slice 2.

## Repository and build deliverables

Minimum structure:

```text
CMakeLists.txt
cmake/warnings.cmake
grammar/horusrf.ebnf
include/horusrf/ast/ast.hpp
include/horusrf/parser/lexer.hpp
include/horusrf/parser/parser.hpp
include/horusrf/parser/token.hpp
tests/test_support.hpp
tests/parser/lexer_tests.cpp
tests/parser/parser_tests.cpp
examples/tx_path_characterization.hrf
```

The exact source partition may differ, but public AST/parser types belong
under `include/horusrf` and implementation files remain separate from tests.

Required targets:

| Target | Kind | Responsibility |
|---|---|---|
| `horusrf_parser` | static library | source, token, lexer, AST, and parser implementation |
| `horusrf_parser_tests` | executable | lexer and parser tests |
| `horusrf_parser_tests` | CTest test | runs the test executable |

The clean-build contract is:

```text
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Canonical fixture

`examples/tx_path_characterization.hrf` must contain:

```hrf
characterize tx_path {

    reference power = -10 dBm

    sweep frequency
        2.40 GHz .. 2.50 GHz
        step 1 MHz

    measure power

    derive error =
        power - reference.power

    derive calibration tx_power {
        correction = reference.power - power
        over frequency
    }
}
```

Whitespace and line comments (`//` through newline or EOF) are ignored. The
language is not line-sensitive and does not require semicolons.

## Lexical contract

The lexer must distinguish these token categories:

```text
EndOfFile, Identifier, Number
Characterize, Reference, Power, Sweep, Frequency, Step, Measure, Derive,
Calibration, Correction, Over
LeftBrace, RightBrace, Equal, Range, Dot, Plus, Minus, LeftParen, RightParen,
Comma
UnitHz, UnitKHz, UnitMHz, UnitGHz, UnitDBm, UnitDB
Invalid
```

Lexing rules:

- An identifier starts with an ASCII letter and continues with letters,
  digits, or `_`.
- A number contains digits and may contain one decimal point followed by
  digits. `2.40` is one token; `-10` is `Minus` followed by `Number`.
- `..` is `Range`; `.` is `Dot` for `reference.power`.
- Units are reserved tokens: `Hz`, `kHz`, `MHz`, `GHz`, `dBm`, and `dB`.
- Keywords and units are case-sensitive.
- Malformed forms such as `2.`, `.5`, `1.2.3`, and identifiers beginning with
  a digit must not be silently discarded; emit an invalid token or diagnostic.

Every token carries a half-open span:

```cpp
struct SourcePosition {
    std::size_t line;    // one-based
    std::size_t column;  // one-based
    std::size_t offset;  // zero-based byte offset
};

struct SourceSpan {
    SourcePosition begin;
    SourcePosition end;  // immediately after the final byte
};
```

The initial position is line 1, column 1, offset 0. Newline increments the
line, resets column to 1, and advances offset. EOF has a zero-width span at
the position immediately after the final source byte.

## AST contract

The AST represents source syntax only. It must not contain device handles,
runtime state, resolved symbols, canonical unit values, or semantic types.
Every node has a span covering its source construct.

Required logical nodes:

```text
Program { characterization, span }
Characterization { name, statements, span }
ReferenceStatement { power: QuantityLiteral, span }
SweepStatement { start, end, step: QuantityLiteral, span }
MeasurementStatement { span }
DeriveStatement { name, expression, span }
CalibrationStatement { name, correction, dimensions, span }
```

`Statement` and `Expression` may use `std::variant`, tagged nodes, or another
value-safe representation, but tests must distinguish every alternative.

Required expression alternatives are `QuantityLiteral`,
`IdentifierExpression`, `ReferenceExpression`, `UnaryExpression`, and
`BinaryExpression`. A quantity preserves numeric spelling, unit spelling, and
span. A reference preserves both identifiers in `reference.power`. A binary
node preserves operator and both operands. Parentheses may be represented by
grouping rather than a dedicated node.

## Parser contract

The parser is recursive descent and implements:

```ebnf
program = characterization , end_of_file ;
characterization = "characterize" , identifier , "{" , statement* , "}" ;
statement = reference_statement | sweep_statement | measurement_statement
          | derive_statement | calibration_statement ;
reference_statement = "reference" , "power" , "=" , signed_quantity ;
sweep_statement = "sweep" , "frequency" , frequency_range , "step" , quantity ;
frequency_range = quantity , ".." , quantity ;
measurement_statement = "measure" , "power" ;
derive_statement = "derive" , identifier , "=" , expression ;
calibration_statement = "derive" , "calibration" , identifier , "{"
          , "correction" , "=" , expression
          , "over" , identifier , { "," , identifier } , "}" ;
expression = additive_expression ;
additive_expression = unary_expression , { ("+" | "-") , unary_expression } ;
unary_expression = [ "-" ] , primary_expression ;
primary_expression = quantity | reference | identifier
          | "(" , expression , ")" ;
reference = "reference" , "." , identifier ;
signed_quantity = [ "-" ] , number , unit ;
quantity = number , unit ;
```

Precedence is parentheses, unary minus, then left-associative addition and
subtraction. The parser only checks token shape. It must not reject
`power + power`, resolve `reference.power`, or require `frequency` to be an
active sweep; those are Slice 2 semantic checks.

## Diagnostics contract

Failures must be structured and source-located. A minimum diagnostic contains
a code, human-readable message, and `SourceSpan`. Codes should distinguish at
least unexpected token, unexpected EOF, invalid token, expected identifier,
expected quantity, expected unit, and expected expression.

The parser may throw an exception containing one diagnostic or return a result
with diagnostics. It must not report success with a partially valid `Program`.
Diagnostics identify the unexpected token or EOF location.

Tests must verify locations for a missing closing brace, missing unit, missing
expression after `=`, malformed calibration syntax, and an invalid token.
Recovery is optional, but must not hide the first syntax error.

## Required tests

Lexer tests cover every keyword and punctuation token, identifiers, integer
and decimal numbers, all units, range/dot/operators, whitespace, comments,
line/column/offset transitions, malformed numbers, invalid characters, and
EOF location.

Parser tests structurally inspect the canonical example and assert:

- characterization name `tx_path`;
- reference `-10 dBm`;
- sweep endpoints `2.40 GHz` and `2.50 GHz`, with step `1 MHz`;
- `measure power`;
- derived expression `power - reference.power`;
- calibration name `tx_power`;
- correction expression `reference.power - power`;
- one index dimension named `frequency`.

Additional tests cover precedence, parentheses, multiple `over` dimensions,
and malformed programs. The calibration test must prove that its correction
and index list are retained separately.

## Implementation sequence

1. Add CMake, warnings, the parser library, and CTest registration.
2. Add source-location primitives and token definitions.
3. Implement lexer and token tests.
4. Implement AST value types and expression alternatives.
5. Implement quantity, reference, and precedence-aware expression parsing.
6. Implement statements and characterization parsing.
7. Add the canonical fixture, structural tests, and negative tests.
8. Run the clean-build commands from this document.

## Slice boundary and handoff

Slice 1 produces only a syntax AST and parse diagnostics:

```text
source text -> lexer -> tokens -> parser -> syntax AST
                                              |
                                              v
                                  Slice 2 semantic analysis
```

Parser code must not include or depend on `RfDevice`, simulator, runtime, IR,
or semantic-analysis headers. Slice 2 consumes this AST to add unit
normalization, semantic types, name resolution, and dimensional diagnostics.

## Acceptance checklist

- [x] Clean CMake configure and C++20 build succeeds.
- [x] CTest discovers and passes parser tests.
- [x] Grammar file matches implemented syntax.
- [x] Canonical example parses without special-case code.
- [x] AST retains all five statement forms.
- [x] Calibration correction and index dimensions are retained separately.
- [x] Unary minus, precedence, references, and parentheses parse.
- [x] Tokens and AST nodes have correct spans.
- [x] Malformed syntax produces structured, source-located diagnostics.
- [x] Parser tests require no runtime, device, simulator, semantic, or IR code.
- [x] No third-party parser or test framework is required.
