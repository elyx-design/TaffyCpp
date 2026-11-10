// Pinned upstream compute/grid/placement.rs and types/cell_occupancy.rs.
// Sparse interval rows/columns preserve occupancy while avoiding area-sized storage.
#include "placement.hpp"

namespace taffy::grid {

std::optional<std::size_t> TrackCounts::try_track_vec_index(std::int32_t line) const {
    if (line < implicit_start_line() || line > implicit_end_line())
        return std::nullopt;
    return 2 * std::size_t(line + negative_implicit);
}

std::size_t TrackCounts::track_vec_index(std::int32_t line) const {
    const auto index = try_track_vec_index(line);
    if (!index)
        throw std::out_of_range("grid line outside implicit grid");
    return *index;
}

void CellOccupancy::TrackIntervals::paint(Line<std::int32_t> range, CellOccupancyState state) {
    if (range.start >= range.end)
        return;
    auto* compact = std::get_if<std::vector<Interval>>(&intervals);
    // Keep contiguous storage (and its append fast path) for common grids.
    // A bounded number of intervals can be rebuilt cheaply; fragmented tracks
    // need logarithmic insertion regardless of the direction of placement.
    constexpr std::size_t indexed_threshold = 8;
    if (compact && compact->size() >= indexed_threshold &&
        compact->back().range.end > range.start) {
        IndexedIntervals indexed;
        for (const auto& interval : *compact)
            indexed.emplace_hint(indexed.end(), interval.range.start, interval);
        intervals = std::move(indexed);
        compact = nullptr;
    }
    if (!compact) {
        auto& indexed = std::get<IndexedIntervals>(intervals);
        // Split only the two boundary intervals, then replace the covered runs.
        const auto split = [&](std::int32_t position) {
            auto after = indexed.upper_bound(position);
            if (after == indexed.begin())
                return;
            auto before = std::prev(after);
            if (before->first < position && position < before->second.range.end) {
                auto tail = before->second;
                tail.range.start = position;
                indexed.emplace_hint(after, position, tail);
                before->second.range.end = position;
            }
        };
        split(range.start);
        split(range.end);
        indexed.erase(indexed.lower_bound(range.start), indexed.lower_bound(range.end));
        auto painted = indexed.emplace(range.start, Interval{range, state}).first;
        if (painted != indexed.begin()) {
            const auto before = std::prev(painted);
            if (before->second.state == state && before->second.range.end == range.start) {
                before->second.range.end = painted->second.range.end;
                indexed.erase(painted);
                painted = before;
            }
        }
        const auto after = std::next(painted);
        if (after != indexed.end() && after->second.state == state &&
            painted->second.range.end == after->first) {
            painted->second.range.end = after->second.range.end;
            indexed.erase(after);
        }
        return;
    }
    auto& runs = *compact;
    if (runs.empty()) {
        runs.push_back({range, state});
        return;
    }
    if (runs.back().range.end <= range.start) {
        auto& last = runs.back();
        if (last.state == state && last.range.end == range.start)
            last.range.end = range.end;
        else
            runs.push_back({range, state});
        return;
    }
    std::vector<Interval> result;
    result.reserve(runs.size() + 2);
    const auto append = [&](Interval interval) {
        if (!result.empty() && result.back().state == interval.state &&
            result.back().range.end == interval.range.start)
            result.back().range.end = interval.range.end;
        else
            result.push_back(interval);
    };
    for (const auto& interval : runs) {
        if (interval.range.end <= range.start)
            append(interval);
        else if (interval.range.start < range.start)
            append({{interval.range.start, range.start}, interval.state});
    }
    append({range, state});
    for (const auto& interval : runs) {
        if (interval.range.start >= range.end)
            append(interval);
        else if (interval.range.end > range.end)
            append({{range.end, interval.range.end}, interval.state});
    }
    runs = std::move(result);
}

std::optional<std::int32_t>
CellOccupancy::TrackIntervals::collision_extent(Line<std::int32_t> range) const {
    if (const auto* indexed = std::get_if<IndexedIntervals>(&intervals)) {
        auto after = indexed->lower_bound(range.end);
        if (after != indexed->begin()) {
            const auto& interval = std::prev(after)->second;
            if (interval.range.end > range.start)
                return interval.range.end - 1;
        }
    } else {
        const auto& runs = std::get<std::vector<Interval>>(intervals);
        // Auto-placement usually queries at or beyond the append frontier.
        if (runs.empty() || runs.back().range.end <= range.start)
            return std::nullopt;
        if (runs.back().range.start < range.end)
            return runs.back().range.end - 1;
        // Endpoints are sorted as well as starts because the runs are disjoint.
        const auto after = std::lower_bound(runs.begin(), runs.end(), range.end,
                                            [](const Interval& interval, std::int32_t end) {
                                                return interval.range.start < end;
                                            });
        if (after != runs.begin() && std::prev(after)->range.end > range.start)
            return std::prev(after)->range.end - 1;
    }
    return std::nullopt;
}

bool CellOccupancy::TrackIntervals::empty() const {
    return std::visit(
        [](const auto& runs) {
            return runs.empty();
        },
        intervals);
}

CellOccupancyState CellOccupancy::TrackIntervals::state_at(std::int32_t coordinate) const {
    if (const auto* indexed = std::get_if<IndexedIntervals>(&intervals)) {
        const auto after = indexed->upper_bound(coordinate);
        if (after != indexed->begin()) {
            const auto& interval = std::prev(after)->second;
            if (coordinate < interval.range.end)
                return interval.state;
        }
    } else {
        for (const auto& interval : std::get<std::vector<Interval>>(intervals))
            if (interval.range.start <= coordinate && coordinate < interval.range.end)
                return interval.state;
    }
    return CellOccupancyState::Unoccupied;
}

CellOccupancy::CellOccupancy(TrackCounts columns, TrackCounts rows)
    : columns_(columns), rows_(rows), row_intervals_(rows.len()), column_intervals_(columns.len()) {
}

const TrackCounts& CellOccupancy::track_counts(AbsoluteAxis axis) const {
    return axis == AbsoluteAxis::Horizontal ? columns_ : rows_;
}

const std::vector<CellOccupancy::TrackIntervals>&
CellOccupancy::track_lists(AbsoluteAxis axis) const {
    return axis == AbsoluteAxis::Horizontal ? column_intervals_ : row_intervals_;
}

void CellOccupancy::expand_to_fit_range(Line<std::int32_t> row, Line<std::int32_t> column) {
    const auto expand = [](TrackCounts& counts, std::vector<TrackIntervals>& tracks,
                           Line<std::int32_t> span) {
        const auto negative = std::max(counts.implicit_start_line() - span.start, 0);
        const auto positive = std::max(span.end - counts.implicit_end_line(), 0);
        if (std::int64_t(counts.negative_implicit) + negative > max_grid_tracks ||
            std::int64_t(counts.explicit_) + counts.positive_implicit + positive > max_grid_tracks)
            throw std::out_of_range("occupancy span outside limited grid");
        tracks.insert(tracks.begin(), std::size_t(negative), TrackIntervals{});
        tracks.resize(tracks.size() + std::size_t(positive));
        counts.negative_implicit += static_cast<std::uint16_t>(negative);
        counts.positive_implicit += static_cast<std::uint16_t>(positive);
    };
    expand(rows_, row_intervals_, row);
    expand(columns_, column_intervals_, column);
}

void CellOccupancy::mark_area_as(AbsoluteAxis primary, Line<std::int32_t> primary_span,
                                 Line<std::int32_t> secondary_span, CellOccupancyState value) {
    const auto row = primary == AbsoluteAxis::Horizontal ? secondary_span : primary_span;
    const auto column = primary == AbsoluteAxis::Horizontal ? primary_span : secondary_span;
    expand_to_fit_range(row, column);
    if (value == CellOccupancyState::AutoPlaced && primary_span.start < primary_span.end &&
        secondary_span.start < secondary_span.end) {
        auto& lists = primary == AbsoluteAxis::Horizontal ? row_intervals_ : column_intervals_;
        const auto index =
            track_counts(other_axis(primary)).oz_line_to_next_track(secondary_span.start);
        auto& cursor = lists.at(std::size_t(index)).cursor;
        cursor = cursor ? std::max(*cursor, primary_span.end) : primary_span.end;
    }
    const auto rows = rows_.oz_line_range_to_track_range(row);
    const auto columns = columns_.oz_line_range_to_track_range(column);
    for (auto i = rows.start; i < rows.end; ++i)
        row_intervals_.at(std::size_t(i)).paint(column, value);
    for (auto i = columns.start; i < columns.end; ++i)
        column_intervals_.at(std::size_t(i)).paint(row, value);
}

std::optional<std::int32_t>
CellOccupancy::line_area_collision_jump(AbsoluteAxis primary, Line<std::int32_t> primary_span,
                                        Line<std::int32_t> secondary_span) const {
    const auto& lists = track_lists(other_axis(primary));
    const auto range =
        track_counts(other_axis(primary)).oz_line_range_to_track_range(secondary_span);
    std::optional<std::int32_t> extent;
    for (auto i = std::max(range.start, 0);
         i < std::min(range.end, static_cast<std::int32_t>(lists.size())); ++i) {
        const auto cell = lists[std::size_t(i)].collision_extent(primary_span);
        if (cell)
            extent = extent ? std::max(*extent, *cell) : *cell;
    }
    return extent ? std::optional(*extent + 1) : std::nullopt;
}

std::optional<std::int32_t> CellOccupancy::occupied_track_jump(AbsoluteAxis axis,
                                                               Line<std::int32_t> span) const {
    const auto& lists = track_lists(axis);
    const auto& counts = track_counts(axis);
    const auto range = counts.oz_line_range_to_track_range(span);
    for (auto i = std::min(range.end, static_cast<std::int32_t>(lists.size()));
         i > std::max(range.start, 0);) {
        --i;
        if (!lists[std::size_t(i)].empty())
            return counts.track_to_prev_oz_line(i) + 1;
    }
    return std::nullopt;
}

std::optional<std::int32_t>
CellOccupancy::auto_placement_cursor(AbsoluteAxis primary, std::int32_t secondary_start) const {
    const auto& lists = track_lists(other_axis(primary));
    const auto index = track_counts(other_axis(primary)).oz_line_to_next_track(secondary_start);
    if (index < 0 || std::size_t(index) >= lists.size())
        return std::nullopt;
    return lists[std::size_t(index)].cursor;
}

bool CellOccupancy::row_is_occupied(std::size_t index) const {
    return index < row_intervals_.size() && !row_intervals_[index].empty();
}

bool CellOccupancy::column_is_occupied(std::size_t index) const {
    return index < column_intervals_.size() && !column_intervals_[index].empty();
}

CellOccupancyState CellOccupancy::state_at(std::int32_t row, std::int32_t column) const {
    const auto index = rows_.oz_line_to_next_track(row);
    if (index >= 0 && std::size_t(index) < row_intervals_.size())
        return row_intervals_[std::size_t(index)].state_at(column);
    return CellOccupancyState::Unoccupied;
}

std::int32_t advance_position(std::int32_t position) {
    return std::min(position + 1, std::int32_t(INT16_MAX));
}

Line<std::int32_t> resolve_indefinite_grid_span(std::int32_t position, std::uint16_t span) {
    return {std::clamp(position, std::int32_t(INT16_MIN), std::int32_t(INT16_MAX)),
            std::clamp(position + span, std::int32_t(INT16_MIN), std::int32_t(INT16_MAX))};
}

namespace {
using Span = Line<std::int32_t>;
using AxisPlacement = Line<OriginZeroGridPlacement>;

AxisPlacement get(ItemPlacement placement, AbsoluteAxis axis) {
    return axis == AbsoluteAxis::Horizontal ? placement.horizontal : placement.vertical;
}

Span clamp_span(Span span) {
    const auto start =
        std::clamp(span.start, -std::int32_t(max_grid_tracks), std::int32_t(max_grid_tracks) - 1);
    return {start, std::clamp(span.end, start + 1, std::int32_t(max_grid_tracks))};
}

void record(CellOccupancy& occupancy, PlacedGridItem& item, AbsoluteAxis axis, Span primary,
            Span secondary, CellOccupancyState state) {
    primary = clamp_span(primary);
    secondary = clamp_span(secondary);
    occupancy.mark_area_as(axis, primary, secondary, state);
    item.column = axis == AbsoluteAxis::Horizontal ? primary : secondary;
    item.row = axis == AbsoluteAxis::Horizontal ? secondary : primary;
}

std::pair<Span, Span> place_secondary(const CellOccupancy& occupancy, ItemPlacement placement,
                                      GridAutoFlow flow) {
    const auto primary = primary_axis(flow);
    const auto secondary = resolve_definite_grid_lines(get(placement, other_axis(primary)));
    const auto start = occupancy.track_counts(primary).implicit_start_line();
    auto position = is_dense(flow)
                        ? start
                        : occupancy.auto_placement_cursor(primary, secondary.start).value_or(start);
    const auto span = indefinite_span(get(placement, primary));
    for (;;) {
        const auto candidate = resolve_indefinite_grid_span(position, span);
        const auto collision = occupancy.line_area_collision_jump(primary, candidate, secondary);
        if (!collision)
            return {candidate, secondary};
        position = *collision;
    }
}

std::pair<Span, Span> place_indefinite(const CellOccupancy& occupancy, ItemPlacement placement,
                                       GridAutoFlow flow,
                                       std::pair<std::int32_t, std::int32_t> cursor) {
    const auto axis = primary_axis(flow), secondary_axis = other_axis(axis);
    const auto primary_style = get(placement, axis),
               secondary_style = get(placement, secondary_axis);
    const auto secondary_length = indefinite_span(secondary_style);
    const auto start = occupancy.track_counts(axis).implicit_start_line();
    const auto end = occupancy.track_counts(axis).implicit_end_line();
    auto [p, s] = cursor;
    if (is_definite(primary_style)) {
        const auto primary = resolve_definite_grid_lines(primary_style);
        if (is_dense(flow))
            s = occupancy.track_counts(secondary_axis).implicit_start_line();
        else if (primary.start < p)
            s = advance_position(s);
        for (;;) {
            const auto secondary = resolve_indefinite_grid_span(s, secondary_length);
            const auto collision =
                occupancy.line_area_collision_jump(secondary_axis, secondary, primary);
            if (!collision)
                return {primary, secondary};
            s = *collision;
        }
    }
    const auto primary_length = indefinite_span(primary_style);
    const bool spans_all = primary_length >= occupancy.track_counts(axis).len();
    for (;;) {
        const auto primary = resolve_indefinite_grid_span(p, primary_length),
                   secondary = resolve_indefinite_grid_span(s, secondary_length);
        if (primary.end > end) {
            if (p == start)
                return {primary, secondary};
            s = advance_position(s);
            p = start;
            continue;
        }
        if (spans_all) {
            const auto next = occupancy.occupied_track_jump(secondary_axis, secondary);
            if (!next)
                return {primary, secondary};
            s = *next;
            p = start;
            continue;
        }
        const auto collision = occupancy.line_area_collision_jump(axis, primary, secondary);
        if (!collision)
            return {primary, secondary};
        p = *collision;
    }
}
} // namespace

void place_grid_items(CellOccupancy& occupancy, std::span<PlacedGridItem> items,
                      std::span<const ItemPlacement> placements, GridAutoFlow flow) {
    if (items.size() != placements.size())
        throw std::invalid_argument("grid items and placements must be parallel");
    const auto axis = primary_axis(flow), secondary_axis = other_axis(axis);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto placement = placements[i];
        if (is_definite(placement.horizontal) && is_definite(placement.vertical))
            record(occupancy, items[i], axis, resolve_definite_grid_lines(get(placement, axis)),
                   resolve_definite_grid_lines(get(placement, secondary_axis)),
                   CellOccupancyState::DefinitelyPlaced);
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto placement = placements[i];
        if (is_definite(get(placement, secondary_axis)) && !is_definite(get(placement, axis))) {
            const auto [primary, secondary] = place_secondary(occupancy, placement, flow);
            record(occupancy, items[i], axis, primary, secondary, CellOccupancyState::AutoPlaced);
        }
    }
    const auto start = std::pair{occupancy.track_counts(axis).implicit_start_line(),
                                 occupancy.track_counts(secondary_axis).implicit_start_line()};
    auto cursor = start;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto placement = placements[i];
        if (is_definite(get(placement, secondary_axis)))
            continue;
        const auto [primary, secondary] = place_indefinite(occupancy, placement, flow, cursor);
        record(occupancy, items[i], axis, primary, secondary, CellOccupancyState::AutoPlaced);
        cursor = is_dense(flow) ? start : std::pair{primary.end, secondary.start};
    }
}

std::vector<PlacedGridItem> place_grid_items(LayoutTree& tree, NodeId node,
                                             CellOccupancy& occupancy,
                                             const NamedLineResolver& resolver) {
    std::vector<PlacedGridItem> items;
    std::vector<ItemPlacement> placements;
    const auto children = tree.child_ids(node);
    for (std::size_t i = 0; i < children.size(); ++i) {
        const auto child = children[i];
        const auto& style = tree.grid_child_style(child);
        if (style.display == Display::None || taffy::is_out_of_flow(style.position))
            continue;
        items.push_back({child, i, {}, {}});
        placements.push_back(resolver.resolve(style));
    }
    place_grid_items(occupancy, items, placements, tree.grid_container_style(node).grid_auto_flow);
    return items;
}

} // namespace taffy::grid
