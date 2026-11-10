#include "internal.hpp"

#include <stdexcept>

namespace taffy::flex_detail {

void distribute_remaining_free_space(std::vector<FlexLine>& lines, const AlgoConstants& c) {
    for (auto& line : lines) {
        float item_space = -0.0f; // Rust's Sum<f32> identity preserves negative zero.
        std::size_t auto_count = 0;
        for (const auto& child : line.items) {
            item_space += child.outer_target_size.main(c.dir);
            auto_count += child.margin_is_auto.main_start(c.dir);
            auto_count += child.margin_is_auto.main_end(c.dir);
        }
        const float used = sum_axis_gaps(c.gap.main(c.dir), line.items.size()) + item_space;
        float free = c.inner_container_size.main(c.dir) - used;
        if (free > 0.0f && auto_count > 0) {
            const float margin = free / static_cast<float>(auto_count);
            for (auto& child : line.items) {
                if (child.margin_is_auto.main_start(c.dir)) {
                    if (c.is_row)
                        child.margin.left = margin;
                    else
                        child.margin.top = margin;
                }
                if (child.margin_is_auto.main_end(c.dir)) {
                    if (c.is_row)
                        child.margin.right = margin;
                    else
                        child.margin.bottom = margin;
                }
            }
            free = 0.0f;
        }
        const bool reverse = is_reverse(c.dir);
        const auto mode = apply_alignment_fallback(free, line.items.size(), c.justify_content);
        for (std::size_t i = 0; i < line.items.size(); ++i) {
            auto& child = line.items[reverse ? line.items.size() - 1 - i : i];
            child.offset_main = compute_alignment_offset(free, line.items.size(), c.gap.main(c.dir),
                                                         mode, reverse, i == 0);
        }
    }
}

namespace {
void calculate_flex_item(LayoutTree& tree, FlexItem& item, float& total_main, float total_cross,
                         float line_cross, Rect<float>& overflow, const AlgoConstants& c) {
    LayoutInput input;
    input.run_mode = RunMode::PerformLayout;
    input.sizing_mode = SizingMode::ContentSize;
    input.axis = RequestedAxis::Both;
    input.known_dimensions = item.target_size.map([](float size) {
        return std::optional<float>{size};
    });
    input.known_dimensions_are_definite = item_known_dimension_definiteness(c, item);
    input.parent_size = c.node_inner_size;
    input.available_space = (c.container_size - item.margin.sum_axes()).map([](float size) {
        return AvailableSpace::definite(f32_max(size, 0.0f));
    });
    auto output = tree.compute_child_layout(item.node, input);
    const auto size = output.size;
    const bool rtl_row = is_row(c.dir) && c.layout_direction == Direction::Rtl;
    const bool rtl_column = is_column(c.dir) && c.layout_direction == Direction::Rtl;
    const float effective_line_cross = rtl_column ? 0.0f : line_cross;
    const float main = rtl_row ? total_main - item.offset_main - item.margin.main_end(c.dir) -
                                     item.relative_inset.main(c.dir) - size.width
                               : total_main + item.offset_main + item.margin.main_start(c.dir) +
                                     item.relative_inset.main(c.dir);
    const float cross = total_cross + item.offset_cross + effective_line_cross +
                        item.margin.cross_start(c.dir) + item.relative_inset.cross(c.dir);
    const float inner_baseline =
        child_first_baseline(output.baselines.first, size.height, item.overflow.y)
            .value_or(size.height);
    if (is_row(c.dir)) {
        const float baseline_cross =
            total_cross + item.offset_cross + effective_line_cross + item.margin.cross_start(c.dir);
        item.baseline = baseline_cross + inner_baseline;
    } else {
        const float baseline_main = total_main + item.offset_main + item.margin.main_start(c.dir);
        item.baseline = baseline_main + inner_baseline;
    }
    const Point<float> location =
        is_row(c.dir) ? Point<float>{main, cross} : Point<float>{cross, main};
    Layout layout;
    layout.order = item.order;
    layout.location = location;
    layout.size = size;
    layout.scrollable_overflow_rect = output.scrollable_overflow_rect;
    layout.scrollbar_size = {item.overflow.y == Overflow::Scroll ? item.scrollbar_width : 0.0f,
                             item.overflow.x == Overflow::Scroll ? item.scrollbar_width : 0.0f};
    layout.border = item.border;
    layout.padding = item.padding;
    layout.margin = item.margin;
    tree.set_unrounded_layout(item.node, layout);
    item.oof_candidates = output.oof_candidates.take();
    if (!item.oof_candidates.empty())
        item.oof_candidates.translate(location);
    if (rtl_row)
        total_main -= item.offset_main + item.margin.main_axis_sum(c.dir) + size.main(c.dir);
    else
        total_main += item.offset_main + item.margin.main_axis_sum(c.dir) + size.main(c.dir);
    const Point<float> contribution =
        c.layout_direction == Direction::Rtl
            ? Point<float>{c.container_size.width - (location.x + size.width) - c.border.right,
                           location.y - c.border.top}
            : Point<float>{location.x - c.border.left, location.y - c.border.top};
    overflow = overflow.united(compute_scrollable_overflow_contribution(
        contribution, size, output.scrollable_overflow_rect, item.overflow, item.contain,
        c.is_scroll_container));
}

void calculate_layout_line(LayoutTree& tree, FlexLine& line, float& total_cross,
                           Rect<float>& overflow, const AlgoConstants& c) {
    float total_main = c.layout_direction == Direction::Rtl && is_row(c.dir)
                           ? c.container_size.width - c.content_box_inset.main_end(c.dir)
                           : c.content_box_inset.main_start(c.dir);
    const bool rtl_column = c.layout_direction == Direction::Rtl && is_column(c.dir);
    if (rtl_column)
        total_cross -= line.offset_cross + line.cross_size;
    for (std::size_t i = 0; i < line.items.size(); ++i) {
        auto& item = line.items[is_reverse(c.dir) ? line.items.size() - 1 - i : i];
        calculate_flex_item(tree, item, total_main, total_cross, line.offset_cross, overflow, c);
    }
    if (!rtl_column)
        total_cross += line.offset_cross + line.cross_size;
}
} // namespace

Rect<float> final_layout_pass(LayoutTree& tree, std::vector<FlexLine>& lines,
                              const AlgoConstants& c) {
    float total_cross = c.is_column && c.layout_direction == Direction::Rtl
                            ? c.container_size.width - c.content_box_inset.cross_end(c.dir)
                            : c.content_box_inset.cross_start(c.dir);
    Rect<float> overflow{};
    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto& line = lines[c.is_wrap_reverse ? lines.size() - 1 - i : i];
        calculate_layout_line(tree, line, total_cross, overflow, c);
    }
    if (c.is_scroll_container) {
        overflow.right += c.layout_direction == Direction::Rtl
                              ? c.content_box_inset.left - c.border.left - c.scrollbar_gutter.x
                              : c.content_box_inset.right - c.border.right - c.scrollbar_gutter.x;
        overflow.bottom += c.content_box_inset.bottom - c.border.bottom - c.scrollbar_gutter.y;
    }
    return overflow;
}

void collect_oof_candidates(LayoutTree& tree, NodeId node, const AlgoConstants& c,
                            std::vector<FlexLine>& lines, OofCandidates& candidates) {
    std::vector<FlexItem*> bubbled;
    for (auto& line : lines)
        for (auto& item : line.items)
            if (!item.oof_candidates.empty())
                bubbled.push_back(&item);
    std::size_t next = 0;
    const bool main_rtl = c.is_row && c.layout_direction == Direction::Rtl;
    const bool cross_rtl = !c.is_row && c.layout_direction == Direction::Rtl;
    const bool main_reverse = is_reverse(c.dir) != main_rtl;
    const bool cross_reverse = c.is_wrap_reverse != cross_rtl;
    const Line<float> main_area{c.content_box_inset.main_start(c.dir),
                                c.container_size.main(c.dir) - c.content_box_inset.main_end(c.dir)};
    const Line<float> cross_area{c.content_box_inset.cross_start(c.dir),
                                 c.container_size.cross(c.dir) -
                                     c.content_box_inset.cross_end(c.dir)};
    // Default indexed access snapshots child_ids() on every call. Take one
    // snapshot for this pass so custom adapters remain linear too.
    const auto children = tree.child_ids(node);
    for (std::size_t order = 0; order < children.size(); ++order) {
        const auto child = children[order];
        const auto style = tree.flex_child_style(child);
        if (style.box_generation_mode() == BoxGenerationMode::None)
            continue;
        if (!is_out_of_flow(style.position)) {
            if (next < bubbled.size() && bubbled[next]->order == static_cast<std::uint32_t>(order))
                candidates.append(bubbled[next++]->oof_candidates);
            continue;
        }
        const auto alignment =
            resolve_normal_alignment(style.align_self.value_or(c.align_items))
                .resolve_self_relative(style.direction, c.layout_direction, c.is_column);
        AxisStaticEdge main_edge;
        switch (c.justify_content.keyword) {
        case AlignContentKeyword::Start:
            main_edge = main_rtl ? AxisStaticEdge::End : AxisStaticEdge::Start;
            break;
        case AlignContentKeyword::End:
            main_edge = main_rtl ? AxisStaticEdge::Start : AxisStaticEdge::End;
            break;
        case AlignContentKeyword::FlexEnd:
            main_edge = main_reverse ? AxisStaticEdge::Start : AxisStaticEdge::End;
            break;
        case AlignContentKeyword::SpaceAround:
        case AlignContentKeyword::SpaceEvenly:
        case AlignContentKeyword::Center:
            main_edge = AxisStaticEdge::Center;
            break;
        default:
            main_edge = main_reverse ? AxisStaticEdge::End : AxisStaticEdge::Start;
            break;
        }
        const auto cross_edge_for = [&](AlignItemsKeyword keyword) {
            switch (keyword) {
            case AlignItemsKeyword::Start:
            case AlignItemsKeyword::Baseline:
                return cross_rtl ? AxisStaticEdge::End : AxisStaticEdge::Start;
            case AlignItemsKeyword::End:
                return cross_rtl ? AxisStaticEdge::Start : AxisStaticEdge::End;
            case AlignItemsKeyword::Stretch:
            case AlignItemsKeyword::FlexStart:
                return cross_reverse ? AxisStaticEdge::End : AxisStaticEdge::Start;
            case AlignItemsKeyword::FlexEnd:
                return cross_reverse ? AxisStaticEdge::Start : AxisStaticEdge::End;
            case AlignItemsKeyword::Center:
                return AxisStaticEdge::Center;
            default:
                throw std::logic_error("unresolved flex static self alignment");
            }
        };
        const AxisStaticPosition cross{
            cross_area,
            {cross_edge_for(alignment.keyword), alignment.safety,
             cross_edge_for(resolve_self_alignment_safety(alignment, true))}};
        const AxisStaticPosition main{main_area, AxisStaticAlign::from_keyword(main_edge)};
        const Point<AxisStaticPosition> position = c.is_row
                                                       ? Point<AxisStaticPosition>{main, cross}
                                                       : Point<AxisStaticPosition>{cross, main};
        candidates.push({child, static_cast<std::uint32_t>(order), style.position, position});
    }
}

} // namespace taffy::flex_detail
