# Repository directives

## Build artifacts

- Use `build/` as the single local CMake build directory.
- Reuse the existing `build/` directory for configuration, compilation, and tests.
- Do not leave alternate build directories such as `build-*` in the repository.
- If a temporary build directory is required for diagnostics, remove it before handing work back.
