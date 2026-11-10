// Remaining translations from upstream tests/hand_written/measure.rs.
#include "support/measure_helpers.hpp"
#include <catch2/catch_test_macros.hpp>

#include <array>

namespace {
using taffy::test::RustBackend;
using taffy::test::TestNodeContext;

constexpr OracleSizing max_content{{4, 0}, {4, 0}};

RustBackend::NodeId measured(RustBackend& tree, const OracleStyle& style,
                             TestNodeContext& context) {
    return tree.new_node(
        style, [&context](auto node, const auto& input, const auto& callback_style) {
            return taffy::test::test_measure_function(input, node, &context, callback_style);
        });
}

// Upstream's context-free leaves still use test_measure_function, with None.
RustBackend::NodeId unmeasured(RustBackend& tree, const OracleStyle& style) {
    return tree.new_node(style, [](auto node, const auto& input, const auto& callback_style) {
        return taffy::test::test_measure_function(input, node, nullptr, callback_style);
    });
}

template <typename... Children>
RustBackend::NodeId parent(RustBackend& tree, const OracleStyle& style, Children... children) {
    const auto node = tree.new_node(style);
    tree.set_children(node, std::array{children...});
    return node;
}

void require_size(const OracleLayout& layout, float width, float height) {
    REQUIRE(layout.size.width == width);
    REQUIRE(layout.size.height == height);
}
} // namespace

TEST_CASE("measure::measure::measure_child_constraint_padding_parent", "[upstream][measure]") {
    RustBackend tree;
    auto context = TestNodeContext::fixed(100, 100);
    const auto child = measured(tree, tree.default_style(), context);
    auto style = tree.default_style();
    style.size.width = {1, 50};
    style.padding = {{1, 10}, {1, 10}, {1, 10}, {1, 10}};
    const auto node = parent(tree, style, child);
    tree.compute(node, max_content);

    REQUIRE(tree.layout(node).x == 0);
    REQUIRE(tree.layout(node).y == 0);
    require_size(tree.layout(node), 50, 120);
    REQUIRE(tree.layout(child).x == 10);
    REQUIRE(tree.layout(child).y == 10);
    require_size(tree.layout(child), 100, 100);
}

TEST_CASE("measure::measure::measure_child_with_flex_grow", "[upstream][measure]") {
    RustBackend tree;
    auto child_style = tree.default_style();
    child_style.size = {{1, 50}, {1, 50}};
    const auto child0 = unmeasured(tree, child_style);
    auto context = TestNodeContext::fixed(50, 50);
    child_style = tree.default_style();
    child_style.flex_grow = 1;
    const auto child1 = measured(tree, child_style, context);
    auto style = tree.default_style();
    style.size.width = {1, 100};
    const auto node = parent(tree, style, child0, child1);
    tree.compute(node, max_content);
    require_size(tree.layout(child1), 50, 50);
}

TEST_CASE("measure::measure::measure_child_with_flex_shrink", "[upstream][measure]") {
    RustBackend tree;
    auto child_style = tree.default_style();
    child_style.size = {{1, 50}, {1, 50}};
    child_style.flex_shrink = 0;
    const auto child0 = unmeasured(tree, child_style);
    auto context = TestNodeContext::fixed(100, 50);
    const auto child1 = measured(tree, tree.default_style(), context);
    auto style = tree.default_style();
    style.size.width = {1, 100};
    const auto node = parent(tree, style, child0, child1);
    tree.compute(node, max_content);
    require_size(tree.layout(child1), 100, 50);
}

TEST_CASE("measure::measure::remeasure_child_after_growing", "[upstream][measure]") {
    RustBackend tree;
    auto child_style = tree.default_style();
    child_style.size = {{1, 50}, {1, 50}};
    const auto child0 = unmeasured(tree, child_style);
    auto context = TestNodeContext::aspect_ratio(10, 2);
    child_style = tree.default_style();
    child_style.flex_grow = 1;
    const auto child1 = measured(tree, child_style, context);
    auto style = tree.default_style();
    style.size.width = {1, 100};
    style.align_items = ORACLE_ALIGN_START; // AlignItems::START, not FLEX_START.
    const auto node = parent(tree, style, child0, child1);
    tree.compute(node, max_content);
    require_size(tree.layout(child1), 50, 100);
}

TEST_CASE("measure::measure::remeasure_child_after_shrinking", "[upstream][measure]") {
    RustBackend tree;
    auto child_style = tree.default_style();
    child_style.size = {{1, 50}, {1, 50}};
    child_style.flex_shrink = 0;
    const auto child0 = unmeasured(tree, child_style);
    auto context = TestNodeContext::aspect_ratio(100, 2);
    const auto child1 = measured(tree, tree.default_style(), context);
    auto style = tree.default_style();
    style.size.width = {1, 100};
    style.align_items = ORACLE_ALIGN_START; // AlignItems::START.
    const auto node = parent(tree, style, child0, child1);
    tree.compute(node, max_content);
    require_size(tree.layout(child1), 100, 200);
}

TEST_CASE("measure::measure::remeasure_child_after_stretching", "[upstream][measure]") {
    RustBackend tree;
    const auto child =
        tree.new_node(tree.default_style(), [](auto, const auto& input, const auto& style) {
            return RustBackend::leaf(style, input, [](OracleKnown known, OracleSizing) {
                const float height = known.height.present ? known.height.value : 50;
                const float width = known.width.present ? known.width.value : height;
                return OracleSize{width, height};
            });
        });
    auto style = tree.default_style();
    style.size = {{1, 100}, {1, 100}};
    const auto node = parent(tree, style, child);
    tree.compute(node, max_content);
    require_size(tree.layout(child), 100, 100);
}

TEST_CASE("measure::measure::width_overrides_measure", "[upstream][measure]") {
    RustBackend tree;
    auto context = TestNodeContext::fixed(100, 100);
    auto style = tree.default_style();
    style.size.width = {1, 50};
    const auto child = measured(tree, style, context);
    const auto node = parent(tree, tree.default_style(), child);
    tree.compute(node, max_content);
    require_size(tree.layout(child), 50, 100);
}

TEST_CASE("measure::measure::height_overrides_measure", "[upstream][measure]") {
    RustBackend tree;
    auto context = TestNodeContext::fixed(100, 100);
    auto style = tree.default_style();
    style.size.height = {1, 50};
    const auto child = measured(tree, style, context);
    const auto node = parent(tree, tree.default_style(), child);
    tree.compute(node, max_content);
    require_size(tree.layout(child), 100, 50);
}

TEST_CASE("measure::measure::flex_basis_overrides_measure", "[upstream][measure]") {
    RustBackend tree;
    auto child_style = tree.default_style();
    child_style.flex_basis = {1, 50};
    child_style.flex_grow = 1;
    const auto child0 = unmeasured(tree, child_style);
    auto context = TestNodeContext::fixed(100, 100);
    const auto child1 = measured(tree, child_style, context);
    auto style = tree.default_style();
    style.size = {{1, 200}, {1, 100}};
    const auto node = parent(tree, style, child0, child1);
    tree.compute(node, max_content);
    require_size(tree.layout(child0), 100, 100);
    require_size(tree.layout(child1), 100, 100);
}

TEST_CASE("measure::measure::stretch_overrides_measure", "[upstream][measure]") {
    RustBackend tree;
    auto context = TestNodeContext::fixed(50, 50);
    const auto child = measured(tree, tree.default_style(), context);
    auto style = tree.default_style();
    style.size = {{1, 100}, {1, 100}};
    const auto node = parent(tree, style, child);
    tree.compute(node, max_content);
    require_size(tree.layout(child), 50, 100);
}

TEST_CASE("measure::measure::explicit_min_main_size_skips_min_content_measurement",
          "[upstream][measure]") {
    RustBackend tree;
    auto style = tree.default_style();
    style.min_size.width = {1, 10};
    const auto child = tree.new_node(style, [](auto, const auto& input, const auto&) {
        // Preserve the assertion on every outer measurement invocation,
        // not just inside an intrinsic callback that leaf sizing may skip.
        REQUIRE(input.available_space.width.tag != 3); // MinContent.
        // Upstream deliberately returns LayoutOutput::from_outer_size,
        // rather than running compute_leaf_layout for this callback.
        OracleLayoutOutput output{};
        output.size = {10, 10};
        return output;
    });
    const auto root = parent(tree, tree.default_style(), child);
    tree.compute(root, max_content);
}

TEST_CASE("measure::measure::measure_absolute_child", "[upstream][measure]") {
    RustBackend tree;
    auto context = TestNodeContext::fixed(50, 50);
    auto child_style = tree.default_style();
    child_style.position = 1; // Position::Absolute.
    const auto child = measured(tree, child_style, context);
    auto style = tree.default_style();
    style.size = {{1, 100}, {1, 100}};
    const auto node = parent(tree, style, child);
    tree.compute(node, max_content);
    require_size(tree.layout(child), 50, 50);
}

TEST_CASE("measure::measure::ignore_invalid_measure", "[upstream][measure]") {
    RustBackend tree;
    auto child_style = tree.default_style();
    child_style.flex_grow = 1;
    const auto child = unmeasured(tree, child_style);
    auto style = tree.default_style();
    style.size = {{1, 100}, {1, 100}};
    const auto node = parent(tree, style, child);
    tree.compute(node, max_content);
    require_size(tree.layout(child), 100, 100);
}
