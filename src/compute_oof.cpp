// Native port of pinned Taffy compute/oof.rs (0064bd54).
#include "block.hpp"
#include "compute_common.hpp"
#include "grid.hpp"
#include "oof.hpp"
#include "sizing.hpp"
#include <taffy/compute.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace taffy {
namespace {

float align_in_line(Line<float> container, float size, AxisStaticEdge edge) {
    switch (edge) {
    case AxisStaticEdge::Start:
        return container.start;
    case AxisStaticEdge::End:
        return container.end - size;
    case AxisStaticEdge::Center:
        return (container.start + container.end - size) / 2.0f;
    }
    throw TreePanic("invalid out-of-flow alignment edge");
}

} // namespace

namespace block_detail {
OofAlignment oof_alignment(AlignSelf alignment) {
    switch (alignment.keyword) {
    case AlignItemsKeyword::Normal:
        return OofAlignment::Normal;
    case AlignItemsKeyword::Stretch:
        return OofAlignment::Stretch;
    case AlignItemsKeyword::Center:
        return OofAlignment::Center;
    case AlignItemsKeyword::End:
    case AlignItemsKeyword::FlexEnd:
    case AlignItemsKeyword::SelfEnd:
        return OofAlignment::End;
    case AlignItemsKeyword::Start:
    case AlignItemsKeyword::FlexStart:
    case AlignItemsKeyword::SelfStart:
    case AlignItemsKeyword::Baseline:
        return OofAlignment::Start;
    }
    throw TreePanic("invalid out-of-flow self alignment");
}

AxisStaticEdge OofAxis::start_edge() const {
    return reversed ? AxisStaticEdge::End : AxisStaticEdge::Start;
}

AxisStaticEdge OofAxis::end_edge() const {
    return reversed ? AxisStaticEdge::Start : AxisStaticEdge::End;
}

AxisStaticEdge OofAxis::alignment_edge() const {
    if (alignment == OofAlignment::Center)
        return AxisStaticEdge::Center;
    return alignment == OofAlignment::End ? end_edge() : start_edge();
}

Line<float> OofAxis::inset_modified_containing_block() const {
    Line<float> imcb;
    AxisStaticEdge weaker_edge;
    if (inset.start && inset.end) {
        imcb = {*inset.start, cb_size - *inset.end};
        weaker_edge = end_edge();
    } else if (inset.start) {
        imcb = {*inset.start, cb_size};
        weaker_edge = AxisStaticEdge::End;
    } else if (inset.end) {
        imcb = {0.0f, cb_size - *inset.end};
        weaker_edge = AxisStaticEdge::Start;
    } else {
        const auto sp = static_position;
        switch (sp.align.keyword) {
        case AxisStaticEdge::Start:
            imcb = {sp.area.start, cb_size};
            break;
        case AxisStaticEdge::End:
            imcb = {0.0f, sp.area.end};
            break;
        case AxisStaticEdge::Center: {
            const float center = (sp.area.start + sp.area.end) / 2.0f;
            const float start_distance = center;
            const float end_distance = cb_size - center;
            imcb = std::abs(start_distance) <= std::abs(end_distance)
                       ? Line<float>{0.0f, 2.0f * start_distance}
                       : Line<float>{cb_size - 2.0f * end_distance, cb_size};
            break;
        }
        }
        weaker_edge = end_edge();
    }
    // Reduce the weaker inset if the insets overconstrain the area.
    if (imcb.end < imcb.start) {
        if (weaker_edge == AxisStaticEdge::Start)
            imcb.start = imcb.end;
        else
            imcb.end = imcb.start;
    }
    return imcb;
}

float OofAxis::inset_modified_containing_block_size() const {
    const auto imcb = inset_modified_containing_block();
    return imcb.end - imcb.start;
}

float OofAxis::stretch_fit_size() const {
    const auto imcb = inset_modified_containing_block();
    const float non_auto_margin_sum = margin.start.value_or(0.0f) + margin.end.value_or(0.0f);
    return f32_max(imcb.end - imcb.start - non_auto_margin_sum, 0.0f);
}

bool OofAxis::is_stretch_sized(bool is_replaced, bool is_table) const {
    return inset.start && inset.end &&
           (alignment == OofAlignment::Stretch ||
            (alignment == OofAlignment::Normal && !is_replaced && !is_table));
}

Line<float> OofAxis::resolve_margins(float size) const {
    const auto imcb = inset_modified_containing_block();
    const float free_space = imcb.end - imcb.start - size;
    if (margin.start && margin.end)
        return {*margin.start, *margin.end};
    if (!inset.start || !inset.end)
        return {margin.start.value_or(0.0f), margin.end.value_or(0.0f)};
    if (margin.start)
        return {*margin.start, free_space - *margin.start};
    if (margin.end)
        return {free_space - *margin.end, *margin.end};
    if (free_space < 0.0f && is_inline) {
        return reversed ? Line<float>{free_space, 0.0f} : Line<float>{0.0f, free_space};
    }
    return {free_space / 2.0f, free_space / 2.0f};
}

float OofAxis::position(float size, Line<float> resolved_margin) const {
    const auto imcb = inset_modified_containing_block();
    const float margin_box_size = size + resolved_margin.start + resolved_margin.end;
    if (!inset.start && !inset.end) {
        const auto sp = static_position;
        const float offset =
            sp.align.safety == AlignmentSafety::Safe && margin_box_size > imcb.end - imcb.start
                ? align_in_line(imcb, margin_box_size, start_edge())
                : align_in_line(sp.area, margin_box_size, sp.align.keyword);
        return offset + resolved_margin.start;
    }
    if (!inset.end)
        return imcb.start + resolved_margin.start;
    if (!inset.start)
        return imcb.end - size - resolved_margin.end;
    if (!margin.start || !margin.end)
        return imcb.start + resolved_margin.start;

    const float imcb_size = imcb.end - imcb.start;
    const auto edge = alignment_edge();
    float margin_box_start;
    if (safety == AlignmentSafety::Unsafe) {
        margin_box_start = align_in_line(imcb, margin_box_size, edge);
    } else if (safety == AlignmentSafety::Safe) {
        margin_box_start =
            align_in_line(imcb, margin_box_size, margin_box_size > imcb_size ? start_edge() : edge);
    } else if (margin_box_size <= imcb_size || alignment == OofAlignment::Normal) {
        margin_box_start = align_in_line(imcb, margin_box_size, edge);
    } else {
        // Default absolute overflow safety uses the union of the IMCB and CB,
        // extended indefinitely in the container's scrollable direction.
        Line<float> limit{f32_min(imcb.start, 0.0f), f32_max(imcb.end, cb_size)};
        if (is_scroll_axis) {
            if (reversed)
                limit.start = -std::numeric_limits<float>::infinity();
            else
                limit.end = std::numeric_limits<float>::infinity();
        }
        if (margin_box_size <= limit.end - limit.start) {
            const float start = align_in_line(imcb, margin_box_size, edge);
            margin_box_start = f32_max(f32_min(start, limit.end - margin_box_size), limit.start);
        } else {
            margin_box_start = align_in_line(limit, margin_box_size, start_edge());
        }
    }
    return margin_box_start + resolved_margin.start;
}
} // namespace block_detail

namespace {
using block_detail::oof_alignment;
using block_detail::OofAlignment;
using block_detail::OofAxis;

OofPositioningArea grid_positioning_area(const DetailedGridInfo& info, const Style& child,
                                         Direction direction, OofPositioningArea area) {
    // Use the grid metadata resolver for both ordinary and hoisted OOF items.
    // An implicit edge outside the grid is auto, not a zero-sized grid area.
    const auto rect =
        grid::resolve_absolute_grid_area(info, child.grid_row, child.grid_column, direction,
                                         {area.offset.x, area.offset.x + area.size.width,
                                          area.offset.y, area.offset.y + area.size.height});
    return {{rect.right - rect.left, rect.bottom - rect.top}, {rect.left, rect.top}};
}

struct OofBoxLayout {
    Layout layout;
    OofCandidates surfaced;
    Point<Overflow> overflow;
    Contain contain;
};

OofBoxLayout
layout_oof_box_impl(LayoutTree& tree, OofCandidate candidate, OofPositioningArea area,
                    Direction direction, Point<Overflow> container_overflow,
                    std::optional<NodeId> grid_owner,
                    const std::function<void(LayoutTree&, const LayoutInput&)>& before_layout) {
    // Copy the style view before calc/measurement/layout callbacks can run.
    const auto child = tree.oof_item_style(candidate.node);
    if (grid_owner) {
        if (const auto* grid =
                std::get_if<DetailedGridInfo>(&tree.detailed_layout_info(*grid_owner)))
            area = grid_positioning_area(*grid, child, direction, area);
    }
    const CalcResolver calc = [&](Calc value, float basis) {
        return tree.resolve_calc(value, basis);
    };
    const auto aspect_ratio = child.aspect_ratio;
    const auto margin = child.margin.maybe_resolve(area.size.width, calc);
    const auto padding = child.padding.resolve_or_zero(area.size.width, calc);
    const auto border = child.border.resolve_or_zero(area.size.width, calc);
    const auto padding_border_sum = (padding + border).sum_axes();
    const auto adjustment =
        child.box_sizing == BoxSizing::ContentBox ? padding_border_sum : Size<float>{};
    const auto inset = child.inset.maybe_resolve(area.size, calc);
    const auto style_size = child.size.maybe_resolve(area.size, calc)
                                .maybe_apply_aspect_ratio(aspect_ratio)
                                .maybe_add(adjustment);
    const auto some = [](float value) {
        return std::optional<float>{value};
    };
    auto min_size = child.min_size.maybe_resolve(area.size, calc)
                        .maybe_apply_aspect_ratio(aspect_ratio)
                        .maybe_add(adjustment)
                        .or_(padding_border_sum.map(some))
                        .maybe_max(padding_border_sum);
    const auto max_size = child.max_size.maybe_resolve(area.size, calc)
                              .maybe_apply_aspect_ratio(aspect_ratio)
                              .maybe_add(adjustment);
    auto known_dimensions = style_size.maybe_clamp(min_size, max_size);
    const auto justify_self = child.justify_self.value_or(AlignSelf::normal())
                                  .resolve_self_relative(child.direction, direction, true);
    const auto align_self = child.align_self.value_or(AlignSelf::normal())
                                .resolve_self_relative(child.direction, direction, false);
    auto static_position = candidate.static_position;
    static_position.x.area = static_position.x.area.map([&](float value) {
        return value - area.offset.x;
    });
    static_position.y.area = static_position.y.area.map([&](float value) {
        return value - area.offset.y;
    });
    const OofAxis horizontal{area.size.width,
                             {inset.left, inset.right},
                             {margin.left, margin.right},
                             static_position.x,
                             oof_alignment(justify_self),
                             justify_self.safety,
                             true,
                             is_rtl(direction),
                             is_scroll_container(container_overflow.x)};
    const OofAxis vertical{area.size.height,
                           {inset.top, inset.bottom},
                           {margin.top, margin.bottom},
                           static_position.y,
                           oof_alignment(align_self),
                           align_self.safety,
                           false,
                           false,
                           is_scroll_container(container_overflow.y)};
    const Size<float> stretch_fit_size{horizontal.stretch_fit_size(), vertical.stretch_fit_size()};
    auto available_size = stretch_fit_size;
    if (child.is_table()) {
        const auto non_auto_margin = margin.map([](auto value) {
            return value.value_or(0.0f);
        });
        available_size = available_size.f32_min(
            {f32_max(area.size.width - non_auto_margin.horizontal_axis_sum(), 0.0f),
             f32_max(area.size.height - non_auto_margin.vertical_axis_sum(), 0.0f)});
    }
    const auto available_space =
        available_size.maybe_clamp(min_size, max_size).map(AvailableSpace::definite);
    if (child.size.width.is_sizing_keyword() || child.size.height.is_sizing_keyword()) {
        resolve_absolute_sizing_keywords(tree, candidate.node, known_dimensions, child.size,
                                         area.size, inset, margin, SizingMode::ContentSize);
        known_dimensions =
            known_dimensions.maybe_apply_aspect_ratio(aspect_ratio).maybe_clamp(min_size, max_size);
    }
    const bool stretch_width = !known_dimensions.width &&
                               horizontal.is_stretch_sized(child.is_replaced(), child.is_table());
    const bool stretch_height = !known_dimensions.height &&
                                vertical.is_stretch_sized(child.is_replaced(), child.is_table());
    const bool both_explicit_stretch = horizontal.alignment == OofAlignment::Stretch &&
                                       vertical.alignment == OofAlignment::Stretch;
    const auto stretch_aspect_ratio = stretch_width && stretch_height && both_explicit_stretch
                                          ? std::optional<float>{}
                                          : aspect_ratio;
    if (stretch_width) {
        if (child.is_table())
            min_size.width = f32_max(min_size.width.value_or(0.0f), stretch_fit_size.width);
        else {
            known_dimensions.width = stretch_fit_size.width;
            known_dimensions = known_dimensions.maybe_apply_aspect_ratio(stretch_aspect_ratio)
                                   .maybe_clamp(min_size, max_size);
        }
    }
    if (!known_dimensions.height && stretch_height) {
        if (child.is_table())
            min_size.height = f32_max(min_size.height.value_or(0.0f), stretch_fit_size.height);
        else {
            known_dimensions.height = stretch_fit_size.height;
            known_dimensions = known_dimensions.maybe_apply_aspect_ratio(stretch_aspect_ratio)
                                   .maybe_clamp(min_size, max_size);
        }
    }
    Size<float> final_size;
    if (known_dimensions.both_axis_defined())
        final_size = {*known_dimensions.width, *known_dimensions.height};
    else {
        const auto measured =
            tree.measure_child_size_both(candidate.node, known_dimensions, area.size.map(some),
                                         available_space, SizingMode::ContentSize, {false, false});
        final_size = known_dimensions.unwrap_or(measured);
    }
    final_size = final_size.maybe_clamp(min_size, max_size);
    const LayoutInput input{
        .run_mode = RunMode::PerformLayout,
        .sizing_mode = SizingMode::ContentSize,
        .axis = RequestedAxis::Both,
        .known_dimensions = final_size.map(some),
        .known_dimensions_are_definite = {true, true},
        .parent_size = area.size.map(some),
        .available_space = available_space,
        .vertical_margins_are_collapsible = {false, false},
    };
    if (before_layout)
        before_layout(tree, input);
    auto output = tree.compute_child_layout(candidate.node, input);
    const auto horizontal_margin = horizontal.resolve_margins(final_size.width);
    const auto vertical_margin = vertical.resolve_margins(final_size.height);
    Layout layout{
        .order = candidate.order,
        .location = {horizontal.position(final_size.width, horizontal_margin) + area.offset.x,
                     vertical.position(final_size.height, vertical_margin) + area.offset.y},
        .size = final_size,
        .scrollable_overflow_rect = output.scrollable_overflow_rect,
        // Scrollbars consume space in the opposite axis.
        .scrollbar_size = {child.overflow.y == Overflow::Scroll ? child.scrollbar_width : 0.0f,
                           child.overflow.x == Overflow::Scroll ? child.scrollbar_width : 0.0f},
        .border = border,
        .padding = padding,
        .margin = {horizontal_margin.start, horizontal_margin.end, vertical_margin.start,
                   vertical_margin.end},
    };
    tree.set_unrounded_layout(candidate.node, layout);
    return {layout, output.oof_candidates.take(), child.overflow, child.contain};
}
} // namespace

// Root integration seam: no grid owner for the initial containing block, and a
// hook immediately before final layout to inspect the exact cache key. Surfaced
// anchors remain relative to this box's border box. No hoisted list is modified.
Layout layout_oof_box(LayoutTree& tree, OofCandidate candidate, Size<float> area_size,
                      Point<float> area_offset, Direction direction,
                      Point<Overflow> container_overflow, std::optional<NodeId> grid_owner,
                      const std::function<void(LayoutTree&, const LayoutInput&)>& before_layout,
                      OofCandidates& surfaced) {
    auto result = layout_oof_box_impl(tree, candidate, {area_size, area_offset}, direction,
                                      container_overflow, grid_owner, before_layout);
    surfaced = std::move(result.surfaced);
    return result.layout;
}

OofLayoutResult compute_oof_layout_for_area(LayoutTree& tree, NodeId geometry_owner,
                                            std::span<const OofCandidate> candidates,
                                            OofPositioningArea area, Direction direction,
                                            ContainingBlockClaims claims) {
    const auto container_overflow = tree.core_container_style(geometry_owner).overflow;
    const bool scroll_container =
        is_scroll_container(container_overflow.x) || is_scroll_container(container_overflow.y);
    OofLayoutResult result;
    std::vector<OofCandidate> worklist;
    for (const auto& candidate : candidates) {
        if (claims.for_position(candidate.position))
            worklist.push_back(candidate);
        else
            result.unclaimed.push(candidate);
    }
    // Re-sweep surfaced descendants without holding references into the worklist
    // across recursive layout or vector growth. Preserve upstream encounter order.
    for (std::size_t index = 0; index < worklist.size(); ++index) {
        const auto candidate = worklist[index];
        result.hoisted.push_back(candidate.node);
        auto box = layout_oof_box_impl(tree, candidate, area, direction, container_overflow,
                                       geometry_owner, {});
        box.surfaced.translate(box.layout.location);
        for (const auto& surfaced : box.surfaced) {
            if (claims.for_position(surfaced.position))
                worklist.push_back(surfaced);
            else
                result.unclaimed.push(surfaced);
        }
        const auto location = box.layout.location;
        const auto size = box.layout.size;
        const Point<float> relative_location{
            is_rtl(direction) ? area.size.width - (location.x - area.offset.x) - size.width
                              : location.x - area.offset.x,
            location.y - area.offset.y};
        result.scrollable_overflow_rect =
            result.scrollable_overflow_rect.united(compute_scrollable_overflow_contribution(
                relative_location, size, box.layout.scrollable_overflow_rect, box.overflow,
                box.contain, scroll_container));
    }
    return result;
}
} // namespace taffy
