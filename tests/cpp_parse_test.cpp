// Standalone native parser coverage: link production src/parse.cpp and Catch2 only.
#include <taffy/parse.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace taffy;
namespace {
template <class T> T value(std::string_view source) {
    INFO("CSS source: " << source);
    auto parsed = parse_css<T>(source);
    if (!parsed) INFO("error " << static_cast<int>(parsed.error().code) << " at "
                              << parsed.error().offset << ": " << parsed.error().message);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}
template <class T> void rejects(std::string_view source) {
    INFO("invalid CSS source: " << source);
    auto parsed = parse_css<T>(source);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().offset <= source.size());
    CHECK_FALSE(parsed.error().message.empty());
}
template <class T> void error_at(std::string_view source, ParseErrorCode code, std::size_t offset) {
    INFO("CSS error source: " << source);
    auto parsed = parse_css<T>(source);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().code == code);
    CHECK(parsed.error().offset == offset);
    CHECK_FALSE(parsed.error().message.empty());
}
template <class T> void keywords(std::initializer_list<std::pair<std::string_view, T>> cases) {
    for (const auto& [source, expected] : cases) {
        CHECK(value<T>(source) == expected);
        std::string upper(source);
        for (char& c : upper) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        CHECK(value<T>(upper) == expected);
        CHECK(value<T>("/*before*/ " + std::string(source) + " /*after*/") == expected);
    }
    rejects<T>("unknown");
    rejects<T>("");
    rejects<T>("none extra");
}
using Edit = std::function<void(Style&)>;
struct PropertyCase { std::string_view name, source; Edit expected; };
// Every stored Style field, every supported longhand, and every shorthand alias.
std::vector<PropertyCase> properties() {
    using L = LengthPercentage;
    using A = LengthPercentageAuto;
    using D = Dimension;
    return {
        {"display", "grid", [](Style& s) { s.display = Display::Grid; }},
        {"item-is-table", "true", [](Style& s) { s.item_is_table = true; }},
        {"item-is-replaced", "true", [](Style& s) { s.item_is_replaced = true; }},
        {"item-is-compressible-replaced", "true", [](Style& s) { s.item_is_compressible_replaced = true; }},
        {"box-sizing", "content-box", [](Style& s) { s.box_sizing = BoxSizing::ContentBox; }},
        {"direction", "rtl", [](Style& s) { s.direction = Direction::Rtl; }},
        {"overflow", "hidden scroll", [](Style& s) { s.overflow = {Overflow::Hidden, Overflow::Scroll}; }},
        {"overflow-x", "clip", [](Style& s) { s.overflow.x = Overflow::Clip; }},
        {"overflow-y", "hidden", [](Style& s) { s.overflow.y = Overflow::Hidden; }},
        {"scrollbar-width", "12.5", [](Style& s) { s.scrollbar_width = 12.5f; }},
        {"contain", "paint layout style", [](Style& s) { s.contain = Contain::content(); }},
        {"float", "right", [](Style& s) { s.float_ = Float::Right; }},
        {"clear", "both", [](Style& s) { s.clear = Clear::Both; }},
        {"position", "sticky", [](Style& s) { s.position = Position::Sticky; }},
        {"width", "fit-content(40%)", [](Style& s) { s.size.width = D::fit_content_percent(.4f); }},
        {"height", "stretch", [](Style& s) { s.size.height = D::stretch(); }},
        {"min-width", "12px", [](Style& s) { s.min_size.width = A::length(12); }},
        {"min-height", "25%", [](Style& s) { s.min_size.height = A::percent(.25f); }},
        {"max-width", "auto", [](Style& s) { s.max_size.width = A::auto_(); }},
        {"max-height", "90px", [](Style& s) { s.max_size.height = A::length(90); }},
        {"aspect-ratio", "16 / 8", [](Style& s) { s.aspect_ratio = 2; }},
        {"inset", "1px 2px 3px 4px", [](Style& s) { s.inset = Rect<A>::from_length(4,2,1,3); }},
        {"left", "11px", [](Style& s) { s.inset.left = A::length(11); }},
        {"right", "12px", [](Style& s) { s.inset.right = A::length(12); }},
        {"top", "13px", [](Style& s) { s.inset.top = A::length(13); }},
        {"bottom", "14px", [](Style& s) { s.inset.bottom = A::length(14); }},
        {"margin", "auto 2px 3px 4px", [](Style& s) { s.margin = {A::length(4), A::length(2), A::auto_(), A::length(3)}; }},
        {"padding", "1px 2px 3px 4px", [](Style& s) { s.padding = Rect<L>::from_length(4,2,1,3); }},
        {"border", "1px 2px 3px 4px", [](Style& s) { s.border = Rect<L>::from_length(4,2,1,3); }},
        {"border-width", "5px", [](Style& s) { s.border = Rect<L>::splat(L::length(5)); }},
        {"margin-left", "21px", [](Style& s) { s.margin.left = A::length(21); }},
        {"margin-right", "22px", [](Style& s) { s.margin.right = A::length(22); }},
        {"margin-top", "23px", [](Style& s) { s.margin.top = A::length(23); }},
        {"margin-bottom", "24px", [](Style& s) { s.margin.bottom = A::length(24); }},
        {"padding-left", "31px", [](Style& s) { s.padding.left = L::length(31); }},
        {"padding-right", "32px", [](Style& s) { s.padding.right = L::length(32); }},
        {"padding-top", "33px", [](Style& s) { s.padding.top = L::length(33); }},
        {"padding-bottom", "34px", [](Style& s) { s.padding.bottom = L::length(34); }},
        {"border-left", "41px", [](Style& s) { s.border.left = L::length(41); }},
        {"border-right", "42px", [](Style& s) { s.border.right = L::length(42); }},
        {"border-top", "43px", [](Style& s) { s.border.top = L::length(43); }},
        {"border-bottom", "44px", [](Style& s) { s.border.bottom = L::length(44); }},
        {"border-left-width", "51px", [](Style& s) { s.border.left = L::length(51); }},
        {"border-right-width", "52px", [](Style& s) { s.border.right = L::length(52); }},
        {"border-top-width", "53px", [](Style& s) { s.border.top = L::length(53); }},
        {"border-bottom-width", "54px", [](Style& s) { s.border.bottom = L::length(54); }},
        {"gap", "6px 7px", [](Style& s) { s.gap = {L::length(7), L::length(6)}; }},
        {"column-gap", "8px", [](Style& s) { s.gap.width = L::length(8); }},
        {"row-gap", "9px", [](Style& s) { s.gap.height = L::length(9); }},
        {"align-items", "safe end", [](Style& s) { s.align_items = AlignItems::safe_end(); }},
        {"align-self", "unsafe center", [](Style& s) { s.align_self = AlignItems::center().with_safety(AlignmentSafety::Unsafe); }},
        {"justify-items", "self-start", [](Style& s) { s.justify_items = AlignItems::self_start(); }},
        {"justify-self", "safe center", [](Style& s) { s.justify_self = AlignItems::safe_center(); }},
        {"align-content", "space-around", [](Style& s) { s.align_content = AlignContent::space_around(); }},
        {"justify-content", "space-evenly", [](Style& s) { s.justify_content = AlignContent::space_evenly(); }},
        {"text-align", "-webkit-center", [](Style& s) { s.text_align = TextAlign::LegacyCenter; }},
        {"place-items", "safe start unsafe end", [](Style& s) { s.align_items = AlignItems::safe_start(); s.justify_items = AlignItems::end().with_safety(AlignmentSafety::Unsafe); }},
        {"place-content", "center space-between", [](Style& s) { s.align_content = AlignContent::center(); s.justify_content = AlignContent::space_between(); }},
        {"place-self", "auto end", [](Style& s) { s.align_self.reset(); s.justify_self = AlignItems::end(); }},
        {"flex-direction", "column-reverse", [](Style& s) { s.flex_direction = FlexDirection::ColumnReverse; }},
        {"flex-wrap", "wrap-reverse balance", [](Style& s) { s.flex_wrap = FlexWrap::BalanceReverse; }},
        {"flex-line-count", "4", [](Style& s) { s.flex_line_count = 4; }},
        {"flex-basis", "content", [](Style& s) { s.flex_basis = D::content(); }},
        {"flex-grow", "2.5", [](Style& s) { s.flex_grow = 2.5f; }},
        {"flex-shrink", "3.5", [](Style& s) { s.flex_shrink = 3.5f; }},
        {"flex-flow", "balance column", [](Style& s) { s.flex_wrap = FlexWrap::Balance; s.flex_direction = FlexDirection::Column; }},
        {"flex", "2 3 40%", [](Style& s) { s.flex_grow = 2; s.flex_shrink = 3; s.flex_basis = D::percent(.4f); }},
        {"grid-auto-flow", "dense column", [](Style& s) { s.grid_auto_flow = GridAutoFlow::ColumnDense; }},
        {"grid-auto-rows", "12px 2fr", [](Style& s) { s.grid_auto_rows = {TrackSizingFunction::length(12), TrackSizingFunction::fr(2)}; }},
        {"grid-auto-columns", "minmax(10px,1fr)", [](Style& s) { s.grid_auto_columns = {{MinTrackSizingFunction::length(10), MaxTrackSizingFunction::fr(1)}}; }},
        {"grid-template-rows", "[a] 10px [b]", [](Style& s) { s.grid_template_rows = {GridTemplateComponent::length(10)}; s.grid_template_row_names = {{"a"},{"b"}}; }},
        {"grid-template-columns", "[c] 20% [d]", [](Style& s) { s.grid_template_columns = {GridTemplateComponent::percent(.2f)}; s.grid_template_column_names = {{"c"},{"d"}}; }},
        {"grid-template-row-names", "[r] []", [](Style& s) { s.grid_template_row_names = {{"r"},{}}; }},
        {"grid-template-column-names", "[] [c]", [](Style& s) { s.grid_template_column_names = {{},{"c"}}; }},
        {"grid-template-areas", "\"a a\"", [](Style& s) { s.grid_template_areas = GridTemplateAreas{{{"a",1,2,1,3}},1,2}; }},
        {"grid-row-start", "2", [](Style& s) { s.grid_row.start = GridPlacement::line(2); }},
        {"grid-row-end", "span 3", [](Style& s) { s.grid_row.end = GridPlacement::span(3); }},
        {"grid-column-start", "a", [](Style& s) { s.grid_column.start = GridPlacement::named_line("a"); }},
        {"grid-column-end", "-2", [](Style& s) { s.grid_column.end = GridPlacement::line(-2); }},
        {"grid-row", "a / span 2", [](Style& s) { s.grid_row = {GridPlacement::named_line("a"),GridPlacement::span(2)}; }},
        {"grid-column", "3 / 5", [](Style& s) { s.grid_column = {GridPlacement::line(3),GridPlacement::line(5)}; }},
        {"grid-area", "1 / 2 / 3 / 4", [](Style& s) { s.grid_row = {GridPlacement::line(1),GridPlacement::line(3)}; s.grid_column = {GridPlacement::line(2),GridPlacement::line(4)}; }},
    };
}
} // namespace

TEST_CASE("native parser keyword enum domains", "[cpp][parse]") {
    keywords<Display>({{"flex",Display::Flex},{"grid",Display::Grid},{"block",Display::Block},{"flow-root",Display::FlowRoot},{"none",Display::None}});
    keywords<Position>({{"static",Position::Static},{"relative",Position::Relative},{"absolute",Position::Absolute},{"fixed",Position::Fixed},{"sticky",Position::Sticky}});
    keywords<BoxSizing>({{"border-box",BoxSizing::BorderBox},{"content-box",BoxSizing::ContentBox}});
    keywords<Direction>({{"ltr",Direction::Ltr},{"rtl",Direction::Rtl}});
    keywords<Overflow>({{"visible",Overflow::Visible},{"clip",Overflow::Clip},{"hidden",Overflow::Hidden},{"scroll",Overflow::Scroll}});
    keywords<TextAlign>({{"auto",TextAlign::Auto},{"-webkit-left",TextAlign::LegacyLeft},{"-webkit-right",TextAlign::LegacyRight},{"-webkit-center",TextAlign::LegacyCenter}});
    keywords<Float>({{"none",Float::None},{"left",Float::Left},{"right",Float::Right}});
    keywords<Clear>({{"none",Clear::None},{"left",Clear::Left},{"right",Clear::Right},{"both",Clear::Both}});
    keywords<FlexDirection>({{"row",FlexDirection::Row},{"column",FlexDirection::Column},{"row-reverse",FlexDirection::RowReverse},{"column-reverse",FlexDirection::ColumnReverse}});
    keywords<bool>({{"true",true},{"false",false}});
    rejects<bool>("1");
    rejects<Overflow>("auto");
    rejects<TextAlign>("center");
    rejects<Float>("inline-start");
}

TEST_CASE("native parser alignment safety and aliases", "[cpp][parse]") {
    keywords<AlignItems>({{"normal",AlignItems::normal()},{"start",AlignItems::start()},{"end",AlignItems::end()},{"flex-start",AlignItems::flex_start()},{"flex-end",AlignItems::flex_end()},{"self-start",AlignItems::self_start()},{"self-end",AlignItems::self_end()},{"center",AlignItems::center()},{"baseline",AlignItems::baseline()},{"stretch",AlignItems::stretch()}});
    keywords<AlignContent>({{"normal",AlignContent::normal()},{"start",AlignContent::start()},{"end",AlignContent::end()},{"flex-start",AlignContent::flex_start()},{"flex-end",AlignContent::flex_end()},{"center",AlignContent::center()},{"stretch",AlignContent::stretch()},{"space-between",AlignContent::space_between()},{"space-around",AlignContent::space_around()},{"space-evenly",AlignContent::space_evenly()}});
    for (auto safety : {AlignmentSafety::Safe, AlignmentSafety::Unsafe}) {
        const std::string prefix = safety == AlignmentSafety::Safe ? "safe " : "unsafe ";
        for (auto word : {"start","end","flex-start","flex-end","self-start","self-end","center"}) {
            CHECK(value<AlignItems>(prefix + word) == value<AlignItems>(word).with_safety(safety));
        }
        for (auto word : {"start","end","flex-start","flex-end","center"}) {
            CHECK(value<AlignContent>(prefix + word) == value<AlignContent>(word).with_safety(safety));
        }
        for (auto word : {"normal","baseline","stretch"}) rejects<AlignItems>(prefix + word);
        for (auto word : {"normal","stretch","space-between","space-around","space-evenly"}) rejects<AlignContent>(prefix + word);
    }
    rejects<AlignItems>("center safe"); rejects<AlignItems>("safe unsafe start");
    rejects<AlignContent>("self-start"); rejects<AlignItems>("first baseline");
    CHECK(value<std::optional<AlignSelf>>("AUTO") == std::nullopt);
    CHECK(value<std::optional<JustifySelf>>("safe end") == AlignItems::safe_end());
    CHECK(value<JustifyItems>("start") == AlignItems::start());
    CHECK(value<JustifyContent>("end") == AlignContent::end());
}

TEST_CASE("native parser containment wrapping and auto flow pinned quirks", "[cpp][parse]") {
    CHECK(value<Contain>("none") == Contain::none());
    CHECK(value<Contain>("style") == Contain::none());
    CHECK(value<Contain>("PAINT style LAYOUT") == Contain::content());
    CHECK(value<Contain>("content") == Contain::content());
    CHECK(value<Contain>("layout") == Contain::layout());
    CHECK(value<Contain>("paint") == Contain::paint());
    for (auto s : {"strict","size","layout layout","none paint","content style","style style"}) rejects<Contain>(s);
    keywords<FlexWrap>({{"nowrap",FlexWrap::NoWrap},{"wrap",FlexWrap::Wrap},{"wrap-reverse",FlexWrap::WrapReverse},{"balance",FlexWrap::Balance},{"balance wrap",FlexWrap::Balance},{"wrap balance",FlexWrap::Balance},{"balance wrap-reverse",FlexWrap::BalanceReverse},{"wrap-reverse balance",FlexWrap::BalanceReverse}});
    for (auto s : {"nowrap balance","wrap wrap","balance balance","wrap wrap-reverse"}) rejects<FlexWrap>(s);
    for (auto [source, expected] : std::initializer_list<std::pair<std::string_view,GridAutoFlow>>{
            {"row",GridAutoFlow::Row},{"column",GridAutoFlow::Column},{"dense",GridAutoFlow::RowDense},
            {"row dense",GridAutoFlow::RowDense},{"dense column",GridAutoFlow::ColumnDense},
            {"row column",GridAutoFlow::Column},{"dense dense",GridAutoFlow::RowDense}})
        CHECK(value<GridAutoFlow>(source) == expected);
    // These two-keyword duplicates are accepted upstream; don't silently impose browser validation.
    rejects<GridAutoFlow>("ROW"); rejects<GridAutoFlow>("row dense column"); rejects<GridAutoFlow>("");
}

TEST_CASE("native parser length domain matrix", "[cpp][parse]") {
    for (auto source : {"12px", "+12px", "1.2e1px", R"(12p\78)", "/*a*/12px/*b*/"}) {
        CHECK(value<LengthPercentage>(source) == LengthPercentage::length(12));
        CHECK(value<LengthPercentageAuto>(source) == LengthPercentageAuto::length(12));
        CHECK(value<Dimension>(source) == Dimension::length(12));
        CHECK(value<MinTrackSizingFunction>(source) == MinTrackSizingFunction::length(12));
        CHECK(value<MaxTrackSizingFunction>(source) == MaxTrackSizingFunction::length(12));
    }
    CHECK(value<LengthPercentage>("-25%") == LengthPercentage::percent(-.25f));
    CHECK(value<LengthPercentageAuto>("auto").is_auto());
    CHECK(value<Dimension>("auto").is_auto());
    CHECK(value<Dimension>("min-content").is_min_content());
    CHECK(value<Dimension>("max-content").is_max_content());
    CHECK(value<Dimension>("content").is_content());
    CHECK(value<Dimension>("stretch").is_stretch());
    CHECK(value<Dimension>("fit-content").kind() == LengthKind::FitContent);
    CHECK(value<Dimension>("fit-content(25%)") == Dimension::fit_content_percent(.25f));
    CHECK(value<Dimension>("fit-content(-2px)") == Dimension::fit_content_px(-2));
    CHECK(value<MinTrackSizingFunction>("min-content").is_min_content());
    CHECK(value<MaxTrackSizingFunction>("max-content").is_max_content());
    CHECK(value<MaxTrackSizingFunction>("fit-content(20px)") == MaxTrackSizingFunction::fit_content_px(20));
    CHECK(value<MaxTrackSizingFunction>("0fr") == MaxTrackSizingFunction::fr(0));
    CHECK(value<MaxTrackSizingFunction>("1.5fr") == MaxTrackSizingFunction::fr(1.5f));
    for (auto source : {"0", "1", "12PX", "AUTO", "1em", "calc(1px + 2px)", "var(--x)", "12 px", "12/**/px", "fit-content()", "fit-content(2px,3px)"}) rejects<Dimension>(source);
    for (auto source : {"auto","min-content","fit-content(2px)","1fr"}) rejects<LengthPercentage>(source);
    for (auto source : {"min-content","stretch","fit-content(2px)"}) rejects<LengthPercentageAuto>(source);
    for (auto source : {"1fr","fit-content(2px)","stretch","content"}) rejects<MinTrackSizingFunction>(source);
    for (auto source : {"-1fr","-0fr","fit-content","stretch","content","1FR"}) rejects<MaxTrackSizingFunction>(source);
}

TEST_CASE("native parser available space follows source not browser units", "[cpp][parse]") {
    for (auto s : {"12", "12px", "12em", "12unknown"}) CHECK(value<AvailableSpace>(s) == AvailableSpace::definite(12));
    CHECK(value<AvailableSpace>("min-content").is_min_content());
    CHECK(value<AvailableSpace>("max-content").is_max_content());
    CHECK(std::signbit(value<AvailableSpace>("-0").value()));
    for (auto s : {"auto","12%","-1","MIN-CONTENT","NaN"}) rejects<AvailableSpace>(s);
}

TEST_CASE("native parser scanner comments escapes strings and UTF8", "[cpp][parse]") {
    CHECK(value<Display>(R"(\67 rid)") == Display::Grid);
    CHECK(value<Display>(R"(g\000072id)") == Display::Grid);
    CHECK(value<Display>("\t\r\n\fGRID/**/") == Display::Grid);
    CHECK(value<std::string>(R"(\31 name)") == "1name");
    CHECK(value<std::string>(R"("a\22 b")") == "a\"b");
    CHECK(value<std::string>("'a\\\r\nb'") == "ab");
    CHECK(value<std::string>("'a\\\fb'") == "ab");
    CHECK(value<std::string>("'unterminated") == "unterminated");
    CHECK(value<std::string>("'end\\") == "end");
    CHECK(value<std::string>(R"(\0)") == "\xef\xbf\xbd");
    CHECK(value<std::string>(R"(\d800)") == "\xef\xbf\xbd");
    CHECK(value<std::string>(R"(\110000)") == "\xef\xbf\xbd");
    CHECK(value<std::string>(R"(\1f600)") == "\xf0\x9f\x98\x80");
    CHECK(value<std::string>("caf\xc3\xa9") == "caf\xc3\xa9");
    CHECK(value<std::string>(std::string("a\0b",3)) == "a\xef\xbf\xbd" "b");
    CHECK(value<Display>("grid/* EOF") == Display::Grid);
    rejects<Display>("gr/**/id"); rejects<Display>("grid//comment");
    rejects<std::string>("'bad\nstring'"); rejects<Display>("g\\\nrid");
    for (auto invalid : {std::string("\xc0\xaf",2), std::string("\xed\xa0\x80",3), std::string("\xf4\x90\x80\x80",4), std::string("\xe2\x82",2), std::string("\x80",1)}) rejects<std::string>(invalid);
    // EOF implicitly closes a supported function/block, following cssparser.
    CHECK(value<Dimension>("fit-content(4px") == Dimension::fit_content_px(4));
    CHECK(value<TemplateLineNames>("[a") == TemplateLineNames{{"a"}});
}

TEST_CASE("native parser complete consumption and exact byte errors", "[cpp][parse]") {
    error_at<Display>("  wobble", ParseErrorCode::InvalidValue, 2);
    error_at<Display>("grid /**/ nope", ParseErrorCode::InvalidSyntax, 10);
    error_at<LengthPercentage>("  2em", ParseErrorCode::InvalidValue, 2);
    error_at<Size<Dimension>>("1px /*x*/ 2em", ParseErrorCode::InvalidValue, 10);
    error_at<TrackSizingFunction>("minmax(1px, 2em)", ParseErrorCode::InvalidValue, 12);
    error_at<std::string>("'a\nb'", ParseErrorCode::InvalidSyntax, 2);
    error_at<std::string>(std::string("a \xc3x",4), ParseErrorCode::InvalidSyntax, 3);
    error_at<std::string>("\xc3\xa9 x", ParseErrorCode::InvalidSyntax, 3);
    error_at<Calc>("calc(1px)", ParseErrorCode::Unsupported, 0);
    error_at<LengthKind>("Length", ParseErrorCode::Unsupported, 0);
    CHECK(ParseError{ParseErrorCode::InvalidValue,3,"x"} == ParseError{ParseErrorCode::InvalidValue,3,"x"});
    for (auto s : {"", " ", "/**/", "grid;", "grid {}", "grid)", "grid !important"}) rejects<Display>(s);
}

TEST_CASE("native parser generic geometry and component order", "[cpp][parse]") {
    CHECK(value<Size<float>>("1") == Size<float>{1,1});
    CHECK(value<Size<float>>("1/**/2") == Size<float>{1,2});
    CHECK(value<Point<int>>("1 2") == Point<int>{1,2});
    CHECK(value<InBothAbsAxis<int>>("3 4") == InBothAbsAxis<int>{3,4});
    CHECK(value<Line<int>>("5") == Line<int>{5,5});
    CHECK(value<Rect<int>>("1") == Rect<int>{1,1,1,1});
    CHECK(value<Rect<int>>("1 2") == Rect<int>{2,2,1,1});
    CHECK(value<Rect<int>>("1 2 3") == Rect<int>{2,2,1,3});
    CHECK(value<Rect<int>>("1 2 3 4") == Rect<int>{4,2,1,3});
    CHECK(value<MinMax<int,float>>("MINMAX(1,2.5)") == MinMax<int,float>{1,2.5f});
    CHECK(value<std::vector<Dimension>>("fit-content(2px)/*x*/3px") == std::vector<Dimension>{Dimension::fit_content_px(2),Dimension::length(3)});
    CHECK(value<std::optional<float>>("auto") == std::nullopt);
    CHECK(value<std::optional<float>>("2") == 2);
    rejects<Size<float>>("1 2 3"); rejects<Rect<int>>("1 2 3 4 5");
    rejects<Line<int>>("1 / 2"); rejects<std::vector<float>>("");
    rejects<MinMax<int>>("minmax(1)"); rejects<MinMax<int>>("minmax(1,2,3)");
    rejects<MinMax<int>>("minmax(1,,2)"); rejects<MinMax<int>>("minmax(,2)");
    rejects<Size<Dimension>>("1px,2px");
}

TEST_CASE("native parser scalar numeric syntax and saturation distinctions", "[cpp][parse]") {
    for (auto [source, expected] : std::initializer_list<std::pair<std::string_view,float>>{{".5",.5f},{"+.5",.5f},{"-2.5e+2",-250},{"1E-2",.01f},{"+12",12}}) CHECK(value<float>(source) == expected);
    CHECK(std::signbit(value<float>("-0")));
    CHECK(std::isinf(value<float>("1e999")));
    CHECK(std::isinf(value<double>("-1e999")));
    CHECK(std::signbit(value<float>("-1e-999")));
    CHECK(value<double>("16777217") == 16777217.0);
    CHECK(value<float>("16777217") == 16777216.0f);
    CHECK(value<std::int16_t>("-32768") == -32768);
    CHECK(value<std::uint16_t>("65535") == 65535);
    CHECK(value<std::int32_t>("2147483647") == 2147483647);
    CHECK(value<std::uint32_t>("4294967295") == 4294967295u);
    CHECK(value<std::int64_t>("-9223372036854775808") == std::numeric_limits<std::int64_t>::min());
    CHECK(value<std::uint64_t>("+18446744073709551615") == std::numeric_limits<std::uint64_t>::max());
    CHECK(value<signed char>("127") == 127); CHECK(value<unsigned char>("255") == 255);
    for (auto s : {"NaN","inf","infinity","0x10","1.","1e","1px","1%","1,2","--1"}) rejects<float>(s);
    for (auto s : {"32768","-32769","1.0","1e0"}) rejects<std::int16_t>(s);
    for (auto s : {"65536","-1","-0"}) rejects<std::uint16_t>(s);
    rejects<std::int64_t>("9223372036854775808"); rejects<std::uint64_t>("18446744073709551616");
    rejects<signed char>("128"); rejects<unsigned char>("256");
    CHECK(value<GridLine>("999999999999999999999").value == 32767);
    CHECK(value<GridLine>("-999999999999999999999").value == -32768);
    CHECK(value<RepetitionCount>("99999999999999999999").value() == 65535);
    CHECK(value<GridPlacement>("span -3") == GridPlacement::span(0));
    CHECK(value<GridPlacement>("span 99999999999999999") == GridPlacement::span(65535));
    for (auto s : {"0","1.0","1e0"}) rejects<GridLine>(s);
}

TEST_CASE("native parser track functions repeats and positional names", "[cpp][parse]") {
    CHECK(value<TrackSizingFunction>("1fr") == TrackSizingFunction::fr(1));
    CHECK(value<TrackSizingFunction>("fit-content(20%)") == TrackSizingFunction::fit_content(LengthPercentage::percent(.2f)));
    auto m = value<TrackSizingFunction>("MINMAX(min-content,2fr)");
    CHECK(m.min.is_min_content()); CHECK(m.max == MaxTrackSizingFunction::fr(2));
    CHECK(value<MinMax<MinTrackSizingFunction,MaxTrackSizingFunction>>("minmax(1px,3fr)") == MinMax<MinTrackSizingFunction,MaxTrackSizingFunction>{MinTrackSizingFunction::length(1),MaxTrackSizingFunction::fr(3)});
    auto tracks = value<GridTemplateTracks<>>(R"([outer] 10px[mid]REPEAT(2,[a b]minmax(5px,1fr)[c]20%[d])[end])");
    REQUIRE(tracks.tracks.size() == 2);
    CHECK(tracks.line_names == TemplateLineNames{{"outer"},{"mid"},{"end"}});
    CHECK(std::get<TrackSizingFunction>(tracks.tracks[0].value) == TrackSizingFunction::length(10));
    const auto& r = std::get<GridTemplateRepetition>(tracks.tracks[1].value);
    CHECK(r.count == RepetitionCount::count(2)); CHECK(r.tracks.size() == 2);
    CHECK(r.line_names == TemplateLineNames{{"a","b"},{"c"},{"d"}});
    CHECK(value<GridTemplateRepetition>("repeat(auto-fit,10px)").count == RepetitionCount::auto_fit());
    CHECK(value<GridTemplateComponent>("repeat(auto-fill,1fr)").is_auto_repetition());
    auto unnamed = value<GridTemplateTracks<TrackSizingFunction>>("1px 2fr");
    CHECK(unnamed.line_names == TemplateLineNames{{},{},{}});
    CHECK(value<TemplateLineNames>(R"([\61  b][][c])") == TemplateLineNames{{"a","b"},{},{"c"}});
    CHECK(value<GridAutoTracks>("1px minmax(auto,1fr)").size() == 2);
    for (auto s : {"0","-1","1.0","1e0","AUTO-FIT"}) rejects<RepetitionCount>(s);
    for (auto s : {"minmax(1fr,2fr)","minmax(1px)","minmax(1px,2px,3px)","fit-content(1fr)"}) rejects<TrackSizingFunction>(s);
    for (auto s : {"repeat(0,1px)","repeat(2,)","repeat(2,[a])","repeat(2,repeat(2,1px))","repeat(2,1px,2px)"}) rejects<GridTemplateRepetition>(s);
    for (auto s : {"", "none", "[a]", "1px [a][b] 2px", "[1] 1px"}) rejects<GridTemplateTracks<>>(s);
    rejects<GridTemplateRepetition>("1px"); rejects<GridAutoTracks>("repeat(2,1px)");
}

TEST_CASE("native parser placement permutations shorthand fallback and bounds", "[cpp][parse]") {
    for (auto s : {"span 2 a","2 span a","a 2 span","span a 2","2 a span","a span 2"}) CHECK(value<GridPlacement>(s) == GridPlacement::named_span("a",2));
    CHECK(value<GridPlacement>("span") == GridPlacement::span(0));
    CHECK(value<GridPlacement>("a") == GridPlacement::named_line("a"));
    CHECK(value<GridPlacement>("a -2") == GridPlacement::named_line("a",-2));
    CHECK(value<GridPlacement>("SPAN") == GridPlacement::named_line("SPAN"));
    CHECK(value<GridPlacement>("auto").is_auto());
    CHECK(value<GridPlacement>("999999999999") == GridPlacement::line(32767));
    CHECK(value<Line<GridPlacement>>("a") == Line<GridPlacement>{GridPlacement::named_line("a"),GridPlacement::named_line("a")});
    CHECK(value<Line<GridPlacement>>("2") == Line<GridPlacement>{GridPlacement::line(2),GridPlacement::auto_()});
    CHECK(value<Line<GridPlacement>>("span 2") == Line<GridPlacement>{GridPlacement::span(2),GridPlacement::auto_()});
    CHECK(value<Line<GridPlacement>>("a 2") == Line<GridPlacement>{GridPlacement::named_line("a",2),GridPlacement::auto_()});
    CHECK(value<Line<GridPlacement>>("auto/span 2") == Line<GridPlacement>{GridPlacement::auto_(),GridPlacement::span(2)});
    for (auto s : {"0","span 0","auto 2","a b","1 2","span span","2.0","1e0","1 / 2"}) rejects<GridPlacement>(s);
    for (auto s : {"/2","1/","1/2/3"}) rejects<Line<GridPlacement>>(s);
}

TEST_CASE("native parser rectangular areas cell tokenization and ownership", "[cpp][parse]") {
    auto areas = value<GridTemplateAreas>("\"head head\"/*x*/'side main' 'side main'");
    CHECK(areas.row_count == 3); CHECK(areas.column_count == 2);
    CHECK(areas.areas == std::vector<GridTemplateArea>{{"head",1,2,1,3},{"side",2,4,1,2},{"main",2,4,2,3}});
    auto adjacent = value<GridTemplateAreas>("'a...b' 'a.b'");
    CHECK(adjacent.column_count == 3); CHECK(adjacent.row_count == 2);
    CHECK(adjacent.areas == std::vector<GridTemplateArea>{{"a",1,3,1,2},{"b",1,3,3,4}});
    CHECK(value<GridTemplateAreas>("'... ..'").areas.empty());
    CHECK(value<GridTemplateAreas>("NONE") == GridTemplateAreas{});
    CHECK(value<GridTemplateAreas>(R"('\61  \61')").areas[0].name == "a");
    for (auto s : {"", "''", "'a b' 'a'", "'a a' 'a .'", "'a . a'", "'a !'", "a", "none 'a'"}) rejects<GridTemplateAreas>(s);
    auto owned = [] { std::string source = "[hello] 1px [world]"; return value<GridTemplateTracks<>>(source); }();
    CHECK(owned.line_names == TemplateLineNames{{"hello"},{"world"}});
    auto name = [] { std::string source = "my-name"; return value<GridPlacement>(source); }();
    CHECK(name.name() == "my-name");
}

TEST_CASE("native parser every Style property isolated and in declarations", "[cpp][parse]") {
    Style accumulated, expected_all;
    std::string declarations;
    for (const auto& c : properties()) {
        INFO("property " << c.name << ": " << c.source);
        Style actual, expected;
        c.expected(expected);
        auto result = parse_style_property(actual,c.name,c.source);
        REQUIRE(result.has_value()); CHECK(actual == expected);
        CHECK(value<Style>(std::string(c.name)+":"+std::string(c.source)) == expected);
        REQUIRE(parse_style_property(accumulated,c.name,c.source).has_value());
        c.expected(expected_all);
        declarations += std::string(c.name)+":"+std::string(c.source)+";";
    }
    CHECK(accumulated == expected_all);
    CHECK(value<Style>(declarations) == expected_all);
    CHECK(value<Style>("") == Style{});
    CHECK(value<Style>(" ; /**/ ; ") == Style{});
    CHECK(value<Style>(R"(\77 idth: 12px; DISPLAY: GRID)").size.width == Dimension::length(12));
}

TEST_CASE("native parser property failure is atomic across every property", "[cpp][parse]") {
    Style seed;
    for (const auto& c : properties()) { c.expected(seed); }
    for (const auto& c : properties()) {
        INFO("atomic property " << c.name);
        Style actual = seed;
        auto result = parse_style_property(actual,c.name,"???");
        REQUIRE_FALSE(result.has_value()); CHECK(actual == seed);
        CHECK_FALSE(result.error().message.empty());
    }
    for (auto [name, source] : std::initializer_list<std::pair<std::string_view,std::string_view>>{
            {"margin","1px 2px 3px bad"},{"flex-flow","column wrap wrap"},
            {"grid-template-columns","[a] 1px [b] invalid"},{"place-items","end safe stretch"},
            {"grid-area","1/2/3/4/5"},{"flex","2 3 4px extra"},{"aspect-ratio","2/0"},
            {"unknown","1px"},{"--custom","1px"},{"width extra","1px"}}) {
        Style actual = seed;
        REQUIRE_FALSE(parse_style_property(actual,name,source).has_value()); CHECK(actual == seed);
    }
}

TEST_CASE("native parser declaration source order importance and shorthand overlap", "[cpp][parse]") {
    auto s = value<Style>("margin:1px!important; margin-left:2px; margin-top:3px!important; margin:4px;");
    CHECK(s.margin == Rect<LengthPercentageAuto>::from_length(1,1,3,1));
    s = value<Style>("margin-left:2px!important; margin:1px!important; margin-left:3px!important");
    CHECK(s.margin == Rect<LengthPercentageAuto>::from_length(3,1,1,1));
    s = value<Style>("border-left-width:9px!important;border:2px;border-width:3px!important;border-right:4px");
    CHECK(s.border == Rect<LengthPercentage>::splat(LengthPercentage::length(3)));
    s = value<Style>("gap:1px 2px!important;row-gap:3px;column-gap:4px!important");
    CHECK(s.gap == Size<LengthPercentage>{LengthPercentage::length(4),LengthPercentage::length(1)});
    s = value<Style>("flex-grow:9!important;flex:none;flex-shrink:2!important;flex:3 4 5px");
    CHECK(s.flex_grow == 9); CHECK(s.flex_shrink == 2); CHECK(s.flex_basis == Dimension::length(5));
    s = value<Style>("grid-area:a!important;grid-row:1/2;grid-column-end:b!important");
    CHECK(s.grid_row == Line<GridPlacement>{GridPlacement::named_line("a"),GridPlacement::named_line("a")});
    CHECK(s.grid_column.end == GridPlacement::named_line("b"));
    s = value<Style>("width:1px !/**/ImPoRtAnT; width:2px; width:3px !\\69mportant");
    CHECK(s.size.width == Dimension::length(3));
    s = value<Style>("grid-template-rows:[a]1px[b]!important;grid-template-row-names:[c][d];grid-template-rows:2px");
    CHECK(s.grid_template_row_names == TemplateLineNames{{"a"},{"b"}});
    CHECK(s.grid_template_rows == std::vector<GridTemplateComponent>{GridTemplateComponent::length(1)});
    CHECK(value<Style>("width:1px;width:2px").size.width == Dimension::length(2));
}

TEST_CASE("native parser invalid declaration lists fail rather than recover", "[cpp][parse]") {
    for (auto s : {"width 1px", "width:", ":1px", "width:1px; bad:2px", "--custom:1", "a{width:1px}", "@media x{}", "width:1px!important nope", "width:1px!!important", "width:var(--x)", "width:calc(1px + 2px)", "width:1px!important;width:bad", "padding:1px 2px bad", "display:initial", "display:inherit", "display:unset"}) rejects<Style>(s);
    error_at<Style>("width:1px; unknown:2px", ParseErrorCode::InvalidValue, 11);
    error_at<Style>("width:1px; height: 2em", ParseErrorCode::InvalidValue, 19);
    error_at<Style>("width:1px; height 2px", ParseErrorCode::InvalidSyntax, 18);
    // Semicolons and colons inside a string are component content, not declarations.
    rejects<Style>("grid-template-areas:'a;b';width:1px");
    CHECK(value<Style>("width:fit-content(4px);/* ; : */height:2px").size.height == Dimension::length(2));
}

TEST_CASE("native parser shorthand defaulting and explicit resets", "[cpp][parse]") {
    auto s = value<Style>("flex:2");
    CHECK(s.flex_grow == 2); CHECK(s.flex_shrink == 1); CHECK(s.flex_basis == Dimension::percent(0));
    s = value<Style>("flex:none"); CHECK(s.flex_grow == 0); CHECK(s.flex_shrink == 0); CHECK(s.flex_basis.is_auto());
    s = value<Style>("flex:initial"); CHECK(s.flex_grow == 0); CHECK(s.flex_shrink == 1); CHECK(s.flex_basis.is_auto());
    s = value<Style>("flex:auto"); CHECK(s.flex_grow == 1); CHECK(s.flex_shrink == 1); CHECK(s.flex_basis.is_auto());
    s = value<Style>("flex:4px 2 3"); CHECK(s.flex_grow == 2); CHECK(s.flex_shrink == 3); CHECK(s.flex_basis == Dimension::length(4));
    s = value<Style>("flex:1 1 0"); CHECK(s.flex_basis == Dimension::zero());
    s = value<Style>("flex-direction:column;flex-flow:wrap"); CHECK(s.flex_direction == FlexDirection::Row); CHECK(s.flex_wrap == FlexWrap::Wrap);
    s = value<Style>("flex-wrap:wrap;flex-flow:column"); CHECK(s.flex_wrap == FlexWrap::NoWrap);
    s = value<Style>("place-items:safe center;place-content:space-around;place-self:auto");
    CHECK(s.align_items == s.justify_items); CHECK(s.align_content == s.justify_content);
    CHECK_FALSE(s.align_self); CHECK_FALSE(s.justify_self);
    s = value<Style>("grid-area:a"); CHECK(s.grid_row.start == s.grid_row.end); CHECK(s.grid_row == s.grid_column);
    s = value<Style>("grid-area:1/2"); CHECK(s.grid_row.end.is_auto()); CHECK(s.grid_column.end.is_auto());
    s = value<Style>("grid-template-rows:[a]1px[b];grid-template-rows:none;grid-template-areas:'a';grid-template-areas:none;aspect-ratio:2;aspect-ratio:auto");
    CHECK(s.grid_template_rows.empty()); CHECK(s.grid_template_row_names.empty()); CHECK_FALSE(s.grid_template_areas); CHECK_FALSE(s.aspect_ratio);
    for (auto source : {"flex:-1", "flex:1 -1", "flex:1 2 3", "aspect-ratio:-1/2", "aspect-ratio:1/0", "aspect-ratio:1/1e-999"}) rejects<Style>(source);
    CHECK(value<Style>("aspect-ratio:0/2").aspect_ratio == 0);
}

TEST_CASE("native parser XML scalar fields preserve nonfinite and signed values", "[cpp][parse]") {
    for (auto field : {"scrollbar-width","flex-grow","flex-shrink","aspect-ratio"}) {
        auto scalar = [&](std::string_view source) {
            Style s;
            auto result = parse_style_property(s,field,source);
            REQUIRE(result.has_value());
            if (std::string_view(field) == "scrollbar-width") return s.scrollbar_width;
            if (std::string_view(field) == "flex-grow") return s.flex_grow;
            if (std::string_view(field) == "flex-shrink") return s.flex_shrink;
            REQUIRE(s.aspect_ratio.has_value()); return *s.aspect_ratio;
        };
        INFO("native scalar property " << field);
        for (auto source : {"NaN","nan","+NaN","-NaN"}) CHECK(std::isnan(scalar(source)));
        for (auto source : {"inf","+inf","Infinity","1e999"}) CHECK(scalar(source) == std::numeric_limits<float>::infinity());
        for (auto source : {"-inf","-Infinity","-1e999"}) CHECK(scalar(source) == -std::numeric_limits<float>::infinity());
        CHECK(scalar("-2") == -2); CHECK(std::signbit(scalar("-0")));
        CHECK(scalar("1e-999") == 0); CHECK(std::signbit(scalar("-1e-999")));
        CHECK(scalar("0e999") == 0);
        CHECK(scalar("1.") == 1); // Rust FromStr accepts a trailing decimal point.
        CHECK(scalar("1.0000000596046447753906250000000000000001") == std::nextafter(1.0f,2.0f));
        CHECK(scalar("/*comment*/2") == 2);
    }
    // CSS token numbers deliberately differ from native scalar FromStr conversion.
    CHECK(value<float>("1.0000000596046447753906250000000000000001") == 1.0f);
    CHECK(std::isnan(value<float>("0e999"))); // cssparser's mantissa * 10^exponent.
    CHECK(std::isinf(value<Dimension>("1e999px").value()));
    CHECK(std::isnan(value<Dimension>("0e999px").value()));
    CHECK(value<Style>("min-width:auto;flex-line-count:0").flex_line_count == 0);
    rejects<Style>("min-width:min-content"); rejects<Style>("flex-line-count:65536");
}

TEST_CASE("native scalar trivia preserves exact Rust scalar spelling", "[cpp][parse]") {
    for (std::string_view field : {"scrollbar-width", "flex-grow", "flex-shrink", "aspect-ratio"}) {
        auto stored = [&](const Style& s) {
            if (field == "scrollbar-width") return s.scrollbar_width;
            if (field == "flex-grow") return s.flex_grow;
            if (field == "flex-shrink") return s.flex_shrink;
            return s.aspect_ratio.value();
        };
        for (auto source : {"0e999", "-0e999", "1.0000000596046447753906250000000000000001",
                            "1.", "+1.", "-0.", "1.e2", "1e999", "-1e999",
                            "1e-999", "-1e-999", "NaN", "-NaN", "+inf", "-Infinity"}) {
            Style plain;
            REQUIRE(parse_style_property(plain, field, source).has_value());
            for (const auto& [before, after] : std::array<std::pair<std::string_view, std::string_view>, 5>{{
                    {"/**/", ""}, {"", "/**/"}, {" \t/**/\n", "\r/*after*/\f"},
                    {"/*one*//**/", "/*two*//**/"}, {"/**/", "/* EOF"}}}) {
                const auto decorated = std::string(before) + source + std::string(after);
                INFO(field << ": " << decorated);
                Style actual;
                REQUIRE(parse_style_property(actual, field, decorated).has_value());
                CHECK(std::bit_cast<std::uint32_t>(stored(actual)) ==
                      std::bit_cast<std::uint32_t>(stored(plain)));
                const auto declaration = parse_style(std::string(field) + ":" + decorated);
                REQUIRE(declaration.has_value());
                CHECK(std::bit_cast<std::uint32_t>(stored(*declaration)) ==
                      std::bit_cast<std::uint32_t>(stored(plain)));
            }
        }
        for (auto source : {"1/**/.", "1. /**/0", "1/**/e2", "1e/**/2", "+/**/1",
                            "-/**/0", "+/**/inf", "in/**/f", "1 2", "+-1", "++1", R"(\69 nf)"}) {
            INFO(field << ": " << source);
            Style actual;
            const Style original = actual;
            REQUIRE_FALSE(parse_style_property(actual, field, source).has_value());
            CHECK(actual == original);
            REQUIRE_FALSE(parse_style(std::string(field) + ":" + source).has_value());
        }
    }
}

TEST_CASE("native flex-flow errors retain original source spans", "[cpp][parse]") {
    for (auto source : {"wrap /**/ wrap", R"(wr\61 p /* longer */ wrap)",
                        "column /**/ wrap /*x*/ wrap", "nowrap /**/ balance",
                        "balance /**/ balance", "wrap /*x*/ column /**/ wrap"}) {
        const std::string_view text(source);
        const auto duplicate = text.find_last_of(' ') + 1;
        Style style;
        const Style original = style;
        const auto direct = parse_style_property(style, "flex-flow", text);
        REQUIRE_FALSE(direct.has_value());
        CHECK(direct.error().offset == duplicate);
        CHECK(style == original);
        const std::string declaration = "flex-flow:" + std::string(text);
        const auto parsed = parse_style(declaration);
        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error().offset == 10 + duplicate);
    }
    error_at<Style>("flex-flow:wrap /**/ wrap", ParseErrorCode::InvalidValue, 20);
    const auto s = value<Style>(R"(flex-flow:wr\61 p /**/ balance /*x*/ column)");
    CHECK(s.flex_wrap == FlexWrap::Balance);
    CHECK(s.flex_direction == FlexDirection::Column);
}

TEST_CASE("native parser named convenience APIs agree with public template", "[cpp][parse]") {
#define SAME(Name, Type, Source) CHECK(parse_##Name(Source) == parse_css<Type>(Source))
    SAME(number,float,"2"); SAME(boolean,bool,"true");
    SAME(dimension,Dimension,"auto"); SAME(length_percentage,LengthPercentage,"2px");
    SAME(length_percentage_auto,LengthPercentageAuto,"auto"); SAME(available_space,AvailableSpace,"20");
    SAME(display,Display,"grid"); SAME(position,Position,"fixed"); SAME(box_sizing,BoxSizing,"border-box");
    SAME(direction,Direction,"rtl"); SAME(overflow,Overflow,"clip"); SAME(contain,Contain,"content");
    SAME(align_items,AlignItems,"center"); SAME(align_self,AlignSelf,"start");
    SAME(justify_items,JustifyItems,"end"); SAME(justify_self,JustifySelf,"center");
    SAME(align_content,AlignContent,"center"); SAME(justify_content,JustifyContent,"center");
    SAME(text_align,TextAlign,"auto"); SAME(float,Float,"left"); SAME(clear,Clear,"both");
    SAME(flex_direction,FlexDirection,"column"); SAME(flex_wrap,FlexWrap,"balance");
    SAME(grid_auto_flow,GridAutoFlow,"row dense"); SAME(min_track_sizing_function,MinTrackSizingFunction,"auto");
    SAME(max_track_sizing_function,MaxTrackSizingFunction,"2fr"); SAME(track_sizing_function,TrackSizingFunction,"1fr");
    SAME(repetition_count,RepetitionCount,"2"); SAME(grid_placement,GridPlacement,"span 2");
    SAME(grid_line,GridLine,"-2"); SAME(grid_template_repetition,GridTemplateRepetition,"repeat(2,1px)");
    SAME(grid_template_component,GridTemplateComponent,"2px"); SAME(grid_template_tracks,GridTemplateTracks<>,"1px");
    SAME(grid_auto_tracks,GridAutoTracks,"1px"); SAME(grid_template_areas,GridTemplateAreas,"'a'");
#undef SAME
    CHECK(parse_style("width:2px") == parse_css<Style>("width:2px"));
}

TEST_CASE("native parser representable area bounds and large lexical input", "[cpp][parse]") {
    // One-based exclusive end lines must fit u16, not wrap to zero.
    std::string row = "'";
    for (int i = 0; i < 65534; ++i) row += ". ";
    row += '\'';
    auto areas = value<GridTemplateAreas>(row);
    CHECK(areas.column_count == 65534); CHECK(areas.row_count == 1);
    row.insert(row.size() - 1,". ");
    rejects<GridTemplateAreas>(row);
    std::string rows;
    for (int i = 0; i < 65534; ++i) rows += "'.'";
    CHECK(value<GridTemplateAreas>(rows).row_count == 65534);
    rows += "'.'"; rejects<GridTemplateAreas>(rows);
    CHECK(value<RepetitionCount>(std::string(10000,'9')).value() == 65535);
    CHECK(value<GridPlacement>("-" + std::string(10000,'9')) == GridPlacement::line(-32768));
    CHECK(std::isinf(value<float>(std::string(10000,'9'))));
    auto escaped = value<std::string>(R"(\000061\000062\000063)");
    CHECK(escaped == "abc");
}

TEST_CASE("native parser property trivia and declarations retain simple values", "[cpp][parse]") {
    for (const auto& c : properties()) {
        INFO("trivia around " << c.name);
        Style expected, actual;
        c.expected(expected);
        REQUIRE(parse_style_property(actual,"/**/" + std::string(c.name) + "/**/",
                                     "/*before*/" + std::string(c.source) + "/*after*/").has_value());
        CHECK(actual == expected);
        const auto source = std::string(c.name) + ":/**/" + std::string(c.source) + "/*x*/ !/**/important;";
        CHECK(value<Style>(source) == expected);
    }
    // Named self parsers return AlignItems, not optional self alignment.
    CHECK_FALSE(parse_align_self("auto").has_value());
    CHECK(value<std::optional<AlignItems>>("/*x*/auto/*y*/") == std::nullopt);
    // Structural validation is deliberately separate from parsing.
    auto style = value<Style>("grid-template-rows:1px;grid-template-row-names:[a][b][c]");
    CHECK(style.grid_template_row_names.size() == 3);
}

TEST_CASE("native parser deterministic malformed input safety and offsets", "[cpp][parse]") {
    std::uint32_t state = 0x91f03a21;
    constexpr std::string_view alphabet = "abc012+-.%pxfr()[]{}'\"\\/*:;! \n\r\t";
    auto verify = [](const auto& parsed, std::size_t length) {
        if (!parsed) { CHECK(parsed.error().offset <= length); CHECK_FALSE(parsed.error().message.empty()); }
    };
    for (int i = 0; i < 3000; ++i) {
        std::string source;
        state = state * 1664525u + 1013904223u;
        const auto length = state % 65;
        for (unsigned j = 0; j < length; ++j) {
            state = state * 1664525u + 1013904223u;
            source += alphabet[state % alphabet.size()];
        }
        verify(parse_css<Dimension>(source),source.size());
        verify(parse_css<GridTemplateTracks<>>(source),source.size());
        verify(parse_css<GridTemplateAreas>(source),source.size());
        verify(parse_css<Rect<LengthPercentageAuto>>(source),source.size());
        verify(parse_style(source),source.size());
    }
    // Iterative scanner must not overflow the stack on deeply nested unknown functions.
    std::string nested(20000,'('); nested += '1'; nested.append(20000,')');
    rejects<Dimension>(nested); rejects<Style>("width:" + nested);
}
