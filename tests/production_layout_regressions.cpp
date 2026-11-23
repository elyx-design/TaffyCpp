#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <taffy/taffy.hpp>

#include "../src/block.hpp"
#include "../src/flex/internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

using namespace taffy;

namespace {
void near(float actual, float expected) {
    CHECK(actual == Catch::Approx(expected).margin(0.0001f));
}

Style box(float width, float height) {
    Style s;
    s.size = Size<Dimension>::from_lengths(width, height);
    return s;
}

void layout(Tree& tree, NodeId root) {
    REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::from_lengths(200, 200)).has_value());
}

// Deliberately uses LayoutTree's default indexed access. A single snapshot per
// pass must suffice even for adapters that cannot provide native O(1) indexing.
struct SnapshotAdapter final : LayoutTree {
    mutable std::unique_ptr<Style> container = std::make_unique<Style>();
    std::vector<Style> children;
    std::vector<Layout> layouts;
    mutable std::size_t snapshots = 0;
    std::size_t presence_queries = 0;
    std::size_t owning_cache_queries = 0;

    bool cache_contains(NodeId, const LayoutInput&) override {
        ++presence_queries;
        return true;
    }

    std::optional<LayoutOutput> cache_get(NodeId, const LayoutInput&) override {
        ++owning_cache_queries;
        return std::nullopt;
    }

    mutable bool replace_on_calc = false;
    mutable bool replaced = false;
    bool replace_on_measure = false;

    std::vector<NodeId> child_ids(NodeId node) const override {
        ++snapshots;
        std::vector<NodeId> result;
        if (node.value == 0)
            for (std::size_t i = 0; i < children.size(); ++i)
                result.push_back(NodeId{static_cast<std::uint64_t>(i + 1)});
        return result;
    }

    const Style& style(NodeId node) const override {
        return node.value == 0 ? *container : children.at(node.value - 1);
    }

    float resolve_calc(Calc, float) const override {
        if (replace_on_calc && !replaced) {
            // Destroy the borrowed Style while the constants computation runs.
            container = std::make_unique<Style>();
            replaced = true;
        }
        return 3;
    }

    LayoutOutput compute_child_layout(NodeId, const LayoutInput& input) override {
        if (replace_on_measure && !replaced) {
            container = std::make_unique<Style>();
            replaced = true;
        }
        if (input.run_mode == RunMode::PerformHiddenLayout)
            return LayoutOutput::HIDDEN;
        return LayoutOutput::from_outer_size(input.known_dimensions.unwrap_or(Size<float>{10, 10}));
    }

    LayoutOutput measure(NodeId node, const LayoutInput& input) override {
        return compute_child_layout(node, input);
    }

    const Layout& unrounded_layout(NodeId n) const override {
        return layouts.at(n.value);
    }

    void set_unrounded_layout(NodeId n, const Layout& l) override {
        layouts.at(n.value) = l;
    }

    void set_final_layout(NodeId n, const Layout& l) override {
        layouts.at(n.value) = l;
    }

    void clear_hoisted_children(NodeId) override {}

    void add_hoisted_children(NodeId, std::span<const NodeId>) override {}

    std::vector<NodeId> hoisted_child_ids(NodeId) const override {
        return {};
    }
};
} // namespace

TEST_CASE("Production hidden roots never generate positioned geometry", "[production][root]") {
    for (auto position : {Position::Relative, Position::Absolute, Position::Fixed}) {
        CAPTURE(position);
        Tree tree;
        auto s = box(80, 40);
        s.position = position;
        s.padding = Rect<LengthPercentage>::from_length(5, 5, 5, 5);
        s.margin = Rect<LengthPercentageAuto>::from_length(7, 7, 7, 7);
        const auto child = tree.new_leaf(box(20, 10)).value();
        const auto root = tree.new_with_children(s, std::vector{child}).value();
        layout(tree, root);
        s.display = Display::None;
        REQUIRE(tree.set_style(root, s).has_value());
        layout(tree, root);
        layout(tree, root);
        for (auto node : {root, child}) {
            const auto& l = tree.unrounded_layout(node);
            CHECK(l.size == Size<float>{});
            CHECK(l.location == Point<float>{});
            CHECK(l.padding == Rect<float>{});
            CHECK(l.margin == Rect<float>{});
            CHECK(tree.layout(node).size == Size<float>{});
        }
    }
}

TEST_CASE("Production FlowRoot roots fill available block width", "[production][root]") {
    for (auto direction : {Direction::Ltr, Direction::Rtl}) {
        Tree tree;
        auto s = Style{};
        s.display = Display::FlowRoot;
        s.direction = direction;
        s.margin.left = LengthPercentageAuto::length(10);
        s.margin.right = LengthPercentageAuto::length(20);
        const auto child = tree.new_leaf(box(20, 10)).value();
        const auto root = tree.new_with_children(s, std::vector{child}).value();
        layout(tree, root);
        near(tree.unrounded_layout(root).size.width, 170);
        near(tree.unrounded_layout(root).location.x, 10);
    }
}

TEST_CASE("Production root cache presence checks do not request owning output",
          "[production][root][cache]") {
    for (auto position : {Position::Relative, Position::Absolute}) {
        SnapshotAdapter tree;
        *tree.container = box(80, 40);
        tree.container->position = position;
        tree.layouts.resize(1);
        compute_root_layout(tree, NodeId{0}, Size<AvailableSpace>::from_lengths(200, 200));
        CHECK(tree.presence_queries == 1);
        CHECK(tree.owning_cache_queries == 0);
    }
}

TEST_CASE("Production fractional flex factors count each gap once", "[production][flex]") {
    for (bool grow : {true, false}) {
        Tree tree;
        auto s = box(grow ? 100 : 80, 20);
        s.gap.width = LengthPercentage::length(10);
        auto child_style = box(grow ? 10 : 50, 10);
        child_style.flex_basis = Dimension::length(grow ? 10 : 50);
        child_style.flex_grow = grow ? 0.1f : 0;
        child_style.flex_shrink = grow ? 0 : 0.1f;
        child_style.min_size.width = LengthPercentageAuto::length(0);
        const auto a = tree.new_leaf(child_style).value();
        const auto b = tree.new_leaf(child_style).value();
        const auto root = tree.new_with_children(s, std::vector{a, b}).value();
        layout(tree, root);
        for (auto child : {a, b})
            near(tree.unrounded_layout(child).size.width, grow ? 17 : 47);
        near(tree.unrounded_layout(b).location.x, grow ? 27 : 57);
    }
}

TEST_CASE("Production cross auto margins overflow toward logical end", "[production][flex]") {
    for (auto dir : {FlexDirection::Row, FlexDirection::RowReverse, FlexDirection::Column,
                     FlexDirection::ColumnReverse})
        for (auto direction : {Direction::Ltr, Direction::Rtl})
            for (auto wrap : {FlexWrap::NoWrap, FlexWrap::WrapReverse})
                for (int margins : {1, 2, 3}) {
                    CAPTURE(dir, direction, wrap, margins);
                    const bool row = is_row(dir);
                    Tree tree;
                    auto s = box(row ? 100 : 20, row ? 20 : 100);
                    s.flex_direction = dir;
                    s.direction = direction;
                    s.flex_wrap = wrap;
                    auto child_style = box(row ? 10 : 40, row ? 40 : 10);
                    if (margins & 1) {
                        if (row)
                            child_style.margin.top = LengthPercentageAuto::auto_();
                        else
                            child_style.margin.left = LengthPercentageAuto::auto_();
                    }
                    if (margins & 2) {
                        if (row)
                            child_style.margin.bottom = LengthPercentageAuto::auto_();
                        else
                            child_style.margin.right = LengthPercentageAuto::auto_();
                    }
                    const auto child = tree.new_leaf(child_style).value();
                    const auto root = tree.new_with_children(s, std::vector{child}).value();
                    layout(tree, root);
                    const auto& l = tree.unrounded_layout(child);
                    const bool rtl_cross = !row && direction == Direction::Rtl;
                    near(row ? l.location.y : l.location.x, rtl_cross ? -20 : 0);
                    near(row ? l.margin.top : (rtl_cross ? l.margin.right : l.margin.left), 0);
                }
}

TEST_CASE("Production float clearance endpoints preserve the next occupied band",
          "[production][float]") {
    for (bool mirror : {false, true}) {
        FloatContext floats;
        floats.set_width(100);
        const auto first = mirror ? FloatDirection::Right : FloatDirection::Left;
        const auto other = mirror ? FloatDirection::Left : FloatDirection::Right;
        const auto clear = mirror ? Clear::Right : Clear::Left;
        floats.place_floated_box({20, 10}, 0, {}, first, Clear::None);
        floats.place_floated_box({20, 10}, 10, {}, other, Clear::None);
        const auto slot = floats.find_content_slot(0, {}, clear);
        near(slot.y, 10);
        near(slot.x, mirror ? 20 : 0);
        near(slot.width, 80);
        for (auto direction : {Direction::Ltr, Direction::Rtl}) {
            const auto bfc = floats.find_bfc_slot(0, {}, {}, direction, clear);
            near(bfc.y, 10);
            near(bfc.border_width, 80);
        }
        REQUIRE(slot.segment_id.has_value());
        const auto after = floats.find_content_slot(0, {}, Clear::None, slot.segment_id);
        near(after.y, 20);
        near(after.width, 100);
        const auto placed = floats.place_floated_box({20, 10}, 0, {}, first, clear);
        near(placed.y, 10);
        near(placed.x, mirror ? 80 : 0);
    }
}

TEST_CASE("Production float subdivision keeps clearance indices synchronized",
          "[production][float]") {
    FloatContext floats;
    floats.set_width(100);
    floats.place_floated_box({20, 20}, 0, {}, FloatDirection::Left, Clear::None);
    floats.place_floated_box({20, 5}, 5, {}, FloatDirection::Right, Clear::None);
    const auto slot = floats.find_content_slot(0, {}, Clear::Right);
    near(slot.y, 10);
    near(slot.width, 80);
    near(floats.find_content_slot(0, {}, Clear::Left).y, 20);
    near(floats.find_content_slot(0, {}, Clear::Both).y, 20);
    near(floats.place_floated_box({20, 5}, 0, {}, FloatDirection::Right, Clear::Right).y, 10);
}

TEST_CASE("Production zero-height floats still impose clearance", "[production][float]") {
    FloatContext floats;
    floats.set_width(100);
    floats.place_floated_box({20, 0}, 15, {}, FloatDirection::Left, Clear::None);
    near(floats.find_content_slot(0, {}, Clear::Left).y, 15);
    near(floats.find_bfc_slot(0, {}, {}, Direction::Ltr, Clear::Left).y, 15);
}

TEST_CASE("Production flex constants snapshot borrowed styles before calc callbacks",
          "[production][flex][adapter]") {
    SnapshotAdapter tree;
    tree.container->flex_direction = FlexDirection::ColumnReverse;
    tree.container->direction = Direction::Rtl;
    tree.container->padding.left = LengthPercentage::calc(Calc{1});
    tree.container->border.right = LengthPercentage::length(2);
    tree.container->align_items = AlignItems::end();
    tree.container->gap.height = LengthPercentage::length(7);
    tree.container->min_size.height = LengthPercentageAuto::length(30);
    tree.container->max_size.height = LengthPercentageAuto::length(90);
    tree.replace_on_calc = true;
    const auto c = flex_detail::compute_constants(tree, *tree.container, {100, 80}, {true, true},
                                                  {100, 80}, Size<AvailableSpace>::max_content());
    REQUIRE(tree.replaced);
    CHECK(c.dir == FlexDirection::ColumnReverse);
    CHECK(c.layout_direction == Direction::Rtl);
    CHECK(c.align_items == AlignItems::end());
    near(c.border.right, 2);
    near(c.gap.height, 7);
    CHECK(c.min_size.height == 30);
    CHECK(c.max_size.height == 90);
}

TEST_CASE("Production flex snapshots child ids once per pass for default adapters",
          "[production][flex][adapter]") {
    for (std::size_t count : {32, 256}) {
        SnapshotAdapter tree;
        *tree.container = box(10000, 20);
        tree.children.assign(count, box(10, 10));
        tree.layouts.resize(count + 1);
        tree.children.back().display = Display::None;
        tree.children[count - 2].position = Position::Absolute;
        tree.replace_on_measure = true;
        const auto output = compute_flex(tree, NodeId{0}, {});
        REQUIRE(tree.replaced);
        CHECK(output.oof_candidates.size() == 1);
        // Includes default child_count()'s compatibility snapshot. This bound is
        // independent of N; the old two indexed scans required 2*N snapshots.
        CHECK(tree.snapshots <= 6);
        near(output.size.width, 10000);
    }
}

TEST_CASE("Production flex wrapping resolves outer limits in content space",
          "[production][flex][wrap-insets]") {
    for (auto dir : {FlexDirection::Row, FlexDirection::Column}) {
        for (float border : {0.0f, 3.0f}) {
            for (float scrollbar : {0.0f, 10.0f}) {
                for (int sizing : {0, 1, 2}) {
                    CAPTURE(dir, border, scrollbar, sizing);
                    Tree tree;
                    auto child_style = box(is_row(dir) ? 40 : 20, is_row(dir) ? 20 : 40);
                    child_style.flex_shrink = 0;
                    const auto first = tree.new_leaf(child_style).value();
                    const auto second = tree.new_leaf(child_style).value();
                    Style style;
                    style.display = Display::Flex;
                    style.box_sizing = BoxSizing::BorderBox;
                    style.flex_direction = dir;
                    style.flex_wrap = FlexWrap::Wrap;
                    style.align_items = AlignItems::start();
                    style.align_content = AlignContent::start();
                    style.padding = Rect<LengthPercentage>::from_length(
                        10 - border, 10 - border, 10 - border, 10 - border);
                    style.border = Rect<LengthPercentage>::from_length(border, border, border, border);
                    style.gap.set_main(dir, LengthPercentage::length(10));
                    style.max_size.set_main(dir, LengthPercentageAuto::length(100));
                    style.scrollbar_width = scrollbar;
                    if (is_row(dir)) style.overflow.y = Overflow::Scroll;
                    else style.overflow.x = Overflow::Scroll;
                    if (sizing == 1) style.size.set_main(dir, Dimension::length(100));
                    if (sizing == 2) style.min_size.set_main(dir, LengthPercentageAuto::length(100));
                    const auto root = tree.new_with_children(style, std::vector{first, second}).value();
                    REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::max_content()).has_value());
                    // 90px of items/gap cannot fit the 80px (or 70px with a
                    // scrollbar) content space. Both axes must form two lines.
                    near(tree.layout(root).size.main(dir), 100);
                    near(tree.layout(root).size.cross(dir), 60);
                    near(tree.layout(first).location.x, 10);
                    near(tree.layout(first).location.y, 10);
                    near(tree.layout(second).location.x, is_row(dir) ? 10 : 30);
                    near(tree.layout(second).location.y, is_row(dir) ? 30 : 10);
                }
            }
        }
    }
}

TEST_CASE("Production capped flex roots still hug content below the limit",
          "[production][flex][wrap-insets]") {
    for (auto dir : {FlexDirection::Row, FlexDirection::Column}) {
        CAPTURE(dir);
        Tree tree;
        auto child_style = box(is_row(dir) ? 40 : 20, is_row(dir) ? 20 : 40);
        child_style.flex_shrink = 0;
        const auto child = tree.new_leaf(child_style).value();
        Style style;
        style.display = Display::Flex;
        style.flex_direction = dir;
        style.flex_wrap = FlexWrap::Wrap;
        style.padding = Rect<LengthPercentage>::from_length(10, 10, 10, 10);
        style.max_size.set_main(dir, LengthPercentageAuto::length(100));
        const auto root = tree.new_with_children(style, std::vector{child}).value();
        REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::max_content()).has_value());
        near(tree.layout(root).size.main(dir), 60);
        near(tree.layout(root).size.cross(dir), 40);
    }
}

// A paragraph 855px wide on one line, never narrower than its longest word
// (107px), and 19px per line.
static Size<float> measure_paragraph(const LayoutInput& input, NodeId, NodeContext*, const Style&) {
    const auto available = input.available_space.width;
    const float width = input.known_dimensions.width.value_or(
        available.is_definite()      ? std::clamp(available.value(), 107.0f, 855.0f)
        : available.is_min_content() ? 107.0f
                                     : 855.0f);
    const float lines = std::max(1.0f, std::ceil(855.0f / width));
    return {width, input.known_dimensions.height.value_or(19 * lines)};
}

// A root with 8px padding around one measured paragraph.
static NodeId paragraph_root(Tree& tree, Style style, NodeId& text) {
    tree.set_intrinsic_measure(measure_paragraph);
    text = tree.new_leaf(Style{}).value();
    style.padding = Rect<LengthPercentage>::from_length(8, 8, 8, 8);
    return tree.new_with_children(style, std::vector{text}).value();
}

static Style capped_column(float max_width) {
    Style style;
    style.display = Display::Flex;
    style.flex_direction = FlexDirection::Column;
    style.max_size.width = LengthPercentageAuto::length(max_width);
    return style;
}

// Browsers size an auto-width box with a max width first, then lay out its
// contents at that width: Chrome gives each of these a 360 x 73 root with the
// paragraph wrapped at 344 (three lines).
TEST_CASE("Production capped roots lay out their children within the cap", "[production][root]") {
    for (int kind : {0, 1, 2}) {
        CAPTURE(kind);
        Tree tree;
        auto style = capped_column(360);
        if (kind == 1)
            style.align_items = AlignItems::start();
        if (kind == 2)
            style.display = Display::Grid;
        NodeId text;
        const auto root = paragraph_root(tree, style, text);
        REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::max_content()).has_value());
        near(tree.layout(root).size.width, 360);
        near(tree.layout(root).size.height, 73);
        near(tree.layout(text).size.width, 344);
        near(tree.layout(text).size.height, 57);
    }
}

// A percentage child resolves against the capped width, as it does when the
// same container is nested in a hugging column, and in Chrome (172 = 50% of 344).
TEST_CASE("Production capped roots resolve percentage children against the cap",
          "[production][root]") {
    Tree tree;
    tree.set_intrinsic_measure(measure_paragraph);
    auto half_style = box(0, 20);
    half_style.size.width = Dimension::percent(0.5f);
    half_style.flex_shrink = 0;
    const auto half = tree.new_leaf(half_style).value();
    const auto text = tree.new_leaf(Style{}).value();
    auto style = capped_column(360);
    style.align_items = AlignItems::start();
    style.padding = Rect<LengthPercentage>::from_length(8, 8, 8, 8);
    const auto root = tree.new_with_children(style, std::vector{half, text}).value();
    REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::max_content()).has_value());
    near(tree.layout(root).size.width, 360);
    near(tree.layout(half).size.width, 172);
    near(tree.layout(text).size.height, 57);
}

// Chrome gives the same result when the root is offered more space than its cap.
TEST_CASE("Production capped roots in a wider definite space lay out within the cap",
          "[production][root]") {
    Tree tree;
    NodeId text;
    const auto root = paragraph_root(tree, capped_column(360), text);
    REQUIRE(
        tree.compute_layout(root, {AvailableSpace::definite(1000), AvailableSpace::max_content()})
            .has_value());
    near(tree.layout(root).size.width, 360);
    near(tree.layout(root).size.height, 73);
    near(tree.layout(text).size.height, 57);
}

// A content-box max width excludes the padding: Chrome gives a 376 x 73 root.
TEST_CASE("Production content-box capped roots lay out within the cap", "[production][root]") {
    Tree tree;
    auto style = capped_column(360);
    style.box_sizing = BoxSizing::ContentBox;
    NodeId text;
    const auto root = paragraph_root(tree, style, text);
    REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::max_content()).has_value());
    near(tree.layout(root).size.width, 376);
    near(tree.layout(root).size.height, 73);
    near(tree.layout(text).size.width, 360);
    near(tree.layout(text).size.height, 57);
}

// A cap above the contents changes nothing, including for a 1fr grid track
// (Chrome: 871 x 35 for both).
TEST_CASE("Production roots below their max size still lay out at their contents' size",
          "[production][root]") {
    for (const bool grid : {false, true}) {
        CAPTURE(grid);
        Tree tree;
        auto style = capped_column(2000);
        if (grid) {
            style.display = Display::Grid;
            style.grid_template_columns = {TrackSizingFunction::fr(1)};
        }
        NodeId text;
        const auto root = paragraph_root(tree, style, text);
        REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::max_content()).has_value());
        near(tree.layout(root).size.width, 871);
        near(tree.layout(root).size.height, 35);
        near(tree.layout(text).size.width, 855);
    }
}

// A max width that is not a number caps nothing: the root lays out exactly as
// it does without one, including a percentage child.
TEST_CASE("Production roots with a NaN max width lay out as without one", "[production][root]") {
    const auto lay_out = [](bool nan_max) {
        Tree tree;
        tree.set_intrinsic_measure(measure_paragraph);
        auto half_style = box(0, 20);
        half_style.size.width = Dimension::percent(0.5f);
        const auto half = tree.new_leaf(half_style).value();
        const auto text = tree.new_leaf(Style{}).value();
        Style style;
        style.display = Display::Flex;
        style.flex_direction = FlexDirection::Column;
        style.align_items = AlignItems::start();
        if (nan_max)
            style.max_size.width = LengthPercentageAuto::length(std::nanf(""));
        const auto root = tree.new_with_children(style, std::vector{half, text}).value();
        REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::max_content()).has_value());
        return std::array{tree.layout(root).size.width, tree.layout(root).size.height,
                          tree.layout(half).size.width, tree.layout(text).size.height};
    };
    const auto with_nan = lay_out(true);
    const auto without = lay_out(false);
    for (std::size_t i = 0; i < with_nan.size(); ++i)
        near(with_nan[i], without[i]);
}

// A root just under its cap is not capped: with content 99.995 wide and a max
// width of 100, it lays out as without the max width, so a percentage child
// still resolves against an indefinite width.
TEST_CASE("Production roots just below their max width lay out as without one",
          "[production][root]") {
    const auto lay_out = [](bool capped) {
        Tree tree;
        const auto wide = tree.new_leaf(box(99.995f, 20)).value();
        auto half_style = box(0, 20);
        half_style.size.width = Dimension::percent(0.5f);
        const auto half = tree.new_leaf(half_style).value();
        Style style;
        style.display = Display::Flex;
        style.flex_direction = FlexDirection::Column;
        style.align_items = AlignItems::start();
        if (capped)
            style.max_size.width = LengthPercentageAuto::length(100);
        const auto root = tree.new_with_children(style, std::vector{wide, half}).value();
        REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::max_content()).has_value());
        return std::array{tree.layout(root).size.width, tree.layout(half).size.width};
    };
    const auto capped = lay_out(true);
    const auto uncapped = lay_out(false);
    near(capped[0], uncapped[0]);
    near(capped[1], uncapped[1]);
}

// A min size is a floor on the root's own size, not space to lay out in: at
// min-content the paragraph still wraps at its longest word (Chrome: 123 x 168).
TEST_CASE("Production min sizes do not widen a min-content root's layout", "[production][root]") {
    Tree tree;
    Style style;
    style.display = Display::Flex;
    style.min_size.width = LengthPercentageAuto::length(10);
    NodeId text;
    const auto root = paragraph_root(tree, style, text);
    REQUIRE(
        tree.compute_layout(root, {AvailableSpace::min_content(), AvailableSpace::max_content()})
            .has_value());
    near(tree.layout(root).size.width, 123);
    near(tree.layout(root).size.height, 168);
    near(tree.layout(text).size.width, 107);
}

static NodeId wrapping_boxes(Tree& tree, Style style, std::vector<NodeId>& boxes) {
    auto child_style = box(150, 20);
    child_style.flex_shrink = 0;
    for (int i = 0; i < 3; ++i)
        boxes.push_back(tree.new_leaf(child_style).value());
    style.display = Display::Flex;
    style.align_items = AlignItems::start();
    style.align_content = AlignContent::start();
    style.padding = Rect<LengthPercentage>::from_length(8, 8, 8, 8);
    return tree.new_with_children(style, boxes).value();
}

// Chrome: a 360px root, two boxes on the first line and one on the second,
// all inside the 8px padding.
TEST_CASE("Production capped wrapping roots keep their padding",
          "[production][flex][wrap-insets]") {
    Tree tree;
    Style style;
    style.flex_wrap = FlexWrap::Wrap;
    style.max_size.width = LengthPercentageAuto::length(360);
    std::vector<NodeId> boxes;
    const auto root = wrapping_boxes(tree, style, boxes);
    REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::max_content()).has_value());
    near(tree.layout(root).size.width, 360);
    near(tree.layout(root).size.height, 56);
    near(tree.layout(boxes[0]).location.x, 8);
    near(tree.layout(boxes[1]).location.x, 158);
    near(tree.layout(boxes[2]).location.x, 8);
    near(tree.layout(boxes[2]).location.y, 28);
}

// Balanced lines shrink to the widest balanced line plus the padding,
// 2 x 150 + 16, as the same row does nested in a hugging column (no browser
// equivalent).
TEST_CASE("Production capped balanced roots keep their padding",
          "[production][flex][wrap-insets]") {
    Tree tree;
    Style style;
    style.flex_wrap = FlexWrap::Balance;
    style.max_size.width = LengthPercentageAuto::length(360);
    std::vector<NodeId> boxes;
    const auto root = wrapping_boxes(tree, style, boxes);
    REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::max_content()).has_value());
    near(tree.layout(root).size.width, 316);
    near(tree.layout(root).size.height, 56);
    near(tree.layout(boxes[2]).location.x, 8);
    near(tree.layout(boxes[2]).location.y, 28);
}

// A wrapping row offered a definite width fills it, padding included (Chrome:
// 360 with or without a 360 max width). This also lets the capped-root path see
// that the cap binds.
TEST_CASE("Production wrapping roots in a definite space keep their padding",
          "[production][flex][wrap-insets]") {
    for (const bool capped : {false, true}) {
        CAPTURE(capped);
        Tree tree;
        Style style;
        style.flex_wrap = FlexWrap::Wrap;
        if (capped)
            style.max_size.width = LengthPercentageAuto::length(360);
        std::vector<NodeId> boxes;
        const auto root = wrapping_boxes(tree, style, boxes);
        REQUIRE(tree.compute_layout(root,
                                    {AvailableSpace::definite(360), AvailableSpace::max_content()})
                    .has_value());
        near(tree.layout(root).size.width, 360);
        near(tree.layout(root).size.height, 56);
        near(tree.layout(boxes[2]).location.x, 8);
        near(tree.layout(boxes[2]).location.y, 28);
    }
}

// The same for balanced lines, which have no browser equivalent: they fill the
// definite width as plain wrapping does.
TEST_CASE("Production balanced roots in a definite space keep their padding",
          "[production][flex][wrap-insets]") {
    Tree tree;
    Style style;
    style.flex_wrap = FlexWrap::Balance;
    std::vector<NodeId> boxes;
    const auto root = wrapping_boxes(tree, style, boxes);
    REQUIRE(
        tree.compute_layout(root, {AvailableSpace::definite(360), AvailableSpace::max_content()})
            .has_value());
    near(tree.layout(root).size.width, 360);
    near(tree.layout(root).size.height, 56);
}

// Nested: a wrapping row offered 400px by an align-start column fills it
// (Chrome: 400).
TEST_CASE("Production wrapping items in a definite space keep their padding",
          "[production][flex][wrap-insets]") {
    Tree tree;
    Style style;
    style.flex_wrap = FlexWrap::Wrap;
    std::vector<NodeId> boxes;
    const auto row = wrapping_boxes(tree, style, boxes);
    Style parent;
    parent.display = Display::Flex;
    parent.flex_direction = FlexDirection::Column;
    parent.align_items = AlignItems::start();
    parent.size.width = Dimension::length(400);
    const auto root = tree.new_with_children(parent, std::vector{row}).value();
    REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::max_content()).has_value());
    near(tree.layout(row).size.width, 400);
    near(tree.layout(row).size.height, 56);
    near(tree.layout(boxes[2]).location.y, 28);
}

TEST_CASE("Production early intrinsic flex sizing still finalizes percentage gaps",
          "[production][flex][intrinsic-finalization]") {
    for (auto dir : {FlexDirection::Row, FlexDirection::Column}) {
        CAPTURE(dir);
        Tree tree;
        auto child_style = box(is_row(dir) ? 20 : 10, is_row(dir) ? 10 : 20);
        child_style.flex_shrink = 0;
        const auto first = tree.new_leaf(child_style).value();
        const auto second = tree.new_leaf(child_style).value();
        Style style;
        style.display = Display::Flex;
        style.flex_direction = dir;
        style.flex_wrap = FlexWrap::Wrap;
        style.gap.set_main(dir, LengthPercentage::percent(0.1f));
        style.max_size.set_main(dir, LengthPercentageAuto::length(100));
        const auto root = tree.new_with_children(style, std::vector{first, second}).value();
        REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::max_content()).has_value());
        // The cyclic gap contributes zero to the 40px intrinsic size, then
        // finalization resolves 10% of that inner size to a 4px used gap.
        near(tree.layout(root).size.main(dir), 40);
        const auto second_position = tree.layout(second).location;
        near(is_row(dir) ? second_position.x : second_position.y, 24);
    }
}

TEST_CASE("Production early intrinsic flex sizing preserves capped outer size below scrollbar insets",
          "[production][flex][intrinsic-finalization]") {
    for (auto dir : {FlexDirection::Row, FlexDirection::Column}) {
        for (float padding : {0.0f, 10.0f}) {
            CAPTURE(dir, padding);
            Tree tree;
            auto child_style = box(is_row(dir) ? 20 : 10, is_row(dir) ? 10 : 20);
            child_style.flex_shrink = 0;
            const auto child = tree.new_leaf(child_style).value();
            Style style;
            style.display = Display::Flex;
            style.box_sizing = BoxSizing::BorderBox;
            style.flex_direction = dir;
            style.flex_wrap = FlexWrap::Wrap;
            style.padding = Rect<LengthPercentage>::from_length(padding, padding, padding, padding);
            style.scrollbar_width = 10;
            if (is_row(dir)) style.overflow.y = Overflow::Scroll;
            else style.overflow.x = Overflow::Scroll;
            const float maximum = 2 * padding + 5;
            style.max_size.set_main(dir, LengthPercentageAuto::length(maximum));
            const auto root = tree.new_with_children(style, std::vector{child}).value();
            REQUIRE(tree.compute_layout(root, Size<AvailableSpace>::max_content()).has_value());
            // Inner size is clamped to zero. Reconstructing outer size as
            // inner + insets would incorrectly produce 10px / 30px here.
            near(tree.layout(root).size.main(dir), maximum);
        }
    }
}
