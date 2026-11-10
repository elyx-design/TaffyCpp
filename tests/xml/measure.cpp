#include "measure.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace xml_test {
namespace {
// Deliberate isolated duplication of the intrinsic Ahem branch in
// tests/support/measure_helpers.cpp (upstream tests/common/src/lib.rs).
// The existing TU's intrinsic is private and its public wrapper links the
// flattened adapter. Do NOT call that wrapper: native Rust compute_leaf_layout
// must retain the full parsed Style. XML supplies only absent/Ahem contexts.
std::size_t saturating_usize(float value) {
    if (!(value > 0.0f))
        return 0;
    if (value >= std::ldexp(1.0f, std::numeric_limits<std::size_t>::digits))
        return std::numeric_limits<std::size_t>::max();
    return static_cast<std::size_t>(value);
}
OracleSize intrinsic(TextContext* context, OracleKnown known, OracleSizing available) {
    if (known.width.present && known.height.present)
        return {known.width.value, known.height.value};
    if (!context)
        return {known.width.present ? known.width.value : 0.0f,
                known.height.present ? known.height.value : 0.0f};
    ++context->count;
    constexpr std::string_view zws = "\xE2\x80\x8B";
    const std::string_view text = context->text;
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
    const auto min_length = *std::max_element(lines.begin(), lines.end());
    std::size_t max_length = 0;
    for (auto length : lines)
        max_length += length;
    const float min_inline = static_cast<float>(min_length) * 10.0f;
    const float max_inline = static_cast<float>(max_length) * 10.0f;
    const auto inline_known = context->vertical ? known.height : known.width;
    const auto block_known = context->vertical ? known.width : known.height;
    const auto inline_available = context->vertical ? available.height : available.width;
    float inline_size;
    if (inline_known.present)
        inline_size = inline_known.value;
    else
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
    inline_size = std::fmax(inline_size, min_inline);
    float block_size;
    if (block_known.present)
        block_size = block_known.value;
    else {
        const auto capacity = saturating_usize(std::floor(inline_size / 10.0f));
        std::size_t line_count = 1, current_length = 0;
        for (auto length : lines) {
            if (current_length + length > capacity) {
                if (current_length > 0)
                    ++line_count;
                current_length = length;
            } else
                current_length += length;
        }
        block_size = static_cast<float>(line_count) * 10.0f;
    }
    const OracleSize size = context->vertical ? OracleSize{block_size, inline_size}
                                              : OracleSize{inline_size, block_size};
    return {known.width.present ? known.width.value : size.width,
            known.height.present ? known.height.value : size.height};
}
} // namespace
uint32_t measure(void* user, uint64_t node, OracleKnown known, OracleSizing available,
                 OracleSize* out) noexcept {
    try {
        auto& contexts = *static_cast<Contexts*>(user);
        const auto found = contexts.find(node);
        *out = intrinsic(found == contexts.end() ? nullptr : &found->second, known, available);
        return 0;
    } catch (...) {
        return ORACLE_CALLBACK_FAILURE;
    }
}
} // namespace xml_test
