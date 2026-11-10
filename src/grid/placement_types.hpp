#pragma once

#include "taffy/grid.hpp"

namespace taffy {

/// Axis used to select a named area's boundary lines.
enum class GridAreaAxis : std::uint8_t {
    Row,   ///< Row boundaries.
    Column ///< Column boundaries.
};
/// Edge used for implicit area-name aliases.
enum class GridAreaEnd : std::uint8_t {
    Start, ///< The area's name-start line.
    End    ///< The area's name-end line.
};

/// Algorithm-facing placement after resolving names; Line is an origin-zero signed index.
struct OriginZeroGridPlacement {
    /// Resolved placement form; names have already been removed.
    enum class Kind : std::uint8_t {
        Auto, ///< Automatic placement.
        Line, ///< Origin-zero signed line coordinate.
        Span  ///< Track span count.
    };
    Kind kind{Kind::Auto}; ///< Discriminant for value.
    std::int32_t value{};  ///< Signed line coordinate or track span count.

    static constexpr OriginZeroGridPlacement auto_() {
        return {};
    }

    static constexpr OriginZeroGridPlacement line(std::int32_t index) {
        return {Kind::Line, index};
    }

    static constexpr OriginZeroGridPlacement span(std::uint16_t count) {
        return {Kind::Span, count};
    }

    constexpr bool is_line() const {
        return kind == Kind::Line;
    }

    friend constexpr bool operator==(OriginZeroGridPlacement, OriginZeroGridPlacement) = default;
};

/// Whether either resolved edge is a line coordinate.
bool is_definite(Line<OriginZeroGridPlacement> placement);
/// Convert numeric lines/spans; named/auto values become auto. Spans clamp to [1, 10000].
OriginZeroGridPlacement into_origin_zero_placement_ignoring_named(const GridPlacement& placement,
                                                                  std::uint16_t explicit_tracks);
/// Convert each numeric edge using explicit_tracks, ignoring unresolved names.
Line<OriginZeroGridPlacement> into_origin_zero_ignoring_named(const Line<GridPlacement>& placement,
                                                              std::uint16_t explicit_tracks);
/// Stored start span, else end span, else one; throws logic_error when both edges are lines.
std::uint16_t indefinite_span(Line<OriginZeroGridPlacement> placement);
/// Resolve/sort definite edges, extending equal lines by one track.
/// Throws logic_error if no line is present, overflow_error if arithmetic exceeds int32_t.
Line<std::int32_t> resolve_definite_grid_lines(Line<OriginZeroGridPlacement> placement);
/// Resolve definite line/span pairs; unresolved automatic edges stay nullopt.
/// Coordinate arithmetic overflow propagates as overflow_error.
Line<std::optional<std::int32_t>>
resolve_absolutely_positioned_grid_tracks(Line<OriginZeroGridPlacement> placement);
/// Place an indefinite span at start; throws logic_error for definite input,
/// or overflow_error if the end coordinate exceeds int32_t.
Line<std::int32_t> resolve_indefinite_grid_tracks(Line<OriginZeroGridPlacement> placement,
                                                  std::int32_t start);

/// Resolve named lines and area aliases against a fully expanded line-name list.
/// Missing occurrences count implicit lines outside the explicit grid, per CSS Grid.
/// A bare named line (occurrence zero) prefers the area's implicit name-start/name-end line.
GridPlacement
resolve_named_grid_placement(const GridPlacement& placement, const TemplateLineNames& names,
                             std::span<const GridTemplateArea> areas, GridAreaAxis axis,
                             GridAreaEnd end, std::uint16_t explicit_tracks,
                             std::optional<std::int32_t> opposite_line = std::nullopt);
/// Resolve both named edges, using opposite definite lines to resolve named spans.
Line<GridPlacement> resolve_grid_placement_names(const Line<GridPlacement>& placement,
                                                 const TemplateLineNames& names,
                                                 std::span<const GridTemplateArea> areas,
                                                 GridAreaAxis axis, std::uint16_t explicit_tracks);
/// Resolve names and convert the pair to bounded origin-zero coordinates.
Line<OriginZeroGridPlacement> into_origin_zero(const Line<GridPlacement>& placement,
                                               const TemplateLineNames& names,
                                               std::span<const GridTemplateArea> areas,
                                               GridAreaAxis axis, std::uint16_t explicit_tracks);

} // namespace taffy
