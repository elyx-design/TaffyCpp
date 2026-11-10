// Pinned upstream compute/grid/explicit_grid.rs. Counts are clamped before
// allocation; alternating implicit patterns are anchored at the explicit grid.
#include "placement.hpp"
#include "types.hpp"
#include <cmath>

namespace taffy::grid {
namespace {
std::uint32_t add_saturating(std::uint32_t a, std::uint32_t b) {
    return static_cast<std::uint32_t>(std::min(std::uint64_t(a) + b, std::uint64_t(UINT32_MAX)));
}

float track_definite_value(const TrackSizingFunction& track, std::optional<float> basis,
                           const CalcResolver& calc) {
    const auto maximum = track.max.definite_value(basis, calc);
    const auto minimum = track.min.definite_value(basis, calc);
    if (maximum)
        return maybe_max(*maximum, minimum);
    return minimum.value();
}
} // namespace

ExplicitGridAxis compute_explicit_grid_size_in_axis(const Style& style, std::optional<float> size,
                                                    AutoRepeatStrategy strategy,
                                                    const CalcResolver& calc, AbsoluteAxis axis) {
    const auto& components =
        axis == AbsoluteAxis::Horizontal ? style.grid_template_columns : style.grid_template_rows;
    if (components.empty())
        return {};
    std::uint32_t non_auto_count = 0, auto_count = 0;
    bool all_fixed = true;
    for (const auto& component : components) {
        if (const auto* track = std::get_if<TrackSizingFunction>(&component.value)) {
            non_auto_count = add_saturating(non_auto_count, 1);
            all_fixed = all_fixed && track->has_fixed_component();
        } else {
            const auto& repeat = std::get<GridTemplateRepetition>(component.value);
            if (repeat.tracks.empty())
                return {};
            if (repeat.count.is_auto())
                ++auto_count;
            else
                non_auto_count = add_saturating(
                    non_auto_count, std::uint32_t(repeat.count.value()) * repeat.track_count());
            for (const auto& track : repeat.tracks)
                all_fixed = all_fixed && track.has_fixed_component();
        }
    }
    non_auto_count = std::min<std::uint32_t>(non_auto_count, max_grid_tracks);
    if (auto_count != 0 && (auto_count != 1 || !all_fixed))
        return {};
    if (auto_count == 0)
        return {0, static_cast<std::uint16_t>(non_auto_count)};
    std::uint32_t insertion_point = 0;
    const GridTemplateRepetition* automatic = nullptr;
    for (const auto& component : components) {
        if (std::holds_alternative<TrackSizingFunction>(component.value))
            insertion_point = add_saturating(insertion_point, 1);
        else {
            const auto& repeat = std::get<GridTemplateRepetition>(component.value);
            if (repeat.count.is_auto()) {
                automatic = &repeat;
                break;
            }
            insertion_point = add_saturating(insertion_point, std::uint32_t(repeat.count.value()) *
                                                                  repeat.track_count());
        }
    }
    const auto repeated_tracks = automatic->track_count();
    std::uint32_t repetitions = 1;
    if (size) {
        float non_repeating_space = 0;
        for (const auto& component : components) {
            if (const auto* track = std::get_if<TrackSizingFunction>(&component.value))
                non_repeating_space += track_definite_value(*track, size, calc);
            else {
                const auto& repeat = std::get<GridTemplateRepetition>(component.value);
                if (repeat.count.is_auto())
                    continue;
                float sum = 0;
                for (const auto& track : repeat.tracks)
                    sum += track_definite_value(track, size, calc);
                non_repeating_space += sum * float(repeat.count.value());
            }
        }
        const auto gap = (axis == AbsoluteAxis::Horizontal ? style.gap.width : style.gap.height)
                             .resolve_or_zero(size, calc);
        float per_repeat_space = 0;
        for (const auto& track : automatic->tracks)
            per_repeat_space += track_definite_value(track, size, calc);
        const auto first_used_space = non_repeating_space + per_repeat_space +
                                      float(non_auto_count + repeated_tracks - 1) * gap;
        if (!(first_used_space > *size)) {
            const auto fitting =
                (*size - first_used_space) / (per_repeat_space + float(repeated_tracks) * gap);
            const auto rounded = strategy == AutoRepeatStrategy::MaxRepetitionsThatDoNotOverflow
                                     ? std::floor(fitting)
                                     : std::ceil(fitting);
            repetitions = add_saturating(saturating_cast<std::uint32_t>(rounded), 1);
        }
    }
    const auto remaining =
        insertion_point >= max_grid_tracks ? 0 : max_grid_tracks - insertion_point;
    repetitions = remaining == 0
                      ? 0
                      : std::clamp<std::uint32_t>(
                            repetitions, 1, (remaining + repeated_tracks - 1) / repeated_tracks);
    return {static_cast<std::uint16_t>(repetitions),
            static_cast<std::uint16_t>(std::min<std::uint32_t>(
                non_auto_count + repeated_tracks * repetitions, max_grid_tracks))};
}

ExplicitGridSize compute_explicit_grid_size(const Style& style,
                                            Size<std::optional<float>> preferred,
                                            Size<std::optional<float>> minimum,
                                            Size<std::optional<float>> maximum,
                                            const CalcResolver& calc) {
    const auto compute = [&](AbsoluteAxis axis, std::optional<float> p, std::optional<float> min,
                             std::optional<float> max) {
        const auto size = maybe_clamp(p ? p : max ? max : min, min, max);
        return compute_explicit_grid_size_in_axis(
            style, size,
            p || max ? AutoRepeatStrategy::MaxRepetitionsThatDoNotOverflow
                     : AutoRepeatStrategy::MinRepetitionsThatDoOverflow,
            calc, axis);
    };
    const auto columns =
        compute(AbsoluteAxis::Horizontal, preferred.width, minimum.width, maximum.width);
    const auto rows =
        compute(AbsoluteAxis::Vertical, preferred.height, minimum.height, maximum.height);
    return {{columns.auto_repetitions, rows.auto_repetitions},
            {columns.explicit_tracks, rows.explicit_tracks}};
}

void initialize_grid_tracks(std::vector<GridTrack>& tracks, TrackCounts counts,
                            const std::vector<GridTemplateComponent>& components,
                            const std::vector<TrackSizingFunction>& automatic, LengthPercentage gap,
                            std::uint16_t auto_repetitions,
                            const std::function<bool(std::int32_t)>& has_items) {
    tracks.clear();
    tracks.reserve(counts.len() * 2 + 1);
    tracks.push_back(GridTrack::gutter(gap));
    const auto append = [&](TrackSizingFunction sizing) {
        tracks.emplace_back(sizing.min, sizing.max);
        tracks.push_back(GridTrack::gutter(gap));
    };
    const auto implicit = [&](std::size_t count, std::size_t offset) {
        for (std::size_t i = 0; i < count; ++i)
            append(automatic.empty() ? TrackSizingFunction::auto_()
                                     : automatic[(i + offset) % automatic.size()]);
    };
    const auto negative_offset =
        automatic.empty() ? 0 : automatic.size() - counts.negative_implicit % automatic.size();
    implicit(counts.negative_implicit, negative_offset);
    std::size_t current = counts.negative_implicit;
    const auto limit = current + counts.explicit_;
    if (counts.explicit_ > 0) {
        for (const auto& component : components) {
            if (const auto* track = std::get_if<TrackSizingFunction>(&component.value)) {
                if (current < limit) {
                    append(*track);
                    ++current;
                }
                continue;
            }
            const auto& repeat = std::get<GridTemplateRepetition>(component.value);
            const auto count = repeat.count.is_auto() ? auto_repetitions : repeat.count.value();
            const auto repeated_count =
                std::min<std::size_t>(repeat.tracks.size() * count, limit - current);
            for (std::size_t i = 0; i < repeated_count; ++i) {
                const auto& track = repeat.tracks[i % repeat.tracks.size()];
                append(track);
                if (repeat.count.kind() == RepetitionCount::Kind::AutoFit &&
                    !has_items(static_cast<std::int32_t>(current))) {
                    tracks[tracks.size() - 2].collapse();
                    tracks.back().collapse();
                }
                ++current;
            }
            if (repeat.count.kind() == RepetitionCount::Kind::AutoFit && current == counts.len()) {
                for (auto it = tracks.rbegin(); it != tracks.rend(); ++it) {
                    if (it->kind == GridTrackKind::Track && !it->is_collapsed)
                        break;
                    it->collapse();
                }
            }
        }
    }
    implicit(counts.positive_implicit + limit - current, 0);
    tracks.front().collapse();
    tracks.back().collapse();
}

} // namespace taffy::grid
