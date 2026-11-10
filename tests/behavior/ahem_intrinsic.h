#ifndef TAFFY_TEST_BEHAVIOR_AHEM_INTRINSIC_H
#define TAFFY_TEST_BEHAVIOR_AHEM_INTRINSIC_H

#include "../support/behavior_suite_oracle.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace grid_behavior {
// Raw intrinsic translation copied from tests/support/measure_helpers.cpp.
// Original: upstream/tests/common/src/lib.rs at v0.15.1,
// 0064bd54cbe38bf1d8bcd6ec6ab0704d45fb048a.
// Source SHA256: 9a12e587d5182aaf771116c52c896a624c13193f3414a7f485ca2f3cee0a0094.
// No RustBackend::leaf or C++ box/layout algorithm: the native wrapper applies
// compute_leaf_layout exactly once around this raw C++ intrinsic callback.
struct AhemContext {
    std::size_t count = 0;
    std::string text_content;
    bool vertical = false;
};

inline std::size_t ahem_saturating_usize(float value) {
    // Match Rust's saturating float-to-usize cast, including NaN -> 0.
    if (!(value > 0.0f)) return 0;
    if (value >= std::ldexp(1.0f, std::numeric_limits<std::size_t>::digits))
        return std::numeric_limits<std::size_t>::max();
    return static_cast<std::size_t>(value);
}

inline BehaviorSize ahem_intrinsic(AhemContext* context, BehaviorKnown known,
                                   BehaviorAvailable available) {
    if (known.width.present && known.height.present)
        return {known.width.value, known.height.value};
    if (context == nullptr)
        return {known.width.present ? known.width.value : 0.0f,
                known.height.present ? known.height.value : 0.0f};
    ++context->count;
    constexpr std::string_view zws = "\xE2\x80\x8B";
    const std::string_view text = context->text_content;
    std::vector<std::size_t> lines;
    std::size_t start = 0;
    for (;;) {
        const auto end = text.find(zws, start);
        if (end == std::string_view::npos) {
            lines.push_back(text.size() - start);
            break;
        }
        lines.push_back(end - start);
        start = end + zws.size();
    }
    // Rust split yields one empty segment for empty text; lengths are bytes.
    const auto min_length = *std::max_element(lines.begin(), lines.end());
    std::size_t max_length = 0;
    for (auto length : lines) max_length += length;
    const float min_inline = static_cast<float>(min_length) * 10.0f;
    const float max_inline = static_cast<float>(max_length) * 10.0f;
    const bool vertical = context->vertical;
    const auto inline_known = vertical ? known.height : known.width;
    const auto block_known = vertical ? known.width : known.height;
    const auto inline_available = vertical ? available.height : available.width;
    float inline_size;
    if (inline_known.present) {
        inline_size = inline_known.value;
    } else {
        switch (inline_available.tag) {
        case 3: inline_size = min_inline; break;
        case 4: inline_size = max_inline; break;
        case 1: inline_size = std::fmin(inline_available.value, max_inline); break;
        default: throw std::invalid_argument("invalid Ahem available-space tag");
        }
    }
    // Rust f32::min/max select the non-NaN operand, unlike std::min/max.
    inline_size = std::fmax(inline_size, min_inline);
    float block_size;
    if (block_known.present) {
        block_size = block_known.value;
    } else {
        const auto capacity = ahem_saturating_usize(std::floor(inline_size / 10.0f));
        std::size_t line_count = 1;
        std::size_t current_length = 0;
        for (auto length : lines) {
            if (current_length + length > capacity) {
                if (current_length > 0) ++line_count;
                current_length = length;
            } else {
                current_length += length;
            }
        }
        block_size = static_cast<float>(line_count) * 10.0f;
    }
    const BehaviorSize size = vertical ? BehaviorSize{block_size, inline_size}
                                       : BehaviorSize{inline_size, block_size};
    return {known.width.present ? known.width.value : size.width,
            known.height.present ? known.height.value : size.height};
}
} // namespace grid_behavior
#endif
