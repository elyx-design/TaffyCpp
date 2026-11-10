#pragma once

/// @file
/// Private balanced flex-line partitioning helpers.

#include <cstddef>
#include <span>
#include <vector>

namespace taffy::flex_detail {

/// Pinned balance::to_size: widen f32 exactly, clamp negatives to zero and
/// positive infinity to float::max; preserve NaN. No integer/pixel rounding.
[[nodiscard]] double quantize_size(float value) noexcept;

/// Pinned balanced_line_item_counts. Nonempty item_sizes is required (otherwise
/// throws invalid_argument). Returns positive contiguous line item counts summing
/// to item_sizes.size(). Sizes and gap use quantize_size; line_limit is widened
/// without clamping. The line count is max(greedy count, min_line_count clamped
/// to [1, item count]). Minimize squared line sizes, glue zero items whenever
/// feasible, and break ties toward more items on earlier lines.
/// O(lines * items * log(items)) time, O(lines * items) transient storage.
[[nodiscard]] std::vector<std::size_t> balance_lines(std::span<const float> item_sizes,
                                                     float line_limit, float gap_between_items,
                                                     std::size_t min_line_count);

} // namespace taffy::flex_detail
