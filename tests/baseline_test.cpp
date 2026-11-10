#include "support/rust_backend.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>
#include <span>

namespace taffy::test {
namespace {

constexpr std::uint32_t align_baseline = ORACLE_ALIGN_BASELINE;
constexpr std::uint32_t align_flex_start = ORACLE_ALIGN_FLEX_START;
constexpr OracleSizing max_content_space{{4, 0}, {4, 0}};

OracleStyle sized_style(float width, float height) {
    auto style = RustBackend::default_style();
    style.size = {{1, width}, {1, height}};
    return style;
}

OracleStyle block_style() {
    auto style = RustBackend::default_style();
    style.display = 2;
    return style;
}

OracleStyle block_box_style(float width, float height, bool hidden = false) {
    auto style = sized_style(width, height);
    style.display = 2;
    if (hidden) {
        style.overflow_x = 1;
        style.overflow_y = 1;
    }
    return style;
}

OracleStyle wrapper_style() {
    auto style = block_style();
    style.size = {{1, 40}, {0, 0}};
    style.padding = {{1, 0}, {1, 0}, {1, 0}, {1, 20}};
    return style;
}

OracleStyle flex_root_style(float height, std::uint32_t alignment = align_baseline) {
    auto style = sized_style(200, height);
    style.display = 0;
    style.flex_direction = 0;
    style.align_items = alignment;
    return style;
}

RustBackend::Measure baseline_measure(OracleSize size, OracleOptional first_baseline = {}) {
    // Match LayoutOutput::from_sizes_and_baselines(size, Rect::ZERO,
    // Baselines::from_first(...)): last is None, NOT a copy of first.
    // Deliberately ignore input and style; do not run native leaf sizing.
    return
        [size, first_baseline](RustBackend::NodeId, const OracleLayoutInput&, const OracleStyle&) {
            OracleLayoutOutput output{};
            output.size = size;
            output.first_baseline = first_baseline;
            return output;
        };
}

RustBackend::NodeId with_children(RustBackend& tree, const OracleStyle& style,
                                  std::initializer_list<RustBackend::NodeId> children) {
    const auto node = tree.new_node(style);
    tree.set_children(node,
                      std::span<const RustBackend::NodeId>(children.begin(), children.size()));
    return node;
}

} // namespace

TEST_CASE("baseline::baseline::flex_baseline_alignment_uses_measure_function_baseline",
          "[baseline]") {
    RustBackend tree;
    // Child A: 50x50 with baseline 40; child B: 30x30 with baseline 20.
    const auto child_a =
        tree.new_node(RustBackend::default_style(), baseline_measure({50, 50}, {1, 40}));
    const auto child_b =
        tree.new_node(RustBackend::default_style(), baseline_measure({30, 30}, {1, 20}));
    const auto root = with_children(tree, flex_root_style(100), {child_a, child_b});

    tree.compute(root, max_content_space);

    const auto layout_a = tree.layout(child_a);
    const auto layout_b = tree.layout(child_b);
    CHECK(layout_a.y == 0.0f);
    CHECK(layout_b.y == 20.0f);
    CHECK(layout_a.y + 40.0f == layout_b.y + 20.0f);
}

TEST_CASE(
    "baseline::baseline::block_scroll_container_child_without_baseline_synthesizes_from_border_box",
    "[baseline]") {
    RustBackend tree;
    // An empty hidden-overflow 30x30 block has no intrinsic baseline.
    const auto scroll_child =
        tree.new_node(block_box_style(30, 30, true), baseline_measure({30, 30}));
    // Bottom padding makes the wrapper's bottom (50) differ from the child's (30).
    const auto item_a = with_children(tree, wrapper_style(), {scroll_child});
    const auto item_b =
        tree.new_node(RustBackend::default_style(), baseline_measure({40, 80}, {1, 10}));
    const auto root = with_children(tree, flex_root_style(120), {item_a, item_b});

    tree.compute(root, max_content_space);

    // The synthesized child baseline (30), not the wrapper bottom (50), propagates.
    CHECK(tree.layout(item_a).y == 0.0f);
    CHECK(tree.layout(item_b).y == 20.0f);
}

TEST_CASE(
    "baseline::baseline::block_non_scroll_container_child_without_baseline_contributes_nothing",
    "[baseline]") {
    RustBackend tree;
    // The otherwise-identical block keeps native overflow: visible.
    const auto plain_child = tree.new_node(block_box_style(30, 30), baseline_measure({30, 30}));
    const auto item_a = with_children(tree, wrapper_style(), {plain_child});
    const auto item_b =
        tree.new_node(RustBackend::default_style(), baseline_measure({40, 80}, {1, 10}));
    const auto root = with_children(tree, flex_root_style(120), {item_a, item_b});

    tree.compute(root, max_content_space);

    // No child baseline propagates; flex synthesizes at the wrapper bottom (50).
    CHECK(tree.layout(item_a).y == 0.0f);
    CHECK(tree.layout(item_b).y == 40.0f);
}

TEST_CASE("baseline::baseline::block_scroll_container_child_baseline_is_clamped_to_border_box",
          "[baseline]") {
    RustBackend tree;
    // Content baseline 48 overflows the 30px-tall hidden-overflow block.
    const auto content = tree.new_node(block_style(), baseline_measure({30, 60}, {1, 48}));
    const auto scroll_child = with_children(tree, block_box_style(30, 30, true), {content});
    const auto item_a = with_children(tree, wrapper_style(), {scroll_child});
    const auto item_b =
        tree.new_node(RustBackend::default_style(), baseline_measure({40, 80}, {1, 10}));
    const auto root = with_children(tree, flex_root_style(120), {item_a, item_b});

    tree.compute(root, max_content_space);

    // The child's border-box height clamps baseline 48 to 30.
    CHECK(tree.layout(item_a).y == 0.0f);
    CHECK(tree.layout(item_b).y == 20.0f);
}

TEST_CASE("baseline::baseline::flex_baseline_is_ignored_when_alignment_is_not_baseline",
          "[baseline]") {
    RustBackend tree;
    const auto child_a =
        tree.new_node(RustBackend::default_style(), baseline_measure({50, 50}, {1, 40}));
    const auto child_b =
        tree.new_node(RustBackend::default_style(), baseline_measure({30, 30}, {1, 20}));
    const auto root =
        with_children(tree, flex_root_style(100, align_flex_start), {child_a, child_b});

    tree.compute(root, max_content_space);

    CHECK(tree.layout(child_a).y == 0.0f);
    CHECK(tree.layout(child_b).y == 0.0f);
}

} // namespace taffy::test
