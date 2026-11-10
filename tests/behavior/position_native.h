// Test-only ABI plumbing; no layout or style algorithms are implemented here.
#pragma once
#include "../support/behavior_suite_oracle.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace position_tests {
using Node = std::uint64_t;
using Style = std::map<std::string, std::string>;
inline std::string px(float n) { return std::to_string(n) + "px"; }
inline BehaviorText text(const std::string& s) { return {s.data(), s.size()}; }
inline Style leaf(float w, float h) { return {{"width", px(w)}, {"height", px(h)}}; }
inline Style floating(float w, float h, const char* side) {
    auto s = leaf(w,h); s["display"]="block"; s["float"]=side; return s;
}
inline Style block_root(float w) { return {{"display","block"},{"width",px(w)},{"height","auto"}}; }
inline Style positioned(float w,float h,const char* position,float x,float y) {
    auto s=leaf(w,h); s["position"]=position; s["left"]=px(x); s["top"]=px(y);
    s["right"]="auto"; s["bottom"]="auto"; return s;
}
inline void offset_margin(Style& s,float x,float y) {
    s["margin-left"]=px(x); s["margin-top"]=px(y);
    s["margin-right"]="auto"; s["margin-bottom"]="auto";
}
class Tree {
    BehaviorOracle* tree_{};
    static std::vector<BehaviorProperty> properties(const Style& s) {
        std::vector<BehaviorProperty> p;
        for (const auto& [k,v]:s) p.push_back({text(k),text(v)});
        return p;
    }
    void ok(std::uint32_t status) const {
        if(status!=BEHAVIOR_OK) {
            char message[4096]{}; std::size_t needed{};
            behavior_oracle_error(tree_,message,sizeof(message),&needed);
            INFO("native behavior ABI status=" << status << "; " << message);
            REQUIRE(status==BEHAVIOR_OK);
        }
    }
public:
    Tree() { ok(behavior_oracle_create(&tree_)); }
    ~Tree() { behavior_oracle_free(tree_); }
    Tree(const Tree&)=delete; Tree& operator=(const Tree&)=delete;
    Node node(const Style& s={},std::initializer_list<Node> children={}) {
        auto p=properties(s); Node n{};
        ok(behavior_oracle_new_node(tree_,p.data(),p.size(),&n));
        if(children.size()) ok(behavior_oracle_set_children(tree_,n,children.begin(),children.size()));
        return n;
    }
    Node node(const Style& s,const std::vector<Node>& children) {
        const auto n=node(s);
        ok(behavior_oracle_set_children(tree_,n,children.data(),children.size())); return n;
    }
    void style(Node n,const Style& s) {
        auto p=properties(s); ok(behavior_oracle_set_style(tree_,n,p.data(),p.size()));
    }
    // Retain upstream tree.style(node).unwrap().clone() snapshots exactly.
    std::string read_style(Node n) const {
        std::size_t count{};
        ok(behavior_oracle_style_json(tree_,n,nullptr,0,&count));
        REQUIRE(count>1);
        std::vector<char> out(count); std::size_t actual{};
        ok(behavior_oracle_style_json(tree_,n,out.data(),out.size(),&actual));
        REQUIRE(actual==count);
        return std::string(out.data(),count-1);
    }
    void snapshot_left(Node n, const std::string& snapshot, float value) {
        ok(behavior_oracle_set_style_json_inset(tree_, n, text(snapshot), 0, 1, value));
    }
    // Upstream compute_layout uses MAX_CONTENT with default rounding enabled.
    // Null measure is explicitly zero intrinsic measurement; these fixtures have no contexts.
    void compute(Node root) { ok(behavior_oracle_compute(tree_,root,{{4,0},{4,0}},1,nullptr,nullptr)); }
    void dirty(Node n) { ok(behavior_oracle_mark_dirty(tree_,n)); }
    BehaviorLayout layout(Node n) const {
        BehaviorLayout out{}; ok(behavior_oracle_layout(tree_,n,&out)); return out;
    }
    BehaviorLayout unrounded(Node n) const {
        BehaviorLayout out{}; ok(behavior_oracle_unrounded_layout(tree_,n,&out)); return out;
    }
    std::vector<Node> hoisted(Node n) const {
        // Capacity is deliberately bounded well above the largest owned fixture (five).
        // Required length is checked explicitly; an oversized result cannot be truncated.
        std::vector<Node> out(64); std::size_t count{};
        ok(behavior_oracle_hoisted_children(tree_,n,out.data(),out.size(),&count));
        REQUIRE(count<=out.size()); out.resize(count); return out;
    }
};
inline void location(const BehaviorLayout& l,float x,float y) {
    REQUIRE(l.x==x); REQUIRE(l.y==y);
}
inline void size(const BehaviorLayout& l,float w,float h) {
    REQUIRE(l.size.width==w); REQUIRE(l.size.height==h);
}
} // namespace position_tests
