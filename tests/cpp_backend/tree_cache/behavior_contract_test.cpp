#include "behavior_style.hpp"
#include <catch2/catch_test_macros.hpp>
#include <limits>
using namespace behavior_cpp;
TEST_CASE("cpp_behavior_contract::strict_native_snapshot_and_nonfinite_bits", "[cpp-behavior-contract]") {
    taffy::Style s;
    const auto original=style_json(s);
    REQUIRE(parse_style_json(original)==s);
    REQUIRE(style_detail::Parser{original}.parse().object.size()==45);
    REQUIRE_THROWS_AS(parse_style_json("[]"),std::invalid_argument);
    REQUIRE_THROWS_AS(parse_style_json("{}"),std::invalid_argument);
    REQUIRE_THROWS_AS(set_native_field(s,"typo","0"),std::invalid_argument);
    REQUIRE_THROWS_AS(set_native_field(s,"grid_row","[]"),std::invalid_argument);
    REQUIRE_THROWS_AS(set_native_field(s,"size",R"({"width":1,"width":2,"height":1})"),std::invalid_argument);
    REQUIRE_THROWS_AS(set_native_field(s,"align_self","\"SafeStretch\""),std::invalid_argument);
    REQUIRE_THROWS_AS(set_native_field(s,"flex_line_count","65536"),std::invalid_argument);
    REQUIRE(style_json(s)==original);
    s.size.width=taffy::Dimension::length(std::numeric_limits<float>::infinity());
    s.flex_basis=taffy::Dimension::length(std::bit_cast<float>(uint32_t(0x7fc12345)));
    auto round=parse_style_json(style_json(s));
    REQUIRE(std::isinf(round.size.width.value()));
    REQUIRE(std::bit_cast<uint32_t>(round.flex_basis.value())==0x7fc12345);
    s.scrollbar_width=std::numeric_limits<float>::infinity();
    REQUIRE_THROWS_AS(style_json(s),std::invalid_argument);
    REQUIRE_THROWS_AS(style_detail::decode<taffy::Dimension>(style_detail::Json::num("12884901889")),std::invalid_argument);
}
TEST_CASE("cpp_behavior_contract::weak_json_value_and_native_scalars", "[cpp-behavior-contract]") {
    REQUIRE_NOTHROW(parse_json_value(R"({"size":{"width":{"Percent":50}},"size":false})"));
    REQUIRE_THROWS_AS(parse_json_value("[1,]"),std::invalid_argument);
    REQUIRE_THROWS_AS(parse_json_value("1e999"),std::invalid_argument);
    REQUIRE_THROWS_AS(parse_json_value("\"\\ud800\""),std::invalid_argument);
    auto txt=[](const char* s){return BehaviorText{s,std::char_traits<char>::length(s)};};
    BehaviorProperty prop{txt("aspect-ratio"),txt("NaN")};
    REQUIRE(std::isnan(*parse_properties(&prop,1).aspect_ratio));
    prop={txt("flex-grow"),txt(" 1")}; REQUIRE_THROWS_AS(parse_properties(&prop,1),std::invalid_argument);
    prop={txt("item-is-table"),txt("TRUE")}; REQUIRE_THROWS_AS(parse_properties(&prop,1),std::invalid_argument);
    prop={txt("flex-line-count"),txt("+2")}; REQUIRE(parse_properties(&prop,1).flex_line_count==2);
}
TEST_CASE("cpp_behavior_contract::caller_owned_short_output_and_poison_free", "[cpp-behavior-contract]") {
    BehaviorOracle* tree=nullptr; REQUIRE(behavior_oracle_create(&tree)==BEHAVIOR_OK);
    struct Owner {BehaviorOracle* p;~Owner(){behavior_oracle_free(p);}} owner{tree};
    uint64_t id=0;REQUIRE(behavior_oracle_new_node(tree,nullptr,0,&id)==BEHAVIOR_OK);
    size_t needed=0; REQUIRE(behavior_oracle_style_json(tree,id,nullptr,0,&needed)==BEHAVIOR_OK);
    char prefix[4]={'x','x','x','x'};size_t short_needed=0;
    REQUIRE(behavior_oracle_style_json(tree,id,prefix,sizeof prefix,&short_needed)==BEHAVIOR_OK);
    REQUIRE(short_needed==needed); REQUIRE(prefix[3]=='\0');
    uint32_t dirty=0;REQUIRE(behavior_oracle_dirty(tree,id,&dirty)==BEHAVIOR_OK);REQUIRE(dirty==1);
    uint64_t child=0;REQUIRE(behavior_oracle_new_node(tree,nullptr,0,&child)==BEHAVIOR_OK);
    REQUIRE(behavior_oracle_insert_child(tree,id,0,child)==BEHAVIOR_OK);
    REQUIRE(behavior_oracle_remove_child(tree,id,child)==BEHAVIOR_OK);
    const BehaviorAvailable max{{4,0},{4,0}};
    REQUIRE(behavior_oracle_set_aspect_context(tree,id,14,.5f)==BEHAVIOR_OK);
    REQUIRE(behavior_oracle_compute_test_measure(tree,id,max,1)==BEHAVIOR_OK);
    BehaviorLayout l{};REQUIRE(behavior_oracle_layout(tree,id,&l)==BEHAVIOR_OK);
    REQUIRE(l.size.width==14);REQUIRE(l.size.height==7);
    REQUIRE(behavior_oracle_dirty(tree,id,&dirty)==BEHAVIOR_OK);REQUIRE(dirty==0);
    REQUIRE(behavior_oracle_detailed_json(tree,id,prefix,sizeof prefix,&needed)==BEHAVIOR_OK);
    REQUIRE(needed==5);REQUIRE(std::string(prefix)=="nul");
    REQUIRE(behavior_oracle_tracks(tree,id,0,prefix,sizeof prefix,&needed)==BEHAVIOR_OK);
    REQUIRE(needed==1);REQUIRE(prefix[0]=='\0');
    auto txt=[](const char* s){return BehaviorText{s,std::char_traits<char>::length(s)};};
    const BehaviorProperty grid{txt("display"),txt("grid")};uint64_t root=0;
    REQUIRE(behavior_oracle_new_node(tree,&grid,1,&root)==BEHAVIOR_OK);
    REQUIRE(behavior_oracle_add_child(tree,root,child)==BEHAVIOR_OK);
    REQUIRE(behavior_oracle_compute(tree,root,max,1,nullptr,nullptr)==BEHAVIOR_OK);
    REQUIRE(behavior_oracle_detailed_json(tree,root,nullptr,0,&needed)==BEHAVIOR_OK);
    std::string metadata(needed,'\0');
    REQUIRE(behavior_oracle_detailed_json(tree,root,metadata.data(),metadata.size(),&needed)==BEHAVIOR_OK);
    metadata.resize(needed-1);auto parsed=style_detail::Parser{metadata}.parse();
    REQUIRE(parsed.object.size()==3);REQUIRE(parsed.object.contains("rows"));REQUIRE(parsed.object.contains("columns"));
    REQUIRE(parsed.object.at("items").array.size()==1);
    REQUIRE(parsed.object.at("columns").object.contains("collapsed_tracks"));
    REQUIRE(parsed.object.at("columns").object.contains("resolved_line_names"));
    REQUIRE(behavior_oracle_inject_panic(tree)==BEHAVIOR_PANIC);
    REQUIRE(behavior_oracle_mark_dirty(tree,id)==BEHAVIOR_POISONED);
    REQUIRE(behavior_oracle_error(tree,prefix,sizeof prefix,&needed)==BEHAVIOR_OK);
}
