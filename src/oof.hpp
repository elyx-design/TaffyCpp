#pragma once

/// @file
/// Private deferred out-of-flow positioning helpers.
#include <taffy/compute.hpp>

namespace taffy {

/// Deferred absolute/fixed positioning result, owning IDs and propagated candidates.
struct OofLayoutResult {
    std::vector<NodeId> hoisted{}; ///< Positioned descendants to include in rounding.
    OofCandidates unclaimed{};     ///< Candidates needing an ancestor containing block.
    /// Combined overflow relative to the positioning area's origin; horizontal
    /// coordinates are direction-normalized (mirrored for RTL).
    Rect<float> scrollable_overflow_rect{};
};

/// Position a borrowed candidate range in area using direction and containing-block
/// claims. Candidates are not retained; recursive layout uses tree's hooks.
OofLayoutResult compute_oof_layout_for_area(LayoutTree& tree, NodeId geometry_owner,
                                            std::span<const OofCandidate> candidates,
                                            OofPositioningArea area, Direction direction,
                                            ContainingBlockClaims claims);
/// Resolve static alignment to the final border-box origin, accounting for margins,
/// overflow safety and start/center/end anchoring in each physical axis.
Point<float> resolve_static_offset(Point<AxisStaticPosition> position, Size<float> final_size,
                                   Rect<float> margin);

} // namespace taffy
