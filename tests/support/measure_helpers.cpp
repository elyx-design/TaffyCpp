#include "measure_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace taffy::test {
namespace {

// Rust's float-to-usize cast saturates, including NaN -> 0. Check against
// 2^digits, not a rounded float representation of SIZE_MAX (also 2^digits).
std::size_t saturating_usize(float value) {
    if (!(value > 0.0f))
        return 0;
    if (value >= std::ldexp(1.0f, std::numeric_limits<std::size_t>::digits)) {
        return std::numeric_limits<std::size_t>::max();
    }
    return static_cast<std::size_t>(value);
}

OracleSize measure_ahem(const AhemTextMeasureData& data, OracleKnown known,
                        OracleSizing available) {
    constexpr std::string_view zws = "\xE2\x80\x8B";
    const std::string_view text = data.text_content;
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
    // split('​') yields one empty segment for empty text, not zero segments.
    const auto min_length = *std::max_element(lines.begin(), lines.end());
    std::size_t max_length = 0;
    for (auto length : lines)
        max_length += length;
    const float min_inline = static_cast<float>(min_length) * 10.0f;
    const float max_inline = static_cast<float>(max_length) * 10.0f;
    const bool vertical = data.writing_mode == WritingMode::Vertical;
    const auto inline_known = vertical ? known.height : known.width;
    const auto block_known = vertical ? known.width : known.height;
    const auto inline_available = vertical ? available.height : available.width;
    float inline_size;
    if (inline_known.present) {
        inline_size = inline_known.value;
    } else {
        switch (inline_available.tag) {
        case 3:
            inline_size = min_inline;
            break;
        case 4:
            inline_size = max_inline;
            break;
        case 1:
            inline_size = std::fmin(inline_available.value, max_inline);
            break;
        default:
            throw std::invalid_argument("invalid Ahem available-space tag");
        }
    }
    // fmin/fmax, unlike std::min/max, select the non-NaN operand as Rust does.
    inline_size = std::fmax(inline_size, min_inline);
    float block_size;
    if (block_known.present) {
        block_size = block_known.value;
    } else {
        const auto capacity = saturating_usize(std::floor(inline_size / 10.0f));
        std::size_t line_count = 1;
        std::size_t current_length = 0;
        for (auto length : lines) {
            if (current_length + length > capacity) {
                if (current_length > 0)
                    ++line_count;
                current_length = length;
            } else {
                current_length += length;
            }
        }
        block_size = static_cast<float>(line_count) * 10.0f;
    }
    return vertical ? OracleSize{block_size, inline_size} : OracleSize{inline_size, block_size};
}

OracleSize intrinsic(TestNodeContext* context, OracleKnown known, OracleSizing available) {
    if (known.width.present && known.height.present) {
        return {known.width.value, known.height.value};
    }
    if (context == nullptr) {
        return {known.width.present ? known.width.value : 0.0f,
                known.height.present ? known.height.value : 0.0f};
    }
    ++context->count;
    const auto size = std::visit(
        [&](const auto& data) -> OracleSize {
            using T = std::remove_cvref_t<decltype(data)>;
            if constexpr (std::is_same_v<T, ZeroMeasureData>) {
                return {};
            } else if constexpr (std::is_same_v<T, OracleSize>) {
                return data;
            } else if constexpr (std::is_same_v<T, AspectRatioMeasureData>) {
                const auto width = known.width.present ? known.width.value : data.width;
                return {width,
                        known.height.present ? known.height.value : width * data.height_ratio};
            } else {
                return measure_ahem(data, known, available);
            }
        },
        context->measure_data);
    return {known.width.present ? known.width.value : size.width,
            known.height.present ? known.height.value : size.height};
}

} // namespace

TestNodeContext TestNodeContext::zero() {
    return {};
}
TestNodeContext TestNodeContext::fixed(float width, float height) {
    return {0, OracleSize{width, height}};
}
TestNodeContext TestNodeContext::aspect_ratio(float width, float height_ratio) {
    return {0, AspectRatioMeasureData{width, height_ratio}};
}
TestNodeContext TestNodeContext::ahem_text(std::string text, WritingMode mode) {
    return {0, AhemTextMeasureData{std::move(text), mode}};
}

// Translation: upstream/tests/common/src/lib.rs at v0.15.1,
// 0064bd54cbe38bf1d8bcd6ec6ab0704d45fb048a.
// Source SHA256: 9a12e587d5182aaf771116c52c896a624c13193f3414a7f485ca2f3cee0a0094.
// Native leaf owns all box sizing.
OracleLayoutOutput test_measure_function(const OracleLayoutInput& input, RustBackend::NodeId,
                                         TestNodeContext* context, const OracleStyle& style) {
    return RustBackend::leaf(style, input, [context](OracleKnown known, OracleSizing available) {
        return intrinsic(context, known, available);
    });
}

} // namespace taffy::test
