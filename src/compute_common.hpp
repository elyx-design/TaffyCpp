#pragma once

/// @file
/// Private CSS alignment, baseline and overflow helpers shared by algorithms.
#include <taffy/layout.hpp>

namespace taffy {

/// Preserve a non-scroll-container's optional first baseline. For scroll containers,
/// synthesize a missing baseline at height and clamp it into [0,height].
std::optional<float> child_first_baseline(std::optional<float> baseline, float height,
                                          Overflow overflow_y);

/// Convert safe alignment to Start when the subject overflows; otherwise preserve
/// the keyword. Resolve Normal/self-relative keywords before using the result.
AlignItemsKeyword resolve_self_alignment_safety(AlignItems alignment, bool overflows);

/// Resolve content-distribution fallbacks for item count, free space and safety.
/// Resolve Normal with layout-specific rules first; unresolved Normal becomes Stretch.
AlignContentKeyword apply_alignment_fallback(float free_space, std::size_t items,
                                             AlignContent alignment);

/// Offset before the first item (first=true), or spacing before a subsequent item.
/// Inputs are layout units after distribution fallback; reversed affects flex edges.
/// Use valid item counts for distribution (at least two for SpaceBetween spacing);
/// a subsequent SpaceBetween query with zero items throws TreePanic.
float compute_alignment_offset(float free_space, std::size_t items, float gap,
                               AlignContentKeyword alignment, bool flex_reversed, bool first);

/// Compute a child's overflow contribution in parent coordinates. Visible overflow
/// propagates only when not suppressed by scrolling/containment. For a scrolling
/// parent, wholly negative-axis contributions are excluded. Rect fields are edges,
/// not width/height; location and size describe the child's border box.
Rect<float> compute_scrollable_overflow_contribution(Point<float> location, Size<float> size,
                                                     Rect<float> overflow_rect,
                                                     Point<Overflow> overflow, Contain contain,
                                                     bool parent_is_scroll_container);

} // namespace taffy
