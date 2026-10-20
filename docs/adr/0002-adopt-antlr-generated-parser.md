# ADR 0002: Adopt the ANTLR-generated parser

- Status: Accepted
- Date: 2026-10-19
- Plan: ANTLR migration, Milestone 6
- Supersedes: [ADR 0001](0001-retain-recursive-descent-parser.md)

## Context

The incremental migration established a reproducible ANTLR 4.13.2 toolchain,
made `grammar/HorusRf.g4` the sole maintained concrete grammar, and added a private
adapter that preserves the existing HorusRF AST, byte-offset source spans, and
structured diagnostics. Dual-frontend tests then proved token, AST, diagnostic,
and semantic parity across the shared regression corpus and every checked-in DSL
example.

Maintaining the handwritten lexer and recursive-descent parser after those gates
would restore the grammar drift that the migration was designed to eliminate.

## Decision

Generate the production lexer and parser from `grammar/HorusRf.g4` during the
normal CMake build. Keep generated ANTLR types, parse trees, error handling, and
AST construction private to `horusrf_parser`. Preserve the public HorusRF token,
AST, diagnostic, and `parser::parse()` interfaces.

Remove the handwritten frontend and the dual-parser comparison harness. Retain
the shared corpus and normalized regression snapshots as permanent production
parser coverage.

## Consequences

The checked-in grammar mechanically drives production syntax recognition, leaving
one manually maintained syntax definition. Builds require the pinned Java ANTLR
generator and compile the matching vendored C++ runtime, but ordinary builds and
tests remain deterministic and offline. Callers and downstream semantic, IR,
runtime, CLI, and Studio layers do not depend on ANTLR types.

## Verification

The pending cross-platform acceptance gate was completed on 2026-10-20. A clean
Linux build with GCC 15.2, Java 17, and CMake 4.2 passed the full unfiltered suite:
23 of 23 tests. The previously recorded Windows Release result and this Linux
result complete the ANTLR migration acceptance.
