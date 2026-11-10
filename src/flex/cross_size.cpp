#include "../sizing.hpp"
#include "internal.hpp"

#include <stdexcept>

namespace taffy::flex_detail {

void determine_hypothetical_cross_size(LayoutTree& tree, FlexLine& line, const AlgoConstants& c,
                                       Size<std::optional<float>> node_size,
                                       Size<AvailableSpace> available_space) {
    const bool known_line = !c.is_wrap && node_size.cross(c.dir).has_value();
    for (auto& child : line.items) {
        const float pb = (child.padding + child.border).cross_axis_sum(c.dir);
        if (known_line && child.is_stretched(c.dir) &&
            !child.participates_in_baseline_alignment(c.dir))
            continue;
        const auto available_main = AvailableSpace::definite(
            f32_max(c.container_size.main(c.dir) - child.margin.main_axis_sum(c.dir), 0.0f));
        const auto min_cross =
            child.min_size.maybe_apply_aspect_ratio(child.aspect_ratio).cross(c.dir);
        const auto max_cross =
            child.max_size.maybe_apply_aspect_ratio(child.aspect_ratio).cross(c.dir);
        const auto child_cross =
            maybe_max(maybe_clamp(child.size.cross(c.dir), min_cross, max_cross), pb);
        auto available_cross = maybe_max(
            maybe_clamp(available_space.cross(c.dir).map_definite_value([&](float value) {
                return f32_max(c.divided_cross_space(value) - child.margin.cross_axis_sum(c.dir),
                               0.0f);
            }),
                        min_cross, max_cross),
            pb);
        auto stretch = c.node_inner_size.cross(c.dir);
        if (stretch)
            stretch = c.divided_cross_space(*stretch);
        stretch = maybe_max(maybe_sub(stretch, child.margin.cross_axis_sum(c.dir)), 0.0f);
        const auto keyword =
            resolve_sizing_keyword(child.size_style.cross(c.dir), stretch,
                                   c.node_inner_size.cross(c.dir), [&](Calc value, float basis) {
                                       return tree.resolve_calc(value, basis);
                                   });
        if (keyword && keyword->kind == SizingKeywordResolution::Kind::Measure)
            available_cross = keyword->available_space;
        float inner_cross;
        if (child_cross) {
            inner_cross = *child_cross;
        } else {
            LayoutInput input;
            input.run_mode = RunMode::ComputeSize;
            input.sizing_mode = SizingMode::ContentSize;
            input.axis = requested_axis(cross_axis(c.dir));
            input.known_dimensions =
                c.is_row ? Size<std::optional<float>>{child.target_size.width, child_cross}
                         : Size<std::optional<float>>{child_cross, child.target_size.height};
            input.known_dimensions_are_definite = item_known_dimension_definiteness(c, child);
            input.parent_size = c.node_inner_size;
            input.available_space = c.is_row
                                        ? Size<AvailableSpace>{available_main, available_cross}
                                        : Size<AvailableSpace>{available_cross, available_main};
            inner_cross =
                f32_max(maybe_clamp(tree.compute_child_layout(child.node, input).size.cross(c.dir),
                                    min_cross, max_cross),
                        pb);
        }
        child.hypothetical_inner_size.set_cross(c.dir, inner_cross);
        child.hypothetical_outer_size.set_cross(c.dir,
                                                inner_cross + child.margin.cross_axis_sum(c.dir));
    }
}

void calculate_children_base_lines(LayoutTree& tree, Size<std::optional<float>> node_size,
                                   Size<AvailableSpace> available_space,
                                   std::vector<FlexLine>& lines, const AlgoConstants& c) {
    if (!c.is_row)
        return;
    for (auto& line : lines) {
        std::size_t count = 0;
        for (const auto& child : line.items)
            count += child.participates_in_baseline_alignment(c.dir);
        if (count <= 1)
            continue;
        for (auto& child : line.items) {
            if (!child.participates_in_baseline_alignment(c.dir))
                continue;
            LayoutInput input;
            input.run_mode = RunMode::PerformLayout;
            input.sizing_mode = SizingMode::ContentSize;
            input.axis = RequestedAxis::Both;
            input.known_dimensions = {child.target_size.width,
                                      child.hypothetical_inner_size.height};
            input.known_dimensions_are_definite = item_known_dimension_definiteness(c, child);
            input.parent_size = c.node_inner_size;
            input.available_space =
                Size<AvailableSpace>{AvailableSpace::definite(c.container_size.width),
                                     available_space.height.maybe_set(node_size.height)}
                    .maybe_sub(child.margin.sum_axes())
                    .maybe_max(Size<float>::ZERO);
            const auto output = tree.compute_child_layout(child.node, input);
            child.baseline =
                child_first_baseline(output.baselines.first, output.size.height, child.overflow.y)
                    .value_or(output.size.height) +
                child.margin.top;
        }
    }
}

void calculate_cross_size(std::vector<FlexLine>& lines, Size<std::optional<float>> node_size,
                          const AlgoConstants& c) {
    const float inset = c.content_box_inset.cross_axis_sum(c.dir);
    const auto minimum = c.min_size.cross(c.dir);
    const auto maximum = c.max_size.cross(c.dir);
    if (!c.is_wrap && node_size.cross(c.dir)) {
        lines.at(0).cross_size =
            maybe_max(maybe_sub(maybe_clamp(node_size.cross(c.dir), minimum, maximum), inset), 0.0f)
                .value_or(0.0f);
    } else {
        for (auto& line : lines) {
            float baseline = 0.0f;
            for (const auto& child : line.items)
                baseline = f32_max(baseline, child.baseline);
            line.cross_size = 0.0f;
            for (const auto& child : line.items) {
                const float size = child.hypothetical_outer_size.cross(c.dir);
                line.cross_size =
                    f32_max(line.cross_size, child.participates_in_baseline_alignment(c.dir)
                                                 ? baseline - child.baseline + size
                                                 : size);
            }
        }
        if (!c.is_wrap)
            lines.at(0).cross_size = maybe_clamp(lines.at(0).cross_size, maybe_sub(minimum, inset),
                                                 maybe_sub(maximum, inset));
    }
}

void handle_align_content_stretch(std::vector<FlexLine>& lines,
                                  Size<std::optional<float>> node_size, const AlgoConstants& c) {
    if (c.align_content != AlignContent::stretch())
        return;
    const auto minimum = c.min_size.cross(c.dir);
    const auto maximum = c.max_size.cross(c.dir);
    const auto size = node_size.cross(c.dir) ? node_size.cross(c.dir) : minimum;
    const float inner = maybe_max(maybe_sub(maybe_clamp(size, minimum, maximum),
                                            c.content_box_inset.cross_axis_sum(c.dir)),
                                  0.0f)
                            .value_or(0.0f);
    float total = -0.0f; // Rust's Sum<f32> identity preserves negative zero.
    for (const auto& line : lines)
        total += line.cross_size;
    total += sum_axis_gaps(c.gap.cross(c.dir), lines.size());
    if (total < inner) {
        const float addition = (inner - total) / static_cast<float>(lines.size());
        for (auto& line : lines)
            line.cross_size += addition;
    }
}

void determine_used_cross_size(LayoutTree& tree, std::vector<FlexLine>& lines,
                               const AlgoConstants& c) {
    for (auto& line : lines)
        for (auto& child : line.items) {
            const auto style = tree.flex_child_style(child.node);
            float size;
            if (child.is_stretched(c.dir)) {
                const auto maximum = style.max_size
                                         .maybe_resolve(c.node_inner_size,
                                                        [&](Calc value, float basis) {
                                                            return tree.resolve_calc(value, basis);
                                                        })
                                         .maybe_add(child.box_sizing_adjustment(style));
                size =
                    maybe_clamp(f32_max(line.cross_size - child.margin.cross_axis_sum(c.dir), 0.0f),
                                child.min_size.cross(c.dir), maximum.cross(c.dir));
            } else
                size = child.hypothetical_inner_size.cross(c.dir);
            child.target_size.set_cross(c.dir, size);
            child.outer_target_size.set_cross(c.dir, size + child.margin.cross_axis_sum(c.dir));
        }
}

namespace {
float item_cross_offset(const FlexItem& child, float free, float baseline, float baseline_to_bottom,
                        const AlgoConstants& c) {
    const bool reverse = c.is_column && c.layout_direction == Direction::Rtl;
    const auto keyword = child.align_self.is_safe() && free < 0.0f ? AlignItemsKeyword::Start
                                                                   : child.align_self.keyword;
    switch (keyword) {
    case AlignItemsKeyword::Start:
        return reverse ? free : 0.0f;
    case AlignItemsKeyword::End:
        return reverse ? 0.0f : free;
    case AlignItemsKeyword::FlexStart:
    case AlignItemsKeyword::Stretch:
        return c.is_wrap_reverse != reverse ? free : 0.0f;
    case AlignItemsKeyword::FlexEnd:
        return c.is_wrap_reverse != reverse ? 0.0f : free;
    case AlignItemsKeyword::Center:
        return free / 2.0f;
    case AlignItemsKeyword::Baseline:
        if (c.is_row)
            return c.is_wrap_reverse ? free + child.outer_target_size.cross(c.dir) -
                                           baseline_to_bottom - child.baseline
                                     : baseline - child.baseline;
        return c.is_wrap_reverse != (reverse && !c.is_wrap) ? free : 0.0f;
    default:
        throw std::logic_error("unresolved flex self alignment");
    }
}
} // namespace

// The shared declaration exposes a line-level helper. Keep the pinned item-level
// calculation private and compute the baseline group from the full line here.
void align_flex_items_along_cross_axis(FlexLine& line, float line_cross_size,
                                       const AlgoConstants& c) {
    float baseline = 0.0f, baseline_to_bottom = 0.0f;
    for (const auto& child : line.items) {
        baseline = f32_max(baseline, child.baseline);
        if (child.participates_in_baseline_alignment(c.dir))
            baseline_to_bottom =
                f32_max(baseline_to_bottom, child.outer_target_size.cross(c.dir) - child.baseline);
    }
    for (auto& child : line.items) {
        if (!child.margin_is_auto.cross_start(c.dir) && !child.margin_is_auto.cross_end(c.dir))
            child.offset_cross =
                item_cross_offset(child, line_cross_size - child.outer_target_size.cross(c.dir),
                                  baseline, baseline_to_bottom, c);
    }
}

void resolve_cross_axis_auto_margins(std::vector<FlexLine>& lines, const AlgoConstants& c) {
    for (auto& line : lines) {
        for (auto& child : line.items) {
            const float free = line.cross_size - child.outer_target_size.cross(c.dir);
            const bool start = child.margin_is_auto.cross_start(c.dir),
                       end = child.margin_is_auto.cross_end(c.dir);
            if (free < 0.0f && (start || end)) {
                // Overflow goes toward logical cross-end, not flex cross-end:
                // wrap-reverse does not reverse this rule, but RTL columns do.
                // Auto logical-start stays zero; the opposite margin (even a
                // non-auto one) absorbs the deficit, per flexbox section 9.6.
                if (c.is_row)
                    child.margin.bottom += free;
                else if (is_rtl(c.layout_direction))
                    child.margin.left += free;
                else
                    child.margin.right += free;
            } else if (start && end) {
                if (c.is_row)
                    child.margin.top = child.margin.bottom = free / 2.0f;
                else
                    child.margin.left = child.margin.right = free / 2.0f;
            } else if (start) {
                if (c.is_row)
                    child.margin.top = free;
                else
                    child.margin.left = free;
            } else if (end) {
                if (c.is_row)
                    child.margin.bottom = free;
                else
                    child.margin.right = free;
            }
        }
        align_flex_items_along_cross_axis(line, line.cross_size, c);
    }
}

float determine_container_cross_size(const std::vector<FlexLine>& lines,
                                     Size<std::optional<float>> node_size, AlgoConstants& c) {
    float total = -0.0f; // Rust's Sum<f32> identity preserves negative zero.
    for (const auto& line : lines)
        total += line.cross_size;
    const float inset = c.content_box_inset.cross_axis_sum(c.dir);
    const float outer =
        f32_max(maybe_clamp(node_size.cross(c.dir).value_or(
                                total + sum_axis_gaps(c.gap.cross(c.dir), lines.size()) + inset),
                            c.min_size.cross(c.dir), c.max_size.cross(c.dir)),
                inset - c.scrollbar_gutter.cross(c.dir));
    c.container_size.set_cross(c.dir, outer);
    c.inner_container_size.set_cross(c.dir, f32_max(outer - inset, 0.0f));
    return total;
}

void align_flex_lines_per_align_content(std::vector<FlexLine>& lines, const AlgoConstants& c,
                                        float total_cross_size) {
    const float gap = c.gap.cross(c.dir);
    const float free =
        c.inner_container_size.cross(c.dir) - total_cross_size - sum_axis_gaps(gap, lines.size());
    const auto mode = apply_alignment_fallback(free, lines.size(), c.align_content);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto& line = lines[c.is_wrap_reverse ? lines.size() - 1 - i : i];
        line.offset_cross =
            compute_alignment_offset(free, lines.size(), gap, mode, c.is_wrap_reverse, i == 0);
    }
}

} // namespace taffy::flex_detail
