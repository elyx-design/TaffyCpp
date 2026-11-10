// MIT: assertion sequences translated from pinned Taffy v0.15.1.
#include <catch2/catch_test_macros.hpp>
#include "support/value_oracle.h"
#include <string_view>

#include "ported/value/alignment.cpp.inc"
#include "ported/value/math_resolve.cpp.inc"
#include "ported/value/style_values.cpp.inc"

namespace {
std::uint32_t parse_contain(std::string_view css) {
    std::uint32_t result = 99;
    REQUIRE(vo_contain_parse(reinterpret_cast<const std::uint8_t*>(css.data()), css.size(), &result) == 0);
    return result;
}
std::int32_t contain_status(std::string_view css) {
    std::uint32_t result = 99;
    return vo_contain_parse(reinterpret_cast<const std::uint8_t*>(css.data()), css.size(), &result);
}
}

TEST_CASE("src/style/mod.rs::style::tests::parse_contain", "[value_oracle][contain]") {
    constexpr std::uint32_t none = 0, layout = 1, paint = 2;
    CHECK(parse_contain("none") == none);
    CHECK(parse_contain("content") == (layout | paint));
    CHECK(parse_contain("layout") == layout);
    CHECK(parse_contain("style") == none);
    CHECK(parse_contain("paint") == paint);
    CHECK(parse_contain("layout paint") == (layout | paint));
    CHECK(parse_contain("paint layout") == (layout | paint));
    CHECK(parse_contain("layout paint style") == (layout | paint));
    CHECK(parse_contain("Paint LAYOUT") == (layout | paint));
    CHECK(contain_status("paint paint") == 2);
    CHECK(contain_status("") == 2);
    CHECK(contain_status("banana") == 2);
    CHECK(contain_status("layout layout") == 2);
    CHECK(contain_status("none layout") == 2);
    CHECK(contain_status("layout none") == 2);
    CHECK(contain_status("content layout") == 2);
    CHECK(contain_status("layout content") == 2);
}
