#include "support/custom_tree_oracle.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <algorithm>
#include <exception>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace custom_tree {
struct Value {
    unsigned tag = 0;
    float value = 0, px = 0, pct = 0;
};
Value automatic() { return {}; }
Value length(float value) { return {1, value}; }
Value percent(float value) { return {2, value}; }
// Coefficients are copied into an aligned native box owned by the tree at import.
Value calc(float px, float pct) { return {3, 0, px, pct}; }
Value fit_content(Value limit) { return limit; }
struct Style {
    Value width, height, min_width, min_height;
    std::vector<Value> rows, columns;
};
enum class Kind : unsigned { Block, Grid, Leaf };
constexpr CtAvailable max_content{2, 0}, min_content{1, 0};
CtAvailable definite(float value) { return {0, value}; }
struct Session;
struct Node {
    Kind kind;
    Style style;
    float min_width = 0, max_width = 0, height = 0;
    CtSize layout{};
    std::vector<Node> children;
    std::unique_ptr<Session> session;
    Node(Kind, Style, std::vector<Node>);
    Node(const Node&);
    ~Node();
    static Node leaf(float min, float max, float height, Style style = {});
    void compute_layout(CtAvailable width = max_content, CtAvailable height = max_content);
};
void check(int status) {
    if (status) throw std::runtime_error("custom-tree ABI status " + std::to_string(status));
}
struct Session {
    std::uint64_t tree = 0, root = 0;
    std::unordered_map<std::uint64_t, Node*> nodes;
    std::exception_ptr exception;
    bool throw_resolve = false, throw_measure = false;
    std::uint64_t resolver_calls = 0, measure_calls = 0;
    Session() { check(ct_create(resolve, measure, this, &tree)); }
    ~Session() { if (tree) ct_destroy(tree); }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    static float resolve(void* context, float px, float pct, float basis) noexcept {
        auto& self = *static_cast<Session*>(context);
        try {
            ++self.resolver_calls;
            if (self.throw_resolve) throw std::runtime_error("resolver exception");
            return pct * basis + px;
        } catch (...) {
            if (!self.exception) self.exception = std::current_exception();
            return 0;
        }
    }
    static CtSize measure(void* context, std::uint64_t id, CtMeasureInput input) noexcept {
        auto& self = *static_cast<Session*>(context);
        try {
            ++self.measure_calls;
            if (self.throw_measure) throw std::runtime_error("measurement exception");
            const auto& node = *self.nodes.at(id);
            const float intrinsic = input.available_width.tag == 0
                ? std::clamp(input.available_width.value, node.min_width, node.max_width)
                : input.available_width.tag == 1 ? node.min_width : node.max_width;
            return {input.known_width ? input.width : intrinsic,
                    input.known_height ? input.height : node.height};
        } catch (...) {
            if (!self.exception) self.exception = std::current_exception();
            return {};
        }
    }
    CtValue value(Value v) {
        std::uint64_t handle = 0;
        if (v.tag == 3) check(ct_calc(tree, v.px, v.pct, &handle));
        return {v.tag, v.value, handle};
    }
    CtStyle style(const Style& s) {
        return {value(s.width), value(s.height), value(s.min_width), value(s.min_height)};
    }
    std::uint64_t add(Node& node) {
        if (node.style.rows.size() > 1 || node.style.columns.size() > 1)
            throw std::invalid_argument("custom-tree helper supports one track per axis");
        std::uint64_t id = 0;
        check(ct_node(tree, static_cast<unsigned>(node.kind), style(node.style), &id));
        nodes.emplace(id, &node);
        for (const auto& row : node.style.rows) check(ct_track(tree, id, 0, value(row)));
        for (const auto& column : node.style.columns) check(ct_track(tree, id, 1, value(column)));
        std::vector<std::uint64_t> children;
        for (auto& child : node.children) children.push_back(add(child));
        check(ct_children(tree, id, children.data(), static_cast<unsigned>(children.size())));
        return id;
    }
    void compute(CtAvailable width, CtAvailable height) {
        exception = nullptr;
        const int status = ct_compute(tree, root, width, height);
        if (exception) {
            // A fallback callback result may have reached native caches: invalidate before retry.
            for (const auto& entry : nodes) check(ct_style(tree, entry.first, style(entry.second->style)));
            std::rethrow_exception(exception);
        }
        check(status);
        for (const auto& entry : nodes) check(ct_layout(tree, entry.first, &entry.second->layout));
    }
    CtCounts counts() { CtCounts c{}; check(ct_counts(tree, &c)); return c; }
};
Node::Node(Kind k, Style s, std::vector<Node> c) : kind(k), style(std::move(s)), children(std::move(c)) {}
Node::Node(const Node& n) : kind(n.kind), style(n.style), min_width(n.min_width), max_width(n.max_width), height(n.height), children(n.children) {}
Node::~Node() = default;
Node Node::leaf(float min, float max, float h, Style s) {
    Node result(Kind::Leaf, std::move(s), {});
    result.min_width = min; result.max_width = max; result.height = h;
    return result;
}
void Node::compute_layout(CtAvailable available_width, CtAvailable available_height) {
    if (!session) { session = std::make_unique<Session>(); session->root = session->add(*this); }
    session->compute(available_width, available_height);
}
std::pair<float, float> layout_single_column_grid(Value column, Value container_width, CtAvailable available_width) {
    Style style;
    style.width = container_width; style.height = length(40);
    style.columns = {column};
    Node root(Kind::Grid, style, {Node::leaf(50, 150, 10)});
    root.compute_layout(available_width, max_content);
    return {root.layout.width, root.children[0].layout.width};
}
} // namespace custom_tree
using namespace custom_tree;

TEST_CASE("hand_written::fit_content_calc::fit_content_calc::grid_track_fit_content_calc_limit", "[custom-tree][ported]") {
    Style root_style;
    root_style.width = length(100); root_style.height = length(300);
    root_style.rows = {fit_content(calc(5, 0.1f))};
    Style child_style; child_style.min_height = length(0);
    Node root(Kind::Grid, root_style, {Node::leaf(0, 0, 400, child_style)});
    root.compute_layout();
    REQUIRE(root.children[0].layout.height == 35.0f);
    const auto counts = root.session->counts();
    REQUIRE(counts.calc > 0);
    REQUIRE(counts.measure > 0);
    REQUIRE(counts.cache_store > 0);
    // These exact fixtures have no out-of-flow items: hoisting is not requested.
    REQUIRE(counts.hoist_clear == 0);
    REQUIRE(counts.hoist_add == 0);
    REQUIRE(counts.calc == root.session->resolver_calls);
    REQUIRE(counts.measure == root.session->measure_calls);
}

TEST_CASE("hand_written::fit_content_calc::fit_content_calc::block_item_fit_content_calc_width", "[custom-tree][ported]") {
    const auto limit = fit_content(calc(70, 0.1f));
    Style root_style; root_style.width = length(300);
    Style child_style; child_style.width = limit;
    Node root(Kind::Block, root_style, {Node::leaf(50, 400, 10, child_style)});
    root.compute_layout();
    REQUIRE(root.children[0].layout.width == 100.0f);
    REQUIRE(root.session->counts().calc > 0);
    REQUIRE(root.session->counts().measure > 0);
}

TEST_CASE("hand_written::fit_content_calc::fit_content_calc::grid_track_fit_content_calc_matches_equivalent_length_and_percentage", "[custom-tree][ported]") {
    const auto calc_result = layout_single_column_grid(fit_content(calc(10, 0.2f)), length(200), max_content);
    const auto px_result = layout_single_column_grid(fit_content(length(50)), length(200), max_content);
    REQUIRE(calc_result == px_result);
    REQUIRE(calc_result.second == 50.0f);
    for (const auto available_width : {definite(200), max_content, min_content}) {
        INFO("available width tag: " << available_width.tag);
        const auto loop_calc_result = layout_single_column_grid(fit_content(calc(0, 0.5f)), automatic(), available_width);
        const auto pct_result = layout_single_column_grid(fit_content(percent(0.5f)), automatic(), available_width);
        REQUIRE(loop_calc_result == pct_result);
    }
    const auto [container_width, item_width] = layout_single_column_grid(fit_content(calc(0, 0.5f)), automatic(), max_content);
    REQUIRE(container_width == 150.0f);
    REQUIRE(item_width == 75.0f);
}

TEST_CASE("custom_tree::supplemental::cache_invalidation_and_callback_exceptions", "[custom-tree][supplemental]") {
    Style s; s.width = length(300);
    Style child; child.width = fit_content(calc(70, 0.1f));
    Node root(Kind::Block, s, {Node::leaf(50, 400, 10, child)});
    root.compute_layout();
    auto& session = *root.session;
    const auto before = session.counts();
    root.compute_layout();
    const auto after = session.counts();
    REQUIRE(after.cache_hit > before.cache_hit);
    REQUIRE(after.measure == before.measure);
    REQUIRE(after.cache_store == before.cache_store);
    const auto child_id = [&] { for (const auto& e : session.nodes) if (e.second == &root.children[0]) return e.first; return std::uint64_t{0}; }();
    child.width = length(123);
    check(ct_style(session.tree, child_id, session.style(child)));
    root.children[0].style = child;
    root.compute_layout();
    REQUIRE(root.children[0].layout.width == 123);
    REQUIRE(session.counts().cache_clear > after.cache_clear);
    child.width = fit_content(calc(70, 0.1f));
    root.children[0].style = child;
    check(ct_style(session.tree, child_id, session.style(child)));
    session.throw_measure = true;
    REQUIRE_THROWS_WITH(root.compute_layout(), "measurement exception");
    session.throw_measure = false;
    session.throw_resolve = true;
    REQUIRE_THROWS_WITH(root.compute_layout(), "resolver exception");
    session.throw_resolve = false;
    root.compute_layout();
    REQUIRE(root.children[0].layout.width == 100);
}

TEST_CASE("custom_tree::supplemental::track_mutation_and_topology_validation", "[custom-tree][supplemental]") {
    Style s; s.width = length(200); s.height = length(40); s.columns = {fit_content(length(50))};
    Node root(Kind::Grid, s, {Node::leaf(50, 150, 10)});
    root.compute_layout();
    REQUIRE(root.children[0].layout.width == 50);
    auto& session = *root.session;
    const auto before = session.counts();
    check(ct_track(session.tree, session.root, 1, session.value(fit_content(length(80)))));
    root.compute_layout();
    REQUIRE(root.children[0].layout.width == 80);
    REQUIRE(session.counts().cache_clear > before.cache_clear);
    REQUIRE(session.counts().measure > before.measure);
    const auto invalid_child = session.root;
    REQUIRE(ct_children(session.tree, session.root, &invalid_child, 1) == 1);
    REQUIRE(ct_track(session.tree, session.root, 2, {1, 50, 0}) == 1);
    root.compute_layout();
    REQUIRE(root.children[0].layout.width == 80);
    check(ct_children(session.tree, session.root, nullptr, 0));
    root.compute_layout();
    REQUIRE(root.layout.width == 200);
    Style unsupported; unsupported.columns = {length(1), length(2)};
    Node multi(Kind::Grid, unsupported, {});
    REQUIRE_THROWS_WITH(multi.compute_layout(), "custom-tree helper supports one track per axis");
}

TEST_CASE("custom_tree::supplemental::logical_handles_and_panic_boundary", "[custom-tree][supplemental]") {
    Session a, b;
    std::uint64_t node = 0, coefficient = 0;
    check(ct_node(a.tree, 2, {}, &node));
    check(ct_calc(a.tree, 1, 0.5f, &coefficient));
    CtSize untouched{17, 19};
    REQUIRE(ct_layout(b.tree, node, &untouched) == 1);
    REQUIRE(untouched.width == 17);
    REQUIRE(untouched.height == 19);
    CtStyle invalid{}; invalid.width = {3, 0, coefficient};
    std::uint64_t unchanged = 999;
    REQUIRE(ct_node(b.tree, 2, invalid, &unchanged) == 1);
    REQUIRE(unchanged == 999);
    const auto stale = a.tree;
    check(ct_destroy(a.tree)); a.tree = 0;
    REQUIRE(ct_layout(stale, node, &untouched) == 1);
    REQUIRE(ct_destroy(stale) == 1);
    REQUIRE(ct_panic_probe() == 2);
    REQUIRE(ct_counts(b.tree, nullptr) == 1);
    REQUIRE(b.counts().measure == 0);
}

TEST_CASE("custom_tree::supplemental::panic_under_lock_permanently_disables_only_affected_tree", "[custom-tree][supplemental][poison-lifecycle]") {
    Node affected = Node::leaf(10, 30, 20);
    Node independent = Node::leaf(40, 60, 25);
    affected.compute_layout();
    independent.compute_layout();
    auto& session = *affected.session;
    const auto tree = session.tree;
    const auto node = session.root;
    REQUIRE(affected.layout.width == 30);
    // The probe writes -999 into native layout before panicking with the mutex held.
    // Status 2 proves Rust unwinding was caught before returning across the C ABI.
    REQUIRE(ct_tree_panic_probe(tree, node) == 2);
    REQUIRE_THROWS_WITH(affected.compute_layout(), "custom-tree ABI status 1");
    REQUIRE(ct_compute(tree, node, max_content, max_content) == 1);
    CtSize untouched{17, 19};
    REQUIRE(ct_layout(tree, node, &untouched) == 1);
    REQUIRE(untouched.width == 17);
    REQUIRE(untouched.height == 19);
    std::uint64_t unchanged = 999;
    REQUIRE(ct_node(tree, 2, {}, &unchanged) == 1);
    REQUIRE(unchanged == 999);
    REQUIRE(ct_calc(tree, 5, 0.5f, &unchanged) == 1);
    REQUIRE(unchanged == 999);
    REQUIRE(ct_style(tree, node, {}) == 1);
    REQUIRE(ct_children(tree, node, nullptr, 0) == 1);
    REQUIRE(ct_track(tree, node, 1, {1, 50, 0}) == 1);
    CtCounts counts{}; counts.measure = 777;
    REQUIRE(ct_counts(tree, &counts) == 1);
    REQUIRE(counts.measure == 777);
    REQUIRE(ct_tree_panic_probe(tree, node) == 1);
    independent.compute_layout();
    REQUIRE(independent.layout.width == 60);
    REQUIRE(independent.layout.height == 25);
    // Independent mutations and creation must work too, not just cached reads.
    std::uint64_t healthy_node = 0;
    REQUIRE(ct_node(independent.session->tree, 2, {}, &healthy_node) == 0);
    independent.compute_layout();
    REQUIRE(independent.layout.width == 60);
    Node created_after = Node::leaf(70, 90, 35);
    created_after.compute_layout();
    REQUIRE(created_after.layout.width == 90);
    REQUIRE(ct_destroy(tree) == 0);
    session.tree = 0; // Session's destructor must not destroy the logical handle twice.
    REQUIRE(ct_destroy(tree) == 1);
    independent.compute_layout();
    REQUIRE(independent.layout.width == 60);
}
