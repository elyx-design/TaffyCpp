// C++ fixture/assertion translations of the five upstream hand-written suites.
// Native ABI is a reference engine, NOT a C++ production implementation.
#include "../support/behavior_suite_oracle.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstring>
#include <limits>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using Props = std::initializer_list<std::pair<const char*, const char*>>;
BehaviorText text(std::string_view s) { return {s.data(), s.size()}; }
constexpr BehaviorAvailable max_content{{4,0},{4,0}};
constexpr BehaviorAvailable definite100{{1,100},{1,100}};

// Extract a native JSON member without changing its representation (not a
// replacement Style serializer). Handles nested arrays/objects and strings.
std::pair<std::size_t,std::size_t> member_range(const std::string& json, std::string_view name) {
    auto key = json.find("\"" + std::string(name) + "\"");
    REQUIRE(key != std::string::npos);
    auto start = json.find(':', key) + 1;
    while (json[start] == ' ') ++start;
    auto end = start; int depth = 0; bool quoted = false, escape = false;
    for (; end < json.size(); ++end) {
        char c = json[end];
        if (quoted) { if (escape) escape=false; else if(c=='\\') escape=true; else if(c=='"') quoted=false; continue; }
        if(c=='"') quoted=true;
        else if(c=='[' || c=='{') ++depth;
        else if(c==']' || c=='}') { if(depth==0) break; --depth; }
        else if(c==',' && depth==0) break;
    }
    return {start,end};
}
std::string member(const std::string& json, std::string_view name) {
    auto [a,b]=member_range(json,name); return json.substr(a,b-a);
}
void replace_member(std::string& json, std::string_view name, std::string_view value) {
    auto [a,b]=member_range(json,name); json.replace(a,b-a,value);
}
struct Image { float width, ratio; std::size_t count=0; };
struct Tree {
    BehaviorOracle* native=nullptr;
    std::map<uint64_t,Image> images;
    Tree() { REQUIRE(behavior_oracle_create(&native)==BEHAVIOR_OK); }
    ~Tree() { behavior_oracle_free(native); }
    Tree(const Tree&)=delete;
    std::string error() {
        size_t n=0; behavior_oracle_error(native,nullptr,0,&n);
        std::vector<char> out(n+1); behavior_oracle_error(native,out.data(),out.size(),&n);
        return std::string(out.data(),n);
    }
    void ok(uint32_t status) { INFO(error()); REQUIRE(status==BEHAVIOR_OK); }
    uint64_t node(Props props={}) {
        std::vector<BehaviorProperty> values;
        for(auto [k,v]:props) values.push_back({text(k),text(v)});
        uint64_t id=0; ok(behavior_oracle_new_node(native,values.data(),values.size(),&id)); return id;
    }
    uint64_t measured(Props props,float width,float ratio) {
        auto id=node(props); images.emplace(id,Image{width,ratio}); return id;
    }
    uint64_t parent(Props props,std::initializer_list<uint64_t> children) {
        auto id=node(props); ok(behavior_oracle_set_children(native,id,children.begin(),children.size())); return id;
    }
    std::string style(uint64_t id) {
        size_t n=0; ok(behavior_oracle_style_json(native,id,nullptr,0,&n));
        std::vector<char> out(n+1); ok(behavior_oracle_style_json(native,id,out.data(),out.size(),&n));
        return std::string(out.data(),n);
    }
    void field(uint64_t id,std::string_view key,std::string_view value) {
        ok(behavior_oracle_set_native_field(native,id,text(key),text(value)));
    }
    uint32_t compute_status(uint64_t root,BehaviorAvailable available=max_content) {
        auto callback=[this](uint64_t id,BehaviorKnown known,BehaviorAvailable,BehaviorText full_style,BehaviorSize* out)->uint32_t {
            // Native Taffy owns full Style leaf sizing. This is only the original
            // test_measure_function's intrinsic aspect-ratio/None branch.
            REQUIRE(full_style.data != nullptr);
            REQUIRE(full_style.len > 0);
            auto it=images.find(id);
            if(known.width.present && known.height.present) {
                *out={known.width.value,known.height.value}; return BEHAVIOR_OK;
            }
            if(it==images.end()) {
                *out={known.width.present?known.width.value:0,known.height.present?known.height.value:0};
            } else {
                ++it->second.count;
                const float w=known.width.present?known.width.value:it->second.width;
                *out={w,known.height.present?known.height.value:w*it->second.ratio};
            }
            return BEHAVIOR_OK;
        };
        return behavior_oracle_compute(native,root,available,1,&callback,behavior_measure_guard<decltype(callback)>);
    }
    void compute(uint64_t root,BehaviorAvailable available=max_content) { ok(compute_status(root,available)); }
    BehaviorLayout layout(uint64_t id) { BehaviorLayout l{}; ok(behavior_oracle_layout(native,id,&l)); return l; }
};
void size(BehaviorLayout l,float w,float h) { REQUIRE(l.size.width==w); REQUIRE(l.size.height==h); }
void location(BehaviorLayout l,float x,float y) { REQUIRE(l.x==x); REQUIRE(l.y==y); }
BehaviorLayout absolute(Props props) {
    Tree t; auto c=t.measured(props,100,0.5f);
    auto r=t.parent({{"display","block"},{"position","relative"},{"width","400px"},{"height","300px"}},{c});
    t.compute(r); return t.layout(c);
}
BehaviorLayout badge(Props props,const char* width="600px") {
    Tree t; auto c=t.measured(props,142,20.0f/142.0f);
    auto r=t.parent({{"display","block"},{"width",width}},{c}); t.compute(r); return t.layout(c);
}
BehaviorLayout input(Props props,bool positioned=false) {
    Tree t; auto c=t.measured(props,150,20.0f/150.0f);
    auto r=positioned ? t.parent({{"display","block"},{"position","relative"},{"width","400px"},{"height","300px"}},{c})
                      : t.parent({{"display","block"},{"width","400px"}},{c});
    t.compute(r); return t.layout(c);
}
float grid_input(bool replaced,bool compressible) {
    Tree t; auto c=t.measured({{"item-is-replaced",replaced?"true":"false"},
        {"item-is-compressible-replaced",compressible?"true":"false"},
        {"width","100px"},{"height","20px"},{"min-width","0px"},{"max-width","100%"}},150,20.0f/150.0f);
    auto r=t.parent({{"display","grid"},{"width","0px"},{"grid-template-columns","1fr"}},{c});
    t.compute(r); return t.layout(c).size.width;
}
// CSS repeat parsing normally supplies empty line-name sets. Upstream's repeat
// helper supplies NO sets: preserve that representation explicitly.
void repeat_without_names(Tree& t,uint64_t root) {
    auto columns=member(t.style(root),"grid_template_columns");
    replace_member(columns,"line_names","[]"); t.field(root,"grid_template_columns",columns);
}
}

TEST_CASE("absolute_replaced::auto_size_uses_intrinsic_size_despite_insets", "[behavior][replaced]") {
    auto l=absolute({{"position","absolute"},{"item-is-replaced","true"},{"left","10px"},{"right","10px"},{"top","10px"},{"bottom","10px"}});
    size(l,100,50); location(l,10,10);
}
TEST_CASE("absolute_replaced::auto_height_uses_aspect_ratio_despite_insets", "[behavior][replaced]") {
    size(absolute({{"position","absolute"},{"item-is-replaced","true"},{"left","10px"},{"right","10px"},{"top","10px"},{"bottom","10px"},{"width","200px"}}),200,100);
}
TEST_CASE("absolute_replaced::non_replaced_auto_size_is_stretched_between_insets", "[behavior][replaced]") {
    size(absolute({{"position","absolute"},{"left","10px"},{"right","10px"},{"top","10px"},{"bottom","10px"}}),380,280);
}
TEST_CASE("block_replaced::auto_width_uses_intrinsic_size", "[behavior][replaced]") {
    size(badge({{"display","block"},{"item-is-replaced","true"}}),142,20);
}
TEST_CASE("block_replaced::non_replaced_child_is_stretch_sized", "[behavior][replaced]") {
    REQUIRE(badge({{"display","block"}}).size.width==600);
}
TEST_CASE("block_replaced::explicit_width_is_used", "[behavior][replaced]") {
    size(badge({{"display","block"},{"item-is-replaced","true"},{"width","300px"},{"height","50px"}}),300,50);
}
TEST_CASE("block_replaced::max_width_clamps_intrinsic_size", "[behavior][replaced]") {
    REQUIRE(badge({{"display","block"},{"item-is-replaced","true"},{"max-width","100%"}},"100px").size.width==100);
}
TEST_CASE("block_replaced::auto_margins_center_replaced_child", "[behavior][replaced]") {
    auto l=badge({{"display","block"},{"item-is-replaced","true"},{"margin-left","auto"},{"margin-right","auto"}});
    REQUIRE(l.size.width==142); REQUIRE(l.x==(600.0f-142.0f)/2.0f);
}
TEST_CASE("compressible_replaced::block_level_auto_width_uses_intrinsic_size", "[behavior][replaced]") {
    size(input({{"display","block"},{"item-is-compressible-replaced","true"}}),150,20);
}
TEST_CASE("compressible_replaced::block_level_justify_self_stretch_stretches", "[behavior][replaced]") {
    REQUIRE(input({{"display","block"},{"item-is-compressible-replaced","true"},{"justify-self","stretch"}}).size.width==400);
}
TEST_CASE("compressible_replaced::absolute_auto_size_is_stretched_between_insets", "[behavior][replaced]") {
    auto l=input({{"position","absolute"},{"item-is-compressible-replaced","true"},{"left","10px"},{"right","10px"},{"top","10px"},{"bottom","10px"}},true);
    size(l,380,280); location(l,10,10);
}
TEST_CASE("compressible_replaced::grid_percent_max_width_does_not_compress_minimum_contribution", "[behavior][replaced]") {
    REQUIRE(grid_input(false,true)==100);
}
TEST_CASE("compressible_replaced::grid_percent_max_width_compresses_replaced_minimum_contribution", "[behavior][replaced]") {
    REQUIRE(grid_input(true,false)==0);
}

TEST_CASE("compressible_replaced::replaced_implies_compressible_replaced", "[behavior][replaced]") {
    Tree t; uint32_t replaced=0,compressible=0;
    auto first=t.node({{"item-is-replaced","true"}});
    t.ok(behavior_oracle_core_flags(t.native,first,&replaced,&compressible));
    REQUIRE(compressible==1); REQUIRE(replaced==1);
    auto second=t.node({{"item-is-compressible-replaced","true"}});
    t.ok(behavior_oracle_core_flags(t.native,second,&replaced,&compressible));
    REQUIRE(compressible==1); REQUIRE(replaced==0);
}

// Rust #[ignore] maps to Catch's hidden [.] tag, not an unimplemented SKIP.
// All fixture loops and original assertions execute when explicitly selected.
namespace {
void single_axis(bool border,bool padding) {
    for(int i=0;i<4;++i) {
        CAPTURE(i); Tree t;
        auto n=t.node({
            {"border-left",border&&i==0?"10px":"0px"},
            {"border-right",border&&i==1?"10px":"0px"},
            {"border-top",border&&i==2?"10px":"0px"},
            {"border-bottom",border&&i==3?"10px":"0px"},
            {"padding-left",padding&&i==0?"10px":"0px"},
            {"padding-right",padding&&i==1?"10px":"0px"},
            {"padding-top",padding&&i==2?"10px":"0px"},
            {"padding-bottom",padding&&i==3?"10px":"0px"}});
        t.compute(n,definite100); auto l=t.layout(n);
        REQUIRE(l.size.width*l.size.height==0);
    }
}
}
TEST_CASE("border_and_padding::border_on_a_single_axis_doesnt_increase_size", "[.][upstream-ignored][behavior][replaced]") { single_axis(true,false); }
TEST_CASE("border_and_padding::padding_on_a_single_axis_doesnt_increase_size", "[.][upstream-ignored][behavior][replaced]") { single_axis(false,true); }
TEST_CASE("border_and_padding::border_and_padding_on_a_single_axis_doesnt_increase_size", "[.][upstream-ignored][behavior][replaced]") { single_axis(true,true); }
TEST_CASE("border_and_padding::vertical_border_and_padding_percentage_values_use_available_space_correctly", "[.][upstream-ignored][behavior][replaced]") {
    Tree t; auto n=t.node({{"padding-left","100%"},{"padding-top","100%"}});
    t.compute(n,{{1,200},{1,100}}); size(t.layout(n),200,200);
}

TEST_CASE("adversarial_styles::non_finite_flex_factor_terminates", "[behavior][replaced][adversarial]") {
    for(float factor:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
        CAPTURE(factor); Tree t; auto c=t.node({{"width","10px"},{"height","10px"}});
        auto r=t.parent({{"display","grid"},{"grid-template-columns","minmax(auto, 1fr)"}},{c});
        t.ok(behavior_oracle_set_grid_track_fr(t.native,r,1,0,factor));
        t.compute(r,definite100);
    }
}
TEST_CASE("adversarial_styles::non_finite_grid_container_size_terminates", "[behavior][replaced][adversarial]") {
    for(float width:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        CAPTURE(width); Tree t; auto c=t.node();
        auto r=t.parent({{"display","grid"},{"width","1px"},{"height","100px"},{"grid-template-columns","repeat(2, 1fr)"}},{c});
        repeat_without_names(t,r);
        t.ok(behavior_oracle_set_dimension(t.native,r,0,0,1,width));
        t.compute(r,definite100);
    }
}
TEST_CASE("adversarial_styles::repetition_without_line_names_resolves_lines", "[behavior][replaced][adversarial]") {
    Tree t; auto c=t.node({{"width","5px"},{"height","5px"},{"grid-column-start","c"}});
    auto r=t.parent({{"display","grid"},{"width","100px"},{"height","100px"},
        {"grid-template-columns","repeat(2, 10px) 10px"}},{c});
    repeat_without_names(t,r);
    t.field(r,"grid_template_column_names",R"([[],[],["c"]])");
    t.compute(r,definite100); REQUIRE(t.layout(c).x==30);
}
TEST_CASE("adversarial_styles::zero_span_and_named_span_terminates", "[behavior][replaced][adversarial]") {
    for(auto direction:{"ltr","rtl"}) {
        CAPTURE(direction); Tree t;
        auto c=t.node({{"grid-column-start","span does-not-exist"}});
        // CSS rejects span 0, but upstream public constructor accepts it.
        t.field(c,"grid_column",R"({"start":{"NamedSpan":["does-not-exist",1]},"end":{"Span":0}})");
        auto r=t.parent({{"display","grid"},{"direction",direction}},{c}); t.compute(r,definite100);
    }
}
TEST_CASE("adversarial_styles::repetition_with_wrong_line_name_set_count_panics", "[behavior][replaced][adversarial]") {
    Tree t; auto c=t.node();
    auto r=t.parent({{"display","grid"},{"grid-template-columns","repeat(2, 10px)"}},{c});
    auto columns=member(t.style(r),"grid_template_columns");
    replace_member(columns,"line_names",R"([["a"]])");
    t.field(r,"grid_template_columns",columns);
    t.field(r,"grid_template_column_names","[[],[]]");
    REQUIRE(t.compute_status(r,definite100)==BEHAVIOR_PANIC);
    REQUIRE(t.error().find("line name sets")!=std::string::npos);
}
TEST_CASE("adversarial_styles::small_item_in_max_size_occupied_grid_terminates", "[behavior][replaced][adversarial]") {
    Tree t;
    auto big=t.node({{"grid-row-start","span 10000"},{"grid-column-start","span 10000"}});
    auto small=t.node({{"grid-row-start","span 10000"},{"grid-column-start","span 1"}});
    auto r=t.parent({{"display","grid"},{"width","100px"},{"height","100px"}},{big,small}); t.compute(r,definite100);
}
TEST_CASE("adversarial_styles::extreme_placements_and_spans_terminate", "[behavior][replaced][adversarial]") {
    Tree t;
    auto a=t.node({{"grid-row-end","span 2000"},{"grid-column-end","span 2000"}});
    auto b=t.node({{"grid-row-start","-2000"},{"grid-row-end","2000"},{"grid-column-start","-2000"},{"grid-column-end","2000"}});
    auto r=t.parent({{"display","grid"},{"width","100px"},{"height","100px"}},{a,b}); t.compute(r,definite100);
}
