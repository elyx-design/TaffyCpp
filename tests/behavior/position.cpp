// MIT upstream handwritten translations; see position_mapping.md.
// Native reference tests only: this checkout does not yet expose production C++ layout APIs.
#include "position_native.h"
#include <array>

namespace position_tests {
namespace {
constexpr const char* displays[]={"block","flex","grid"};
constexpr const char* directions[]={"ltr","rtl"};

TEST_CASE("floats.rs::float_no_higher_than_earlier_floats", "[behavior][position][native-oracle]") {
    Tree t;
    const auto spacer=t.node(floating(400,43.2f,"none"));
    const auto a=t.node(floating(258,42,"left"));
    const auto b=t.node(floating(298,42,"left"));
    const auto c=t.node(floating(98,42,"right"));
    const auto root=t.node(block_root(400),{spacer,a,b,c});
    t.compute(root);
    location(t.layout(a),0,43); location(t.layout(b),0,85); location(t.layout(c),302,85);
}
TEST_CASE("floats.rs::clear_past_zero_width_float", "[behavior][position][native-oracle]") {
    Tree t;
    const auto floated=t.node({{"display","block"},{"float","left"},{"width","auto"},{"height","1px"}});
    const auto cleared=t.node({{"display","block"},{"clear","left"}});
    const auto sibling=t.node({{"display","block"},{"margin-top","99px"}});
    const auto container=t.node(block_root(100),{floated,cleared,sibling});
    const auto root=t.node({{"display","block"}},{container});
    t.compute(root); REQUIRE(t.layout(container).size.height==100);
}
TEST_CASE("floats.rs::oversized_float_overflows_containing_block", "[behavior][position][native-oracle]") {
    Tree t; const auto a=t.node(floating(150,50,"left"));
    const auto root=t.node(block_root(100),{a}); t.compute(root); location(t.layout(a),0,0);
}
TEST_CASE("floats.rs::float_beside_existing_float_moves_down_instead_of_overflowing", "[behavior][position][native-oracle]") {
    Tree t;
    const auto left=t.node(floating(60,50,"left"));
    const auto right=t.node(floating(60,50,"right"));
    const auto left2=t.node(floating(60,50,"left"));
    const auto root=t.node(block_root(100),{left,right,left2}); t.compute(root);
    location(t.layout(left),0,0); location(t.layout(right),40,50); location(t.layout(left2),0,100);
}
TEST_CASE("floats.rs::auto_margins_include_float_avoidance_offsets", "[behavior][position][native-oracle]") {
    constexpr std::pair<float,float> widths[]={{0,20},{20,0},{20,20},{0,0}};
    constexpr std::pair<bool,bool> sides[]={{true,true},{true,false},{false,true}};
    for(const auto direction:directions) for(const auto& [lw,rw]:widths) {
        CAPTURE(direction,lw,rw); Tree t;
        const auto left=t.node(floating(lw,40,"left"));
        const auto right=t.node(floating(rw,40,"right"));
        std::vector<Node> boxes,children{left,right};
        for(const auto& [la,ra]:sides) {
            auto s=leaf(40,10); s["display"]="flow-root"; s["direction"]=direction;
            s["margin-left"]=la?"auto":"0px"; s["margin-right"]=ra?"auto":"0px";
            s["margin-top"]="auto"; s["margin-bottom"]="auto";
            boxes.push_back(t.node(s)); children.push_back(boxes.back());
        }
        Style s={{"display","flow-root"},{"direction",direction},{"width","100px"},{"height","auto"},
                 {"padding-left","5px"},{"padding-right","5px"},{"padding-top","5px"},{"padding-bottom","5px"}};
        const auto root=t.node(s,children); t.compute(root);
        for(std::size_t i=0;i<boxes.size();++i) {
            CAPTURE(i); const auto [la,ra]=sides[i]; const auto l=t.layout(boxes[i]);
            const float free_space=90-lw-rw-40;
            const float alignment_margin=free_space/(static_cast<unsigned>(la)+static_cast<unsigned>(ra));
            location(l,5+lw+(la?alignment_margin:0),5+static_cast<float>(i)*10);
            size(l,40,10);
            REQUIRE(l.margin.left==(la?lw+alignment_margin:0));
            REQUIRE(l.margin.right==(ra?rw+alignment_margin:0));
            REQUIRE(l.margin.top==0); REQUIRE(l.margin.bottom==0);
        }
    }
}
TEST_CASE("floats.rs::float_avoiding_auto_margins_in_nested_block_ignore_relative_insets_and_clearance", "[behavior][position][native-oracle]") {
    for(const auto direction:directions) {
        CAPTURE(direction); Tree t;
        const auto left=t.node(floating(40,40,"left")); const auto right=t.node(floating(60,40,"right"));
        auto box=leaf(40,10); box["display"]="flow-root"; box["direction"]=direction;
        box["margin-left"]="auto"; box["margin-right"]="auto";
        box["margin-top"]="0px"; box["margin-bottom"]="0px";
        auto relative=box; relative["position"]="relative"; relative["left"]="7px";
        const auto relative_box=t.node(relative);
        box["clear"]="both"; const auto cleared_box=t.node(box);
        Style nested_style={{"display","block"},{"direction",direction},
            {"margin-left","20px"},{"margin-right","30px"},
            {"padding-left","5px"},{"padding-right","7px"},
            {"border-left","3px"},{"border-right","5px"}};
        const auto nested=t.node(nested_style,{relative_box,cleared_box});
        Style root_style={{"display","flow-root"},{"direction",direction},{"width","200px"},{"height","auto"},
            {"padding-left","10px"},{"padding-right","10px"},{"padding-top","10px"},{"padding-bottom","10px"}};
        const auto root=t.node(root_style,{left,right,nested}); t.compute(root);
        const auto r=t.layout(relative_box); location(r,47,0); REQUIRE(r.margin.left==32); REQUIRE(r.margin.right==38);
        const auto c=t.layout(cleared_box); location(c,43,40); REQUIRE(c.margin.left==35); REQUIRE(c.margin.right==35);
        const auto n=t.layout(nested); location(n,30,10); REQUIRE(n.margin.left==20); REQUIRE(n.margin.right==30);
    }
}

TEST_CASE("oof_hoisting.rs::absolute_skips_static_parent", "[behavior][position][native-oracle]") {
    for(const auto display:displays) {
        CAPTURE(display); Tree t;
        const auto abs=t.node(positioned(30,30,"absolute",10,20));
        auto s=leaf(50,50); s["display"]=display; offset_margin(s,40,40);
        const auto parent=t.node(s,{abs});
        s=leaf(200,200); s["display"]=display; s["position"]="relative";
        const auto cb=t.node(s,{parent}); t.compute(cb);
        location(t.layout(abs),10,20); size(t.layout(abs),30,30);
    }
}
TEST_CASE("oof_hoisting.rs::hoisted_children_are_in_document_order", "[behavior][position][native-oracle]") {
    for(const auto display:displays) {
        CAPTURE(display); Tree t;
        auto a=leaf(10,10); a["position"]="absolute";
        auto f=leaf(20,20); f["display"]=display;
        const auto direct_a=t.node(a); const auto nested_b=t.node(a); const auto static_b=t.node(f,{nested_b});
        const auto nested_c=t.node(a); const auto static_c=t.node(f,{nested_c}); const auto direct_d=t.node(a);
        const auto nested_e=t.node(a); const auto static_e=t.node(f,{nested_e});
        auto s=leaf(50,200); s["display"]=display; s["position"]="relative";
        s["flex-wrap"]="wrap-reverse"; s["flex-direction"]="row-reverse";
        const auto cb=t.node(s,{direct_a,static_b,static_c,direct_d,static_e}); t.compute(cb);
        REQUIRE(t.hoisted(cb)==std::vector<Node>{direct_a,nested_b,nested_c,direct_d,nested_e});
    }
}
TEST_CASE("oof_hoisting.rs::static_position_is_containing_block_relative", "[behavior][position][native-oracle]") {
    for(const auto display:displays) {
        CAPTURE(display); Tree t;
        auto s=leaf(30,30); s["position"]="absolute"; const auto abs=t.node(s);
        const auto sibling=t.node(leaf(50,50));
        s=leaf(100,100); s["display"]=display; offset_margin(s,40,40);
        const auto parent=t.node(s,{sibling,abs});
        s=leaf(200,200); s["display"]=display; s["position"]="relative";
        const auto cb=t.node(s,{parent}); t.compute(cb);
        location(t.layout(abs),40,std::string(display)=="block"?90:40);
    }
}
TEST_CASE("oof_hoisting.rs::fixed_hoists_to_root", "[behavior][position][native-oracle]") {
    Tree t; const auto fixed=t.node(positioned(10,10,"fixed",5,5));
    auto s=leaf(50,50); s["position"]="relative"; offset_margin(s,50,50);
    const auto parent=t.node(s,{fixed}); const auto root=t.node(leaf(200,200),{parent});
    t.compute(root); location(t.layout(fixed),5,5);
}
TEST_CASE("oof_hoisting.rs::absolute_within_absolute", "[behavior][position][native-oracle]") {
    Tree t; auto s=leaf(10,10); s["position"]="absolute";
    s["right"]="0px"; s["bottom"]="0px"; s["left"]="auto"; s["top"]="auto";
    const auto inner=t.node(s); const auto outer=t.node(positioned(50,50,"absolute",20,20),{inner});
    const auto root=t.node(leaf(200,200),{outer}); t.compute(root);
    location(t.layout(outer),20,20); location(t.layout(inner),40,40);
}
TEST_CASE("oof_hoisting.rs::fixed_within_absolute_hoists_to_root", "[behavior][position][native-oracle]") {
    Tree t; const auto fixed=t.node(positioned(10,10,"fixed",1,2));
    const auto abs=t.node(positioned(50,50,"absolute",20,20),{fixed});
    auto s=leaf(100,100); s["position"]="relative"; offset_margin(s,30,30);
    const auto cb=t.node(s,{abs}); const auto root=t.node(leaf(200,200),{cb}); t.compute(root);
    location(t.layout(abs),20,20); location(t.layout(fixed),1,2);
}
TEST_CASE("oof_hoisting.rs::hoisting_survives_cache_hits", "[behavior][position][native-oracle]") {
    Tree t; auto abs_style=positioned(30,30,"absolute",10,20); const auto abs=t.node(abs_style);
    auto ancestor=t.node(leaf(50,50),{abs});
    for(int i=0;i<3;++i) ancestor=t.node(leaf(50,50),{ancestor});
    auto s=leaf(200,200); s["position"]="relative"; const auto cb=t.node(s,{ancestor});
    t.compute(cb); location(t.layout(abs),10,20);
    t.compute(cb); location(t.layout(abs),10,20);
    t.dirty(cb); t.compute(cb); location(t.layout(abs),10,20);
    const auto native_abs_style=t.read_style(abs); // upstream style(abs).unwrap().clone()
    t.snapshot_left(abs,native_abs_style,15); t.compute(cb); location(t.layout(abs),15,20);
}
TEST_CASE("oof_hoisting.rs::cached_root_does_not_duplicate_hoisted_children", "[behavior][position][native-oracle]") {
    Tree t; auto s=leaf(30,30); s["position"]="fixed"; const auto fixed=t.node(s);
    const auto inner=t.node(leaf(50,50),{fixed}); const auto root=t.node(leaf(200,200),{inner});
    t.compute(root); REQUIRE(t.hoisted(root)==std::vector<Node>{fixed});
    t.compute(root); REQUIRE(t.hoisted(root)==std::vector<Node>{fixed});
    t.dirty(root); t.compute(root); REQUIRE(t.hoisted(root)==std::vector<Node>{fixed});
}
TEST_CASE("oof_hoisting.rs::rounding_uses_containing_block_offsets", "[behavior][position][native-oracle]") {
    Tree t; // Native compute explicitly enables rounding, matching upstream enable_rounding().
    const auto abs=t.node(positioned(10,10,"absolute",10.6f,10.6f));
    const auto parent=t.node(leaf(50,50),{abs});
    auto s=leaf(100,100); s["position"]="relative"; offset_margin(s,20.3f,20.3f);
    const auto cb=t.node(s,{parent}); const auto root=t.node(leaf(200,200),{cb}); t.compute(root);
    location(t.layout(cb),20,20); location(t.layout(abs),11,11);
}
TEST_CASE("oof_hoisting.rs::claim_change_relayouts_with_hot_cache", "[behavior][position][native-oracle]") {
    Tree t; const auto abs=t.node(positioned(30,30,"absolute",10,20));
    auto middle_style=leaf(50,50); offset_margin(middle_style,40,40);
    const auto middle=t.node(middle_style,{abs});
    auto s=leaf(200,200); s["position"]="relative"; const auto cb=t.node(s,{middle});
    t.compute(cb); location(t.layout(abs),10,20);
    auto positioned_middle=middle_style; positioned_middle["position"]="relative";
    t.style(middle,positioned_middle); t.compute(cb); location(t.layout(abs),10,20); location(t.layout(middle),40,40);
    t.style(middle,middle_style); t.compute(cb); location(t.layout(abs),10,20); location(t.layout(middle),40,40);
}
TEST_CASE("oof_hoisting.rs::fixed_claim_change_relayouts_with_hot_cache", "[behavior][position][native-oracle]") {
    Tree t; auto fixed_style=positioned(10,10,"fixed",5,5); const auto fixed=t.node(fixed_style);
    const auto abs=t.node(positioned(80,80,"absolute",50,50),{fixed});
    const auto root=t.node(leaf(200,200),{abs}); t.compute(root);
    location(t.layout(fixed),5,5); location(t.layout(abs),50,50);
    // Upstream test's actual operation changes only the fixed inset (despite its broader doc comment).
    const auto native_fixed_style=t.read_style(fixed); // upstream style(fixed).unwrap().clone()
    t.snapshot_left(fixed,native_fixed_style,7); t.compute(root); location(t.layout(fixed),7,5);
}
TEST_CASE("oof_hoisting.rs::content_change_inside_hoisted_subtree", "[behavior][position][native-oracle]") {
    Tree t; const auto inner=t.node(leaf(10,10));
    Style a={{"position","absolute"},{"left","10px"},{"top","10px"},{"right","auto"},{"bottom","auto"}};
    const auto abs=t.node(a,{inner}); const auto parent=t.node(leaf(50,50),{abs});
    auto s=leaf(200,200); s["position"]="relative"; const auto cb=t.node(s,{parent});
    t.compute(cb); size(t.layout(abs),10,10);
    t.style(inner,leaf(20,30)); t.compute(cb); size(t.layout(abs),20,30);
}
TEST_CASE("oof_hoisting.rs::hiding_an_ancestor_zeroes_out_of_flow_layout", "[behavior][position][native-oracle]") {
    for(const auto position:{"absolute","fixed"}) for(const auto parent_position:{"static","relative"}) {
        CAPTURE(position,parent_position); Tree t;
        auto s=leaf(30,30); s["position"]=position; const auto oof=t.node(s);
        auto parent_style=leaf(50,50); parent_style["position"]=parent_position;
        const auto parent=t.node(parent_style,{oof}); const auto root=t.node(leaf(200,200),{parent});
        t.compute(root); size(t.layout(oof),30,30);
        parent_style["display"]="none"; t.style(parent,parent_style); t.compute(root);
        size(t.unrounded(oof),0,0); size(t.layout(oof),0,0);
    }
}

// The shared fixture builder changes only fields that differ in the upstream cases.
void self_alignment(const char* display,float w,float h,const char* field,const char* alignment,
                    bool rtl,bool horizontal,float expected) {
    Tree t; auto s=leaf(w,h); s[field]=alignment; const auto child=t.node(s);
    s=leaf(100,100); s["display"]=display;
    if(std::string(display)=="grid") { s["grid-template-rows"]="100px"; s["grid-template-columns"]="100px"; }
    if(rtl) s["direction"]="rtl";
    const auto root=t.node(s,{child}); t.compute(root);
    if(horizontal) REQUIRE(t.layout(child).x==expected); else REQUIRE(t.layout(child).y==expected);
}
TEST_CASE("safe_alignment.rs::grid_safe_align_self_falls_back_to_start_on_overflow", "[behavior][position][native-oracle]") {
    self_alignment("grid",50,150,"align-self","safe end",false,false,0);
}
TEST_CASE("safe_alignment.rs::grid_safe_align_self_behaves_as_end_when_no_overflow", "[behavior][position][native-oracle]") {
    self_alignment("grid",50,40,"align-self","safe end",false,false,60);
}
TEST_CASE("safe_alignment.rs::grid_unsafe_align_self_keeps_overflowing_position", "[behavior][position][native-oracle]") {
    self_alignment("grid",50,150,"align-self","end",false,false,-50);
}
TEST_CASE("safe_alignment.rs::grid_safe_justify_self_falls_back_to_start_on_overflow", "[behavior][position][native-oracle]") {
    self_alignment("grid",150,50,"justify-self","safe end",false,true,0);
}
TEST_CASE("safe_alignment.rs::grid_safe_justify_self_rtl_falls_back_to_rtl_start_edge", "[behavior][position][native-oracle]") {
    self_alignment("grid",150,50,"justify-self","safe end",true,true,-50);
}
TEST_CASE("safe_alignment.rs::grid_safe_align_content_overflow_falls_back_to_start", "[behavior][position][native-oracle]") {
    Tree t; const auto a=t.node(leaf(40,80)); const auto b=t.node(leaf(40,80));
    auto s=leaf(100,100); s["display"]="grid"; s["grid-template-rows"]="80px 80px";
    s["grid-template-columns"]="40px"; s["align-content"]="safe end";
    const auto root=t.node(s,{a,b}); t.compute(root); REQUIRE(t.layout(a).y==0);
}
TEST_CASE("safe_alignment.rs::flex_safe_align_self_falls_back_to_start_on_overflow", "[behavior][position][native-oracle]") {
    self_alignment("flex",50,150,"align-self","safe end",false,false,0);
}
TEST_CASE("safe_alignment.rs::flex_unsafe_align_self_keeps_overflowing_position", "[behavior][position][native-oracle]") {
    self_alignment("flex",50,150,"align-self","end",false,false,-50);
}
TEST_CASE("safe_alignment.rs::flex_safe_justify_content_falls_back_to_start_on_overflow", "[behavior][position][native-oracle]") {
    Tree t; const auto a=t.node(leaf(80,50)); const auto b=t.node(leaf(80,50));
    auto s=leaf(100,100); s["display"]="flex"; s["justify-content"]="safe end";
    const auto root=t.node(s,{a,b}); t.compute(root); REQUIRE(t.layout(a).x==0);
}
TEST_CASE("safe_alignment.rs::flex_safe_align_content_falls_back_to_start_on_multiline_overflow", "[behavior][position][native-oracle]") {
    Tree t; const auto a=t.node(leaf(60,80)); const auto b=t.node(leaf(60,80));
    auto s=leaf(100,100); s["display"]="flex"; s["flex-wrap"]="wrap"; s["align-content"]="safe end";
    const auto root=t.node(s,{a,b}); t.compute(root); REQUIRE(t.layout(a).y==0);
}
} // namespace
} // namespace position_tests
