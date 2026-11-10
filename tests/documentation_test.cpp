// Pinned source: Taffy v0.15.1, 0064bd54cbe38bf1d8bcd6ec6ab0704d45fb048a.
// Exact source spans and assertion correspondence: docs/documentation-tests.md.
#include "support/rust_backend.hpp"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <filesystem>
#include <iostream>

#ifndef TAFFY_DOCUMENTATION_SOURCE_ROOT
#error "Define TAFFY_DOCUMENTATION_SOURCE_ROOT to the absolute test-source root"
#endif

namespace taffy::test {

TEST_CASE("tests/xml.rs::get_dir", "[documentation][xml-harness-helper]") {
    // tests/xml.rs:156-162: path construction and printing only. No assertion.
    const auto root_dir = std::filesystem::path(TAFFY_DOCUMENTATION_SOURCE_ROOT);
    const auto test_dir = root_dir / "tests";
    const auto xml_test_dir = test_dir / "xml";
    std::cout << xml_test_dir.string() << '\n';
}

TEST_CASE("README.md::rustdoc-fence:31", "[documentation][readme]") {
    RustBackend tree;
    // Default style is obtained from the real native API, not zero initialization:
    // notably Display::Flex (ABI 0) and Position::Static (ABI 3).
    auto header_style = RustBackend::default_style();
    header_style.size = {{1, 800.0f}, {1, 100.0f}};
    const auto header_node = tree.new_node(header_style);

    auto body_style = RustBackend::default_style();
    body_style.size = {{1, 800.0f}, {0, 0}}; // length(800), auto()
    body_style.flex_grow = 1.0f;
    const auto body_node = tree.new_node(body_style);

    auto root_style = RustBackend::default_style();
    root_style.flex_direction = 1; // FlexDirection::Column
    root_style.size = {{1, 800.0f}, {1, 600.0f}};
    const auto root_node = tree.new_node(root_style);
    const std::array children{header_node, body_node};
    tree.set_children(root_node, children); // new_with_children facade decomposition
    tree.compute(root_node, {{4, 0}, {4, 0}}); // Size::MAX_CONTENT

    // README.md:72-77, in original order, exact equalities (no approximation).
    CHECK(tree.layout(root_node).size.width == 800.0f);
    CHECK(tree.layout(root_node).size.height == 600.0f);
    CHECK(tree.layout(header_node).size.width == 800.0f);
    CHECK(tree.layout(header_node).size.height == 100.0f);
    CHECK(tree.layout(body_node).size.width == 800.0f);
    CHECK(tree.layout(body_node).size.height == 500.0f);
}

} // namespace taffy::test
