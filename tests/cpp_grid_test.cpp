#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <taffy/compute.hpp>
#include "../src/grid.hpp"
#include <taffy/tree.hpp>

using namespace taffy;

TEST_CASE("native grid tracks preserve fixture padding and space-between geometry",
          "[grid][alignment]") {
    // grid_align_content_space_between_with_padding_border__border_box_ltr.xml
    std::array<grid::GridTrack, 7> rows{};
    rows[1].base_size = rows[3].base_size = rows[5].base_size = 40;
    grid::align_tracks(152, {10, 30}, {2, 6}, rows, AlignContent::space_between(), false);
    CHECK(rows[1].offset == 12);
    CHECK(rows[3].offset == 68);
    CHECK(rows[5].offset == 124);
}

TEST_CASE("native grid item centered fixture includes negative alignment space",
          "[grid][alignment]") {
    // grid_align_items_sized_center__border_box_ltr.xml
    Tree tree;
    for (const auto size : {20.0f, 60.0f}) {
        Style style;
        style.position = Position::Relative;
        style.size = Size<Dimension>::from_lengths(size, size);
        const auto node = tree.new_leaf(style).value();
        const float start = size == 20 ? 0 : 80;
        OofCandidates bubbled;
        const auto [overflow, y, height, baseline] =
            grid::align_and_position_item(tree, node, 0, {start, start + 40, start, start + 40},
                                          {40, 40}, {AlignItems::normal(), AlignItems::center()}, 0,
                                          Direction::Ltr, 120, {}, false, bubbled);
        (void)overflow;
        (void)baseline;
        CHECK(tree.unrounded_layout(node).location.x == start);
        CHECK(y == (size == 20 ? 10 : 70));
        CHECK(height == size);
        CHECK(tree.unrounded_layout(node).size == Size<float>{size, size});
        CHECK(bubbled.empty());
    }
}

TEST_CASE("native grid relative insets do not shrink stretched fixture item", "[grid][alignment]") {
    // grid_relative_all_sides__border_box_ltr.xml
    Tree tree;
    Style style;
    style.position = Position::Relative;
    style.size.width = Dimension::length(40);
    style.inset = Rect<LengthPercentageAuto>::from_length(10, 10, 10, 10);
    const auto node = tree.new_leaf(style).value();
    OofCandidates bubbled;
    const auto [overflow, y, height, baseline] =
        grid::align_and_position_item(tree, node, 2, {0, 100, 0, 100}, {100, 100}, {}, 0,
                                      Direction::Ltr, 100, {}, false, bubbled);
    (void)overflow;
    (void)baseline;
    CHECK(tree.unrounded_layout(node).location == Point<float>{10, 10});
    CHECK(tree.unrounded_layout(node).size == Size<float>{40, 100});
    CHECK(tree.unrounded_layout(node).order == 2);
    CHECK(y == 0);
    CHECK(height == 100);
}

TEST_CASE("native grid intrinsic keywords share a two-axis measurement", "[grid][alignment]") {
    Tree tree;
    Style style;
    style.size = {Dimension::min_content(), Dimension::max_content()};
    const auto node = tree.new_leaf(style).value();
    unsigned measures = 0, layouts = 0;
    tree.set_leaf_layout([&](LayoutTree&, NodeId, const LayoutInput& input) {
        if (input.run_mode == RunMode::ComputeSize) {
            ++measures;
            CHECK(input.available_space.width.kind() == AvailableSpace::Kind::MinContent);
            CHECK(input.available_space.height.kind() == AvailableSpace::Kind::MaxContent);
            CHECK(input.known_dimensions == Size<std::optional<float>>{});
        } else {
            ++layouts;
            CHECK(input.known_dimensions == Size<std::optional<float>>{25, 35});
        }
        return LayoutOutput::from_outer_size({25, 35});
    });
    OofCandidates bubbled;
    grid::align_and_position_item(tree, node, 0, {0, 100, 0, 80}, {100, 80}, {}, 0, Direction::Ltr,
                                  100, {}, false, bubbled);
    CHECK(measures == 1);
    CHECK(layouts == 1);
    CHECK(tree.unrounded_layout(node).size == Size<float>{25, 35});
}

TEST_CASE("native grid bubbles anchors and excludes relative offsets from baseline position",
          "[grid][alignment]") {
    Tree tree;
    Style style;
    style.position = Position::Relative;
    style.inset = Rect<LengthPercentageAuto>::from_length(7, 11, 9, 13);
    style.size = Size<Dimension>::from_lengths(30, 20);
    style.overflow.y = Overflow::Scroll;
    style.scrollbar_width = 3;
    const auto node = tree.new_leaf(style).value();
    tree.set_leaf_layout([](LayoutTree&, NodeId, const LayoutInput&) {
        auto output = LayoutOutput::from_sizes_and_baselines({30, 20}, {0, 50, 0, 40},
                                                             Baselines::from_first(25));
        OofCandidate candidate;
        candidate.node = NodeId{999};
        candidate.static_position = {AxisStaticPosition::from_edge(2, AxisStaticEdge::Start),
                                     AxisStaticPosition::from_edge(4, AxisStaticEdge::Start)};
        output.oof_candidates.push(candidate);
        return output;
    });
    OofCandidates bubbled;
    const auto [overflow, y, height, baseline] = grid::align_and_position_item(
        tree, node, 0, {10, 110, 20, 100}, {100, 80}, {AlignSelf::start(), AlignSelf::baseline()},
        5, Direction::Rtl, 140, {2, 4, 3, 6}, true, bubbled);
    CHECK(y == 25);
    CHECK(height == 20);
    CHECK(baseline == 20); // Scroll container clamps the child's supplied baseline.
    CHECK(tree.unrounded_layout(node).location == Point<float>{69, 34});
    CHECK(tree.unrounded_layout(node).scrollbar_size == Size<float>{3, 0});
    CHECK(tree.unrounded_layout(node).margin.top == 5);
    REQUIRE(bubbled.size() == 1);
    CHECK(bubbled.inner[0].static_position.x.area == Line<float>{71, 71});
    CHECK(bubbled.inner[0].static_position.y.area == Line<float>{38, 38});
    CHECK(overflow == Rect<float>{37, 67, 31, 51});
}

TEST_CASE("native grid RTL track alignment retains collapsed and empty lines",
          "[grid][alignment]") {
    std::array<grid::GridTrack, 5> tracks{};
    tracks[1].base_size = 20;
    tracks[3].base_size = 30;
    grid::align_tracks(100, {2, 3}, {4, 5}, tracks, AlignContent::start(), true);
    CHECK(tracks[3].offset == 56);
    CHECK(tracks[1].offset == 86);
    tracks[1].base_size = tracks[3].base_size = 0;
    tracks[1].is_collapsed = tracks[3].is_collapsed = true;
    grid::align_tracks(100, {2, 3}, {4, 5}, tracks, AlignContent::start(), true);
    for (const auto& track : tracks)
        CHECK(track.offset == 106);
}

TEST_CASE("native grid auto margins and safe overflow are axis-local", "[grid][alignment]") {
    const auto [offset, margin] =
        grid::align_item_within_area({10, 110}, AlignSelf::end(), 40, Position::Static, {},
                                     {std::nullopt, 10}, 0, Direction::Ltr);
    CHECK(offset == 60);
    CHECK(margin == Line<float>{50, 10});
    CHECK(grid::align_item_within_area({10, 50}, AlignSelf::safe_end(), 60, Position::Static, {},
                                       {0, 0}, 0, Direction::Ltr)
              .first == 10);
    CHECK(grid::align_item_within_area({10, 50}, AlignSelf::safe_end(), 60, Position::Static, {},
                                       {0, 0}, 0, Direction::Rtl)
              .first == -10);
}

namespace {
using GP = GridPlacement;
using GT = GridTemplateComponent;
using TS = TrackSizingFunction;

Style fixture_grid() {
    Style style;
    style.display = Display::Grid;
    style.position = Position::Absolute;
    return style;
}
Style fixture_item() {
    Style style;
    style.position = Position::Relative;
    return style;
}
void fixture_layout(Tree& tree, NodeId root, Size<float> size) {
    REQUIRE(
        tree.compute_layout(root, {AvailableSpace::max_content(), AvailableSpace::max_content()}));
    CHECK(tree.layout(root).location == Point<float>{0, 0});
    CHECK(tree.layout(root).size == size);
}
void fixture_box(const Tree& tree, NodeId node, float x, float y, float width, float height) {
    CHECK(tree.layout(node).location == Point<float>{x, y});
    CHECK(tree.layout(node).size == Size<float>{width, height});
}
const DetailedGridInfo& fixture_info(const Tree& tree, NodeId node) {
    const auto& info = tree.detailed_layout_info(node);
    REQUIRE(std::holds_alternative<DetailedGridInfo>(info));
    return std::get<DetailedGridInfo>(info);
}
Style repeated_names_grid() {
    auto style = fixture_grid();
    style.grid_template_rows = {GT::length(20), GT::length(40), GT::length(80)};
    style.grid_template_columns = {GT::length(50), GT::length(100), GT::length(200)};
    style.grid_template_row_names = {{"row"}, {"row", "mid"}, {"row"}, {"row"}};
    style.grid_template_column_names = {{"col"}, {"col", "mid"}, {"col"}, {"col"}};
    style.grid_auto_rows = {TS::length(10)};
    style.grid_auto_columns = {TS::length(5)};
    return style;
}
} // namespace

TEST_CASE("native grid whole container named area edge priority", "[grid][container][fixture]") {
    // grid_placement_named_area_edge_priority__border_box_ltr.xml
    Tree tree;
    auto style = repeated_names_grid();
    style.grid_template_row_names =
        style.grid_template_column_names = {{"a"}, {"a-start"}, {"a-end"}, {"a"}};
    std::array<NodeId, 3> children;
    auto child = fixture_item();
    child.grid_row = child.grid_column = {GP::named_line("a"), GP::named_line("a")};
    children[0] = tree.new_leaf(child).value();
    child.grid_row = child.grid_column = {GP::named_line("a", 1), GP::named_line("a", 2)};
    children[1] = tree.new_leaf(child).value();
    child.grid_row = {GP::named_span("a"), GP::named_line("a")};
    child.grid_column = {GP::named_line("a"), GP::named_span("a")};
    children[2] = tree.new_leaf(child).value();
    const auto root = tree.new_with_children(style, children).value();
    fixture_layout(tree, root, {350, 140});
    fixture_box(tree, children[0], 50, 20, 100, 40);
    fixture_box(tree, children[1], 0, 0, 350, 140);
    fixture_box(tree, children[2], 50, 0, 300, 60);
    const auto& info = fixture_info(tree, root);
    CHECK(info.grid_template_columns() == "[a] 50px [a-start] 100px [a-end] 200px [a]");
    CHECK(info.grid_template_rows() == "[a] 20px [a-start] 40px [a-end] 80px [a]");
    REQUIRE(info.items.size() == 3);
    CHECK(info.item_grid_area(0) == std::pair{Point<float>{50, 20}, Size<float>{100, 40}});
}

TEST_CASE("native grid whole container template area supplies implicit names",
          "[grid][container][metadata]") {
    // Constructed area variant of grid_placement_named_area_edge_priority: same
    // 350x140 tracks and middle-cell geometry; XML uses explicit a-start/a-end.
    Tree tree;
    auto style = repeated_names_grid();
    style.grid_template_areas = GridTemplateAreas{{{"a", 2, 3, 2, 3}}, 3, 3};
    auto child = fixture_item();
    child.grid_row = child.grid_column = {GP::named_line("a"), GP::named_line("a")};
    const auto node = tree.new_leaf(child).value();
    const auto root = tree.new_with_children(style, std::array{node}).value();
    fixture_layout(tree, root, {350, 140});
    fixture_box(tree, node, 50, 20, 100, 40);
    CHECK(fixture_info(tree, root).areas.at("a") == GridAreaLines{2, 3, 2, 3});
}

TEST_CASE("native grid whole container repeated named lines negative indices",
          "[grid][container][fixture]") {
    // grid_placement_named_line_negative_index__border_box_ltr.xml
    Tree tree;
    const auto style = repeated_names_grid();
    const std::array<Line<GP>, 6> rows{{{GP::named_line("row", -2), {}},
                                        {GP::named_line("mid", -1), {}},
                                        {GP::named_line("mid", -3), {}},
                                        {GP::named_line("row", -6), {}},
                                        {GP::named_line("foo", -2), {}},
                                        {GP::named_line("mid", -2), GP::named_line("foo", -1)}}};
    const std::array<Line<GP>, 6> columns{{{GP::named_line("col", -1), {}},
                                           {GP::named_line("mid", -1), {}},
                                           {GP::named_line("mid", -2), {}},
                                           {GP::named_line("col", -5), {}},
                                           {GP::named_line("foo", -1), {}},
                                           {GP::named_line("foo", -3), GP::named_line("mid", -2)}}};
    std::array<NodeId, 6> children;
    for (std::size_t i = 0; i < children.size(); ++i) {
        auto child = fixture_item();
        child.grid_row = rows[i];
        child.grid_column = columns[i];
        children[i] = tree.new_leaf(child).value();
    }
    const auto root = tree.new_with_children(style, children).value();
    fixture_layout(tree, root, {370, 160});
    fixture_box(tree, children[0], 365, 80, 5, 80);
    fixture_box(tree, children[1], 65, 40, 100, 40);
    for (auto i : {2, 3, 4})
        fixture_box(tree, children[i], 10, 0, 5, 10);
    fixture_box(tree, children[5], 0, 10, 10, 10);
    const auto& info = fixture_info(tree, root);
    CHECK(info.columns.negative_implicit_tracks == 3);
    CHECK(info.columns.explicit_tracks == 3);
    CHECK(info.columns.positive_implicit_tracks == 1);
    CHECK(info.rows.negative_implicit_tracks == 2);
    CHECK(info.grid_template_columns() ==
          "5px 5px 5px [col] 50px [col mid] 100px [col] 200px [col] 5px");
}

TEST_CASE("native grid whole container backwards named spans", "[grid][container][fixture]") {
    // grid_placement_named_span_backwards__border_box_ltr.xml
    Tree tree;
    std::array<NodeId, 4> children;
    for (std::uint16_t i = 0; i < 4; ++i) {
        auto child = fixture_item();
        child.grid_row = {GP::named_span(i == 3 ? "mid" : "row", i == 3 ? 0 : i + 1),
                          GP::line(i == 3 ? -1 : 4)};
        child.grid_column = {GP::named_span(i == 3 ? "mid" : "col", i == 3 ? 0 : i + 1),
                             GP::line(i == 3   ? -1
                                      : i == 2 ? 4
                                               : 3)};
        children[i] = tree.new_leaf(child).value();
    }
    const auto root = tree.new_with_children(repeated_names_grid(), children).value();
    fixture_layout(tree, root, {350, 140});
    fixture_box(tree, children[0], 50, 60, 100, 80);
    fixture_box(tree, children[1], 0, 20, 150, 120);
    fixture_box(tree, children[2], 0, 0, 350, 140);
    fixture_box(tree, children[3], 50, 20, 300, 120);
}

TEST_CASE("native grid whole container auto placement enters negative implicit tracks",
          "[grid][container][fixture]") {
    // grid_placement_auto_negative__border_box_ltr.xml
    Tree tree;
    auto style = fixture_grid();
    style.size = Size<Dimension>::from_lengths(120, 120);
    style.grid_template_rows = style.grid_template_columns = {GT::length(40), GT::length(40)};
    auto child = fixture_item();
    child.grid_row.start = GP::line(1);
    child.grid_column.start = GP::line(-5);
    const auto a = tree.new_leaf(child).value();
    const auto b = tree.new_leaf(fixture_item()).value();
    child = fixture_item();
    child.grid_row.start = GP::line(2);
    const auto c = tree.new_leaf(child).value();
    const auto root = tree.new_with_children(style, std::array{a, b, c}).value();
    fixture_layout(tree, root, {120, 120});
    fixture_box(tree, a, 0, 0, 20, 40);
    fixture_box(tree, b, 20, 0, 20, 40);
    fixture_box(tree, c, 0, 40, 20, 40);
    CHECK(fixture_info(tree, root).grid_template_columns() == "20px 20px 40px 40px");
}

TEST_CASE("native grid whole container minmax and fr", "[grid][container][fixture]") {
    // grid_minmax_column_with_fr_fixed__border_box_ltr.xml
    Tree tree;
    auto style = fixture_grid();
    style.size.width = Dimension::length(60);
    style.grid_template_rows = {GT::length(40)};
    style.grid_template_columns = {
        minmax(MinTrackSizingFunction::length(20), MaxTrackSizingFunction::length(40)), GT::fr(1)};
    const auto a = tree.new_leaf(fixture_item()).value(), b = tree.new_leaf(fixture_item()).value();
    const auto root = tree.new_with_children(style, std::array{a, b}).value();
    fixture_layout(tree, root, {60, 40});
    fixture_box(tree, a, 0, 0, 40, 40);
    fixture_box(tree, b, 40, 0, 20, 40);
    CHECK(fixture_info(tree, root).grid_template_columns() == "40px 20px");
}

TEST_CASE("native grid whole container fit content caps intrinsic text",
          "[grid][container][fixture]") {
    // grid_fit_content_points_argument__border_box_ltr.xml, HH<ZWSP>HH:
    // deterministic Ahem-equivalent min-content 20, max-content 40, line height 10.
    Tree tree;
    tree.set_intrinsic_measure([](const LayoutInput& input, NodeId, NodeContext*, const Style&) {
        const auto space = input.available_space.width;
        const float width = input.known_dimensions.width.value_or(
            space.kind() == AvailableSpace::Kind::MinContent ? 20.f
            : space.kind() == AvailableSpace::Kind::Definite ? std::min(40.f, space.value())
                                                             : 40.f);
        return Size<float>{width, width < 40 ? 20.f : 10.f};
    });
    auto style = fixture_grid();
    style.grid_template_rows = {GT::length(40)};
    style.grid_template_columns = {GT::fit_content(LengthPercentage::length(30))};
    const auto child = tree.new_leaf(fixture_item()).value();
    const auto root = tree.new_with_children(style, std::array{child}).value();
    fixture_layout(tree, root, {30, 40});
    fixture_box(tree, child, 0, 0, 30, 40);
    CHECK(fixture_info(tree, root).grid_template_columns() == "30px");
}

TEST_CASE("native grid whole container auto fit collapses empty tracks",
          "[grid][container][fixture]") {
    // grid_auto_fit_with_empty_auto_track__border_box_ltr.xml (rounded XML).
    Tree tree;
    auto style = fixture_grid();
    style.size = Size<Dimension>::from_lengths(120, 120);
    style.justify_content = JustifyContent::space_evenly();
    style.grid_template_rows = {GT::length(40), GT::length(40), GT::length(40)};
    style.grid_template_columns = {repeat(RepetitionCount::auto_fit(), {TS::length(40)})};
    const auto a = tree.new_leaf(fixture_item()).value(), b = tree.new_leaf(fixture_item()).value();
    const auto root = tree.new_with_children(style, std::array{a, b}).value();
    fixture_layout(tree, root, {120, 120});
    fixture_box(tree, a, 13, 0, 40, 40);
    fixture_box(tree, b, 67, 0, 40, 40);
    CHECK(tree.unrounded_layout(a).location.x == Catch::Approx(40.f / 3));
    CHECK(tree.unrounded_layout(b).location.x == Catch::Approx(200.f / 3));
    const auto& info = fixture_info(tree, root);
    CHECK(info.grid_template_columns() == "40px 40px 0px");
    CHECK(info.columns.explicit_tracks == 3);
    CHECK(info.columns.collapsed_tracks == std::vector<GridTrackRange>{{2, 3}});
}

TEST_CASE("native grid whole container percentage padding uses area width",
          "[grid][container][fixture]") {
    // grid_percent_items_width_and_padding__border_box_ltr.xml
    Tree tree;
    auto style = fixture_grid();
    style.size.width = Dimension::length(200);
    auto child = fixture_item();
    child.size.width = Dimension::percent(.5f);
    child.padding = Rect<LengthPercentage>::from_percent(.03f, .03f, .03f, .03f);
    const auto node = tree.new_leaf(child).value();
    const auto root = tree.new_with_children(style, std::array{node}).value();
    fixture_layout(tree, root, {200, 12});
    fixture_box(tree, node, 0, 0, 100, 12);
    CHECK(tree.layout(node).padding == Rect<float>{6, 6, 6, 6});
    CHECK(fixture_info(tree, root).grid_template_rows() == "12px");
}

TEST_CASE("native grid whole container relative offsets respect RTL",
          "[grid][container][fixture]") {
    // grid_relative_all_sides__border_box_{ltr,rtl}.xml
    for (const auto direction : {Direction::Ltr, Direction::Rtl}) {
        CAPTURE(direction);
        Tree tree;
        auto style = fixture_grid();
        style.direction = direction;
        style.size = Size<Dimension>::from_lengths(100, 100);
        auto child = fixture_item();
        child.direction = direction;
        child.size.width = Dimension::length(40);
        child.inset = Rect<LengthPercentageAuto>::from_length(10, 10, 10, 10);
        const auto node = tree.new_leaf(child).value();
        const auto root = tree.new_with_children(style, std::array{node}).value();
        fixture_layout(tree, root, {100, 100});
        fixture_box(tree, node, direction == Direction::Ltr ? 10 : 50, 10, 40, 100);
        CHECK(fixture_info(tree, root).item_grid_area(0) ==
              std::pair{Point<float>{0, 0}, Size<float>{100, 100}});
    }
}

TEST_CASE("native grid direct container baseline ignores nested relative offset",
          "[grid][container][baseline][fixture]") {
    // Grid subtree of grid_container_baseline_ignores_relative_offset__border_box_ltr.xml.
    Tree tree;
    auto leaf = fixture_item();
    leaf.size = Size<Dimension>::from_lengths(30, 20);
    const auto child = tree.new_leaf(leaf).value();
    auto inner = fixture_grid();
    inner.position = Position::Relative;
    inner.size = Size<Dimension>::from_lengths(30, 30);
    inner.inset.top = LengthPercentageAuto::length(7);
    const auto nested = tree.new_with_children(inner, std::array{child}).value();
    auto outer = fixture_grid();
    outer.size = Size<Dimension>::from_lengths(100, 50);
    outer.align_items = AlignItems::start();
    outer.justify_items = JustifyItems::start();
    const auto root = tree.new_with_children(outer, std::array{nested}).value();
    const auto output = compute_grid(tree, root, {});
    CHECK(output.size == Size<float>{100, 50});
    CHECK(output.baselines.first == 20);
    CHECK(tree.unrounded_layout(nested).location == Point<float>{0, 7});
    CHECK(tree.unrounded_layout(nested).size == Size<float>{30, 30});
    CHECK(tree.unrounded_layout(child).location == Point<float>{0, 0});
    CHECK(tree.unrounded_layout(child).size == Size<float>{30, 20});
    CHECK(output.oof_candidates.empty());
}

TEST_CASE("native grid direct OOF candidates retain named grid areas",
          "[grid][container][oof][fixture]") {
    // grid_absolute_known_named_lines__border_box_ltr.xml: inspect before OOF pass.
    Tree tree;
    auto style = fixture_grid();
    style.position = Position::Relative;
    style.size = Size<Dimension>::from_lengths(200, 150);
    style.padding = Rect<LengthPercentage>::from_length(10, 10, 10, 10);
    style.grid_template_rows = {GT::length(20), GT::length(50)};
    style.grid_template_columns = {GT::length(40), GT::length(60)};
    style.grid_template_row_names = style.grid_template_column_names = {{"a"}, {"b"}, {"c"}};
    std::array<NodeId, 3> children;
    for (int i = 0; i < 3; ++i) {
        auto child = fixture_item();
        child.position = Position::Absolute;
        child.inset = Rect<LengthPercentageAuto>::from_length(0, 0, 0, 0);
        child.grid_row = child.grid_column = {GP::named_line(i == 2 ? "c" : "a"),
                                              GP::named_line(i == 2 ? "a" : "c")};
        if (i == 1) {
            child.grid_row = {{}, GP::named_line("b")};
            child.grid_column = {GP::named_line("b"), {}};
        }
        children[i] = tree.new_leaf(child).value();
    }
    const auto root = tree.new_with_children(style, children).value();
    const auto output = compute_grid(tree, root, {});
    CHECK(output.size == Size<float>{200, 150});
    REQUIRE(output.oof_positioning_area);
    CHECK(output.oof_positioning_area->size == Size<float>{200, 150});
    REQUIRE(output.oof_candidates.size() == 3);
    for (std::size_t i = 0; i < children.size(); ++i) {
        const auto& candidate = output.oof_candidates.inner[i];
        CHECK(candidate.node == children[i]);
        CHECK(candidate.order == i);
        CHECK(candidate.position == Position::Absolute);
        CHECK(candidate.static_position.x.area ==
              (i == 1 ? Line<float>{50, 200} : Line<float>{10, 110}));
        CHECK(candidate.static_position.y.area ==
              (i == 1 ? Line<float>{0, 30} : Line<float>{10, 80}));
        CHECK(tree.unrounded_layout(children[i]).size == Size<float>{0, 0});
    }
    CHECK(fixture_info(tree, root).items.empty()); // OOF must not size/occupy tracks.
    fixture_layout(tree, root, {200, 150});
    fixture_box(tree, children[0], 10, 10, 100, 70);
    fixture_box(tree, children[1], 50, 0, 150, 30);
    fixture_box(tree, children[2], 10, 10, 100, 70);
}

TEST_CASE("native grid direct container bubbles descendant candidates in physical coordinates",
          "[grid][container][oof]") {
    // Constructed extension of grid_relative_all_sides: the leaf callback represents
    // a custom subtree with a fixed descendant not claimed by the relative item.
    Tree tree;
    auto descendant_style = fixture_item();
    descendant_style.position = Position::Fixed;
    const auto descendant = tree.new_leaf(descendant_style).value();
    tree.set_leaf_layout([&](LayoutTree&, NodeId, const LayoutInput& input) {
        auto output = LayoutOutput::from_outer_size({input.known_dimensions.width.value_or(40),
                                                     input.known_dimensions.height.value_or(100)});
        OofCandidate candidate;
        candidate.node = descendant;
        candidate.position = Position::Fixed;
        candidate.order = 9;
        candidate.static_position = {AxisStaticPosition::from_edge(2, AxisStaticEdge::Start),
                                     AxisStaticPosition::from_edge(4, AxisStaticEdge::End)};
        output.oof_candidates.push(candidate);
        return output;
    });
    auto style = fixture_grid();
    style.size = Size<Dimension>::from_lengths(100, 100);
    auto child = fixture_item();
    child.size.width = Dimension::length(40);
    child.inset = Rect<LengthPercentageAuto>::from_length(10, 10, 10, 10);
    const auto node = tree.new_leaf(child).value();
    const auto root = tree.new_with_children(style, std::array{node}).value();
    const auto output = compute_grid(tree, root, {});
    CHECK(output.size == Size<float>{100, 100});
    REQUIRE(output.oof_candidates.size() == 1);
    const auto& candidate = output.oof_candidates.inner[0];
    CHECK(candidate.node == descendant);
    CHECK(candidate.position == Position::Fixed);
    CHECK(candidate.order == 9);
    CHECK(candidate.static_position.x.area == Line<float>{12, 12});
    CHECK(candidate.static_position.y.area == Line<float>{14, 14});
    CHECK(candidate.static_position.y.align.keyword == AxisStaticEdge::End);
}

TEST_CASE("native grid occupancy expands both implicit sides and preserves holes",
          "[grid][occupancy]") {
    using namespace grid;
    CellOccupancy cells({0, 2, 0}, {0, 2, 0});
    cells.mark_area_as(AbsoluteAxis::Horizontal, {-2, 1}, {-1, 1},
                       CellOccupancyState::DefinitelyPlaced);
    cells.mark_area_as(AbsoluteAxis::Vertical, {2, 4}, {3, 5}, CellOccupancyState::AutoPlaced);
    CHECK(cells.track_counts(AbsoluteAxis::Horizontal) == TrackCounts{2, 2, 3});
    CHECK(cells.track_counts(AbsoluteAxis::Vertical) == TrackCounts{1, 2, 2});
    CHECK(cells.state_at(-1, -2) == CellOccupancyState::DefinitelyPlaced);
    CHECK(cells.state_at(3, 4) == CellOccupancyState::AutoPlaced);
    CHECK(cells.state_at(1, 1) == CellOccupancyState::Unoccupied);
    CHECK(cells.line_area_is_unoccupied(AbsoluteAxis::Horizontal, {1, 3}, {0, 2}));
    CHECK(cells.line_area_collision_jump(AbsoluteAxis::Horizontal, {-1, 2}, {-1, 0}) == 1);
    CHECK(cells.line_area_collision_jump(AbsoluteAxis::Vertical, {1, 3}, {3, 4}) == 4);
    CHECK(cells.line_area_is_unoccupied(AbsoluteAxis::Horizontal, {5, 6}, {2, 4}));
}

TEST_CASE("native grid fractional distribution freezes floors and respects subunit factors",
          "[grid][sizing]") {
    using namespace grid;
    std::array<GridTrack, 2> tracks{
        {GridTrack{MinTrackSizingFunction::zero(), MaxTrackSizingFunction::fr(1)},
         GridTrack{MinTrackSizingFunction::zero(), MaxTrackSizingFunction::fr(2)}}};
    tracks[0].base_size = 80;
    CHECK(find_size_of_fr(tracks, 100) == 10);
    CHECK(find_size_of_fr(tracks, 0) == 0);
    tracks[0].base_size = 0;
    tracks[0].max_track_sizing_function = MaxTrackSizingFunction::fr(.25f);
    tracks[1].max_track_sizing_function = MaxTrackSizingFunction::fr(.25f);
    CHECK(find_size_of_fr(tracks, 100) == 100); // factor sum floored to one
    tracks[0].max_track_sizing_function = tracks[1].max_track_sizing_function =
        MaxTrackSizingFunction::fr(0);
    const auto zero = find_size_of_fr(tracks, 100);
    CHECK(std::isfinite(zero));
    CHECK(zero == 100); // zero factors consume none of this hypothetical fraction
}

TEST_CASE("native grid whole container sparse auto placement respects locked numeric spans",
          "[grid][container][fixture]") {
    // grid_auto_placement_sparse_spanning_locked_items__border_box_ltr.xml
    Tree tree;
    auto style = fixture_grid();
    style.grid_auto_rows = {TS::length(20)};
    style.grid_auto_columns = {TS::length(50)};
    const std::array<int, 8> row_starts{2, 3, 2, 1, 1, 3, 2, 0};
    std::array<NodeId, 8> children;
    for (std::size_t i = 0; i < children.size(); ++i) {
        auto child = fixture_item();
        if (row_starts[i])
            child.grid_row.start = GP::line(row_starts[i]);
        if (i < 2)
            child.grid_column.start = GP::line(i + 1);
        if (i == 2 || i == 3)
            child.grid_row.end = GP::span(2);
        children[i] = tree.new_leaf(child).value();
    }
    const auto root = tree.new_with_children(style, children).value();
    fixture_layout(tree, root, {200, 60});
    const std::array<Point<float>, 8> positions{
        {{0, 20}, {50, 40}, {100, 20}, {50, 0}, {100, 0}, {0, 40}, {150, 20}, {0, 0}}};
    for (std::size_t i = 0; i < children.size(); ++i) {
        CAPTURE(i);
        fixture_box(tree, children[i], positions[i].x, positions[i].y, 50,
                    i == 2 || i == 3 ? 40 : 20);
        CHECK(tree.layout(children[i]).order == i);
    }
    const auto& info = fixture_info(tree, root);
    CHECK(info.columns.explicit_tracks == 0);
    CHECK(info.columns.positive_implicit_tracks == 4);
    CHECK(info.rows.explicit_tracks == 0);
    CHECK(info.rows.positive_implicit_tracks == 3);
    CHECK(info.grid_template_columns() == "50px 50px 50px 50px");
    CHECK(info.grid_template_rows() == "20px 20px 20px");
}

TEST_CASE("native grid whole container backwards numeric spans match named fixture geometry",
          "[grid][container][sizing]") {
    // Numeric equivalent of grid_placement_named_span_backwards__border_box_ltr.xml:
    // negative end -1 counts from the explicit end, not the implicit grid.
    Tree tree;
    std::array<NodeId, 4> children;
    for (std::uint16_t i = 0; i < 4; ++i) {
        auto child = fixture_item();
        child.grid_row = {GP::span(i == 3 ? 2 : i + 1), GP::line(i == 3 ? -1 : 4)};
        child.grid_column = {GP::span(i == 3 ? 2 : i + 1), GP::line(i == 3 ? -1 : i == 2 ? 4 : 3)};
        children[i] = tree.new_leaf(child).value();
    }
    const auto root = tree.new_with_children(repeated_names_grid(), children).value();
    fixture_layout(tree, root, {350, 140});
    fixture_box(tree, children[0], 50, 60, 100, 80);
    fixture_box(tree, children[1], 0, 20, 150, 120);
    fixture_box(tree, children[2], 0, 0, 350, 140);
    fixture_box(tree, children[3], 50, 20, 300, 120);
}

TEST_CASE("native grid whole container numeric span creates negative implicit row",
          "[grid][container][fixture]") {
    // grid_implicit_tracks_named_line_placement__border_box_ltr.xml
    Tree tree;
    auto style = fixture_grid();
    style.grid_template_rows = {GT::length(20), GT::length(40)};
    style.grid_template_columns = {GT::length(50), GT::length(100)};
    style.grid_template_row_names =
        style.grid_template_column_names = {{"first"}, {"mid"}, {"last"}};
    style.grid_auto_rows = {TS::length(10)};
    style.grid_auto_columns = {TS::length(5)};
    std::array<NodeId, 4> children;
    auto child = fixture_item();
    child.grid_row = {GP::line(1), GP::named_line("foo")};
    child.grid_column = {GP::named_line("foo"), GP::line(1)};
    children[0] = tree.new_leaf(child).value();
    child.grid_row = {GP::span(3), GP::named_line("last")};
    child.grid_column = {GP::span(2), GP::named_line("last")};
    children[1] = tree.new_leaf(child).value();
    child.grid_row = {GP::named_line("last"), GP::named_line("first")};
    child.grid_column = {GP::named_line("mid"), GP::line(1)};
    children[2] = tree.new_leaf(child).value();
    children[3] = tree.new_leaf(fixture_item()).value();
    const auto root = tree.new_with_children(style, children).value();
    fixture_layout(tree, root, {155, 80});
    fixture_box(tree, children[0], 0, 10, 155, 70);
    fixture_box(tree, children[1], 0, 0, 150, 70);
    fixture_box(tree, children[2], 0, 10, 50, 60);
    fixture_box(tree, children[3], 150, 0, 5, 10);
    const auto& info = fixture_info(tree, root);
    CHECK(info.rows.negative_implicit_tracks == 1);
    CHECK(info.rows.positive_implicit_tracks == 1);
    CHECK(info.columns.positive_implicit_tracks == 1);
    CHECK(info.grid_template_rows() == "10px [first] 20px [mid] 40px [last] 10px");
    CHECK(info.grid_template_columns() == "[first] 50px [mid] 100px [last] 5px");
}

TEST_CASE("native grid whole container baseline alignment preserves pinned leaf dispatch",
          "[grid][container][baseline][fixture]") {
    // grid_align_items_baseline_child{,_top}__border_box_ltr.xml.
    for (const float relative_top : {0.f, 10.f}) {
        CAPTURE(relative_top);
        Tree tree;
        auto child_style = fixture_grid();
        child_style.position = Position::Relative;
        child_style.size = Size<Dimension>::from_lengths(50, 50);
        child_style.inset.top = LengthPercentageAuto::length(relative_top);
        const auto a = tree.new_leaf(child_style).value();
        child_style.inset.top = LengthPercentageAuto::auto_();
        child_style.size.height = Dimension::length(10);
        const auto c = tree.new_leaf(child_style).value();
        child_style.size.height = Dimension::length(20);
        const auto b = tree.new_with_children(child_style, std::array{c}).value();
        auto style = fixture_grid();
        style.align_items = AlignItems::baseline();
        style.size = Size<Dimension>::from_lengths(100, 100);
        const auto root = tree.new_with_children(style, std::array{a, b}).value();
        fixture_layout(tree, root, {100, 100});
        fixture_box(tree, a, 0, relative_top, 50, 50);
        fixture_box(tree, b, 0, 65, 50, 20);
        fixture_box(tree, c, 0, 0, 50, 10);
        CHECK(fixture_info(tree, root).grid_template_rows() == "65px 35px");
        CHECK(fixture_info(tree, b).grid_template_rows() == "20px");
        // Pinned tree/taffy_tree.rs dispatches childless nodes to leaf layout.
        // These fixtures expect "none" track lists, a documented strict-XML
        // gap, but native parity must not fabricate absent Grid metadata.
        for (const auto empty : {a, c}) {
            CAPTURE(empty.value);
            CHECK(std::holds_alternative<std::monostate>(tree.detailed_layout_info(empty)));
        }
    }
}

TEST_CASE("native grid direct empty container emits explicit empty track metadata",
          "[grid][container][metadata]") {
    // Isolate direct compute_grid from the childless-node dispatch in
    // grid_align_items_baseline_child__border_box_ltr.xml.
    Tree tree;
    auto style = fixture_grid();
    style.position = Position::Relative;
    style.size = Size<Dimension>::from_lengths(50, 50);
    const auto root = tree.new_leaf(style).value();
    const auto output = compute_grid(tree, root, {});
    CHECK(output.size == Size<float>{50, 50});
    CHECK(output.baselines == Baselines::NONE);
    CHECK(output.oof_candidates.empty());
    const auto& info = fixture_info(tree, root);
    CHECK(info.grid_template_rows() == "none");
    CHECK(info.grid_template_columns() == "none");
    CHECK(info.rows.positions.empty());
    CHECK(info.columns.positions.empty());
    CHECK(info.rows.empty_axis_line == 0);
    CHECK(info.columns.empty_axis_line == 0);
}

TEST_CASE("native grid reusable distribution saturates limits without losing remainder",
          "[grid][sizing]") {
    using namespace grid;
    std::array<GridTrack, 3> tracks;
    tracks[0].growth_limit = 5;
    tracks[1].growth_limit = 30;
    tracks[2].growth_limit = 100;
    const auto affected = [](const GridTrack& t) { return t.growth_limit < 100; };
    const auto weight = [](const GridTrack& t) { return t.growth_limit == 5 ? 1.f : 3.f; };
    const auto property = [](const GridTrack& t) { return t.base_size; };
    const auto limit = [](const GridTrack& t) { return t.growth_limit; };
    CHECK(distribute_space_up_to_limits(50, tracks, affected, weight, property, limit) == 15);
    CHECK(tracks[0].item_incurred_increase == 5);
    CHECK(tracks[1].item_incurred_increase == 30);
    CHECK(tracks[2].item_incurred_increase == 0);
    CHECK(distribute_space_up_to_limits(7, tracks, affected, weight, property, limit) == 7);
    CHECK(distribute_space_up_to_limits(0, tracks, affected, weight, property, limit) == 0);
    CHECK(distribute_space_up_to_limits(.005f, tracks, affected, weight, property, limit) == .005f);
    CHECK(distribute_space_up_to_limits(7, {}, affected, weight, property, limit) == 7);
}

TEST_CASE("native grid detailed resolver preserves absent implicit area edges",
          "[grid][metadata][oof]") {
    // A lone foo-start creates only the start edge of implicit area foo.
    // Zero in GridAreaLines is absence, not CSS line zero or a negative line.
    for (const auto direction : {Direction::Ltr, Direction::Rtl}) {
        Tree tree;
        auto style = fixture_grid();
        style.position = Position::Relative;
        style.direction = direction;
        style.size = Size<Dimension>::from_lengths(100, 40);
        style.grid_template_columns = {TrackSizingFunction::length(20),
                                       TrackSizingFunction::length(30)};
        style.grid_template_column_names = {{"foo-start"}, {}, {}};
        const auto root = tree.new_leaf(style).value();
        compute_grid(tree, root, {});
        const auto& info = fixture_info(tree, root);
        CHECK(info.areas.at("foo").column_start == 1);
        CHECK(info.areas.at("foo").column_end == 0);
        const auto area = grid::resolve_absolute_grid_area(
            info, {}, {GridPlacement::named_line("foo"), GridPlacement::named_line("foo")},
            direction, {0, 100, 0, 40});
        CHECK(area == Rect<float>{0, 100, 0, 40});
        CHECK(info.grid_template_columns() == "[foo-start] 20px 30px");
    }
}

TEST_CASE("native grid calc tracks resolve against actual content box", "[grid][calc]") {
    Tree tree;
    tree.set_calc_resolver([](Calc value, float basis) {
        CHECK(value.handle == 17);
        return basis * .25f + 10;
    });
    auto style = fixture_grid();
    style.size = Size<Dimension>::from_lengths(200, 80);
    style.padding = Rect<LengthPercentage>::from_length(10, 10, 0, 0);
    style.grid_template_columns = {TrackSizingFunction{MinTrackSizingFunction::calc(Calc{17}),
                                                       MaxTrackSizingFunction::calc(Calc{17})},
                                   TrackSizingFunction::fr(1)};
    const auto a = tree.new_leaf(fixture_item()).value();
    const auto b = tree.new_leaf(fixture_item()).value();
    const auto root = tree.new_with_children(style, std::array{a, b}).value();
    fixture_layout(tree, root, {200, 80});
    fixture_box(tree, a, 10, 0, 55, 80);
    fixture_box(tree, b, 65, 0, 125, 80);
    CHECK(fixture_info(tree, root).grid_template_columns() == "55px 125px");
}

TEST_CASE("native grid known horizontal size avoids child measurement", "[grid][compute-size]") {
    Tree tree;
    auto style = fixture_grid();
    style.size.width = Dimension::length(80);
    const auto child = tree.new_leaf(fixture_item()).value();
    const auto root = tree.new_with_children(style, std::array{child}).value();
    unsigned measurements = 0;
    tree.set_intrinsic_measure([&](const LayoutInput&, NodeId, NodeContext*, const Style&) {
        ++measurements;
        return Size<float>{13, 29};
    });
    LayoutInput input;
    input.run_mode = RunMode::ComputeSize;
    input.axis = RequestedAxis::Horizontal;
    CHECK(compute_grid(tree, root, input).size == Size<float>{80, 0});
    CHECK(measurements == 0);
    CHECK(std::holds_alternative<std::monostate>(tree.detailed_layout_info(root)));
}

// Explicit strings confirmed against the pinned Rust public getter; see
// reference/cpp-integration/evidence/track-format (not derived from our formatter).
#include <bit>
#include <cstdint>

TEST_CASE("native computed track strings match Rust f32 Display extremes", "[grid][detailed][track-format]") {
    struct Case { std::uint32_t bits; const char* expected; };
    const Case cases[] = {
        {0x358637bd, "0.000001px"},
        {0x60ad78ec, "100000000000000000000px"},
        {0x4ceb79a3, "123456790px"},
        {0x00000001, "0.000000000000000000000000000000000000000000001px"},
        {0x80000001, "-0.000000000000000000000000000000000000000000001px"},
        {0x007fffff, "0.000000000000000000000000000000000000011754942px"},
        {0x00800000, "0.000000000000000000000000000000000000011754944px"},
        {0x7f7fffff, "340282350000000000000000000000000000000px"},
        {0xff7fffff, "-340282350000000000000000000000000000000px"},
        // Exact shortest-decimal halfway: Rust rounds magnitude up, C++ to even.
        {0xc86b6ec8, "-241083.13px"},
        {0x486b6ec8, "241083.13px"},
        {0x00000000, "0px"}, {0x80000000, "-0px"},
        {0x7fc00000, "NaNpx"}, {0xffc00000, "NaNpx"},
        {0x7f800001, "NaNpx"},
        {0x7f800000, "infpx"}, {0xff800000, "-infpx"},
    };
    for (const auto& c : cases) {
        CAPTURE(c.bits);
        DetailedGridTracksInfo tracks;
        tracks.explicit_tracks = 1;
        tracks.positions = {{0, std::bit_cast<float>(c.bits)}};
        CHECK(tracks.to_track_list_string() == c.expected);
    }
}

TEST_CASE("native track string zoom retains f32 arithmetic and IEEE special values", "[grid][detailed][track-format]") {
    struct Case { float start; float end; float zoom; const char* expected; };
    const float infinity = std::bit_cast<float>(std::uint32_t{0x7f800000});
    const float nan = std::bit_cast<float>(std::uint32_t{0x7fc00000});
    const Case cases[] = {
        {1, 1.000001f, 2, "0.00000047683716px"},
        {0, 1e-6f, 2, "0.0000005px"},
        {0, 1e20f, 2, "50000000000000000000px"},
        {0, 1, .1f, "10px"}, {0, 1, -2, "-0.5px"},
        {0, 1, 0, "infpx"}, {0, -1, 0, "-infpx"},
        {0, 0, 0, "NaNpx"}, {0, 1, -0.0f, "-infpx"},
        {0, 1, nan, "NaNpx"}, {0, 1, infinity, "0px"},
        {0, 1, -infinity, "-0px"}, {infinity, infinity, 1, "NaNpx"},
        {-infinity, infinity, 1, "infpx"}, {0, -0.0f, 2, "-0px"},
    };
    for (const auto& c : cases) {
        CAPTURE(c.start, c.end, c.zoom);
        DetailedGridTracksInfo tracks;
        tracks.positions = {{c.start, c.end}};
        CHECK(tracks.to_track_list_string(c.zoom) == c.expected);
    }
    DetailedGridTracksInfo empty;
    CHECK(empty.to_track_list_string(0) == "none");
    CHECK(empty.to_track_list_string(nan) == "none");
}

TEST_CASE("native track string full-grid counts and names survive decimal expansion", "[grid][detailed][track-format]") {
    DetailedGridTracksInfo tracks;
    tracks.negative_implicit_tracks = 1;
    tracks.explicit_tracks = 1;
    tracks.positive_implicit_tracks = 1;
    tracks.positions = {{0, 1e-6f}, {0, 1e20f}, {0, -0.0f}};
    tracks.line_names = {{"alpha", "beta"}, {"omega"}};
    const auto original_positions = tracks.positions;
    CHECK(tracks.to_track_list_string() == "0.000001px [alpha beta] 100000000000000000000px [omega] -0px");
    CHECK(tracks.to_track_list_string(2) == "0.0000005px [alpha beta] 50000000000000000000px [omega] -0px");
    CHECK(tracks.negative_implicit_tracks == 1);
    CHECK(tracks.explicit_tracks == 1);
    CHECK(tracks.positive_implicit_tracks == 1);
    CHECK(tracks.positions == original_positions);
    CHECK(tracks.names_for_line(0).empty());
    CHECK(tracks.names_for_line(1).size() == 2);
    CHECK(tracks.names_for_line(2).front() == "omega");
    CHECK(tracks.names_for_line(3).empty());
    CHECK(tracks.iter_line_names() == std::vector<std::vector<std::string>>{{}, {"alpha", "beta"}, {"omega"}, {}});
}
