// Pinned upstream compute/grid/implicit_grid.rs and types/grid_track_counts.rs.
// The estimate may grow during placement; negative and positive sides stay distinct.
#include "placement.hpp"

namespace taffy::grid {

Size<TrackCounts> compute_grid_size_estimate(Size<std::uint16_t> explicit_counts,
                                             std::span<const ItemPlacement> placements) {
    const auto estimate = [&](AbsoluteAxis axis, std::uint16_t explicit_count) {
        std::int32_t minimum = 0, maximum = 0;
        std::uint16_t max_span = 0;
        for (const auto& placement : placements) {
            const auto line =
                axis == AbsoluteAxis::Horizontal ? placement.horizontal : placement.vertical;
            if (is_definite(line)) {
                const auto resolved = resolve_definite_grid_lines(line);
                minimum =
                    std::min(minimum, std::max(resolved.start, -std::int32_t(max_grid_tracks)));
                maximum = std::max(maximum, std::min(resolved.end, std::int32_t(max_grid_tracks)));
                max_span = std::max<std::uint16_t>(max_span, 1);
            } else {
                max_span = std::max(max_span, indefinite_span(line));
            }
        }
        const auto negative = static_cast<std::uint16_t>(-minimum);
        auto positive = static_cast<std::uint16_t>(std::max(maximum - explicit_count, 0));
        if (std::uint32_t(negative) + explicit_count + positive < max_span)
            positive = static_cast<std::uint16_t>(max_span - explicit_count - negative);
        return TrackCounts{negative, explicit_count, positive};
    };
    return {estimate(AbsoluteAxis::Horizontal, explicit_counts.width),
            estimate(AbsoluteAxis::Vertical, explicit_counts.height)};
}

Size<TrackCounts> estimate_implicit_grid_tracks(LayoutTree& tree, NodeId node,
                                                Size<std::uint16_t> explicit_counts,
                                                const NamedLineResolver& resolver) {
    std::vector<ItemPlacement> placements;
    for (const auto child : tree.child_ids(node)) {
        const auto& style = tree.grid_child_style(child);
        if (style.display == Display::None || taffy::is_out_of_flow(style.position))
            continue;
        placements.push_back(resolver.resolve(style));
    }
    return compute_grid_size_estimate(explicit_counts, placements);
}

} // namespace taffy::grid
