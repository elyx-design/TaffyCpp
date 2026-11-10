// Test-only, independent translation of upstream/tests/common/src/lib.rs.
// The helper fixture is translated here; all CSS leaf sizing is production C++.
// Do not call tests/support/measure_helpers.cpp: that is the comparison side.
#include "../support/helper_oracle.h"
#include <taffy/compute.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace {
struct AbiError { std::uint32_t status; };
[[noreturn]] void invalid() { throw AbiError{ORACLE_INVALID}; }
[[noreturn]] void unsupported() { throw AbiError{ORACLE_UNSUPPORTED}; }

bool boolean(std::uint32_t value) {
    if (value > 1) unsupported();
    return value != 0;
}
std::optional<float> optional(OracleOptional value) {
    return boolean(value.present) ? std::optional<float>{value.value} : std::nullopt;
}
taffy::Size<std::optional<float>> known(OracleKnown value) {
    return {optional(value.width), optional(value.height)};
}
taffy::AvailableSpace available(OracleScalar value) {
    switch (value.tag) {
    case 1: return taffy::AvailableSpace::definite(value.value);
    case 3: return taffy::AvailableSpace::min_content();
    case 4: return taffy::AvailableSpace::max_content();
    default: unsupported();
    }
}
taffy::LayoutInput convert_input(const OracleLayoutInput& value) {
    taffy::LayoutInput result;
    switch (value.run_mode) {
    case 0: result.run_mode = taffy::RunMode::PerformLayout; break;
    case 1: result.run_mode = taffy::RunMode::ComputeSize; break;
    case 2: result.run_mode = taffy::RunMode::PerformHiddenLayout; break;
    default: unsupported();
    }
    switch (value.sizing_mode) {
    case 0: result.sizing_mode = taffy::SizingMode::ContentSize; break;
    case 1: result.sizing_mode = taffy::SizingMode::InherentSize; break;
    default: unsupported();
    }
    switch (value.axis) {
    case 0: result.axis = taffy::RequestedAxis::Horizontal; break;
    case 1: result.axis = taffy::RequestedAxis::Vertical; break;
    case 2: result.axis = taffy::RequestedAxis::Both; break;
    default: unsupported();
    }
    result.known_dimensions = known(value.known_dimensions);
    result.known_dimensions_are_definite = {boolean(value.definite_width), boolean(value.definite_height)};
    result.parent_size = known(value.parent_size);
    result.available_space = {available(value.available_space.width), available(value.available_space.height)};
    result.vertical_margins_are_collapsible = {boolean(value.collapse_start), boolean(value.collapse_end)};
    return result;
}

struct AddressRange { std::uintptr_t start, end; };
AddressRange range(const void* pointer, std::size_t length) {
    const auto start = reinterpret_cast<std::uintptr_t>(pointer);
    if (length > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) ||
        length > std::numeric_limits<std::uintptr_t>::max() - start) invalid();
    return {start, start + length};
}
bool overlaps(AddressRange a, AddressRange b) {
    return a.start != a.end && b.start != b.end && a.start < b.end && b.start < a.end;
}
template<class T> void record(const T* pointer) {
    if (!pointer || reinterpret_cast<std::uintptr_t>(pointer) % alignof(T) != 0) invalid();
}

// Strict scalar-value UTF-8: reject overlong forms, surrogates, values above
// U+10FFFF, isolated continuation bytes, and truncated sequences. NUL is valid.
void validate_utf8(std::string_view text) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto first = static_cast<unsigned char>(text[offset++]);
        if (first < 0x80) continue;
        unsigned trailing;
        std::uint32_t scalar, minimum;
        if (first >= 0xc2 && first <= 0xdf) {
            trailing = 1; scalar = first & 0x1f; minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            trailing = 2; scalar = first & 0x0f; minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            trailing = 3; scalar = first & 0x07; minimum = 0x10000;
        } else { invalid(); }
        if (trailing > text.size() - offset) invalid();
        for (unsigned i = 0; i < trailing; ++i) {
            const auto next = static_cast<unsigned char>(text[offset++]);
            if ((next & 0xc0) != 0x80) invalid();
            scalar = (scalar << 6) | (next & 0x3f);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) invalid();
    }
}

std::size_t saturating_usize(float value) {
    if (!(value > 0)) return 0;
    // SIZE_MAX rounds up to 2^digits in f32, so use that exclusive bound.
    if (value >= std::ldexp(1.0f, std::numeric_limits<std::size_t>::digits))
        return std::numeric_limits<std::size_t>::max();
    return static_cast<std::size_t>(value);
}

taffy::Size<float> ahem(std::string_view text, bool vertical, const taffy::LayoutInput& input) {
    // Rust split(char) keeps empty segments, and str::len counts UTF-8 bytes.
    constexpr std::string_view separator = "\xe2\x80\x8b";
    std::vector<std::size_t> segments;
    std::size_t start = 0, total = 0, longest = 0;
    for (;;) {
        const auto end = text.find(separator, start);
        const auto length = (end == std::string_view::npos ? text.size() : end) - start;
        segments.push_back(length);
        total += length;
        longest = std::max(longest, length);
        if (end == std::string_view::npos) break;
        start = end + separator.size();
    }
    const auto inline_known = vertical ? input.known_dimensions.height : input.known_dimensions.width;
    const auto block_known = vertical ? input.known_dimensions.width : input.known_dimensions.height;
    const auto space = vertical ? input.available_space.height : input.available_space.width;
    const float minimum = static_cast<float>(longest) * 10.0f;
    const float maximum = static_cast<float>(total) * 10.0f;
    float inline_size;
    if (inline_known) inline_size = *inline_known;
    else if (space.is_min_content()) inline_size = minimum;
    else if (space.is_max_content()) inline_size = maximum;
    else inline_size = std::fmin(space.value(), maximum);
    inline_size = std::fmax(inline_size, minimum);
    float block_size;
    if (block_known) block_size = *block_known;
    else {
        const auto capacity = saturating_usize(std::floor(inline_size / 10.0f));
        std::size_t line_count = 1, used = 0;
        for (const auto length : segments) {
            if (used + length > capacity) {
                if (used > 0) ++line_count;
                used = length;
            } else used += length;
        }
        block_size = static_cast<float>(line_count) * 10.0f;
    }
    return vertical ? taffy::Size<float>{block_size, inline_size}
                    : taffy::Size<float>{inline_size, block_size};
}

taffy::Size<float> intrinsic(const HelperOracleRequest& request, std::string_view text,
                            bool vertical, std::size_t& count, const taffy::LayoutInput& input) {
    const auto dimensions = input.known_dimensions;
    if (dimensions.width && dimensions.height) return {*dimensions.width, *dimensions.height};
    if (request.kind == 0) return {dimensions.width.value_or(0), dimensions.height.value_or(0)};
    ++count;
    taffy::Size<float> size{};
    switch (request.kind) {
    case 1: break;
    case 2: size = {request.width, request.height_ratio_or_height}; break;
    case 3: {
        const float width = dimensions.width.value_or(request.width);
        size = {width, dimensions.height.value_or(width * request.height_ratio_or_height)};
        break;
    }
    case 4: size = ahem(text, vertical, input); break;
    default: unsupported();
    }
    return {dimensions.width.value_or(size.width), dimensions.height.value_or(size.height)};
}
} // namespace

extern "C" std::uint32_t helper_oracle_measure(const HelperOracleRequest* request,
                                               HelperOracleResult* result) {
    try {
        record(request);
        record(result);
        const auto request_range = range(request, sizeof(*request));
        const auto output_range = range(result, sizeof(*result));
        if (overlaps(request_range, output_range)) invalid();
        // Copy only after validating alignment/ranges, like the standalone oracle.
        const auto value = *request;
        const auto text_range = range(value.text, value.text_length);
        if ((!value.text && value.text_length != 0) || overlaps(text_range, output_range)) invalid();
        const auto input = convert_input(value.input);
        const bool vertical = boolean(value.vertical);
        if (value.kind > 4) unsupported();
        const std::string_view text = value.text_length == 0 ? std::string_view{}
            : std::string_view{reinterpret_cast<const char*>(value.text), value.text_length};
        validate_utf8(text); // Required even for non-text/known-size calls.
        std::size_t count = 0;
        const taffy::IntrinsicSizeFunction measure = [&](const taffy::LayoutInput& measured) {
            return intrinsic(value, text, vertical, count, measured);
        };
        const auto output = taffy::compute_leaf_layout(
            input, taffy::Style{}, {}, measure);
        *result = HelperOracleResult{{output.size.width, output.size.height}, count};
        return ORACLE_OK;
    } catch (const AbiError& error) {
        return error.status;
    } catch (...) {
        // No C++ exception crosses the C ABI; output is unchanged on failure.
        return ORACLE_PANIC;
    }
}
