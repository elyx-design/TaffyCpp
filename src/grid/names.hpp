// Private, owning name index shared by placement and detailed-grid snapshots.
#pragma once
#include "placement_types.hpp"
#include "taffy/layout.hpp"
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace taffy::grid {
class NameIndex {
    std::unordered_map<std::string, std::vector<std::int32_t>> lines_;
    std::unordered_map<std::string, Line<std::optional<std::int32_t>>> areas_;

    static std::int32_t clamp(std::int64_t n) {
        return static_cast<std::int32_t>(
            std::clamp<std::int64_t>(n, -max_grid_tracks, max_grid_tracks));
    }

    static GridPlacement css(std::int64_t n, std::uint16_t tracks) {
        const auto line = clamp(n);
        const auto index = line >= 0 ? line + 1 : line - (std::int32_t(tracks) + 1);
        return GridPlacement::line(static_cast<std::int16_t>(
            std::clamp<std::int32_t>(index, std::numeric_limits<std::int16_t>::min(),
                                     std::numeric_limits<std::int16_t>::max())));
    }

public:
    NameIndex() = default;

    explicit NameIndex(const TemplateLineNames& names) {
        for (std::size_t i = 0; i < names.size(); ++i)
            for (const auto& name : names[i])
                add(name, static_cast<std::uint32_t>(i + 1));
    }

    // CSS line zero denotes an absent area edge, not origin-zero line -1.
    void add(const std::string& name, std::uint32_t line) {
        if (line != 0)
            lines_[name].push_back(static_cast<std::int32_t>(line - 1));
    }

    void add_areas(std::span<const GridTemplateArea> areas, GridAreaAxis axis) {
        for (const auto& area : areas) {
            add(area.name + "-start",
                axis == GridAreaAxis::Column ? area.column_start : area.row_start);
            add(area.name + "-end", axis == GridAreaAxis::Column ? area.column_end : area.row_end);
        }
    }

    void finish() {
        areas_.clear();
        for (auto& [name, lines] : lines_) {
            std::sort(lines.begin(), lines.end());
            lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
            const bool start = name.ends_with("-start"), end = name.ends_with("-end");
            if (!start && !end)
                continue;
            auto& area = areas_[name.substr(0, name.size() - (start ? 6 : 4))];
            (start ? area.start : area.end) = lines.front();
        }
    }

    GridPlacement resolve(const GridPlacement& placement, GridAreaEnd end, std::uint16_t tracks,
                          std::optional<std::int32_t> opposite = {}) const {
        if (!placement.is_named())
            return placement;
        const auto find = [&](const std::string& name) -> std::span<const std::int32_t> {
            const auto it = lines_.find(name);
            return it == lines_.end() ? std::span<const std::int32_t>{} : it->second;
        };
        if (placement.kind() == GridPlacement::Kind::NamedLine && placement.line_index() == 0) {
            const auto area = areas_.find(placement.name());
            if (area != areas_.end()) {
                const auto edge = end == GridAreaEnd::Start ? area->second.start : area->second.end;
                if (edge)
                    return css(*edge, tracks);
            }
        }
        const auto matches = find(placement.name());
        bool backwards;
        std::int32_t count;
        std::int64_t boundary;
        if (placement.kind() == GridPlacement::Kind::NamedSpan) {
            if (!opposite)
                return GridPlacement::span(1);
            backwards = end == GridAreaEnd::Start;
            count = std::max<std::int32_t>(1, placement.span_count());
            boundary = std::int64_t(*opposite) + (backwards ? -1 : 1);
        } else {
            const auto index = placement.line_index();
            backwards = index < 0;
            count = backwards ? -std::int32_t(index) : std::max<std::int32_t>(1, index);
            boundary = backwards ? tracks : 0;
        }
        if (backwards) {
            boundary = std::min<std::int64_t>(boundary, tracks);
            const auto last = std::upper_bound(matches.begin(), matches.end(), boundary);
            const auto found = static_cast<std::int32_t>(last - matches.begin());
            if (count <= found)
                return css(*(last - count), tracks);
            return css(std::min<std::int64_t>(boundary, -1) - (count - found) + 1, tracks);
        }
        boundary = std::max<std::int64_t>(boundary, 0);
        const auto first = std::lower_bound(matches.begin(), matches.end(), boundary);
        const auto found = static_cast<std::int32_t>(matches.end() - first);
        if (count <= found)
            return css(*(first + count - 1), tracks);
        return css(std::max<std::int64_t>(boundary, std::int32_t(tracks) + 1) + (count - found) - 1,
                   tracks);
    }

    Line<GridPlacement> resolve(const Line<GridPlacement>& placement, std::uint16_t tracks) const {
        const auto initial = [&](const GridPlacement& p, GridAreaEnd edge) {
            if (p.kind() == GridPlacement::Kind::NamedLine)
                return resolve(p, edge, tracks);
            // Do not copy the owned string of a named span just to resolve it.
            return p.kind() == GridPlacement::Kind::NamedSpan ? GridPlacement::span(1) : p;
        };
        auto start = initial(placement.start, GridAreaEnd::Start);
        auto end = initial(placement.end, GridAreaEnd::End);
        const auto origin = [tracks](const GridPlacement& p) -> std::optional<std::int32_t> {
            if (p.kind() != GridPlacement::Kind::Line || p.line_index() == 0)
                return {};
            return GridLine(p.line_index()).into_origin_zero_line(tracks);
        };
        // Evaluate both spans against the original opposite endpoint, as in Rust.
        const auto start_origin = origin(start), end_origin = origin(end);
        if (placement.start.kind() == GridPlacement::Kind::NamedSpan)
            start = resolve(placement.start, GridAreaEnd::Start, tracks, end_origin);
        if (placement.end.kind() == GridPlacement::Kind::NamedSpan)
            end = resolve(placement.end, GridAreaEnd::End, tracks, start_origin);
        return {std::move(start), std::move(end)};
    }
};

// Kept separate from serialized metadata; immutable owners may share this index.
struct DetailedNameIndexes {
    std::shared_ptr<const NameIndex> columns;
    std::shared_ptr<const NameIndex> rows;

    DetailedNameIndexes(std::shared_ptr<const NameIndex> column_index,
                        std::shared_ptr<const NameIndex> row_index)
        : columns(std::move(column_index)), rows(std::move(row_index)) {}

    explicit DetailedNameIndexes(const DetailedGridInfo& info) {
        auto column_index = std::make_shared<NameIndex>(info.columns.line_names);
        auto row_index = std::make_shared<NameIndex>(info.rows.line_names);
        for (const auto& [name, area] : info.areas) {
            // Preserve the checked narrowing contract of the metadata resolver.
            const auto check = [](std::uint32_t n) {
                if (n > std::numeric_limits<std::uint16_t>::max())
                    throw std::overflow_error("detailed grid area line exceeds uint16");
                return n;
            };
            column_index->add(name + "-start", check(area.column_start));
            column_index->add(name + "-end", check(area.column_end));
            row_index->add(name + "-start", check(area.row_start));
            row_index->add(name + "-end", check(area.row_end));
        }
        column_index->finish();
        row_index->finish();
        columns = std::move(column_index);
        rows = std::move(row_index);
    }
};

inline bool has_names(const Line<GridPlacement>& line) {
    return line.start.is_named() || line.end.is_named();
}
} // namespace taffy::grid
