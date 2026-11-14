# Building

TaffyCpp requires CMake 3.25+, a C++23 compiler and standard library with
`std::expected`, and a native build tool. The library depends only on the C++
standard library. Neither Rust nor Python is needed to build or run native tests.

## Build and test

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

A top-level build enables tests by default: the normal suite has **16 CTest
groups**, including the 6,604 XML layout fixtures. See [tests](../tests/README.md).
Use a separate build directory for Release. With multi-configuration generators,
`--config` and CTest's `-C` select the configuration; `CMAKE_BUILD_TYPE` alone does
not.

## Library-only build and install

This path needs no third-party downloads:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --config Release --parallel
cmake --install build --config Release --prefix "$PWD/install"
```

Alternatively, `-DTAFFY_BUILD_CPP_TESTS=OFF` disables Taffy's native tests without
turning off other projects' tests. `-DBUILD_SHARED_LIBS=ON` selects a shared library.

An installed consumer uses:

```cmake
find_package(taffy_cpp CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE taffy::taffy)
```

Set `CMAKE_PREFIX_PATH` to the installation prefix when configuring the consumer.
See the [standalone examples](../examples/README.md) and [API guide](api.md).

## Embedding and test dependencies

With `add_subdirectory(TaffyCpp)`, Taffy's tests default **off**. Enabling them
with `TAFFY_BUILD_CPP_TESTS=ON` still respects `BUILD_TESTING=OFF`.
The library itself has no third-party dependencies in either mode.

Tests first reuse existing parent targets, then look for compatible installed
packages, and only then use optional SHA-256-pinned download fallbacks:

| Required target | Package minimum | Download fallback |
| --- | --- | --- |
| `Catch2::Catch2WithMain` | Catch2 3.5.4 | 3.8.1 |
| `pugixml::pugixml` | pugixml 1.15 | 1.15 |
| `nlohmann_json::nlohmann_json` | nlohmann/json 3.11.3 | 3.11.3 |

Parent-provided targets take priority; the parent owns their versions and options.
Package discovery uses normal CMake config lookup, including `CMAKE_PREFIX_PATH`
and package `_DIR` hints. Set `TAFFY_FETCH_TEST_DEPENDENCIES=OFF` to forbid fallback
downloads; missing test packages then produce a configure error. This is useful
for offline builds with preinstalled dependencies. Test packages are not required
by consumers of the installed library. See [notices](third-party-notices.md).

## Optional checks and limitations

- `TAFFY_WARNINGS_AS_ERRORS=ON` enables fatal project warnings.
- `TAFFY_SANITIZE=ON` enables supported Clang/GCC native sanitizer checks; use a
  fresh Debug build directory.
- Do not enable fast-math or floating-point contraction: the library target
  carries strict floating-point options needed by its layout arithmetic.

Native Windows has been verified with MSVC 19.44 (Visual Studio 2022) using the
Visual Studio generator, Debug and Release, static and shared, plus Clang 21 with
Ninja: all 16 CTest groups, the installed-package consumer and the examples pass.
Test executables use an 8 MiB stack on Windows because deep-tree fixtures exceed
the 1 MiB default in unoptimized builds; applications that lay out very deep trees
in Debug builds may need the same. See [known issues](known-issues.md) for
compatibility limits.
