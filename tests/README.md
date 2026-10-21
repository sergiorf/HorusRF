# HorusRF tests

Tests remain in the lowest layer that can prove the behavior. They are deterministic,
offline, independent of wall-clock time and randomness, and use no real hardware.

| Suite | Responsibility | CTest label |
|---|---|---|
| parser, frontend, domain, semantic, IR | syntax, structured language services, quantities, analysis, lowering | `unit` |
| AI contracts | authoring requests, validation/proposal invariants, errors, cancellation | `unit` |
| AI validation | complete compiler adaptation, source ranges, deterministic diagnostic JSON | `unit` |
| AI golden dataset | manifest/metadata integrity, discovery, unique IDs, curated script compilation | `unit` |
| AI semantic comparison | canonical IR comparison, mismatch details, golden-case self-equivalence | `unit` |
| AI provider | provider contracts, Ollama protocol/error adaptation, fake provider | `unit` |
| AI prompt | compiler-derived contexts, compiling examples, response schema and parsing | `unit` |
| AI agent | bounded generation/repair states, compiler feedback, cancellation and failures | `unit` |
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

`horusrf_ai_golden_dataset_tests` validates the versioned dataset in
`tests/ai/golden/` without Ollama or network access. It checks strict metadata,
duplicate identifiers, unlisted directories, malformed expected programs, and
passes every supported curated output through parsing, semantic analysis, and IR
lowering. Explicitly unsupported requests remain separate rejection cases.

`horusrf_ai_semantic_comparison_tests` compiles both sides through the existing
frontend and IR lowering path, then checks conservative structural equivalence.
It covers canonical unit conversion and harmless renaming, ordered sweep bounds
and steps, measurement sets, normalized derived expressions, assertions,
calibration indexes and correction expressions, invalid-source handling, and
self-equivalence for all supported golden cases. It uses no model, network,
runtime, simulator, or hardware.

`horusrf_ai_provider_tests` covers provider request validation, Ollama request
serialization and response decoding, structured error mapping, cancellation,
and the scripted fake provider. A fake transport keeps the suite independent of
Ollama, Qwen, sockets, internet access, GPUs, and wall-clock timing.

`horusrf_ai_prompt_tests` covers deterministic generation, modification, and
repair messages, compiler-owned capability context, request bounds, curated
examples through complete compiler validation, strict proposal-schema parsing,
and malformed response rejection. It does not invoke a provider or network.

`horusrf_ai_agent_tests` combines the scripted fake provider with the real
in-memory compiler validation adapter. It covers first-pass completion,
diagnostic-directed repair, retry exhaustion, unchanged invalid output, provider
and structured-response failures, cancellation, invalid requests, modification
context preservation, retained invalid proposals, and conservative numeric
fidelity warnings. It does not invoke Ollama, the network, runtime, simulator, or
hardware.

`horusrf_studio_tests` exercises the assistant through an injected fake backend.
Its offline model-selection coverage includes discovery of the 3B and 7B Qwen
identifiers, friendly labels, propagation of the exact selected identifier,
restart persistence in isolated test settings, and explicit fallback when a
saved model is no longer installed.

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
