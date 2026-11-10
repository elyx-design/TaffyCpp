// Pinned upstream compute/grid/alignment.rs. Track vectors remain logical;
// physical RTL offsets and relative-position deltas are applied only at layout.
#include "alignment.hpp"
#include "../compute_common.hpp"
#include "../sizing.hpp"
#include "types.hpp"
#include <stdexcept>

namespace taffy::grid {

void align_tracks(float content_size, Line<float> padding, Line<float> border,
                  std::span<GridTrack> tracks, AlignContent style, bool axis_is_reversed) {
    float used_size = 0;
    for (const auto& track : tracks)
        used_size += track.base_size;
    const float free_space = content_size - used_size;
    std::size_t num_tracks = 0;
    for (std::size_t i = 1; i < tracks.size(); i += 2)
        if (!tracks[i].is_collapsed)
            ++num_tracks;
    auto alignment = apply_alignment_fallback(free_space, num_tracks, style);
    if (axis_is_reversed)
        alignment = reversed(alignment);
    const float empty_offset = num_tracks == 0 ? compute_alignment_offset(free_space, num_tracks, 0,
                                                                          alignment, false, true)
                                               : 0;
    float total_offset = padding.start + border.start + empty_offset;
    bool seen_track = false;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        auto& track = tracks[axis_is_reversed ? tracks.size() - 1 - i : i];
        const bool non_collapsed = i % 2 != 0 && !track.is_collapsed;
        const float offset = non_collapsed ? compute_alignment_offset(free_space, num_tracks, 0,
                                                                      alignment, false, !seen_track)
                                           : 0;
        track.offset = total_offset + offset;
        total_offset = total_offset + offset + track.base_size;
        if (non_collapsed)
            seen_track = true;
    }
}

namespace {
float relative_offset(Position position, Line<std::optional<float>> inset, Direction direction) {
    if (position != Position::Relative)
        return 0;
    if (is_rtl(direction)) {
        if (inset.end)
            return -*inset.end;
        return inset.start.value_or(0);
    }
    if (inset.start)
        return *inset.start;
    return inset.end ? -*inset.end : 0;
}
} // namespace

std::pair<float, Line<float>> align_item_within_area(Line<float> grid_area,
                                                     AlignSelf alignment_style, float resolved_size,
                                                     Position position,
                                                     Line<std::optional<float>> inset,
                                                     Line<std::optional<float>> margin,
                                                     float baseline_shim, Direction direction) {
    const Line<float> non_auto_margin{margin.start.value_or(0) + baseline_shim,
                                      margin.end.value_or(0)};
    const float area_size = f32_max(grid_area.end - grid_area.start, 0);
    const float free_space = f32_max(area_size - resolved_size - non_auto_margin.sum(), 0);
    const unsigned auto_count = !margin.start + !margin.end;
    const float auto_size = auto_count > 0 ? free_space / static_cast<float>(auto_count) : 0;
    const Line<float> resolved_margin{margin.start.value_or(auto_size) + baseline_shim,
                                      margin.end.value_or(auto_size)};
    const auto keyword = resolve_self_alignment_safety(
        alignment_style, resolved_size + non_auto_margin.sum() > area_size);
    float offset;
    switch (keyword) {
    case AlignItemsKeyword::Start:
    case AlignItemsKeyword::FlexStart:
    case AlignItemsKeyword::Baseline:
    case AlignItemsKeyword::Stretch:
        offset = is_rtl(direction) ? area_size - resolved_size - resolved_margin.end
                                   : resolved_margin.start;
        break;
    case AlignItemsKeyword::End:
    case AlignItemsKeyword::FlexEnd:
        offset = is_rtl(direction) ? resolved_margin.start
                                   : area_size - resolved_size - resolved_margin.end;
        break;
    case AlignItemsKeyword::Center:
        offset = (area_size - resolved_size + resolved_margin.start - resolved_margin.end) / 2;
        break;
    default:
        throw std::logic_error("unresolved grid item alignment");
    }
    if (is_out_of_flow(position)) {
        if (inset.start && inset.end) {
            offset = is_rtl(direction)
                         ? area_size - *inset.end - resolved_size - non_auto_margin.end
                         : *inset.start + non_auto_margin.start;
        } else if (inset.start)
            offset = *inset.start + non_auto_margin.start;
        else if (inset.end)
            offset = area_size - *inset.end - resolved_size - non_auto_margin.end;
    }
    return {grid_area.start + offset, resolved_margin};
}

std::tuple<Rect<float>, float, float, std::optional<float>>
align_and_position_item(LayoutTree& tree, NodeId node, std::uint32_t order, Rect<float> grid_area,
                        Size<float> containing_block_size,
                        InBothAbsAxis<AlignItems> container_alignment_styles, float baseline_shim,
                        Direction direction, float container_border_box_width,
                        Rect<float> container_border, bool container_is_scroll_container,
                        OofCandidates& bubbled_candidates) {
    const Size<float> area_size{grid_area.right - grid_area.left, grid_area.bottom - grid_area.top};
    // Copy the view before any recursive callbacks can invalidate custom-tree style storage.
    const auto style = tree.grid_child_style(node);
    const CalcResolver calc = [&tree](Calc value, float basis) {
        return tree.resolve_calc(value, basis);
    };
    const auto justify_self = style.justify_self.value_or(container_alignment_styles.horizontal)
                                  .resolve_self_relative(style.direction, direction, true);
    const auto align_self = style.align_self.value_or(container_alignment_styles.vertical)
                                .resolve_self_relative(style.direction, direction, false);
    const auto position = style.position;
    const bool oof = is_out_of_flow(position);
    const auto inset_horizontal = style.inset.horizontal_components().map([&](const auto& v) {
        return v.maybe_resolve(containing_block_size.width, calc);
    });
    const auto inset_vertical = style.inset.vertical_components().map([&](const auto& v) {
        return v.maybe_resolve(containing_block_size.height, calc);
    });
    const auto padding = style.padding.resolve_or_zero(containing_block_size.width, calc);
    const auto border = style.border.resolve_or_zero(containing_block_size.width, calc);
    const auto padding_border_size = (padding + border).sum_axes();
    const auto adjustment =
        style.box_sizing == BoxSizing::ContentBox ? padding_border_size : Size<float>{};
    const auto inherent_size = style.size.maybe_resolve(containing_block_size, calc)
                                   .maybe_apply_aspect_ratio(style.aspect_ratio)
                                   .maybe_add(adjustment);
    const auto min_size = style.min_size.maybe_resolve(containing_block_size, calc)
                              .maybe_add(adjustment)
                              .or_(padding_border_size.map([](float v) {
                                  return std::optional<float>{v};
                              }))
                              .maybe_max(padding_border_size)
                              .maybe_apply_aspect_ratio(style.aspect_ratio);
    const auto max_size = style.max_size.maybe_resolve(containing_block_size, calc)
                              .maybe_apply_aspect_ratio(style.aspect_ratio)
                              .maybe_add(adjustment);
    const InBothAbsAxis<AlignSelf> alignment{
        justify_self.keyword == AlignItemsKeyword::Normal
            ? (inherent_size.width || style.size.width.is_sizing_keyword() || style.is_replaced()
                   ? AlignSelf::start()
                   : AlignSelf::stretch())
            : justify_self,
        align_self.keyword == AlignItemsKeyword::Normal
            ? (inherent_size.height || style.size.height.is_sizing_keyword() ||
                       style.aspect_ratio || style.is_replaced()
                   ? AlignSelf::start()
                   : AlignSelf::stretch())
            : align_self};
    // CSS uses the containing block width even for vertical margin percentages.
    const auto margin = style.margin.maybe_resolve(containing_block_size.width, calc);
    const Size<float> available{
        f32_max(maybe_sub(maybe_sub(area_size.width, margin.left), margin.right), 0),
        f32_max(maybe_sub(maybe_sub(area_size.height, margin.top), margin.bottom) - baseline_shim,
                0)};
    // Explicit construction avoids GCC's false positive for conditional optionals.
    std::optional<SizingKeywordResolution> keyword_width{};
    std::optional<SizingKeywordResolution> keyword_height{};
    if (!inherent_size.width) {
        if (const auto resolved = resolve_sizing_keyword(style.size.width, available.width,
                                                         containing_block_size.width, calc))
            keyword_width.emplace(*resolved);
    }
    if (!inherent_size.height) {
        if (const auto resolved = resolve_sizing_keyword(style.size.height, available.height,
                                                         containing_block_size.height, calc))
            keyword_height.emplace(*resolved);
    }
    const auto parent_size = containing_block_size.map([](float v) {
        return std::optional<float>{v};
    });
    const auto available_space = available.map([](float v) {
        return AvailableSpace::definite(v);
    });
    Size<std::optional<float>> keyword_measured{};
    if (!oof && keyword_width && keyword_height &&
        keyword_width->kind == SizingKeywordResolution::Kind::Measure &&
        keyword_height->kind == SizingKeywordResolution::Kind::Measure) {
        keyword_measured = tree.measure_child_size_both(node, {}, parent_size,
                                                        {keyword_width->available_space,
                                                         keyword_height->available_space},
                                                        SizingMode::InherentSize, {})
                               .map([](float v) {
                                   return std::optional<float>{v};
                               });
    }
    auto width = inherent_size.width;
    if (!width) {
        if (oof && inset_horizontal.start && inset_horizontal.end)
            width = f32_max(available.width - *inset_horizontal.start - *inset_horizontal.end, 0);
        else if (keyword_width) {
            if (keyword_width->kind == SizingKeywordResolution::Kind::Exact)
                width = keyword_width->exact;
            else if (keyword_measured.width)
                width = keyword_measured.width;
            else
                width = tree.measure_child_size(
                    node, {}, parent_size, {keyword_width->available_space, available_space.height},
                    SizingMode::InherentSize, AbsoluteAxis::Horizontal, {});
        } else if (margin.left && margin.right && alignment.horizontal == AlignSelf::stretch() &&
                   !oof)
            width = available.width;
    }
    auto dimensions =
        Size<std::optional<float>>{width, inherent_size.height}.maybe_apply_aspect_ratio(
            style.aspect_ratio);
    if (!dimensions.height) {
        if (oof && inset_vertical.start && inset_vertical.end)
            dimensions.height =
                f32_max(available.height - *inset_vertical.start - *inset_vertical.end, 0);
        else if (keyword_height) {
            if (keyword_height->kind == SizingKeywordResolution::Kind::Exact)
                dimensions.height = keyword_height->exact;
            else if (keyword_measured.height)
                dimensions.height = keyword_measured.height;
            else
                dimensions.height = tree.measure_child_size(
                    node, {dimensions.width, {}}, parent_size,
                    {AvailableSpace::definite(dimensions.width.value_or(available.width)),
                     keyword_height->available_space},
                    SizingMode::InherentSize, AbsoluteAxis::Vertical, {});
        } else if (margin.top && margin.bottom && alignment.vertical == AlignSelf::stretch() &&
                   !oof)
            dimensions.height = available.height;
    }
    dimensions =
        dimensions.maybe_apply_aspect_ratio(style.aspect_ratio).maybe_clamp(min_size, max_size);
    if (oof && !dimensions.both_axis_defined()) {
        dimensions = tree.measure_child_size_both(node, dimensions, parent_size, available_space,
                                                  SizingMode::InherentSize, {})
                         .map([](float v) {
                             return std::optional<float>{v};
                         });
    }
    auto output = tree.perform_child_layout(node, dimensions, parent_size, available_space,
                                            SizingMode::InherentSize, {});
    const auto size = dimensions.unwrap_or(output.size).maybe_clamp(min_size, max_size);
    const auto [x_without_relative, x_margin] = align_item_within_area(
        {grid_area.left, grid_area.right}, alignment.horizontal, size.width, position,
        inset_horizontal, margin.horizontal_components(), 0, direction);
    const auto [y_without_relative, y_margin] = align_item_within_area(
        {grid_area.top, grid_area.bottom}, alignment.vertical, size.height, position,
        inset_vertical, margin.vertical_components(), baseline_shim, Direction::Ltr);
    const float x = x_without_relative + relative_offset(position, inset_horizontal, direction);
    const float y = y_without_relative + relative_offset(position, inset_vertical, Direction::Ltr);
    auto candidates = output.oof_candidates.take();
    if (!candidates.empty()) {
        candidates.translate({x, y});
        bubbled_candidates.append(candidates);
    }
    Layout layout;
    layout.order = order;
    layout.location = {x, y};
    layout.size = size;
    layout.scrollable_overflow_rect = output.scrollable_overflow_rect;
    layout.scrollbar_size = {style.overflow.y == Overflow::Scroll ? style.scrollbar_width : 0,
                             style.overflow.x == Overflow::Scroll ? style.scrollbar_width : 0};
    layout.padding = padding;
    layout.border = border;
    layout.margin = {x_margin.start, x_margin.end, y_margin.start, y_margin.end};
    tree.set_unrounded_layout(node, layout);
    const Point<float> contribution_location{
        is_rtl(direction) ? container_border_box_width - (x + size.width) - container_border.right
                          : x - container_border.left,
        y - container_border.top};
    const auto contribution = compute_scrollable_overflow_contribution(
        contribution_location, size, output.scrollable_overflow_rect, style.overflow, style.contain,
        container_is_scroll_container);
    return {contribution, y_without_relative, size.height,
            child_first_baseline(output.baselines.first, size.height, style.overflow.y)};
}
} // namespace taffy::grid
