// Keep the umbrella header's getting-started example executable.
#include <taffy/taffy.hpp>
#include <array>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("native public header quickstart creates and lays out a flex tree", "[cpp][foundation][public-api]") {
    taffy::Tree tree;
    taffy::Style item;
    item.size = {taffy::Dimension::length(80), taffy::Dimension::length(30)};
    auto child = tree.new_leaf(item).value();

    taffy::Style row;
    row.display = taffy::Display::Flex;
    row.size = {taffy::Dimension::length(200), taffy::Dimension::length(40)};
    std::array children{child};
    auto root = tree.new_with_children(row, children).value();

    tree.compute_layout(root, taffy::Size<taffy::AvailableSpace>::max_content()).value();
    const auto& box = tree.layout(child);
    CHECK(box.size.width == 80);
    CHECK(box.size.height == 30);
    CHECK(box.location == taffy::Point<float>{0, 0});
    CHECK(tree.layout(root).size == taffy::Size<float>{200, 40});
}
