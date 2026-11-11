#include "flex/internal.hpp"
#include <algorithm>

namespace taffy {
namespace {
using namespace flex_detail;

LayoutOutput empty_container(LayoutTree& tree, const LayoutInput& input, AlgoConstants c) {
    const auto dir = c.dir;
    const auto available =
        determine_available_space(input.known_dimensions, input.available_space, c);
    std::vector<FlexLine> lines;
    if (const auto main = c.node_inner_size.main(dir)) {
        c.inner_container_size.set_main(dir, *main);
        c.container_size.set_main(dir, *main + c.content_box_inset.main_axis_sum(dir));
    } else
        determine_container_main_size(tree, available, lines, c);
    if (input.run_mode == RunMode::ComputeSize && main_axis_is_requested_axis(dir, input.axis))
        return LayoutOutput::from_outer_size(
            Size<float>{}.with_main(dir, c.container_size.main(dir)));
    determine_container_cross_size(lines, input.known_dimensions, c);
    auto output = LayoutOutput::from_outer_size(c.container_size);
    output.oof_positioning_area = OofPositioningArea{};
    return output;
}

LayoutOutput preliminary(LayoutTree& tree, NodeId node, const LayoutInput& input) {
    auto c = compute_constants(tree, tree.flex_container_style(node), input.known_dimensions,
                               input.known_dimensions_are_definite, input.parent_size,
                               input.available_space);
    if (tree.child_count(node) == 0)
        return empty_container(tree, input, c);
    auto items = generate_anonymous_flex_items(tree, node, c);
    const auto available =
        determine_available_space(input.known_dimensions, input.available_space, c);
    determine_flex_base_size(tree, c, available, items);
    const auto known_main_size = c.node_inner_size.main(c.dir);
    // Resolve capped max-content sizing from the unwrapped contributions. Doing
    // this after line collection would shrink the container to its wrapped lines
    // instead of retaining the intrinsic size clamped by the maximum.
    const bool intrinsic_before_wrap = c.is_wrap && !c.is_balance &&
        !known_main_size.has_value() && c.max_size.main(c.dir).has_value() &&
        available.main(c.dir).is_max_content();
    if (intrinsic_before_wrap) {
        std::vector<FlexLine> intrinsic_lines{{std::span<FlexItem>{items}, 0.0f, 0.0f}};
        determine_container_main_size(tree, available, intrinsic_lines, c);
    }
    auto lines = c.is_balance ? collect_balanced_flex_lines(c, available, items)
                              : collect_flex_lines(c, available, items);
    if (known_main_size) {
        c.inner_container_size.set_main(c.dir, *known_main_size);
        c.container_size.set_main(c.dir, *known_main_size + c.content_box_inset.main_axis_sum(c.dir));
    } else {
        if (!intrinsic_before_wrap)
            determine_container_main_size(tree, available, lines, c);
        c.node_inner_size.set_main(c.dir, c.inner_container_size.main(c.dir));
        c.node_outer_size.set_main(c.dir, c.container_size.main(c.dir));
        const auto style = tree.flex_container_style(node);
        c.gap.set_main(c.dir, style.gap.main(c.dir)
                                  .maybe_resolve(c.inner_container_size.main(c.dir),
                                                 [&](Calc value, float basis) {
                                                     return tree.resolve_calc(value, basis);
                                                 })
                                  .value_or(0.0f));
    }
    if (input.run_mode == RunMode::ComputeSize && main_axis_is_requested_axis(c.dir, input.axis))
        return LayoutOutput::from_outer_size(
            Size<float>{}.with_main(c.dir, c.container_size.main(c.dir)));
    for (auto& line : lines)
        resolve_flexible_lengths(line, c);
    for (auto& line : lines)
        determine_hypothetical_cross_size(tree, line, c, input.known_dimensions, available);
    calculate_children_base_lines(tree, input.known_dimensions, available, lines, c);
    calculate_cross_size(lines, input.known_dimensions, c);
    handle_align_content_stretch(lines, input.known_dimensions, c);
    determine_used_cross_size(tree, lines, c);
    distribute_remaining_free_space(lines, c);
    resolve_cross_axis_auto_margins(lines, c);
    const float total_cross = determine_container_cross_size(lines, input.known_dimensions, c);
    if (input.run_mode == RunMode::ComputeSize)
        return LayoutOutput::from_outer_size(c.container_size);
    align_flex_lines_per_align_content(lines, c, total_cross);
    const auto overflow = final_layout_pass(tree, lines, c);
    OofCandidates candidates;
    collect_oof_candidates(tree, node, c, lines, candidates);
    const auto absolute_inset =
        c.border + Rect<float>{is_rtl(c.layout_direction) ? c.scrollbar_gutter.x : 0.0f,
                               is_rtl(c.layout_direction) ? 0.0f : c.scrollbar_gutter.x, 0.0f,
                               c.scrollbar_gutter.y};
    const auto children = tree.child_ids(node);
    for (std::size_t order = 0; order < children.size(); ++order) {
        const auto child = children[order];
        if (tree.flex_child_style(child).box_generation_mode() != BoxGenerationMode::None)
            continue;
        tree.set_unrounded_layout(child, Layout::with_order(static_cast<std::uint32_t>(order)));
        tree.perform_child_layout(child, {}, {}, Size<AvailableSpace>::max_content(),
                                  SizingMode::ContentSize, {});
    }
    std::optional<float> baseline;
    if (!lines.empty()) {
        const auto& line = c.is_wrap_reverse ? lines.back() : lines.front();
        if (!line.items.empty()) {
            if (c.is_column)
                baseline = (is_reverse(c.dir) ? line.items.back() : line.items.front()).baseline;
            else {
                const auto found =
                    std::find_if(line.items.begin(), line.items.end(), [&](const FlexItem& item) {
                        return item.participates_in_baseline_alignment(c.dir);
                    });
                baseline = (found == line.items.end() ? line.items.front() : *found).baseline;
            }
        }
    }
    auto output = LayoutOutput::from_sizes_and_baselines(c.container_size, overflow,
                                                         Baselines::from_first(baseline));
    output.oof_candidates = std::move(candidates);
    output.oof_positioning_area = OofPositioningArea{c.container_size - absolute_inset.sum_axes(),
                                                     {absolute_inset.left, absolute_inset.top}};
    return output;
}
} // namespace

LayoutOutput compute_flex(LayoutTree& tree, NodeId node, const LayoutInput& input) {
    const auto style = tree.flex_container_style(node);
    const auto calc = [&](Calc value, float basis) {
        return tree.resolve_calc(value, basis);
    };
    const auto padding = style.padding.resolve_or_zero(input.parent_size.width, calc);
    const auto border = style.border.resolve_or_zero(input.parent_size.width, calc);
    const auto padding_border = padding.sum_axes() + border.sum_axes();
    const auto adjustment =
        style.box_sizing == BoxSizing::ContentBox ? padding_border : Size<float>{};
    const auto min_size = style.min_size.maybe_resolve(input.parent_size, calc)
                              .maybe_apply_aspect_ratio(style.aspect_ratio)
                              .maybe_add(adjustment);
    const auto max_size = style.max_size.maybe_resolve(input.parent_size, calc)
                              .maybe_apply_aspect_ratio(style.aspect_ratio)
                              .maybe_add(adjustment);
    const auto preferred = input.sizing_mode == SizingMode::InherentSize
                               ? style.size.maybe_resolve(input.parent_size, calc)
                                     .maybe_apply_aspect_ratio(style.aspect_ratio)
                                     .maybe_add(adjustment)
                                     .maybe_clamp(min_size, max_size)
                               : Size<std::optional<float>>{};
    const auto min_max = min_size.zip_map(
        max_size, [](std::optional<float> min, std::optional<float> max) -> std::optional<float> {
            return min && max && *max <= *min ? min : std::nullopt;
        });
    const auto known = input.known_dimensions.or_(min_max.or_(preferred).maybe_max(padding_border));
    if (input.run_mode == RunMode::ComputeSize) {
        if (known.both_axis_defined())
            return LayoutOutput::from_outer_size({*known.width, *known.height});
        if (input.axis == RequestedAxis::Horizontal && known.width)
            return LayoutOutput::from_outer_size({*known.width, 0.0f});
    }
    auto inner_input = input;
    inner_input.known_dimensions = known;
    inner_input.known_dimensions_are_definite = input.known_dimensions_are_definite.zip_map(
        input.known_dimensions, [](bool definite, std::optional<float> value) {
            return definite || !value;
        });
    auto output = preliminary(tree, node, inner_input);
    if (style.contain.suppresses_baseline())
        output.baselines = Baselines::NONE;
    return output;
}
} // namespace taffy
