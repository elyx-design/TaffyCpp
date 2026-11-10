#include "taffy/style.hpp"
#include "grid/names.hpp"
#include "grid/placement_types.hpp"

#include <map>
#include <set>
#include <unordered_set>

namespace taffy {

bool AvailableSpace::is_roughly_equal(AvailableSpace other) const {
    return kind_ == other.kind_ && (!is_definite() || std::abs(value_ - other.value_) <
                                                          std::numeric_limits<float>::epsilon());
}

Size<std::optional<float>> into_options(Size<AvailableSpace> value) {
    return {value.width.into_option(), value.height.into_option()};
}

Size<AvailableSpace> maybe_set(Size<AvailableSpace> value, Size<std::optional<float>> replacement) {
    return {value.width.maybe_set(replacement.width), value.height.maybe_set(replacement.height)};
}

std::uint16_t GridTemplateRepetition::track_count() const {
    return static_cast<std::uint16_t>(
        std::min<std::size_t>(tracks.size(), std::numeric_limits<std::uint16_t>::max()));
}

GridTemplateComponent::GridTemplateComponent(TrackSizingFunction track) : value(track) {}

GridTemplateComponent::GridTemplateComponent(GridTemplateRepetition repetition)
    : value(std::move(repetition)) {}

GridTemplateComponent GridTemplateComponent::single(TrackSizingFunction track) {
    return track;
}

GridTemplateComponent GridTemplateComponent::repeat(GridTemplateRepetition repetition) {
    return repetition;
}

GridTemplateComponent GridTemplateComponent::auto_() {
    return TrackSizingFunction::auto_();
}

GridTemplateComponent GridTemplateComponent::zero() {
    return TrackSizingFunction::zero();
}

GridTemplateComponent GridTemplateComponent::length(float value) {
    return TrackSizingFunction::length(value);
}

GridTemplateComponent GridTemplateComponent::percent(float value) {
    return TrackSizingFunction::percent(value);
}

GridTemplateComponent GridTemplateComponent::fr(float value) {
    return TrackSizingFunction::fr(value);
}

GridTemplateComponent GridTemplateComponent::min_content() {
    return TrackSizingFunction::min_content();
}

GridTemplateComponent GridTemplateComponent::max_content() {
    return TrackSizingFunction::max_content();
}

GridTemplateComponent GridTemplateComponent::fit_content(LengthPercentage value) {
    return TrackSizingFunction::fit_content(value);
}

bool GridTemplateComponent::is_auto_repetition() const {
    auto repetition = std::get_if<GridTemplateRepetition>(&value);
    return repetition && repetition->count.is_auto();
}

GridPlacement GridPlacement::auto_() {
    return {};
}

GridPlacement GridPlacement::line(std::int16_t index) {
    GridPlacement result;
    result.kind_ = Kind::Line;
    result.line_ = index;
    return result;
}

GridPlacement GridPlacement::line(GridLine index) {
    return line(index.value);
}

GridPlacement GridPlacement::span(std::uint16_t count) {
    GridPlacement result;
    result.kind_ = Kind::Span;
    result.span_ = count;
    return result;
}

GridPlacement GridPlacement::named_line(std::string name, std::int16_t occurrence) {
    auto result = line(occurrence);
    result.kind_ = Kind::NamedLine;
    result.name_ = std::move(name);
    return result;
}

GridPlacement GridPlacement::named_span(std::string name, std::uint16_t count) {
    auto result = span(count);
    result.kind_ = Kind::NamedSpan;
    result.name_ = std::move(name);
    return result;
}

std::int16_t GridPlacement::line_index() const {
    if (kind_ != Kind::Line && kind_ != Kind::NamedLine)
        throw std::logic_error("placement has no line index");
    return line_;
}

std::uint16_t GridPlacement::span_count() const {
    if (kind_ != Kind::Span && kind_ != Kind::NamedSpan)
        throw std::logic_error("placement has no span");
    return span_;
}

const std::string& GridPlacement::name() const {
    if (!is_named())
        throw std::logic_error("placement has no name");
    return name_;
}

AlignContent Style::grid_align_content(AbstractAxis axis) const {
    return grid_align_content(as_abs_naive(axis));
}

AlignContent Style::grid_align_content(AbsoluteAxis axis) const {
    auto alignment = axis == AbsoluteAxis::Horizontal ? justify_content : align_content;
    return alignment.keyword == AlignContentKeyword::Normal
               ? AlignContent{AlignContentKeyword::Stretch}
               : alignment;
}

void GridTemplateRepetition::validate() const {
    if (!line_names.empty() && line_names.size() != tracks.size() + 1)
        throw std::invalid_argument(
            "grid repetition line name sets must be empty or have tracks.size() + 1 entries");
}

GridTemplateComponent repeat(RepetitionCount count, std::vector<TrackSizingFunction> tracks,
                             TemplateLineNames line_names) {
    GridTemplateRepetition repetition{count, std::move(tracks), std::move(line_names)};
    repetition.validate();
    return repetition;
}

std::vector<GridTemplateComponent> evenly_sized_tracks(std::uint16_t count) {
    return {repeat(RepetitionCount::count(count), {TrackSizingFunction::flex(1)})};
}

GridTemplateTracks<TrackSizingFunction>
expand_grid_template(std::span<const GridTemplateComponent> components,
                     const TemplateLineNames& line_names, std::uint16_t auto_repetitions,
                     std::uint16_t track_limit) {
    if (!line_names.empty() && line_names.size() != components.size() + 1)
        throw std::invalid_argument(
            "grid template line_names must be empty or have components.size() + 1 entries");
    track_limit = std::min(track_limit, max_grid_tracks);
    GridTemplateTracks<TrackSizingFunction> result;
    result.line_names.emplace_back();
    std::unordered_set<std::string> boundary_names;
    const auto merge = [&](const std::vector<std::string>& names) {
        auto& target = result.line_names.back();
        for (const auto& name : names) {
            // Ordinary boundaries have only a few aliases; keep those allocation-free.
            if (target.size() < 8) {
                if (std::find(target.begin(), target.end(), name) == target.end())
                    target.push_back(name);
                continue;
            }
            if (boundary_names.empty())
                boundary_names.insert(target.begin(), target.end());
            if (boundary_names.insert(name).second)
                target.push_back(name);
        }
    };
    const auto append = [&](const TrackSizingFunction& track) {
        result.tracks.push_back(track);
        result.line_names.emplace_back();
        boundary_names.clear();
    };
    if (!line_names.empty())
        merge(line_names.front());
    for (std::size_t i = 0; i < components.size(); ++i) {
        const auto& component = components[i];
        bool component_complete = true;
        if (const auto* track = std::get_if<TrackSizingFunction>(&component.value)) {
            if (result.tracks.size() >= track_limit)
                break;
            append(*track);
        } else {
            const auto& repetition = std::get<GridTemplateRepetition>(component.value);
            repetition.validate();
            const auto count =
                repetition.count.is_auto() ? auto_repetitions : repetition.count.value();
            const auto remaining = track_limit - result.tracks.size();
            component_complete = count == 0 || repetition.tracks.size() <= remaining / count;
            if (count != 0 && !repetition.line_names.empty())
                merge(repetition.line_names.front());
            // Empty repeats have one shared boundary and cannot advance the track limit.
            const auto iterations =
                repetition.tracks.empty() ? std::min<std::uint32_t>(count, 1) : count;
            for (std::uint32_t n = 0; n < iterations && result.tracks.size() < track_limit; ++n) {
                std::size_t j = 0;
                for (; j < repetition.tracks.size() && result.tracks.size() < track_limit; ++j) {
                    append(repetition.tracks[j]);
                    if (!repetition.line_names.empty())
                        merge(repetition.line_names[j + 1]);
                }
                if (j != repetition.tracks.size())
                    break;
                // Shared boundary names also exist at the clipping line, even when the
                // next repetition's tracks lie beyond the grid limit.
                if (n + 1 < count && !repetition.line_names.empty())
                    merge(repetition.line_names.front());
            }
        }
        if (!component_complete)
            break;
        if (!line_names.empty())
            merge(line_names[i + 1]);
    }
    return result;
}

void GridTemplateAreas::validate() const {
    std::set<std::string> names;
    for (std::size_t i = 0; i < areas.size(); ++i) {
        const auto& area = areas[i];
        if (area.name.empty() || !names.insert(area.name).second)
            throw std::invalid_argument("grid areas must have unique nonempty names");
        if (area.row_start == 0 || area.column_start == 0 || area.row_end <= area.row_start ||
            area.column_end <= area.column_start ||
            static_cast<unsigned>(area.row_end) > static_cast<unsigned>(row_count) + 1 ||
            static_cast<unsigned>(area.column_end) > static_cast<unsigned>(column_count) + 1)
            throw std::invalid_argument(
                "grid area rectangle lies outside its template or is empty");
    }

    // Sweep rows, removing ends before starts at the same boundary. Until an
    // overlap is found, active column intervals are disjoint; only neighbours
    // in column order can intersect an inserted rectangle.
    struct Event {
        std::uint16_t row;
        bool start;
        std::size_t index;
    };

    std::vector<Event> events;
    events.reserve(areas.size() * 2);
    for (std::size_t i = 0; i < areas.size(); ++i) {
        events.push_back({areas[i].row_start, true, i});
        events.push_back({areas[i].row_end, false, i});
    }
    std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        return a.row != b.row ? a.row < b.row : a.start < b.start;
    });
    std::map<std::uint16_t, std::uint16_t> active;
    for (const auto& event : events) {
        const auto& area = areas[event.index];
        if (!event.start) {
            active.erase(area.column_start);
            continue;
        }
        const auto next = active.lower_bound(area.column_start);
        if ((next != active.end() && next->first < area.column_end) ||
            (next != active.begin() && std::prev(next)->second > area.column_start))
            throw std::invalid_argument("grid area rectangles overlap");
        active.emplace_hint(next, area.column_start, area.column_end);
    }
}

GridPlacement line(std::int16_t index) {
    return GridPlacement::line(index);
}

GridPlacement span(std::uint16_t count) {
    return GridPlacement::span(count);
}

bool is_definite(const Line<GridPlacement>& placement) {
    return placement.start.is_definite() || placement.end.is_definite();
}

bool is_definite(Line<OriginZeroGridPlacement> placement) {
    return placement.start.is_line() || placement.end.is_line();
}

OriginZeroGridPlacement into_origin_zero_placement_ignoring_named(const GridPlacement& placement,
                                                                  std::uint16_t explicit_tracks) {
    using K = GridPlacement::Kind;
    switch (placement.kind()) {
    case K::Line:
        if (placement.line_index() == 0)
            return {};
        return OriginZeroGridPlacement::line(
            GridLine(placement.line_index()).into_origin_zero_line(explicit_tracks));
    case K::Span:
        return OriginZeroGridPlacement::span(
            std::clamp<std::uint16_t>(placement.span_count(), 1, max_grid_tracks));
    default:
        return {};
    }
}

Line<OriginZeroGridPlacement> into_origin_zero_ignoring_named(const Line<GridPlacement>& placement,
                                                              std::uint16_t explicit_tracks) {
    return {into_origin_zero_placement_ignoring_named(placement.start, explicit_tracks),
            into_origin_zero_placement_ignoring_named(placement.end, explicit_tracks)};
}

std::uint16_t indefinite_span(Line<OriginZeroGridPlacement> placement) {
    using K = OriginZeroGridPlacement::Kind;
    if (placement.start.is_line() && placement.end.is_line())
        throw std::logic_error("indefinite_span requires an indefinite placement");
    if (placement.start.kind == K::Span)
        return static_cast<std::uint16_t>(placement.start.value);
    if (placement.end.kind == K::Span)
        return static_cast<std::uint16_t>(placement.end.value);
    return 1;
}

namespace {
std::int32_t checked_grid_line(std::int64_t value) {
    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max())
        throw std::overflow_error("grid coordinate arithmetic exceeds int32_t");
    return static_cast<std::int32_t>(value);
}
} // namespace

Line<std::int32_t> resolve_definite_grid_lines(Line<OriginZeroGridPlacement> placement) {
    using K = OriginZeroGridPlacement::Kind;
    const auto a = placement.start, b = placement.end;
    if (a.is_line() && b.is_line()) {
        if (a.value == b.value)
            return {a.value, checked_grid_line(static_cast<std::int64_t>(a.value) + 1)};
        return {std::min(a.value, b.value), std::max(a.value, b.value)};
    }
    if (a.is_line())
        return {a.value, checked_grid_line(static_cast<std::int64_t>(a.value) +
                                           (b.kind == K::Span ? b.value : 1))};
    if (b.is_line())
        return {checked_grid_line(static_cast<std::int64_t>(b.value) -
                                  (a.kind == K::Span ? a.value : 1)),
                b.value};
    throw std::logic_error("resolve_definite_grid_lines requires a definite placement");
}

Line<std::optional<std::int32_t>>
resolve_absolutely_positioned_grid_tracks(Line<OriginZeroGridPlacement> placement) {
    using K = OriginZeroGridPlacement::Kind;
    const auto a = placement.start, b = placement.end;
    if ((a.is_line() && b.is_line()) || (a.is_line() && b.kind == K::Span) ||
        (a.kind == K::Span && b.is_line())) {
        auto resolved = resolve_definite_grid_lines(placement);
        return {resolved.start, resolved.end};
    }
    return {a.is_line() ? std::optional(a.value) : std::nullopt,
            b.is_line() ? std::optional(b.value) : std::nullopt};
}

Line<std::int32_t> resolve_indefinite_grid_tracks(Line<OriginZeroGridPlacement> placement,
                                                  std::int32_t start) {
    if (is_definite(placement))
        throw std::logic_error("resolve_indefinite_grid_tracks requires an indefinite placement");
    return {start,
            checked_grid_line(static_cast<std::int64_t>(start) + indefinite_span(placement))};
}

GridPlacement resolve_named_grid_placement(const GridPlacement& placement,
                                           const TemplateLineNames& names,
                                           std::span<const GridTemplateArea> areas,
                                           GridAreaAxis axis, GridAreaEnd end,
                                           std::uint16_t explicit_tracks,
                                           std::optional<std::int32_t> opposite_line) {
    if (!placement.is_named())
        return placement;
    grid::NameIndex index(names);
    index.add_areas(areas, axis);
    index.finish();
    return index.resolve(placement, end, explicit_tracks, opposite_line);
}

Line<GridPlacement> resolve_grid_placement_names(const Line<GridPlacement>& placement,
                                                 const TemplateLineNames& names,
                                                 std::span<const GridTemplateArea> areas,
                                                 GridAreaAxis axis, std::uint16_t explicit_tracks) {
    if (!grid::has_names(placement))
        return placement;
    grid::NameIndex index(names);
    index.add_areas(areas, axis);
    index.finish();
    return index.resolve(placement, explicit_tracks);
}

Line<OriginZeroGridPlacement> into_origin_zero(const Line<GridPlacement>& placement,
                                               const TemplateLineNames& names,
                                               std::span<const GridTemplateArea> areas,
                                               GridAreaAxis axis, std::uint16_t explicit_tracks) {
    return into_origin_zero_ignoring_named(
        resolve_grid_placement_names(placement, names, areas, axis, explicit_tracks),
        explicit_tracks);
}

void Style::validate() const {
    if (flex_line_count == 0)
        throw std::invalid_argument("flex_line_count must be at least one");
    const auto validate_template = [](std::span<const GridTemplateComponent> tracks,
                                      const TemplateLineNames& names) {
        if (!names.empty() && names.size() != tracks.size() + 1)
            throw std::invalid_argument(
                "grid template line_names must be empty or have components.size() + 1 entries");
        for (const auto& component : tracks)
            if (const auto* repetition = std::get_if<GridTemplateRepetition>(&component.value))
                repetition->validate();
    };
    validate_template(grid_template_rows, grid_template_row_names);
    validate_template(grid_template_columns, grid_template_column_names);
    if (grid_template_areas)
        grid_template_areas->validate();
}

} // namespace taffy
