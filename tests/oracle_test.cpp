// Translations from Taffy 0.15.1 tests/hand_written/{measure,caching}.rs.
// Other cases are bridge checks, not claimed as translated upstream coverage.
#include "support/measure_helpers.hpp"
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace {
using taffy::test::RustBackend;
using taffy::test::RustBackendError;
using taffy::test::TestNodeContext;

constexpr OracleSizing max_content{{4, 0}, {4, 0}};

RustBackend::NodeId measured(RustBackend& tree, TestNodeContext& context) {
    return tree.new_node(
        tree.default_style(), [&context](auto node, const auto& input, const auto& style) {
            return taffy::test::test_measure_function(input, node, &context, style);
        });
}

RustBackend::NodeId parent(RustBackend& tree, RustBackend::NodeId child) {
    const auto node = tree.new_node(tree.default_style());
    tree.set_children(node, std::array{child});
    return node;
}

void require_size(const OracleLayout& layout, float width, float height) {
    REQUIRE(layout.size.width == width);
    REQUIRE(layout.size.height == height);
}

void require_status(auto&& action, std::uint32_t status) {
    try {
        action();
        FAIL("Expected a backend error");
    } catch (const RustBackendError& error) {
        REQUIRE(error.status() == status);
    }
}
} // namespace

TEST_CASE("measure::measure::measure_root", "[upstream][measure]") {
    RustBackend tree;
    auto context = TestNodeContext::fixed(100, 100);
    const auto node = measured(tree, context);
    tree.compute(node, max_content);
    require_size(tree.layout(node), 100, 100);
}

TEST_CASE("measure::measure::measure_child", "[upstream][measure]") {
    RustBackend tree;
    auto context = TestNodeContext::fixed(100, 100);
    const auto child = measured(tree, context);
    const auto node = parent(tree, child);
    tree.compute(node, max_content);
    require_size(tree.layout(node), 100, 100);
    require_size(tree.layout(child), 100, 100);
}

TEST_CASE("measure::measure::measure_child_constraint", "[upstream][measure]") {
    RustBackend tree;
    auto context = TestNodeContext::fixed(100, 100);
    const auto child = measured(tree, context);
    auto style = tree.default_style();
    style.size.width = {1, 50};
    const auto node = tree.new_node(style);
    tree.set_children(node, std::array{child});
    tree.compute(node, max_content);
    require_size(tree.layout(node), 50, 100);
    require_size(tree.layout(child), 100, 100);
}

TEST_CASE("caching::caching::measure_count_flexbox", "[upstream][cache]") {
    RustBackend tree;
    auto context = TestNodeContext::fixed(50, 50);
    const auto leaf = measured(tree, context);
    auto node = parent(tree, leaf);
    for (int depth = 0; depth < 100; ++depth)
        node = parent(tree, node);
    tree.compute(node, max_content);
    REQUIRE(context.count == 6);
}

// Hidden, intentionally failing tooling case. oracle_negative_checks.py must
// select it explicitly and verify the exact count assertion really fails.
TEST_CASE("wrong measurement count is rejected", "[.negative-count]") {
    RustBackend tree;
    auto context = TestNodeContext::fixed(50, 50);
    const auto leaf = measured(tree, context);
    auto node = parent(tree, leaf);
    for (int depth = 0; depth < 100; ++depth)
        node = parent(tree, node);
    tree.compute(node, max_content);
    REQUIRE(context.count == 7); // The unchanged upstream expectation is six.
}

TEST_CASE("bridge retains dirty state, contexts and layout cache", "[bridge]") {
    RustBackend tree;
    auto context = TestNodeContext::fixed(30, 20);
    const auto child = measured(tree, context);
    const auto root = parent(tree, child);
    REQUIRE(tree.dirty(root));
    REQUIRE(tree.parent(child) == root);
    REQUIRE(tree.child(root, 0) == child);
    tree.compute(root, max_content);
    REQUIRE_FALSE(tree.dirty(root));
    const auto count = context.count;
    tree.compute(root, max_content);
    REQUIRE(context.count == count);
    context.measure_data = OracleSize{80, 40};
    tree.mark_dirty(child);
    REQUIRE(tree.dirty(root));
    tree.compute(root, max_content);
    require_size(tree.layout(root), 80, 40);
    REQUIRE(context.count > count);
}

TEST_CASE("bridge releases callback contexts and rejects stale logical IDs", "[bridge]") {
    RustBackend tree;
    auto lifetime = std::make_shared<int>(0);
    std::weak_ptr<int> observer = lifetime;
    const auto node =
        tree.new_node(tree.default_style(),
                      [lifetime](auto, const auto&, const auto&) { return OracleLayoutOutput{}; });
    lifetime.reset();
    REQUIRE_FALSE(observer.expired());
    tree.set_context(node);
    REQUIRE(observer.expired());
    tree.remove(node);
    require_status([&] { (void)tree.layout(node); }, ORACLE_STALE);
    const auto replacement = tree.new_node(tree.default_style());
    REQUIRE(replacement != node);
}

TEST_CASE("bridge distinguishes Result errors, panic and callback exceptions", "[bridge]") {
    RustBackend tree;
    const auto node = tree.new_node(tree.default_style());
    require_status([&] { (void)tree.child(node, 0); }, ORACLE_ERROR);
    tree.set_context(node, [](auto, const auto&, const auto&) -> OracleLayoutOutput {
        throw std::logic_error("measurement failure");
    });
    REQUIRE_THROWS_AS(tree.compute(node, max_content), std::logic_error);
    // No exception crossed the C ABI. A failed callback invalidates this oracle.
    require_status([&] { (void)tree.dirty(node); }, ORACLE_POISONED);
    RustBackend panic_tree;
    require_status([&] { panic_tree.test_panic(); }, ORACLE_PANIC);
    require_status([&] { (void)panic_tree.dirty(node); }, ORACLE_POISONED);
}

TEST_CASE("poisoned contexts retain callbacks until unconditional destruction", "[bridge]") {
    auto lifetime = std::make_shared<int>(0);
    std::weak_ptr<int> observer = lifetime;
    {
        RustBackend tree;
        const auto node =
            tree.new_node(tree.default_style(), [lifetime](auto, const auto&, const auto&) {
                return OracleLayoutOutput{};
            });
        lifetime.reset();
        require_status([&] { tree.test_panic(); }, ORACLE_PANIC);
        require_status([&] { tree.remove(node); }, ORACLE_POISONED);
        require_status([&] { tree.clear(); }, ORACLE_POISONED);
        REQUIRE_FALSE(observer.expired());
    }
    REQUIRE(observer.expired());
}

TEST_CASE("bridge rejects unsupported inputs instead of defaulting", "[bridge]") {
    RustBackend tree;
    auto style = tree.default_style();
    style.size.width = {10, 0}; // calc requires a custom resolver, not implemented yet.
    require_status([&] { (void)tree.new_node(style); }, ORACLE_UNSUPPORTED);
}
