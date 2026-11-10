# TaffyCpp

A standalone C++23 port of [Taffy](https://github.com/DioxusLabs/taffy), based on
v0.15.1. Computes flexbox, grid, and block layout for application-owned content.
The production library depends only on the C++ standard library.

This repository contains the library, native C++ tests, XML fixtures, examples,
and user documentation. It does not contain Rust tools, porting archives,
compiled artifacts, or the original development history.

## Build and test

Requires CMake 3.25+ and a compiler with C++23 support, including `std::expected`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Tests use Catch2, pugixml, and nlohmann/json. CMake first reuses parent targets or
compatible installed packages, then optionally downloads pinned fallbacks. No
Python or Rust is needed. See [building](docs/building.md) for offline options,
sanitisers, shared libraries, and installation.

For a library-only build:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-release --parallel
```

## Use from CMake

Embed the source:

```cmake
add_subdirectory(path/to/TaffyCpp)
target_link_libraries(my_app PRIVATE taffy::taffy)
```

Tests and warnings-as-errors default off when embedded. Alternatively, install
and consume the package:

```sh
cmake --install build-release --prefix /path/to/install
```

```cmake
find_package(taffy_cpp CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE taffy::taffy)
```

## Basic usage

```cpp
#include <taffy/taffy.hpp>

int main() {
    taffy::Tree tree;
    taffy::Style style;
    style.size = {taffy::Dimension::length(40), taffy::Dimension::length(20)};
    const auto node = tree.new_leaf(style).value();
    tree.compute_layout(node, {taffy::AvailableSpace::definite(800),
                               taffy::AvailableSpace::definite(600)}).value();
    const auto& box = tree.layout(node); // size is 40 x 20, in layout units
    return box.size.width == 40 && box.size.height == 20 ? 0 : 1;
}
```

`Tree` owns nodes, styles, contexts, and caches. `NodeId` is a non-owning handle.
Applications supply intrinsic measurements for text/images and render the
resulting boxes. The library does not provide a DOM, renderer, stylesheet loader,
or selector/cascade engine.

- [Public API and ownership](docs/api.md)
- [Examples](examples/README.md)
- [Tests](tests/README.md)
- [Known issues and compatibility boundaries](docs/known-issues.md)
- [Provenance](docs/provenance.md)

## License

[MIT](LICENSE), retaining the upstream Visly Inc. and Taffy Authors notices.
See [third-party notices](docs/third-party-notices.md) for test-fixture provenance,
retained notices, and test-only dependency licensing. No third-party test package
is linked into or installed with the production library.
