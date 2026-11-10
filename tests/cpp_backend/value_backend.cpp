// Native test-only implementation of the operation ABI in support/value_oracle.h.
// Wire tags are semantic codes, never native enum discriminants or packed storage.
#include "../support/value_oracle.h"
#include <taffy/parse.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

// The concrete ABI records live beside the assertions in the included fragments.
// Keep their C++ identities as well as their field layouts identical across TUs.
namespace vo_alignment_test {
struct Value {
    std::uint32_t keyword;
    std::uint32_t safety;
    bool operator==(const Value&) const = default;
};
}
namespace vo_style_tests {
struct Value {
    std::uint32_t kind;
    float number;
    const void* handle;
    bool operator==(const Value&) const = default;
};
struct Placement {
    std::uint32_t kind;
    std::int32_t index;
    std::uint32_t name_len;
    std::uint8_t name[256];
};
struct Repetition {
    std::uint32_t component, kind, count, tracks, line_name_sets;
    bool operator==(const Repetition&) const = default;
};
}
extern "C" {
struct VoMrOptional { std::uint32_t present; float value; };
struct VoMrDimension { std::uint32_t kind; float value; };
}

namespace {
using namespace taffy;
using AV = vo_alignment_test::Value;
using SV = vo_style_tests::Value;
using SR = vo_style_tests::Repetition;
struct AbiError { std::int32_t status; };
[[noreturn]] void fail(std::int32_t status = 1) { throw AbiError{status}; }
template<class F> std::int32_t guarded(F&& work) noexcept {
    try { work(); return 0; }
    catch (AbiError error) { return error.status; }
    catch (...) { return 3; }
}
template<class T> void check_out(T* out) {
    if (!out || reinterpret_cast<std::uintptr_t>(out) % alignof(T)) fail();
}
template<class T> void store(T* out, T value) { check_out(out); *out = std::move(value); }

// Validate UTF-8 at the ABI boundary, before invoking the production CSS parser.
// This is input validation, not an alternative CSS parser.
std::string_view text(const std::uint8_t* data, std::size_t length) {
    if ((!data && length) || length > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max())) fail();
    for (std::size_t i = 0; i < length;) {
        const auto c = data[i++];
        if (c < 0x80) continue;
        unsigned count; std::uint32_t code, minimum;
        if (c >= 0xc2 && c <= 0xdf) { count = 1; code = c & 0x1f; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { count = 2; code = c & 0xf; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { count = 3; code = c & 7; minimum = 0x10000; }
        else fail();
        if (length - i < count) fail();
        while (count--) {
            const auto next = data[i++];
            if ((next & 0xc0) != 0x80) fail();
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) fail();
    }
    return length ? std::string_view(reinterpret_cast<const char*>(data), length) : std::string_view{};
}
template<class T> T parsed(std::string_view css) {
    auto result = parse_css<T>(css);
    if (!result) fail(2);
    return std::move(*result);
}
AlignmentSafety safety(std::uint32_t code) {
    switch (code) {
    case 0: return AlignmentSafety::Default;
    case 1: return AlignmentSafety::Unsafe;
    case 2: return AlignmentSafety::Safe;
    default: fail();
    }
}
std::uint32_t encode_safety(AlignmentSafety value) {
    switch (value) {
    case AlignmentSafety::Default: return 0;
    case AlignmentSafety::Unsafe: return 1;
    case AlignmentSafety::Safe: return 2;
    }
    fail();
}
AlignItems items(AV value) {
    AlignItemsKeyword keyword;
    switch (value.keyword) {
    case 0: keyword = AlignItemsKeyword::Normal; break;
    case 1: keyword = AlignItemsKeyword::Start; break;
    case 2: keyword = AlignItemsKeyword::End; break;
    case 3: keyword = AlignItemsKeyword::FlexStart; break;
    case 4: keyword = AlignItemsKeyword::FlexEnd; break;
    case 5: keyword = AlignItemsKeyword::SelfStart; break;
    case 6: keyword = AlignItemsKeyword::SelfEnd; break;
    case 7: keyword = AlignItemsKeyword::Center; break;
    case 8: keyword = AlignItemsKeyword::Baseline; break;
    case 9: keyword = AlignItemsKeyword::Stretch; break;
    default: fail();
    }
    return {keyword, safety(value.safety)};
}
AlignContent content(AV value) {
    AlignContentKeyword keyword;
    switch (value.keyword) {
    case 0: keyword = AlignContentKeyword::Normal; break;
    case 1: keyword = AlignContentKeyword::Start; break;
    case 2: keyword = AlignContentKeyword::End; break;
    case 3: keyword = AlignContentKeyword::FlexStart; break;
    case 4: keyword = AlignContentKeyword::FlexEnd; break;
    case 7: keyword = AlignContentKeyword::Center; break;
    case 9: keyword = AlignContentKeyword::Stretch; break;
    case 10: keyword = AlignContentKeyword::SpaceBetween; break;
    case 11: keyword = AlignContentKeyword::SpaceEvenly; break;
    case 12: keyword = AlignContentKeyword::SpaceAround; break;
    default: fail();
    }
    return {keyword, safety(value.safety)};
}
AV encode(AlignItems value) {
    std::uint32_t code;
    switch (value.keyword) {
    case AlignItemsKeyword::Normal: code = 0; break;
    case AlignItemsKeyword::Start: code = 1; break;
    case AlignItemsKeyword::End: code = 2; break;
    case AlignItemsKeyword::FlexStart: code = 3; break;
    case AlignItemsKeyword::FlexEnd: code = 4; break;
    case AlignItemsKeyword::SelfStart: code = 5; break;
    case AlignItemsKeyword::SelfEnd: code = 6; break;
    case AlignItemsKeyword::Center: code = 7; break;
    case AlignItemsKeyword::Baseline: code = 8; break;
    case AlignItemsKeyword::Stretch: code = 9; break;
    default: fail();
    }
    return {code, encode_safety(value.safety)};
}
AV encode(AlignContent value) {
    std::uint32_t code;
    switch (value.keyword) {
    case AlignContentKeyword::Normal: code = 0; break;
    case AlignContentKeyword::Start: code = 1; break;
    case AlignContentKeyword::End: code = 2; break;
    case AlignContentKeyword::FlexStart: code = 3; break;
    case AlignContentKeyword::FlexEnd: code = 4; break;
    case AlignContentKeyword::Center: code = 7; break;
    case AlignContentKeyword::Stretch: code = 9; break;
    case AlignContentKeyword::SpaceBetween: code = 10; break;
    case AlignContentKeyword::SpaceEvenly: code = 11; break;
    case AlignContentKeyword::SpaceAround: code = 12; break;
    default: fail();
    }
    return {code, encode_safety(value.safety)};
}
AlignItems constant_items(std::uint32_t id) {
    switch (id) {
    case 0: return AlignItems::normal();
    case 1: return AlignItems::start();
    case 2: return AlignItems::end();
    case 3: return AlignItems::flex_start();
    case 4: return AlignItems::flex_end();
    case 5: return AlignItems::self_start();
    case 6: return AlignItems::self_end();
    case 7: return AlignItems::center();
    case 8: return AlignItems::baseline();
    case 9: return AlignItems::stretch();
    case 101: return AlignItems::safe_start();
    case 102: return AlignItems::safe_end();
    case 103: return AlignItems::safe_flex_start();
    case 104: return AlignItems::safe_flex_end();
    case 105: return AlignItems::safe_self_start();
    case 106: return AlignItems::safe_self_end();
    case 107: return AlignItems::safe_center();
    default: fail();
    }
}
AlignContent constant_content(std::uint32_t id) {
    switch (id) {
    case 0: return AlignContent::normal();
    case 1: return AlignContent::start();
    case 2: return AlignContent::end();
    case 3: return AlignContent::flex_start();
    case 4: return AlignContent::flex_end();
    case 7: return AlignContent::center();
    case 9: return AlignContent::stretch();
    case 10: return AlignContent::space_between();
    case 11: return AlignContent::space_evenly();
    case 12: return AlignContent::space_around();
    case 101: return AlignContent::safe_start();
    case 102: return AlignContent::safe_end();
    case 103: return AlignContent::safe_flex_start();
    case 104: return AlignContent::safe_flex_end();
    case 107: return AlignContent::safe_center();
    default: fail();
    }
}
bool items_domain(std::uint32_t domain) {
    if (domain < 1 || domain > 4) fail();
    return domain == 1 || domain == 4;
}
std::optional<float> optional(VoMrOptional value) {
    if (value.present > 1) fail();
    return value.present ? std::optional<float>(value.value) : std::nullopt;
}
VoMrOptional encode(std::optional<float> value) { return {value ? 1u : 0u, value.value_or(0)}; }
Dimension dimension(VoMrDimension value) {
    switch (value.kind) {
    case 0: return Dimension::auto_();
    case 1: return Dimension::length(value.value);
    case 2: return Dimension::percent(value.value);
    case 3: return Dimension::from_length(static_cast<double>(value.value));
    case 4: return Dimension::from_percent(static_cast<double>(value.value));
    default: fail();
    }
}
Size<std::optional<float>> optional_size(std::uint32_t constructor, VoMrOptional w, VoMrOptional h) {
    switch (constructor) {
    case 0: return {optional(w), optional(h)};
    case 1: return Size<std::optional<float>>::none();
    case 2: {
        const auto width = optional(w), height = optional(h);
        if (!width || !height) fail();
        return Size<std::optional<float>>::new_(*width, *height);
    }
    default: fail();
    }
}
template<class L, class R> auto math(std::uint32_t op, L lhs, R rhs, R maximum) {
    switch (op) {
    case 0: return maybe_min(lhs, rhs);
    case 1: return maybe_max(lhs, rhs);
    case 2: return maybe_add(lhs, rhs);
    case 3: return maybe_sub(lhs, rhs);
    case 4: return maybe_clamp(lhs, rhs, maximum);
    default: fail();
    }
}
void store_size(VoMrOptional* out, Size<std::optional<float>> value) {
    check_out(out); const std::array values{encode(value.width), encode(value.height)};
    std::copy(values.begin(), values.end(), out);
}
void store_rect(float* out, Rect<float> value) {
    check_out(out); const std::array values{value.left, value.right, value.top, value.bottom};
    std::copy(values.begin(), values.end(), out);
}

// Calc's native identity is uintptr_t. Preserve ALL bits: no Rust tagging,
// low-bit stripping, dereference, allocation, or ownership is implemented here.
// The aligned non-null check is solely the existing wire ABI's precondition.
void check_value(SV input) {
    if ((input.kind == 11 || input.kind == 12) &&
        (!input.handle || (reinterpret_cast<std::uintptr_t>(input.handle) & 7))) fail();
}
template<class T> T construct(SV input) {
    check_value(input);
    const auto n = input.number;
    const Calc calc{reinterpret_cast<std::uintptr_t>(input.handle)};
    switch (input.kind) {
    case 0: return T::length(n);
    case 1: return T::percent(n);
    case 2: if constexpr (requires { T::auto_(); }) return T::auto_(); break;
    case 3: if constexpr (requires { T::min_content(); }) return T::min_content(); break;
    case 4: if constexpr (requires { T::max_content(); }) return T::max_content(); break;
    case 5: if constexpr (requires { T::fit_content_px(n); }) return T::fit_content_px(n); break;
    case 6: if constexpr (requires { T::fit_content_percent(n); }) return T::fit_content_percent(n); break;
    case 7: if constexpr (requires { T::fit_content(); }) return T::fit_content(); break;
    case 8: if constexpr (requires { T::stretch(); }) return T::stretch(); break;
    case 9: if constexpr (requires { T::content(); }) return T::content(); break;
    case 10: if constexpr (requires { T::fr(n); }) return T::fr(n); break;
    case 11: return T::calc(calc);
    case 12: if constexpr (requires { T::fit_content_calc(calc); }) return T::fit_content_calc(calc); break;
    case 13: return T::zero();
    default: break;
    }
    fail(2);
}
template<class T> SV encode_length(T value) {
    std::uint32_t kind;
    switch (value.kind()) {
    case LengthKind::Length: kind = 0; break;
    case LengthKind::Percent: kind = 1; break;
    case LengthKind::Auto: kind = 2; break;
    case LengthKind::MinContent: kind = 3; break;
    case LengthKind::MaxContent: kind = 4; break;
    case LengthKind::FitContentPx: kind = 5; break;
    case LengthKind::FitContentPercent: kind = 6; break;
    case LengthKind::FitContent: kind = 7; break;
    case LengthKind::Stretch: kind = 8; break;
    case LengthKind::Content: kind = 9; break;
    case LengthKind::Fr: kind = 10; break;
    case LengthKind::Calc: kind = 11; break;
    case LengthKind::FitContentCalc: kind = 12; break;
    default: fail();
    }
    if (value.has_calc()) return {kind, 0, reinterpret_cast<const void*>(value.calc_value().handle)};
    const bool numeric = kind == 0 || kind == 1 || kind == 5 || kind == 6 || kind == 10;
    return {kind, numeric ? value.value() : 0, nullptr};
}
template<class T> T native(std::uint32_t operation, SV input) {
    check_value(input);
    if (operation > 3) fail();
    if (operation == 1 && input.kind == 13) fail(2);
    auto value = construct<T>(input);
    // C++ deliberately has no CompactLength/Expanded* representation. Exercise
    // its public checked accessors + factories for semantic conversion selectors;
    // do not claim these selectors validate Rust storage or packing roundtrips.
    if (operation == 1 || operation == 3) return construct<T>(encode_length(value));
    return value;
}
template<class T> T operand(std::uint32_t operation, SV input) {
    if constexpr (std::same_as<T, MaxTrackSizingFunction>) {
        if (operation == 4) return T::fit_content(construct<LengthPercentage>(input));
    }
    if constexpr (std::same_as<T, MinTrackSizingFunction>) {
        if (operation == 5) return T(construct<Dimension>(input));
        if (operation == 6) return T(construct<MaxTrackSizingFunction>(input));
    }
    return native<T>(operation, input);
}
SR encode_count(RepetitionCount count) {
    switch (count.kind()) {
    case RepetitionCount::Kind::Count: return {0, 0, count.value(), 0, 0};
    case RepetitionCount::Kind::AutoFit: return {0, 1, 0, 0, 0};
    case RepetitionCount::Kind::AutoFill: return {0, 2, 0, 0, 0};
    }
    fail();
}
SR encode_component(const GridTemplateComponent& component) {
    if (auto r = std::get_if<GridTemplateRepetition>(&component.value)) {
        auto result = encode_count(r->count);
        result.component = 1;
        result.tracks = static_cast<std::uint32_t>(r->tracks.size());
        result.line_name_sets = static_cast<std::uint32_t>(r->line_names.size());
        return result;
    }
    return {2, 0, 0, 1, 0};
}
}

extern "C" std::int32_t vo_alignment_constant(std::uint32_t domain, std::uint32_t id, AV* out) {
    return guarded([&] {
        if (items_domain(domain)) store(out, encode(constant_items(id)));
        else { if (domain == 3 && id != 0) fail(); store(out, encode(constant_content(id))); }
    });
}
extern "C" std::int32_t vo_alignment_default(std::uint32_t domain, AV* out) {
    return guarded([&] { store(out, items_domain(domain) ? encode(AlignItems{}) : encode(AlignContent{})); });
}
extern "C" std::int32_t vo_alignment_default_component(std::uint32_t kind, std::uint32_t* out) {
    return guarded([&] {
        if (kind == 1) store(out, encode(AlignItems{}).keyword);
        else if (kind == 2) store(out, encode_safety(AlignItems{}.safety));
        else fail();
    });
}
extern "C" std::int32_t vo_alignment_is_safe(std::uint32_t domain, AV value, std::uint32_t* out) {
    return guarded([&] { store(out, std::uint32_t(items_domain(domain) ? items(value).is_safe() : content(value).is_safe())); });
}
extern "C" std::int32_t vo_alignment_keyword(std::uint32_t domain, AV value, std::uint32_t* out) {
    return guarded([&] { store(out, (items_domain(domain) ? encode(items(value)) : encode(content(value))).keyword); });
}
extern "C" std::int32_t vo_alignment_resolve_self_relative(AV value, std::uint32_t item, std::uint32_t container, std::uint32_t inline_axis, AV* out) {
    return guarded([&] {
        if (item > 1 || container > 1 || inline_axis > 1) fail();
        store(out, encode(items(value).resolve_self_relative(item ? Direction::Rtl : Direction::Ltr,
            container ? Direction::Rtl : Direction::Ltr, inline_axis != 0)));
    });
}
extern "C" std::int32_t vo_alignment_parse(std::uint32_t domain, const std::uint8_t* data, std::size_t length, AV* out) {
    return guarded([&] {
        check_out(out); const auto css = text(data, length);
        store(out, items_domain(domain) ? encode(parsed<AlignItems>(css)) : encode(parsed<AlignContent>(css)));
    });
}
extern "C" std::int32_t vo_contain_parse(const std::uint8_t* data, std::size_t length, std::uint32_t* out) {
    return guarded([&] {
        check_out(out); const auto value = parsed<Contain>(text(data, length));
        store(out, (value.contains(Contain::layout()) ? 1u : 0u) | (value.contains(Contain::paint()) ? 2u : 0u));
    });
}
extern "C" std::int32_t vo_math_scalar(std::uint32_t op, std::uint32_t flavor, VoMrOptional lhs, VoMrOptional rhs, VoMrOptional maximum, VoMrOptional* out) {
    return guarded([&] {
        check_out(out);
        const auto l = optional(lhs), r = optional(rhs), m = op == 4 ? optional(maximum) : std::nullopt;
        switch (flavor) {
        case 0: store(out, encode(math(op, l, r, m))); break;
        case 1:
            if (!r || (op == 4 && !m)) fail();
            store(out, encode(math(op, l, *r, m.value_or(0)))); break;
        case 2:
            if (!l) fail();
            store(out, encode(math(op, *l, r, m))); break;
        default: fail();
        }
    });
}
extern "C" std::int32_t vo_resolve_dimension(std::uint32_t mode, VoMrDimension input, VoMrOptional context, VoMrOptional* out) {
    return guarded([&] {
        check_out(out); const auto d = dimension(input); const auto c = optional(context);
        if (mode == 0) store(out, encode(d.maybe_resolve(c)));
        else if (mode == 1) store(out, encode(d.resolve_or_zero(c)));
        else fail();
    });
}
extern "C" std::int32_t vo_construct_optional_size(std::uint32_t constructor, VoMrOptional w, VoMrOptional h, VoMrOptional* out) {
    return guarded([&] { check_out(out); store_size(out, optional_size(constructor, w, h)); });
}
extern "C" std::int32_t vo_construct_rect(std::uint32_t constructor, float l, float r, float t, float b, float* out) {
    return guarded([&] {
        check_out(out);
        if (constructor == 0) store_rect(out, Rect<float>::new_(l, r, t, b));
        else if (constructor == 1) store_rect(out, Rect<float>::zero());
        else fail();
    });
}
extern "C" std::int32_t vo_resolve_size_op(std::uint32_t mode, std::uint32_t constructor, std::uint32_t context_constructor, VoMrDimension w, VoMrDimension h, VoMrOptional cw, VoMrOptional ch, VoMrOptional* out) {
    return guarded([&] {
        check_out(out); Size<Dimension> input;
        switch (constructor) {
        case 0: input = {dimension(w), dimension(h)}; break;
        case 1: input = Size<Dimension>::auto_(); break;
        case 2: input = Size<Dimension>::from_lengths(w.value, h.value); break;
        case 3: input = Size<Dimension>::from_percent(w.value, h.value); break;
        default: fail();
        }
        const auto context = optional_size(context_constructor, cw, ch);
        if (mode == 0) store_size(out, input.maybe_resolve(context));
        else if (mode == 1) store_size(out, input.resolve_or_zero(context).map([](float v) { return std::optional<float>(v); }));
        else fail();
    });
}
extern "C" std::int32_t vo_resolve_rect_op(std::uint32_t context_kind, std::uint32_t constructor, std::uint32_t context_constructor, VoMrDimension l, VoMrDimension r, VoMrDimension t, VoMrDimension b, VoMrOptional cw, VoMrOptional ch, float* out) {
    return guarded([&] {
        check_out(out); Rect<Dimension> input;
        switch (constructor) {
        case 0: input = {dimension(l), dimension(r), dimension(t), dimension(b)}; break;
        case 1: input = Rect<Dimension>::auto_(); break;
        case 2: input = Rect<Dimension>::from_length(l.value, r.value, t.value, b.value); break;
        case 3: input = Rect<Dimension>::from_percent(l.value, r.value, t.value, b.value); break;
        default: fail();
        }
        if (context_kind == 0) store_rect(out, input.resolve_or_zero(optional_size(context_constructor, cw, ch)));
        else if (context_kind == 1 && context_constructor == 0) store_rect(out, input.resolve_or_zero(optional(cw)));
        else fail();
    });
}
extern "C" std::int32_t vo_style_value(std::uint32_t family, std::uint32_t operation, SV input, SV* out) {
    return guarded([&] {
        switch (family) {
        case 0: store(out, encode_length(native<LengthPercentage>(operation, input))); break;
        case 1: store(out, encode_length(native<LengthPercentageAuto>(operation, input))); break;
        case 2: store(out, encode_length(native<Dimension>(operation, input))); break;
        case 3: store(out, encode_length(native<MaxTrackSizingFunction>(operation, input))); break;
        case 4: store(out, encode_length(native<MinTrackSizingFunction>(operation, input))); break;
        default: fail();
        }
    });
}
extern "C" std::int32_t vo_style_equal(std::uint32_t family, std::uint32_t lop, SV left, std::uint32_t rop, SV right, bool* out) {
    return guarded([&] {
        switch (family) {
        case 0: store(out, operand<LengthPercentage>(lop, left) == operand<LengthPercentage>(rop, right)); break;
        case 1: store(out, operand<LengthPercentageAuto>(lop, left) == operand<LengthPercentageAuto>(rop, right)); break;
        case 2: store(out, operand<Dimension>(lop, left) == operand<Dimension>(rop, right)); break;
        case 3: store(out, operand<MaxTrackSizingFunction>(lop, left) == operand<MaxTrackSizingFunction>(rop, right)); break;
        case 4: store(out, operand<MinTrackSizingFunction>(lop, left) == operand<MinTrackSizingFunction>(rop, right)); break;
        default: fail();
        }
    });
}
extern "C" std::int32_t vo_style_to_min(std::uint32_t source, SV input, SV* out) {
    return guarded([&] {
        if (source == 2) store(out, encode_length(MinTrackSizingFunction(construct<Dimension>(input))));
        else if (source == 3) store(out, encode_length(MinTrackSizingFunction(construct<MaxTrackSizingFunction>(input))));
        else fail();
    });
}
extern "C" std::int32_t vo_style_fit_content(SV input, SV* out) {
    return guarded([&] { store(out, encode_length(MaxTrackSizingFunction::fit_content(construct<LengthPercentage>(input)))); });
}
extern "C" std::int32_t vo_style_parse_value(std::uint32_t family, const std::uint8_t* data, std::size_t length, SV* out) {
    return guarded([&] {
        const auto css = text(data, length);
        switch (family) {
        case 0: store(out, encode_length(parsed<LengthPercentage>(css))); break;
        case 1: store(out, encode_length(parsed<LengthPercentageAuto>(css))); break;
        case 2: store(out, encode_length(parsed<Dimension>(css))); break;
        case 3: store(out, encode_length(parsed<MaxTrackSizingFunction>(css))); break;
        case 4: store(out, encode_length(parsed<MinTrackSizingFunction>(css))); break;
        default: fail();
        }
    });
}
extern "C" std::int32_t vo_style_parse_placement(const std::uint8_t* data, std::size_t length, vo_style_tests::Placement* out) {
    return guarded([&] {
        const auto value = parsed<GridPlacement>(text(data, length));
        vo_style_tests::Placement result{};
        switch (value.kind()) {
        case GridPlacement::Kind::Auto: result.kind = 0; break;
        case GridPlacement::Kind::Line: result.kind = 1; result.index = value.line_index(); break;
        case GridPlacement::Kind::Span: result.kind = 2; result.index = value.span_count(); break;
        case GridPlacement::Kind::NamedLine: result.kind = 3; result.index = value.line_index(); break;
        case GridPlacement::Kind::NamedSpan: result.kind = 4; result.index = value.span_count(); break;
        }
        if (value.is_named()) {
            if (value.name().size() > sizeof result.name) fail();
            result.name_len = static_cast<std::uint32_t>(value.name().size());
            std::copy(value.name().begin(), value.name().end(), result.name);
        }
        store(out, result);
    });
}
extern "C" std::int32_t vo_style_parse_repetition(std::uint32_t mode, const std::uint8_t* data, std::size_t length, SR* out) {
    return guarded([&] {
        const auto css = text(data, length);
        if (mode == 0) store(out, encode_count(parsed<RepetitionCount>(css)));
        else if (mode == 1) store(out, encode_component(parsed<GridTemplateComponent>(css)));
        else fail();
    });
}
extern "C" std::int32_t vo_style_repeat(std::uint32_t mode, std::uint16_t count, const std::uint8_t* data, std::size_t length, SR* out) {
    return guarded([&] {
        RepetitionCount repetitions;
        if (mode == 0) repetitions = RepetitionCount::count(count);
        else if (mode == 1) {
            const auto result = parse_css<RepetitionCount>(text(data, length));
            // This ABI models the upstream repeat(string) unwrap boundary, not
            // ordinary CSS parse failure. No exception escapes the C boundary.
            if (!result) fail(3);
            repetitions = *result;
        } else fail();
        store(out, encode_component(repeat(repetitions, {})));
    });
}
extern "C" std::int32_t vo_style_track_count(std::uint32_t n, std::uint16_t* out) {
    return guarded([&] {
        if (n > 1'000'000) fail();
        GridTemplateRepetition repetition{RepetitionCount::count(1), std::vector<TrackSizingFunction>(n, TrackSizingFunction::auto_()), {}};
        store(out, repetition.track_count());
    });
}
