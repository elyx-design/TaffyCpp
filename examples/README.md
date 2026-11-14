# C++ examples

These small programs use `<taffy/taffy.hpp>` and the installed `taffy::taffy`
target. They require C++23 with `std::expected` and only the standard library—no
test packages, Rust or Python.

From the repository root, build and install the library, then configure the
examples as a separate consumer:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --config Release --parallel
cmake --install build --config Release --prefix "$PWD/install"
cmake -S examples -B build-examples -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/install"
cmake --build build-examples --config Release --parallel
ctest --test-dir build-examples -C Release --output-on-failure
```

| Program | Demonstrates |
| --- | --- |
| `flex` | Fixed root, two growing children and a column gap |
| `grid` | Fixed and fractional columns, with grid metadata |
| `measurement` | Owned text context and a full-output measurement callback |
| `mutation` | Cache hits, dirty propagation, style changes and errors |
| `custom_tree` | A minimal externally stored one-node `LayoutTree` adapter |

Each program checks representative results, including in Release. They use
`.value()` for fail-fast error handling and are teaching examples, not rendering
or text-shaping implementations.

See [building](../docs/building.md), the [API guide](../docs/api.md) and
[known issues](../docs/known-issues.md) for configuration, lifetime rules and
compatibility limits.
