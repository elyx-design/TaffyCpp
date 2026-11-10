// MIT: tests translated from Taffy 0.15.1, commit
// 0064bd54cbe38bf1d8bcd6ec6ab0704d45fb048a, src/util/{math,resolve}.rs.
// Included after Catch2 macros and tests/support/value_oracle.h.
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

extern "C" {
struct VoMrOptional { std::uint32_t present; float value; };
struct VoMrDimension { std::uint32_t kind; float value; };
std::int32_t vo_math_scalar(std::uint32_t, std::uint32_t, VoMrOptional, VoMrOptional, VoMrOptional, VoMrOptional*);
std::int32_t vo_resolve_dimension(std::uint32_t, VoMrDimension, VoMrOptional, VoMrOptional*);
std::int32_t vo_resolve_size_op(std::uint32_t, std::uint32_t, std::uint32_t, VoMrDimension, VoMrDimension, VoMrOptional, VoMrOptional, VoMrOptional*);
std::int32_t vo_resolve_rect_op(std::uint32_t, std::uint32_t, std::uint32_t, VoMrDimension, VoMrDimension, VoMrDimension, VoMrDimension,
    VoMrOptional, VoMrOptional, float*);
std::int32_t vo_construct_optional_size(std::uint32_t, VoMrOptional, VoMrOptional, VoMrOptional*);
std::int32_t vo_construct_rect(std::uint32_t, float, float, float, float, float*);
}
// Component-only adapters are for supplemental/invalid-input checks, not source cases.
inline std::int32_t vo_resolve_size(std::uint32_t mode, VoMrDimension w, VoMrDimension h,
    VoMrOptional cw, VoMrOptional ch, VoMrOptional* out) {
    return vo_resolve_size_op(mode, 0, 0, w, h, cw, ch, out);
}
inline std::int32_t vo_resolve_rect(std::uint32_t context, VoMrDimension l, VoMrDimension r,
    VoMrDimension t, VoMrDimension b, VoMrOptional cw, VoMrOptional ch, float* out) {
    return vo_resolve_rect_op(context, 0, 0, l, r, t, b, cw, ch, out);
}

namespace math_resolve_tests {
using Optional = std::optional<float>;
using Pair = std::array<Optional, 2>;
using Edges = std::array<float, 4>;
using Composite = value_oracle::DimensionComposite;
using SizeConstructor = value_oracle::OptionalSizeConstructor;
using RectConstructor = value_oracle::FloatRectConstructor;
template<class T> constexpr std::uint32_t tag(T op) { return static_cast<std::uint32_t>(op); }
inline VoMrOptional wire(Optional v) { return {v ? 1u : 0u, v.value_or(0.0f)}; }
inline Optional value(VoMrOptional v) {
    REQUIRE(v.present <= 1u);
    return v.present ? Optional{v.value} : std::nullopt;
}
inline Optional calculate(std::uint32_t op, std::uint32_t flavor, Optional lhs, Optional rhs,
                          Optional max = std::nullopt) {
    VoMrOptional result{};
    REQUIRE(vo_math_scalar(op, flavor, wire(lhs), wire(rhs), wire(max), &result) == 0);
    return value(result);
}
inline Optional resolve(std::uint32_t mode, VoMrDimension input, Optional context) {
    VoMrOptional result{};
    REQUIRE(vo_resolve_dimension(mode, input, wire(context), &result) == 0);
    return value(result);
}
inline Pair resolve_size(VoMrDimension width, VoMrDimension height, Optional cw, Optional ch,
                         Composite constructor = Composite::components,
                         SizeConstructor context = SizeConstructor::components) {
    std::array<VoMrOptional, 2> result{};
    REQUIRE(vo_resolve_size_op(0, tag(constructor), tag(context), width, height, wire(cw), wire(ch), result.data()) == 0);
    return {value(result[0]), value(result[1])};
}
inline Edges resolve_rect(std::uint32_t context_kind, VoMrDimension input, Optional cw, Optional ch,
                          Composite constructor, SizeConstructor context) {
    Edges result{};
    REQUIRE(vo_resolve_rect_op(context_kind, tag(constructor), tag(context), input, input, input, input, wire(cw), wire(ch), result.data()) == 0);
    return result;
}
// These helpers preserve four upstream calls, contexts, and exact expected
// values. Size/Rect results are compared as entire domain values, not per-edge
// expansions presented as additional upstream assertions.
inline void scalar_cases(std::uint32_t mode, VoMrDimension input, const std::array<Optional, 4>& expected,
                         float last_context = 0.0f) {
    const std::array<Optional, 4> contexts{std::nullopt, 5.0f, -5.0f, last_context};
    for (std::size_t i = 0; i < contexts.size(); ++i) {
        INFO("upstream call " << i + 1);
        CHECK(resolve(mode, input, contexts[i]) == expected[i]);
    }
}
inline void size_cases(Composite constructor, float payload, const std::array<Optional, 4>& expected) {
    const VoMrDimension input{0, payload};
    const std::array<Optional, 4> contexts{std::nullopt, 5.0f, -5.0f, 0.0f};
    for (std::size_t i = 0; i < contexts.size(); ++i) {
        INFO("upstream call " << i + 1);
        const Pair literal{expected[i], expected[i]};
        std::array<VoMrOptional, 2> constructed{};
        REQUIRE(vo_construct_optional_size(tag(expected[i] ? SizeConstructor::new_value : SizeConstructor::none),
            wire(expected[i]), wire(expected[i]), constructed.data()) == 0);
        const Pair wanted{value(constructed[0]), value(constructed[1])};
        CHECK(wanted == literal); // independent fixed C++ expectation, not oracle-generated
        const Pair actual = resolve_size(input, input, contexts[i], contexts[i], constructor,
            i == 0 ? SizeConstructor::none : SizeConstructor::new_value);
        INFO("height: present=" << actual[1].has_value() << " value=" << actual[1].value_or(0.0f)
             << "; expected: present=" << wanted[1].has_value() << " value=" << wanted[1].value_or(0.0f));
        CHECK(actual == wanted);
    }
}
inline void rect_cases(std::uint32_t context_kind, Composite constructor, float payload, const std::array<float, 4>& expected) {
    const VoMrDimension input{0, payload};
    const std::array<Optional, 4> contexts{std::nullopt, 5.0f, -5.0f, 0.0f};
    for (std::size_t i = 0; i < contexts.size(); ++i) {
        INFO("upstream call " << i + 1);
        const Edges literal{expected[i], expected[i], expected[i], expected[i]};
        Edges wanted{};
        REQUIRE(vo_construct_rect(tag(expected[i] == 0.0f ? RectConstructor::zero : RectConstructor::new_value),
            expected[i], expected[i], expected[i], expected[i], wanted.data()) == 0);
        CHECK(wanted == literal); // retain independent expected values
        CHECK(resolve_rect(context_kind, input, contexts[i], contexts[i], constructor,
            context_kind == 1 ? SizeConstructor::components :
            (i == 0 ? SizeConstructor::none : SizeConstructor::new_value)) == wanted);
    }
}
} // namespace math_resolve_tests

TEST_CASE("src/util/math.rs::util::math::tests::lhs_option_f32_rhs_option_f32::test_maybe_min", "[value][math]") {
    using namespace math_resolve_tests;
    CHECK(calculate(0, 0, 3.0f, 5.0f) == Optional{3.0f});
    CHECK(calculate(0, 0, 5.0f, 3.0f) == Optional{3.0f});
    CHECK(calculate(0, 0, 3.0f, std::nullopt) == Optional{3.0f});
    CHECK(calculate(0, 0, std::nullopt, 3.0f) == std::nullopt);
    CHECK(calculate(0, 0, std::nullopt, std::nullopt) == std::nullopt);
}
TEST_CASE("src/util/math.rs::util::math::tests::lhs_option_f32_rhs_option_f32::test_maybe_max", "[value][math]") {
    using namespace math_resolve_tests;
    CHECK(calculate(1, 0, 3.0f, 5.0f) == Optional{5.0f});
    CHECK(calculate(1, 0, 5.0f, 3.0f) == Optional{5.0f});
    CHECK(calculate(1, 0, 3.0f, std::nullopt) == Optional{3.0f});
    CHECK(calculate(1, 0, std::nullopt, 3.0f) == std::nullopt);
    CHECK(calculate(1, 0, std::nullopt, std::nullopt) == std::nullopt);
}
TEST_CASE("src/util/math.rs::util::math::tests::lhs_option_f32_rhs_option_f32::test_maybe_add", "[value][math]") {
    using namespace math_resolve_tests;
    CHECK(calculate(2, 0, 3.0f, 5.0f) == Optional{8.0f});
    CHECK(calculate(2, 0, 5.0f, 3.0f) == Optional{8.0f});
    CHECK(calculate(2, 0, 3.0f, std::nullopt) == Optional{3.0f});
    CHECK(calculate(2, 0, std::nullopt, 3.0f) == std::nullopt);
    CHECK(calculate(2, 0, std::nullopt, std::nullopt) == std::nullopt);
}
TEST_CASE("src/util/math.rs::util::math::tests::lhs_option_f32_rhs_option_f32::test_maybe_sub", "[value][math]") {
    using namespace math_resolve_tests;
    CHECK(calculate(3, 0, 3.0f, 5.0f) == Optional{-2.0f});
    CHECK(calculate(3, 0, 5.0f, 3.0f) == Optional{2.0f});
    CHECK(calculate(3, 0, 3.0f, std::nullopt) == Optional{3.0f});
    CHECK(calculate(3, 0, std::nullopt, 3.0f) == std::nullopt);
    CHECK(calculate(3, 0, std::nullopt, std::nullopt) == std::nullopt);
}
TEST_CASE("src/util/math.rs::util::math::tests::lhs_option_f32_rhs_f32::test_maybe_min", "[value][math]") {
    using namespace math_resolve_tests;
    CHECK(calculate(0, 1, 3.0f, 5.0f) == Optional{3.0f});
    CHECK(calculate(0, 1, 5.0f, 3.0f) == Optional{3.0f});
    CHECK(calculate(0, 1, std::nullopt, 3.0f) == std::nullopt);
}
TEST_CASE("src/util/math.rs::util::math::tests::lhs_option_f32_rhs_f32::test_maybe_max", "[value][math]") {
    using namespace math_resolve_tests;
    CHECK(calculate(1, 1, 3.0f, 5.0f) == Optional{5.0f});
    CHECK(calculate(1, 1, 5.0f, 3.0f) == Optional{5.0f});
    CHECK(calculate(1, 1, std::nullopt, 3.0f) == std::nullopt);
}
TEST_CASE("src/util/math.rs::util::math::tests::lhs_option_f32_rhs_f32::test_maybe_add", "[value][math]") {
    using namespace math_resolve_tests;
    CHECK(calculate(2, 1, 3.0f, 5.0f) == Optional{8.0f});
    CHECK(calculate(2, 1, 5.0f, 3.0f) == Optional{8.0f});
    CHECK(calculate(2, 1, std::nullopt, 3.0f) == std::nullopt);
}
TEST_CASE("src/util/math.rs::util::math::tests::lhs_option_f32_rhs_f32::test_maybe_sub", "[value][math]") {
    using namespace math_resolve_tests;
    CHECK(calculate(3, 1, 3.0f, 5.0f) == Optional{-2.0f});
    CHECK(calculate(3, 1, 5.0f, 3.0f) == Optional{2.0f});
    CHECK(calculate(3, 1, std::nullopt, 3.0f) == std::nullopt);
}
TEST_CASE("src/util/math.rs::util::math::tests::lhs_f32_rhs_option_f32::test_maybe_min", "[value][math]") {
    using namespace math_resolve_tests;
    CHECK(calculate(0, 2, 3.0f, 5.0f) == Optional{3.0f});
    CHECK(calculate(0, 2, 5.0f, 3.0f) == Optional{3.0f});
    CHECK(calculate(0, 2, 3.0f, std::nullopt) == Optional{3.0f});
}
TEST_CASE("src/util/math.rs::util::math::tests::lhs_f32_rhs_option_f32::test_maybe_max", "[value][math]") {
    using namespace math_resolve_tests;
    CHECK(calculate(1, 2, 3.0f, 5.0f) == Optional{5.0f});
    CHECK(calculate(1, 2, 5.0f, 3.0f) == Optional{5.0f});
    CHECK(calculate(1, 2, 3.0f, std::nullopt) == Optional{3.0f});
}
TEST_CASE("src/util/math.rs::util::math::tests::lhs_f32_rhs_option_f32::test_maybe_add", "[value][math]") {
    using namespace math_resolve_tests;
    CHECK(calculate(2, 2, 3.0f, 5.0f) == Optional{8.0f});
    CHECK(calculate(2, 2, 5.0f, 3.0f) == Optional{8.0f});
    CHECK(calculate(2, 2, 3.0f, std::nullopt) == Optional{3.0f});
}
TEST_CASE("src/util/math.rs::util::math::tests::lhs_f32_rhs_option_f32::test_maybe_sub", "[value][math]") {
    using namespace math_resolve_tests;
    CHECK(calculate(3, 2, 3.0f, 5.0f) == Optional{-2.0f});
    CHECK(calculate(3, 2, 5.0f, 3.0f) == Optional{2.0f});
    CHECK(calculate(3, 2, 3.0f, std::nullopt) == Optional{3.0f});
}

TEST_CASE("src/util/resolve.rs::util::resolve::tests::maybe_resolve_dimension::resolve_auto", "[value][resolve]") {
    math_resolve_tests::scalar_cases(0, {0, 0}, {std::nullopt, std::nullopt, std::nullopt, std::nullopt});
}
TEST_CASE("src/util/resolve.rs::util::resolve::tests::maybe_resolve_dimension::resolve_length", "[value][resolve]") {
    math_resolve_tests::scalar_cases(0, {3, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
}
TEST_CASE("src/util/resolve.rs::util::resolve::tests::maybe_resolve_dimension::resolve_percent", "[value][resolve]") {
    math_resolve_tests::scalar_cases(0, {4, 1.0f}, {std::nullopt, 5.0f, -5.0f, 50.0f}, 50.0f);
}
TEST_CASE("src/util/resolve.rs::util::resolve::tests::maybe_resolve_size_dimension::maybe_resolve_auto", "[value][resolve]") {
    math_resolve_tests::size_cases(math_resolve_tests::Composite::auto_value, 0, {std::nullopt, std::nullopt, std::nullopt, std::nullopt});
}
TEST_CASE("src/util/resolve.rs::util::resolve::tests::maybe_resolve_size_dimension::maybe_resolve_length", "[value][resolve]") {
    math_resolve_tests::size_cases(math_resolve_tests::Composite::lengths, 5.0f, {5.0f, 5.0f, 5.0f, 5.0f});
}
TEST_CASE("src/util/resolve.rs::util::resolve::tests::maybe_resolve_size_dimension::maybe_resolve_percent", "[value][resolve]") {
    math_resolve_tests::size_cases(math_resolve_tests::Composite::percents, 5.0f, {std::nullopt, 25.0f, -25.0f, 0.0f});
}
TEST_CASE("src/util/resolve.rs::util::resolve::tests::resolve_or_zero_dimension_to_option_f32::resolve_or_zero_auto", "[value][resolve]") {
    math_resolve_tests::scalar_cases(1, {0, 0}, {0.0f, 0.0f, 0.0f, 0.0f});
}
TEST_CASE("src/util/resolve.rs::util::resolve::tests::resolve_or_zero_dimension_to_option_f32::resolve_or_zero_length", "[value][resolve]") {
    math_resolve_tests::scalar_cases(1, {3, 5.0f}, {5.0f, 5.0f, 5.0f, 5.0f});
}
TEST_CASE("src/util/resolve.rs::util::resolve::tests::resolve_or_zero_dimension_to_option_f32::resolve_or_zero_percent", "[value][resolve]") {
    math_resolve_tests::scalar_cases(1, {4, 5.0f}, {0.0f, 25.0f, -25.0f, 0.0f});
}
TEST_CASE("src/util/resolve.rs::util::resolve::tests::resolve_or_zero_rect_dimension_to_rect::resolve_or_zero_auto", "[value][resolve]") {
    math_resolve_tests::rect_cases(0, math_resolve_tests::Composite::auto_value, 0, {0.0f, 0.0f, 0.0f, 0.0f});
}
TEST_CASE("src/util/resolve.rs::util::resolve::tests::resolve_or_zero_rect_dimension_to_rect::resolve_or_zero_length", "[value][resolve]") {
    math_resolve_tests::rect_cases(0, math_resolve_tests::Composite::lengths, 5.0f, {5.0f, 5.0f, 5.0f, 5.0f});
}
TEST_CASE("src/util/resolve.rs::util::resolve::tests::resolve_or_zero_rect_dimension_to_rect::resolve_or_zero_percent", "[value][resolve]") {
    math_resolve_tests::rect_cases(0, math_resolve_tests::Composite::percents, 5.0f, {0.0f, 25.0f, -25.0f, 0.0f});
}
TEST_CASE("src/util/resolve.rs::util::resolve::tests::resolve_or_zero_rect_dimension_to_rect_f32_via_option::resolve_or_zero_auto", "[value][resolve]") {
    math_resolve_tests::rect_cases(1, math_resolve_tests::Composite::auto_value, 0, {0.0f, 0.0f, 0.0f, 0.0f});
}
TEST_CASE("src/util/resolve.rs::util::resolve::tests::resolve_or_zero_rect_dimension_to_rect_f32_via_option::resolve_or_zero_length", "[value][resolve]") {
    math_resolve_tests::rect_cases(1, math_resolve_tests::Composite::lengths, 5.0f, {5.0f, 5.0f, 5.0f, 5.0f});
}
TEST_CASE("src/util/resolve.rs::util::resolve::tests::resolve_or_zero_rect_dimension_to_rect_f32_via_option::resolve_or_zero_percent", "[value][resolve]") {
    math_resolve_tests::rect_cases(1, math_resolve_tests::Composite::percents, 5.0f, {0.0f, 25.0f, -25.0f, 0.0f});
}

TEST_CASE("supplemental::math_resolve::nan_semantics", "[value][math][resolve][supplemental]") {
    using namespace math_resolve_tests;
    const float nan = std::bit_cast<float>(std::uint32_t{0x7fc12345});
    // Rust min/max ignore one NaN, unlike std::min/std::max's ordering rules.
    for (std::uint32_t flavor = 0; flavor < 3; ++flavor) {
        CHECK(calculate(0, flavor, nan, 3.0f) == Optional{3.0f});
        CHECK(calculate(0, flavor, 3.0f, nan) == Optional{3.0f});
        CHECK(calculate(1, flavor, nan, 3.0f) == Optional{3.0f});
        CHECK(calculate(1, flavor, 3.0f, nan) == Optional{3.0f});
        const auto sum = calculate(2, flavor, nan, 3.0f);
        REQUIRE(sum.has_value());
        CHECK(std::isnan(*sum));
        const auto both_nan = calculate(0, flavor, nan, nan);
        REQUIRE(both_nan.has_value());
        CHECK(std::isnan(*both_nan));
    }
    CHECK(calculate(0, 0, std::nullopt, nan) == std::nullopt);
    const auto unchanged = calculate(0, 0, nan, std::nullopt);
    REQUIRE(unchanged.has_value());
    CHECK(std::isnan(*unchanged));
    CHECK(std::bit_cast<std::uint32_t>(*unchanged) == std::uint32_t{0x7fc12345});
    const auto length = resolve(0, {1, nan}, std::nullopt);
    REQUIRE(length.has_value());
    CHECK(std::isnan(*length));
    CHECK(std::bit_cast<std::uint32_t>(*length) == std::uint32_t{0x7fc12345});
    CHECK(resolve(0, {2, nan}, std::nullopt) == std::nullopt);
    const auto percent = resolve(1, {2, nan}, 5.0f);
    REQUIRE(percent.has_value());
    CHECK(std::isnan(*percent));
}
TEST_CASE("supplemental::math_resolve::clamp_and_axis_routing", "[value][math][resolve][supplemental]") {
    using namespace math_resolve_tests;
    for (std::uint32_t flavor = 0; flavor < 3; ++flavor) {
        CHECK(calculate(4, flavor, 3.0f, 5.0f, 10.0f) == Optional{5.0f});
        CHECK(calculate(4, flavor, 12.0f, 5.0f, 10.0f) == Optional{10.0f});
        // Taffy computes min(maximum).max(minimum), even for reversed bounds.
        CHECK(calculate(4, flavor, 3.0f, 10.0f, 5.0f) == Optional{10.0f});
    }
    CHECK(calculate(4, 0, std::nullopt, 5.0f, 10.0f) == std::nullopt);
    CHECK(calculate(4, 0, 3.0f, std::nullopt, std::nullopt) == Optional{3.0f});
    const Pair wanted_size{10.0f, 21.0f};
    CHECK(resolve_size({2, 2.0f}, {2, 3.0f}, 5.0f, 7.0f) == wanted_size);
    Edges rect{};
    REQUIRE(vo_resolve_rect(0, {2, 2}, {2, 3}, {2, 4}, {2, 5}, wire(7.0f), wire(11.0f), rect.data()) == 0);
    const Edges wanted_rect{14.0f, 21.0f, 44.0f, 55.0f};
    CHECK(rect == wanted_rect);
    // Exercise constructor operands independently: uniform upstream values alone
    // cannot reveal swapped axes/edges in a new operation selector.
    CHECK(resolve_size({0, 2}, {0, 3}, 5.0f, 7.0f, Composite::percents, SizeConstructor::new_value) == wanted_size);
    REQUIRE(vo_resolve_rect_op(0, tag(Composite::percents), tag(SizeConstructor::new_value),
        {0, 2}, {0, 3}, {0, 4}, {0, 5}, wire(7.0f), wire(11.0f), rect.data()) == 0);
    CHECK(rect == wanted_rect);
    REQUIRE(vo_resolve_rect_op(1, tag(Composite::lengths), tag(SizeConstructor::components),
        {0, 2}, {0, 3}, {0, 4}, {0, 5}, wire(std::nullopt), wire(std::nullopt), rect.data()) == 0);
    const Edges length_edges{2, 3, 4, 5};
    CHECK(rect == length_edges);
    REQUIRE(vo_construct_rect(tag(RectConstructor::new_value), 2, 3, 4, 5, rect.data()) == 0);
    CHECK(rect == length_edges);
}
TEST_CASE("supplemental::math_resolve::invalid_input", "[value][math][resolve][supplemental]") {
    VoMrOptional result{1, 123.0f};
    CHECK(vo_math_scalar(99, 0, {1, 3}, {1, 5}, {0, 0}, &result) == 1);
    CHECK(result.value == 123.0f);
    CHECK(vo_math_scalar(0, 99, {1, 3}, {1, 5}, {0, 0}, &result) == 1);
    CHECK(vo_math_scalar(0, 0, {2, 3}, {1, 5}, {0, 0}, &result) == 1);
    CHECK(vo_math_scalar(0, 1, {1, 3}, {0, 0}, {0, 0}, &result) == 1);
    CHECK(vo_math_scalar(0, 0, {1, 3}, {1, 5}, {0, 0}, nullptr) == 1);
    CHECK(vo_resolve_dimension(0, {99, 0}, {0, 0}, &result) == 1);
    CHECK(vo_resolve_dimension(99, {0, 0}, {0, 0}, &result) == 1);
    CHECK(vo_resolve_dimension(0, {0, 0}, {2, 0}, &result) == 1);
    CHECK(vo_resolve_dimension(0, {0, 0}, {0, 0}, nullptr) == 1);
    std::array<VoMrOptional, 2> size{};
    CHECK(vo_resolve_size(0, {99, 0}, {0, 0}, {0, 0}, {0, 0}, size.data()) == 1);
    CHECK(vo_resolve_size(99, {0, 0}, {0, 0}, {0, 0}, {0, 0}, size.data()) == 1);
    CHECK(vo_resolve_size(0, {0, 0}, {0, 0}, {0, 0}, {2, 0}, size.data()) == 1);
    CHECK(vo_resolve_size(0, {0, 0}, {0, 0}, {0, 0}, {0, 0}, nullptr) == 1);
    std::array<float, 4> rect{};
    CHECK(vo_resolve_rect(0, {0, 0}, {0, 0}, {99, 0}, {0, 0}, {0, 0}, {0, 0}, rect.data()) == 1);
    CHECK(vo_resolve_rect(99, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, rect.data()) == 1);
    CHECK(vo_resolve_rect(0, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 0}, rect.data()) == 1);
    CHECK(vo_resolve_rect(0, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, nullptr) == 1);
    CHECK(vo_resolve_size_op(0, 99, 0, {0, 0}, {0, 0}, {0, 0}, {0, 0}, size.data()) == 1);
    CHECK(vo_resolve_size_op(0, 1, 99, {0, 0}, {0, 0}, {0, 0}, {0, 0}, size.data()) == 1);
    CHECK(vo_resolve_rect_op(0, 99, 0, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, rect.data()) == 1);
    CHECK(vo_resolve_rect_op(0, 1, 99, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, rect.data()) == 1);
    CHECK(vo_resolve_rect_op(1, 1, 1, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, rect.data()) == 1);
    CHECK(vo_construct_optional_size(99, {0, 0}, {0, 0}, size.data()) == 1);
    CHECK(vo_construct_optional_size(2, {0, 0}, {1, 2}, size.data()) == 1);
    CHECK(vo_construct_optional_size(1, {0, 0}, {0, 0}, nullptr) == 1);
    CHECK(vo_construct_rect(99, 0, 0, 0, 0, rect.data()) == 1);
    CHECK(vo_construct_rect(1, 0, 0, 0, 0, nullptr) == 1);
}
