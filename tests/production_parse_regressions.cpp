#include <catch2/catch_test_macros.hpp>
#include <taffy/context.hpp>
#include <taffy/parse.hpp>

using namespace taffy;

TEST_CASE("scalar property updates retain unrelated owned style storage", "[production][parse]") {
    Style style;
    style.grid_template_columns.assign(2000, GridTemplateComponent::length(1));
    style.grid_template_column_names.resize(2001);
    for (auto& boundary : style.grid_template_column_names)
        boundary.emplace_back("a-line-name-long-enough-to-allocate-string-storage");
    style.grid_template_areas = GridTemplateAreas{{{"area", 1, 2, 1, 2}}, 1, 1};
    const auto* tracks = style.grid_template_columns.data();
    const auto* boundaries = style.grid_template_column_names.data();
    const auto* names = style.grid_template_column_names.front().data();
    const auto* text = style.grid_template_column_names.front().front().data();
    const auto* areas = style.grid_template_areas->areas.data();
    for (int i = 0; i < 100; ++i)
        REQUIRE(parse_style_property(style, "width", "20px").has_value());
    CHECK(style.size.width == Dimension::length(20));
    CHECK(style.grid_template_columns.data() == tracks);
    CHECK(style.grid_template_column_names.data() == boundaries);
    CHECK(style.grid_template_column_names.front().data() == names);
    CHECK(style.grid_template_column_names.front().front().data() == text);
    CHECK(style.grid_template_areas->areas.data() == areas);
}

TEST_CASE("property and shorthand failures leave the complete style unchanged",
          "[production][parse]") {
    Style style;
    REQUIRE(parse_style_property(style, "flex-flow", "column wrap-reverse").has_value());
    REQUIRE(parse_style_property(style, "grid-area", "old-row / old-column").has_value());
    REQUIRE(
        parse_style_property(style, "grid-template-columns", "[before] 20px [after]").has_value());
    const auto original = style;
    for (const auto& [name, value] :
         {std::pair{"width", "10px garbage"}, std::pair{"flex-flow", "row wrap invalid"},
          std::pair{"flex-flow", "row column"}, std::pair{"flex-flow", "wrap nowrap"},
          std::pair{"flex", "1 2 3px garbage"}, std::pair{"place-items", "start invalid"},
          std::pair{"grid-area", "new / names / here / there / excess"},
          std::pair{"grid-template-columns", "[new] 30px garbage"},
          std::pair{"aspect-ratio", "2 / 0"}}) {
        INFO(name << ": " << value);
        CHECK_FALSE(parse_style_property(style, name, value).has_value());
        CHECK(style == original);
    }
    REQUIRE(parse_style_property(style, "flex-flow", "wrap").has_value());
    CHECK(style.flex_direction == FlexDirection::Row);
    CHECK(style.flex_wrap == FlexWrap::Wrap);
    REQUIRE(parse_style_property(style, "grid-area", "new-row / new-column").has_value());
    CHECK(style.grid_row.start == GridPlacement::named_line("new-row", 0));
    CHECK(style.grid_row.end == style.grid_row.start);
    CHECK(style.grid_column.start == GridPlacement::named_line("new-column", 0));
    CHECK(style.grid_column.end == style.grid_column.start);
}

TEST_CASE("context_cast ignores overloaded address-of operators", "[production][context]") {
    struct Payload {
        int value = 17;

        Payload* operator&() {
            return nullptr;
        }

        const Payload* operator&() const {
            return nullptr;
        }
    };

    auto context = make_context<Payload>();
    auto* typed = context_cast<Payload>(context.get());
    REQUIRE(typed != nullptr);
    CHECK(typed->value == 17);
    const NodeContext* borrowed = context.get();
    CHECK(context_cast<Payload>(borrowed) == typed);
    CHECK(context_cast<int>(context.get()) == nullptr);
    CHECK(context_cast<Payload>(static_cast<NodeContext*>(nullptr)) == nullptr);
}
