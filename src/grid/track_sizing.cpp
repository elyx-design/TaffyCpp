// Native translation of Taffy's CSS Grid track sizing algorithm (MIT).
#include "track_sizing.hpp"
#include "../compute_common.hpp"
#include "placement.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <vector>

namespace taffy::grid {
namespace {
constexpr float infinity = std::numeric_limits<float>::infinity();

CalcResolver resolver(const LayoutTree& tree) {
    return [&tree](Calc c, float b) {
        return tree.resolve_calc(c, b);
    };
}

// Rust f32::total_cmp ordering, including signed zero and NaNs.
bool total_less(float a, float b) {
    auto key = [](float f) {
        auto i = std::bit_cast<std::int32_t>(f);
        return i ^ static_cast<std::int32_t>(static_cast<std::uint32_t>(i >> 31) >> 1);
    };
    return key(a) < key(b);
}

float sum_base(std::span<const GridTrack> tracks) {
    float sum = 0;
    for (const auto& t : tracks)
        sum += t.base_size;
    return sum;
}

struct IntrinsicSizeMeasurer {
    LayoutTree& tree;
    std::span<const GridTrack> other_tracks;
    AbstractAxis axis;
    OptionalSize inner;
    const TrackSizeEstimateWithTree& estimate;

    OptionalSize grid_area_size(GridItem& item, std::span<const GridTrack> tracks) {
        return item.grid_area_size_cached(
            axis, tracks, other_tracks, inner,
            [&](const auto& t, auto b) {
                return estimate(t, b, tree);
            },
            resolver(tree));
    }

    float min_content_contribution(GridItem& item, std::span<const GridTrack> tracks) {
        auto area = grid_area_size(item, tracks);
        auto available = area.with(axis, std::nullopt);
        auto margins = item.margins_axis_sums_with_baseline_shims(available.width, tree);
        return item.min_content_contribution_cached(axis, tree, area, available) +
               margins.get(axis);
    }

    float max_content_contribution(GridItem& item, std::span<const GridTrack> tracks) {
        auto area = grid_area_size(item, tracks);
        auto available = area.with(axis, std::nullopt);
        auto margins = item.margins_axis_sums_with_baseline_shims(available.width, tree);
        return item.max_content_contribution_cached(axis, tree, area, available) +
               margins.get(axis);
    }

    float minimum_contribution(GridItem& item, std::span<const GridTrack> tracks) {
        auto area = grid_area_size(item, tracks);
        auto available = area.with(axis, std::nullopt);
        auto margins = item.margins_axis_sums_with_baseline_shims(available.width, tree);
        return item.minimum_contribution_cached(tree, axis, tracks, area, inner) +
               margins.get(axis);
    }
};
} // namespace

float compute_alignment_gutter_adjustment(AlignContent alignment, std::optional<float> inner,
                                          const TrackSizeEstimate& estimate,
                                          std::span<const GridTrack> tracks) {
    if (tracks.size() <= 1)
        return 0;
    std::size_t outer = 1, weight = 0;
    switch (alignment.keyword) {
    case AlignContentKeyword::Normal:
    case AlignContentKeyword::Stretch:
        outer = 0;
        break;
    case AlignContentKeyword::SpaceBetween:
        outer = 0;
        weight = 1;
        break;
    case AlignContentKeyword::SpaceAround:
        weight = 2;
        break;
    case AlignContentKeyword::SpaceEvenly:
        weight = 1;
        break;
    default:
        break;
    }
    if (!weight || !inner)
        return 0;
    float sum = 0;
    for (const auto& t : tracks) {
        auto size = estimate(t, inner);
        if (!size)
            return 0;
        sum += *size;
    }
    auto free = f32_max(0, *inner - sum);
    auto weighted_count = ((tracks.size() - 3) / 2) * weight + 2 * outer;
    return (free / static_cast<float>(weighted_count)) * static_cast<float>(weight);
}

void resolve_item_track_indexes(std::span<GridItem> items, TrackCounts columns, TrackCounts rows) {
    auto index = [](TrackCounts c, std::int32_t line) {
        auto i = c.track_vec_index(line);
        if (i > UINT16_MAX)
            throw std::overflow_error("grid track vector index");
        return static_cast<std::uint16_t>(i);
    };
    for (auto& item : items) {
        item.column_indexes = item.column.map([&](auto l) {
            return index(columns, l);
        });
        item.row_indexes = item.row.map([&](auto l) {
            return index(rows, l);
        });
    }
}

void determine_if_item_crosses_flexible_or_intrinsic_tracks(std::span<GridItem> items,
                                                            std::span<const GridTrack> columns,
                                                            std::span<const GridTrack> rows) {
    for (auto& i : items) {
        auto flex = [](const GridTrack& t) {
            return t.is_flexible();
        };
        auto intrinsic = [](const GridTrack& t) {
            return t.has_intrinsic_sizing_function();
        };
        i.crosses_flexible_column = i.spans_track_matching(AbstractAxis::Inline, columns, flex);
        i.crosses_intrinsic_column =
            i.spans_track_matching(AbstractAxis::Inline, columns, intrinsic);
        i.crosses_flexible_row = i.spans_track_matching(AbstractAxis::Block, rows, flex);
        i.crosses_intrinsic_row = i.spans_track_matching(AbstractAxis::Block, rows, intrinsic);
    }
}

void initialize_track_sizes(const LayoutTree& tree, std::span<GridTrack> tracks,
                            std::optional<float> basis) {
    auto calc = resolver(tree);
    for (auto& t : tracks) {
        t.base_size = t.min_track_sizing_function.definite_value(basis, calc).value_or(0);
        t.growth_limit = t.max_track_sizing_function.definite_value(basis, calc).value_or(infinity);
        if (t.growth_limit < t.base_size)
            t.growth_limit = t.base_size;
    }
}

void resolve_item_baselines(LayoutTree& tree, AbstractAxis axis, std::span<GridItem> items,
                            OptionalSize inner) {
    std::vector<GridItem*> sorted;
    sorted.reserve(items.size());
    for (auto& i : items)
        sorted.push_back(&i);
    auto other_axis = other(axis);
    std::stable_sort(sorted.begin(), sorted.end(), [&](auto a, auto b) {
        return a->placement(other_axis).start < b->placement(other_axis).start;
    });
    for (std::size_t start = 0; start < sorted.size();) {
        auto end = start + 1;
        auto row = sorted[start]->placement(other_axis).start;
        while (end < sorted.size() && sorted[end]->placement(other_axis).start == row)
            ++end;
        std::size_t count = 0;
        for (auto k = start; k < end; ++k)
            count += sorted[k]->participates_in_baseline_alignment();
        if (count > 1) {
            for (auto k = start; k < end; ++k) {
                auto& i = *sorted[k];
                if (!i.participates_in_baseline_alignment())
                    continue;
                auto layout = tree.perform_child_layout(i.node, {}, inner,
                                                        Size<AvailableSpace>::min_content(),
                                                        SizingMode::InherentSize, {});
                auto height = layout.size.height;
                i.baseline = child_first_baseline(layout.baselines.first, height, i.overflow.y)
                                 .value_or(height);
            }
            auto margin_box_baseline = [&](const GridItem& i) {
                return i.baseline.value_or(0) +
                       i.margin.top.resolve_or_zero(inner.width, resolver(tree));
            };
            std::optional<float> maximum;
            for (auto k = start; k < end; ++k)
                if (sorted[k]->participates_in_baseline_alignment()) {
                    float b = margin_box_baseline(*sorted[k]);
                    if (!maximum || !total_less(b, *maximum))
                        maximum = b;
                }
            for (auto k = start; k < end; ++k)
                if (sorted[k]->participates_in_baseline_alignment()) {
                    sorted[k]->baseline_shim = *maximum - margin_box_baseline(*sorted[k]);
                    sorted[k]->known_dimensions_cache.reset();
                }
        }
        start = end;
    }
}

void flush_planned_base_size_increases(std::span<GridTrack> tracks) {
    for (auto& t : tracks) {
        t.base_size += t.base_size_planned_increase;
        t.base_size_planned_increase = 0;
    }
}

void flush_planned_growth_limit_increases(std::span<GridTrack> tracks, bool growable) {
    for (auto& t : tracks) {
        if (t.growth_limit_planned_increase > 0) {
            t.growth_limit = (t.growth_limit == infinity ? t.base_size : t.growth_limit) +
                             t.growth_limit_planned_increase;
            t.infinitely_growable = growable;
        } else
            t.infinitely_growable = false;
        t.growth_limit_planned_increase = 0;
    }
}

float crossed_flex_factor_sum(std::span<const GridTrack> tracks) {
    float sum = 0;
    for (const auto& t : tracks)
        if (t.is_flexible())
            sum += t.flex_factor();
    return sum;
}

float distribute_space_up_to_limits(float space, std::span<GridTrack> tracks,
                                    const TrackPredicate& affected, const TrackProperty& proportion,
                                    const TrackProperty& property, const TrackProperty& limit) {
    constexpr float threshold = 0.01f;
    for (std::size_t iteration = 0; iteration < tracks.size() + 1; ++iteration) {
        if (space <= threshold)
            break;
        float sum = 0;
        for (const auto& t : tracks)
            if (property(t) + t.item_incurred_increase < limit(t) && affected(t))
                sum += proportion(t);
        if (sum == 0)
            break;
        std::optional<float> min_limit;
        for (const auto& t : tracks)
            if (property(t) + t.item_incurred_increase < limit(t) && affected(t)) {
                float v = (limit(t) - property(t) - t.item_incurred_increase) / proportion(t);
                if (!min_limit || total_less(v, *min_limit))
                    min_limit = v;
            }
        float increase = f32_min(min_limit.value(), space / sum);
        for (auto& t : tracks)
            if (affected(t)) {
                float inc = increase * proportion(t);
                if (inc > 0 &&
                    property(t) + t.item_incurred_increase + inc <= limit(t) + threshold) {
                    t.item_incurred_increase += inc;
                    space -= inc;
                }
            }
    }
    return space;
}

void distribute_item_space_to_base_size(bool is_flex, float space, std::span<GridTrack> tracks,
                                        const TrackPredicate& affected, const TrackProperty& limit,
                                        IntrinsicContributionType type) {
    TrackPredicate filter = [&](const GridTrack& t) {
        return affected(t) && (!is_flex || t.is_flexible());
    };
    float factor_sum = 0;
    if (is_flex)
        for (const auto& t : tracks)
            if (filter(t))
                factor_sum += t.flex_factor();
    TrackProperty proportion = [=](const GridTrack& t) {
        return is_flex && factor_sum > 0 ? t.flex_factor() : 1.0f;
    };
    if (space == 0 || !std::any_of(tracks.begin(), tracks.end(), filter))
        return;
    auto base = [](const GridTrack& t) {
        return t.base_size;
    };
    auto extra = f32_max(0, space - sum_base(tracks));
    extra = distribute_space_up_to_limits(extra, tracks, filter, proportion, base, limit);
    if (extra > 0.000001f) {
        auto preferred = [=](const GridTrack& t) {
            return type == IntrinsicContributionType::Minimum
                       ? t.max_track_sizing_function.is_intrinsic()
                       : t.max_track_sizing_function.is_max_or_fit_content();
        };
        bool has_preferred = std::any_of(tracks.begin(), tracks.end(), [&](const auto& t) {
            return filter(t) && preferred(t);
        });
        distribute_space_up_to_limits(
            extra, tracks,
            [&](const auto& t) {
                return filter(t) && (!has_preferred || preferred(t));
            },
            proportion, base,
            [](const auto& t) {
                return t.fit_content_limit();
            });
    }
    for (auto& t : tracks) {
        if (t.item_incurred_increase > t.base_size_planned_increase)
            t.base_size_planned_increase = t.item_incurred_increase;
        t.item_incurred_increase = 0;
    }
}

void distribute_item_space_to_growth_limit(float space, std::span<GridTrack> tracks,
                                           const TrackPredicate& affected) {
    if (space == 0 || !std::any_of(tracks.begin(), tracks.end(), affected))
        return;
    auto property = [](const GridTrack& t) {
        return t.growth_limit == infinity ? t.base_size : t.growth_limit;
    };
    float sum = 0;
    for (const auto& t : tracks)
        sum += property(t);
    auto proportion = [](const GridTrack&) {
        return 1.0f;
    };
    auto limit = [](const GridTrack& t) {
        return t.fit_content_limit();
    };
    float extra = distribute_space_up_to_limits(
        f32_max(0, space - sum), tracks,
        [&](const auto& t) {
            return affected(t) && (t.infinitely_growable || t.growth_limit == infinity);
        },
        proportion, property, limit);
    if (extra > 0)
        distribute_space_up_to_limits(extra, tracks, affected, proportion, property, limit);
    for (auto& t : tracks) {
        if (t.item_incurred_increase > t.growth_limit_planned_increase)
            t.growth_limit_planned_increase = t.item_incurred_increase;
        t.item_incurred_increase = 0;
    }
}

void resolve_intrinsic_track_sizes(LayoutTree& tree, AbstractAxis axis, std::span<GridTrack> tracks,
                                   std::span<const GridTrack> others, std::span<GridItem> items,
                                   AvailableSpace available, OptionalSize inner,
                                   const TrackSizeEstimateWithTree& estimate) {
    // FrontBackVecBuilder preserves the front order and reverses the back order.
    std::vector<GridItem*> ordered;
    ordered.reserve(items.size());
    for (auto& i : items)
        if (!i.crosses_flexible_track(axis))
            ordered.push_back(&i);
    auto non_flex_count = ordered.size();
    bool needs_sort = std::any_of(ordered.begin(), ordered.end(), [&](auto i) {
        return i->span(axis) > 1;
    });
    if (needs_sort)
        std::stable_sort(ordered.begin(), ordered.end(), [&](auto a, auto b) {
            return a->span(axis) < b->span(axis);
        });
    for (auto i = items.rbegin(); i != items.rend(); ++i)
        if (i->crosses_flexible_track(axis))
            ordered.push_back(&*i);
    IntrinsicSizeMeasurer sizer{tree, others, axis, inner, estimate};
    auto basis = inner.get(axis);
    auto calc = resolver(tree);
    const auto growth = [](const GridTrack& t) {
        return t.growth_limit;
    };
    const auto fit_growth = [](const GridTrack& t) {
        return t.fit_content_limited_growth_limit();
    };
    const auto min_type = IntrinsicContributionType::Minimum,
               max_type = IntrinsicContributionType::Maximum;
    for (std::size_t start = 0; start < ordered.size();) {
        bool flex = start >= non_flex_count;
        auto span = ordered[start]->span(axis);
        auto end = start + 1;
        if (flex)
            end = ordered.size();
        else
            while (end < non_flex_count && ordered[end]->span(axis) == span)
                ++end;
        auto batch = std::span<GridItem*>(ordered).subspan(start, end - start);
        start = end;
        if (!flex && span == 1) {
            for (auto item : batch) {
                auto index = std::size_t(item->placement_indexes(axis).start) + 1;
                if (index >= tracks.size())
                    throw std::out_of_range("grid intrinsic track index");
                auto& track = tracks[index];
                auto min = track.min_track_sizing_function;
                float new_base = track.base_size;
                if (min.is_min_content())
                    new_base =
                        f32_max(track.base_size, sizer.min_content_contribution(*item, tracks));
                else if (min.is_max_content())
                    new_base =
                        f32_max(track.base_size, sizer.max_content_contribution(*item, tracks));
                else if (min.behaves_as_auto(basis)) {
                    float space;
                    if (!available.is_definite() &&
                        !is_scroll_container(item->overflow.get(axis))) {
                        auto minimum = sizer.minimum_contribution(*item, tracks);
                        auto content = sizer.min_content_contribution(*item, tracks);
                        auto limit = track.max_track_sizing_function.definite_limit(basis, calc);
                        space = f32_max(maybe_min(content, limit), minimum);
                    } else
                        space = sizer.minimum_contribution(*item, tracks);
                    new_base = f32_max(track.base_size, space);
                }
                track.base_size = new_base;
                auto max = track.max_track_sizing_function;
                auto fit_limit = track.fit_content_limit();
                std::optional<float> min_content;
                if (track.has_intrinsic_sizing_function())
                    min_content = sizer.min_content_contribution(*item, tracks);
                std::optional<float> planned;
                if (max.is_fit_content()) {
                    float content =
                        f32_min(sizer.max_content_contribution(*item, tracks), fit_limit);
                    planned = min_content && !is_scroll_container(item->overflow.get(axis))
                                  ? f32_max(*min_content, content)
                                  : content;
                } else if (max.is_max_content_alike() || (max.uses_percentage() && !basis))
                    planned = sizer.max_content_contribution(*item, tracks);
                else if (max.is_intrinsic())
                    planned = min_content;
                if (planned)
                    track.growth_limit_planned_increase =
                        f32_max(track.growth_limit_planned_increase, *planned);
            }
            for (auto& t : tracks) {
                if (t.growth_limit_planned_increase > 0)
                    t.growth_limit = t.growth_limit == infinity
                                         ? t.growth_limit_planned_increase
                                         : f32_max(t.growth_limit, t.growth_limit_planned_increase);
                t.infinitely_growable = false;
                t.growth_limit_planned_increase = 0;
                if (t.growth_limit < t.base_size)
                    t.growth_limit = t.base_size;
            }
            continue;
        }
        // 1. Intrinsic minimums, including the browser scroll-container and <1fr rules.
        for (auto item : batch)
            if (item->crosses_intrinsic_track(axis)) {
                float space;
                if (!available.is_definite() && !is_scroll_container(item->overflow.get(axis))) {
                    float minimum = sizer.minimum_contribution(*item, tracks);
                    float content = sizer.min_content_contribution(*item, tracks);
                    auto limit = item->spanned_track_limit(axis, tracks, basis, calc);
                    float limited = f32_max(maybe_min(content, limit), minimum);
                    if (flex) {
                        auto spanned = item->spanned_tracks(axis, tracks);
                        float inflexible = 0;
                        for (const auto& t : spanned)
                            if (!t.is_flexible())
                                inflexible += t.base_size;
                        float scale = f32_min(crossed_flex_factor_sum(spanned), 1);
                        space =
                            f32_max(minimum, inflexible + f32_max(limited - inflexible, 0) * scale);
                    } else
                        space = limited;
                } else
                    space = sizer.minimum_contribution(*item, tracks);
                if (space > 0) {
                    auto affected = [&](const GridTrack& t) {
                        return !t.min_track_sizing_function.definite_value(basis, calc);
                    };
                    TrackProperty limit = is_scroll_container(item->overflow.get(axis))
                                              ? TrackProperty(fit_growth)
                                              : TrackProperty(growth);
                    distribute_item_space_to_base_size(
                        flex, space, item->spanned_tracks(axis, tracks), affected, limit, min_type);
                }
            }
        flush_planned_base_size_increases(tracks);
        // 2. Content-based minimums.
        auto min_or_max = [](const GridTrack& t) {
            return t.min_track_sizing_function.is_min_or_max_content();
        };
        for (auto item : batch)
            if (item->spans_track_matching(axis, tracks, min_or_max)) {
                float space = sizer.min_content_contribution(*item, tracks);
                if (space > 0) {
                    TrackProperty limit = is_scroll_container(item->overflow.get(axis))
                                              ? TrackProperty(fit_growth)
                                              : TrackProperty(growth);
                    distribute_item_space_to_base_size(flex, space,
                                                       item->spanned_tracks(axis, tracks),
                                                       min_or_max, limit, min_type);
                }
            }
        flush_planned_base_size_increases(tracks);
        // 3. Max-content minimums. Browser priority: max-content before auto.
        auto auto_min = [](const GridTrack& t) {
            return t.min_track_sizing_function.is_auto() &&
                   !t.max_track_sizing_function.is_min_content();
        };
        auto max_min = [](const GridTrack& t) {
            return t.min_track_sizing_function.is_max_content();
        };
        if (available.is_max_content()) {
            for (auto item : batch)
                if (item->spans_track_matching(axis, tracks, [&](const auto& t) {
                        return auto_min(t) || max_min(t);
                    })) {
                    float space = maybe_min(sizer.max_content_contribution(*item, tracks),
                                            item->spanned_track_limit(axis, tracks, basis, calc));
                    auto spanned = item->spanned_tracks(axis, tracks);
                    if (flex) {
                        float inflexible = 0;
                        for (const auto& t : spanned)
                            if (!t.is_flexible())
                                inflexible += t.base_size;
                        float scale = f32_min(crossed_flex_factor_sum(spanned), 1);
                        space = inflexible + f32_max(space - inflexible, 0) * scale;
                    }
                    if (space > 0) {
                        if (std::any_of(spanned.begin(), spanned.end(), max_min))
                            distribute_item_space_to_base_size(
                                flex, space, spanned, max_min,
                                [](const auto&) {
                                    return infinity;
                                },
                                max_type);
                        else
                            distribute_item_space_to_base_size(flex, space, spanned, auto_min,
                                                               fit_growth, max_type);
                    }
                }
            flush_planned_base_size_increases(tracks);
        }
        for (auto item : batch)
            if (item->spans_track_matching(axis, tracks, max_min)) {
                float space = sizer.max_content_contribution(*item, tracks);
                if (space > 0)
                    distribute_item_space_to_base_size(
                        flex, space, item->spanned_tracks(axis, tracks), max_min, growth, max_type);
            }
        flush_planned_base_size_increases(tracks);
        // 4. Growth limits are floored by base sizes.
        for (auto& t : tracks)
            if (t.growth_limit < t.base_size)
                t.growth_limit = t.base_size;
        if (!flex) {
            // 5. Intrinsic maximums.
            auto intrinsic_max = [=](const GridTrack& t) {
                return !t.max_track_sizing_function.has_definite_value(basis);
            };
            for (auto item : batch)
                if (item->spans_track_matching(axis, tracks, intrinsic_max)) {
                    float space = sizer.min_content_contribution(*item, tracks);
                    if (space > 0)
                        distribute_item_space_to_growth_limit(
                            space, item->spanned_tracks(axis, tracks), intrinsic_max);
                }
            flush_planned_growth_limit_increases(tracks, true);
            // 6. Max-content maximums, fit-content capped.
            auto max_content_max = [=](const GridTrack& t) {
                return t.max_track_sizing_function.is_max_content_alike() ||
                       (t.max_track_sizing_function.uses_percentage() && !basis);
            };
            for (auto item : batch)
                if (item->spans_track_matching(axis, tracks, max_content_max)) {
                    float space = sizer.max_content_contribution(*item, tracks);
                    if (space > 0)
                        distribute_item_space_to_growth_limit(
                            space, item->spanned_tracks(axis, tracks), max_content_max);
                }
            flush_planned_growth_limit_increases(tracks, false);
        }
    }
    for (auto& t : tracks)
        if (t.growth_limit == infinity)
            t.growth_limit = t.base_size;
}

void maximise_tracks(std::span<GridTrack> tracks, AvailableSpace available) {
    float free = available.compute_free_space(sum_base(tracks));
    if (free == infinity)
        for (auto& t : tracks)
            t.base_size = t.growth_limit;
    else if (free > 0) {
        distribute_space_up_to_limits(
            free, tracks,
            [](const auto&) {
                return true;
            },
            [](const auto&) {
                return 1.0f;
            },
            [](const auto& t) {
                return t.base_size;
            },
            [](const auto& t) {
                return t.fit_content_limited_growth_limit();
            });
        for (auto& t : tracks) {
            t.base_size += t.item_incurred_increase;
            t.item_incurred_increase = 0;
        }
    }
}

float find_size_of_fr(std::span<const GridTrack> tracks, float space) {
    if (space == 0)
        return 0;
    float hypothetical = infinity;
    for (std::size_t iteration = 0; iteration < tracks.size() + 1; ++iteration) {
        float used = 0, sum = 0;
        for (const auto& t : tracks) {
            if (t.is_flexible() && t.flex_factor() * hypothetical >= t.base_size)
                sum += t.flex_factor();
            else
                used += t.base_size;
        }
        float previous = hypothetical;
        hypothetical = (space - used) / f32_max(sum, 1);
        bool valid = std::all_of(tracks.begin(), tracks.end(), [&](const auto& t) {
            return !t.is_flexible() || t.flex_factor() * hypothetical >= t.base_size ||
                   t.flex_factor() * previous < t.base_size;
        });
        if (valid)
            break;
    }
    return hypothetical;
}

void expand_flexible_tracks(LayoutTree& tree, AbstractAxis axis, std::span<GridTrack> tracks,
                            std::span<const GridTrack> others, std::span<GridItem> items,
                            std::optional<float> minimum, std::optional<float> maximum,
                            AvailableSpace available, OptionalSize inner,
                            const TrackSizeEstimateWithTree& estimate) {
    IntrinsicSizeMeasurer sizer{tree, others, axis, inner, estimate};
    float fraction = 0;
    if (available.is_definite()) {
        const auto free_space = available.value() - sum_base(tracks);
        // Keep Rust's <= branch: NaN must enter flexible sizing, not look full.
        fraction = free_space <= 0 ? 0 : find_size_of_fr(tracks, available.value());
    } else if (available.is_max_content()) {
        std::optional<float> track_max, item_max;
        for (const auto& t : tracks)
            if (t.is_flexible()) {
                float f = t.flex_factor();
                float v = f > 1 ? t.base_size / f : t.base_size;
                if (!track_max || !total_less(v, *track_max))
                    track_max = v;
            }
        for (auto& i : items)
            if (i.crosses_flexible_track(axis)) {
                float content = sizer.max_content_contribution(i, tracks);
                float v = find_size_of_fr(
                    i.spanned_tracks(axis, std::span<const GridTrack>(tracks)), content);
                if (!item_max || !total_less(v, *item_max))
                    item_max = v;
            }
        fraction = f32_max(track_max.value_or(0), item_max.value_or(0));
        float hypothetical = 0;
        for (const auto& t : tracks)
            hypothetical +=
                t.is_flexible() ? f32_max(t.base_size, t.flex_factor() * fraction) : t.base_size;
        if (hypothetical < minimum.value_or(0))
            fraction = find_size_of_fr(tracks, *minimum);
        else if (hypothetical > maximum.value_or(infinity))
            fraction = find_size_of_fr(tracks, *maximum);
    }
    for (auto& t : tracks)
        if (t.is_flexible())
            t.base_size = f32_max(t.base_size, t.flex_factor() * fraction);
}

void stretch_auto_tracks(std::span<GridTrack> tracks, std::optional<float> minimum,
                         AvailableSpace available) {
    auto count = std::count_if(tracks.begin(), tracks.end(), [](const auto& t) {
        return t.max_track_sizing_function.is_auto();
    });
    if (!count)
        return;
    float used = sum_base(tracks);
    float free = available.is_definite() ? available.compute_free_space(used)
                                         : (minimum ? *minimum - used : 0);
    if (free > 0)
        for (auto& t : tracks)
            if (t.max_track_sizing_function.is_auto())
                t.base_size += free / static_cast<float>(count);
}

void track_sizing_algorithm(LayoutTree& tree, AbstractAxis axis, std::optional<float> minimum,
                            std::optional<float> maximum, AlignContent alignment,
                            AlignContent other_alignment, Size<AvailableSpace> available,
                            OptionalSize inner, std::span<GridTrack> tracks,
                            std::span<GridTrack> others, std::span<GridItem> items,
                            const TrackSizeEstimateWithTree& estimate, bool baseline) {
    auto basis = inner.get(axis) ? inner.get(axis) : minimum;
    initialize_track_sizes(tree, tracks, basis);
    auto calc = resolver(tree);
    for (auto& t : tracks)
        t.resolve_fit_content_limit(inner.get(axis), calc);
    if (baseline)
        resolve_item_baselines(tree, axis, items, inner);
    if (std::all_of(tracks.begin(), tracks.end(), [&](const auto& t) {
            return t.base_size == t.growth_limit &&
                   t.min_track_sizing_function.definite_value(basis, calc).has_value();
        }))
        return;
    float adjustment = compute_alignment_gutter_adjustment(
        other_alignment, inner.get(other(axis)),
        [&](const auto& t, auto b) {
            return estimate(t, b, tree);
        },
        others);
    if (others.size() > 3)
        for (std::size_t i = 2; i < others.size(); i += 2)
            others[i].content_alignment_adjustment = adjustment;
    resolve_intrinsic_track_sizes(tree, axis, tracks, others, items, available.get(axis), inner,
                                  estimate);
    maximise_tracks(tracks, available.get(axis));
    auto expansion = inner.get(axis)
                         ? AvailableSpace::definite(*inner.get(axis))
                         : (available.get(axis).is_min_content() && axis == AbstractAxis::Inline
                                ? AvailableSpace::min_content()
                                : AvailableSpace::max_content());
    expand_flexible_tracks(tree, axis, tracks, others, items, minimum, maximum, expansion, inner,
                           estimate);
    if (alignment == AlignContent::stretch())
        stretch_auto_tracks(tracks, minimum, expansion);
}
} // namespace taffy::grid
