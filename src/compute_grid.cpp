#include "grid/alignment.hpp"
#include "grid/metadata.hpp"
#include "grid/placement.hpp"
#include "grid/track_sizing.hpp"
#include "grid/types.hpp"
#include <algorithm>
#include <taffy/compute.hpp>

namespace taffy {
namespace {
using namespace grid;

// Pinned compute/grid/mod.rs: resolve once; all sizes below are border-box
// except the explicitly named inner/content-box values used by track sizing.
struct ContainerConstants {
    Direction direction;
    Contain contain;
    ContainingBlockClaims claims;
    Rect<float> padding, border, inset;
    Size<float> padding_border;
    OptionalSize minimum, maximum, preferred, outer, inner_min, inner_max, inner;
    Point<float> scrollbar;
    bool scroll_container;
    AlignContent align_content, justify_content;
    AlignItems align_items, justify_items;
    Size<AvailableSpace> available;
};

ContainerConstants container_constants(LayoutTree& tree, NodeId node, const Style& s,
                                       const LayoutInput& in) {
    const CalcResolver calc = [&](Calc c, float b) {
        return tree.resolve_calc(c, b);
    };
    ContainerConstants c{};
    c.direction = s.direction;
    c.contain = s.contain;
    c.claims = tree.containing_block_claims(node);
    c.padding = s.padding.resolve_or_zero(in.parent_size.width, calc);
    c.border = s.border.resolve_or_zero(in.parent_size.width, calc);
    c.inset = c.padding + c.border;
    c.padding_border = c.inset.sum_axes();
    const Size<float> adjustment =
        s.box_sizing == BoxSizing::ContentBox ? c.padding_border : Size<float>{};
    c.minimum = s.min_size.maybe_resolve(in.parent_size, calc)
                    .maybe_apply_aspect_ratio(s.aspect_ratio)
                    .maybe_add(adjustment);
    c.maximum = s.max_size.maybe_resolve(in.parent_size, calc)
                    .maybe_apply_aspect_ratio(s.aspect_ratio)
                    .maybe_add(adjustment);
    if (in.sizing_mode == SizingMode::InherentSize)
        c.preferred = s.size.maybe_resolve(in.parent_size, calc)
                          .maybe_apply_aspect_ratio(s.aspect_ratio)
                          .maybe_add(adjustment);
    c.scrollbar = {s.overflow.y == Overflow::Scroll ? s.scrollbar_width : 0,
                   s.overflow.x == Overflow::Scroll ? s.scrollbar_width : 0};
    c.scroll_container = is_scroll_container(s.overflow.x) || is_scroll_container(s.overflow.y);
    c.inset.bottom += c.scrollbar.y;
    if (c.direction == Direction::Rtl)
        c.inset.left += c.scrollbar.x;
    else
        c.inset.right += c.scrollbar.x;
    c.align_content = s.grid_align_content(AbstractAxis::Block);
    c.justify_content = s.grid_align_content(AbstractAxis::Inline);
    c.align_items = s.align_items;
    c.justify_items = s.justify_items;
    const auto resolved = in.known_dimensions.or_(c.preferred);
    c.available = resolved
                      .zip_map(in.available_space,
                               [](auto size, auto space) {
                                   return size ? AvailableSpace::definite(*size) : space;
                               })
                      .maybe_clamp(c.minimum, c.maximum)
                      .maybe_max(c.padding_border);
    c.available.width = c.available.width.map_definite_value([&](float v) {
        return v - c.inset.horizontal_axis_sum();
    });
    c.available.height = c.available.height.map_definite_value([&](float v) {
        return v - c.inset.vertical_axis_sum();
    });
    c.outer = resolved.maybe_clamp(c.minimum, c.maximum).maybe_max(c.padding_border);
    c.inner_min = c.minimum.maybe_sub(c.inset.sum_axes());
    c.inner_max = c.maximum.maybe_sub(c.inset.sum_axes());
    c.inner = c.outer.maybe_sub(c.inset.sum_axes());
    return c;
}

float track_sum(std::span<const GridTrack> tracks) {
    float sum = 0;
    for (const auto& track : tracks)
        sum += track.base_size;
    return sum;
}

void clear_contributions(std::span<GridItem> items, AbstractAxis axis) {
    for (auto& item : items) {
        item.grid_area_size_cache.reset();
        item.min_content_contribution_cache.set(axis, std::nullopt);
        item.max_content_contribution_cache.set(axis, std::nullopt);
        item.minimum_contribution_cache.set(axis, std::nullopt);
    }
}

bool refresh_contributions(LayoutTree& tree, std::span<GridItem> items, AbstractAxis axis,
                           std::span<const GridTrack> tracks, std::span<const GridTrack> others,
                           OptionalSize inner) {
    const CalcResolver calc = [&](Calc c, float b) {
        return tree.resolve_calc(c, b);
    };
    bool changed = false;
    for (auto& item : items) {
        if (!item.crosses_intrinsic_track(axis))
            continue;
        const auto area = item.grid_area_size(
            axis, tracks, others, inner,
            [](const GridTrack& track, std::optional<float>) {
                return std::optional<float>(track.base_size);
            },
            calc);
        const float contribution =
            item.min_content_contribution(axis, tree, area, area.with(axis, std::nullopt));
        changed |=
            item.min_content_contribution_cache.get(axis) != std::optional<float>(contribution);
        item.grid_area_size_cache = area;
        item.min_content_contribution_cache.set(axis, contribution);
        item.max_content_contribution_cache.set(axis, std::nullopt);
        item.minimum_contribution_cache.set(axis, std::nullopt);
    }
    return changed;
}

// At most one rerun per axis. Percentage cycles must not grow the container;
// changed intrinsic contributions without percentages may update its size.
Size<bool> rerun_tracks(LayoutTree& tree, const ContainerConstants& c, OptionalSize inner,
                        std::span<GridTrack> columns, std::span<GridTrack> rows,
                        std::span<GridItem> items, bool baselines) {
    const bool percent_col = std::any_of(columns.begin(), columns.end(), [](const auto& t) {
        return t.uses_percentage();
    });
    const bool percent_row = std::any_of(rows.begin(), rows.end(), [](const auto& t) {
        return t.uses_percentage();
    });
    bool rerun_col = !c.outer.width && percent_col;
    bool changed_col = false, changed_row = false;
    if (rerun_col)
        clear_contributions(items, AbstractAxis::Inline);
    else
        rerun_col = changed_col =
            refresh_contributions(tree, items, AbstractAxis::Inline, columns, rows, inner);
    bool rerun_row = !c.outer.height && percent_row;
    if (rerun_row)
        clear_contributions(items, AbstractAxis::Block);
    const TrackSizeEstimateWithTree estimate = [](const GridTrack& t, std::optional<float>,
                                                  const LayoutTree&) {
        return std::optional<float>(t.base_size);
    };
    if (rerun_col) {
        track_sizing_algorithm(tree, AbstractAxis::Inline, c.inner_min.width, c.inner_max.width,
                               c.justify_content, c.align_content, c.available, inner, columns,
                               rows, items, estimate, baselines);
        if (!rerun_row)
            rerun_row = changed_row =
                refresh_contributions(tree, items, AbstractAxis::Block, rows, columns, inner);
    }
    if (rerun_row)
        track_sizing_algorithm(tree, AbstractAxis::Block, c.inner_min.height, c.inner_max.height,
                               c.align_content, c.justify_content, c.available, inner, rows,
                               columns, items, estimate, false);
    return {changed_col && !percent_col, changed_row && !percent_row};
}

float border_size(std::optional<float> preferred, float tracks, float inset,
                  std::optional<float> minimum, std::optional<float> maximum, float floor) {
    return f32_max(maybe_clamp(preferred.value_or(tracks + inset), minimum, maximum), floor);
}

Rect<float> item_area(const GridItem& item, std::span<const GridTrack> rows,
                      std::span<const GridTrack> columns, bool rtl) {
    return {columns[rtl ? item.column_indexes.end - 1 : item.column_indexes.start + 1].offset,
            columns[rtl ? item.column_indexes.start : item.column_indexes.end].offset,
            rows[item.row_indexes.start + 1].offset, rows[item.row_indexes.end].offset};
}

OofCandidates position_children(LayoutTree& tree, NodeId node, const ContainerConstants& c,
                                Size<float> border_box, std::span<GridItem> items,
                                std::span<const GridTrack> rows, std::span<const GridTrack> columns,
                                TrackCounts row_counts, TrackCounts column_counts,
                                const NamedLineResolver& resolver, Rect<float>& overflow) {
    const bool rtl = c.direction == Direction::Rtl;
    const InBothAbsAxis<AlignItems> alignments{c.justify_items, c.align_items};
    for (std::size_t index = 0; index < items.size(); ++index) {
        auto& item = items[index];
        const auto area = item_area(item, rows, columns, rtl);
        auto [contribution, y, height, baseline] = align_and_position_item(
            tree, item.node, static_cast<std::uint32_t>(index), area,
            {area.right - area.left, area.bottom - area.top}, alignments, item.baseline_shim,
            c.direction, border_box.width, c.border, c.scroll_container, item.oof_candidates);
        item.y_position = y;
        item.height = height;
        item.baseline = baseline;
        overflow = overflow.united(contribution);
    }
    OofCandidates candidates;
    std::uint32_t order = static_cast<std::uint32_t>(items.size());
    std::size_t in_flow = 0;
    const auto children = tree.child_ids(node);
    for (std::size_t index = 0; index < children.size(); ++index) {
        if (in_flow < items.size() && items[in_flow].source_order == index) {
            candidates.append(items[in_flow++].oof_candidates);
            continue;
        }
        const auto child = children[index];
        const auto& style = tree.grid_child_style(child);
        if (style.box_generation_mode() == BoxGenerationMode::None) {
            tree.set_unrounded_layout(child, Layout::with_order(order++));
            tree.perform_child_layout(
                child, {}, {}, {AvailableSpace::max_content(), AvailableSpace::max_content()},
                SizingMode::InherentSize, {false, false});
        } else if (is_out_of_flow(style.position)) {
            const auto justify = style.justify_self.value_or(c.justify_items)
                                     .resolve_self_relative(style.direction, c.direction, true);
            const auto align = style.align_self.value_or(c.align_items)
                                   .resolve_self_relative(style.direction, c.direction, false);
            const auto area =
                c.claims.for_position(style.position)
                    ? resolve_static_position_grid_area(style, resolver, row_counts, column_counts,
                                                        rows, columns, c.direction, c.border,
                                                        c.scrollbar, border_box)
                    : Rect<float>{c.border.left + c.padding.left + (rtl ? c.scrollbar.x : 0),
                                  border_box.width - c.border.right - c.padding.right -
                                      (rtl ? 0 : c.scrollbar.x),
                                  c.border.top + c.padding.top,
                                  border_box.height - c.border.bottom - c.padding.bottom -
                                      c.scrollbar.y};
            candidates.push(
                {child,
                 order++,
                 style.position,
                 {AxisStaticPosition::from_alignment(justify, {area.left, area.right}, rtl),
                  AxisStaticPosition::from_alignment(align, {area.top, area.bottom}, false)}});
        }
    }
    return candidates;
}
} // namespace

LayoutOutput compute_grid(LayoutTree& tree, NodeId node, const LayoutInput& in) {
    using namespace grid;
    const auto& style = tree.grid_container_style(node);
    const auto c = container_constants(tree, node, style, in);
    if (in.run_mode == RunMode::ComputeSize) {
        if (c.outer.width && c.outer.height)
            return LayoutOutput::from_outer_size({*c.outer.width, *c.outer.height});
        if (in.axis == RequestedAxis::Horizontal && c.outer.width)
            return LayoutOutput::from_outer_size({*c.outer.width, 0});
    }
    const CalcResolver calc = [&](Calc v, float basis) {
        return tree.resolve_calc(v, basis);
    };
    const auto auto_fit = c.outer.or_(c.maximum)
                              .or_(c.minimum)
                              .maybe_clamp(c.minimum, c.maximum)
                              .maybe_max(c.padding_border)
                              .maybe_sub(c.inset.sum_axes());
    const auto strategy = c.outer.or_(c.maximum).map([](auto size) {
        return size ? AutoRepeatStrategy::MaxRepetitionsThatDoNotOverflow
                    : AutoRepeatStrategy::MinRepetitionsThatDoOverflow;
    });
    const auto col = compute_explicit_grid_size_in_axis(style, auto_fit.width, strategy.width, calc,
                                                        AbsoluteAxis::Horizontal);
    const auto row = compute_explicit_grid_size_in_axis(style, auto_fit.height, strategy.height,
                                                        calc, AbsoluteAxis::Vertical);
    NamedLineResolver resolver(style, col.auto_repetitions, row.auto_repetitions);
    const Size<std::uint16_t> explicit_counts{
        std::min<std::uint16_t>(max_grid_tracks,
                                std::max(col.explicit_tracks, resolver.area_column_count())),
        std::min<std::uint16_t>(max_grid_tracks,
                                std::max(row.explicit_tracks, resolver.area_row_count()))};
    resolver.set_explicit_column_count(explicit_counts.width);
    resolver.set_explicit_row_count(explicit_counts.height);
    // Read child style and resolve names exactly once, before estimating occupancy.
    std::vector<PlacedGridItem> placed;
    std::vector<ItemPlacement> placements;
    std::vector<GridItem> items;
    const auto children = tree.child_ids(node);
    placed.reserve(children.size());
    placements.reserve(children.size());
    items.reserve(children.size());
    for (std::size_t i = 0; i < children.size(); ++i) {
        const auto& child_style = tree.grid_child_style(children[i]);
        if (child_style.box_generation_mode() == BoxGenerationMode::None ||
            is_out_of_flow(child_style.position))
            continue;
        const auto order = i;
        placed.push_back({children[i], order, {}, {}});
        placements.push_back(resolver.resolve(child_style));
        items.emplace_back(children[i], child_style, c.align_items, c.justify_items, order);
    }
    const auto estimated = compute_grid_size_estimate(explicit_counts, placements);
    CellOccupancy occupancy(estimated.width, estimated.height);
    place_grid_items(occupancy, placed, placements, style.grid_auto_flow);
    for (std::size_t i = 0; i < items.size(); ++i) {
        items[i].row = placed[i].row;
        items[i].column = placed[i].column;
    }
    const auto columns_count = occupancy.track_counts(AbsoluteAxis::Horizontal);
    const auto rows_count = occupancy.track_counts(AbsoluteAxis::Vertical);
    std::vector<GridTrack> columns, rows;
    initialize_grid_tracks(columns, columns_count, style.grid_template_columns,
                           style.grid_auto_columns, style.gap.width, col.auto_repetitions,
                           [&](auto index) {
                               return occupancy.column_is_occupied(static_cast<std::size_t>(index));
                           });
    initialize_grid_tracks(rows, rows_count, style.grid_template_rows, style.grid_auto_rows,
                           style.gap.height, row.auto_repetitions, [&](auto index) {
                               return occupancy.row_is_occupied(static_cast<std::size_t>(index));
                           });
    resolve_item_track_indexes(items, columns_count, rows_count);
    determine_if_item_crosses_flexible_or_intrinsic_tracks(items, columns, rows);
    const bool baselines = std::any_of(items.begin(), items.end(), [](const auto& item) {
        return item.participates_in_baseline_alignment();
    });
    auto inner = c.inner;
    track_sizing_algorithm(
        tree, AbstractAxis::Inline, c.inner_min.width, c.inner_max.width, c.justify_content,
        c.align_content, c.available, inner, columns, rows, items,
        [](const GridTrack& t, std::optional<float> basis, const LayoutTree& tr) {
            return t.max_track_sizing_function.definite_value(basis, [&](Calc v, float b) {
                return tr.resolve_calc(v, b);
            });
        },
        baselines);
    const float column_sum = track_sum(columns);
    if (!inner.width)
        inner.width = column_sum;
    if (in.run_mode == RunMode::ComputeSize && in.axis == RequestedAxis::Horizontal)
        return LayoutOutput::from_outer_size(
            {border_size({}, column_sum, c.inset.horizontal_axis_sum(), c.minimum.width,
                         c.maximum.width, c.padding_border.width),
             0});
    for (auto& item : items)
        item.grid_area_size_cache.reset();
    track_sizing_algorithm(
        tree, AbstractAxis::Block, c.inner_min.height, c.inner_max.height, c.align_content,
        c.justify_content, c.available, inner, rows, columns, items,
        [](const GridTrack& t, std::optional<float>, const LayoutTree&) {
            return std::optional<float>(t.base_size);
        },
        false);
    const auto preferred = in.known_dimensions.or_(c.preferred);
    Size<float> border_box{border_size(preferred.width, column_sum, c.inset.horizontal_axis_sum(),
                                       c.minimum.width, c.maximum.width, c.padding_border.width),
                           border_size(preferred.height, track_sum(rows),
                                       c.inset.vertical_axis_sum(), c.minimum.height,
                                       c.maximum.height, c.padding_border.height)};
    if (in.run_mode == RunMode::ComputeSize)
        return LayoutOutput::from_outer_size(border_box);
    auto content_box = (border_box - c.inset.sum_axes()).map([](float size) {
        return f32_max(0, size);
    });
    inner = content_box.map([](float size) {
        return std::optional<float>(size);
    });
    const auto changed = rerun_tracks(tree, c, inner, columns, rows, items, baselines);
    if (changed.width) {
        border_box.width =
            border_size(preferred.width, track_sum(columns), c.inset.horizontal_axis_sum(),
                        c.minimum.width, c.maximum.width, c.padding_border.width);
        content_box.width = f32_max(0, border_box.width - c.inset.horizontal_axis_sum());
    }
    if (changed.height) {
        border_box.height =
            border_size(preferred.height, track_sum(rows), c.inset.vertical_axis_sum(),
                        c.minimum.height, c.maximum.height, c.padding_border.height);
        content_box.height = f32_max(0, border_box.height - c.inset.vertical_axis_sum());
    }
    const bool rtl = c.direction == Direction::Rtl;
    const float gutter =
        f32_min(c.scrollbar.x, f32_max(border_box.width - c.padding_border.width, 0));
    align_tracks(content_box.width,
                 {c.padding.left + (rtl ? gutter : 0), c.padding.right + (rtl ? 0 : gutter)},
                 {c.border.left, c.border.right}, columns, c.justify_content, rtl);
    align_tracks(content_box.height, {c.padding.top, c.padding.bottom},
                 {c.border.top, c.border.bottom}, rows, c.align_content, false);
    Rect<float> overflow{};
    auto candidates = position_children(tree, node, c, border_box, items, rows, columns, rows_count,
                                        columns_count, resolver, overflow);
    tree.set_detailed_grid_info(node, make_detailed_grid_info(rows_count, columns_count, rows,
                                                              columns, items, resolver, style));
    std::optional<float> baseline;
    if (!items.empty() && !c.contain.suppresses_baseline()) {
        const auto first_row =
            std::min_element(items.begin(), items.end(), [](const auto& a, const auto& b) {
                return a.row_indexes.start < b.row_indexes.start;
            })->row_indexes.start;
        const GridItem* first = nullptr;
        for (const auto& item : items)
            if (item.row_indexes.start == first_row) {
                if (!first)
                    first = &item;
                if (item.participates_in_baseline_alignment()) {
                    first = &item;
                    break;
                }
            }
        baseline = first->y_position + first->baseline.value_or(first->height);
    }
    if (c.scroll_container) {
        overflow.right += rtl ? c.padding.left : c.padding.right;
        overflow.bottom += c.padding.bottom;
    }
    auto output = LayoutOutput::from_sizes_and_baselines(border_box, overflow,
                                                         Baselines::from_first(baseline));
    output.oof_candidates = std::move(candidates);
    const auto absolute_inset =
        c.border + Rect<float>{rtl ? c.scrollbar.x : 0, rtl ? 0 : c.scrollbar.x, 0, c.scrollbar.y};
    output.oof_positioning_area = OofPositioningArea{border_box - absolute_inset.sum_axes(),
                                                     {absolute_inset.left, absolute_inset.top}};
    return output;
}
} // namespace taffy
