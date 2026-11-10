#include <catch2/catch_test_macros.hpp>
#include <taffy/compute.hpp>
#include <taffy/tree.hpp>

#include <array>
#include <functional>

namespace {
using namespace taffy;

struct ReentrantContext final : NodeContext {
    std::function<void()> callback;

    explicit ReentrantContext(std::function<void()> value) : callback(std::move(value)) {}

    ~ReentrantContext() override {
        callback();
    }
};

LayoutInput size_input() {
    LayoutInput input;
    input.run_mode = RunMode::ComputeSize;
    input.parent_size = {100.0f, 100.0f};
    return input;
}
} // namespace

TEST_CASE("context destruction rejects reentrant mutation after consistent bookkeeping",
          "[production][tree]") {
    for (int operation = 0; operation != 5; ++operation) {
        Tree tree(1);
        bool rejected = false;
        bool observed = false;
        NodeId node;
        Tree* active = &tree;
        auto context = std::make_unique<ReentrantContext>([&] {
            observed = true;
            // Removal publishes the stale handle before calling application code.
            if (operation == 0)
                CHECK_FALSE(active->contains(node));
            try {
                (void)active->new_leaf();
            } catch (const TreePanic&) {
                rejected = true;
            }
        });
        node = tree.new_leaf_with_context({}, std::move(context)).value();
        if (operation == 0)
            REQUIRE(tree.remove(node));
        else if (operation == 1)
            tree.clear();
        else if (operation == 2)
            REQUIRE(tree.set_node_context(node, nullptr));
        else if (operation == 3)
            tree = Tree{};
        else {
            auto owner = std::make_unique<Tree>(std::move(tree));
            active = owner.get();
            owner.reset();
        }
        CHECK(observed);
        CHECK(rejected);
    }
}

TEST_CASE("removed absolute descendants do not survive leaf or hidden transitions",
          "[production][tree]") {
    Tree tree;
    Style container;
    container.position = Position::Relative;
    container.size = {Dimension::length(100), Dimension::length(100)};
    Style absolute;
    absolute.position = Position::Absolute;
    absolute.size = {Dimension::length(10), Dimension::length(10)};
    const auto child = tree.new_leaf(absolute).value();
    const auto root = tree.new_with_children(container, std::array{child}).value();
    const Size<AvailableSpace> available{AvailableSpace::definite(100),
                                         AvailableSpace::definite(100)};
    REQUIRE(tree.compute_layout(root, available));
    REQUIRE_FALSE(tree.hoisted_child_ids(root).empty());
    const auto hoisted = tree.hoisted_child_ids(root);
    REQUIRE(tree.compute_layout(root, available)); // A warm hit must preserve IDs.
    CHECK(tree.hoisted_child_ids(root) == hoisted);
    REQUIRE(tree.remove(child));
    REQUIRE_NOTHROW(tree.compute_layout(root, available).value());
    CHECK(tree.hoisted_child_ids(root).empty());

    const auto next = tree.new_leaf(absolute).value();
    REQUIRE(tree.add_child(root, next));
    REQUIRE(tree.compute_layout(root, available));
    REQUIRE_FALSE(tree.hoisted_child_ids(root).empty());
    container.display = Display::None;
    REQUIRE(tree.set_style(root, container));
    REQUIRE(tree.compute_layout(root, available));
    CHECK(tree.hoisted_child_ids(root).empty());
}

TEST_CASE("measurement cache keys include parent height", "[production][cache]") {
    Tree tree;
    Style style;
    style.size = {Dimension::length(10), Dimension::percent(0.5f)};
    const auto leaf = tree.new_leaf(style).value();
    auto input = size_input();
    CHECK(tree.compute_child_layout(leaf, input).size.height == 50);
    CHECK(tree.cache_contains(leaf, input));
    input.parent_size.height = 200;
    CHECK_FALSE(tree.cache_contains(leaf, input));
    CHECK(tree.compute_child_layout(leaf, input).size.height == 100);

    for (const auto axis :
         {RequestedAxis::Horizontal, RequestedAxis::Vertical, RequestedAxis::Both}) {
        Cache cache;
        auto key = size_input();
        key.axis = axis;
        cache.store(key, LayoutOutput::from_outer_size({10, 20}));
        for (const auto requested :
             {RequestedAxis::Horizontal, RequestedAxis::Vertical, RequestedAxis::Both}) {
            auto query = key;
            query.axis = requested;
            CHECK(cache.contains(query) == (axis == RequestedAxis::Both || axis == requested));
            query.parent_size.height = 200;
            CHECK_FALSE(cache.contains(query));
        }
    }

    for (const auto run : {RunMode::ComputeSize, RunMode::PerformLayout}) {
        Cache cache;
        auto normal = size_input();
        normal.run_mode = run;
        cache.store(normal, LayoutOutput::from_outer_size({10, 20}));
        const auto before = cache;
        REQUIRE(cache.contains(normal));
        CHECK(cache == before);
    }
}

TEST_CASE("child mutations invalidate cached ancestors with negative percentage bases",
          "[production][tree][cache]") {
    Tree tree;
    Style child_style;
    child_style.size = Size<Dimension>::from_lengths(5, 5);
    const auto child = tree.new_leaf(child_style).value();
    Style root_style;
    root_style.size = Size<Dimension>::from_lengths(10, 10);
    root_style.overflow = {Overflow::Scroll, Overflow::Scroll};
    root_style.scrollbar_width = 20;
    const auto root = tree.new_with_children(root_style, std::array{child}).value();
    const auto available = Size<AvailableSpace>::from_lengths(100, 100);
    REQUIRE(tree.compute_layout(root, available));
    REQUIRE(tree.layout(child).size.height == 5);
    REQUIRE_FALSE(tree.dirty(child).value());
    REQUIRE_FALSE(tree.dirty(root).value());

    child_style.size.height = Dimension::length(8);
    REQUIRE(tree.set_style(child, child_style));
    REQUIRE(tree.dirty(root).value());
    REQUIRE(tree.compute_layout(root, available));
    CHECK(tree.layout(child).size.height == 8);
    REQUIRE(tree.compute_layout(root, available));
    CHECK(tree.layout(child).size.height == 8);
}

TEST_CASE("cycle certificates respect shared duplicate and reparented child lists",
          "[production][tree]") {
    Tree tree;
    const auto a = tree.new_leaf().value();
    const auto b = tree.new_leaf().value();
    const auto c = tree.new_leaf().value();
    REQUIRE(tree.add_child(a, b));
    REQUIRE(tree.add_child(c, b));    // parent(b) no longer describes the a -> b edge.
    REQUIRE(tree.remove_child(c, b)); // parent(b) is now empty, but a -> b remains.
    CHECK_THROWS_AS(tree.add_child(b, a), TreePanic);
    REQUIRE(tree.add_child(a, b));
    REQUIRE(tree.remove_child(a, b)); // One duplicate remains.
    CHECK_THROWS_AS(tree.add_child(b, a), TreePanic);
    REQUIRE(tree.replace_child_at_index(a, 0, b)); // old == child resets parent only.
    CHECK_THROWS_AS(tree.add_child(b, a), TreePanic);
    REQUIRE(tree.set_children(c, std::array{a}));
    CHECK_THROWS_AS(tree.add_child(b, c), TreePanic);
    REQUIRE(tree.remove_children_range(a, 0, 1));
    REQUIRE(tree.add_child(b, c)); // No cycle after removing the actual last edge.
}

TEST_CASE("bottom-up chain attachment retains cycle safety", "[production][tree]") {
    Tree tree;
    const auto bottom = tree.new_leaf().value();
    auto root = bottom;
    for (int i = 0; i != 2000; ++i) {
        const auto parent = tree.new_leaf().value();
        REQUIRE(tree.add_child(parent, root));
        root = parent;
    }
    CHECK_THROWS_AS(tree.add_child(bottom, root), TreePanic);
}
