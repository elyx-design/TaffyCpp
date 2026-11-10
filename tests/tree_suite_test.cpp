#include <catch2/catch_test_macros.hpp>
#include "support/tree_suite_oracle.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Operation-by-operation translations of upstream tree/taffy_tree.rs tests,
// tests/hand_written/{caching,relayout}.rs. C++ owns construction, mutation,
// intrinsic measurement, loops and assertions; Rust owns native tree/layout work.
namespace tree_suite {
using Node = std::uint64_t;
using Style = std::vector<std::pair<std::string, std::string>>;
using Layout = TsLayout;
constexpr OracleSizing max_content{{4, 0}, {4, 0}};
constexpr OracleSizing min_content{{3, 0}, {3, 0}};
OracleSizing definite(float width, float height) { return {{1, width}, {1, height}}; }

struct Properties {
    std::vector<TsProperty> values;
    explicit Properties(const Style& style) {
        for (const auto& [name, value] : style)
            values.push_back({{name.data(), name.size()}, {value.data(), value.size()}});
    }
};
void ok(std::uint32_t status) {
    if (status != ORACLE_OK)
        throw std::runtime_error("tree-suite native operation status " + std::to_string(status));
}
bool same_size(OracleSize a, OracleSize b) { return a.width == b.width && a.height == b.height; }
bool same_rect(OracleRect a, OracleRect b) {
    return a.left == b.left && a.right == b.right && a.top == b.top && a.bottom == b.bottom;
}
// Native Layout equality, field for field (not object bytes/padding). content_size
// and scroll_width/height are ABI-derived extras, not fields of upstream Layout.
bool same_layout(const Layout& a, const Layout& b) {
    return a.order == b.order && a.x == b.x && a.y == b.y && same_size(a.size, b.size) &&
        same_rect(a.scrollable_overflow, b.scrollable_overflow) &&
        same_size(a.scrollbar_size, b.scrollbar_size) && same_rect(a.border, b.border) &&
        same_rect(a.padding, b.padding) && same_rect(a.margin, b.margin);
}
bool same_layouts(const std::vector<Layout>& a, const std::vector<Layout>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), same_layout);
}

class Tree;
struct Context {
    Tree* owner = nullptr;
    OracleSize fixed{};
    bool text = false;
    bool size_measure = false;
    std::string content;
    std::size_t count = 0;
    std::atomic_size_t* drops = nullptr;
};

class Tree {
public:
    TsTree* native = nullptr;
    std::exception_ptr callback_exception;
    explicit Tree(std::size_t capacity = std::numeric_limits<std::size_t>::max()) {
        ok(ts_create(capacity, &native));
    }
    ~Tree() { if (native) ts_free(native); }
    Tree(const Tree&) = delete;
    Tree& operator=(const Tree&) = delete;
    static void drop(void* user) noexcept {
        auto* context = static_cast<Context*>(user);
        if (context->drops) context->drops->fetch_add(1, std::memory_order_seq_cst);
        delete context;
    }
    static std::uint32_t intrinsic(void* user, OracleKnown known, OracleSizing available, OracleSize* out) noexcept {
        auto& c = *static_cast<Context*>(user);
        try {
            if (c.size_measure) {
                *out = {known.width.present ? known.width.value : c.fixed.width,
                        known.height.present ? known.height.value : c.fixed.height};
                return ORACLE_OK;
            }
            // Exact test_measure_function early-return ordering (common/src/lib.rs).
            if (known.width.present && known.height.present) {
                *out = {known.width.value, known.height.value};
                return ORACLE_OK;
            }
            ++c.count;
            auto result = c.fixed;
            if (c.text) {
                // Horizontal AhemTextMeasureData::measure; byte lengths, ZWS only
                // (ordinary spaces are not breaking opportunities upstream).
                constexpr std::string_view zws = "\xE2\x80\x8B";
                std::vector<std::size_t> lines;
                std::size_t begin = 0;
                for (;;) {
                    const auto end = c.content.find(zws, begin);
                    if (end == std::string::npos) { lines.push_back(c.content.size() - begin); break; }
                    lines.push_back(end - begin);
                    begin = end + zws.size();
                }
                const auto min_length = *std::max_element(lines.begin(), lines.end());
                std::size_t max_length = 0;
                for (auto length : lines) max_length += length;
                float width;
                if (known.width.present) width = known.width.value;
                else if (available.width.tag == 3) width = float(min_length) * 10;
                else if (available.width.tag == 4) width = float(max_length) * 10;
                else if (available.width.tag == 1) width = std::fmin(available.width.value, float(max_length) * 10);
                else throw std::runtime_error("invalid Ahem available width");
                width = std::fmax(width, float(min_length) * 10);
                float height;
                if (known.height.present) height = known.height.value;
                else {
                    const float f = std::floor(width / 10);
                    const auto capacity = !(f > 0) ? std::size_t(0) :
                        f >= std::ldexp(1.0f, std::numeric_limits<std::size_t>::digits) ?
                        std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(f);
                    std::size_t line_count = 1, current = 0;
                    for (auto length : lines) {
                        if (current + length > capacity) {
                            if (current > 0) ++line_count;
                            current = length;
                        } else current += length;
                    }
                    height = float(line_count) * 10;
                }
                result = {width, height};
            }
            *out = {known.width.present ? known.width.value : result.width,
                    known.height.present ? known.height.value : result.height};
            return ORACLE_OK;
        } catch (...) {
            c.owner->callback_exception = std::current_exception();
            return ORACLE_CALLBACK_FAILURE;
        }
    }
    static std::uint32_t measure(void* user, Node, const TsInput* input, const void* style, TsOutput* out) noexcept {
        auto& c = *static_cast<Context*>(user);
        try { return ts_leaf(style, input, user, intrinsic, out); }
        catch (...) {
            c.owner->callback_exception = std::current_exception();
            return ORACLE_CALLBACK_FAILURE;
        }
    }
    Node leaf(const Style& style = {}) {
        Properties p(style); Node node{};
        ok(ts_new_leaf(native, p.values.data(), p.values.size(), &node)); return node;
    }
    std::uint32_t leaf_result(const Style& style, Node& node) {
        Properties p(style); return ts_new_leaf(native, p.values.data(), p.values.size(), &node);
    }
    Node context_leaf(const Style& style, Context context) {
        context.owner = this;
        auto owned = std::make_unique<Context>(std::move(context));
        Properties p(style); Node node{};
        ok(ts_new_leaf_with_context(native, p.values.data(), p.values.size(), owned.get(), measure, drop, &node));
        owned.release(); return node;
    }
    Node fixed_leaf(const Style& style, float width, float height, bool size_measure = false) {
        Context c; c.fixed = {width, height}; c.size_measure = size_measure;
        return context_leaf(style, std::move(c));
    }
    Node text_leaf(const Style& style = {}, std::string text = "HH\xE2\x80\x8BHH\xE2\x80\x8BHH\xE2\x80\x8BHH") {
        Context c; c.text = true; c.content = std::move(text); return context_leaf(style, std::move(c));
    }
    void set_fixed_context(Node node, float width, float height) {
        auto c = std::make_unique<Context>(); c->owner = this; c->fixed = {width,height}; c->size_measure = true;
        ok(ts_set_context(native, node, c.get(), measure, drop)); c.release();
    }
    std::size_t count(Node node) {
        std::uint32_t present{}; void* user{}; std::uint64_t calls{};
        ok(ts_get_context(native, node, &present, &user, &calls));
        if (!present || !user) throw std::runtime_error("missing native node context");
        return static_cast<Context*>(user)->count;
    }
    Node branch(const Style& style, std::initializer_list<Node> children) {
        Properties p(style); Node node{};
        ok(ts_new_with_children(native, p.values.data(), p.values.size(), children.begin(), children.size(), &node)); return node;
    }
    void add(Node parent, Node child) { ok(ts_add_child(native, parent, child)); }
    void insert(Node parent, std::size_t index, Node child) { ok(ts_insert_child(native, parent, index, child)); }
    void set_children(Node parent, std::initializer_list<Node> children) {
        ok(ts_set_children(native, parent, children.begin(), children.size()));
    }
    void remove(Node node) { Node removed{}; ok(ts_remove(native, node, &removed)); }
    void remove_child(Node parent, Node child) { Node removed{}; ok(ts_remove_child(native,parent,child,&removed)); }
    void remove_at(Node parent, std::size_t index) { Node removed{}; ok(ts_remove_child_at(native,parent,index,&removed)); }
    void replace(Node parent, std::size_t index, Node child) { Node old{}; ok(ts_replace_child(native,parent,index,child,&old)); }
    std::size_t child_count(Node node) { std::size_t count{}; ok(ts_child_count(native,node,&count)); return count; }
    Node child(Node node, std::size_t index) { Node child{}; ok(ts_child(native,node,index,&child)); return child; }
    std::vector<Node> children(Node node) {
        std::size_t count{}; ok(ts_children(native,node,nullptr,0,&count));
        std::vector<Node> result(count); ok(ts_children(native,node,result.data(),result.size(),&count)); return result;
    }
    Node parent(Node node) { Node p{}; ok(ts_parent(native,node,&p)); return p; }
    void style(Node node, const Style& style) { Properties p(style); ok(ts_set_style(native,node,p.values.data(),p.values.size())); }
    bool style_equals(Node node, const Style& style) {
        Properties p(style); std::uint32_t equal{}; ok(ts_style_equals(native,node,p.values.data(),p.values.size(),&equal)); return equal;
    }
    std::uint32_t display(Node node) { std::uint32_t value{}; ok(ts_style_display(native,node,&value)); return value; }
    std::uint32_t compute_result(Node node, OracleSizing available = max_content, bool measured = false) {
        // The upstream function also runs on leaves without a node context.
        // Its no-context intrinsic result is known_dimensions.unwrap_or(ZERO).
        Context fallback; fallback.owner = this; fallback.size_measure = true;
        const auto status = measured ?
            ts_compute_with_measure(native,node,available,&fallback,measure) :
            ts_compute(native,node,available,0);
        if (callback_exception) { auto e = std::exchange(callback_exception, {}); std::rethrow_exception(e); }
        return status;
    }
    void compute(Node node, OracleSizing available = max_content, bool measured = false) { ok(compute_result(node,available,measured)); }
    Layout layout(Node node, bool unrounded = false) { Layout l{}; ok(ts_layout(native,node,unrounded,&l)); return l; }
    bool dirty(Node node) { std::uint32_t value{}; ok(ts_dirty(native,node,&value)); return value; }
    void mark_dirty(Node node) { ok(ts_mark_dirty(native,node)); }
    void print(Node node) { ok(ts_print_tree(native,node)); }
};
} // namespace tree_suite
using namespace tree_suite;

TEST_CASE("tree::taffy_tree::tests::new_should_allocate_default_capacity", "[tree-suite][source-unit]") {
    Tree t; std::size_t nodes{}, children{}, parents{};
    ok(ts_capacities(t.native,&nodes,&children,&parents));
    REQUIRE(children >= 16); REQUIRE(parents >= 16); REQUIRE(nodes >= 16);
}
TEST_CASE("tree::taffy_tree::tests::test_with_capacity", "[tree-suite][source-unit]") {
    Tree t(8); std::size_t nodes{}, children{}, parents{};
    ok(ts_capacities(t.native,&nodes,&children,&parents));
    REQUIRE(children >= 8); REQUIRE(parents >= 8); REQUIRE(nodes >= 8);
}
TEST_CASE("tree::taffy_tree::tests::test_new_leaf", "[tree-suite]") {
    Tree t; Node node{}; const auto res=t.leaf_result({},node);
    REQUIRE(res == ORACLE_OK); REQUIRE(t.child_count(node) == 0);
}
TEST_CASE("tree::taffy_tree::tests::new_leaf_with_context", "[tree-suite]") {
    Tree t; Context c; c.owner=&t; c.size_measure=true;
    auto owned=std::make_unique<Context>(std::move(c)); Node node{};
    const auto res=ts_new_leaf_with_context(t.native,nullptr,0,owned.get(),Tree::measure,Tree::drop,&node);
    if (res == ORACLE_OK) owned.release();
    REQUIRE(res == ORACLE_OK); REQUIRE(t.child_count(node) == 0);
}
TEST_CASE("tree::taffy_tree::tests::test_new_with_children", "[tree-suite]") {
    Tree t; auto c0=t.leaf(), c1=t.leaf(); auto n=t.branch({}, {c0,c1});
    REQUIRE(t.child_count(n)==2); REQUIRE(t.children(n)[0]==c0); REQUIRE(t.children(n)[1]==c1);
}
TEST_CASE("tree::taffy_tree::tests::remove_node_should_remove", "[tree-suite]") {
    Tree t; auto n=t.leaf();
    REQUIRE_NOTHROW(t.remove(n)); // upstream unwrap must complete successfully
}
TEST_CASE("tree::taffy_tree::tests::remove_node_should_detach_hierarchy", "[tree-suite]") {
    Tree t; auto n2=t.leaf(); auto n1=t.branch({}, {n2}); auto n0=t.branch({}, {n1});
    REQUIRE(t.children(n0)==std::vector<Node>{n1}); REQUIRE(t.children(n1)==std::vector<Node>{n2});
    t.remove(n1); REQUIRE(t.children(n0).empty()); REQUIRE(t.children(n2).empty());
}
TEST_CASE("tree::taffy_tree::tests::remove_last_node", "[tree-suite]") {
    Tree t; auto p=t.leaf(), c=t.leaf(); t.add(p,c);
    REQUIRE_NOTHROW(t.remove(c)); REQUIRE_NOTHROW(t.remove(p));
}
TEST_CASE("tree::taffy_tree::tests::remove_node_marks_parent_dirty", "[tree-suite]") {
    Tree t; auto child=t.leaf({{"border-bottom","1px"}}); auto root=t.branch({}, {child});
    t.compute(root,min_content); REQUIRE(t.layout(root).size.height==1);
    t.remove(child); REQUIRE(t.dirty(root));
    t.compute(root,min_content); REQUIRE(t.layout(root).size.height==0);
}
TEST_CASE("tree::taffy_tree::tests::set_measure", "[tree-suite]") {
    Tree t; auto n=t.fixed_leaf({},200,200,true);
    t.compute(n,max_content,true); REQUIRE(t.layout(n).size.width==200);
    t.set_fixed_context(n,100,100); t.compute(n,max_content,true); REQUIRE(t.layout(n).size.width==100);
}
TEST_CASE("tree::taffy_tree::tests::set_measure_of_previously_unmeasured_node", "[tree-suite]") {
    Tree t; auto n=t.leaf(); t.compute(n,max_content,true); REQUIRE(t.layout(n).size.width==0);
    t.set_fixed_context(n,100,100); t.compute(n,max_content,true); REQUIRE(t.layout(n).size.width==100);
}
TEST_CASE("tree::taffy_tree::tests::add_child", "[tree-suite]") {
    Tree t; auto n=t.leaf(); REQUIRE(t.child_count(n)==0);
    auto c0=t.leaf(); t.add(n,c0); REQUIRE(t.child_count(n)==1);
    auto c1=t.leaf(); t.add(n,c1); REQUIRE(t.child_count(n)==2);
}
TEST_CASE("tree::taffy_tree::tests::insert_child_at_index", "[tree-suite]") {
    Tree t; auto c0=t.leaf(), c1=t.leaf(), c2=t.leaf(); auto n=t.leaf(); REQUIRE(t.child_count(n)==0);
    t.insert(n,0,c0); REQUIRE(t.child_count(n)==1); REQUIRE(t.children(n)[0]==c0);
    t.insert(n,0,c1); REQUIRE(t.child_count(n)==2); REQUIRE(t.children(n)[0]==c1); REQUIRE(t.children(n)[1]==c0);
    t.insert(n,1,c2); REQUIRE(t.child_count(n)==3); REQUIRE(t.children(n)[0]==c1); REQUIRE(t.children(n)[1]==c2); REQUIRE(t.children(n)[2]==c0);
}
TEST_CASE("tree::taffy_tree::tests::set_children", "[tree-suite]") {
    Tree t; auto c0=t.leaf(), c1=t.leaf(); auto n=t.branch({}, {c0,c1});
    REQUIRE(t.child_count(n)==2); REQUIRE(t.children(n)[0]==c0); REQUIRE(t.children(n)[1]==c1);
    auto c2=t.leaf(), c3=t.leaf(); t.set_children(n,{c2,c3});
    REQUIRE(t.child_count(n)==2); REQUIRE(t.children(n)[0]==c2); REQUIRE(t.children(n)[1]==c3);
}
TEST_CASE("tree::taffy_tree::tests::remove_child", "[tree-suite]") {
    Tree t; auto c0=t.leaf(), c1=t.leaf(); auto n=t.branch({}, {c0,c1}); REQUIRE(t.child_count(n)==2);
    t.remove_child(n,c0); REQUIRE(t.child_count(n)==1); REQUIRE(t.children(n)[0]==c1);
    t.remove_child(n,c1); REQUIRE(t.child_count(n)==0);
}
TEST_CASE("tree::taffy_tree::tests::remove_child_at_index", "[tree-suite]") {
    Tree t; auto c0=t.leaf(), c1=t.leaf(); auto n=t.branch({}, {c0,c1}); REQUIRE(t.child_count(n)==2);
    t.remove_at(n,0); REQUIRE(t.child_count(n)==1); REQUIRE(t.children(n)[0]==c1);
    t.remove_at(n,0); REQUIRE(t.child_count(n)==0);
}
TEST_CASE("tree::taffy_tree::tests::remove_children_range", "[tree-suite]") {
    Tree t; auto c0=t.leaf(), c1=t.leaf(), c2=t.leaf(), c3=t.leaf(); auto n=t.branch({}, {c0,c1,c2,c3});
    REQUIRE(t.child_count(n)==4); ok(ts_remove_children_range(t.native,n,1,1,1,2));
    REQUIRE(t.child_count(n)==2); REQUIRE(t.children(n)==std::vector<Node>{c0,c3});
    for (auto c : {c0,c3}) REQUIRE(t.parent(c)==n);
    for (auto c : {c1,c2}) REQUIRE(t.parent(c)==0);
}
TEST_CASE("tree::taffy_tree::tests::remove_child_updates_parents", "[tree-suite]") {
    Tree t; auto p=t.leaf(), c=t.leaf(); t.add(p,c); t.remove(p);
    REQUIRE(ts_set_children(t.native,c,nullptr,0)==ORACLE_OK);
}
TEST_CASE("tree::taffy_tree::tests::replace_child_at_index", "[tree-suite]") {
    Tree t; auto c0=t.leaf(), c1=t.leaf(); auto n=t.branch({}, {c0});
    REQUIRE(t.child_count(n)==1); REQUIRE(t.children(n)[0]==c0);
    t.replace(n,0,c1); REQUIRE(t.child_count(n)==1); REQUIRE(t.children(n)[0]==c1);
}
TEST_CASE("tree::taffy_tree::tests::test_child_at_index", "[tree-suite]") {
    Tree t; auto c0=t.leaf(), c1=t.leaf(), c2=t.leaf(); auto n=t.branch({}, {c0,c1,c2});
    Node result{}; auto res=ts_child(t.native,n,0,&result); REQUIRE((res==ORACLE_OK && result==c0));
    res=ts_child(t.native,n,1,&result); REQUIRE((res==ORACLE_OK && result==c1));
    res=ts_child(t.native,n,2,&result); REQUIRE((res==ORACLE_OK && result==c2));
}
TEST_CASE("tree::taffy_tree::tests::test_child_count", "[tree-suite]") {
    Tree t; auto c0=t.leaf(), c1=t.leaf(); auto n=t.branch({}, {c0,c1});
    REQUIRE(t.child_count(n)==2); REQUIRE(t.child_count(c0)==0); REQUIRE(t.child_count(c1)==0);
}
TEST_CASE("tree::taffy_tree::tests::test_children", "[tree-suite]") {
    Tree t; auto c0=t.leaf(), c1=t.leaf(); auto n=t.branch({}, {c0,c1});
    std::vector<Node> children; children.push_back(c0); children.push_back(c1);
    auto result=t.children(n); REQUIRE(result==children); REQUIRE(t.children(c0).empty());
}
TEST_CASE("tree::taffy_tree::tests::test_set_style", "[tree-suite]") {
    Tree t; auto n=t.leaf(); REQUIRE(t.display(n)==0);
    t.style(n,{{"display","none"}}); REQUIRE(t.display(n)==3);
}
TEST_CASE("tree::taffy_tree::tests::test_style", "[tree-suite][source-unit]") {
    Tree t; Style s{{"display","none"},{"flex-direction","row-reverse"}}; auto n=t.leaf(s);
    Properties p(s); std::uint32_t equal{};
    const auto res=ts_style_equals(t.native,n,p.values.data(),p.values.size(),&equal);
    REQUIRE(res==ORACLE_OK); REQUIRE(equal==1);
}
TEST_CASE("tree::taffy_tree::tests::test_layout", "[tree-suite]") {
    Tree t; auto n=t.leaf(); Layout l{}; REQUIRE(ts_layout(t.native,n,0,&l)==ORACLE_OK);
}
TEST_CASE("tree::taffy_tree::tests::test_mark_dirty", "[tree-suite]") {
    Tree t; auto c0=t.leaf(), c1=t.leaf(); auto n=t.branch({}, {c0,c1}); t.compute(n);
    REQUIRE_FALSE(t.dirty(c0)); REQUIRE_FALSE(t.dirty(c1)); REQUIRE_FALSE(t.dirty(n));
    t.mark_dirty(n); REQUIRE_FALSE(t.dirty(c0)); REQUIRE_FALSE(t.dirty(c1)); REQUIRE(t.dirty(n));
    t.compute(n); t.mark_dirty(c0); REQUIRE(t.dirty(c0)); REQUIRE_FALSE(t.dirty(c1)); REQUIRE(t.dirty(n));
}
TEST_CASE("tree::taffy_tree::tests::compute_layout_should_produce_valid_result", "[tree-suite]") {
    Tree t; Node n{}; auto res=t.leaf_result({{"width","10px"},{"height","10px"}},n); REQUIRE(res==ORACLE_OK);
    REQUIRE(t.compute_result(n,definite(100,100))==ORACLE_OK);
}
TEST_CASE("tree::taffy_tree::tests::make_sure_layout_location_is_top_left", "[tree-suite]") {
    Tree t; auto n=t.leaf({{"width","100%"},{"height","100%"}});
    auto root=t.branch({{"width","100px"},{"height","100px"},{"padding-left","10px"},
        {"padding-right","20px"},{"padding-top","30px"},{"padding-bottom","40px"}}, {n});
    t.compute(root); auto l=t.layout(n); REQUIRE(l.x==10); REQUIRE(l.y==30);
}
TEST_CASE("tree::taffy_tree::tests::set_children_reparents", "[tree-suite]") {
    Tree t; auto child=t.leaf(); auto old=t.branch({}, {child}); auto next=t.leaf();
    t.set_children(next,{child}); REQUIRE(t.children(old).empty());
}
TEST_CASE("tree::taffy_tree::tests::remove_drops_the_nodes_context", "[tree-suite]") {
    std::atomic_size_t drops{0}; Tree t; Context c; c.drops=&drops;
    auto n=t.context_leaf({},std::move(c)); t.remove(n);
    INFO("removing a node must drop its context rather than strand it in node_context_data");
    REQUIRE(drops.load(std::memory_order_seq_cst)==1);
}
TEST_CASE("tree::taffy_tree::tests::clear_drops_every_nodes_context", "[tree-suite]") {
    std::atomic_size_t drops{0}; Tree t;
    for (int i=0;i<8;++i) { Context c; c.drops=&drops; t.context_leaf({},std::move(c)); }
    ok(ts_clear(t.native));
    INFO("clear() drops all nodes, so it must drop their contexts too");
    REQUIRE(drops.load(std::memory_order_seq_cst)==8);
}

TEST_CASE("hand_written::caching::caching::measure_count_grid", "[tree-suite][caching]") {
    Tree t; auto leaf=t.fixed_leaf({{"display","grid"}},50,50); auto n=t.branch({}, {leaf});
    for (int i=0;i<100;++i) n=t.branch({}, {n});
    t.compute(n,max_content,true); REQUIRE(t.count(leaf)==6);
}
TEST_CASE("hand_written::caching::caching::measure_count_nested_grid", "[tree-suite][caching]") {
    Tree t; auto leaf=t.text_leaf(); auto inner=t.branch({{"display","grid"}}, {leaf});
    auto middle=t.branch({{"display","grid"}}, {inner});
    auto outer=t.branch({{"display","grid"},{"width","50px"},{"height","auto"}}, {middle});
    t.compute(outer,max_content,true); REQUIRE(same_size(t.layout(leaf).size,{50,20})); REQUIRE(t.count(leaf)==6);
}
TEST_CASE("hand_written::caching::caching::measure_count_grid_item_with_known_width", "[tree-suite][caching]") {
    Tree t; auto leaf=t.text_leaf({{"width","40px"},{"height","auto"}}); auto grid=t.branch({{"display","grid"}}, {leaf});
    t.compute(grid,max_content,true); REQUIRE(same_size(t.layout(grid).size,{40,20}));
    REQUIRE(same_size(t.layout(leaf).size,{40,20})); REQUIRE(t.count(leaf)==3);
}
TEST_CASE("hand_written::caching::caching::measure_count_grid_item_in_fixed_track", "[tree-suite][caching]") {
    Tree t; auto leaf=t.text_leaf({{"justify-self","start"},{"grid-column-start","1"}});
    auto sibling=t.leaf({{"width","50px"},{"height","50px"},{"grid-column-start","2"}});
    auto grid=t.branch({{"display","grid"},{"grid-template-columns","100px auto"}}, {leaf,sibling});
    t.compute(grid,max_content,true); REQUIRE(same_size(t.layout(grid).size,{150,50}));
    REQUIRE(same_size(t.layout(leaf).size,{80,50})); REQUIRE(t.count(leaf)==3);
}
TEST_CASE("hand_written::caching::caching::measure_count_flex_in_grid", "[tree-suite][caching]") {
    for (const auto& [direction,expected] : std::array<std::pair<const char*,int>,2>{{{"row",9},{"column",10}}}) {
        Tree t; auto leaf=t.text_leaf(); auto flex=t.branch({{"display","flex"},{"flex-direction",direction}}, {leaf});
        auto grid=t.branch({{"display","grid"},{"width","50px"},{"height","auto"}}, {flex});
        t.compute(grid,max_content,true); auto count=t.count(leaf);
        REQUIRE(same_size(t.layout(leaf).size,{50,20})); REQUIRE(count==expected);
    }
}
TEST_CASE("hand_written::caching::caching::grid_block_item_height_not_polluted_by_width_measure", "[tree-suite][caching]") {
    Tree t;
    auto make_item=[&](int row) {
        auto leaf=t.fixed_leaf({},100,19);
        return t.branch({{"display","block"},{"grid-row-start",std::to_string(row)},
            {"grid-row-end",std::to_string(row+1)},{"grid-column-start","1"},{"grid-column-end","2"}}, {leaf});
    };
    auto a=make_item(1), b=make_item(2), c=make_item(3);
    auto container=t.branch({{"display","grid"},{"grid-template-columns","60% auto"},{"width","1000px"},{"height","auto"}}, {a,b,c});
    t.compute(container,definite(1000,1000),true); int index=0;
    for (auto n : {a,b,c}) { auto l=t.layout(n); REQUIRE(l.size.height==19); REQUIRE(l.y==19*index); ++index; }
}
TEST_CASE("hand_written::caching::caching::grid_item_spanning_only_unaffected_tracks_is_not_measured", "[tree-suite][caching]") {
    Tree t; auto leaf=t.text_leaf({{"grid-column-start","2"},{"grid-column-end","3"}},"HH HH HH HH");
    auto grid=t.branch({{"display","grid"},{"width","400px"},{"height","50px"},
        {"grid-template-columns","100px minmax(0px, 1fr)"},{"grid-template-rows","50px"}}, {leaf});
    t.compute(grid,max_content,true); REQUIRE(t.layout(leaf).size.width==300); REQUIRE(t.count(leaf)==1);
}

TEST_CASE("hand_written::relayout::relayout", "[tree-suite][relayout]") {
    Tree t; auto n1=t.leaf({{"width","8px"},{"height","80px"}});
    auto n0=t.branch({{"align-self","center"},{"width","auto"},{"height","auto"}}, {n1});
    auto n=t.branch({{"width","100%"},{"height","100%"}}, {n0});
    t.compute(n,definite(100,100));
    const auto initial=t.layout(n), initial0=t.layout(n0), initial1=t.layout(n1);
    for (int i=1;i<10;++i) {
        t.compute(n,definite(100,100));
        auto l=t.layout(n); REQUIRE((l.x==initial.x && l.y==initial.y));
        l=t.layout(n0); REQUIRE((l.x==initial0.x && l.y==initial0.y));
        l=t.layout(n1); REQUIRE((l.x==initial1.x && l.y==initial1.y));
    }
}

namespace tree_suite {
// Each call below emits the original four scalar assertions in original order.
void check_box(const Layout& l, float x, float y, float width, float height) {
    REQUIRE(l.x==x); REQUIRE(l.y==y); REQUIRE(l.size.width==width); REQUIRE(l.size.height==height);
}
void toggle_display(const char* display, bool child_case, bool container_case) {
    Style hidden{{"display","none"},{"width","100px"},{"height","100px"}};
    Style shown{{"display",display},{"width","100px"},{"height","100px"}};
    Tree t; auto node=t.leaf(hidden);
    auto root=child_case ? t.branch(shown,{node}) : container_case ? t.branch(hidden,{node}) : node;
    auto target=container_case ? root : node;
    t.compute(root); check_box(t.layout(target),0,0,0,0);
    t.style(target,shown); t.compute(root); check_box(t.layout(target),0,0,100,100);
    t.style(target,hidden); t.compute(root); check_box(t.layout(target),0,0,0,0);
}
}
TEST_CASE("hand_written::relayout::toggle_root_display_none", "[tree-suite][relayout]") {
    toggle_display("flex",false,false);
}
TEST_CASE("hand_written::relayout::toggle_root_display_none_with_children", "[tree-suite][relayout]") {
    Tree t; auto child=t.leaf({{"width","800px"},{"height","100px"}});
    auto parent=t.branch({{"width","800px"},{"height","100px"}}, {child});
    auto root=t.branch({}, {parent}); t.compute(root);
    REQUIRE(t.layout(child).size.width==800); REQUIRE(t.layout(child).size.height==100);
    t.style(root,{{"display","none"}}); t.compute(root);
    REQUIRE(t.layout(child).size.width==0); REQUIRE(t.layout(child).size.height==0);
    t.style(root,{}); t.compute(root);
    REQUIRE(t.layout(parent).size.width==800); REQUIRE(t.layout(parent).size.height==100);
    REQUIRE(t.layout(child).size.width==800); REQUIRE(t.layout(child).size.height==100);
}
TEST_CASE("hand_written::relayout::toggle_flex_child_display_none", "[tree-suite][relayout]") {
    toggle_display("flex",true,false);
}
TEST_CASE("hand_written::relayout::toggle_flex_container_display_none", "[tree-suite][relayout]") {
    toggle_display("flex",false,true);
}
TEST_CASE("hand_written::relayout::toggle_grid_child_display_none", "[tree-suite][relayout]") {
    toggle_display("grid",true,false);
}
TEST_CASE("hand_written::relayout::toggle_grid_container_display_none", "[tree-suite][relayout]") {
    toggle_display("grid",false,true);
}
TEST_CASE("hand_written::relayout::relayout_is_stable_with_rounding", "[tree-suite][relayout]") {
    Tree t; ok(ts_rounding(t.native,1));
    auto inner=t.leaf({{"min-width","300px"},{"min-height","auto"}});
    auto wrapper=t.branch({{"width","150px"},{"height","auto"},{"justify-content","end"}}, {inner});
    auto outer=t.branch({{"width","100%"},{"height","auto"},{"position","relative"},
        {"left","1.5px"},{"right","auto"},{"top","auto"},{"bottom","auto"}}, {wrapper});
    auto root=t.branch({{"width","1920px"},{"height","1080px"}}, {outer});
    t.compute(root); t.print(root);
    const auto initial_root=t.layout(root); check_box(initial_root,0,0,1920,1080);
    const auto initial_outer=t.layout(outer); check_box(initial_outer,2,0,1920,1080);
    const auto initial_wrapper=t.layout(wrapper); check_box(initial_wrapper,0,0,150,1080);
    const auto initial_inner=t.layout(inner); check_box(initial_inner,-150,0,300,1080);
    for (int i=0;i<5;++i) {
        t.mark_dirty(root); t.compute(root); t.print(root);
        auto l=t.layout(root); check_box(l,initial_root.x,initial_root.y,initial_root.size.width,initial_root.size.height);
        l=t.layout(outer); check_box(l,initial_outer.x,initial_outer.y,initial_outer.size.width,initial_outer.size.height);
        // Preserve upstream's deliberate source spelling: wrapper x is also the y reference.
        l=t.layout(wrapper); check_box(l,initial_wrapper.x,initial_wrapper.x,initial_wrapper.size.width,initial_wrapper.size.height);
        l=t.layout(inner); check_box(l,initial_inner.x,initial_inner.y,initial_inner.size.width,initial_inner.size.height);
    }
}

namespace tree_suite {
using LayoutPair=std::pair<std::vector<Layout>,std::vector<Layout>>;
using Build=std::vector<Node>(*)(Tree&,const Style&);
LayoutPair relayout_and_fresh(const Style& before, const Style& after, Build build) {
    auto layouts=[](Tree& tree,const std::vector<Node>& ids) {
        std::vector<Layout> result; for (auto id : ids) result.push_back(tree.layout(id,true)); return result;
    };
    Tree tree; auto ids=build(tree,before); auto root=ids.back();
    tree.compute(root); tree.style(ids[0],after); tree.compute(root);
    Tree fresh; auto fresh_ids=build(fresh,after); fresh.compute(fresh_ids.back());
    return {layouts(tree,ids),layouts(fresh,fresh_ids)};
}
void relayout_block_parent_as(Style container) {
    Style block{{"display","block"},{"width","100px"},{"height","200px"}};
    container.emplace_back("width","100px"); container.emplace_back("height","200px");
    auto [relaid,fresh]=relayout_and_fresh(block,container,[](Tree& t,const Style& root_style) {
        auto inner=t.leaf({{"display","block"},{"width","auto"},{"height","10px"},
            {"margin-left","0px"},{"margin-right","0px"},{"margin-top","20px"},{"margin-bottom","0px"}});
        auto middle=t.branch({{"display","block"},{"width","auto"},{"height","50px"}}, {inner});
        auto root=t.branch(root_style,{middle}); return std::vector<Node>{root,middle,inner,root};
    });
    { INFO("the item is at the top of its container"); REQUIRE(fresh[1].y==0); }
    { INFO("the item contains its child's margin"); REQUIRE(fresh[2].y==20); }
    REQUIRE(same_layouts(relaid,fresh));
}
}
TEST_CASE("hand_written::relayout::block_child_does_not_keep_collapsed_margin_as_flex_item", "[tree-suite][relayout]") {
    relayout_block_parent_as({{"display","flex"},{"flex-direction","column"}});
}
TEST_CASE("hand_written::relayout::block_child_does_not_keep_collapsed_margin_as_grid_item", "[tree-suite][relayout]") {
    relayout_block_parent_as({{"display","grid"}});
}
TEST_CASE("hand_written::relayout::flex_base_size_is_not_answered_by_an_inherent_size_measurement", "[tree-suite][relayout]") {
    auto container=[](const char* display) { return Style{{"display",display},{"width","100px"},{"height","auto"}}; };
    auto [relaid,fresh]=relayout_and_fresh(container("grid"),container("flex"),[](Tree& t,const Style& root_style) {
        auto first=t.leaf({{"display","grid"},{"flex-grow","1"},{"width","auto"},{"height","20px"},
            {"min-width","40px"},{"min-height","auto"}});
        auto second=t.leaf({{"display","block"},{"flex-grow","1"},{"width","auto"},{"height","20px"}});
        auto root=t.branch(root_style,{first,second}); return std::vector<Node>{root,first,second,root};
    });
    REQUIRE(fresh[1].size.width==50); REQUIRE(fresh[2].size.width==50); REQUIRE(same_layouts(relaid,fresh));
}
TEST_CASE("hand_written::relayout::a_percentage_height_is_not_reused_after_its_containing_block_loses_its_height", "[tree-suite][relayout]") {
    auto block=[](const char* height) { return Style{{"display","block"},{"width","78px"},{"height",height}}; };
    auto [incremental,fresh]=relayout_and_fresh(block("62px"),block("auto"),[](Tree& t,const Style& middle_style) {
        auto leaf=t.leaf({{"display","block"},{"width","auto"},{"height","19px"}});
        auto item=t.branch({{"display","flex"},{"width","31px"},{"height","86%"}}, {leaf});
        auto middle=t.branch(middle_style,{item});
        auto root=t.branch({{"display","flex"},{"flex-direction","column"},{"width","78px"},{"height","auto"}}, {middle});
        return std::vector<Node>{middle,item,leaf,root};
    });
    REQUIRE(fresh[1].size.height==19); REQUIRE(same_layouts(incremental,fresh));
}

#include "ported/tree/cache_tests.inc"
