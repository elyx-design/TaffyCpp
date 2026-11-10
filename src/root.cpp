// Taffy 0.15.1 compute/mod.rs: in-flow roots and the initial containing block.
#include "block.hpp"
#include "oof.hpp"
#include <taffy/compute.hpp>
#include <taffy/numeric.hpp>

namespace taffy {
namespace {
using OptionalSize = Size<std::optional<float>>;

OptionalSize block_root_dimensions(const Style& style, OptionalSize parent,
                                   Size<AvailableSpace> space, const CalcResolver& calc) {
    const auto padding = style.padding.resolve_or_zero(parent.width, calc);
    const auto border = style.border.resolve_or_zero(parent.width, calc);
    const auto padding_border = (padding + border).sum_axes();
    const auto adjustment =
        style.box_sizing == BoxSizing::ContentBox ? padding_border : Size<float>{};
    const auto minimum = style.min_size.maybe_resolve(parent, calc)
                             .maybe_apply_aspect_ratio(style.aspect_ratio)
                             .maybe_add(adjustment);
    const auto maximum = style.max_size.maybe_resolve(parent, calc)
                             .maybe_apply_aspect_ratio(style.aspect_ratio)
                             .maybe_add(adjustment);
    const auto preferred = style.size.maybe_resolve(parent, calc)
                               .maybe_apply_aspect_ratio(style.aspect_ratio)
                               .maybe_add(adjustment)
                               .maybe_clamp(minimum, maximum);
    const auto fixed = minimum.zip_map(maximum, [](auto min, auto max) -> std::optional<float> {
        return min && max && *max <= *min ? min : std::nullopt;
    });
    return fixed.or_(preferred)
        .or_(OptionalSize{space.width.into_option(), std::nullopt})
        .maybe_max(padding_border);
}

Point<float> in_flow_root_location(const Style& style, OptionalSize parent, Size<float> size,
                                   Rect<float> margin, const CalcResolver& calc) {
    const bool rtl = is_rtl(style.direction);
    Point<float> location{
        rtl && parent.width ? *parent.width - size.width - margin.right : margin.left, margin.top};
    if (style.position != Position::Relative)
        return location;
    const auto left = style.inset.left.maybe_resolve(parent.width, calc);
    const auto right = style.inset.right.maybe_resolve(parent.width, calc);
    const auto top = style.inset.top.maybe_resolve(parent.height, calc);
    const auto bottom = style.inset.bottom.maybe_resolve(parent.height, calc);
    location.x += rtl ? (right ? -*right : left.value_or(0)) : (left ? *left : -right.value_or(0));
    location.y += top ? *top : -bottom.value_or(0);
    return location;
}

std::pair<Layout, OofCandidates> in_flow_root(LayoutTree& tree, NodeId root,
                                              Size<AvailableSpace> available, bool& cached) {
    const auto style = tree.core_container_style(root);
    const CalcResolver calc = [&tree](Calc value, float basis) {
        return tree.resolve_calc(value, basis);
    };
    const auto parent = available.into_options();
    const auto margin = style.margin.resolve_or_zero(parent.width, calc);
    const auto space = available.maybe_sub(margin.sum_axes()).maybe_max(Size<float>{});
    LayoutInput input;
    input.available_space = space;
    input.parent_size = parent;
    if (style.is_block() || style.display == Display::FlowRoot)
        input.known_dimensions = block_root_dimensions(style, parent, space, calc);
    cached = tree.cache_contains(root, input);
    auto output = tree.compute_child_layout(root, input);
    Layout layout;
    layout.location = in_flow_root_location(style, parent, output.size, margin, calc);
    layout.size = output.size;
    layout.scrollable_overflow_rect = output.scrollable_overflow_rect;
    layout.scrollbar_size = {style.overflow.y == Overflow::Scroll ? style.scrollbar_width : 0,
                             style.overflow.x == Overflow::Scroll ? style.scrollbar_width : 0};
    layout.padding = style.padding.resolve_or_zero(parent.width, calc);
    layout.border = style.border.resolve_or_zero(parent.width, calc);
    layout.margin = margin;
    tree.set_unrounded_layout(root, layout);
    return {layout, std::move(output.oof_candidates)};
}

std::pair<Layout, OofCandidates> out_of_flow_root(LayoutTree& tree, NodeId root, Size<float> area,
                                                  Position position, Direction direction,
                                                  bool& cached) {
    const bool rtl = is_rtl(direction);
    const auto inline_anchor = rtl ? area.width : 0.0f;
    const auto inline_edge = rtl ? AxisStaticEdge::End : AxisStaticEdge::Start;
    const OofCandidate candidate{root,
                                 0,
                                 position,
                                 {AxisStaticPosition::from_edge(inline_anchor, inline_edge),
                                  AxisStaticPosition::from_edge(0, AxisStaticEdge::Start)}};
    OofCandidates surfaced;
    const auto layout = layout_oof_box(
        tree, candidate, area, {}, direction, {Overflow::Visible, Overflow::Visible}, std::nullopt,
        [&cached, root](LayoutTree& current, const LayoutInput& input) {
            cached = current.cache_contains(root, input);
        },
        surfaced);
    return {layout, std::move(surfaced)};
}

OofPositioningArea initial_containing_area(const Layout& root, OptionalSize viewport) {
    const auto inset =
        root.border + Rect<float>{0, root.scrollbar_size.width, 0, root.scrollbar_size.height};
    const auto padding_box = root.size - inset.sum_axes();
    return {{viewport.width ? f32_max(*viewport.width - root.scrollbar_size.width, 0)
                            : padding_box.width,
             viewport.height ? f32_max(*viewport.height - root.scrollbar_size.height, 0)
                             : padding_box.height},
            {viewport.width ? -root.location.x : inset.left,
             viewport.height ? -root.location.y : inset.top}};
}
} // namespace

void compute_root_layout(LayoutTree& tree, NodeId root, Size<AvailableSpace> available) {
    const auto style = tree.core_container_style(root);
    // Hidden roots generate no box, even when absolute/fixed and explicitly sized.
    // Do not let ordinary root positioning overwrite the hidden layout with sizes,
    // margins, padding, or insets from the style.
    if (style.box_generation_mode() == BoxGenerationMode::None) {
        tree.compute_child_layout(root, LayoutInput::HIDDEN);
        tree.set_unrounded_layout(root, {});
        tree.set_final_layout(root, {});
        return;
    }
    const auto viewport = available.into_options();
    bool cached = false;
    auto [layout, candidates] =
        is_out_of_flow(style.position) && viewport.width && viewport.height
            ? out_of_flow_root(tree, root, {*viewport.width, *viewport.height}, style.position,
                               style.direction, cached)
            : in_flow_root(tree, root, available, cached);
    if (candidates.empty())
        return;
    auto result = compute_oof_layout_for_area(tree, root, candidates.as_slice(),
                                              initial_containing_area(layout, viewport),
                                              style.direction, ContainingBlockClaims::all());
    if (!result.unclaimed.empty())
        throw TreePanic("initial containing block must claim all candidates");
    // A cache hit retains hoisted IDs from the previous positioning pass.
    if (!cached)
        tree.add_hoisted_children(root, result.hoisted);
}
} // namespace taffy
