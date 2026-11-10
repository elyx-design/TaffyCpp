#include "../src/grid.hpp"
#include "../src/grid/names.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace taffy;

TEST_CASE("repeat local names do not require outer name groups", "[production][grid][names]") {
    Style style;
    style.grid_template_columns = {
        repeat(RepetitionCount::count(2), {TrackSizingFunction::flex(1)}, {{"cell"}, {"edge"}})};
    grid::NamedLineResolver absent(style);
    absent.set_explicit_column_count(2);
    style.grid_template_column_names.resize(2);
    grid::NamedLineResolver empty(style);
    empty.set_explicit_column_count(2);
    for (const auto index : {1, 2, -1, -2}) {
        const Line<GridPlacement> line{GridPlacement::named_line("cell", index), {}};
        CHECK(absent.resolve_column_names(line) == empty.resolve_column_names(line));
    }
    CHECK(absent.resolve_column_names({GridPlacement::named_line("cell", 2), {}})
              .start.line_index() == 2);
    CHECK(absent.detailed_line_names(AbsoluteAxis::Horizontal) ==
          TemplateLineNames{{"cell"}, {"edge", "cell"}, {"edge"}});
}

TEST_CASE("indexed occurrences deduplicate a boundary and merge area aliases",
          "[production][grid][names]") {
    Style style;
    style.grid_template_columns.resize(4, TrackSizingFunction::flex(1));
    style.grid_template_column_names = {
        {"area-start", "area-start"}, {}, {"area-start"}, {"area-end"}, {"area-end"}};
    style.grid_template_areas = GridTemplateAreas{{{"area", 1, 2, 2, 4}}, 1, 4};
    grid::NamedLineResolver resolver(style);
    resolver.set_explicit_column_count(4);
    const auto start = [&](const char* name, std::int16_t n) {
        return resolver.resolve_column_names({GridPlacement::named_line(name, n), {}})
            .start.line_index();
    };
    CHECK(start("area-start", 1) == 1);
    CHECK(start("area-start", 2) == 2);
    CHECK(start("area-start", 3) == 3);
    CHECK(start("area-start", -1) == 3);
    CHECK(start("area-start", 4) == 6);
    CHECK(start("area", 0) == 1);
    CHECK(start("area", 1) == 6);
    const auto span = resolver.resolve_column_names(
        {GridPlacement::line(1), GridPlacement::named_span("area-start", 2)});
    CHECK(span.end.line_index() == 3);
    const auto backwards = resolver.resolve_column_names(
        {GridPlacement::named_span("area-start", 2), GridPlacement::line(4)});
    CHECK(backwards.start.line_index() == 2);
    const auto indefinite = resolver.resolve_column_names(
        {GridPlacement::named_span("area", 2), GridPlacement::named_span("area", 2)});
    CHECK(indefinite.start == GridPlacement::span(1));
    CHECK(indefinite.end == GridPlacement::span(1));
}

TEST_CASE("indexed implicit fallback saturates extreme occurrences and spans",
          "[production][grid][names]") {
    grid::NamedLineResolver resolver(Style{});
    resolver.set_explicit_column_count(max_grid_tracks);
    CHECK(resolver
              .resolve_column_names(
                  {GridPlacement::named_line("missing", std::numeric_limits<std::int16_t>::max()),
                   {}})
              .start.line_index() == 10001);
    CHECK(resolver
              .resolve_column_names(
                  {GridPlacement::named_line("missing", std::numeric_limits<std::int16_t>::min()),
                   {}})
              .start.line_index() == -20001);
    CHECK(resolver
              .resolve_column_names(
                  {GridPlacement::line(1),
                   GridPlacement::named_span("missing", std::numeric_limits<std::uint16_t>::max())})
              .end.line_index() == 10001);
}

TEST_CASE("metadata retains area generated occurrences behind an earlier explicit alias",
          "[production][grid][names]") {
    Style style;
    style.grid_template_columns.resize(4, TrackSizingFunction::flex(1));
    style.grid_template_column_names = {{"area-start"}, {}, {"area-start"}, {}, {}};
    style.grid_template_areas = GridTemplateAreas{{{"area", 1, 2, 2, 4}}, 1, 4};
    grid::NamedLineResolver resolver(style);
    resolver.set_explicit_column_count(4);
    resolver.set_explicit_row_count(1);
    auto info = grid::make_detailed_grid_info({0, 1, 0}, {0, 4, 0}, {}, {}, {}, resolver, style);
    info.columns.positions = {{0, 10}, {10, 20}, {20, 30}, {30, 40}};
    REQUIRE(info.areas.at("area").column_start == 1);
    const auto rect =
        grid::resolve_absolute_grid_area(info, {}, {GridPlacement::named_line("area-start", 2), {}},
                                         Direction::Ltr, {0, 100, 0, 100});
    CHECK(rect.left == 10);
    CHECK(info.name_indexes->columns == resolver.name_index(AbsoluteAxis::Horizontal));
}

TEST_CASE("large boundary aliases retain first source order", "[production][grid][names]") {
    std::vector<std::string> aliases;
    for (int i = 0; i < 4000; ++i)
        aliases.push_back("alias" + std::to_string(i));
    auto repeated = aliases;
    repeated.insert(repeated.end(), aliases.begin(), aliases.end());
    const std::vector<GridTemplateComponent> components{TrackSizingFunction::flex(1)};
    const auto expanded = expand_grid_template(components, {repeated, repeated});
    CHECK(expanded.line_names.front() == aliases);
    CHECK(expanded.line_names.back() == aliases);
    Style style;
    style.grid_template_columns = components;
    style.grid_template_column_names = {repeated, repeated};
    grid::NamedLineResolver resolver(style);
    resolver.set_explicit_column_count(1);
    CHECK(resolver.detailed_line_names(AbsoluteAxis::Horizontal) == expanded.line_names);
}

TEST_CASE("empty repeats retain names without iterating the repetition count",
          "[production][grid][names]") {
    std::vector<GridTemplateComponent> components;
    for (int i = 0; i < 1000; ++i)
        components.push_back(repeat(
            RepetitionCount::count(std::numeric_limits<std::uint16_t>::max()), {}, {{"empty"}}));
    components.emplace_back(TrackSizingFunction::flex(1));
    const auto expanded = expand_grid_template(components, {});
    REQUIRE(expanded.tracks.size() == 1);
    CHECK(expanded.line_names.front() == std::vector<std::string>{"empty"});
    Style style;
    style.grid_template_columns = components;
    grid::NamedLineResolver resolver(style);
    resolver.set_explicit_column_count(1);
    CHECK(resolver.resolve_column_names({GridPlacement::named_line("empty", 1), {}})
              .start.line_index() == 1);
    CHECK(resolver.resolve_column_names({GridPlacement::named_line("empty", 2), {}})
              .start.line_index() == 3);
}

TEST_CASE("zero count repeats do not advance named line coordinates", "[production][grid][names]") {
    Style style;
    style.grid_template_columns = {
        repeat(RepetitionCount::count(0), {TrackSizingFunction::flex(1)}, {{"absent"}, {"absent"}}),
        TrackSizingFunction::flex(1)};
    style.grid_template_column_names = {{"before"}, {"after"}, {"last"}};
    grid::NamedLineResolver resolver(style);
    resolver.set_explicit_column_count(1);
    CHECK(resolver.resolve_column_names({GridPlacement::named_line("after", 1), {}})
              .start.line_index() == 1);
    CHECK(resolver.resolve_column_names({GridPlacement::named_line("last", 1), {}})
              .start.line_index() == 2);
    CHECK(resolver.resolve_column_names({GridPlacement::named_line("absent", 1), {}})
              .start.line_index() == 3);
}

TEST_CASE("area sweep accepts adjacent rectangles and rejects nested overlaps",
          "[production][grid][names]") {
    GridTemplateAreas areas;
    areas.row_count = 2;
    areas.column_count = 5000;
    for (std::uint16_t i = 1; i <= 5000; ++i) {
        areas.areas.push_back(
            {"top" + std::to_string(i), 1, 2, i, static_cast<std::uint16_t>(i + 1)});
        areas.areas.push_back(
            {"bottom" + std::to_string(i), 2, 3, i, static_cast<std::uint16_t>(i + 1)});
    }
    std::reverse(areas.areas.begin(), areas.areas.end());
    CHECK_NOTHROW(areas.validate());
    areas.areas.push_back({"nested", 1, 3, 2500, 2501});
    CHECK_THROWS_AS(areas.validate(), std::invalid_argument);
    areas.areas.pop_back();
    areas.areas.push_back({"wide", 1, 2, 1, 5001});
    CHECK_THROWS_AS(areas.validate(), std::invalid_argument);
}

TEST_CASE("absolute metadata uses shared indexes and partial area aliases",
          "[production][grid][names]") {
    DetailedGridInfo info;
    info.columns.explicit_tracks = 2;
    info.rows.explicit_tracks = 1;
    info.columns.positions = {{10, 30}, {30, 50}};
    info.rows.positions = {{5, 25}};
    info.columns.line_names = {{"cell-start", "cell-start"}, {}, {"cell-end"}};
    info.areas.emplace("cell", GridAreaLines{0, 0, 1, 3});
    info.name_indexes = std::make_shared<grid::DetailedNameIndexes>(info);
    const auto shared = info.name_indexes;
    for (int i = 0; i < 100; ++i) {
        const auto rect = grid::resolve_absolute_grid_area(
            info, {GridPlacement::named_line("cell"), GridPlacement::named_line("cell")},
            {GridPlacement::named_line("cell"), GridPlacement::named_line("cell")}, Direction::Ltr,
            {0, 100, 0, 100});
        CHECK(rect == Rect<float>{10, 50, 0, 100});
        CHECK(info.name_indexes == shared);
    }
    const auto copy = info;
    CHECK(copy.name_indexes == shared);
    CHECK(copy == info);
    info.name_indexes.reset();
    CHECK(copy == info); // Cache ownership is not observable metadata equality.
    CHECK(grid::resolve_absolute_grid_area(
              info, {}, {GridPlacement::named_line("cell"), GridPlacement::named_line("cell")},
              Direction::Ltr, {0, 100, 0, 100}) == Rect<float>{10, 50, 0, 100});
    // The numeric fast path does not inspect or narrow unrelated area data.
    info.areas.emplace("unrelated", GridAreaLines{100000, 100001, 100000, 100001});
    CHECK_NOTHROW(grid::resolve_absolute_grid_area(info, {},
                                                   {GridPlacement::line(1), GridPlacement::line(3)},
                                                   Direction::Ltr, {0, 100, 0, 100}));
}
