// Faithful public-value translations from Taffy v0.15.1 (MIT).
// Included after Catch2 macros; the parent runner supplies support/value_oracle.h.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string_view>

namespace vo_style_tests {
// These are concrete bridge records, NOT mirrors of Rust enums or CompactLength.
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
extern "C" {
std::int32_t vo_style_value(std::uint32_t, std::uint32_t, Value, Value*);
std::int32_t vo_style_equal(std::uint32_t, std::uint32_t, Value, std::uint32_t, Value, bool*);
std::int32_t vo_style_to_min(std::uint32_t, Value, Value*);
std::int32_t vo_style_fit_content(Value, Value*);
std::int32_t vo_style_parse_value(std::uint32_t, const std::uint8_t*, std::size_t, Value*);
std::int32_t vo_style_parse_placement(const std::uint8_t*, std::size_t, Placement*);
std::int32_t vo_style_parse_repetition(std::uint32_t, const std::uint8_t*, std::size_t,
                                       Repetition*);
std::int32_t vo_style_repeat(std::uint32_t, std::uint16_t, const std::uint8_t*, std::size_t,
                             Repetition*);
std::int32_t vo_style_track_count(std::uint32_t, std::uint16_t*);
}
enum Family : std::uint32_t { LP, LPA, Dim, MaxTrack, MinTrack };
enum Kind : std::uint32_t {
    Length,
    Percent,
    Auto,
    MinContent,
    MaxContent,
    FitPx,
    FitPercent,
    FitKeyword,
    Stretch,
    Content,
    Fr,
    Calc,
    FitCalc,
    Zero
};
enum Operation : std::uint32_t {
    Expand,
    FromExpanded,
    ExpandedFrom,
    RoundTrip,
    FitContent,
    DimensionToMin,
    MaxTrackToMin
};
static Value v(Kind kind, float number = 0) {
    return {kind, number, nullptr};
}
static Value calc(Kind kind, const void* handle) {
    return {kind, 0, handle};
}
static const std::uint8_t* bytes(std::string_view s) {
    return reinterpret_cast<const std::uint8_t*>(s.data());
}
static Value value(Family family, Operation operation, Value input) {
    Value result{};
    REQUIRE(vo_style_value(family, operation, input, &result) == 0);
    return result;
}
// Operation success is a bridge check; the caller checks native PartialEq's result.
static bool native_equal(Family family, Operation left_op, Value left, Operation right_op,
                         Value right) {
    bool equal = false;
    REQUIRE(vo_style_equal(family, left_op, left, right_op, right, &equal) == 0);
    return equal;
}
static Value to_min(Family source, Value input) {
    Value result{};
    REQUIRE(vo_style_to_min(source, input, &result) == 0);
    return result;
}
static Placement placement(std::string_view css) {
    Placement result{};
    REQUIRE(vo_style_parse_placement(bytes(css), css.size(), &result) == 0);
    return result;
}
static Repetition parse_repetition(std::uint32_t mode, std::string_view css) {
    Repetition result{};
    REQUIRE(vo_style_parse_repetition(mode, bytes(css), css.size(), &result) == 0);
    return result;
}
static Repetition repeat(std::uint16_t count) {
    Repetition result{};
    REQUIRE(vo_style_repeat(0, count, nullptr, 0, &result) == 0);
    return result;
}
static Repetition repeat(std::string_view kind) {
    Repetition result{};
    REQUIRE(vo_style_repeat(1, 0, bytes(kind), kind.size(), &result) == 0);
    return result;
}
} // namespace vo_style_tests

TEST_CASE("src/style/dimension.rs::style::dimension::expand_tests::length_percentage_round_trips",
          "[value_oracle][style_values]") {
    using namespace vo_style_tests;
    for (auto input : {v(Length, 12), v(Percent, .5f), v(Zero)}) {
        CHECK(value(LP, RoundTrip, input) == value(LP, Expand, input));
        CHECK(native_equal(LP, RoundTrip, input, Expand, input));
        CHECK(value(LP, ExpandedFrom, input) == value(LP, Expand, input));
    }
    CHECK(value(LP, Expand, v(Length, 3)) == v(Length, 3));
    CHECK(value(LP, Expand, v(Percent, .25f)) == v(Percent, .25f));
}
TEST_CASE(
    "src/style/dimension.rs::style::dimension::expand_tests::length_percentage_auto_round_trips",
    "[value_oracle][style_values]") {
    using namespace vo_style_tests;
    for (auto input : {v(Length, 12), v(Percent, .5f), v(Auto)}) {
        CHECK(value(LPA, RoundTrip, input) == value(LPA, Expand, input));
        CHECK(native_equal(LPA, RoundTrip, input, Expand, input));
    }
    CHECK(value(LPA, Expand, v(Auto)) == v(Auto));
}
TEST_CASE(
    "src/style/dimension.rs::style::dimension::expand_tests::dimension_round_trips_all_keywords",
    "[value_oracle][style_values]") {
    using namespace vo_style_tests;
    for (auto input : {v(Length, 12), v(Percent, .5f), v(Auto), v(MinContent), v(MaxContent),
                       v(FitPx, 30), v(FitPercent, .75f), v(FitKeyword), v(Stretch), v(Content)}) {
        CHECK(value(Dim, RoundTrip, input) == value(Dim, Expand, input));
        CHECK(native_equal(Dim, RoundTrip, input, Expand, input));
    }
    CHECK(value(Dim, Expand, v(FitPx, 30)) == v(FitPx, 30));
    CHECK(value(Dim, Expand, v(Content)) == v(Content));
}
TEST_CASE("src/style/dimension.rs::style::dimension::expand_tests::calc_round_trips",
          "[value_oracle][style_values][calc]") {
    using namespace vo_style_tests;
    alignas(8) static const std::uint64_t handle = 0;
    CHECK(value(LP, Expand, calc(Calc, &handle)) == calc(Calc, &handle));
    CHECK(value(LP, FromExpanded, calc(Calc, &handle)) == value(LP, Expand, calc(Calc, &handle)));
    CHECK(native_equal(LP, FromExpanded, calc(Calc, &handle), Expand, calc(Calc, &handle)));
    CHECK(value(Dim, Expand, calc(Calc, &handle)) == calc(Calc, &handle));
    CHECK(value(Dim, Expand, calc(FitCalc, &handle)) == calc(FitCalc, &handle));
    CHECK(value(Dim, FromExpanded, calc(FitCalc, &handle)) ==
          value(Dim, Expand, calc(FitCalc, &handle)));
    CHECK(native_equal(Dim, FromExpanded, calc(FitCalc, &handle), Expand, calc(FitCalc, &handle)));
    CHECK(value(LPA, Expand, calc(Calc, &handle)) == calc(Calc, &handle));
}
TEST_CASE("src/style/grid.rs::style::grid::expand_tests::max_track_sizing_function_round_trips",
          "[value_oracle][style_values]") {
    using namespace vo_style_tests;
    for (auto input : {v(Length, 12), v(Percent, .5f), v(Auto), v(MinContent), v(MaxContent),
                       v(FitPx, 30), v(FitPercent, .75f), v(Fr, 2)}) {
        CHECK(value(MaxTrack, RoundTrip, input) == value(MaxTrack, Expand, input));
        CHECK(native_equal(MaxTrack, RoundTrip, input, Expand, input));
        CHECK(value(MaxTrack, ExpandedFrom, input) == value(MaxTrack, Expand, input));
    }
    CHECK(value(MaxTrack, Expand, v(Fr, 2)) == v(Fr, 2));
    CHECK(value(MaxTrack, Expand, v(FitPx, 30)) == v(FitPx, 30));
}
TEST_CASE("src/style/grid.rs::style::grid::expand_tests::min_track_sizing_function_round_trips",
          "[value_oracle][style_values]") {
    using namespace vo_style_tests;
    for (auto input : {v(Length, 12), v(Percent, .5f), v(Auto), v(MinContent), v(MaxContent)}) {
        CHECK(value(MinTrack, RoundTrip, input) == value(MinTrack, Expand, input));
        CHECK(native_equal(MinTrack, RoundTrip, input, Expand, input));
        CHECK(value(MinTrack, ExpandedFrom, input) == value(MinTrack, Expand, input));
    }
    CHECK(value(MinTrack, Expand, v(MaxContent)) == v(MaxContent));
}
TEST_CASE("src/style/grid.rs::style::grid::expand_tests::track_sizing_function_calc_round_trips",
          "[value_oracle][style_values][calc]") {
    using namespace vo_style_tests;
    alignas(8) static const std::uint64_t handle = 0;
    CHECK(value(MaxTrack, Expand, calc(Calc, &handle)) == calc(Calc, &handle));
    CHECK(value(MaxTrack, Expand, calc(FitCalc, &handle)) == calc(FitCalc, &handle));
    CHECK(value(MaxTrack, FromExpanded, calc(FitCalc, &handle)) ==
          value(MaxTrack, Expand, calc(FitCalc, &handle)));
    CHECK(native_equal(MaxTrack, FromExpanded, calc(FitCalc, &handle), Expand,
                       calc(FitCalc, &handle)));
    Value fit{};
    REQUIRE(vo_style_fit_content(calc(Calc, &handle), &fit) == 0);
    CHECK(fit == value(MaxTrack, Expand, calc(FitCalc, &handle)));
    CHECK(native_equal(MaxTrack, FitContent, calc(Calc, &handle), Expand, calc(FitCalc, &handle)));
    CHECK(to_min(MaxTrack, calc(FitCalc, &handle)) == v(Auto));
    CHECK(native_equal(MinTrack, MaxTrackToMin, calc(FitCalc, &handle), Expand, v(Auto)));
    CHECK(to_min(Dim, calc(FitCalc, &handle)) == v(Auto));
    CHECK(native_equal(MinTrack, DimensionToMin, calc(FitCalc, &handle), Expand, v(Auto)));
    CHECK(value(MaxTrack, FromExpanded, calc(Calc, &handle)) ==
          value(MaxTrack, Expand, calc(Calc, &handle)));
    CHECK(native_equal(MaxTrack, FromExpanded, calc(Calc, &handle), Expand, calc(Calc, &handle)));
    CHECK(value(MinTrack, Expand, calc(Calc, &handle)) == calc(Calc, &handle));
}
TEST_CASE("src/style/grid.rs::style::grid::tests::grid_placement_parser_saturates_numeric_values",
          "[value_oracle][style_values][parse]") {
    using namespace vo_style_tests;
    const auto positive = placement("32768");
    CHECK((positive.kind == 1 && positive.index == 32767));
    const auto negative = placement("-32769");
    CHECK((negative.kind == 1 && negative.index == -32768));
    const auto span = placement("span 65536");
    CHECK((span.kind == 2 && span.index == 65535));
    const auto named_line = placement("32768 line");
    CHECK((named_line.kind == 3 && named_line.index == 32767)); // upstream ignores name
    const auto named_span = placement("span 65536 line");
    CHECK((named_span.kind == 4 && named_span.index == 65535)); // upstream ignores name
}
TEST_CASE("src/style/grid.rs::style::grid::tests::repetition_parser_saturates_numeric_values",
          "[value_oracle][style_values][parse]") {
    using namespace vo_style_tests;
    CHECK(parse_repetition(0, "65536") == (Repetition{0, 0, 65535, 0, 0}));
    const auto component = parse_repetition(1, "repeat(65536, 1px)");
    CHECK((component.component == 1 && component.kind == 0 && component.count == 65535));
}
TEST_CASE("src/style/grid.rs::style::grid::tests::repetition_track_count_saturates",
          "[value_oracle][style_values][parse]") {
    using namespace vo_style_tests;
    std::uint16_t count = 0;
    REQUIRE(vo_style_track_count(65536, &count) == 0);
    CHECK(count == 65535);
}
TEST_CASE("src/style_helpers.rs::style_helpers::repeat_fn_tests::test_repeat_u16",
          "[value_oracle][style_values]") {
    using namespace vo_style_tests;
    CHECK(repeat(123) == (Repetition{1, 0, 123, 0, 0}));
}
TEST_CASE("src/style_helpers.rs::style_helpers::repeat_fn_tests::test_repeat_auto_fit_str",
          "[value_oracle][style_values]") {
    using namespace vo_style_tests;
    CHECK(repeat("auto-fit") == (Repetition{1, 1, 0, 0, 0}));
}
TEST_CASE("src/style_helpers.rs::style_helpers::repeat_fn_tests::test_repeat_auto_fill_str",
          "[value_oracle][style_values]") {
    using namespace vo_style_tests;
    CHECK(repeat("auto-fill") == (Repetition{1, 2, 0, 0, 0}));
}

// Supplemental checks are NOT attributed to any upstream source-unit identity.
TEST_CASE("supplemental::style_values::invalid_css",
          "[value_oracle][style_values][supplemental][parse]") {
    using namespace vo_style_tests;
    Placement p{};
    for (std::string_view css :
         {"", "0", "span 0", "auto 2", "span span", "one two", "1 2", "1.5"}) {
        INFO(css);
        CHECK(vo_style_parse_placement(bytes(css), css.size(), &p) == 2);
    }
    Repetition r{};
    for (std::string_view css : {"", "0", "-1", "1.5", "auto", "auto-fit junk"}) {
        INFO(css);
        CHECK(vo_style_parse_repetition(0, bytes(css), css.size(), &r) == 2);
    }
    for (std::string_view css : {"repeat(0, 1px)", "repeat(1,)", "repeat(auto, 1px)",
                                 "repeat(2 1px)", "minmax(1fr, 1px)"}) {
        INFO(css);
        CHECK(vo_style_parse_repetition(1, bytes(css), css.size(), &r) == 2);
    }
    Value result{};
    for (Family family : {LP, LPA, Dim, MaxTrack, MinTrack}) {
        for (std::string_view css : {"", "garbage", "1em", "1px junk", "calc(1px)"}) {
            INFO(family);
            INFO(css);
            CHECK(vo_style_parse_value(family, bytes(css), css.size(), &result) == 2);
        }
    }
    const std::string_view negative_fr = "-1fr";
    CHECK(vo_style_parse_value(MaxTrack, bytes(negative_fr), negative_fr.size(), &result) == 2);
}
TEST_CASE("supplemental::style_values::repeat_invalid_string_panics",
          "[value_oracle][style_values][supplemental][panic]") {
    using namespace vo_style_tests;
    Repetition r{};
    const std::string_view css = "not-a-repetition";
    // repeat() unwraps TryFrom<&str>; unlike parse failure, this must be status 3.
    CHECK(vo_style_repeat(1, 0, bytes(css), css.size(), &r) == 3);
}
TEST_CASE("supplemental::style_values::abi_validation",
          "[value_oracle][style_values][supplemental]") {
    using namespace vo_style_tests;
    Value result{};
    CHECK(vo_style_value(99, Expand, v(Length, 1), &result) == 1);
    CHECK(vo_style_value(LP, 99, v(Length, 1), &result) == 1);
    CHECK(vo_style_value(LP, Expand, v(Auto), &result) == 2);
    CHECK(vo_style_value(LP, Expand, calc(Calc, nullptr), &result) == 1);
    CHECK(vo_style_value(LP, Expand, v(Length, 1), nullptr) == 1);
    bool equal = false;
    CHECK(vo_style_equal(99, Expand, v(Length, 1), Expand, v(Length, 1), &equal) == 1);
    CHECK(vo_style_equal(LP, 99, v(Length, 1), Expand, v(Length, 1), &equal) == 1);
    CHECK(vo_style_equal(LP, Expand, v(Length, 1), 99, v(Length, 1), &equal) == 1);
    CHECK(vo_style_equal(LP, FitContent, v(Length, 1), Expand, v(Length, 1), &equal) == 1);
    CHECK(vo_style_equal(LP, FromExpanded, v(Auto), Expand, v(Length, 1), &equal) == 2);
    CHECK(vo_style_equal(LP, Expand, v(Length, 1), Expand, calc(Calc, nullptr), &equal) == 1);
    CHECK(vo_style_equal(LP, Expand, v(Length, 1), Expand, v(Length, 1), nullptr) == 1);
    CHECK_FALSE(native_equal(LP, Expand, v(Length, 1), Expand, v(Length, 2)));
    CHECK_FALSE(native_equal(Dim, Expand, v(Auto), Expand, v(Content)));
    alignas(8) static const std::uint64_t handles[2] = {};
    CHECK_FALSE(native_equal(MaxTrack, FromExpanded, calc(Calc, &handles[0]), Expand,
                             calc(Calc, &handles[1])));
    Placement p{};
    CHECK(vo_style_parse_placement(nullptr, 1, &p) == 1);
    const std::uint8_t invalid_utf8[] = {0xff};
    CHECK(vo_style_parse_placement(invalid_utf8, 1, &p) == 1);
    std::uint16_t count{};
    CHECK(vo_style_track_count(1'000'001, &count) == 1);
}
