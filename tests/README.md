# HorusRF tests

Tests remain in the lowest layer that can prove the behavior. They are deterministic,
offline, independent of wall-clock time and randomness, and use no real hardware.

| Suite | Responsibility | CTest label |
|---|---|---|
| parser, frontend, domain, semantic, IR | syntax, structured language services, quantities, analysis, lowering | `unit` |
| AI contracts | authoring requests, validation/proposal invariants, errors, cancellation | `unit` |
| AI validation | complete compiler adaptation, source ranges, deterministic diagnostic JSON | `unit` |
| AI provider | provider contracts, Ollama protocol/error adaptation, fake provider | `unit` |
| device, executor, characterization | separated simulated equipment and runtime routing | `unit` |
| procedural example, CSV, JSON | independent orchestration and serialization | `unit` |
| declarative end-to-end | actual fixture through compiler and runtime | `integration` |
| procedural/declarative equivalence | independent orchestration comparison | `integration` |
| CLI acceptance | process exits, streams, check mode, diagnostics, CSV/JSON side effects | `cli` |
| Studio | headless editor/results behavior, strict CSV parsing, process orchestration, diagnostic navigation | `unit` |

The parser suite includes a shared regression corpus for every checked-in `.hrf`
file plus focused accepted and rejected syntax. A filesystem inventory test fails
when a new example has not been classified as either valid or intentionally
semantic-invalid, and the semantic suite verifies that classification.

`horusrf_parser_regression_tests` locks complete normalized token, AST,
source-span, and parse-diagnostic observations for the generated production
frontend. The token baseline covers every token kind, comments, ASCII whitespace, CRLF input,
malformed numbers, invalid characters, EOF, and the number/unit boundary. The AST
baseline covers every statement and expression variant, declaration order,
written names, grouping, precedence, associativity, and byte-offset spans. It also
covers every stable parse diagnostic code and span without treating message wording
as a compatibility contract.

`tests/parser/regression_support.hpp` defines normalized token, AST, and diagnostic
snapshots. Generated parser types stay inside `horusrf_parser`; the snapshots are
the permanent behavioral guard for grammar and frontend changes.

`horusrf_frontend_tests` covers the Qt-free structured `parse` and `validate`
services: successful results, syntax and semantic classification, stable codes,
messages and spans, conservative fail-fast syntax handling, and multiple semantic
diagnostics. It also locks the diagnostics-only view and deterministic language
specification/capability metadata, including grammar-derived tokens, units,
measurements, sweeps, functions, and constraints. CLI and Studio suites separately
retain their application-boundary coverage.

`horusrf_ai_contract_tests` covers the Qt-free, dependency-free values shared by
later AI-assisted authoring layers. It verifies request separation, validation
invariants, repair attempt numbering, proposal apply eligibility and
immutability, provider identity, errors, and cancellation without contacting a
model or network service.

`horusrf_ai_validation_tests` covers complete frontend-plus-IR validation through
the AI-facing contract, representative syntax and semantic failures, multiple
diagnostics, exact source spans, and JSON escaping. CLI acceptance separately
parses valid and invalid `horusrf.validation-result` documents and locks their
stdout, stderr, and exit-status behavior.

`horusrf_ai_provider_tests` covers provider request validation, Ollama request
serialization and response decoding, structured error mapping, cancellation,
and the scripted fake provider. A fake transport keeps the suite independent of
Ollama, Qwen, sockets, internet access, GPUs, and wall-clock timing.

`horusrf_antlr_toolchain_smoke_tests` generates a small test-only C++ lexer and
parser with the vendored ANTLR 4.13.2 JAR, compiles them against the matching
vendored runtime, and parses a fixed input. This keeps tool/runtime versioning,
offline generation, and C++ linkage under CTest.

`horusrf_antlr_grammar_recognition_tests` exercises the generated recognizer from the
authoritative `grammar/HorusRf.g4` and checks every example plus the shared syntax
corpus. Focused token checks cover malformed numeric runs, the number/unit and
number/range boundaries, comments, ASCII whitespace, contextual names, and invalid
characters. Production AST, source-span, and diagnostic adaptation is covered by
the parser regression suite, while the semantic, end-to-end, equivalence, and CLI
suites retain their separate cross-layer coverage.

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
