// Standalone test-only conversion smoke/negative tests (not a backend).
#include "behavior_style.hpp"
#include <cassert>
#include <iostream>
using namespace behavior_cpp;
using namespace behavior_cpp::style_detail;
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}
int main() {
    taffy::Style s;
    const auto baseline = style_json(s);
    assert(parse_style_json(baseline) == s);
    assert(Parser{baseline}.parse().object.size() == 45);
    assert(baseline.find("\"dummy\":null") != std::string::npos);
    rejects([] { parse_style_json("{}"); });
    rejects([] { parse_style_json("[]"); });
    rejects([&] { set_native_field(s, "typo", "0"); });
    rejects([&] { set_native_field(s, "grid_row", "[]"); });
    rejects([&] { set_native_field(s, "size", R"({"width":1,"width":2,"height":1})"); });
    rejects([&] { set_native_field(s, "size", R"({"width":4294967296})"); });
    rejects([&] { set_native_field(s, "align_self", "\"SafeStretch\""); });
    rejects([&] { set_native_field(s, "flex_line_count", "65536"); });
    rejects([&] {
        set_native_field(s, "grid_column", R"({"start":{"NamedSpan":["x",65536]},"end":"Auto"})");
    });
    assert(style_json(s) == baseline);
    set_native_field(s, "align_self", "\"UnsafeSelfEnd\"");
    set_native_field(s, "align_content", "\"SafeCenter\"");
    set_native_field(s, "grid_template_column_names", R"([["a"],[],["\u03b1"]])");
    set_native_field(
        s, "grid_template_areas",
        R"({"areas":[{"name":"hero","row_start":1,"row_end":2,"column_start":1,"column_end":3}],"row_count":1,"column_count":2})");
    set_native_field(s, "grid_column", R"({"start":{"NamedSpan":["x",1]},"end":{"Span":0}})");
    set_native_field(s, "contain", "255");
    set_native_field(s, "float", "\"Right\"");
    set_native_field(s, "item_is_replaced", "true");
    set_native_field(s, "flex_line_count", "0");
    assert(parse_style_json(style_json(s)) == s);
    s.size.width = taffy::Dimension::length(std::numeric_limits<float>::infinity());
    s.flex_basis = taffy::Dimension::length(std::bit_cast<float>(std::uint32_t(0x7fc12345)));
    auto round = parse_style_json(style_json(s));
    assert(std::isinf(round.size.width.value()));
    assert(std::bit_cast<std::uint32_t>(round.flex_basis.value()) == 0x7fc12345);
    assert(encode(taffy::Dimension::length(1)).text == "5360320512");
    s.scrollbar_width = std::numeric_limits<float>::infinity();
    rejects([&] { style_json(s); });
    parse_json_value(R"({"size":{"width":{"Percent":50}},"size":false})");
    rejects([] { parse_json_value("[1,]"); });
    rejects([] { parse_json_value("1e999"); });
    rejects([] { parse_json_value("\"\\ud800\""); });
    auto txt = [](const char* p) { return BehaviorText{p, std::char_traits<char>::length(p)}; };
    BehaviorProperty p[] = {{txt("width"), txt("25%")},
                            {txt("flex-line-count"), txt("2")},
                            {txt("aspect-ratio"), txt("NaN")},
                            {txt("grid-template-columns"), txt("repeat(2, [a] 10px [b])")}};
    auto css = parse_properties(p, 4);
    assert(css.size.width.value() == 0.25f && css.flex_line_count == 2 &&
           std::isnan(*css.aspect_ratio));
    assert(std::get<taffy::GridTemplateRepetition>(css.grid_template_columns[0].value)
               .line_names.size() == 2);
    p[1] = p[0];
    rejects([&] { parse_properties(p, 4); });
    p[0] = {txt("flex-grow"), txt(" 1")};
    rejects([&] { parse_properties(p, 1); });
    p[0] = {txt("aspect-ratio"), txt("auto")};
    rejects([&] { parse_properties(p, 1); });
    p[0] = {txt("width"), txt("auto")};
    assert(parse_properties(p, 1).size.width.is_auto());
    std::cout << "behavior_style selftest passed\n";
}
