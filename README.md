# HorusRF
A declarative DSL for RF characterization, measurement workflows, device simulation, and reproducible RF testing.

The M0 reference experiment characterizes an imperfect tester TX path using
calibrated external power measurements. See the canonical
[`tx_path_characterization.hrf`](examples/tx_path_characterization.hrf) program and
the [M0 roadmap](docs/m0-roadmap.md).

## Implementation status

- Slice 1, build and parsing: complete. See the
  [archived specification and acceptance record](docs/archive/slice-1-spec.md).
- Slice 2, domain semantics: ready for implementation. See the
  [detailed implementation plan](docs/slice-2-plan.md).

## Running the parser tests

Configure and build the C++20 project, then run the tests through CTest:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

When building an explicit non-default configuration with a multi-config
generator, pass the same configuration to both commands, for example
`cmake --build build --config Release` followed by
`ctest --test-dir build -C Release --output-on-failure`.
