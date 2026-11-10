// Native std-only tests. Expected values follow reference/upstream (pinned Taffy 0.15.1),
// especially geometry.rs, style/, tree/cache.rs, tree/layout.rs and compute/leaf.rs.
#include <catch2/catch_test_macros.hpp>
#include <taffy/cache.hpp>
#include <taffy/parse.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <taffy/tree.hpp>
#include <taffy/compute.hpp>
#include "../src/compute_common.hpp"
#include "../src/grid/placement_types.hpp"
#include "../src/oof.hpp"
#include "../src/sizing.hpp"
#include <random>
#include <memory>

namespace {
using namespace taffy;
using Opt = std::optional<float>;
constexpr auto horizontal = AbsoluteAxis::Horizontal;
constexpr auto vertical = AbsoluteAxis::Vertical;
constexpr float inf = std::numeric_limits<float>::infinity();
constexpr float nan = std::numeric_limits<float>::quiet_NaN();

// Invalid cross-domain constructions must be rejected at compile time, not silently
// coerced to a keyword. Grid conversions intentionally are broader than CSS lengths.
template<class T> concept HasAuto = requires { T::auto_(); };
template<class T> concept HasFr = requires { T::fr(1.0f); };
template<class T> concept HasFit = requires { T::fit_content(LengthPercentage::length(1)); };
static_assert(!HasAuto<LengthPercentage>);
static_assert(HasAuto<LengthPercentageAuto> && HasAuto<Dimension>);
static_assert(HasFr<MaxTrackSizingFunction> && !HasFr<MinTrackSizingFunction> && !HasFr<Dimension>);
static_assert(HasFit<Dimension> && HasFit<MaxTrackSizingFunction> && !HasFit<MinTrackSizingFunction>);
static_assert(std::is_constructible_v<Dimension, LengthPercentage>);
static_assert(std::is_constructible_v<Dimension, LengthPercentageAuto>);
static_assert(!std::is_constructible_v<LengthPercentage, Dimension>);
static_assert(!std::is_constructible_v<LengthPercentageAuto, Dimension>);
static_assert(std::is_constructible_v<MinTrackSizingFunction, MaxTrackSizingFunction>);
static_assert(std::is_same_v<decltype(Size<int>{} + Size<float>{}), Size<float>>);
static_assert(Size<int>{2, 5}.transpose() == Size<int>{5, 2});
static_assert(Rect<int>{2, 3, 5, 7}.sum_axes() == Size<int>{5, 12});
static_assert(GridLine(-1).into_origin_zero_line(4) == 4);
static_assert(Dimension::percent(0.5f).uses_percentage());

template<class T> T parsed(std::string_view css) {
    auto result = parse_css<T>(css);
    INFO(css);
    REQUIRE(result.has_value());
    return *result;
}
template<class T> void parse_error(std::string_view css, ParseErrorCode code, std::size_t offset = 0) {
    auto result = parse_css<T>(css);
    INFO(css);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == code);
    CHECK(result.error().offset == offset);
    CHECK_FALSE(result.error().message.empty());
}
LayoutInput measure_key(float width) {
    LayoutInput result;
    result.run_mode = RunMode::ComputeSize;
    result.available_space = {AvailableSpace::definite(width), AvailableSpace::max_content()};
    return result;
}
void check_size(const LayoutOutput& output, float width, float height) {
    CHECK(output.size.width == width);
    CHECK(output.size.height == height);
}

TEST_CASE("native geometry axes, construction and component mapping", "[cpp][foundation][geometry]") {
    CHECK(other_axis(horizontal) == vertical);
    CHECK(other_axis(vertical) == horizontal);
    CHECK(other(AbstractAxis::Inline) == AbstractAxis::Block);
    CHECK(other(AbstractAxis::Block) == AbstractAxis::Inline);
    CHECK(as_abs_naive(AbstractAxis::Inline) == horizontal);
    CHECK(as_abs_naive(AbstractAxis::Block) == vertical);
    InBothAbsAxis<int> axes{3, 7};
    axes.set(vertical, 11);
    CHECK(axes.get(horizontal) == 3);
    CHECK(std::as_const(axes).get(vertical) == 11);
    CHECK(Size<int>::ZERO == Size<int>{0, 0});
    CHECK(Point<float>::ZERO == Point<float>{0, 0});
    CHECK(Rect<int>::ZERO == Rect<int>{0, 0, 0, 0});
    CHECK(Size<Opt>::NONE == Size<Opt>{});
    CHECK(Point<Opt>::none() == Point<Opt>{});
    CHECK(Rect<Opt>::NONE == Rect<Opt>{});
    CHECK(Line<bool>::TRUE == Line<bool>{true, true});
    CHECK(Line<bool>::FALSE == Line<bool>{false, false});
    auto size = Size<int>::new_(3, 7);
    CHECK(size.transpose() == Size<int>{7, 3});
    CHECK(size.to_point() == Point<int>{3, 7});
    CHECK(Size<int>::from_point({5, 9}) == Size<int>{5, 9});
    CHECK(size.map([](int x) { return x * 2; }) == Size<int>{6, 14});
    CHECK(size.map_width([](int x) { return x + 1; }) == Size<int>{4, 7});
    CHECK(size.map_height([](int x) { return x - 1; }) == Size<int>{3, 6});
    CHECK(size.zip_map(Size<int>{10, 20}, [](int a, int b) { return b - a; }) == Size<int>{7, 13});
    CHECK(size.with(AbstractAxis::Inline, 8) == Size<int>{8, 7});
    size.set(AbstractAxis::Block, 12);
    CHECK(size.get_abs(vertical) == 12);
    CHECK(size.get(AbstractAxis::Inline) == 3);
    CHECK(Size<int>::splat(4) == Size<int>{4, 4});
    CHECK(Size<float>{3, 4}.has_non_zero_area());
    CHECK_FALSE(Size<float>{0, 4}.has_non_zero_area());
    CHECK_FALSE(Size<float>{-1, 4}.has_non_zero_area());
    CHECK_FALSE(Size<float>{nan, 4}.has_non_zero_area());
    Point<int> point = Point<int>::new_(2, 5);
    point.set(AbstractAxis::Block, 8);
    CHECK(point.transpose() == Point<int>{8, 2});
    CHECK(point.to_size() == Size<int>{2, 8});
    CHECK(point.map([](int x) { return -x; }) == Point<int>{-2, -8});
    CHECK(point.zip_map(Point<int>{3, 4}, [](int a, int b) { return a + b; }) == Point<int>{5, 12});
    CHECK(Line<int>{3, 7}.sum() == 10);
    CHECK(Line<int>::splat(6).map([](int x) { return x / 2; }) == Line<int>{3, 3});
    CHECK(Line<int>{2, 5}.zip_map(Line<int>{3, 7}, [](int a, int b) { return a * b; }) == Line<int>{6, 35});
}

TEST_CASE("native geometry physical edges never reverse with flex direction", "[cpp][foundation][geometry]") {
    const Rect<int> rect{2, 3, 5, 7};
    CHECK(rect.transpose() == Rect<int>{5, 7, 2, 3});
    CHECK(rect.horizontal_components() == Line<int>{2, 3});
    CHECK(rect.vertical_components() == Line<int>{5, 7});
    CHECK(rect.sum_axes() == Size<int>{5, 12});
    CHECK(rect.axis_sum(AbstractAxis::Inline) == 5);
    CHECK(rect.axis_sum(vertical) == 12);
    CHECK(rect.map([](int x) { return x + 1; }) == Rect<int>{3, 4, 6, 8});
    CHECK(rect.zip_size(Size<int>{10, 20}, [](int a, int b) { return a + b; }) == Rect<int>{12, 13, 25, 27});
    CHECK(rect.zip_map(Rect<int>::splat(2), [](int a, int b) { return a * b; }) == Rect<int>{4, 6, 10, 14});
    for (auto direction : {FlexDirection::Row, FlexDirection::RowReverse, FlexDirection::Column, FlexDirection::ColumnReverse}) {
        const bool row = direction == FlexDirection::Row || direction == FlexDirection::RowReverse;
        INFO(static_cast<int>(direction));
        CHECK(rect.main_start(direction) == (row ? 2 : 5));
        CHECK(rect.main_end(direction) == (row ? 3 : 7));
        CHECK(rect.cross_start(direction) == (row ? 5 : 2));
        CHECK(rect.cross_end(direction) == (row ? 7 : 3));
        CHECK(rect.main_axis_sum(direction) == (row ? 5 : 12));
        CHECK(rect.cross_axis_sum(direction) == (row ? 12 : 5));
        const Size<int> size{11, 13};
        CHECK(size.main(direction) == (row ? 11 : 13));
        CHECK(size.cross(direction) == (row ? 13 : 11));
        CHECK(size.with_main(direction, 20) == (row ? Size<int>{20, 13} : Size<int>{11, 20}));
        CHECK(size.with_cross(direction, 30) == (row ? Size<int>{11, 30} : Size<int>{30, 13}));
        CHECK(size.map_main(direction, [](int x) { return x + 1; }) == (row ? Size<int>{12, 13} : Size<int>{11, 14}));
        CHECK(size.map_cross(direction, [](int x) { return x * 2; }) == (row ? Size<int>{11, 26} : Size<int>{22, 13}));
        CHECK(Point<int>{11, 13}.main(direction) == (row ? 11 : 13));
        CHECK(Point<int>{11, 13}.cross(direction) == (row ? 13 : 11));
        CHECK(Size<Opt>::from_cross(direction, 8) == (row ? Size<Opt>{std::nullopt, 8} : Size<Opt>{8, std::nullopt}));
    }
    CHECK(Rect<float>{2, 4, 3, 8}.union_({1, 3, 5, 10}) == Rect<float>{1, 4, 3, 10});
    CHECK(Size<int>{2, 5} + Size<float>{0.5f, 1.5f} == Size<float>{2.5f, 6.5f});
    CHECK(Size<int>{2, 5} - Size<int>{4, 1} == Size<int>{-2, 4});
    CHECK(3 * Size<int>{2, 5} == Size<int>{6, 15});
    CHECK(Size<int>{6, 10} / 2 == Size<int>{3, 5});
    CHECK(Point<int>{2, 4} * 3 == Point<int>{6, 12});
    CHECK(Point<int>{6, 12} / 3 == Point<int>{2, 4});
    CHECK(Point<int>{2, 4} + Point<int>{3, 7} == Point<int>{5, 11});
    CHECK(Point<int>{2, 4} - Point<int>{3, 7} == Point<int>{-1, -3});
    CHECK(Rect<int>{2, 3, 5, 7} + Rect<int>::splat(1) == Rect<int>{3, 4, 6, 8});
    CHECK(Rect<int>{2, 3, 5, 7} - Rect<int>::splat(1) == Rect<int>{1, 2, 4, 6});
    CHECK(Line<int>{2, 5} + Line<int>{3, 7} == Line<int>{5, 12});
    CHECK(Line<int>{2, 5} - Line<int>{3, 7} == Line<int>{-1, -2});
}

TEST_CASE("native optional geometry and percentage resolution", "[cpp][foundation][geometry]") {
    const Size<Opt> one{12, std::nullopt};
    CHECK_FALSE(one.both_axis_defined());
    CHECK(Size<Opt>{0, 0}.both_axis_defined());
    CHECK(one.unwrap_or(Size<float>{1, 2}) == Size<float>{12, 2});
    CHECK(one.or_(Size<Opt>{3, 5}) == Size<Opt>{12, 5});
    CHECK(one.maybe_apply_aspect_ratio(3) == Size<Opt>{12, 4});
    CHECK(Size<Opt>{std::nullopt, 4}.maybe_apply_aspect_ratio(3) == Size<Opt>{12, 4});
    CHECK(Size<Opt>{9, 4}.maybe_apply_aspect_ratio(3) == Size<Opt>{9, 4});
    CHECK(Size<Opt>{}.maybe_apply_aspect_ratio(3) == Size<Opt>{});
    CHECK(one.maybe_apply_aspect_ratio(std::nullopt) == Size<Opt>{12, std::nullopt});
    CHECK(std::isinf(*one.maybe_apply_aspect_ratio(0).height));
    CHECK(Size<float>{nan, 4}.f32_min({3, nan}) == Size<float>{3, 4});
    CHECK(Size<float>{nan, 4}.f32_max({3, nan}) == Size<float>{3, 4});
    CHECK(one.maybe_add(Size<Opt>{2, 5}) == Size<Opt>{14, std::nullopt});
    CHECK(one.maybe_sub(Size<Opt>{2, 5}) == Size<Opt>{10, std::nullopt});
    CHECK(one.maybe_min(Size<Opt>{8, 5}) == Size<Opt>{8, std::nullopt});
    CHECK(one.maybe_max(Size<Opt>{18, 5}) == Size<Opt>{18, std::nullopt});
    CHECK(one.maybe_clamp(Size<Opt>{15, 1}, Size<Opt>{10, 9}) == Size<Opt>{15, std::nullopt});
    CHECK(Size<Dimension>::from_lengths(3, 7).into_options() == Size<Opt>{3, 7});
    CHECK(Size<Dimension>::from_percent(0.5f, 0.25f).maybe_resolve(Size<Opt>{80, 120}) == Size<Opt>{40, 30});
    CHECK(Size<Dimension>::auto_().resolve_or_zero(Size<Opt>{80, 120}) == Size<float>{0, 0});
    CHECK(Size<AvailableSpace>::min_content().width.is_min_content());
    CHECK(Size<AvailableSpace>::max_content().height.is_max_content());
    const auto edges = Rect<LengthPercentage>::from_percent(0.1f, 0.2f, 0.5f, 0.25f);
    CHECK(edges.resolve_or_zero(Opt{100}) == Rect<float>{10, 20, 50, 25});
    CHECK(edges.resolve_or_zero(Size<Opt>{100, 200}) == Rect<float>{10, 20, 100, 50});
    CHECK(edges.maybe_resolve(Size<Opt>{std::nullopt, 200}) == Rect<Opt>{std::nullopt, std::nullopt, 100, 50});
    CHECK(Rect<Dimension>::from_length(1, 2, 3, 4).resolve_or_zero(Opt{}) == Rect<float>{1, 2, 3, 4});
}

TEST_CASE("native float NaNs, infinities and the two rounding contracts", "[cpp][foundation][numeric]") {
    CHECK(f32_min(nan, 3) == 3);
    CHECK(f32_min(3, nan) == 3);
    CHECK(f32_max(nan, -3) == -3);
    CHECK(f32_max(-3, nan) == -3);
    CHECK(std::isnan(f32_min(nan, nan)));
    CHECK(std::isnan(f32_max(nan, nan)));
    CHECK(f32_min(inf, -inf) == -inf);
    CHECK(f32_max(inf, -inf) == inf);
    CHECK(maybe_clamp(5, 10, 2) == 10); // not std::clamp
    CHECK(maybe_clamp(nan, 2, 8) == 8);
    CHECK(maybe_clamp(5, nan, 8) == 5);
    CHECK(maybe_clamp(5, 2, nan) == 5);
    CHECK(taffy::round(0.5f) == 1);
    CHECK(taffy::round(-0.5f) == -1);
    CHECK(round_ties_away(-1.5f) == -2);
    CHECK(layout_round(-1.5f) == -1);
    CHECK(layout_round(-0.5f) == 0);
    CHECK_FALSE(std::signbit(layout_round(-0.5f)));
    CHECK(std::signbit(round_ties_away(-0.0f)));
    CHECK(layout_round(2.5f) == 3);
    CHECK(taffy::floor(-1.25f) == -2);
    CHECK(taffy::ceil(-1.25f) == -1);
    CHECK_FALSE(std::signbit(taffy::abs(-0.0f)));
    CHECK(taffy::abs(-inf) == inf);
    CHECK(std::isnan(layout_round(nan)));
    CHECK(layout_round(inf) == inf);
    CHECK(layout_round(-inf) == -inf);
}

TEST_CASE("native saturating Rust casts cover exact integer boundaries", "[cpp][foundation][numeric]") {
    CHECK(saturating_cast<std::int8_t>(127.9f) == 127);
    CHECK(saturating_cast<std::int8_t>(128.0f) == 127);
    CHECK(saturating_cast<std::int8_t>(-128.9f) == -128);
    CHECK(saturating_cast<std::int8_t>(-12.9f) == -12);
    CHECK(saturating_cast<std::uint8_t>(-1.5f) == 0);
    CHECK(saturating_cast<std::uint8_t>(255.9f) == 255);
    CHECK(saturating_cast<std::uint8_t>(256.0f) == 255);
    CHECK(saturating_cast<std::int32_t>(nan) == 0);
    CHECK(saturating_cast<std::uint64_t>(nan) == 0);
    CHECK(saturating_cast<std::int32_t>(inf) == 2147483647);
    CHECK(saturating_cast<std::int32_t>(-inf) == (-2147483647 - 1));
    CHECK(saturating_cast<std::uint64_t>(inf) == UINT64_MAX);
    CHECK(saturating_cast<std::uint64_t>(-inf) == 0);
    CHECK(saturating_cast<std::uint64_t>(0x1p64) == UINT64_MAX);
    CHECK(saturating_cast<std::uint64_t>(std::nextafter(0x1p64, 0.0)) == UINT64_MAX - 2047);
    CHECK(saturating_cast<std::int64_t>(0x1p63) == INT64_MAX);
    CHECK(saturating_cast<std::int64_t>(std::nextafter(0x1p63, 0.0)) == INT64_MAX - 1023);
    CHECK(saturating_float_to_int<std::int64_t>(-0x1p63) == INT64_MIN);
    CHECK(saturating_cast<std::uint32_t>(-0.0) == 0);
}

TEST_CASE("native optional math preserves absent left operands", "[cpp][foundation][numeric]") {
    const Opt none;
    CHECK(maybe_min(8, none) == 8);
    CHECK(maybe_max(8, Opt{12}) == 12);
    CHECK(maybe_add(8, none) == 8);
    CHECK(maybe_sub(8, Opt{3}) == 5);
    CHECK_FALSE(maybe_min(none, 2));
    CHECK_FALSE(maybe_max(none, Opt{2}));
    CHECK_FALSE(maybe_add(none, 2));
    CHECK_FALSE(maybe_sub(none, Opt{2}));
    CHECK(maybe_min(Opt{8}, 3) == Opt{3});
    CHECK(maybe_max(Opt{8}, none) == Opt{8});
    CHECK(maybe_add(Opt{8}, Opt{3}) == Opt{11});
    CHECK(maybe_sub(Opt{8}, none) == Opt{8});
    CHECK(maybe_clamp(8, Opt{10}, Opt{2}) == 10);
    CHECK_FALSE(maybe_clamp(none, Opt{2}, Opt{8}));
    CHECK_FALSE(maybe_clamp(none, 2, 8));
    CHECK(maybe_clamp(Opt{12}, Opt{}, Opt{8}) == Opt{8});
}

TEST_CASE("native available space intrinsic and lazy helpers", "[cpp][foundation][style]") {
    const auto min = AvailableSpace::min_content();
    const auto max = AvailableSpace::max_content();
    const auto definite = AvailableSpace::definite(20);
    CHECK(AvailableSpace{}.is_max_content());
    CHECK(AvailableSpace(Opt{}).is_max_content());
    CHECK(AvailableSpace(Opt{0}).is_definite());
    CHECK(min.compute_free_space(10) == 0);
    CHECK(max.compute_free_space(10) == inf);
    CHECK(definite.compute_free_space(25) == -5);
    CHECK_THROWS_AS(min.unwrap(), std::bad_optional_access);
    CHECK(min.unwrap_or(7) == 7);
    CHECK(definite.or_(min).value() == 20);
    CHECK(min.or_(max).is_max_content());
    int calls = 0;
    auto fallback = [&] { ++calls; return max; };
    CHECK(definite.or_else(fallback).value() == 20);
    CHECK(calls == 0);
    CHECK(min.or_else(fallback).is_max_content());
    CHECK(calls == 1);
    CHECK(definite.unwrap_or_else([&] { ++calls; return 9.0f; }) == 20);
    CHECK(calls == 1);
    CHECK(min.unwrap_or_else([&] { ++calls; return 9.0f; }) == 9);
    CHECK(calls == 2);
    CHECK(min.map_definite_value([&](float x) { ++calls; return x * 2; }).is_min_content());
    CHECK(calls == 2);
    CHECK(definite.map_definite_value([](float x) { return x * 2; }).value() == 40);
    CHECK(min.maybe_set(7).value() == 7);
    CHECK(min.maybe_set(std::nullopt).is_min_content());
    CHECK(maybe_min(min, 12).value() == 12);
    CHECK(maybe_min(max, 12).value() == 12);
    CHECK(maybe_max(min, 12).is_min_content());
    CHECK(maybe_add(max, 12).is_max_content());
    CHECK(maybe_sub(min, 12).is_min_content());
    CHECK(maybe_clamp(max, 2, 8).is_max_content());
    CHECK(maybe_clamp(definite, 2, 8).value() == 8);
    CHECK(maybe_min(definite, Opt{}).value() == 20);
    CHECK(AvailableSpace::definite(0).is_roughly_equal(AvailableSpace::definite(std::numeric_limits<float>::epsilon() / 2)));
    CHECK_FALSE(AvailableSpace::definite(0).is_roughly_equal(AvailableSpace::definite(std::numeric_limits<float>::epsilon())));
    CHECK_FALSE(AvailableSpace::definite(inf).is_roughly_equal(AvailableSpace::definite(inf)));
    CHECK_FALSE(min.is_roughly_equal(max));
    CHECK(into_options(Size<AvailableSpace>{definite, max}) == Size<Opt>{20, std::nullopt});
    CHECK(maybe_set(Size<AvailableSpace>{min, max}, Size<Opt>{5, std::nullopt}).width.value() == 5);
}

TEST_CASE("native every Style field has pinned all-feature defaults", "[cpp][foundation][style]") {
    const Style s;
    CHECK(s.display == Display::Flex);
    CHECK_FALSE(s.item_is_table);
    CHECK_FALSE(s.item_is_replaced);
    CHECK_FALSE(s.item_is_compressible_replaced);
    CHECK(s.box_sizing == BoxSizing::BorderBox);
    CHECK(s.direction == Direction::Ltr);
    CHECK(s.overflow == Point<Overflow>{Overflow::Visible, Overflow::Visible});
    CHECK(s.scrollbar_width == 0);
    CHECK(s.contain == Contain::none());
    CHECK(s.float_ == Float::None);
    CHECK(s.clear == Clear::None);
    CHECK(s.position == Position::Static);
    CHECK(s.inset == Rect<LengthPercentageAuto>::auto_());
    CHECK(s.size.width.kind() == LengthKind::Auto);
    CHECK(s.size.height.kind() == LengthKind::Auto);
    CHECK(s.min_size.width.kind() == LengthKind::Auto);
    CHECK(s.min_size.height.kind() == LengthKind::Auto);
    CHECK(s.max_size.width.kind() == LengthKind::Auto);
    CHECK(s.max_size.height.kind() == LengthKind::Auto);
    CHECK_FALSE(s.aspect_ratio);
    for (auto edge : {s.margin.left, s.margin.right, s.margin.top, s.margin.bottom}) {
        CHECK(edge.kind() == LengthKind::Length); CHECK(edge.value() == 0);
    }
    for (auto edge : {s.padding.left, s.padding.right, s.padding.top, s.padding.bottom,
                      s.border.left, s.border.right, s.border.top, s.border.bottom, s.gap.width, s.gap.height}) {
        CHECK(edge.kind() == LengthKind::Length); CHECK(edge.value() == 0);
    }
    CHECK(s.align_items.keyword == AlignItemsKeyword::Normal);
    CHECK(s.align_items.safety == AlignmentSafety::Default);
    CHECK_FALSE(s.align_self);
    CHECK(s.justify_items.keyword == AlignItemsKeyword::Normal);
    CHECK(s.justify_items.safety == AlignmentSafety::Default);
    CHECK_FALSE(s.justify_self);
    CHECK(s.align_content.keyword == AlignContentKeyword::Normal);
    CHECK(s.align_content.safety == AlignmentSafety::Default);
    CHECK(s.justify_content.keyword == AlignContentKeyword::Normal);
    CHECK(s.justify_content.safety == AlignmentSafety::Default);
    CHECK(s.text_align == TextAlign::Auto);
    CHECK(s.flex_direction == FlexDirection::Row);
    CHECK(s.flex_wrap == FlexWrap::NoWrap);
    CHECK(s.flex_line_count == 1);
    CHECK(s.flex_basis.kind() == LengthKind::Auto);
    CHECK(s.flex_grow == 0);
    CHECK(s.flex_shrink == 1);
    CHECK(s.grid_template_rows.empty()); CHECK(s.grid_template_columns.empty());
    CHECK(s.grid_auto_rows.empty()); CHECK(s.grid_auto_columns.empty());
    CHECK(s.grid_auto_flow == GridAutoFlow::Row);
    CHECK_FALSE(s.grid_template_areas);
    CHECK(s.grid_template_column_names.empty()); CHECK(s.grid_template_row_names.empty());
    CHECK(s.grid_row.start.kind() == GridPlacement::Kind::Auto);
    CHECK(s.grid_row.end.kind() == GridPlacement::Kind::Auto);
    CHECK(s.grid_column.start.kind() == GridPlacement::Kind::Auto);
    CHECK(s.grid_column.end.kind() == GridPlacement::Kind::Auto);
    CHECK_NOTHROW(s.validate());
}

TEST_CASE("native style flags, axes, containment and alignment helpers", "[cpp][foundation][style]") {
    for (auto position : {Position::Static, Position::Relative, Position::Absolute, Position::Fixed, Position::Sticky}) {
        CHECK(is_out_of_flow(position) == (position == Position::Absolute || position == Position::Fixed));
        CHECK(is_positioned(position) == (position != Position::Static));
        Style s; s.position = position;
        CHECK(s.is_containing_block().absolute == (position != Position::Static));
        CHECK_FALSE(s.is_containing_block().fixed);
        CHECK(ContainingBlockClaims::all().for_position(position) == (position == Position::Absolute || position == Position::Fixed));
        CHECK_FALSE(ContainingBlockClaims::none().for_position(position));
    }
    CHECK(is_rtl(Direction::Rtl)); CHECK_FALSE(is_rtl(Direction::Ltr));
    for (auto overflow : {Overflow::Visible, Overflow::Clip, Overflow::Hidden, Overflow::Scroll}) {
        const bool scroll = overflow == Overflow::Hidden || overflow == Overflow::Scroll;
        CHECK(is_scroll_container(overflow) == scroll);
        CHECK(maybe_into_automatic_min_size(overflow) == (scroll ? Opt{0} : Opt{}));
    }
    CHECK_FALSE(Contain::none().establishes_independent_formatting_context());
    CHECK(Contain::paint().establishes_independent_formatting_context());
    CHECK_FALSE(Contain::paint().suppresses_baseline());
    CHECK(Contain::layout().suppresses_baseline());
    CHECK(Contain::content().contains(Contain::layout()));
    CHECK(Contain::none().contains(Contain::none()));
    CHECK_FALSE(Contain::paint().intersects(Contain::layout()));
    CHECK((Contain::paint() | Contain::layout()) == Contain::content());
    CHECK(Contain::paint().contains_scrollable_overflow());
    CHECK_FALSE(Contain::none().contains_scrollable_overflow());
    CHECK_FALSE(is_floated(Float::None)); CHECK(is_floated(Float::Right));
    CHECK_FALSE(float_direction(Float::None));
    CHECK(float_direction(Float::Left) == FloatDirection::Left);
    CHECK(float_direction(Float::Right) == FloatDirection::Right);
    for (auto d : {FlexDirection::Row, FlexDirection::Column, FlexDirection::RowReverse, FlexDirection::ColumnReverse}) {
        bool row = d == FlexDirection::Row || d == FlexDirection::RowReverse;
        CHECK(is_row(d) == row); CHECK(is_column(d) == !row);
        CHECK(is_reverse(d) == (d == FlexDirection::RowReverse || d == FlexDirection::ColumnReverse));
        CHECK(main_axis(d) == (row ? horizontal : vertical));
        CHECK(cross_axis(d) == (row ? vertical : horizontal));
        CHECK(main_axis_is_requested_axis(d, RequestedAxis::Horizontal) == row);
        CHECK(main_axis_is_requested_axis(d, RequestedAxis::Vertical) == !row);
        CHECK_FALSE(main_axis_is_requested_axis(d, RequestedAxis::Both));
    }
    for (auto wrap : {FlexWrap::NoWrap, FlexWrap::Wrap, FlexWrap::WrapReverse, FlexWrap::Balance, FlexWrap::BalanceReverse}) {
        CHECK(is_multi_line(wrap) == (wrap != FlexWrap::NoWrap));
        CHECK(is_reverse(wrap) == (wrap == FlexWrap::WrapReverse || wrap == FlexWrap::BalanceReverse));
        CHECK(is_balance(wrap) == (wrap == FlexWrap::Balance || wrap == FlexWrap::BalanceReverse));
    }
    for (auto flow : {GridAutoFlow::Row, GridAutoFlow::Column, GridAutoFlow::RowDense, GridAutoFlow::ColumnDense}) {
        CHECK(is_dense(flow) == (flow == GridAutoFlow::RowDense || flow == GridAutoFlow::ColumnDense));
        CHECK(primary_axis(flow) == (flow == GridAutoFlow::Row || flow == GridAutoFlow::RowDense ? horizontal : vertical));
    }
    Style s;
    CHECK(s.box_generation_mode() == BoxGenerationMode::Normal);
    s.display = Display::None; CHECK(s.box_generation_mode() == BoxGenerationMode::None);
    s.display = Display::FlowRoot; CHECK_FALSE(s.is_block());
    s.display = Display::Block; CHECK(s.is_block());
    s.item_is_replaced = true; CHECK(s.is_replaced()); CHECK(s.is_compressible_replaced());
    s.item_is_replaced = false; s.item_is_compressible_replaced = true;
    CHECK_FALSE(s.is_replaced()); CHECK(s.is_compressible_replaced());
    s.item_is_table = true; CHECK(s.is_table());
    CHECK(s.grid_align_content(horizontal).keyword == AlignContentKeyword::Stretch);
    s.justify_content = AlignContent::safe_end();
    CHECK(s.grid_align_content(AbstractAxis::Inline).keyword == AlignContentKeyword::End);
    CHECK(s.grid_align_content(horizontal).safety == AlignmentSafety::Safe);
    s.grid_column = {line(2), span(3)}; s.grid_row = {line(4), span(5)};
    CHECK(s.grid_placement(horizontal).start.line_index() == 2);
    CHECK(s.grid_placement(vertical).end.span_count() == 5);
    s.grid_template_columns = {GridTemplateComponent::length(30)};
    CHECK(s.grid_template_tracks(horizontal).size() == 1);
    CHECK(s.grid_template_tracks(vertical).empty());
    s.grid_template_areas = GridTemplateAreas{{}, 2, 3};
    CHECK(s.grid_template_area_row_count() == 2); CHECK(s.grid_template_area_column_count() == 3);
    CHECK(AlignItems::normal().is_stretch_or_normal());
    CHECK(AlignItems::stretch().is_stretch_or_normal());
    CHECK_FALSE(AlignItems::center().is_stretch_or_normal());
    CHECK(AlignItems::safe_self_start().resolve_self_relative(Direction::Rtl, Direction::Ltr, true) == AlignItems::safe_end());
    CHECK(AlignItems::self_end().resolve_self_relative(Direction::Rtl, Direction::Ltr, true).keyword == AlignItemsKeyword::Start);
    CHECK(AlignItems::self_start().resolve_self_relative(Direction::Rtl, Direction::Ltr, false).keyword == AlignItemsKeyword::Start);
    CHECK(AlignItems::self_end().resolve_self_relative(Direction::Ltr, Direction::Ltr, true).keyword == AlignItemsKeyword::End);
    CHECK(reversed(AlignContentKeyword::Start) == AlignContentKeyword::End);
    CHECK(reversed(AlignContentKeyword::End) == AlignContentKeyword::Start);
    CHECK(reversed(AlignContentKeyword::FlexStart) == AlignContentKeyword::FlexEnd);
    CHECK(reversed(AlignContentKeyword::FlexEnd) == AlignContentKeyword::FlexStart);
    CHECK(reversed(AlignContentKeyword::Stretch) == AlignContentKeyword::End);
    CHECK(reversed(AlignContentKeyword::SpaceAround) == AlignContentKeyword::SpaceAround);
}

TEST_CASE("native length domains resolve values and opaque calc handles", "[cpp][foundation][style]") {
    CHECK(LengthPercentage{}.is_zero()); CHECK(LengthPercentageAuto{}.is_auto());
    CHECK(Dimension{}.is_auto()); CHECK(MinTrackSizingFunction{}.is_auto()); CHECK(MaxTrackSizingFunction{}.is_auto());
    CHECK(length(12).value() == 12); CHECK(percent(0.5f).value() == 0.5f);
    CHECK(zero<LengthPercentage>().is_zero()); CHECK(auto_().is_auto());
    CHECK(min_content().is_min_content()); CHECK(max_content().is_max_content());
    const auto fixed = Dimension::length(-3);
    CHECK(fixed.maybe_resolve(std::nullopt) == Opt{-3});
    CHECK(fixed.into_option() == Opt{-3});
    CHECK_FALSE(fixed.resolved_percentage_size(100));
    const auto pct = Dimension::percent(0.25f);
    CHECK(pct.maybe_resolve(80) == Opt{20});
    CHECK_FALSE(pct.maybe_resolve(std::nullopt));
    CHECK(pct.resolve_or_zero(std::nullopt) == 0);
    CHECK(pct.behaves_as_auto(std::nullopt)); CHECK_FALSE(pct.behaves_as_auto(80));
    CHECK(pct.has_definite_value(80)); CHECK_FALSE(pct.has_definite_value(std::nullopt));
    CHECK(pct.uses_percentage()); CHECK_FALSE(pct.into_option());
    CHECK(pct.resolved_percentage_size(80) == Opt{20});
    CHECK(LengthPercentage::percent(0.5f).resolve(80) == 40);
    int calls = 0;
    const CalcResolver resolver = [&](Calc c, float basis) { ++calls; CHECK(c.handle == 17); return basis / 2 + 3; };
    const auto calc = Dimension::calc(Calc{17});
    CHECK(calc.is_plain_calc()); CHECK(calc.uses_percentage()); CHECK(calc.calc_value().handle == 17);
    CHECK_FALSE(calc.definite_value(std::nullopt, resolver)); CHECK(calls == 0);
    CHECK(calc.definite_value(80, resolver) == Opt{43}); CHECK(calls == 1);
    CHECK_THROWS_AS(calc.definite_value(80), std::bad_function_call);
    CHECK_THROWS_AS(calc.value(), std::logic_error);
    CHECK_THROWS_AS(fixed.calc_value(), std::logic_error);
    CHECK_THROWS_AS(Dimension::auto_().value(), std::logic_error);
    for (auto keyword : {Dimension::auto_(), Dimension::min_content(), Dimension::max_content(), Dimension::fit_content(), Dimension::stretch(), Dimension::content()}) {
        CHECK_FALSE(keyword.definite_value(80, resolver));
        CHECK(keyword.resolve_or_zero(80, resolver) == 0);
    }
    CHECK(Dimension::fit_content_px(25).fit_content_limit(std::nullopt) == Opt{25});
    CHECK_FALSE(Dimension::fit_content_percent(0.5f).fit_content_limit(std::nullopt));
    CHECK(Dimension::fit_content_percent(0.5f).definite_limit(80) == Opt{40});
    const auto fitcalc = Dimension::fit_content_calc(Calc{17});
    CHECK(fitcalc.is_fit_content_calc()); CHECK(fitcalc.is_fit_content());
    CHECK(fitcalc.fit_content_limit(80, resolver) == Opt{43});
    CHECK(Dimension::fit_content(LengthPercentage::length(9)).kind() == LengthKind::FitContentPx);
    CHECK(Dimension::fit_content(LengthPercentage::percent(0.2f)).kind() == LengthKind::FitContentPercent);
    CHECK(Dimension::fit_content(LengthPercentage::calc(Calc{17})).calc_value().handle == 17);
    CHECK_FALSE(Dimension::fit_content().is_fit_content());
    CHECK(Dimension::fit_content().is_sizing_keyword());
    CHECK_FALSE(Dimension::content().is_sizing_keyword());
    CHECK(Dimension::auto_().is_intrinsic()); CHECK(Dimension::max_content().is_max_content_alike());
    CHECK(Dimension::fit_content_px(2).is_max_or_fit_content());
    CHECK(MinTrackSizingFunction(Dimension::fit_content_px(2)).is_auto());
    CHECK(MinTrackSizingFunction(Dimension::stretch()).is_auto());
    CHECK(MaxTrackSizingFunction(Dimension::content()).is_auto());
    CHECK(MaxTrackSizingFunction(Dimension::fit_content()).is_auto());
    CHECK(MinTrackSizingFunction(MaxTrackSizingFunction::fr(2)).is_auto());
    CHECK(MaxTrackSizingFunction(Dimension::percent(0.5f)).value() == 0.5f);
    CHECK(size_all(3) == Size<int>{3, 3}); CHECK(point_all(4) == Point<int>{4, 4});
    CHECK(line_all(5) == Line<int>{5, 5}); CHECK(rect_all(6) == Rect<int>{6, 6, 6, 6});
}

TEST_CASE("native grid sizing helpers distinguish fr and flex minima", "[cpp][foundation][grid-values]") {
    CHECK(fr(2).min.is_auto()); CHECK(fr(2).max.is_fr()); CHECK(fr(2).max.value() == 2);
    CHECK(flex(3).min.is_zero()); CHECK(flex(3).max.value() == 3);
    CHECK_FALSE(fr(2).has_fixed_component()); CHECK(flex(2).has_fixed_component());
    CHECK(TrackSizingFunction::percent(0.5f).has_fixed_component());
    CHECK(TrackSizingFunction::calc(Calc{4}).has_fixed_component());
    CHECK(TrackSizingFunction::min_content().min.is_min_content());
    CHECK(TrackSizingFunction::max_content().max.is_max_content());
    const auto fit = fit_content(LengthPercentage::percent(0.5f));
    CHECK(fit.min.is_auto()); CHECK(fit.max.kind() == LengthKind::FitContentPercent);
    CHECK_FALSE(fit.has_fixed_component());
    const auto mm = minmax(MinTrackSizingFunction::length(10), MaxTrackSizingFunction::fr(2));
    CHECK(mm.min_sizing_function().value() == 10); CHECK(mm.max_sizing_function().value() == 2);
    CHECK(RepetitionCount{}.value() == 1);
    CHECK_THROWS_AS(RepetitionCount::auto_fit().value(), std::logic_error);
    CHECK_THROWS_AS(RepetitionCount::auto_fill().value(), std::logic_error);
    const auto even = evenly_sized_tracks(3);
    REQUIRE(even.size() == 1);
    const auto& repetition = std::get<GridTemplateRepetition>(even.front().value);
    CHECK(repetition.count.value() == 3); CHECK(repetition.track_count() == 1);
    CHECK(repetition.tracks.front().min.is_zero()); CHECK(repetition.tracks.front().max.value() == 1);
    CHECK(expand_grid_template(even).tracks.size() == 3);
}

TEST_CASE("native grid repeat expansion merges boundary names and clips safely", "[cpp][foundation][grid-values]") {
    const std::vector<GridTemplateComponent> components{
        repeat(RepetitionCount::count(2), {TrackSizingFunction::length(10), fr(2)}, {{"a"}, {"b"}, {"c"}}),
        GridTemplateComponent::length(30)};
    const auto expanded = expand_grid_template(components, {{"outer"}, {"tail"}, {"end"}});
    REQUIRE(expanded.tracks.size() == 5);
    CHECK(expanded.tracks[0].min.value() == 10);
    CHECK(expanded.tracks[1].max.value() == 2);
    CHECK(expanded.tracks[4].min.value() == 30);
    CHECK(expanded.line_names == TemplateLineNames{{"outer", "a"}, {"b"}, {"c", "a"}, {"b"}, {"c", "tail"}, {"end"}});
    const auto clipped = expand_grid_template(components, {}, 1, 2);
    CHECK(clipped.tracks.size() == 2);
    CHECK(clipped.line_names == TemplateLineNames{{"a"}, {"b"}, {"c", "a"}});
    const std::vector<GridTemplateComponent> automatic{repeat(RepetitionCount::auto_fill(), {TrackSizingFunction::length(7)})};
    CHECK(expand_grid_template(automatic, {}, 4).tracks.size() == 4);
    CHECK(expand_grid_template(automatic, {}, 0).tracks.empty());
    CHECK(expand_grid_template(automatic, {}, 4, 0).tracks.empty());
    CHECK_THROWS_AS(repeat(RepetitionCount::count(2), {fr(1)}, {{"bad"}}), std::invalid_argument);
    CHECK_THROWS_AS(expand_grid_template(components, {{"bad"}}), std::invalid_argument);
    GridTemplateTracks<> invalid{{GridTemplateComponent::length(10)}, {{"only"}}};
    CHECK_THROWS_AS(invalid.validate(), std::invalid_argument);
    Style s; s.flex_line_count = 0;
    CHECK_THROWS_AS(s.validate(), std::invalid_argument);
    s.flex_line_count = 1; s.grid_template_row_names = {{"bad"}, {"extra"}};
    CHECK_THROWS_AS(s.validate(), std::invalid_argument);
    GridTemplateAreas overlap{{{"a", 1, 3, 1, 3}, {"b", 2, 3, 2, 3}}, 2, 2};
    CHECK_THROWS_AS(overlap.validate(), std::invalid_argument);
    GridTemplateAreas duplicate{{{"a", 1, 2, 1, 2}, {"a", 2, 3, 1, 2}}, 2, 1};
    CHECK_THROWS_AS(duplicate.validate(), std::invalid_argument);
    GridTemplateAreas outside{{{"a", 0, 2, 1, 2}}, 1, 1};
    CHECK_THROWS_AS(outside.validate(), std::invalid_argument);
}

TEST_CASE("native grid placements resolve signed indices, spans and absolute auto edges", "[cpp][foundation][grid-values]") {
    CHECK(GridLine(1).into_origin_zero_line(4) == 0);
    CHECK(GridLine(5).into_origin_zero_line(4) == 4);
    CHECK(GridLine(-1).into_origin_zero_line(4) == 4);
    CHECK(GridLine(-6).into_origin_zero_line(4) == -1);
    CHECK(GridLine(32767).into_origin_zero_line(4) == 10000);
    CHECK(GridLine(-32768).into_origin_zero_line(4) == -10000);
    CHECK_THROWS_AS(GridLine(0).into_origin_zero_line(4), std::invalid_argument);
    CHECK(line(0).is_auto()); CHECK_FALSE(line(0).is_definite());
    CHECK(line(-1).is_definite()); CHECK_FALSE(span(2).is_definite());
    CHECK_THROWS_AS(span(2).line_index(), std::logic_error);
    CHECK_THROWS_AS(line(2).span_count(), std::logic_error);
    CHECK_THROWS_AS(line(2).name(), std::logic_error);
    CHECK(into_origin_zero_placement_ignoring_named(span(0), 4).value == 1);
    CHECK(into_origin_zero_placement_ignoring_named(span(65535), 4).value == 10000);
    CHECK(into_origin_zero_placement_ignoring_named(GridPlacement::named_line("x"), 4).kind == OriginZeroGridPlacement::Kind::Auto);
    using P = OriginZeroGridPlacement;
    CHECK(resolve_definite_grid_lines({P::line(5), P::line(2)}) == Line<std::int32_t>{2, 5});
    CHECK(resolve_definite_grid_lines({P::line(2), P::line(2)}) == Line<std::int32_t>{2, 3});
    CHECK(resolve_definite_grid_lines({P::span(3), P::line(2)}) == Line<std::int32_t>{-1, 2});
    CHECK(resolve_definite_grid_lines({P::line(2), P::span(3)}) == Line<std::int32_t>{2, 5});
    CHECK(resolve_definite_grid_lines({P::auto_(), P::line(2)}) == Line<std::int32_t>{1, 2});
    CHECK(resolve_indefinite_grid_tracks({P::span(3), P::auto_()}, -2) == Line<std::int32_t>{-2, 1});
    CHECK(indefinite_span({P::auto_(), P::auto_()}) == 1);
    CHECK(indefinite_span({P::auto_(), P::span(4)}) == 4);
    CHECK_THROWS_AS(resolve_definite_grid_lines({P::auto_(), P::span(2)}), std::logic_error);
    CHECK_THROWS_AS(resolve_indefinite_grid_tracks({P::line(2), P::auto_()}, 0), std::logic_error);
    CHECK_THROWS_AS(indefinite_span({P::line(2), P::line(4)}), std::logic_error);
    CHECK(resolve_absolutely_positioned_grid_tracks({P::line(2), P::auto_()}) == Line<std::optional<std::int32_t>>{2, std::nullopt});
    CHECK(resolve_absolutely_positioned_grid_tracks({P::span(2), P::auto_()}) == Line<std::optional<std::int32_t>>{});
    CHECK(resolve_absolutely_positioned_grid_tracks({P::span(2), P::line(5)}) == Line<std::optional<std::int32_t>>{3, 5});
}

TEST_CASE("native named grid lines include occurrences, implicit lines and area aliases", "[cpp][foundation][grid-values]") {
    const TemplateLineNames names{{"x"}, {"a-start"}, {"x"}, {}, {"x"}};
    const std::vector<GridTemplateArea> areas{{"a", 2, 4, 2, 5}};
    auto resolve = [&](GridPlacement p, GridAreaEnd end = GridAreaEnd::Start) {
        return resolve_named_grid_placement(p, names, areas, GridAreaAxis::Column, end, 4).line_index();
    };
    CHECK(resolve(GridPlacement::named_line("x", 2)) == 3);
    CHECK(resolve(GridPlacement::named_line("x", -1)) == 5);
    CHECK(resolve(GridPlacement::named_line("x", 4)) == 6);
    CHECK(resolve(GridPlacement::named_line("missing", -1)) == -6);
    CHECK(resolve(GridPlacement::named_line("a")) == 2);
    CHECK(resolve(GridPlacement::named_line("a"), GridAreaEnd::End) == 5);
    CHECK(resolve(GridPlacement::named_line("a-end")) == 5);
    const auto placed = into_origin_zero({line(1), GridPlacement::named_span("x", 2)}, names, areas, GridAreaAxis::Column, 4);
    CHECK(placed.start.value == 0); CHECK(placed.end.value == 4);
    CHECK(resolve_named_grid_placement(GridPlacement::named_span("x"), names, areas, GridAreaAxis::Column, GridAreaEnd::End, 4).span_count() == 1);
}

TEST_CASE("native CSS scalar parser accepts concrete supported values", "[cpp][foundation][parse]") {
    // Pinned FromStr matches units/sizing identifiers case-sensitively (dimension.rs).
    // The initial bounded-parser scaffold incorrectly accepted uppercase spellings.
    parse_error<Dimension>(" \t+12.5PX\n", ParseErrorCode::InvalidValue, 2);
    CHECK(parsed<Dimension>(" \t+12.5px\n").value() == 12.5f);
    CHECK(parsed<Dimension>("-3px").value() == -3);
    CHECK(parsed<Dimension>("25%").value() == 0.25f);
    parse_error<LengthPercentage>("-0", ParseErrorCode::InvalidValue);
    CHECK(parsed<LengthPercentage>("-0px").is_zero());
    parse_error<LengthPercentageAuto>("AUTO", ParseErrorCode::InvalidValue);
    CHECK(parsed<LengthPercentageAuto>("auto").is_auto());
    CHECK(parsed<Dimension>("fit-content").kind() == LengthKind::FitContent);
    CHECK(parsed<Dimension>("fit-content(25%)").kind() == LengthKind::FitContentPercent);
    CHECK(parsed<Dimension>("stretch").is_stretch()); CHECK(parsed<Dimension>("content").is_content());
    CHECK(parsed<MinTrackSizingFunction>("min-content").is_min_content());
    CHECK(parsed<MaxTrackSizingFunction>("2fr").value() == 2);
    CHECK(parsed<AvailableSpace>("12").value() == 12);
    CHECK(parsed<AvailableSpace>("12px").value() == 12);
    parse_error<AvailableSpace>("MIN-CONTENT", ParseErrorCode::InvalidValue);
    CHECK(parsed<AvailableSpace>("min-content").is_min_content());
    CHECK(parsed<AvailableSpace>("max-content").is_max_content());
    CHECK(parsed<Display>("FLOW-ROOT") == Display::FlowRoot);
    CHECK(parsed<Position>("fixed") == Position::Fixed);
    CHECK(parsed<BoxSizing>("content-box") == BoxSizing::ContentBox);
    CHECK(parsed<Direction>("rtl") == Direction::Rtl);
    CHECK(parsed<Overflow>("clip") == Overflow::Clip);
    CHECK(parsed<TextAlign>("-webkit-center") == TextAlign::LegacyCenter);
    CHECK(parsed<Float>("right") == Float::Right); CHECK(parsed<Clear>("both") == Clear::Both);
    CHECK(parsed<FlexDirection>("column-reverse") == FlexDirection::ColumnReverse);
    CHECK(parsed<FlexWrap>("balance wrap-reverse") == FlexWrap::BalanceReverse);
    CHECK(parsed<FlexWrap>("wrap balance") == FlexWrap::Balance);
    CHECK(parsed<GridAutoFlow>("dense column") == GridAutoFlow::ColumnDense);
    CHECK(parsed<GridAutoFlow>("dense") == GridAutoFlow::RowDense);
    CHECK(parsed<Contain>("paint LAYOUT style") == Contain::content());
    CHECK(parsed<Contain>("style") == Contain::none());
    CHECK(parsed<AlignItems>("safe self-end") == AlignItems::safe_self_end());
    CHECK(parsed<AlignItems>("unsafe center").safety == AlignmentSafety::Unsafe);
    CHECK(parsed<AlignContent>("space-between").keyword == AlignContentKeyword::SpaceBetween);
    const auto track = parsed<TrackSizingFunction>("minmax(10px, 2fr)");
    CHECK(track.min.value() == 10); CHECK(track.max.value() == 2);
    CHECK(parsed<RepetitionCount>("999999999999999999999").value() == 65535);
    CHECK(parsed<RepetitionCount>("auto-fit").kind() == RepetitionCount::Kind::AutoFit);
    CHECK(parsed<GridPlacement>("-999999999999999999999").line_index() == -32768);
    CHECK(parsed<GridPlacement>("999999999999999999999").line_index() == 32767);
    const auto named = parsed<GridPlacement>("2 Header");
    CHECK(named.name() == "Header"); CHECK(named.line_index() == 2);
    CHECK(parsed<GridPlacement>("span 3 Header").span_count() == 3);
    CHECK(parsed<GridPlacement>("span Header").span_count() == 0);
    CHECK(parsed<GridPlacement>("span -2").span_count() == 0); // pinned parser clamps negative span
}

TEST_CASE("native CSS keyword parsers cover every enumerator", "[cpp][foundation][parse]") {
    auto keywords = []<class T>(std::initializer_list<std::pair<std::string_view, T>> values) {
        for (const auto& [text, expected] : values) { INFO(text); CHECK(parsed<T>(text) == expected); }
    };
    keywords.operator()<Display>({{"flex", Display::Flex}, {"grid", Display::Grid}, {"block", Display::Block}, {"flow-root", Display::FlowRoot}, {"none", Display::None}});
    keywords.operator()<Position>({{"static", Position::Static}, {"relative", Position::Relative}, {"absolute", Position::Absolute}, {"fixed", Position::Fixed}, {"sticky", Position::Sticky}});
    keywords.operator()<BoxSizing>({{"border-box", BoxSizing::BorderBox}, {"content-box", BoxSizing::ContentBox}});
    keywords.operator()<Direction>({{"ltr", Direction::Ltr}, {"rtl", Direction::Rtl}});
    keywords.operator()<Overflow>({{"visible", Overflow::Visible}, {"clip", Overflow::Clip}, {"hidden", Overflow::Hidden}, {"scroll", Overflow::Scroll}});
    keywords.operator()<TextAlign>({{"auto", TextAlign::Auto}, {"-webkit-left", TextAlign::LegacyLeft}, {"-webkit-right", TextAlign::LegacyRight}, {"-webkit-center", TextAlign::LegacyCenter}});
    keywords.operator()<Float>({{"none", Float::None}, {"left", Float::Left}, {"right", Float::Right}});
    keywords.operator()<Clear>({{"none", Clear::None}, {"left", Clear::Left}, {"right", Clear::Right}, {"both", Clear::Both}});
    keywords.operator()<FlexDirection>({{"row", FlexDirection::Row}, {"column", FlexDirection::Column}, {"row-reverse", FlexDirection::RowReverse}, {"column-reverse", FlexDirection::ColumnReverse}});
    keywords.operator()<FlexWrap>({{"nowrap", FlexWrap::NoWrap}, {"wrap", FlexWrap::Wrap}, {"wrap-reverse", FlexWrap::WrapReverse}, {"balance", FlexWrap::Balance}, {"wrap-reverse balance", FlexWrap::BalanceReverse}});
    keywords.operator()<GridAutoFlow>({{"row", GridAutoFlow::Row}, {"column", GridAutoFlow::Column}, {"row dense", GridAutoFlow::RowDense}, {"column dense", GridAutoFlow::ColumnDense}});
    keywords.operator()<Contain>({{"none", Contain::none()}, {"style", Contain::none()}, {"layout", Contain::layout()}, {"paint", Contain::paint()}, {"content", Contain::content()}});
    keywords.operator()<AlignItems>({{"normal", AlignItems::normal()}, {"start", AlignItems::start()}, {"end", AlignItems::end()}, {"flex-start", AlignItems::flex_start()}, {"flex-end", AlignItems::flex_end()}, {"self-start", AlignItems::self_start()}, {"self-end", AlignItems::self_end()}, {"center", AlignItems::center()}, {"baseline", AlignItems::baseline()}, {"stretch", AlignItems::stretch()}});
    keywords.operator()<AlignContent>({{"normal", AlignContent::normal()}, {"start", AlignContent::start()}, {"end", AlignContent::end()}, {"flex-start", AlignContent::flex_start()}, {"flex-end", AlignContent::flex_end()}, {"center", AlignContent::center()}, {"stretch", AlignContent::stretch()}, {"space-between", AlignContent::space_between()}, {"space-evenly", AlignContent::space_evenly()}, {"space-around", AlignContent::space_around()}});
}

TEST_CASE("native CSS compound grid parser preserves names and rectangular area coordinates", "[cpp][foundation][parse]") {
    const auto tracks = parsed<GridTemplateTracks<>>("[first] 10px [middle] repeat(2, [a] 1fr [b]) [last]");
    REQUIRE(tracks.tracks.size() == 2);
    CHECK(tracks.line_names == TemplateLineNames{{"first"}, {"middle"}, {"last"}});
    const auto expanded = expand_grid_template(tracks.tracks, tracks.line_names);
    CHECK(expanded.tracks.size() == 3);
    CHECK(expanded.line_names == TemplateLineNames{{"first"}, {"middle", "a"}, {"b", "a"}, {"b", "last"}});
    // Standalone FromStr requires a nonempty track list; only Style adds `none`.
    parse_error<GridTemplateTracks<>>("none", ParseErrorCode::InvalidValue);
    const auto cleared = parse_style("grid-template-columns:none");
    REQUIRE(cleared);
    CHECK(cleared->grid_template_columns.empty());
    CHECK(parsed<GridTemplateTracks<TrackSizingFunction>>("[a] 1fr [b] 20px [c]").tracks.size() == 2);
    const auto automatic = parsed<GridTemplateComponent>("repeat(auto-fill, 20px)");
    CHECK(automatic.is_auto_repetition());
    // Unitless zero is not a minimum-track length in pinned FromStr.
    parse_error<GridAutoTracks>("10px minmax(0, 1fr)", ParseErrorCode::InvalidValue, 12);
    const auto auto_tracks = parsed<GridAutoTracks>("10px minmax(0px, 1fr)");
    REQUIRE(auto_tracks.size() == 2); CHECK(auto_tracks[0].min.value() == 10); CHECK(auto_tracks[1].min.is_zero());
    const auto areas = parsed<GridTemplateAreas>("\"head head\" 'side main' 'side ...'");
    CHECK(areas.row_count == 3); CHECK(areas.column_count == 2);
    CHECK(areas.areas == std::vector<GridTemplateArea>{{"head", 1, 2, 1, 3}, {"side", 2, 4, 1, 2}, {"main", 2, 3, 2, 3}});
    CHECK_NOTHROW(areas.validate());
    CHECK(parsed<GridTemplateAreas>("none").areas.empty());
}

TEST_CASE("native parser reports syntax, value and explicitly unsupported errors", "[cpp][foundation][parse]") {
    using E = ParseErrorCode;
    // These error categories/byte offsets are the native API contract, not Rust's
    // string-only ParseError. All rejection/acceptance decisions were oracle-checked.
    for (auto css : {"", "1", "1em"}) parse_error<Dimension>(css, E::InvalidValue);
    parse_error<Dimension>("auto junk", E::InvalidSyntax, 5);
    parse_error<Dimension>("25% junk", E::InvalidSyntax, 4);
    for (auto css : {"1 px", "1 %", "NaNpx", "infpx"}) parse_error<Dimension>(css, E::InvalidValue);
    // cssparser stores f32 overflow as infinity, not an invalid numeric token.
    CHECK(std::isinf(parsed<Dimension>("1e99px").value()));
    parse_error<LengthPercentage>("auto", E::InvalidValue);
    parse_error<MinTrackSizingFunction>("1fr", E::InvalidValue);
    parse_error<MaxTrackSizingFunction>("-1fr", E::InvalidValue);
    parse_error<MaxTrackSizingFunction>("-0fr", E::InvalidValue);
    parse_error<AvailableSpace>("-1px", E::InvalidValue);
    parse_error<AvailableSpace>("50%", E::InvalidValue);
    parse_error<Display>("inline", E::InvalidValue);
    parse_error<Direction>("ltr rtl", E::InvalidSyntax, 4);
    parse_error<AlignItems>("safe baseline", E::InvalidValue);
    parse_error<AlignContent>("safe space-between", E::InvalidValue);
    parse_error<AlignItems>("center safe", E::InvalidSyntax, 7);
    parse_error<FlexWrap>("wrap wrap", E::InvalidValue, 5);
    // Pinned grid.rs allows two axis/dense tokens: last axis wins, dense is idempotent.
    CHECK(parsed<GridAutoFlow>("row column") == GridAutoFlow::Column);
    CHECK(parsed<GridAutoFlow>("dense dense") == GridAutoFlow::RowDense);
    parse_error<Contain>("", E::InvalidSyntax);
    parse_error<Contain>("paint paint", E::InvalidValue, 6);
    for (auto css : {"none layout", "content layout", "size"}) parse_error<Contain>(css, E::InvalidValue);
    parse_error<TrackSizingFunction>("minmax(1px)", E::InvalidSyntax, 10);
    parse_error<TrackSizingFunction>("minmax(1fr, 2fr)", E::InvalidValue, 7);
    parse_error<RepetitionCount>("0", E::InvalidValue);
    parse_error<RepetitionCount>("-2", E::InvalidValue);
    parse_error<RepetitionCount>("1.5", E::InvalidValue);
    parse_error<GridPlacement>("0", E::InvalidSyntax);
    parse_error<GridPlacement>("1 2", E::InvalidSyntax, 2);
    parse_error<GridPlacement>("span span", E::InvalidSyntax, 5);
    CHECK(parsed<GridPlacement>("inherit") == GridPlacement::named_line("inherit", 0));
    parse_error<GridTemplateComponent>("repeat(2, none)", E::InvalidValue, 10);
    parse_error<GridTemplateTracks<>>("[only]", E::InvalidSyntax, 6);
    CHECK(parsed<GridTemplateTracks<>>("[auto] 1px").line_names == TemplateLineNames{{"auto"}, {}});
    parse_error<GridTemplateTracks<>>("1px ]", E::InvalidValue, 4);
    // cssparser implicitly closes blocks at EOF, including repeat().
    CHECK(parsed<GridTemplateTracks<>>("repeat(2, 1px").tracks ==
          parsed<GridTemplateTracks<>>("repeat(2, 1px)").tracks);
    parse_error<GridAutoTracks>("", E::InvalidValue);
    parse_error<GridTemplateAreas>("\"a a\" \"a .\"", E::InvalidValue);
    parse_error<GridTemplateAreas>("\"a\" \"a b\"", E::InvalidValue, 4);
    parse_error<GridTemplateAreas>("a b", E::InvalidSyntax);
    parse_error<GridTemplateAreas>("\"\"", E::InvalidValue);
    parse_error<Dimension>("calc(10px + 2%)", E::InvalidValue);
    parse_error<Dimension>("  var(--size)", E::InvalidValue, 2);
    // Supersede old unsupported-feature scaffolding with retained positive checks:
    // comments/escapes follow cssparser; int/Style are documented native extensions.
    CHECK(parsed<Dimension>("1px /* comment */") == length(1));
    CHECK(parsed<GridPlacement>("escaped\\name") == GridPlacement::named_line("escapedname", 0));
    CHECK(parsed<int>("1") == 1);
    parse_error<Calc>("1", E::Unsupported);
    const auto full = parse_style("width: 10px");
    REQUIRE(full); CHECK(full->size.width == length(10));
}

TEST_CASE("native alignment factories expose every keyword and safe positional variant", "[cpp][foundation][style]") {
    const std::array item_values{
        AlignItems::normal(), AlignItems::start(), AlignItems::end(), AlignItems::flex_start(),
        AlignItems::flex_end(), AlignItems::self_start(), AlignItems::self_end(),
        AlignItems::center(), AlignItems::baseline(), AlignItems::stretch()};
    const std::array item_keywords{
        AlignItemsKeyword::Normal, AlignItemsKeyword::Start, AlignItemsKeyword::End,
        AlignItemsKeyword::FlexStart, AlignItemsKeyword::FlexEnd, AlignItemsKeyword::SelfStart,
        AlignItemsKeyword::SelfEnd, AlignItemsKeyword::Center, AlignItemsKeyword::Baseline, AlignItemsKeyword::Stretch};
    for (std::size_t i = 0; i < item_values.size(); ++i) {
        CHECK(item_values[i].keyword == item_keywords[i]);
        CHECK(item_values[i].safety == AlignmentSafety::Default);
        CHECK_FALSE(item_values[i].is_safe());
        CHECK(item_values[i].with_safety(AlignmentSafety::Unsafe).keyword == item_keywords[i]);
        CHECK(item_values[i].with_safety(AlignmentSafety::Unsafe).safety == AlignmentSafety::Unsafe);
    }
    const std::array safe_items{
        AlignItems::safe_start(), AlignItems::safe_end(), AlignItems::safe_flex_start(),
        AlignItems::safe_flex_end(), AlignItems::safe_self_start(), AlignItems::safe_self_end(), AlignItems::safe_center()};
    for (std::size_t i = 0; i < safe_items.size(); ++i) {
        CHECK(safe_items[i].keyword == item_keywords[i + 1]); CHECK(safe_items[i].is_safe());
    }
    const std::array content_values{
        AlignContent::normal(), AlignContent::start(), AlignContent::end(), AlignContent::flex_start(),
        AlignContent::flex_end(), AlignContent::center(), AlignContent::stretch(),
        AlignContent::space_between(), AlignContent::space_evenly(), AlignContent::space_around()};
    const std::array content_keywords{
        AlignContentKeyword::Normal, AlignContentKeyword::Start, AlignContentKeyword::End,
        AlignContentKeyword::FlexStart, AlignContentKeyword::FlexEnd, AlignContentKeyword::Center,
        AlignContentKeyword::Stretch, AlignContentKeyword::SpaceBetween,
        AlignContentKeyword::SpaceEvenly, AlignContentKeyword::SpaceAround};
    for (std::size_t i = 0; i < content_values.size(); ++i) {
        CHECK(content_values[i].keyword == content_keywords[i]);
        CHECK(content_values[i].safety == AlignmentSafety::Default);
        CHECK_FALSE(content_values[i].is_safe());
        CHECK(content_values[i].with_safety(AlignmentSafety::Safe).keyword == content_keywords[i]);
        CHECK(content_values[i].with_safety(AlignmentSafety::Safe).is_safe());
    }
    const std::array safe_contents{AlignContent::safe_start(), AlignContent::safe_end(),
        AlignContent::safe_flex_start(), AlignContent::safe_flex_end(), AlignContent::safe_center()};
    for (std::size_t i = 0; i < safe_contents.size(); ++i) {
        CHECK(safe_contents[i].keyword == content_keywords[i + 1]); CHECK(safe_contents[i].is_safe());
    }
}

TEST_CASE("native dimension predicate matrix is explicit for every domain kind", "[cpp][foundation][style]") {
    struct Row {
        Dimension value;
        LengthKind kind;
        bool intrinsic, sizing, max_alike, fit, percentage, definite;
    };
    const std::array rows{
        Row{Dimension::length(8), LengthKind::Length, false, false, false, false, false, true},
        Row{Dimension::percent(0.5f), LengthKind::Percent, false, false, false, false, true, true},
        Row{Dimension::auto_(), LengthKind::Auto, true, false, true, false, false, false},
        Row{Dimension::min_content(), LengthKind::MinContent, true, true, false, false, false, false},
        Row{Dimension::max_content(), LengthKind::MaxContent, true, true, true, false, false, false},
        Row{Dimension::fit_content(), LengthKind::FitContent, false, true, false, false, false, false},
        Row{Dimension::fit_content_px(20), LengthKind::FitContentPx, true, true, true, true, false, false},
        Row{Dimension::fit_content_percent(0.5f), LengthKind::FitContentPercent, true, true, true, true, true, false},
        Row{Dimension::stretch(), LengthKind::Stretch, false, true, false, false, false, false},
        Row{Dimension::content(), LengthKind::Content, false, false, false, false, false, false},
        Row{Dimension::calc(Calc{1}), LengthKind::Calc, false, false, false, false, true, true},
        Row{Dimension::fit_content_calc(Calc{1}), LengthKind::FitContentCalc, true, true, true, true, true, false}};
    for (const auto& row : rows) {
        INFO(static_cast<int>(row.kind));
        CHECK(row.value.kind() == row.kind);
        CHECK(row.value.is_intrinsic() == row.intrinsic);
        CHECK(row.value.is_sizing_keyword() == row.sizing);
        CHECK(row.value.is_max_content_alike() == row.max_alike);
        CHECK(row.value.is_fit_content() == row.fit);
        CHECK(row.value.uses_percentage() == row.percentage);
        CHECK(row.value.has_definite_value(100) == row.definite);
        CHECK(row.value.has_definite_value(std::nullopt) == (row.kind == LengthKind::Length));
        CHECK(row.value.is_length_or_percentage() == (row.kind == LengthKind::Length || row.kind == LengthKind::Percent));
        CHECK(row.value.is_length_percentage_or_calc() == (row.kind == LengthKind::Length || row.kind == LengthKind::Percent || row.kind == LengthKind::Calc));
        CHECK(row.value.behaves_as_auto(std::nullopt) == (row.kind == LengthKind::Auto || row.percentage));
        CHECK(row.value.behaves_as_auto(100) == (row.kind == LengthKind::Auto));
    }
}

TEST_CASE("native cache final layout stores all metadata and exact keys", "[cpp][foundation][cache]") {
    Cache cache;
    CHECK(cache.is_empty()); CHECK(cache.clear() == ClearState::AlreadyEmpty);
    LayoutInput key;
    key.parent_size = {100, 200}; key.known_dimensions = {30, 40};
    auto output = LayoutOutput::from_sizes_and_baselines({30, 40}, {1, 50, 2, 60}, {7, 9});
    output.top_margin = {8, -2}; output.bottom_margin = {5, -3}; output.margins_can_collapse_through = true;
    output.oof_candidates.push(OofCandidate{NodeId{17}, 3, Position::Fixed, {}});
    output.oof_positioning_area = OofPositioningArea{{90, 80}, {3, 4}};
    cache.store(key, output);
    REQUIRE(cache.get(key));
    const auto hit = *cache.get(key);
    CHECK(hit.size == Size<float>{30, 40}); CHECK(hit.scrollable_overflow_rect == Rect<float>{1, 50, 2, 60});
    CHECK(hit.baselines.first == Opt{7}); CHECK(hit.baselines.last == Opt{9});
    CHECK(hit.top_margin == CollapsibleMarginSet{8, -2}); CHECK(hit.bottom_margin == CollapsibleMarginSet{5, -3});
    CHECK(hit.margins_can_collapse_through); REQUIRE(hit.oof_candidates.size() == 1);
    CHECK(hit.oof_candidates.inner.front().node.value == 17);
    REQUIRE(hit.oof_positioning_area); CHECK(hit.oof_positioning_area->offset == Point<float>{3, 4});
    auto changed = key; changed.parent_size.height = 201; CHECK_FALSE(cache.get(changed));
    changed = key; changed.axis = RequestedAxis::Horizontal; CHECK_FALSE(cache.get(changed));
    changed = key; changed.known_dimensions_are_definite.width = false; CHECK_FALSE(cache.get(changed));
    changed = key; changed.available_space = {AvailableSpace::min_content(), AvailableSpace::definite(2)};
    CHECK(cache.get(changed).has_value()); // known dimensions mask available-space values
    changed = key; changed.run_mode = RunMode::ComputeSize; CHECK_FALSE(cache.get(changed));
    changed = key; changed.run_mode = RunMode::PerformHiddenLayout; CHECK_FALSE(cache.get(changed));
    cache.store(changed, LayoutOutput::from_outer_size({99, 99}));
    CHECK(cache.get(key)->size == Size<float>{30, 40});
    auto copy = *cache.get(key); copy.oof_candidates.inner.clear();
    CHECK(cache.get(key)->oof_candidates.size() == 1);
    cache.store(key, LayoutOutput::from_outer_size({31, 41}));
    check_size(*cache.get(key), 31, 41);
    CHECK(cache.clear() == ClearState::Cleared); CHECK(cache.is_empty()); CHECK_FALSE(cache.get(key));
    CHECK(cache.clear() == ClearState::AlreadyEmpty);
}

TEST_CASE("native cache measure keys respect both parent bases and requested axes", "[cpp][foundation][cache]") {
    for (auto stored_axis : {RequestedAxis::Horizontal, RequestedAxis::Vertical, RequestedAxis::Both}) {
        Cache cache;
        auto key = measure_key(100); key.parent_size = {200, 300}; key.axis = stored_axis;
        cache.store(key, LayoutOutput::from_outer_size({17, 23}));
        for (auto requested : {RequestedAxis::Horizontal, RequestedAxis::Vertical, RequestedAxis::Both}) {
            auto query = key; query.axis = requested;
            const bool expected = stored_axis == RequestedAxis::Both || requested == stored_axis;
            CHECK(cache.get(query).has_value() == expected);
            if (expected) check_size(*cache.get(query), 17, 23);
        }
        auto query = key; query.parent_size.width = 201; CHECK_FALSE(cache.get(query));
        query = key; query.parent_size.height = 999; CHECK_FALSE(cache.get(query));
    }
    Cache cache;
    auto key = measure_key(100);
    key.known_dimensions_are_definite = {false, false};
    cache.store(key, LayoutOutput::from_outer_size({10, 20}));
    auto query = key; query.known_dimensions_are_definite = {true, true};
    CHECK(cache.get(query).has_value()); // absent known dimensions normalize flags to true
    key.known_dimensions.width = 30;
    cache.store(key, LayoutOutput::from_outer_size({30, 20}));
    query = key; query.known_dimensions_are_definite.width = true;
    CHECK_FALSE(cache.get(query));
    query = key; query.available_space.width = AvailableSpace::min_content();
    CHECK(cache.get(query).has_value());
    query = key; query.known_dimensions.width = 31; CHECK_FALSE(cache.get(query));
}

TEST_CASE("native cache bit keys distinguish signed zero and intrinsic constraints", "[cpp][foundation][cache]") {
    Cache cache;
    auto key = measure_key(0);
    cache.store(key, LayoutOutput::from_outer_size({1, 2}));
    auto query = key; query.available_space.width = AvailableSpace::definite(-0.0f);
    CHECK_FALSE(cache.get(query));
    query = key; query.known_dimensions.width = 0.0f; CHECK_FALSE(cache.get(query));
    query = key; query.available_space.width = AvailableSpace::min_content(); CHECK_FALSE(cache.get(query));
    query = key; query.available_space.width = AvailableSpace::max_content(); CHECK_FALSE(cache.get(query));
    key.parent_size.width = 0.0f; cache.store(key, LayoutOutput::from_outer_size({3, 4}));
    query = key; query.parent_size.width = -0.0f;
    check_size(*cache.get(query), 3, 4); // parent sign bits intentionally carry the axis
    key.known_dimensions.width = std::bit_cast<float>(std::uint32_t{0x7fc00001});
    cache.store(key, LayoutOutput::from_outer_size({5, 6}));
    REQUIRE(cache.get(key)); check_size(*cache.get(key), 5, 6);
    query = key; query.known_dimensions.width = std::bit_cast<float>(std::uint32_t{0x7fc00002});
    CHECK_FALSE(cache.get(query));
}

TEST_CASE("native cache all eight modes evict on store only", "[cpp][foundation][cache]") {
    for (unsigned old_mode = 0; old_mode < 8; ++old_mode) {
        for (unsigned new_mode = 0; new_mode < 8; ++new_mode) {
            auto mode_key = [](unsigned mode) {
                auto key = measure_key(10);
                key.sizing_mode = mode & 1 ? SizingMode::InherentSize : SizingMode::ContentSize;
                key.vertical_margins_are_collapsible = {bool(mode & 2), bool(mode & 4)};
                return key;
            };
            Cache cache;
            auto old = mode_key(old_mode); auto next = mode_key(new_mode);
            cache.store(old, LayoutOutput::from_outer_size({1, 2}));
            CHECK(cache.get(next).has_value() == (old_mode == new_mode));
            REQUIRE(cache.get(old)); // mismatching reads do not evict
            auto final = old; final.run_mode = RunMode::PerformLayout;
            cache.store(final, LayoutOutput::from_outer_size({3, 4}));
            const auto before = cache_mode_change_evictions();
            next.available_space.width = AvailableSpace::definite(11);
            cache.store(next, LayoutOutput::from_outer_size({5, 6}));
            REQUIRE(cache.get(next)); check_size(*cache.get(next), 5, 6);
            CHECK(cache.get(old).has_value() == (old_mode == new_mode));
            CHECK(cache.get(final).has_value() == (old_mode == new_mode));
#ifndef NDEBUG
            CHECK(cache_mode_change_evictions() - before == (old_mode == new_mode ? 0 : 1));
#else
            CHECK(cache_mode_change_evictions() == before);
#endif
        }
    }
}

TEST_CASE("native cache measure reconstruction drops metadata and rejects collapsing margins", "[cpp][foundation][cache]") {
    const auto key = measure_key(100);
    for (int metadata = 0; metadata < 3; ++metadata) {
        Cache cache;
        auto output = LayoutOutput::from_outer_size({10, 20});
        if (metadata == 0) output.top_margin = {3, -1};
        if (metadata == 1) output.bottom_margin = {2, 0};
        if (metadata == 2) output.margins_can_collapse_through = true;
        cache.store(key, output);
        CHECK(cache.is_empty()); CHECK_FALSE(cache.get(key));
    }
    Cache cache;
    auto output = LayoutOutput::from_sizes_and_baselines({10, 20}, {0, 50, 0, 60}, {3, 4});
    output.oof_candidates.push({NodeId{9}, 0, Position::Absolute, {}});
    output.oof_positioning_area = OofPositioningArea{{100, 200}, {2, 3}};
    cache.store(key, output);
    const auto hit = cache.get(key); REQUIRE(hit);
    check_size(*hit, 10, 20);
    CHECK(hit->scrollable_overflow_rect == Rect<float>{0, 0, 0, 0});
    CHECK_FALSE(hit->baselines.first); CHECK_FALSE(hit->baselines.last);
    CHECK(hit->top_margin == CollapsibleMarginSet{0, 0}); CHECK(hit->bottom_margin == CollapsibleMarginSet{0, 0});
    CHECK_FALSE(hit->margins_can_collapse_through); CHECK(hit->oof_candidates.empty()); CHECK_FALSE(hit->oof_positioning_area);
    auto rejected = output; rejected.top_margin = {1, 0};
    auto changed_mode = key; changed_mode.vertical_margins_are_collapsible.start = true;
    cache.store(changed_mode, rejected);
    CHECK(cache.is_empty()); CHECK_FALSE(cache.get(key)); // mode changes precede metadata rejection
}

TEST_CASE("native hidden and rejected same-mode cache stores leave valid entries intact", "[cpp][foundation][cache]") {
    Cache cache;
    auto key = measure_key(10);
    const auto initial_evictions = cache_mode_change_evictions();
    cache.store(key, LayoutOutput::from_outer_size({10, 20}));
    CHECK(cache_mode_change_evictions() == initial_evictions); // changing empty initial mode is not eviction
    auto hidden = key;
    hidden.run_mode = RunMode::PerformHiddenLayout;
    hidden.sizing_mode = SizingMode::ContentSize;
    hidden.vertical_margins_are_collapsible = {true, true};
    cache.store(hidden, LayoutOutput::from_outer_size({99, 99}));
    CHECK(cache_mode_change_evictions() == initial_evictions);
    REQUIRE(cache.get(key)); check_size(*cache.get(key), 10, 20);
    auto collapsing = LayoutOutput::from_outer_size({88, 88});
    collapsing.margins_can_collapse_through = true;
    cache.store(key, collapsing);
    REQUIRE(cache.get(key)); check_size(*cache.get(key), 10, 20);
    auto final = key; final.run_mode = RunMode::PerformLayout;
    cache.store(final, LayoutOutput::from_outer_size({30, 40}));
    for (int i = 0; i < 12; ++i) cache.store(measure_key(static_cast<float>(i)), LayoutOutput::from_outer_size({1, 2}));
    REQUIRE(cache.get(final)); check_size(*cache.get(final), 30, 40);
    cache.clear();
    const auto before = cache_mode_change_evictions();
    auto changed_mode = key; changed_mode.sizing_mode = SizingMode::ContentSize;
    cache.store(changed_mode, LayoutOutput::from_outer_size({5, 6}));
    CHECK(cache_mode_change_evictions() == before);
    REQUIRE(cache.get(changed_mode)); check_size(*cache.get(changed_mode), 5, 6);
}

TEST_CASE("native nine-slot cache uses clock second chances and updates existing keys", "[cpp][foundation][cache]") {
    Cache cache;
    static_assert(Cache::cache_size == 9);
    for (int i = 0; i < 9; ++i) cache.store(measure_key(static_cast<float>(i)), LayoutOutput::from_outer_size({static_cast<float>(i + 10), 20}));
    // All nine insertions are recent. First wrap clears all bits and replaces slot zero.
    cache.store(measure_key(9), LayoutOutput::from_outer_size({19, 20}));
    CHECK_FALSE(cache.get(measure_key(0)));
    REQUIRE(cache.get(measure_key(1))); // grant slot one a second chance
    cache.store(measure_key(10), LayoutOutput::from_outer_size({20, 20}));
    CHECK_FALSE(cache.get(measure_key(2)));
    REQUIRE(cache.get(measure_key(1))); check_size(*cache.get(measure_key(1)), 11, 20);
    for (int i : {3, 4, 5, 6, 7, 8, 9, 10}) CHECK(cache.get(measure_key(static_cast<float>(i))).has_value());
    cache.store(measure_key(1), LayoutOutput::from_outer_size({111, 222}));
    check_size(*cache.get(measure_key(1)), 111, 222);
    auto final = measure_key(200); final.run_mode = RunMode::PerformLayout;
    cache.store(final, LayoutOutput::from_outer_size({7, 8}));
    CHECK(cache.get(measure_key(10)).has_value());
    CHECK(cache.get(final).has_value());
    CHECK(cache.clear() == ClearState::Cleared);
    for (int i = 0; i < 11; ++i) CHECK_FALSE(cache.get(measure_key(static_cast<float>(i))));
    CHECK_FALSE(cache.get(final));
}

TEST_CASE("native layout defaults, transpose and margin collapse", "[cpp][foundation][layout]") {
    LayoutInput input;
    CHECK(input.run_mode == RunMode::PerformLayout); CHECK(input.sizing_mode == SizingMode::InherentSize);
    CHECK(input.axis == RequestedAxis::Both); CHECK(input.known_dimensions == Size<Opt>{});
    CHECK(input.known_dimensions_are_definite == Size<bool>{true, true});
    CHECK(input.parent_size == Size<Opt>{}); CHECK(input.available_space.width.is_max_content());
    CHECK(input.available_space.height.is_max_content()); CHECK(input.vertical_margins_are_collapsible == Line<bool>{false, false});
    CHECK(LayoutInput::HIDDEN.run_mode == RunMode::PerformHiddenLayout);
    input.axis = RequestedAxis::Horizontal; input.known_dimensions = {3, 5};
    input.known_dimensions_are_definite = {false, true}; input.parent_size = {100, 200};
    input.available_space = {AvailableSpace::min_content(), AvailableSpace::definite(80)};
    input.vertical_margins_are_collapsible = {true, true};
    const auto transposed = input.transpose();
    CHECK(transposed.axis == RequestedAxis::Vertical); CHECK(transposed.known_dimensions == Size<Opt>{5, 3});
    CHECK(transposed.known_dimensions_are_definite == Size<bool>{true, false});
    CHECK(transposed.parent_size == Size<Opt>{200, 100}); CHECK(transposed.available_space.width.value() == 80);
    CHECK(transposed.available_space.height.is_min_content()); CHECK(transposed.vertical_margins_are_collapsible == Line<bool>{false, false});
    CHECK(transpose(RequestedAxis::Both) == RequestedAxis::Both); CHECK_FALSE(absolute_axis(RequestedAxis::Both));
    CHECK(requested_axis(vertical) == RequestedAxis::Vertical); CHECK(absolute_axis(RequestedAxis::Horizontal) == horizontal);
    auto margins = CollapsibleMarginSet::from_margin(10).collapse_with_margin(7).collapse_with_margin(-4).collapse_with_margin(-9);
    CHECK(margins.positive == 10); CHECK(margins.negative == -9); CHECK(margins.resolve() == 1);
    margins = margins.collapse_with_set({15, -3}); CHECK(margins.resolve() == 6);
    CHECK(LayoutOutput::DEFAULT.size == Size<float>{0, 0}); CHECK(LayoutOutput::HIDDEN.oof_candidates.empty());
    auto output = LayoutOutput::from_sizes_and_baselines({10, 20}, {1, 11, 2, 22}, {3, 4});
    output.top_margin = {5, -1}; output.margins_can_collapse_through = true;
    output.oof_positioning_area = OofPositioningArea{{100, 200}, {7, 9}};
    const auto t = output.transpose();
    CHECK(t.size == Size<float>{20, 10}); CHECK(t.scrollable_overflow_rect == Rect<float>{2, 22, 1, 11});
    CHECK_FALSE(t.baselines.first); CHECK_FALSE(t.baselines.last); CHECK(t.top_margin.resolve() == 0);
    CHECK_FALSE(t.margins_can_collapse_through); REQUIRE(t.oof_positioning_area);
    CHECK(t.oof_positioning_area->size == Size<float>{200, 100}); CHECK(t.oof_positioning_area->offset == Point<float>{9, 7});
}

TEST_CASE("native layout content box, overflow and out-of-flow candidate helpers", "[cpp][foundation][layout]") {
    Layout layout = Layout::with_order(7);
    layout.location = {10, 20}; layout.size = {100, 80};
    layout.padding = {2, 3, 4, 5}; layout.border = {6, 7, 8, 9};
    layout.scrollbar_size = {10, 11}; layout.scrollable_overflow_rect = {-10, 120, -20, 90};
    CHECK(layout.order == 7); CHECK(layout.content_box_size() == Size<float>{82, 54});
    CHECK(layout.content_box_x() == 18); CHECK(layout.content_box_y() == 32);
    CHECK(layout.scroll_width() == 43); CHECK(layout.scroll_height() == 38);
    layout.scrollable_overflow_rect = {}; CHECK(layout.scroll_width() == 0); CHECK(layout.scroll_height() == 0);
    auto position = AxisStaticPosition::from_alignment(AlignItems::safe_center(), {10, 30}, true);
    CHECK(position.align.keyword == AxisStaticEdge::Center); CHECK(position.align.fallback == AxisStaticEdge::End);
    CHECK(position.align.safety == AlignmentSafety::Safe);
    CHECK(AxisStaticPosition::from_edge(7, AxisStaticEdge::End).area == Line<float>{7, 7});
    CHECK_THROWS_AS(AxisStaticPosition::from_alignment(AlignItems::self_start(), {}, false), std::invalid_argument);
    OofCandidates a, b;
    a.push({NodeId{1}, 0, Position::Absolute, {AxisStaticPosition::from_edge(10, AxisStaticEdge::Start), AxisStaticPosition::from_edge(20, AxisStaticEdge::End)}});
    b.push({NodeId{2}, 1, Position::Fixed, {}});
    a.append(b); CHECK(b.empty()); REQUIRE(a.size() == 2);
    a.translate({3, -4}); CHECK(a.inner[0].static_position.x.area == Line<float>{13, 13});
    CHECK(a.inner[0].static_position.y.area == Line<float>{16, 16}); CHECK(a.inner[1].static_position.x.area == Line<float>{3, 3});
    auto taken = a.take(); CHECK(a.empty()); CHECK(taken.size() == 2);
    CHECK(taken.as_slice()[1].node.value == 2); taken.as_mut_slice()[1].order = 9; CHECK(taken.inner[1].order == 9);
    taken.clear(); CHECK(taken.empty());
}

TEST_CASE("native sizing keyword helpers distinguish exact stretch and measurement constraints", "[cpp][foundation][layout]") {
    auto result = resolve_sizing_keyword(Dimension::stretch(), 75, 100, {});
    REQUIRE(result); CHECK(result->kind == SizingKeywordResolution::Kind::Exact); CHECK(result->exact == 75);
    CHECK_FALSE(resolve_sizing_keyword(Dimension::stretch(), std::nullopt, 100, {}));
    result = resolve_sizing_keyword(Dimension::min_content(), {}, {}, {});
    REQUIRE(result); CHECK(result->kind == SizingKeywordResolution::Kind::Measure); CHECK(result->available_space.is_min_content());
    result = resolve_sizing_keyword(Dimension::max_content(), {}, {}, {});
    REQUIRE(result); CHECK(result->available_space.is_max_content());
    result = resolve_sizing_keyword(Dimension::fit_content_percent(0.25f), {}, 200, {});
    REQUIRE(result); CHECK(result->available_space.value() == 50);
    CHECK_FALSE(resolve_sizing_keyword(Dimension::fit_content_percent(0.25f), 100, {}, {}));
    result = resolve_sizing_keyword(Dimension::fit_content(), 80, {}, {});
    REQUIRE(result); CHECK(result->available_space.value() == 80);
    result = resolve_sizing_keyword(Dimension::fit_content_px(60), {}, {}, {});
    REQUIRE(result); CHECK(result->available_space.value() == 60);
    CHECK_FALSE(resolve_sizing_keyword(Dimension::length(10), 100, 200, {}));
    CHECK_FALSE(resolve_sizing_keyword(Dimension::auto_(), 100, 200, {}));
}

TEST_CASE("native detailed grid geometry and names are reported in physical coordinates", "[cpp][foundation][layout]") {
    DetailedGridInfo info;
    CHECK(info.grid_template_rows() == "none"); CHECK_FALSE(info.item_grid_area(0));
    info.columns.negative_implicit_tracks = 1;
    info.columns.explicit_tracks = 2;
    info.columns.positions = {{0, 10}, {12, 32}, {34, 64}, {66, 106}};
    info.columns.line_names = {{"start"}, {"middle"}, {"end"}};
    CHECK(info.columns.names_for_line(0).empty());
    REQUIRE(info.columns.names_for_line(1).size() == 1);
    CHECK(info.columns.names_for_line(1)[0] == "start");
    CHECK(info.columns.names_for_line(4).empty());
    CHECK(info.columns.iter_line_names() == TemplateLineNames{{}, {"start"}, {"middle"}, {"end"}, {}});
    CHECK(info.grid_template_columns() == "10px [start] 20px [middle] 30px [end] 40px");
    CHECK(info.grid_template_columns(2) == "5px [start] 10px [middle] 15px [end] 20px");
    info.rows.positions = {{5, 15}, {20, 50}};
    info.items = {{1, 3, 2, 4}};
    const auto area = info.item_grid_area(0); REQUIRE(area);
    CHECK(area->first == Point<float>{12, 5}); CHECK(area->second == Size<float>{52, 45});
    CHECK_FALSE(info.item_grid_area(1));
    DetailedGridTracksInfo unnamed; unnamed.positions = {{0, 10}}; unnamed.line_names = {{}, {}};
    CHECK(unnamed.iter_line_names().empty());
}

TEST_CASE("native leaf sizing wraps intrinsic measurement and caches full layouts", "[cpp][foundation][leaf]") {
    Tree tree;
    Style style;
    style.padding = Rect<LengthPercentage>::from_length(2, 3, 5, 7);
    style.border = Rect<LengthPercentage>::from_length(1, 1, 1, 1);
    const auto node = tree.new_leaf(style).value();
    int calls = 0;
    tree.set_intrinsic_measure([&](const LayoutInput& input, NodeId id, NodeContext* context, const Style& actual) {
        ++calls;
        CHECK(id == node); CHECK(context == nullptr); CHECK(actual == style);
        CHECK(input.known_dimensions == Size<Opt>{});
        CHECK(input.available_space == Size<AvailableSpace>{AvailableSpace::definite(93), AvailableSpace::definite(86)});
        return Size<float>{20, 30};
    });
    LayoutInput input;
    input.parent_size = {100, 100};
    input.available_space = {AvailableSpace::definite(100), AvailableSpace::definite(100)};
    const auto first = tree.compute_child_layout(node, input);
    CHECK(first.size == Size<float>{27, 44});
    CHECK(first.scrollable_overflow_rect == Rect<float>{0, 22, 0, 35});
    CHECK(first.baselines == Baselines::NONE);
    CHECK(tree.compute_child_layout(node, input) == first);
    CHECK(calls == 1);
    CHECK_FALSE(tree.dirty(node).value());
    tree.mark_dirty(node).value();
    CHECK(tree.compute_child_layout(node, input) == first);
    CHECK(calls == 2);
}

TEST_CASE("native leaf resolves percentages calc aspect ratios and box sizing", "[cpp][foundation][leaf]") {
    Tree tree;
    Style style;
    style.box_sizing = BoxSizing::ContentBox;
    style.size = {Dimension::calc(Calc{7}), Dimension::percent(0.25f)};
    style.padding = Rect<LengthPercentage>::from_percent(0.1f, 0.1f, 0.1f, 0.1f);
    tree.set_calc_resolver([](Calc calc, float basis) { CHECK(calc.handle == 7); return basis / 2; });
    auto node = tree.new_leaf(style).value();
    LayoutInput input;
    input.parent_size = {200, 100};
    input.available_space = {AvailableSpace::max_content(), AvailableSpace::max_content()};
    CHECK(tree.compute_child_layout(node, input).size == Size<float>{140, 65});
    style.size = {Dimension::length(80), Dimension::auto_()};
    style.padding = {};
    style.aspect_ratio = 2;
    tree.set_style(node, style).value();
    CHECK(tree.compute_child_layout(node, input).size == Size<float>{80, 40});
    style.min_size.width = LengthPercentageAuto::length(100);
    style.max_size.width = LengthPercentageAuto::length(60);
    tree.set_style(node, style).value();
    CHECK(tree.compute_child_layout(node, input).size.width == 100);
    tree.set_calc_resolver({});
    style.size.width = Dimension::calc(Calc{7});
    tree.set_style(node, style).value();
    // Clearing the custom resolver restores TaffyTree's built-in zero resolver.
    CHECK(tree.compute_child_layout(node, input).size.width == 100);
}

TEST_CASE("native size-only leaf shortcut and zero block collapse match pinned semantics", "[cpp][foundation][leaf]") {
    Tree tree;
    int calls = 0;
    tree.set_intrinsic_measure([&](const LayoutInput&, NodeId, NodeContext*, const Style&) { ++calls; return Size<float>{9, 11}; });
    Style style;
    style.size = Size<Dimension>::from_lengths(50, 60);
    auto node = tree.new_leaf(style).value();
    LayoutInput input;
    input.run_mode = RunMode::ComputeSize;
    CHECK(tree.compute_child_layout(node, input).size == Size<float>{50, 60});
    CHECK(calls == 0);
    input.run_mode = RunMode::PerformLayout;
    CHECK(tree.compute_child_layout(node, input).size == Size<float>{50, 60});
    CHECK(calls == 1);
    style.display = Display::Block;
    style.size = {};
    tree.set_style(node, style).value();
    tree.set_intrinsic_measure({});
    auto collapsed = tree.compute_child_layout(node, input);
    CHECK(collapsed.size == Size<float>{});
    CHECK(collapsed.margins_can_collapse_through);
    style.contain = Contain::layout();
    tree.set_style(node, style).value();
    CHECK_FALSE(tree.compute_child_layout(node, input).margins_can_collapse_through);
}

struct DropContext final : NodeContext {
    int* drops;
    explicit DropContext(int& count) : drops(&count) {}
    ~DropContext() override { ++*drops; }
};
TEST_CASE("native tree owns move-only contexts and never revives stale handles", "[cpp][foundation][tree]") {
    int drops = 0;
    NodeId stale;
    {
        Tree tree(1);
        auto node = tree.new_leaf_with_context({}, std::make_unique<DropContext>(drops)).value();
        stale = node;
        tree.set_node_context(node, std::make_unique<DropContext>(drops)).value();
        CHECK(drops == 1);
        CHECK(tree.node_context(node) != nullptr);
        tree.remove(node).value();
        CHECK(drops == 2); CHECK_FALSE(tree.contains(node)); CHECK(tree.node_context(node) == nullptr);
        CHECK_THROWS_AS(tree.style(node), NodeAccessError);
        CHECK_THROWS_AS(tree.remove(node), NodeAccessError);
        auto reused = tree.new_leaf_with_context({}, std::make_unique<DropContext>(drops)).value();
        CHECK(reused != stale);
        tree.clear();
        CHECK(drops == 3); CHECK_FALSE(tree.contains(reused));
        CHECK(tree.new_leaf().value() != reused);
        auto owned = tree.new_leaf_with_context({}, make_context<std::unique_ptr<int>>(std::make_unique<int>(42))).value();
        auto* value = context_cast<std::unique_ptr<int>>(tree.node_context(owned));
        REQUIRE(value); CHECK(**value == 42);
        const std::array keys{owned};
        REQUIRE(tree.disjoint_node_contexts(keys));
        CHECK((*tree.disjoint_node_contexts(keys))[0] == tree.node_context(owned));
        const std::array duplicate{owned, owned};
        CHECK_FALSE(tree.disjoint_node_contexts(duplicate));
        const std::array invalid{stale};
        CHECK_FALSE(tree.disjoint_node_contexts(invalid));
        tree.new_leaf_with_context({}, std::make_unique<DropContext>(drops)).value();
        Tree moved = std::move(tree);
        CHECK(moved.contains(owned)); CHECK(tree.total_node_count() == 0);
    }
    CHECK(drops == 4);
}

TEST_CASE("native child mutation ordering reparenting and error categories are observable", "[cpp][foundation][tree]") {
    Tree tree;
    auto a = tree.new_leaf().value(); auto b = tree.new_leaf().value(); auto c = tree.new_leaf().value();
    const std::array initial{a, b};
    auto p = tree.new_with_children({}, initial).value();
    auto q = tree.new_leaf().value();
    CHECK(tree.parent(a) == p);
    tree.insert_child_at_index(p, 1, c).value();
    CHECK(tree.children(p).value() == std::vector<NodeId>{a, c, b});
    auto bad = tree.insert_child_at_index(p, 4, q);
    REQUIRE_FALSE(bad); CHECK(bad.error().kind == TreeErrorKind::ChildIndexOutOfBounds);
    CHECK(bad.error().node == p); CHECK(bad.error().child_index == 4); CHECK(bad.error().child_count == 3);
    CHECK_FALSE(tree.child_at_index(p, 3));
    CHECK_FALSE(tree.remove_child_at_index(p, 3));
    CHECK_FALSE(tree.replace_child_at_index(p, 3, q));
    CHECK_THROWS_AS(tree.remove_child(q, a), TreePanic);
    CHECK_THROWS_AS(tree.remove_children_range(p, 2, 1), TreePanic);
    CHECK_THROWS_AS(tree.parent(NodeId{}), NodeAccessError);
    const std::array next{b, a};
    tree.set_children(q, next).value();
    CHECK(tree.children(p).value() == std::vector<NodeId>{c});
    CHECK(tree.children(q).value() == std::vector<NodeId>{b, a});
    CHECK(tree.parent(b) == q);
    CHECK(tree.replace_child_at_index(q, 0, c).value() == b);
    CHECK_FALSE(tree.parent(b)); CHECK(tree.parent(c) == q);
    // add/insert/replace deliberately do not detach from an old parent's list,
    // matching the pinned tree operations. set_children performs reparenting.
    CHECK(tree.children(p).value() == std::vector<NodeId>{c});
    tree.remove_children_range(q, 0, 1).value();
    CHECK_FALSE(tree.parent(c));
    tree.remove(q).value(); CHECK_FALSE(tree.parent(a)); CHECK(tree.contains(a));
    CHECK_THROWS_AS(tree.add_child(p, p), TreePanic);
    const std::array parent_child{p};
    auto root = tree.new_with_children({}, parent_child).value();
    CHECK_THROWS_AS(tree.add_child(p, root), TreePanic);
}

TEST_CASE("native dirty propagation clears ancestors but leaves siblings and descendants alone", "[cpp][foundation][tree]") {
    Tree tree;
    auto leaf = tree.new_leaf().value(); auto sibling = tree.new_leaf().value();
    const std::array children{leaf, sibling};
    auto parent = tree.new_with_children({}, children).value();
    const std::array roots{parent};
    auto root = tree.new_with_children({}, roots).value();
    LayoutInput input;
    for (auto node : {leaf, sibling, parent, root}) tree.cache(node).store(input, LayoutOutput::from_outer_size({1, 2}));
    tree.mark_dirty(leaf).value();
    CHECK(tree.dirty(leaf).value()); CHECK(tree.dirty(parent).value()); CHECK(tree.dirty(root).value());
    CHECK_FALSE(tree.dirty(sibling).value());
    tree.set_intrinsic_measure({});
    CHECK(tree.dirty(sibling).value());
    for (auto node : {leaf, sibling, parent, root}) tree.cache(node).store(input, LayoutOutput::from_outer_size({1, 2}));
    tree.set_style(parent, {}).value();
    CHECK(tree.dirty(parent).value()); CHECK(tree.dirty(root).value());
    CHECK_FALSE(tree.dirty(leaf).value()); CHECK_FALSE(tree.dirty(sibling).value());
}

TEST_CASE("native tree deterministic mutation sequences preserve live relationships", "[cpp][foundation][tree]") {
    Tree tree;
    auto parent = tree.new_leaf().value();
    std::vector<NodeId> children;
    std::mt19937 rng(0x7aff123u);
    for (int step = 0; step < 2000; ++step) {
        auto op = rng() % 4;
        if (children.empty() || op == 0) {
            auto node = tree.new_leaf().value();
            const auto index = rng() % (children.size() + 1);
            tree.insert_child_at_index(parent, index, node).value();
            children.insert(children.begin() + static_cast<std::ptrdiff_t>(index), node);
        } else if (op == 1) {
            const auto index = rng() % children.size();
            const auto removed = tree.remove_child_at_index(parent, index).value();
            CHECK(removed == children[index]); CHECK_FALSE(tree.parent(removed));
            tree.remove(removed).value();
            children.erase(children.begin() + static_cast<std::ptrdiff_t>(index));
        } else if (op == 2) {
            const auto index = rng() % children.size();
            const auto old = children[index];
            const auto replacement = tree.new_leaf().value();
            CHECK(tree.replace_child_at_index(parent, index, replacement).value() == old);
            CHECK_FALSE(tree.parent(old)); tree.remove(old).value(); children[index] = replacement;
        } else {
            std::reverse(children.begin(), children.end());
            tree.set_children(parent, children).value();
        }
        CHECK(tree.children(parent).value() == children);
        CHECK(tree.total_node_count() == children.size() + 1);
        for (auto node : children) CHECK(tree.parent(node) == parent);
    }
}

TEST_CASE("native full-output leaf callbacks retain metadata and exceptions are never cached", "[cpp][foundation][tree]") {
    Tree tree;
    auto leaf = tree.new_leaf().value();
    int calls = 0;
    tree.set_leaf_layout([&](LayoutTree&, NodeId, const LayoutInput&) {
        ++calls;
        auto result = LayoutOutput::from_outer_size({13, 17});
        result.baselines = {3, 11}; result.top_margin = {9, -2}; result.bottom_margin = {8, -1};
        result.oof_candidates.push({NodeId{123}, 1, Position::Fixed, {}});
        return result;
    });
    LayoutInput input;
    auto output = tree.compute_child_layout(leaf, input);
    CHECK(output.baselines == Baselines{3, 11}); CHECK(output.top_margin.resolve() == 7);
    CHECK(output.bottom_margin.resolve() == 7); CHECK(output.oof_candidates.size() == 1);
    CHECK(tree.compute_child_layout(leaf, input) == output); CHECK(calls == 1);
    tree.set_leaf_layout([&](LayoutTree&, NodeId, const LayoutInput&) -> LayoutOutput { ++calls; throw std::runtime_error("measure failure"); });
    CHECK_THROWS_AS(tree.compute_child_layout(leaf, input), std::runtime_error);
    CHECK_THROWS_AS(tree.compute_child_layout(leaf, input), std::runtime_error);
    CHECK(calls == 3); CHECK(tree.dirty(leaf).value());
}

TEST_CASE("native root supports margins RTL relative insets and repeated rounded layouts", "[cpp][foundation][root]") {
    Tree tree;
    Style style; style.position = Position::Relative; style.size = Size<Dimension>::from_lengths(10.4f, 20.6f);
    style.margin = Rect<LengthPercentageAuto>::from_length(2, 3, 4, 5);
    style.inset.left = LengthPercentageAuto::length(1.5f);
    style.inset.top = LengthPercentageAuto::length(-1.5f);
    auto root = tree.new_leaf(style).value();
    const Size<AvailableSpace> space{AvailableSpace::definite(100), AvailableSpace::definite(80)};
    tree.compute_layout(root, space).value();
    CHECK(tree.unrounded_layout(root).location == Point<float>{3.5f, 2.5f});
    CHECK(tree.layout(root).location == Point<float>{4, 3});
    CHECK(tree.layout(root).size == Size<float>{10, 20});
    auto rounded = tree.layout(root);
    tree.compute_layout(root, space).value(); CHECK(tree.layout(root) == rounded);
    tree.disable_rounding(); CHECK(tree.layout(root).size == Size<float>{10.4f, 20.6f});
    style.direction = Direction::Rtl;
    style.inset.right = LengthPercentageAuto::length(2);
    tree.set_style(root, style).value(); tree.compute_layout(root, space).value();
    CHECK(tree.layout(root).location.x == 84.6f);
    style.display = Display::Block; style.size.width = Dimension::auto_(); style.inset = {};
    tree.set_style(root, style).value(); tree.compute_layout(root, space).value();
    CHECK(tree.layout(root).size.width == 95);
}

TEST_CASE("native hidden layout clears visible out-of-flow descendants without container dispatch", "[cpp][foundation][root]") {
    Tree tree;
    Style style; style.position = Position::Absolute;
    auto child = tree.new_leaf(style).value();
    tree.set_final_layout(child, Layout{.size = {50, 60}});
    tree.set_unrounded_layout(child, Layout{.size = {50, 60}});
    const std::array children{child};
    Style hidden; hidden.display = Display::None;
    auto root = tree.new_with_children(hidden, children).value();
    tree.compute_layout(root, {AvailableSpace::max_content(), AvailableSpace::max_content()}).value();
    CHECK(tree.layout(root).size == Size<float>{}); CHECK(tree.layout(child).size == Size<float>{});
    CHECK(tree.unrounded_layout(child).size == Size<float>{});
}

TEST_CASE("native installed container algorithms dispatch real root geometry", "[cpp][foundation][dispatch]") {
    // Fixed dimensions and zero edges give the same source geometry for each
    // container: a 100x80 root with its 20x30 first child at the origin.
    for (auto display : {Display::Block, Display::FlowRoot, Display::Flex, Display::Grid}) {
        Tree tree;
        Style child_style; child_style.size = Size<Dimension>::from_lengths(20, 30);
        auto child = tree.new_leaf(child_style).value();
        const std::array children{child};
        Style style; style.display = display;
        style.size = Size<Dimension>::from_lengths(100, 80);
        auto root = tree.new_with_children(style, children).value();
        const Size<AvailableSpace> space{AvailableSpace::definite(100), AvailableSpace::definite(100)};
        tree.compute_layout(root, space).value();
        CHECK(tree.layout(root).size == Size<float>{100, 80});
        CHECK(tree.layout(root).location == Point<float>{0, 0});
        CHECK(tree.layout(child).size == Size<float>{20, 30});
        CHECK(tree.layout(child).location == Point<float>{0, 0});
        CHECK_FALSE(tree.dirty(root).value());
    }
}

TEST_CASE("native rounding uses cumulative coordinates and containing-block hoisting", "[cpp][foundation][round]") {
    Tree tree;
    Style abs; abs.position = Position::Fixed;
    auto hoisted = tree.new_leaf(abs).value();
    const std::array child_ids{hoisted};
    auto middle = tree.new_with_children({}, child_ids).value();
    const std::array middle_ids{middle};
    auto root = tree.new_with_children({}, middle_ids).value();
    Layout r; r.location = {0.4f, -0.5f}; r.size = {20.4f, 30.6f};
    r.border = {0.4f, 0.6f, 0.4f, 0.6f}; r.padding = {1.4f, 1.6f, 1.4f, 1.6f};
    r.scrollable_overflow_rect = {-0.6f, 20.6f, -0.6f, 30.6f};
    tree.set_unrounded_layout(root, r);
    Layout m; m.location = {10.4f, 10.4f}; m.size = {5.4f, 5.4f}; tree.set_unrounded_layout(middle, m);
    Layout h; h.location = {2.4f, 2.4f}; h.size = {3.4f, 3.4f}; tree.set_unrounded_layout(hoisted, h);
    const std::array hoisted_ids{hoisted}; tree.add_hoisted_children(root, hoisted_ids);
    round_layout(tree, root);
    CHECK(tree.layout(root).location == Point<float>{0, 0});
    CHECK(tree.layout(root).size == Size<float>{21, 30});
    CHECK(tree.layout(middle).location == Point<float>{11, 10});
    CHECK(tree.layout(hoisted).location == Point<float>{3, 2});
    CHECK(tree.layout(hoisted).size == Size<float>{3, 3});
    CHECK(tree.unrounded_layout(root) == r);
    auto once = tree.layout(hoisted); round_layout(tree, root); CHECK(tree.layout(hoisted) == once);
}

TEST_CASE("native root layout rejects callback reentrancy and resets its guard after exceptions", "[cpp][foundation][tree]") {
    Tree tree;
    auto node = tree.new_leaf().value();
    const Size<AvailableSpace> space{AvailableSpace::max_content(), AvailableSpace::max_content()};
    tree.set_leaf_layout([&](LayoutTree&, NodeId, const LayoutInput&) -> LayoutOutput {
        CHECK_THROWS_AS(tree.new_leaf(), TreePanic);
        CHECK_THROWS_AS(tree.set_style(node, {}), TreePanic);
        CHECK_THROWS_AS(tree.compute_layout(node, space), TreePanic);
        throw std::runtime_error("callback exception");
    });
    CHECK_THROWS_AS(tree.compute_layout(node, space), std::runtime_error);
    tree.set_leaf_layout({});
    CHECK_NOTHROW(tree.compute_layout(node, space).value());
}

TEST_CASE("native static position safe fallback aligns margin boxes", "[cpp][foundation][layout]") {
    Point<AxisStaticPosition> position;
    position.x.area = {10, 30};
    position.x.align = {AxisStaticEdge::Center, AlignmentSafety::Safe, AxisStaticEdge::Start};
    position.y = AxisStaticPosition::from_edge(20, AxisStaticEdge::End);
    CHECK(resolve_static_offset(position, {40, 10}, {2, 3, 4, 5}) == Point<float>{12, 5});
    position.x.align.safety = AlignmentSafety::Unsafe;
    CHECK(resolve_static_offset(position, {40, 10}, {2, 3, 4, 5}).x == -0.5f);
}

// Independent storage: exercises the virtual boundary, not Tree's implementation.
class SingleCustomTree : public LayoutTree {
public:
    Style css;
    Layout raw;
    Layout final;
    std::optional<LayoutOutput> remembered;
    LayoutInput latest;
    int calls = 0;
    std::vector<NodeId> child_ids(NodeId) const override { return {}; }
    const Style& style(NodeId) const override { return css; }
    LayoutOutput compute_child_layout(NodeId node, const LayoutInput& input) override {
        latest = input;
        return compute_cached_layout(*this, node, input, [this](LayoutTree&, NodeId, const LayoutInput&) {
            ++calls;
            auto result = LayoutOutput::from_outer_size({27, 31});
            result.baselines = {4, 24}; result.top_margin = {7, -3}; return result;
        });
    }
    LayoutOutput measure(NodeId, const LayoutInput&) override { return LayoutOutput::from_outer_size({9, 11}); }
    std::optional<LayoutOutput> cache_get(NodeId, const LayoutInput&) override { return remembered; }
    void cache_store(NodeId, const LayoutInput&, LayoutOutput output) override { remembered = std::move(output); }
    void cache_clear(NodeId) override { remembered.reset(); }
    const Layout& unrounded_layout(NodeId) const override { return raw; }
    void set_unrounded_layout(NodeId, const Layout& value) override { raw = value; }
    void set_final_layout(NodeId, const Layout& value) override { final = value; }
    void clear_hoisted_children(NodeId) override {}
    void add_hoisted_children(NodeId, std::span<const NodeId>) override {}
    std::vector<NodeId> hoisted_child_ids(NodeId) const override { return {}; }
    ContainingBlockClaims containing_block_claims(NodeId) const override { return ContainingBlockClaims::all(); }
};
class SnapshotChildTree : public SingleCustomTree {
public:
    std::vector<NodeId> ids{NodeId{100}, NodeId{101}, NodeId{102}};
    mutable std::size_t snapshots = 0;
    std::vector<NodeId> child_ids(NodeId) const override { ++snapshots; return ids; }
};
class IndexedChildTree final : public SnapshotChildTree {
public:
    std::size_t child_count(NodeId) const override { return ids.size(); }
    NodeId child_id(NodeId, std::size_t index) const override { return ids.at(index); }
};

TEST_CASE("native child access dispatches without snapshots and retains custom tree defaults", "[cpp][foundation][tree]") {
    const NodeId node{42};
    SnapshotChildTree fallback;
    const LayoutTree& legacy = fallback;
    CHECK(legacy.child_count(node) == 3);
    CHECK(legacy.child_id(node, 1) == fallback.ids[1]);
    CHECK_THROWS_AS(legacy.child_id(node, 3), std::out_of_range);
    CHECK(fallback.snapshots == 3);

    IndexedChildTree indexed;
    const LayoutTree& direct = indexed;
    CHECK(direct.child_count(node) == 3);
    for (std::size_t i = 0; i < direct.child_count(node); ++i)
        CHECK(direct.child_id(node, i) == indexed.ids[i]);
    CHECK_THROWS_AS(direct.child_id(node, 3), std::out_of_range);
    CHECK(indexed.snapshots == 0);
}

TEST_CASE("native Tree indexed child reads track mutations and reject invalid access", "[cpp][foundation][tree]") {
    Tree tree;
    const auto parent = tree.new_leaf().value();
    const auto first = tree.new_leaf().value();
    const auto second = tree.new_leaf().value();
    const LayoutTree& view = tree;
    CHECK(view.child_count(parent) == 0);
    CHECK_THROWS_AS(view.child_id(parent, 0), std::out_of_range);
    tree.set_children(parent, std::array{first, second}).value();
    CHECK(view.child_count(parent) == 2);
    CHECK(view.child_id(parent, 0) == first);
    CHECK(view.child_id(parent, 1) == second);
    CHECK_THROWS_AS(view.child_id(parent, 2), std::out_of_range);
    tree.set_children(parent, std::array{second, first}).value();
    CHECK(view.child_id(parent, 0) == second);
    tree.remove_child(parent, first).value();
    CHECK(view.child_count(parent) == 1);
    CHECK(view.child_id(parent, 0) == second);
    tree.remove(parent).value();
    CHECK_THROWS_AS(view.child_count(parent), NodeAccessError);
    CHECK_THROWS_AS(view.child_id(parent, 0), NodeAccessError);
}

TEST_CASE("native custom tree supports independent storage cache and full output", "[cpp][foundation][custom]") {
    SingleCustomTree tree;
    NodeId node{42};
    auto output = tree.perform_child_layout(node, {}, {}, Size<AvailableSpace>::max_content(), SizingMode::InherentSize);
    CHECK(output.size == Size<float>{27, 31}); CHECK(output.baselines == Baselines{4, 24});
    CHECK(output.top_margin.resolve() == 4);
    CHECK(tree.measure_child_size(node, {}, {}, Size<AvailableSpace>::max_content(), SizingMode::InherentSize, AbsoluteAxis::Vertical) == 31);
    CHECK(tree.calls == 1); CHECK(tree.containing_block_claims(node) == ContainingBlockClaims::all());
    CHECK_THROWS_AS(tree.cache(node), UnsupportedAlgorithm);
    tree.cache_clear(node);
    CHECK(compute_leaf(tree, node, {}).size == Size<float>{9, 11});
    compute_root_layout(tree, node, Size<AvailableSpace>::max_content()); round_layout(tree, node);
    CHECK(tree.final.size == Size<float>{27, 31});
}

TEST_CASE("native shared alignment baselines and overflow helpers preserve CSS rules", "[cpp][foundation][common]") {
    CHECK_FALSE(child_first_baseline({}, 20, Overflow::Visible));
    CHECK(child_first_baseline({}, 20, Overflow::Scroll) == 20);
    CHECK(child_first_baseline(30, 20, Overflow::Hidden) == 20);
    CHECK(child_first_baseline(-5, 20, Overflow::Hidden) == 0);
    CHECK(resolve_self_alignment_safety({AlignItemsKeyword::End, AlignmentSafety::Safe}, true) == AlignItemsKeyword::Start);
    CHECK(resolve_self_alignment_safety({AlignItemsKeyword::End, AlignmentSafety::Unsafe}, true) == AlignItemsKeyword::End);
    CHECK(apply_alignment_fallback(-5, 3, {AlignContentKeyword::SpaceAround, AlignmentSafety::Default}) == AlignContentKeyword::Start);
    CHECK(apply_alignment_fallback(10, 1, {AlignContentKeyword::SpaceBetween, AlignmentSafety::Default}) == AlignContentKeyword::FlexStart);
    CHECK(compute_alignment_offset(60, 3, 5, AlignContentKeyword::SpaceAround, false, true) == 10);
    CHECK(compute_alignment_offset(60, 3, 5, AlignContentKeyword::SpaceAround, false, false) == 25);
    CHECK(compute_alignment_offset(-10, 3, 5, AlignContentKeyword::SpaceAround, false, true) == -5);
    CHECK(compute_alignment_offset(60, 3, 5, AlignContentKeyword::FlexStart, true, true) == 60);
    const Point<Overflow> visible{Overflow::Visible, Overflow::Visible};
    CHECK(compute_scrollable_overflow_contribution({10, 20}, {30, 40}, {-5, 80, -10, 100}, visible, {}, false)
        == Rect<float>{5, 90, 10, 120});
    CHECK(compute_scrollable_overflow_contribution({10, 20}, {30, 40}, {-5, 80, -10, 100}, {Overflow::Visible, Overflow::Scroll}, {}, false)
        == Rect<float>{10, 40, 20, 60});
    CHECK(compute_scrollable_overflow_contribution({10, 20}, {30, 40}, {-5, 80, -10, 100}, visible, Contain::layout(), false)
        == Rect<float>{10, 40, 20, 60});
    CHECK(compute_scrollable_overflow_contribution({-100, 20}, {30, 40}, {}, visible, {}, true) == Rect<float>{});
    CHECK(compute_scrollable_overflow_contribution({-100, 20}, {30, 40}, {}, visible, {}, false) == Rect<float>{-100, -70, 20, 60});
}

TEST_CASE("native low-level layout protects borrowed styles from callback mutation", "[cpp][foundation][safety]") {
    Tree tree;
    auto node = tree.new_leaf().value();
    int calls = 0;
    tree.set_intrinsic_measure([&](const LayoutInput&, NodeId, NodeContext*, const Style&) {
        ++calls;
        CHECK_THROWS_AS(tree.clear(), TreePanic);
        CHECK_THROWS_AS(tree.remove(node), TreePanic);
        CHECK_THROWS_AS(tree.set_style(node, {}), TreePanic);
        return Size<float>{3, 4};
    });
    CHECK(tree.compute_child_layout(node, {}).size == Size<float>{3, 4});
    tree.mark_dirty(node).value();
    CHECK(tree.compute_block_child_layout(node, {}, nullptr).size == Size<float>{3, 4});
    CHECK(compute_leaf(tree, node, {}).size == Size<float>{3, 4});
    CHECK(tree.measure(node, {}).size == Size<float>{3, 4});
    CHECK(calls == 4); CHECK(tree.contains(node));
    tree.set_intrinsic_measure([&](const LayoutInput&, NodeId, NodeContext*, const Style&) -> Size<float> {
        tree.clear(); return {};
    });
    CHECK_THROWS_AS(compute_leaf(tree, node, {}), TreePanic);
    tree.set_intrinsic_measure({});
    CHECK(tree.compute_child_layout(node, {}).size == Size<float>{});
    tree.set_calc_resolver([&](Calc, float) { CHECK_THROWS_AS(tree.clear(), TreePanic); return 7.0f; });
    CHECK(tree.resolve_calc({}, 100) == 7);
    tree.set_calc_resolver({});
    CHECK_NOTHROW(tree.clear());
}

TEST_CASE("native public widened grid coordinates reject arithmetic overflow", "[cpp][foundation][safety]") {
    OofCandidates aliased;
    CHECK_THROWS_AS(aliased.append(aliased), std::invalid_argument);
    constexpr auto high = std::numeric_limits<std::int32_t>::max();
    constexpr auto low = std::numeric_limits<std::int32_t>::min();
    const auto automatic = OriginZeroGridPlacement::auto_();
    CHECK_THROWS_AS(resolve_definite_grid_lines({OriginZeroGridPlacement::line(high), automatic}), std::overflow_error);
    CHECK_THROWS_AS(resolve_definite_grid_lines({OriginZeroGridPlacement::line(high), OriginZeroGridPlacement::line(high)}), std::overflow_error);
    CHECK_THROWS_AS(resolve_definite_grid_lines({automatic, OriginZeroGridPlacement::line(low)}), std::overflow_error);
    CHECK_THROWS_AS(resolve_indefinite_grid_tracks({}, high), std::overflow_error);
    CHECK_THROWS_AS(resolve_absolutely_positioned_grid_tracks({OriginZeroGridPlacement::line(high), OriginZeroGridPlacement::span(2)}), std::overflow_error);
    CHECK(resolve_definite_grid_lines({OriginZeroGridPlacement::line(high - 1), automatic}) == Line<std::int32_t>{high - 1, high});
    CHECK(resolve_definite_grid_lines({automatic, OriginZeroGridPlacement::line(low + 1)}) == Line<std::int32_t>{low, low + 1});
}

TEST_CASE("native absolute sizing keywords route through full custom child layout", "[cpp][foundation][keyword]") {
    SingleCustomTree tree;
    const NodeId node{42};
    Size<Opt> known;
    resolve_absolute_sizing_keywords(tree, node, known,
        {Dimension::min_content(), Dimension::max_content()}, {100, 80}, {}, {}, SizingMode::ContentSize);
    CHECK(known == Size<Opt>{27, 31});
    CHECK(tree.calls == 1);
    CHECK(tree.latest.axis == RequestedAxis::Both);
    CHECK(tree.latest.run_mode == RunMode::ComputeSize);
    CHECK(tree.latest.sizing_mode == SizingMode::ContentSize);
    CHECK(tree.latest.available_space == Size<AvailableSpace>{AvailableSpace::min_content(), AvailableSpace::max_content()});
    CHECK(tree.latest.parent_size == Size<Opt>{100, 80});
    known = {std::nullopt, 20};
    resolve_absolute_sizing_keywords(tree, node, known,
        {Dimension::stretch(), Dimension::auto_()}, {100, 80}, {5, 7, 2, 4}, {3, 4, 1, 3}, SizingMode::InherentSize);
    CHECK(known == Size<Opt>{81, 20}); CHECK(tree.calls == 1);
    known = {};
    tree.remembered.reset();
    resolve_absolute_sizing_keywords(tree, node, known,
        {Dimension::fit_content_px(40), Dimension::auto_()}, {100, 80}, {5, 7, 2, 4}, {3, 4, 1, 3}, SizingMode::InherentSize);
    CHECK(known == Size<Opt>{27, std::nullopt});
    CHECK(tree.latest.axis == RequestedAxis::Horizontal);
    CHECK(tree.latest.available_space == Size<AvailableSpace>{AvailableSpace::definite(40), AvailableSpace::definite(70)});
    CHECK(tree.calls == 2);
}
} // namespace
