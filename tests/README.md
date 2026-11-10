# Native C++ tests

The default suite has **16 CTest groups** and needs no Rust or Python:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

Catch2, pugixml and nlohmann/json are **test-only** packages. CMake reuses parent
targets or installed packages before optional pinned downloads. Tests default on
in a top-level build and off when embedded. `BUILD_TESTING=OFF` or
`TAFFY_BUILD_CPP_TESTS=OFF` disables them. See [building](../docs/building.md) for
offline configuration, dependency versions and optional sanitizer/harness checks.

## Layout

- `fixtures/xml/`: 6,604 unchanged XML input/expectation files, discovered at runtime.
- `ported/`: translated value, helper and cache assertions from the upstream pin.
- `support/`: shared declarations and C++ wrappers.
- `cpp_backend/`: native implementations of test-only adapter interfaces.
- `cpp_*.cpp` and `production_*_regressions.cpp`: direct tests and regressions.
- `package_consumer/`: separate installed-package consumer checks.

Historical `oracle` and `rust_backend` identifiers in C++ test files do not imply
a Rust runtime or build dependency. XML tests use pugixml, not a system XML SDK.
Public headers also receive independent compile-only checks during the build.

The suite covers native API behavior and translated assertions, not universal
CSS correctness. The XML comparator checks geometry for every node and follows
upstream's conditional comparisons for optional metadata. Read
[known issues](../docs/known-issues.md) before interpreting a passing run.

The corpus and translated tests originate from upstream commit
`0064bd54cbe38bf1d8bcd6ec6ab0704d45fb048a`. Preserve the root license,
[fixture license](fixtures/LICENSE) and applicable [notices](../docs/third-party-notices.md).
See [fixture identity](fixtures/README.md) and [provenance](../docs/provenance.md).
