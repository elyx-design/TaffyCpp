// Faithful public/default+parse assertions from upstream src/style/alignment.rs.
// Included by the parent's Catch2 translation unit after tests/support/value_oracle.h.
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace vo_alignment_test {
struct Value {
    std::uint32_t keyword;
    std::uint32_t safety;
    bool operator==(const Value&) const = default;
};
enum Domain : std::uint32_t { Items = 1, Content = 2, JustifyContent = 3, AlignSelf = 4 };
enum Keyword : std::uint32_t {
    Normal = 0, Start = 1, End = 2, FlexStart = 3, FlexEnd = 4,
    SelfStart = 5, SelfEnd = 6, Center = 7, Baseline = 8, Stretch = 9,
    SpaceBetween = 10, SpaceEvenly = 11, SpaceAround = 12
};
enum Safety : std::uint32_t { Default = 0, Unsafe = 1, Safe = 2 };
enum Constant : std::uint32_t {
    NORMAL = 0, START = 1, END = 2, FLEX_START = 3, FLEX_END = 4,
    SELF_START = 5, SELF_END = 6, CENTER = 7, BASELINE = 8, STRETCH = 9,
    SPACE_BETWEEN = 10, SPACE_EVENLY = 11, SPACE_AROUND = 12,
    SAFE_START = 101, SAFE_END = 102, SAFE_FLEX_START = 103, SAFE_FLEX_END = 104,
    SAFE_SELF_START = 105, SAFE_SELF_END = 106, SAFE_CENTER = 107
};
enum Direction : std::uint32_t { Ltr = 0, Rtl = 1 };
extern "C" {
std::int32_t vo_alignment_constant(std::uint32_t, std::uint32_t, Value*);
std::int32_t vo_alignment_default(std::uint32_t, Value*);
std::int32_t vo_alignment_default_component(std::uint32_t, std::uint32_t*);
std::int32_t vo_alignment_is_safe(std::uint32_t, Value, std::uint32_t*);
std::int32_t vo_alignment_keyword(std::uint32_t, Value, std::uint32_t*);
std::int32_t vo_alignment_resolve_self_relative(Value, std::uint32_t, std::uint32_t, std::uint32_t, Value*);
std::int32_t vo_alignment_parse(std::uint32_t, const std::uint8_t*, std::size_t, Value*);
}
inline Value constant(Domain domain, Constant id) {
    Value out{};
    REQUIRE(vo_alignment_constant(domain, id, &out) == 0);
    return out;
}
inline bool is_safe(Domain domain, Constant id) {
    const auto value = constant(domain, id);
    std::uint32_t out = 99;
    REQUIRE(vo_alignment_is_safe(domain, value, &out) == 0);
    REQUIRE(out <= 1);
    return out != 0;
}
inline std::uint32_t keyword(Domain domain, Constant id) {
    const auto value = constant(domain, id);
    std::uint32_t out = 99;
    REQUIRE(vo_alignment_keyword(domain, value, &out) == 0);
    return out;
}
inline Value defaults(Domain domain) {
    Value out{};
    REQUIRE(vo_alignment_default(domain, &out) == 0);
    return out;
}
inline std::uint32_t default_component(std::uint32_t kind) {
    std::uint32_t out = 99;
    REQUIRE(vo_alignment_default_component(kind, &out) == 0);
    return out;
}
inline Value resolve(Constant id, Direction item, Direction container, bool inline_axis) {
    const auto value = constant(Items, id);
    Value out{};
    REQUIRE(vo_alignment_resolve_self_relative(value, item, container, inline_axis ? 1 : 0, &out) == 0);
    return out;
}
inline Value parse(Domain domain, std::string_view css) {
    Value out{};
    REQUIRE(vo_alignment_parse(domain, reinterpret_cast<const std::uint8_t*>(css.data()), css.size(), &out) == 0);
    return out;
}
inline std::int32_t parse_status(Domain domain, std::string_view css) {
    Value out{};
    return vo_alignment_parse(domain, reinterpret_cast<const std::uint8_t*>(css.data()), css.size(), &out);
}

TEST_CASE("style::alignment::tests::align_items_is_safe", "[value_oracle][alignment]") {
    CHECK(is_safe(Items, SAFE_START));
    CHECK(is_safe(Items, SAFE_END));
    CHECK(is_safe(Items, SAFE_FLEX_START));
    CHECK(is_safe(Items, SAFE_FLEX_END));
    CHECK(is_safe(Items, SAFE_CENTER));
    CHECK_FALSE(is_safe(Items, START));
    CHECK_FALSE(is_safe(Items, END));
    CHECK_FALSE(is_safe(Items, FLEX_START));
    CHECK_FALSE(is_safe(Items, FLEX_END));
    CHECK_FALSE(is_safe(Items, CENTER));
    CHECK_FALSE(is_safe(Items, BASELINE));
    CHECK_FALSE(is_safe(Items, STRETCH));
    CHECK(is_safe(Items, SAFE_SELF_START));
    CHECK(is_safe(Items, SAFE_SELF_END));
    CHECK_FALSE(is_safe(Items, SELF_START));
    CHECK_FALSE(is_safe(Items, SELF_END));
    CHECK_FALSE(is_safe(Items, NORMAL));
}
TEST_CASE("style::alignment::tests::align_items_keyword_strips_safe", "[value_oracle][alignment]") {
    CHECK(keyword(Items, SAFE_START) == Start);
    CHECK(keyword(Items, SAFE_END) == End);
    CHECK(keyword(Items, SAFE_FLEX_START) == FlexStart);
    CHECK(keyword(Items, SAFE_FLEX_END) == FlexEnd);
    CHECK(keyword(Items, SAFE_CENTER) == Center);
    CHECK(keyword(Items, SAFE_SELF_START) == SelfStart);
    CHECK(keyword(Items, SAFE_SELF_END) == SelfEnd);
}
TEST_CASE("style::alignment::tests::align_items_keyword_passthrough", "[value_oracle][alignment]") {
    CHECK(keyword(Items, START) == Start);
    CHECK(keyword(Items, STRETCH) == Stretch);
    CHECK(keyword(Items, BASELINE) == Baseline);
    CHECK(keyword(Items, FLEX_START) == FlexStart);
    CHECK(keyword(Items, NORMAL) == Normal);
}
TEST_CASE("style::alignment::tests::align_items_default_is_normal", "[value_oracle][alignment]") {
    CHECK(defaults(Items) == constant(Items, NORMAL));
    CHECK(default_component(1) == Normal);
    CHECK(default_component(2) == Default);
}
TEST_CASE("style::alignment::tests::resolve_self_relative_inline_axis", "[value_oracle][alignment]") {
    CHECK(resolve(SELF_START, Ltr, Ltr, true) == constant(Items, START));
    CHECK(resolve(SELF_END, Ltr, Ltr, true) == constant(Items, END));
    CHECK(resolve(SELF_START, Rtl, Rtl, true) == constant(Items, START));
    CHECK(resolve(SELF_END, Rtl, Rtl, true) == constant(Items, END));
    CHECK(resolve(SELF_START, Ltr, Rtl, true) == constant(Items, END));
    CHECK(resolve(SELF_END, Ltr, Rtl, true) == constant(Items, START));
    CHECK(resolve(SELF_START, Rtl, Ltr, true) == constant(Items, END));
    CHECK(resolve(SELF_END, Rtl, Ltr, true) == constant(Items, START));
    CHECK(resolve(SAFE_SELF_START, Ltr, Rtl, true) == constant(Items, SAFE_END));
    CHECK(resolve(START, Ltr, Rtl, true) == constant(Items, START));
    CHECK(resolve(FLEX_END, Ltr, Rtl, true) == constant(Items, FLEX_END));
    CHECK(resolve(NORMAL, Ltr, Rtl, true) == constant(Items, NORMAL));
}
TEST_CASE("style::alignment::tests::resolve_self_relative_block_axis", "[value_oracle][alignment]") {
    CHECK(resolve(SELF_START, Ltr, Rtl, false) == constant(Items, START));
    CHECK(resolve(SELF_END, Rtl, Ltr, false) == constant(Items, END));
}
TEST_CASE("style::alignment::tests::align_content_is_safe", "[value_oracle][alignment]") {
    CHECK(is_safe(Content, SAFE_START));
    CHECK(is_safe(Content, SAFE_CENTER));
    CHECK_FALSE(is_safe(Content, SPACE_BETWEEN));
    CHECK_FALSE(is_safe(Content, STRETCH));
    CHECK_FALSE(is_safe(Content, NORMAL));
}
TEST_CASE("style::alignment::tests::align_content_keyword_strips_safe", "[value_oracle][alignment]") {
    CHECK(keyword(Content, SAFE_START) == Start);
    CHECK(keyword(Content, SAFE_FLEX_END) == FlexEnd);
    CHECK(keyword(Content, SAFE_CENTER) == Center);
    CHECK(keyword(Content, SPACE_BETWEEN) == SpaceBetween);
    CHECK(keyword(Content, NORMAL) == Normal);
    CHECK(constant(JustifyContent, NORMAL) == constant(Content, NORMAL));
}
// Private keyword.reversed() and Rust storage-budget tests are tracked in alignment-notes.md.
TEST_CASE("style::alignment::tests::parse_align_items_plain", "[value_oracle][alignment][parse]") {
    CHECK(parse(Items, "start") == constant(Items, START));
    CHECK(parse(Items, "end") == constant(Items, END));
    CHECK(parse(Items, "flex-start") == constant(Items, FLEX_START));
    CHECK(parse(Items, "flex-end") == constant(Items, FLEX_END));
    CHECK(parse(Items, "center") == constant(Items, CENTER));
    CHECK(parse(Items, "self-start") == constant(Items, SELF_START));
    CHECK(parse(Items, "self-end") == constant(Items, SELF_END));
    CHECK(parse(Items, "baseline") == constant(Items, BASELINE));
    CHECK(parse(Items, "stretch") == constant(Items, STRETCH));
}
TEST_CASE("style::alignment::tests::align_content_default_is_normal", "[value_oracle][alignment]") {
    CHECK(defaults(Content) == constant(Content, NORMAL));
    CHECK(defaults(JustifyContent) == constant(JustifyContent, NORMAL));
}
TEST_CASE("style::alignment::tests::parse_align_items_normal", "[value_oracle][alignment][parse]") {
    CHECK(parse(Items, "normal") == constant(Items, NORMAL));
    CHECK(parse(Items, "NORMAL") == constant(Items, NORMAL));
    CHECK(constant(Items, NORMAL).safety == Default);
    CHECK(parse_status(AlignSelf, "auto") == 2);
}
TEST_CASE("style::alignment::tests::parse_align_items_safe", "[value_oracle][alignment][parse]") {
    CHECK(parse(Items, "safe start") == constant(Items, SAFE_START));
    CHECK(parse(Items, "safe end") == constant(Items, SAFE_END));
    CHECK(parse(Items, "safe flex-start") == constant(Items, SAFE_FLEX_START));
    CHECK(parse(Items, "safe flex-end") == constant(Items, SAFE_FLEX_END));
    CHECK(parse(Items, "safe self-start") == constant(Items, SAFE_SELF_START));
    CHECK(parse(Items, "safe self-end") == constant(Items, SAFE_SELF_END));
    CHECK(parse(Items, "safe center") == constant(Items, SAFE_CENTER));
}
TEST_CASE("style::alignment::tests::parse_align_items_safe_case_insensitive", "[value_oracle][alignment][parse]") {
    CHECK(parse(Items, "SAFE Start") == constant(Items, SAFE_START));
    CHECK(parse(Items, "Safe FLEX-end") == constant(Items, SAFE_FLEX_END));
}
TEST_CASE("style::alignment::tests::parse_align_items_unsafe", "[value_oracle][alignment][parse]") {
    CHECK(parse(Items, "unsafe start") == (Value{Start, Unsafe}));
    CHECK(parse(Items, "unsafe end") == (Value{End, Unsafe}));
    CHECK(parse(Items, "unsafe self-start") == (Value{SelfStart, Unsafe}));
    CHECK(parse(Items, "unsafe self-end") == (Value{SelfEnd, Unsafe}));
    CHECK(parse(Items, "unsafe center") == (Value{Center, Unsafe}));
    CHECK(constant(Items, START).safety == Default);
}
TEST_CASE("style::alignment::tests::parse_align_items_rejects_invalid_safe_combos", "[value_oracle][alignment][parse]") {
    CHECK(parse_status(Items, "safe stretch") == 2);
    CHECK(parse_status(Items, "safe baseline") == 2);
    CHECK(parse_status(Items, "safe space-between") == 2);
    CHECK(parse_status(Items, "safe") == 2);
    CHECK(parse_status(Items, "safe garbage") == 2);
    CHECK(parse_status(Items, "unsafe stretch") == 2);
    CHECK(parse_status(Items, "unsafe baseline") == 2);
    CHECK(parse_status(Items, "safe normal") == 2);
    CHECK(parse_status(Items, "safe auto") == 2);
    CHECK(parse_status(Items, "unsafe normal") == 2);
    CHECK(parse_status(Items, "unsafe auto") == 2);
}
TEST_CASE("style::alignment::tests::parse_align_content_plain", "[value_oracle][alignment][parse]") {
    CHECK(parse(Content, "normal") == constant(Content, NORMAL));
    CHECK(parse(JustifyContent, "Normal") == constant(JustifyContent, NORMAL));
    CHECK(constant(Content, NORMAL).safety == Default);
    CHECK(parse(Content, "start") == constant(Content, START));
    CHECK(parse(Content, "space-between") == constant(Content, SPACE_BETWEEN));
    CHECK(parse(Content, "space-evenly") == constant(Content, SPACE_EVENLY));
    CHECK(parse(Content, "space-around") == constant(Content, SPACE_AROUND));
    CHECK(parse(Content, "stretch") == constant(Content, STRETCH));
}
TEST_CASE("style::alignment::tests::parse_align_content_safe", "[value_oracle][alignment][parse]") {
    CHECK(parse(Content, "safe start") == constant(Content, SAFE_START));
    CHECK(parse(Content, "safe end") == constant(Content, SAFE_END));
    CHECK(parse(Content, "safe flex-start") == constant(Content, SAFE_FLEX_START));
    CHECK(parse(Content, "safe flex-end") == constant(Content, SAFE_FLEX_END));
    CHECK(parse(Content, "safe center") == constant(Content, SAFE_CENTER));
}
TEST_CASE("style::alignment::tests::parse_align_content_unsafe", "[value_oracle][alignment][parse]") {
    CHECK(parse(Content, "unsafe start") == (Value{Start, Unsafe}));
    CHECK(parse(Content, "unsafe flex-end") == (Value{FlexEnd, Unsafe}));
}
TEST_CASE("style::alignment::tests::parse_align_content_rejects_invalid_safe_combos", "[value_oracle][alignment][parse]") {
    CHECK(parse_status(Content, "safe stretch") == 2);
    CHECK(parse_status(Content, "safe space-between") == 2);
    CHECK(parse_status(Content, "safe space-evenly") == 2);
    CHECK(parse_status(Content, "safe space-around") == 2);
    CHECK(parse_status(Content, "safe") == 2);
    CHECK(parse_status(Content, "unsafe stretch") == 2);
    CHECK(parse_status(Content, "unsafe space-between") == 2);
    CHECK(parse_status(Content, "safe normal") == 2);
    CHECK(parse_status(Content, "unsafe normal") == 2);
    CHECK(parse_status(Content, "auto") == 2);
}
} // namespace vo_alignment_test
