// Native translation of the pinned Taffy grid item sizing model (MIT).
#include "../sizing.hpp"
#include "types.hpp"
#include <algorithm>
#include <stdexcept>

namespace taffy::grid {
namespace {
CalcResolver resolver(const LayoutTree& tree) {
    return [&tree](Calc c, float b) {
        return tree.resolve_calc(c, b);
    };
}
} // namespace

GridTrack GridTrack::gutter(LengthPercentage size) {
    GridTrack t{MinTrackSizingFunction(size), MaxTrackSizingFunction(size)};
    t.kind = GridTrackKind::Gutter;
    return t;
}

void GridTrack::collapse() {
    is_collapsed = true;
    min_track_sizing_function = MinTrackSizingFunction::zero();
    max_track_sizing_function = MaxTrackSizingFunction::zero();
    resolved_fit_content_limit = std::numeric_limits<float>::infinity();
}

bool GridTrack::uses_percentage() const {
    return min_track_sizing_function.uses_percentage() ||
           max_track_sizing_function.uses_percentage();
}

bool GridTrack::has_intrinsic_sizing_function() const {
    return min_track_sizing_function.is_intrinsic() || max_track_sizing_function.is_intrinsic();
}

void GridTrack::resolve_fit_content_limit(std::optional<float> basis, const CalcResolver& calc) {
    resolved_fit_content_limit = max_track_sizing_function.fit_content_limit(basis, calc)
                                     .value_or(std::numeric_limits<float>::infinity());
}

GridItem::GridItem(NodeId n, const Style& s, AlignItems align, AlignItems justify,
                   std::size_t order)
    : node(n), source_order(order), is_compressible_replaced(s.is_compressible_replaced()),
      is_replaced(s.is_replaced()), overflow(s.overflow), box_sizing(s.box_sizing), size(s.size),
      min_size(s.min_size), max_size(s.max_size), aspect_ratio(s.aspect_ratio), padding(s.padding),
      border(s.border), margin(s.margin), align_self(s.align_self.value_or(align)),
      justify_self(s.justify_self.value_or(justify)) {}

bool GridItem::has_auto_block_margin() const {
    return margin.top.is_auto() || margin.bottom.is_auto();
}

bool GridItem::has_cyclic_block_size_dependency() const {
    return size.height.uses_percentage() && (crosses_intrinsic_row || crosses_flexible_row);
}

bool GridItem::participates_in_baseline_alignment() const {
    return align_self.keyword == AlignItemsKeyword::Baseline && !has_auto_block_margin() &&
           !has_cyclic_block_size_dependency();
}

Line<std::int32_t> GridItem::placement(AbstractAxis a) const {
    return a == AbstractAxis::Block ? row : column;
}

Line<std::uint16_t> GridItem::placement_indexes(AbstractAxis a) const {
    return a == AbstractAxis::Block ? row_indexes : column_indexes;
}

std::pair<std::size_t, std::size_t> GridItem::track_range_excluding_lines(AbstractAxis a) const {
    auto i = placement_indexes(a);
    return {std::size_t(i.start) + 1, i.end};
}

std::span<const GridTrack> GridItem::spanned_tracks(AbstractAxis a,
                                                    std::span<const GridTrack> tracks) const {
    auto [start, end] = track_range_excluding_lines(a);
    if (start > end || end > tracks.size())
        throw std::out_of_range("grid item track range");
    return tracks.subspan(start, end - start);
}

std::span<GridTrack> GridItem::spanned_tracks(AbstractAxis a, std::span<GridTrack> tracks) const {
    auto [start, end] = track_range_excluding_lines(a);
    if (start > end || end > tracks.size())
        throw std::out_of_range("grid item track range");
    return tracks.subspan(start, end - start);
}

bool GridItem::spans_track_matching(AbstractAxis a, std::span<const GridTrack> t,
                                    const std::function<bool(const GridTrack&)>& p) const {
    auto s = spanned_tracks(a, t);
    return std::any_of(s.begin(), s.end(), p);
}

std::uint16_t GridItem::span(AbstractAxis a) const {
    auto p = placement(a);
    auto span = std::int64_t(p.end) - p.start;
    if (span < 0 || span > UINT16_MAX)
        throw std::overflow_error("grid item span");
    return static_cast<std::uint16_t>(span);
}

bool GridItem::crosses_flexible_track(AbstractAxis a) const {
    return a == AbstractAxis::Block ? crosses_flexible_row : crosses_flexible_column;
}

bool GridItem::crosses_intrinsic_track(AbstractAxis a) const {
    return a == AbstractAxis::Block ? crosses_intrinsic_row : crosses_intrinsic_column;
}

std::optional<float> GridItem::spanned_track_limit(AbstractAxis a, std::span<const GridTrack> t,
                                                   std::optional<float> b,
                                                   const CalcResolver& c) const {
    auto tracks = spanned_tracks(a, t);
    for (const auto& tr : tracks)
        if (!tr.max_track_sizing_function.definite_limit(b, c))
            return {};
    float sum = 0;
    for (const auto& tr : tracks)
        sum += *tr.max_track_sizing_function.definite_limit(b, c);
    return sum;
}

std::optional<float> GridItem::spanned_fixed_track_limit(AbstractAxis a,
                                                         std::span<const GridTrack> t,
                                                         std::optional<float> b,
                                                         const CalcResolver& c) const {
    auto tracks = spanned_tracks(a, t);
    for (const auto& tr : tracks)
        if (!tr.max_track_sizing_function.definite_value(b, c))
            return {};
    float sum = 0;
    for (const auto& tr : tracks)
        sum += *tr.max_track_sizing_function.definite_value(b, c);
    return sum;
}

OptionalSize GridItem::known_dimensions_cached(LayoutTree& tree, OptionalSize area) {
    if (known_dimensions_cache && known_dimensions_cache->grid_area_size == area)
        return known_dimensions_cache->known_dimensions;
    auto known = known_dimensions(tree, area);
    known_dimensions_cache = KnownDimensionsCacheEntry{area, known};
    return known;
}

bool GridItem::is_stretch_aligned(AlignSelf alignment) const {
    return alignment.keyword == AlignItemsKeyword::Stretch ||
           (alignment.keyword == AlignItemsKeyword::Normal && !is_replaced);
}

OptionalSize GridItem::known_dimensions(LayoutTree& tree, OptionalSize area) const {
    auto calc = resolver(tree);
    auto margins = margins_axis_sums_with_baseline_shims(area.width, tree);
    auto pb = (padding.resolve_or_zero(area.width, calc) + border.resolve_or_zero(area.width, calc))
                  .sum_axes();
    auto adjustment = box_sizing == BoxSizing::ContentBox ? pb : Size<float>{};
    auto inherent =
        size.maybe_resolve(area, calc).maybe_apply_aspect_ratio(aspect_ratio).maybe_add(adjustment);
    auto minimum = min_size.maybe_resolve(area, calc)
                       .maybe_apply_aspect_ratio(aspect_ratio)
                       .maybe_add(adjustment)
                       .or_(pb.map([](float f) {
                           return std::optional<float>(f);
                       }))
                       .maybe_max(pb);
    auto maximum = max_size.maybe_resolve(area, calc)
                       .maybe_apply_aspect_ratio(aspect_ratio)
                       .maybe_add(adjustment);
    auto remaining = area.maybe_sub(margins).maybe_max(Size<float>{});
    auto width = inherent.width;
    if (!width) {
        if (size.width.is_sizing_keyword()) {
            auto r = resolve_sizing_keyword(size.width, remaining.width, area.width, calc);
            if (r && r->kind == SizingKeywordResolution::Kind::Exact)
                width = r->exact;
        } else if (!margin.left.is_auto() && !margin.right.is_auto() &&
                   is_stretch_aligned(justify_self))
            width = remaining.width;
    }
    auto stretched = OptionalSize{width, inherent.height}.maybe_apply_aspect_ratio(aspect_ratio);
    auto stretched_height = stretched.height;
    if (!stretched_height) {
        if (size.height.is_sizing_keyword()) {
            auto r = resolve_sizing_keyword(size.height, remaining.height, area.height, calc);
            if (r && r->kind == SizingKeywordResolution::Kind::Exact)
                stretched_height = r->exact;
        } else if (!margin.top.is_auto() && !margin.bottom.is_auto() &&
                   is_stretch_aligned(align_self))
            stretched_height = remaining.height;
    }
    return OptionalSize{stretched.width, stretched_height}
        .maybe_apply_aspect_ratio(aspect_ratio)
        .maybe_clamp(minimum, maximum);
}

OptionalSize GridItem::grid_area_size(AbstractAxis a, std::span<const GridTrack> tracks,
                                      std::span<const GridTrack> others, OptionalSize available,
                                      const TrackSizeEstimate& estimate,
                                      const CalcResolver& calc) const {
    OptionalSize result{};
    std::optional<float> sum = 0.0f;
    for (const auto& t : spanned_tracks(a, tracks)) {
        auto min = t.min_track_sizing_function.definite_value(available.get(a), calc);
        // Rust's Option sum short-circuits at the first indefinite track.
        if (!min) {
            sum.reset();
            break;
        }
        auto max = t.max_track_sizing_function.definite_value(available.get(a), calc);
        if (!max || *min != *max) {
            sum.reset();
            break;
        }
        *sum += t.base_size;
    }
    result.set(a, sum);
    sum = 0.0f;
    for (const auto& t : spanned_tracks(other(a), others)) {
        auto s = estimate(t, available.get(other(a)));
        if (!s) {
            sum.reset();
            break;
        }
        *sum += *s + t.content_alignment_adjustment;
    }
    result.set(other(a), sum);
    return result;
}

OptionalSize GridItem::grid_area_size_cached(AbstractAxis a, std::span<const GridTrack> t,
                                             std::span<const GridTrack> o, OptionalSize available,
                                             const TrackSizeEstimate& e, const CalcResolver& c) {
    if (!grid_area_size_cache)
        grid_area_size_cache = grid_area_size(a, t, o, available, e, c);
    return *grid_area_size_cache;
}

Size<float> GridItem::margins_axis_sums_with_baseline_shims(std::optional<float> width,
                                                            const LayoutTree& tree) const {
    auto calc = resolver(tree);
    return {margin.left.resolve_or_zero(0.0f, calc) + margin.right.resolve_or_zero(0.0f, calc),
            margin.top.resolve_or_zero(width, calc) + baseline_shim +
                margin.bottom.resolve_or_zero(width, calc)};
}

OptionalSize GridItem::available_space_minus_margins(OptionalSize area, OptionalSize available,
                                                     const LayoutTree& tree) const {
    if (!available.width && !available.height)
        return available;
    auto m = margin.resolve_or_zero(area.width, resolver(tree));
    return {maybe_max(maybe_sub(available.width, m.horizontal_axis_sum()), 0.0f),
            maybe_max(maybe_sub(available.height, m.vertical_axis_sum() + baseline_shim), 0.0f)};
}

Size<AvailableSpace> GridItem::keyword_adjusted_available_space(OptionalSize area,
                                                                Size<AvailableSpace> available,
                                                                const LayoutTree& tree) const {
    if (!size.width.is_sizing_keyword() && !size.height.is_sizing_keyword())
        return available;
    auto margins = margins_axis_sums_with_baseline_shims(area.width, tree);
    for (auto a : {AbstractAxis::Inline, AbstractAxis::Block}) {
        if (!size.get(a).is_sizing_keyword())
            continue;
        auto stretch = maybe_max(maybe_sub(area.get(a), margins.get(a)), 0.0f);
        auto r = resolve_sizing_keyword(size.get(a), stretch, area.get(a), resolver(tree));
        if (r && r->kind == SizingKeywordResolution::Kind::Measure)
            available.set(a, r->available_space);
    }
    return available;
}

float GridItem::min_content_contribution(AbstractAxis a, LayoutTree& tree, OptionalSize area,
                                         OptionalSize available) {
    auto known = known_dimensions_cached(tree, area);
    if (known.get(a))
        return *known.get(a);
    auto space = available_space_minus_margins(area, available, tree).map([](auto v) {
        return v ? AvailableSpace::definite(*v) : AvailableSpace::min_content();
    });
    return tree.measure_child_size(node, known, area,
                                   keyword_adjusted_available_space(area, space, tree),
                                   SizingMode::InherentSize, as_abs_naive(a), {});
}

float GridItem::max_content_contribution(AbstractAxis a, LayoutTree& tree, OptionalSize area,
                                         OptionalSize available) {
    auto known = known_dimensions_cached(tree, area);
    if (known.get(a))
        return *known.get(a);
    auto space = available_space_minus_margins(area, available, tree).map([](auto v) {
        return v ? AvailableSpace::definite(*v) : AvailableSpace::max_content();
    });
    return tree.measure_child_size(node, known, area,
                                   keyword_adjusted_available_space(area, space, tree),
                                   SizingMode::InherentSize, as_abs_naive(a), {});
}

float GridItem::min_content_contribution_cached(AbstractAxis a, LayoutTree& tree, OptionalSize area,
                                                OptionalSize available) {
    auto& c = min_content_contribution_cache.get(a);
    if (!c)
        c = min_content_contribution(a, tree, area, available);
    return *c;
}

float GridItem::max_content_contribution_cached(AbstractAxis a, LayoutTree& tree, OptionalSize area,
                                                OptionalSize available) {
    auto& c = max_content_contribution_cache.get(a);
    if (!c)
        c = max_content_contribution(a, tree, area, available);
    return *c;
}

float GridItem::minimum_contribution(LayoutTree& tree, AbstractAxis a,
                                     std::span<const GridTrack> tracks, OptionalSize area,
                                     OptionalSize inner) {
    auto calc = resolver(tree);
    auto pb = (padding.resolve_or_zero(area.width, calc) + border.resolve_or_zero(area.width, calc))
                  .sum_axes();
    auto adjustment = box_sizing == BoxSizing::ContentBox ? pb : Size<float>{};
    auto preferred = size.maybe_resolve(area, calc)
                         .maybe_apply_aspect_ratio(aspect_ratio)
                         .maybe_add(adjustment)
                         .get(a);
    if (preferred) {
        auto min = min_size.get(a);
        auto max = max_size.get(a);
        if (min.is_auto() && max.is_auto())
            return *preferred;
        auto basis = area.get(a);
        auto max_basis = is_replaced && !basis ? std::optional<float>(0.0f) : basis;
        return maybe_clamp(*preferred, maybe_add(min.maybe_resolve(basis, calc), adjustment.get(a)),
                           maybe_add(max.maybe_resolve(max_basis, calc), adjustment.get(a)));
    }
    if (size.get(a).is_sizing_keyword() && !size.get(a).is_stretch())
        return min_content_contribution_cached(a, tree, area, area);
    auto min = min_size
                   .maybe_resolve(area.map([](auto s) {
                       return s.value_or(0);
                   }),
                                  calc)
                   .maybe_apply_aspect_ratio(aspect_ratio)
                   .maybe_add(adjustment)
                   .get(a);
    if (min)
        return *min;
    auto automatic = maybe_into_automatic_min_size(overflow.get(a));
    if (automatic)
        return *automatic;
    auto spanned = spanned_tracks(a, tracks);
    bool auto_track = std::any_of(spanned.begin(), spanned.end(), [&](const auto& t) {
        return t.min_track_sizing_function.behaves_as_auto(inner.get(a));
    });
    bool flex = std::any_of(spanned.begin(), spanned.end(), [](const auto& t) {
        return t.is_flexible();
    });
    if (!auto_track || (spanned.size() != 1 && flex))
        return 0;
    float contribution = min_content_contribution_cached(a, tree, area, area);
    if (is_compressible_replaced)
        contribution = maybe_min(maybe_min(contribution, size.get(a).maybe_resolve(0.0f, calc)),
                                 max_size.get(a).maybe_resolve(0.0f, calc));
    auto limit = spanned_fixed_track_limit(a, tracks, inner.get(a), calc);
    return limit ? f32_min(contribution, f32_max(*limit, pb.get(a))) : contribution;
}

float GridItem::minimum_contribution_cached(LayoutTree& tree, AbstractAxis a,
                                            std::span<const GridTrack> tracks, OptionalSize area,
                                            OptionalSize inner) {
    auto& c = minimum_contribution_cache.get(a);
    if (!c)
        c = minimum_contribution(tree, a, tracks, area, inner);
    return *c;
}
} // namespace taffy::grid
