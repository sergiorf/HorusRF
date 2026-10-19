# ANTLR 4.13.2 vendored toolchain

This directory contains the pinned artifacts used to generate and compile the
HorusRF ANTLR C++ frontend without network access:

- `antlr-4.13.2-complete.jar`, downloaded from
  `https://www.antlr.org/download/antlr-4.13.2-complete.jar`;
- `runtime/src/`, extracted from
  `https://www.antlr.org/download/antlr4-cpp-runtime-4.13.2-source.zip`; and
- the upstream `LICENSE.txt` and `VERSION` files from that runtime archive.

The expected SHA-256 hashes of the unmodified downloads are:

```text
eae2dfa119a64327444672aff63e9ec35a20180dc5b8090b7a6ab85125df4d76  antlr-4.13.2-complete.jar
0ed13668906e86dbc0dcddf30fdee68c10203dea4e83852b4edb810821bee3c4  antlr4-cpp-runtime-4.13.2-source.zip
```

Only the runtime archive's `runtime/src/`, `LICENSE.txt`, and `VERSION` are
vendored. Do not replace one artifact independently: generated code and the C++
runtime must use the same ANTLR release.

`runtime/src/atn/ProfilingATNSimulator.cpp` has one compatibility patch: an
explicit `<chrono>` include required by current MSVC standard-library headers.
The source uses `std::chrono` directly but the 4.13.2 archive relied on a
transitive include.
