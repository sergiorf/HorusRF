# HorusRF tests

Tests remain in the lowest layer that can prove the behavior. They are deterministic,
offline, independent of wall-clock time and randomness, and use no real hardware.

| Suite | Responsibility | CTest label |
|---|---|---|
| parser, domain, semantic, IR | syntax, quantities, analysis, lowering | `unit` |
| device, executor, characterization | separated simulated equipment and runtime routing | `unit` |
| procedural example, CSV, JSON | independent orchestration and serialization | `unit` |
| declarative end-to-end | actual fixture through compiler and runtime | `integration` |
| procedural/declarative equivalence | independent orchestration comparison | `integration` |
| CLI acceptance | process exits, streams, check mode, diagnostics, CSV/JSON side effects | `cli` |
| Studio | headless editor/results behavior, strict CSV parsing, process orchestration, diagnostic navigation | `unit` |

The parser suite includes a shared migration corpus for every checked-in `.hrf`
file plus focused accepted and rejected syntax. A filesystem inventory test fails
when a new example has not been classified as either valid or intentionally
semantic-invalid, and the semantic suite verifies that classification.

`horusrf_parser_migration_regression_tests` locks complete normalized token, AST,
source-span, and parse-diagnostic observations for the handwritten frontend. The
token baseline covers every token kind, comments, ASCII whitespace, CRLF input,
malformed numbers, invalid characters, EOF, and the number/unit boundary. The AST
baseline covers every statement and expression variant, declaration order,
written names, grouping, precedence, associativity, and byte-offset spans. It also
covers every stable parse diagnostic code and span without treating message wording
as a compatibility contract.

`tests/parser/regression_support.hpp` defines a frontend adapter and normalized
comparison helper for the dual-parser parity milestone. The source-private ANTLR
adapter exposes only the existing AST and diagnostics; generated parser types stay
inside `horusrf_parser`. These tests are the behavioral guard used when
synchronizing the grammar and frontend implementations.

`horusrf_antlr_toolchain_smoke_tests` generates a small test-only C++ lexer and
parser with the vendored ANTLR 4.13.2 JAR, compiles them against the matching
vendored runtime, and parses a fixed input. This keeps tool/runtime versioning,
offline generation, and C++ linkage under CTest without changing the production
parser before the migration parity gate.

`horusrf_antlr_grammar_recognition_tests` exercises the generated recognizer from the
authoritative `grammar/HorusRf.g4` and checks every example plus the shared syntax
corpus. Focused token checks cover malformed numeric runs, the number/unit and
number/range boundaries, comments, ASCII whitespace, contextual names, and invalid
characters. `horusrf_antlr_adapter_tests` constructs every checked-in and shared
valid program through the private adapter, covers every AST variant, checks
one-based positions and byte-offset spans, and verifies every stable parse
diagnostic code while the public parser remains handwritten.

`horusrf_antlr_parity_tests` runs the handwritten and generated frontends over the
entire shared corpus. It compares normalized complete token streams, AST structure
and spans, parse diagnostics, and downstream semantic results. This is the
migration parity gate; the public parser remains handwritten until the separate
cutover milestone.

The equivalence suite is intentionally separate from the declarative end-to-end
suite. The CLI suite crosses a process boundary; unit tests should not do so.

From the repository root:

```sh
ctest --test-dir build -L unit --output-on-failure
ctest --test-dir build -L integration --output-on-failure
ctest --test-dir build -L cli --output-on-failure
ctest --test-dir build --output-on-failure
```

With a multi-config generator, add the same configuration used for the build, for
example `-C Debug`, to every CTest command. Unfiltered CTest is the final acceptance
authority. New tests should target the lowest useful layer and use integration or
process coverage only when the contract crosses those boundaries.
