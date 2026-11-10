// Regressions minimized by differential checks against pinned Taffy 0.15.1.
#include "../src/grid.hpp"
#include <taffy/taffy.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <limits>

using namespace taffy;

TEST_CASE("grid flexible sizing preserves the NaN branch and measurement count", "[cpp][regression]") {
    Tree tree;
    tree.disable_rounding();
    int count = 0;
    tree.set_intrinsic_measure([&](const LayoutInput& input, NodeId, NodeContext*, const Style&) {
        ++count;
        const auto available = input.available_space.width;
        const float width = input.known_dimensions.width.value_or(
            available.is_min_content() ? 10.0f : available.is_max_content() ? 70.0f
                : f32_min(f32_max(available.value(), 10), 70));
        return Size<float>{width, input.known_dimensions.height.value_or(width < 35 ? 30 : 15)};
    });
    Style infinite;
    infinite.size.width = Dimension::length(std::numeric_limits<float>::infinity());
    const std::array children{tree.new_leaf(infinite).value(), tree.new_leaf({}).value()};
    Style grid_style;
    grid_style.display = Display::Grid;
    grid_style.item_is_compressible_replaced = true;
    const TrackSizingFunction fraction{MinTrackSizingFunction::auto_(), MaxTrackSizingFunction::fr(1)};
    grid_style.grid_template_columns = {fraction, fraction};
    const auto grid_node = tree.new_with_children(grid_style, children).value();
    Style block;
    block.display = Display::Block;
    block.size = Size<Dimension>::from_lengths(200, 200);
    const auto root = tree.new_with_children(block, std::array{grid_node}).value();
    REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::splat(AvailableSpace::definite(200))));
    REQUIRE(tree.unrounded_layout(grid_node).size.height == 15);
    REQUIRE(count == 9);
}

TEST_CASE("absolute grid area with one named edge falls back to the padding box", "[cpp][regression]") {
    Tree tree;
    tree.disable_rounding();
    Style absolute;
    absolute.position = Position::Absolute;
    absolute.grid_column = {GridPlacement::named_line("foo"), GridPlacement::named_line("foo")};
    absolute.inset.left = absolute.inset.right = LengthPercentageAuto::length(0);
    absolute.size.height = Dimension::length(10);
    const auto child = tree.new_leaf(absolute).value();
    Style grid_style;
    grid_style.display = Display::Grid;
    grid_style.position = Position::Relative;
    grid_style.size = Size<Dimension>::from_lengths(100, 40);
    grid_style.grid_template_columns = {TrackSizingFunction::length(20), TrackSizingFunction::length(30)};
    grid_style.grid_template_column_names = {{"foo-start"}, {}, {}};
    const auto root = tree.new_with_children(grid_style, std::array{child}).value();
    REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::max_content()));
    REQUIRE(tree.unrounded_layout(child).size.width == 100);
}

TEST_CASE("unhandled calc values use the pinned tree zero resolver", "[cpp][regression]") {
    Tree tree;
    CHECK(tree.resolve_calc(Calc{123}, 200) == 0);
    CHECK(tree.LayoutTree::resolve_calc(Calc{123}, 200) == 0);
    Style style;
    style.size = {Dimension::calc(Calc{123}), Dimension::length(12)};
    const auto node = tree.new_leaf(style).value();
    const auto available = Size<AvailableSpace>::splat(AvailableSpace::definite(200));
    REQUIRE(tree.compute_layout(node, available));
    CHECK(tree.layout(node).size == Size<float>{0, 12});
    tree.set_calc_resolver([](Calc, float) { return 40.0f; });
    REQUIRE(tree.compute_layout(node, available));
    CHECK(tree.layout(node).size == Size<float>{40, 12});
    tree.set_calc_resolver({});
    REQUIRE(tree.compute_layout(node, available));
    CHECK(tree.layout(node).size == Size<float>{0, 12});
}

TEST_CASE("absolute and fixed roots resolve definite initial containing blocks", "[cpp][regression]") {
    for (const auto position : {Position::Absolute, Position::Fixed}) {
        for (const auto direction : {Direction::Ltr, Direction::Rtl}) {
            Tree tree;
            Style style;
            style.position = position;
            style.direction = direction;
            style.inset.left = LengthPercentageAuto::length(10);
            style.inset.top = LengthPercentageAuto::length(20);
            style.size = Size<Dimension>::from_lengths(30, 40);
            const auto root = tree.new_leaf(style).value();
            const auto available = Size<AvailableSpace>::splat(AvailableSpace::definite(100));
            REQUIRE(tree.compute_layout(root, available));
            REQUIRE(tree.layout(root).location == Point<float>{10, 20});
            REQUIRE(tree.layout(root).size == Size<float>{30, 40});
            REQUIRE(tree.compute_layout(root, available));
            REQUIRE(tree.layout(root).location == Point<float>{10, 20});
        }
    }
}
