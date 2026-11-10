// Faithful translations of Taffy v0.15.1 handwritten geometry tests (MIT).
// Source: reference/upstream/tests/hand_written; see reference/geometry_handwritten.
#include "support/measure_helpers.hpp"
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {
using taffy::test::RustBackend;
using taffy::test::TestNodeContext;
using Node = RustBackend::NodeId;
constexpr OracleSizing max_content{{4, 0}, {4, 0}};
constexpr OracleSizing viewport{{1, 800}, {1, 600}};
constexpr std::array<std::uint32_t, 3> displays{2, 0, 1}; // Block, Flex, Grid
constexpr std::array<std::pair<float, float>, 4> edges{{{20, 0}, {0, 20}, {20, 20}, {0, 0}}};

constexpr OracleScalar length(float value) {
    return {1, value};
}
constexpr OracleScalar automatic() {
    return {0, 0};
}
constexpr OracleSizing size(float width, float height) {
    return {length(width), length(height)};
}
constexpr OracleEdges all(float value) {
    return {length(value), length(value), length(value), length(value)};
}
constexpr OracleEdges vertical(float top, float bottom) {
    return {length(0), length(0), length(top), length(bottom)};
}

template <typename... Children>
Node parent(RustBackend& tree, const OracleStyle& style, Children... children) {
    const auto node = tree.new_node(style);
    tree.set_children(node, std::array{children...});
    return node;
}

void require_size(const OracleLayout& layout, float width, float height) {
    REQUIRE(layout.size.width == width);
    REQUIRE(layout.size.height == height);
}
void require_location(const OracleLayout& layout, float x, float y) {
    REQUIRE(layout.x == x);
    REQUIRE(layout.y == y);
}

OracleStyle root_style() {
    auto style = RustBackend::default_style();
    style.display = 2;
    style.margin = {length(10), length(10), length(20), length(20)};
    style.border = all(5);
    return style;
}
OracleStyle oof_style(std::uint32_t position, OracleEdges inset, OracleSizing dimensions) {
    auto style = RustBackend::default_style();
    style.position = position;
    style.inset = inset;
    style.size = dimensions;
    return style;
}
OracleStyle block(float top, float bottom, OracleScalar height) {
    auto style = RustBackend::default_style();
    style.display = 2;
    style.size = {automatic(), height};
    style.margin = vertical(top, bottom);
    return style;
}

OracleLayout scroller_with_padding(std::uint32_t display, OracleEdges border, OracleEdges padding,
                                   float* scroll_height = nullptr) {
    RustBackend tree;
    auto style = tree.default_style();
    style.size = size(100, 1000);
    const auto child = tree.new_node(style);
    style = tree.default_style();
    style.display = display;
    style.box_sizing = 0;
    style.size = size(300, 200);
    style.border = border;
    style.padding = padding;
    style.overflow_x = style.overflow_y = 2;
    const auto node = parent(tree, style, child);
    tree.compute(node, max_content);
    if (scroll_height) {
        *scroll_height = tree.scroll_height(node);
    }
    return tree.layout(node);
}

OracleLayout measured_leaf(std::uint32_t overflow, OracleEdges padding) {
    RustBackend tree;
    auto context = TestNodeContext::fixed(100, 1000);
    auto style = tree.default_style();
    style.box_sizing = 0;
    style.size = size(300, 200);
    style.padding = padding;
    style.overflow_x = style.overflow_y = overflow;
    const auto node = tree.new_node(style, [&context](auto id, const auto& input, const auto& s) {
        return taffy::test::test_measure_function(input, id, &context, s);
    });
    tree.compute(node, max_content);
    return tree.layout(node);
}

OracleLayout container_with_absolute_child(std::uint32_t display, std::uint32_t overflow,
                                           std::uint32_t direction, OracleScalar left,
                                           OracleScalar right, float* scroll_width = nullptr) {
    RustBackend tree;
    const auto child =
        tree.new_node(oof_style(1, {left, right, length(0), automatic()}, size(50, 50)));
    auto style = tree.default_style();
    style.display = display;
    style.direction = direction;
    style.position = 0;
    style.size = size(200, 200);
    style.overflow_x = style.overflow_y = overflow;
    const auto node = parent(tree, style, child);
    tree.compute(node, max_content);
    if (scroll_width) {
        *scroll_width = tree.scroll_width(node);
    }
    return tree.layout(node);
}

OracleLayout nested_overflow_layout(std::uint32_t display, std::uint32_t overflow_x,
                                    std::uint32_t overflow_y) {
    RustBackend tree;
    const auto grandchild = tree.new_node(
        oof_style(1, {length(0), automatic(), length(200), automatic()}, size(50, 50)));
    auto style = tree.default_style();
    style.display = display;
    style.position = 0;
    style.size = size(200, 200);
    style.overflow_x = overflow_x;
    style.overflow_y = overflow_y;
    const auto child = parent(tree, style, grandchild);
    style = tree.default_style();
    style.display = display;
    style.size = size(200, 200);
    const auto node = parent(tree, style, child);
    tree.compute(node, max_content);
    return tree.layout(node);
}
void assert_floored_at_zero(std::uint32_t display, std::uint32_t flex_direction) {
    RustBackend tree;
    auto context = TestNodeContext::zero();
    std::vector<std::string> negative_inputs;
    auto style = tree.default_style();
    style.margin = {length(30), length(10), length(0), length(0)};
    style.size = {automatic(), automatic()};
    const auto leaf = tree.new_node(style, [&](auto id, const auto& input, const auto& s) {
        const auto known = input.known_dimensions;
        const auto available = input.available_space;
        const auto is_negative = [](OracleOptional value) {
            return value.present && value.value < 0;
        };
        const auto is_negative_available = [](OracleScalar value) {
            return value.tag == 1 && value.value < 0;
        };
        if (is_negative(known.width) || is_negative(known.height) ||
            is_negative_available(available.width) || is_negative_available(available.height)) {
            negative_inputs.push_back("known width=" + std::to_string(known.width.value) +
                                      " present=" + std::to_string(known.width.present) +
                                      " height=" + std::to_string(known.height.value) +
                                      " present=" + std::to_string(known.height.present) +
                                      " available width=" + std::to_string(available.width.value) +
                                      " tag=" + std::to_string(available.width.tag) +
                                      " height=" + std::to_string(available.height.value) +
                                      " tag=" + std::to_string(available.height.tag));
        }
        return taffy::test::test_measure_function(input, id, &context, s);
    });
    style = tree.default_style();
    style.display = display;
    style.flex_direction = flex_direction;
    style.size = {length(20), automatic()};
    const auto root = parent(tree, style, leaf);
    // The upstream helper sets this even for Block and Flex.
    const std::array<OracleGridTrack, 1> columns{{{length(20), length(20)}}};
    tree.set_grid_template_columns(root, std::span<const OracleGridTrack>(columns));
    tree.compute(root, max_content);
    CAPTURE(negative_inputs);
    REQUIRE(negative_inputs.empty());
    REQUIRE(tree.layout(leaf).size.width == 0);
}

OracleLayout leaf_with_inset(std::uint32_t position, std::uint32_t display) {
    RustBackend tree;
    const auto child = tree.new_node(
        oof_style(position, {length(5), automatic(), length(7), automatic()}, size(10, 10)));
    auto style = tree.default_style();
    style.display = display;
    style.size = size(100, 100);
    const auto root = parent(tree, style, child);
    tree.compute(root, max_content);
    return tree.layout(child);
}

void require_zero_rect(const OracleRect& rect) {
    REQUIRE(rect.left == 0);
    REQUIRE(rect.right == 0);
    REQUIRE(rect.top == 0);
    REQUIRE(rect.bottom == 0);
}
} // namespace

TEST_CASE("hand_written::root_constraints::root_constraints::root_with_percentage_size",
          "[upstream][geometry]") {
    RustBackend tree;
    auto style = tree.default_style();
    style.size = {{2, 1}, {2, 1}};
    const auto node = tree.new_node(style);
    tree.compute(node, size(100, 200));
    require_size(tree.layout(node), 100, 200);
}

TEST_CASE("hand_written::root_constraints::root_constraints::root_with_no_size",
          "[upstream][geometry]") {
    RustBackend tree;
    const auto node = tree.new_node(tree.default_style());
    tree.compute(node, size(100, 100));
    require_size(tree.layout(node), 0, 0);
}

TEST_CASE("hand_written::root_constraints::root_constraints::root_with_larger_size",
          "[upstream][geometry]") {
    RustBackend tree;
    auto style = tree.default_style();
    style.size = size(200, 200);
    const auto node = tree.new_node(style);
    tree.compute(node, size(100, 100));
    require_size(tree.layout(node), 200, 200);
}

TEST_CASE("hand_written::root_constraints::root_constraints::root_padding_and_border_larger_than_"
          "definite_size",
          "[upstream][geometry]") {
    RustBackend tree;
    const auto child = tree.new_node(tree.default_style());
    auto style = tree.default_style();
    style.size = size(10, 10);
    style.padding = all(10);
    style.border = all(10);
    const auto root = parent(tree, style, child);
    tree.compute(root, max_content);
    require_size(tree.layout(root), 40, 40);
}

TEST_CASE("hand_written::min_max_overrides::min_max_overrides::min_overrides_max",
          "[upstream][geometry]") {
    RustBackend tree;
    auto style = tree.default_style();
    style.size = size(50, 50);
    style.min_size = size(100, 100);
    style.max_size = size(10, 10);
    const auto child = tree.new_node(style);
    tree.compute(child, size(100, 100));
    require_size(tree.layout(child), 100, 100);
}

TEST_CASE("hand_written::min_max_overrides::min_max_overrides::max_overrides_size",
          "[upstream][geometry]") {
    RustBackend tree;
    auto style = tree.default_style();
    style.size = size(50, 50);
    style.max_size = size(10, 10);
    const auto child = tree.new_node(style);
    tree.compute(child, size(100, 100));
    require_size(tree.layout(child), 10, 10);
}

TEST_CASE("hand_written::min_max_overrides::min_max_overrides::min_overrides_size",
          "[upstream][geometry]") {
    RustBackend tree;
    auto style = tree.default_style();
    style.size = size(50, 50);
    style.min_size = size(100, 100);
    const auto child = tree.new_node(style);
    tree.compute(child, size(100, 100));
    require_size(tree.layout(child), 100, 100);
}

TEST_CASE("hand_written::rounding::rounding_doesnt_leave_gaps", "[upstream][geometry]") {
    RustBackend tree;
    auto style = tree.default_style();
    style.size = size(100.3f, 100.3f);
    const auto child_a = tree.new_node(style);
    const auto child_b = tree.new_node(style);
    style = tree.default_style();
    style.size = size(963.3333f, 1000);
    style.justify_content = ORACLE_CONTENT_CENTER;
    const auto root = parent(tree, style, child_a, child_b);
    tree.compute(root, max_content);
    const auto layout_a = tree.layout(child_a);
    const auto layout_b = tree.layout(child_b);
    REQUIRE(layout_a.x + layout_a.size.width == layout_b.x);
}

TEST_CASE("hand_written::block_margin::layout_margin_excludes_collapsed_margins",
          "[upstream][geometry]") {
    RustBackend tree;
    const auto inner = tree.new_node(block(13, 20, length(10)));
    const auto outer = parent(tree, block(8, 5, automatic()), inner);
    const auto sibling = tree.new_node(block(30, 0, length(10)));
    auto style = tree.default_style();
    style.display = 4;
    style.size = {length(100), automatic()};
    const auto root = parent(tree, style, outer, sibling);
    tree.compute(root, max_content);
    const auto outer_layout = tree.layout(outer);
    REQUIRE(outer_layout.y == 13);
    REQUIRE(outer_layout.size.height == 10);
    REQUIRE(outer_layout.margin.top == 8);
    REQUIRE(outer_layout.margin.bottom == 5);
    const auto inner_layout = tree.layout(inner);
    REQUIRE(inner_layout.y == 0);
    REQUIRE(inner_layout.margin.top == 13);
    REQUIRE(inner_layout.margin.bottom == 20);
    const auto sibling_layout = tree.layout(sibling);
    REQUIRE(sibling_layout.y == 53);
    REQUIRE(sibling_layout.margin.top == 30);
}

TEST_CASE("hand_written::initial_containing_block::initial_containing_block::root_is_placed_at_its_"
          "margin",
          "[upstream][geometry]") {
    RustBackend tree;
    const auto root = tree.new_node(root_style());
    tree.compute(root, viewport);
    const auto layout = tree.layout(root);
    require_location(layout, 10, 20);
    REQUIRE(layout.size.width == 780);
}

TEST_CASE(
    "hand_written::initial_containing_block::initial_containing_block::inset_zero_fills_viewport",
    "[upstream][geometry]") {
    for (const auto position : {1u, 2u}) { // Absolute, Fixed: both source branches.
        CAPTURE(position);
        RustBackend tree;
        const auto oof = tree.new_node(oof_style(position, all(0), {automatic(), automatic()}));
        const auto root = parent(tree, root_style(), oof);
        tree.compute(root, viewport);
        const auto layout = tree.layout(oof);
        require_location(layout, -10, -20);
        require_size(layout, 800, 600);
    }
}

TEST_CASE("hand_written::initial_containing_block::initial_containing_block::bottom_right_insets_"
          "resolve_against_viewport",
          "[upstream][geometry]") {
    for (const auto position : {1u, 2u}) {
        CAPTURE(position);
        RustBackend tree;
        const auto oof = tree.new_node(
            oof_style(position, {automatic(), length(0), automatic(), length(0)}, size(50, 50)));
        const auto root = parent(tree, root_style(), oof);
        tree.compute(root, viewport);
        require_location(tree.layout(oof), 800 - 50 - 10, 600 - 50 - 20);
    }
}

TEST_CASE("hand_written::initial_containing_block::initial_containing_block::static_position_is_"
          "unaffected",
          "[upstream][geometry]") {
    RustBackend tree;
    const auto oof = tree.new_node(
        oof_style(1, {automatic(), automatic(), automatic(), automatic()}, size(50, 50)));
    const auto root = parent(tree, root_style(), oof);
    tree.compute(root, viewport);
    require_location(tree.layout(oof), 5, 5);
}

TEST_CASE("hand_written::initial_containing_block::initial_containing_block::fixed_root_is_"
          "positioned_by_its_insets",
          "[upstream][geometry]") {
    RustBackend tree;
    auto style = tree.default_style();
    style.display = 2;
    style.position = 2;
    style.inset = {automatic(), length(10), {2, 0.1f}, automatic()};
    style.size = size(100, 50);
    const auto root = tree.new_node(style);
    tree.compute(root, viewport);
    const auto layout = tree.layout(root);
    require_location(layout, 800 - 100 - 10, 60);
    require_size(layout, 100, 50);
}

TEST_CASE("hand_written::scroll_size::scrollable_overflow_excludes_the_containers_own_border",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        for (const auto& [top, bottom] : edges) {
            CAPTURE(display, top, bottom);
            const auto layout =
                scroller_with_padding(display, vertical(top, bottom), vertical(0, 0));
            REQUIRE(layout.scrollable_overflow.bottom == 1000);
        }
    }
}

TEST_CASE("hand_written::scroll_size::scrollable_overflow_includes_the_containers_own_padding",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        for (const auto& [top, bottom] : edges) {
            CAPTURE(display, top, bottom);
            const auto layout =
                scroller_with_padding(display, vertical(0, 0), vertical(top, bottom));
            REQUIRE(layout.scrollable_overflow.bottom == 1000 + top + bottom);
        }
    }
}

TEST_CASE("hand_written::scroll_size::scrollable_overflow_excludes_the_own_padding_of_a_non_scroll_"
          "container",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        for (const auto& [top, bottom] : edges) {
            CAPTURE(display, top, bottom);
            RustBackend tree;
            auto style = tree.default_style();
            style.size = size(100, 1000);
            const auto child = tree.new_node(style);
            style = tree.default_style();
            style.display = display;
            style.box_sizing = 0;
            style.size = size(300, 200);
            style.padding = vertical(top, bottom);
            const auto node = parent(tree, style, child);
            tree.compute(node, max_content);
            const auto layout = tree.layout(node);
            REQUIRE(layout.scrollable_overflow.bottom == top + 1000);
        }
    }
}

TEST_CASE("hand_written::scroll_size::leaf_content_size_includes_the_containers_own_padding",
          "[upstream][geometry]") {
    for (const auto& [top, bottom] : edges) {
        CAPTURE(top, bottom);
        const auto layout = measured_leaf(2, vertical(top, bottom));
        REQUIRE(layout.scrollable_overflow.bottom == top + 1000 + bottom);
    }
}

TEST_CASE("hand_written::scroll_size::leaf_content_size_excludes_the_own_padding_of_a_non_scroll_"
          "container",
          "[upstream][geometry]") {
    for (const auto& [top, bottom] : edges) {
        CAPTURE(top, bottom);
        const auto layout = measured_leaf(0, vertical(top, bottom));
        REQUIRE(layout.scrollable_overflow.bottom == top + 1000);
    }
}

TEST_CASE("hand_written::scrollable_overflow::wholly_unreachable_boxes_contribute_to_non_scroll_"
          "containers",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        CAPTURE(display);
        const auto layout = container_with_absolute_child(display, 0, 0, length(-100), automatic());
        REQUIRE(layout.scrollable_overflow.left == -100);
    }
}

TEST_CASE("hand_written::scrollable_overflow::absolute_only_containers_include_scroll_container_"
          "end_padding",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        CAPTURE(display);
        RustBackend tree;
        const auto abs = tree.new_node(
            oof_style(1, {length(0), automatic(), length(0), automatic()}, size(5, 5)));
        auto style = tree.default_style();
        style.display = display;
        style.position = 0;
        style.overflow_x = style.overflow_y = 2;
        style.padding = {length(0), length(20), length(0), length(30)};
        style.size = size(200, 200);
        const auto node = parent(tree, style, abs);
        tree.compute(node, max_content);
        const auto layout = tree.layout(node);
        REQUIRE(layout.scrollable_overflow.right == 20);
        REQUIRE(layout.scrollable_overflow.bottom == 30);
    }
}

TEST_CASE("hand_written::scrollable_overflow::scroll_container_in_one_axis_clips_both_axes",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        CAPTURE(display);
        const auto layout = nested_overflow_layout(display, 1, 0);
        REQUIRE(layout.scrollable_overflow.bottom == 200);
    }
}

TEST_CASE("hand_written::scrollable_overflow::clip_in_one_axis_does_not_clip_the_other",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        CAPTURE(display);
        const auto layout = nested_overflow_layout(display, 3, 0);
        REQUIRE(layout.scrollable_overflow.bottom == 200 + 50);
    }
}

TEST_CASE("hand_written::negative_available_space::negative_available_space::block_child_margins_"
          "exceed_container_width",
          "[upstream][geometry]") {
    assert_floored_at_zero(2, 0);
}
TEST_CASE("hand_written::negative_available_space::negative_available_space::flex_column_child_"
          "margins_exceed_container_width",
          "[upstream][geometry]") {
    assert_floored_at_zero(0, 1);
}
TEST_CASE("hand_written::negative_available_space::negative_available_space::grid_child_margins_"
          "exceed_container_width",
          "[upstream][geometry]") {
    assert_floored_at_zero(1, 0);
}
TEST_CASE("hand_written::position::position_variants::static_and_sticky_ignore_inset",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        for (const auto position : {3u, 4u}) { // Static, Sticky
            CAPTURE(display, position);
            require_location(leaf_with_inset(position, display), 0, 0);
        }
        CAPTURE(display);
        require_location(leaf_with_inset(0, display), 5, 7); // Relative
    }
}
TEST_CASE("hand_written::position::position_variants::sticky_is_positioned_but_in_flow",
          "[upstream][geometry]") {
    REQUIRE(RustBackend::position_is_positioned(4));
    REQUIRE_FALSE(RustBackend::position_is_out_of_flow(4));
    REQUIRE_FALSE(RustBackend::position_is_positioned(3));
}
TEST_CASE("hand_written::scroll_size::scroll_height_does_not_depend_on_which_edge_the_border_is_on",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        CAPTURE(display);
        float top, bottom;
        scroller_with_padding(display, vertical(20, 0), vertical(0, 0), &top);
        scroller_with_padding(display, vertical(0, 20), vertical(0, 0), &bottom);
        REQUIRE(top == bottom);
    }
}
TEST_CASE("hand_written::scroll_size::scroll_height_is_the_content_beyond_the_padding_box",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        for (const auto& [top, bottom] : edges) {
            CAPTURE(display, top, bottom);
            float scroll_height;
            scroller_with_padding(display, vertical(top, bottom), vertical(0, 0), &scroll_height);
            const auto padding_box = 200 - top - bottom;
            REQUIRE(scroll_height == 1000 - padding_box);
        }
    }
}
TEST_CASE("hand_written::scroll_size::scroll_height_accounts_for_the_containers_own_padding",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        for (const auto& [top, bottom] : edges) {
            CAPTURE(display, top, bottom);
            float scroll_height;
            scroller_with_padding(display, vertical(0, 0), vertical(top, bottom), &scroll_height);
            const auto expected = (top + 1000 + bottom) - 200;
            REQUIRE(scroll_height == expected);
        }
    }
}
TEST_CASE("hand_written::scroll_size::grid_scrollable_overflow_includes_item_positions_in_"
          "overflowing_tracks",
          "[upstream][geometry]") {
    RustBackend tree;
    auto style = tree.default_style();
    style.size = size(100, 500);
    const auto c1 = tree.new_node(style);
    const auto c2 = tree.new_node(style);
    style = tree.default_style();
    style.display = 1;
    style.size = size(300, 200);
    style.overflow_x = style.overflow_y = 2;
    const auto node = parent(tree, style, c1, c2);
    tree.compute(node, max_content);
    REQUIRE(tree.layout(node).scrollable_overflow.bottom == 1000);
    REQUIRE(tree.scroll_height(node) == 1000 - 200);
}
TEST_CASE("hand_written::scroll_size::scroll_height_agrees_with_borders_and_padding_combined",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        CAPTURE(display);
        float scroll_height;
        scroller_with_padding(display, vertical(20, 20), vertical(20, 20), &scroll_height);
        const auto padding_box = 200 - 20 - 20;
        const auto expected = (20 + 1000 + 20) - padding_box;
        REQUIRE(scroll_height == expected);
    }
}
TEST_CASE("hand_written::scrollable_overflow::wholly_unreachable_boxes_are_excluded_for_scroll_"
          "containers",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        CAPTURE(display);
        float scroll_width;
        const auto layout =
            container_with_absolute_child(display, 2, 0, length(-100), automatic(), &scroll_width);
        require_zero_rect(layout.scrollable_overflow);
        REQUIRE(scroll_width == 0);
    }
}
TEST_CASE("hand_written::scrollable_overflow::partially_unreachable_boxes_contribute_their_whole_"
          "border_box",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        CAPTURE(display);
        float scroll_width;
        const auto layout =
            container_with_absolute_child(display, 2, 0, length(-25), automatic(), &scroll_width);
        REQUIRE(layout.scrollable_overflow.left == -25);
        REQUIRE(layout.scrollable_overflow.right == 25);
        REQUIRE(scroll_width == 0);
    }
}
TEST_CASE("hand_written::scrollable_overflow::end_side_overflow_is_captured_and_scrollable",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        CAPTURE(display);
        float scroll_width;
        const auto layout =
            container_with_absolute_child(display, 2, 0, length(200), automatic(), &scroll_width);
        REQUIRE(layout.scrollable_overflow.right == 200 + 50);
        REQUIRE(scroll_width == 50);
    }
}
TEST_CASE("hand_written::scrollable_overflow::wholly_unreachable_boxes_are_excluded_for_rtl_scroll_"
          "containers",
          "[upstream][geometry]") {
    for (const auto display : displays) {
        CAPTURE(display);
        float scroll_width;
        const auto layout =
            container_with_absolute_child(display, 2, 1, automatic(), length(-100), &scroll_width);
        require_zero_rect(layout.scrollable_overflow);
        REQUIRE(scroll_width == 0);
    }
}
TEST_CASE(
    "hand_written::scrollable_overflow::end_side_overflow_is_captured_for_rtl_scroll_containers",
    "[upstream][geometry]") {
    for (const auto display : displays) {
        CAPTURE(display);
        float scroll_width;
        const auto layout =
            container_with_absolute_child(display, 2, 1, length(-50), automatic(), &scroll_width);
        REQUIRE(layout.scrollable_overflow.right == 200 + 50);
        REQUIRE(scroll_width == 50);
    }
}
