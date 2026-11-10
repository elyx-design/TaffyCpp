#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <taffy/taffy.hpp>

#include "../src/block.hpp"
#include "../src/flex/internal.hpp"

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
