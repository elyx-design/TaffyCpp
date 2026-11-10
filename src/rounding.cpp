#include <taffy/compute.hpp>
#include <taffy/numeric.hpp>

namespace taffy {
namespace {
void round_inner(LayoutTree& tree, NodeId node, float parent_x, float parent_y) {
    const auto source = tree.unrounded_layout(node);
    auto layout = source;
    const float x = parent_x + source.location.x;
    const float y = parent_y + source.location.y;
    const auto r = [](float value) {
        return layout_round(value);
    };
    layout.location = {r(x) - r(parent_x), r(y) - r(parent_y)};
    layout.size = {r(x + source.size.width) - r(x), r(y + source.size.height) - r(y)};
    layout.scrollbar_size = {r(source.scrollbar_size.width), r(source.scrollbar_size.height)};
    const auto round_edges = [&](Rect<float> edges) -> Rect<float> {
        return {r(x + edges.left) - r(x),
                r(x + source.size.width) - r(x + source.size.width - edges.right),
                r(y + edges.top) - r(y),
                r(y + source.size.height) - r(y + source.size.height - edges.bottom)};
    };
    layout.border = round_edges(source.border);
    layout.padding = round_edges(source.padding);
    const auto overflow = source.scrollable_overflow_rect;
    layout.scrollable_overflow_rect = {r(x + overflow.left) - r(x), r(x + overflow.right) - r(x),
                                       r(y + overflow.top) - r(y), r(y + overflow.bottom) - r(y)};
    tree.set_final_layout(node, layout);
    for (auto child : tree.child_ids(node))
        if (!tree.is_out_of_flow(child))
            round_inner(tree, child, x, y);
    for (auto child : tree.hoisted_child_ids(node))
        round_inner(tree, child, x, y);
}
} // namespace

void round_layout(LayoutTree& tree, NodeId root) {
    round_inner(tree, root, 0, 0);
}
} // namespace taffy
