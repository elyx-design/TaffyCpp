#include "grid_native.h"

using namespace grid_behavior;

TEST_CASE("detailed_grid_info.rs::no_line_names", "[behavior][grid][detailed_grid_info]") {
    Native tree;
    const auto child = tree.node();
    const auto root = tree.node({{"display", "grid"}, {"width", "100px"}, {"height", "50px"},
        {"grid-template-columns", "40px 60px"}, {"grid-template-rows", "50px"}}, {child});
    tree.compute(root, definite(100), definite(50));
    // Supplemental checks read genuine native TrackCounts, not style-derived counts.
    REQUIRE(tree.axis(root, 1).negative_implicit == 0);
    REQUIRE(tree.axis(root, 1).explicit_tracks == 2);
    REQUIRE(tree.axis(root, 1).positive_implicit == 0);
    REQUIRE(tree.axis(root, 1).position_count == 2);
    REQUIRE(tree.axis(root, 0).explicit_tracks == 1);
    REQUIRE(tree.axis(root, 1).names_empty);
    REQUIRE(tree.axis(root, 0).names_empty);
    REQUIRE(tree.axis(root, 1).line_count == 0);
    REQUIRE(tree.tracks(root, 1, 1) == "40px 60px");
    REQUIRE(tree.tracks(root, 0, 1) == "50px");
}

TEST_CASE("detailed_grid_info.rs::zoomed_track_list", "[behavior][grid][detailed_grid_info]") {
    Native tree;
    const auto child = tree.node();
    const auto root = tree.node({{"display", "grid"}, {"grid-template-columns", "80px 120px"},
        {"grid-template-rows", "100px"}}, {child});
    tree.compute(root, definite(200), definite(100));
    REQUIRE(tree.tracks(root, 1, 2) == "40px 60px");
    REQUIRE(tree.tracks(root, 0, 2) == "50px");
}

TEST_CASE("detailed_grid_info.rs::template_and_area_line_names", "[behavior][grid][detailed_grid_info]") {
    Native tree;
    const auto child = tree.node();
    const auto root = tree.node({{"display", "grid"}, {"width", "100px"}, {"height", "50px"},
        {"grid-template-columns", "40px 60px"}, {"grid-template-rows", "50px"}}, {child});
    tree.field(root, "grid_template_column_names", R"([["full-start"],["main-start"],["main-end","full-end"]])");
    tree.field(root, "grid_template_areas", R"({"areas":[{"name":"hero","row_start":1,"row_end":2,"column_start":1,"column_end":3}],"row_count":1,"column_count":2})");
    tree.compute(root, definite(100), definite(50));
    REQUIRE(tree.axis(root, 1).line_count == 3);
    REQUIRE(tree.line_names(root, 1, 0) == Names{"full-start"});
    REQUIRE(tree.line_names(root, 1, 1) == Names{"main-start"});
    REQUIRE(tree.line_names(root, 1, 2) == (Names{"main-end", "full-end"}));
    REQUIRE(tree.names_for_line(root, 1, 1) == Names{"main-start"});
    // Area-generated names must not contaminate the resolved track list.
    REQUIRE(tree.axis(root, 0).line_count == 0);
    REQUIRE(tree.tracks(root, 1, 1) == "[full-start] 40px [main-start] 60px [main-end full-end]");
    REQUIRE(tree.tracks(root, 0, 1) == "50px");
    rect_is(tree.absolute(root, line(1), line(2), named("hero-start", 1), named("hero-end", 1), false,
        {0, 100, 0, 50}), {0, 100, 0, 50});
}

TEST_CASE("detailed_grid_info.rs::repeat_expansion_line_names", "[behavior][grid][detailed_grid_info]") {
    Native tree;
    const auto child = tree.node();
    const auto root = tree.node({{"display", "grid"}, {"width", "100px"}, {"height", "50px"},
        {"grid-template-columns", "repeat(2, [col-start] 50px [col-end])"}, {"grid-template-rows", "50px"}}, {child});
    tree.field(root, "grid_template_column_names", R"([["outer-start"],["outer-end"]])");
    tree.compute(root, definite(100), definite(50));
    REQUIRE(tree.axis(root, 1).line_count == 3);
    REQUIRE(tree.line_names(root, 1, 0) == (Names{"outer-start", "col-start"}));
    REQUIRE(tree.line_names(root, 1, 1) == (Names{"col-end", "col-start"}));
    REQUIRE(tree.line_names(root, 1, 2) == (Names{"col-end", "outer-end"}));
    REQUIRE(tree.tracks(root, 1, 1) == "[outer-start col-start] 50px [col-end col-start] 50px [col-end outer-end]");
}

TEST_CASE("detailed_grid_info.rs::implicit_tracks_shift_line_names", "[behavior][grid][detailed_grid_info]") {
    Native tree;
    const auto child = tree.node({{"grid-column-start", "-3"}});
    const auto root = tree.node({{"display", "grid"}, {"width", "100px"}, {"height", "50px"},
        {"grid-template-columns", "50px"}, {"grid-template-rows", "50px"}}, {child});
    tree.field(root, "grid_template_column_names", R"([["a"],["b"]])");
    tree.compute(root, definite(100), definite(50));
    REQUIRE(tree.axis(root, 1).negative_implicit == 1);
    REQUIRE(tree.axis(root, 1).explicit_tracks == 1);
    REQUIRE(tree.axis(root, 1).positive_implicit == 0);
    REQUIRE(tree.axis(root, 1).position_count == 2);
    REQUIRE(tree.axis(root, 1).line_count == 3);
    REQUIRE(tree.line_names(root, 1, 0).empty());
    REQUIRE(tree.line_names(root, 1, 1) == Names{"a"});
    REQUIRE(tree.line_names(root, 1, 2) == Names{"b"});
    const auto first = tree.position(root, 1, 0);
    // Source formats the actual native implicit track length, not a fixed guess.
    REQUIRE(tree.tracks(root, 1, 1) == pixels(first.end - first.start) + "px [a] 50px [b]");
}

TEST_CASE("detailed_grid_info.rs::item_grid_area", "[behavior][grid][detailed_grid_info]") {
    Native tree;
    const auto child_a = tree.node();
    const auto child_b = tree.node({{"grid-row-start", "2"}, {"grid-column-start", "1"}, {"grid-column-end", "3"}});
    const auto root = tree.node({{"display", "grid"}, {"grid-template-columns", "40px 60px"},
        {"grid-template-rows", "50px 30px"}, {"column-gap", "10px"}, {"row-gap", "10px"},
        {"padding-left", "5px"}, {"padding-right", "5px"}, {"padding-top", "5px"}, {"padding-bottom", "5px"}}, {child_a, child_b});
    tree.compute(root);
    area_is(tree.item(root, 0), 5, 5, 40, 50);
    area_is(tree.item(root, 1), 5, 65, 110, 30);
    REQUIRE_FALSE(tree.item(root, 2).has_value());
    const BehaviorRect padding{0, 120, 0, 100};
    rect_is(tree.absolute(root, line(1), line(3), line(1), line(3), false, padding), {5, 115, 5, 95});
    rect_is(tree.absolute(root, automatic(), automatic(), automatic(), line(2), false, padding), {0, 45, 0, 100});
    rect_is(tree.absolute(root, automatic(), automatic(), line(-1), automatic(), false, padding), {115, 120, 0, 100});
    rect_is(tree.absolute(root, automatic(), automatic(), named("missing", 1), line(2), false, padding), {55, 120, 0, 100});
    rect_is(tree.absolute(root, automatic(), automatic(), span(2), span(3), false, padding), {0, 120, 0, 100});
}

TEST_CASE("detailed_grid_info.rs::absolute_grid_area_named_span", "[behavior][grid][detailed_grid_info]") {
    Native tree;
    const auto child = tree.node();
    const auto root = tree.node({{"display", "grid"}, {"width", "90px"}, {"height", "50px"},
        {"grid-template-columns", "30px 30px 30px"}, {"grid-template-rows", "50px"}}, {child});
    tree.field(root, "grid_template_column_names", R"([["a"],["b"],["b"],["c"]])");
    tree.compute(root, definite(90), definite(50));
    rect_is(tree.absolute(root, automatic(), automatic(), named("a", 1), named_span("b", 2), false,
        {0, 90, 0, 50}), {0, 60, 0, 50});
    rect_is(tree.absolute(root, automatic(), automatic(), named("b", 2), named("c", 1), false,
        {0, 90, 0, 50}), {60, 90, 0, 50});
}

TEST_CASE("detailed_grid_info.rs::rtl_line_names_are_logical_order", "[behavior][grid][detailed_grid_info]") {
    Native tree;
    const auto child = tree.node();
    const auto root = tree.node({{"display", "grid"}, {"direction", "rtl"}, {"width", "100px"}, {"height", "50px"},
        {"grid-template-columns", "40px 60px"}, {"grid-template-rows", "50px"}}, {child});
    tree.field(root, "grid_template_column_names", R"([["a"],["b"],["c"]])");
    tree.compute(root, definite(100), definite(50));
    REQUIRE(tree.line_names(root, 1, 0) == Names{"a"});
    REQUIRE(tree.line_names(root, 1, 1) == Names{"b"});
    REQUIRE(tree.line_names(root, 1, 2) == Names{"c"});
    REQUIRE(tree.tracks(root, 1, 1) == "[a] 40px [b] 60px [c]");
    const auto first = tree.position(root, 1, 0);
    REQUIRE(first.start == 60);
    REQUIRE(first.end == 100);
    const auto second = tree.position(root, 1, 1);
    REQUIRE(second.start == 0);
    REQUIRE(second.end == 60);
    area_is(tree.item(root, 0), 60, 0, 40, 50);
    rect_is(tree.absolute(root, automatic(), automatic(), line(1), line(2), true, {0, 100, 0, 50}), {60, 100, 0, 50});
    rect_is(tree.absolute(root, automatic(), automatic(), named("a", 1), named("b", 1), true, {0, 100, 0, 50}), {60, 100, 0, 50});
    rect_is(tree.absolute(root, automatic(), automatic(), line(1), automatic(), true, {0, 100, 0, 50}), {0, 100, 0, 50});
}

namespace {
BehaviorSize measure_flex_child(std::string_view wrap, bool fit_content) {
    Native tree;
    const auto child = fit_content ? tree.node({{"width", "fit-content"}}) : tree.node();
    tree.ahem(child, "HHHHHHHHHH\xe2\x80\x8bHHHHHHHHHH\xe2\x80\x8bHHHHHHHHHH");
    const auto root = tree.node({{"display", "flex"}, {"flex-direction", "column"}, {"flex-wrap", wrap},
        {"flex-line-count", "2"}, {"width", "210px"}, {"height", "100px"},
        {"column-gap", "10px"}, {"row-gap", "0px"}, {"align-items", "start"}, {"align-content", "start"}}, {child});
    tree.compute_text(root);
    return tree.layout(child).size;
}
void percentage_column(BehaviorScalar available) {
    Native tree;
    const auto item = tree.node();
    tree.ahem(item, "HH\xe2\x80\x8bHH\xe2\x80\x8bHH");
    const auto root = tree.node({{"display", "grid"}, {"grid-template-columns", "fit-content(50%)"},
        {"grid-template-rows", "40px"}}, {item});
    tree.compute_text(root, available, max_content);
    REQUIRE(tree.layout(root).size.width == 60);
    REQUIRE(tree.layout(item).size.width == 30);
}
void percentage_rows(BehaviorScalar available) {
    Native tree;
    const auto a = tree.node({{"width", "20px"}, {"height", "20px"}});
    const auto b = tree.node({{"width", "20px"}, {"height", "60px"}});
    const auto c = tree.node({{"width", "20px"}, {"height", "20px"}});
    const auto root = tree.node({{"display", "grid"}, {"grid-template-columns", "auto"},
        {"grid-template-rows", "auto 20% auto"}}, {a, b, c});
    tree.compute(root, definite(200), available);
    const auto total = tree.layout(root).size.height;
    const auto by = tree.layout(b).y;
    const auto cy = tree.layout(c).y;
    REQUIRE(total == 100);
    REQUIRE(by == 40);
    REQUIRE(cy - by == 20);
    REQUIRE(total - cy == 40);
}
}

TEST_CASE("flex_line_count.rs::wrap_line_count_divides_cross_available_space", "[behavior][flex_line_count]") {
    size_is(measure_flex_child("wrap", false), 100, 30);
}
TEST_CASE("flex_line_count.rs::wrap_reverse_line_count_divides_cross_available_space", "[behavior][flex_line_count]") {
    size_is(measure_flex_child("wrap-reverse", false), 100, 30);
}
TEST_CASE("flex_line_count.rs::nowrap_line_count_has_no_effect", "[behavior][flex_line_count]") {
    size_is(measure_flex_child("nowrap", false), 210, 20);
}
TEST_CASE("flex_line_count.rs::wrap_line_count_divides_fit_content_cross_size", "[behavior][flex_line_count]") {
    size_is(measure_flex_child("wrap", true), 100, 30);
}
TEST_CASE("flex_line_count.rs::nowrap_line_count_does_not_divide_fit_content_cross_size", "[behavior][flex_line_count]") {
    size_is(measure_flex_child("nowrap", true), 210, 20);
}
TEST_CASE("grid_percentage_rerun.rs::fit_content_percentage_indefinite_container_max_content_available_space", "[behavior][grid_percentage_rerun]") {
    percentage_column(max_content);
}
TEST_CASE("grid_percentage_rerun.rs::fit_content_percentage_indefinite_container_definite_available_space", "[behavior][grid_percentage_rerun]") {
    percentage_column(definite(200));
}
TEST_CASE("grid_percentage_rerun.rs::percentage_rows_indefinite_container_max_content_available_space", "[behavior][grid_percentage_rerun]") {
    percentage_rows(max_content);
}
TEST_CASE("grid_percentage_rerun.rs::percentage_rows_indefinite_container_definite_available_space", "[behavior][grid_percentage_rerun]") {
    percentage_rows(definite(500));
}

TEST_CASE("serde.rs::serde_can_serialize", "[behavior][serde]") {
    Native tree;
    // Exact source operation: serialize actual Style<String>::DEFAULT successfully.
    REQUIRE_FALSE(tree.serialize_default_style().empty());
}
TEST_CASE("serde.rs::serde_can_deserialize_partial_values", "[behavior][serde]") {
    Native tree;
    // Deliberately NOT a Style deserialization test. Preserve the source's weak
    // serde_json::Value-only assertion and all of its original input values.
    tree.parse_value(R"({
        "inset": { "left": { "Length": 22 }, "right": "Auto" },
        "size": { "width": { "Percent": 50 } },
        "min_size": { "height": { "Length": 10 } },
        "max_size": { "width": "Auto" },
        "margin": { "right": { "Length": 99.0 }, "bottom": { "Length": 99.0 } },
        "padding": { "left": { "Length": 99.0 } },
        "border": { "bottom": { "Length": 99.0 } },
        "gap": { "width": { "Length": 99.0 } },
        "grid_row": { "start": "Auto" },
        "grid_column": { "end": "Auto" }
    })");
}
