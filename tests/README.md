# HorusRF tests

Tests remain in the lowest layer that can prove the behavior. They are deterministic,
offline, independent of wall-clock time and randomness, and use no real hardware.

| Suite | Responsibility | CTest label |
|---|---|---|
| parser, domain, semantic, IR | syntax, quantities, analysis, lowering | `unit` |
| device, executor, characterization | separated simulated equipment and runtime routing | `unit` |
| procedural example, CSV | independent orchestration and serialization | `unit` |
| declarative end-to-end | actual fixture through compiler and runtime | `integration` |
| procedural/declarative equivalence | independent orchestration comparison | `integration` |
| CLI acceptance | process exits, streams, diagnostics, CSV side effects | `cli` |

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
