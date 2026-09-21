# HorusRF
A declarative DSL for RF characterization, measurement workflows, device simulation, and reproducible RF testing.

The M0 reference experiment characterizes an imperfect tester TX path using
calibrated external power measurements. See the canonical
[`tx_path_characterization.hrf`](examples/tx_path_characterization.hrf) program and
the [M0 roadmap](docs/m0-roadmap.md).

## Implementation status

- Slice 1, build and parsing: complete. See the
  [archived specification and acceptance record](docs/archive/slice-1-spec.md).
- Slice 2, domain semantics: complete. See the
  [archived implementation and acceptance record](docs/archive/slice-2-plan.md).
- Slice 3, IR lowering: complete. See the
  [archived implementation and acceptance record](docs/archive/slice-3-plan.md).
- Slice 4, device-bound point execution: complete. See the
  [archived implementation and acceptance record](docs/archive/slice-4-plan.md).
- Slice 5, complete characterization: complete. See the
  [archived implementation and acceptance record](docs/archive/slice-5-plan.md).
- Slice 6, procedural reference: complete. See the
  [archived implementation and acceptance record](docs/archive/slice-6-plan.md).
- Slice 7, end-to-end proof and CSV: planned. See the
  [implementation plan](docs/slice-7-plan.md).

## Running the tests

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
