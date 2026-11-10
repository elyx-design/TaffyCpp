#pragma once
// Test-only detailed-grid transport. All grid queries use production metadata/APIs.
#include "behavior_style.hpp"
#include "../../../src/grid.hpp"
#include <cmath>
#include <stdexcept>

namespace behavior_cpp {
namespace grid_detail {
inline void axis_valid(std::uint32_t axis) {
    if (axis > 1) throw std::invalid_argument("invalid track axis");
}
inline const taffy::DetailedGridTracksInfo& axis(const taffy::DetailedGridInfo* g,
                                                std::uint32_t which) {
    axis_valid(which);
    if (!g) throw std::runtime_error("node has no native detailed grid info");
    return which == 0 ? g->rows : g->columns;
}
inline taffy::GridPlacement placement(BehaviorText text) {
    if (text.len && !text.data) throw std::invalid_argument("null placement text");
    const std::string_view s(text.data ? text.data : "", text.len);
    style_detail::utf8(s);
    auto result = taffy::parse_css<taffy::GridPlacement>(s);
    if (!result) throw std::invalid_argument("invalid grid placement: " + result.error().message);
    return *result;
}
inline style_detail::Json tracks_json(const taffy::DetailedGridTracksInfo& t) {
    using style_detail::Json;
    using style_detail::encode;
    auto result = Json::obj();
    result.object["negative_implicit_tracks"] = encode(t.negative_implicit_tracks);
    result.object["explicit_tracks"] = encode(t.explicit_tracks);
    result.object["positive_implicit_tracks"] = encode(t.positive_implicit_tracks);
    auto positions = Json::arr();
    for (const auto& p : t.positions) {
        auto position = Json::obj();
        position.object["start"] = encode(p.start);
        position.object["end"] = encode(p.end);
        positions.array.push_back(std::move(position));
    }
    result.object["positions"] = std::move(positions);
    auto collapsed = Json::arr();
    for (const auto& range : t.collapsed_tracks) {
        auto r = Json::obj();
        r.object["start"] = encode(range.start);
        r.object["end"] = encode(range.end);
        collapsed.array.push_back(std::move(r));
    }
    result.object["collapsed_tracks"] = std::move(collapsed);
    result.object["empty_axis_line"] = encode(t.empty_axis_line);
    result.object["line_names"] = encode(t.line_names);
    result.object["resolved_line_names"] = encode(t.iter_line_names());
    return result;
}
} // namespace grid_detail

inline const taffy::DetailedGridInfo* grid(const taffy::Tree& tree, taffy::NodeId node) {
    return std::get_if<taffy::DetailedGridInfo>(&tree.detailed_layout_info(node));
}

inline std::string detailed_json(const taffy::DetailedGridInfo* g) {
    if (!g) return "null";
    using style_detail::Json;
    using style_detail::encode;
    auto result = Json::obj();
    result.object["rows"] = grid_detail::tracks_json(g->rows);
    result.object["columns"] = grid_detail::tracks_json(g->columns);
    auto items = Json::arr();
    for (const auto& i : g->items) {
        auto item = Json::obj();
        item.object["row_start"] = encode(i.row_start);
        item.object["row_end"] = encode(i.row_end);
        item.object["column_start"] = encode(i.column_start);
        item.object["column_end"] = encode(i.column_end);
        items.array.push_back(std::move(item));
    }
    result.object["items"] = std::move(items);
    return style_detail::dump(result);
}

inline std::string tracks(const taffy::DetailedGridInfo* g, std::uint32_t axis, float zoom) {
    grid_detail::axis_valid(axis);
    if (!std::isfinite(zoom) || zoom <= 0)
        throw std::invalid_argument("invalid track zoom");
    if (!g) return {};
    return axis == 0 ? g->grid_template_rows(zoom) : g->grid_template_columns(zoom);
}

inline std::optional<taffy::Rect<float>> item_area(const taffy::DetailedGridInfo* g,
                                                 std::size_t index) {
    if (!g) return std::nullopt;
    auto area = g->item_grid_area(index);
    if (!area) return std::nullopt;
    const auto& [p, s] = *area;
    return taffy::Rect<float>{p.x, p.x + s.width, p.y, p.y + s.height};
}

inline std::optional<taffy::Rect<float>> resolve_area(
    const taffy::DetailedGridInfo* g, BehaviorText rowstart, BehaviorText rowend,
    BehaviorText colstart, BehaviorText colend, std::uint32_t direction, BehaviorRect padding) {
    if (direction > 1) throw std::invalid_argument("invalid grid direction");
    const taffy::Line<taffy::GridPlacement> rows{
        grid_detail::placement(rowstart), grid_detail::placement(rowend)};
    const taffy::Line<taffy::GridPlacement> columns{
        grid_detail::placement(colstart), grid_detail::placement(colend)};
    if (!g) return std::nullopt;
    return taffy::grid::resolve_absolute_grid_area(
        *g, rows, columns, direction == 0 ? taffy::Direction::Ltr : taffy::Direction::Rtl,
        {padding.left, padding.right, padding.top, padding.bottom});
}

inline void axis_info(const taffy::DetailedGridInfo* g, std::uint32_t axis,
                      BehaviorGridAxisInfo* out) {
    if (!out) throw std::invalid_argument("null grid axis output");
    const auto& t = grid_detail::axis(g, axis);
    *out = {t.negative_implicit_tracks, t.explicit_tracks, t.positive_implicit_tracks,
            static_cast<std::uint32_t>(t.line_names.empty()), t.positions.size(),
            t.iter_line_names().size(),
            {static_cast<std::uint32_t>(t.empty_axis_line.has_value()),
             t.empty_axis_line.value_or(0)}};
}

inline void position(const taffy::DetailedGridInfo* g, std::uint32_t axis,
                     std::size_t index, float* start, float* end) {
    if (!start || !end) throw std::invalid_argument("null grid position output");
    const auto& t = grid_detail::axis(g, axis);
    if (index >= t.positions.size()) throw std::invalid_argument("grid position index out of bounds");
    *start = t.positions[index].start;
    *end = t.positions[index].end;
}

inline std::vector<std::string> names(const taffy::DetailedGridInfo* g, std::uint32_t axis,
                                     std::size_t line, std::uint32_t mode) {
    const auto& t = grid_detail::axis(g, axis);
    switch (mode) {
    case 0: return line < t.line_names.size() ? t.line_names[line] : std::vector<std::string>{};
    case 1: {
        auto resolved = t.iter_line_names();
        if (line >= resolved.size()) throw std::invalid_argument("line-name iterator index out of bounds");
        return std::move(resolved[line]);
    }
    case 2: {
        auto resolved = t.names_for_line(line);
        return {resolved.begin(), resolved.end()};
    }
    default: throw std::invalid_argument("unknown grid names mode");
    }
}
} // namespace behavior_cpp
