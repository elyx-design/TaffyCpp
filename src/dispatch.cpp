#include "oof.hpp"
#include <taffy/compute.hpp>

namespace taffy {
LayoutOutput compute_block(LayoutTree& tree, NodeId node, const LayoutInput& input) {
    return compute_block(tree, node, input, nullptr);
}

LayoutOutput compute_dispatch(LayoutTree& tree, NodeId node, const LayoutInput& input) {
    if (input.run_mode == RunMode::PerformHiddenLayout) {
        tree.set_final_layout(node, {});
        return compute_hidden_layout(tree, node);
    }
    if (tree.style(node).display == Display::None)
        return compute_hidden_layout(tree, node);
    if (tree.child_count(node) == 0)
        return compute_leaf(tree, node, input);
    switch (tree.style(node).display) {
    case Display::Block:
    case Display::FlowRoot:
        return compute_block(tree, node, input);
    case Display::Flex:
        return compute_flex(tree, node, input);
    case Display::Grid:
        return compute_grid(tree, node, input);
    case Display::None:
        break;
    }
    throw UnsupportedAlgorithm("unknown display mode");
}

LayoutOutput compute_cached_layout(LayoutTree& tree, NodeId node, const LayoutInput& input,
                                   const ComputeFunction& compute) {
    if (auto result = tree.cache_get(node, input))
        return std::move(*result);
    auto result = compute(tree, node, input);
    tree.cache_store(node, input, result);
    return result;
}

LayoutOutput compute_cached_layout(LayoutTree& tree, NodeId node, const LayoutInput& input) {
    // compute_child_layout owns the custom-tree dispatch policy. In particular,
    // an intrinsic sizing query must not bypass a custom full-output callback.
    return tree.compute_child_layout(node, input);
}

LayoutOutput compute_hidden_layout(LayoutTree& tree, NodeId node) {
    tree.cache_clear(node);
    tree.clear_hoisted_children(node);
    tree.set_unrounded_layout(node, Layout{});
    for (auto child : tree.child_ids(node))
        tree.compute_child_layout(child, LayoutInput::HIDDEN);
    return LayoutOutput::HIDDEN;
}

void compute_oof_layout(LayoutTree& tree, NodeId node, LayoutOutput& output) {
    // Called only for fresh full layouts, not cache hits: even a new leaf
    // without a positioning area must discard its former container's IDs.
    tree.clear_hoisted_children(node);
    if (!output.oof_positioning_area)
        return;
    if (output.oof_candidates.empty())
        return;
    const auto& style = tree.core_container_style(node);
    auto result = compute_oof_layout_for_area(tree, node, output.oof_candidates.as_slice(),
                                              *output.oof_positioning_area, style.direction,
                                              tree.containing_block_claims(node));
    tree.add_hoisted_children(node, result.hoisted);
    output.oof_candidates = std::move(result.unclaimed);
    output.scrollable_overflow_rect =
        output.scrollable_overflow_rect.united(result.scrollable_overflow_rect);
}

Point<float> resolve_static_offset(Point<AxisStaticPosition> position, Size<float> size,
                                   Rect<float> margin) {
    const auto resolve = [](AxisStaticPosition sp, float length, Line<float> margins) {
        const auto outer = length + margins.start + margins.end;
        const auto keyword =
            sp.align.safety == AlignmentSafety::Safe && outer > sp.area.end - sp.area.start
                ? sp.align.fallback
                : sp.align.keyword;
        switch (keyword) {
        case AxisStaticEdge::Start:
            return sp.area.start + margins.start;
        case AxisStaticEdge::End:
            return sp.area.end - outer + margins.start;
        case AxisStaticEdge::Center:
            return (sp.area.start + sp.area.end - outer) / 2 + margins.start;
        }
        throw TreePanic("invalid static alignment edge");
    };
    return {resolve(position.x, size.width, {margin.left, margin.right}),
            resolve(position.y, size.height, {margin.top, margin.bottom})};
}
} // namespace taffy
