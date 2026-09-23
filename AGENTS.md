# Repository directives

## Build artifacts

- Use `build/` as the single local CMake build directory.
- Reuse the existing `build/` directory for configuration, compilation, and tests.
- Do not leave alternate build directories such as `build-*` in the repository.
- If a temporary build directory is required for diagnostics, remove it before handing work back.

## Language and architecture

- Keep `grammar/horusrf.ebnf`, lexer/parser behavior, `docs/language.md`, and tests synchronized for every syntax change.
- Preserve the AST, semantic, IR, runtime, device, simulator, and output boundaries documented in `docs/architecture.md`.
- Use `Frequency`, `Power`, and `PowerDelta` at public and cross-layer boundaries; keep dimensional conversion in domain or semantic code.
- Runtime must depend on `RfDevice`, never simulator internals.
- Test simulator observables without exposing or depending on its private transfer function.
- Preserve independent procedural and declarative sweep/per-point orchestration.
- Avoid speculative frameworks, dependencies, and extension scaffolding.

## Tests and documentation

- Keep tests deterministic and free of network access, wall-clock dependence, randomness, and real hardware.
- Add tests at the lowest layer that proves the behavior; retain separate end-to-end, equivalence, and CLI process suites.
- Run the full unfiltered CTest suite after cross-layer changes.
- Update public documentation when behavior, diagnostics, commands, or architectural boundaries change.
- Keep archived slice records unchanged except for factual link repairs.
