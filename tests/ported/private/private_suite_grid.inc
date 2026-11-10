// Source-unit ports: explicit_grid.rs, implicit_grid.rs, pending placement.rs.
// Included by private_suite_test.cpp (Catch2 TEST_CASE/CHECK/REQUIRE supplied there).
#include "support/private_suite_grid.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace ps_grid_test {
using Def = PsGridTrackDef;
using Counts = PsGridCounts;
inline PsGridSizing sizing(uint32_t tag, float value = 0, const void* ptr = nullptr) { return {tag,value,ptr}; }
inline Def length(float x) { return {sizing(1,x),sizing(1,x)}; }
inline Def percent(float x) { return {sizing(2,x),sizing(2,x)}; }
inline Def fr(float x) { return {sizing(0),sizing(3,x)}; }
inline Def automatic() { return {sizing(0),sizing(0)}; }
inline Def minmax(Def min, Def max) { return {min.min,max.max}; }
struct Component {
    uint32_t mode, count;
    std::vector<Def> tracks;
    Component(Def track): mode(0),count(0),tracks{track} {}
    Component(uint32_t m, std::vector<Def> t, uint32_t n=0): mode(m),count(n),tracks(std::move(t)) {}
};
inline Component fill(std::vector<Def> t) { return {3,std::move(t)}; }
inline Component repeat(uint32_t n, std::vector<Def> t) { return {1,std::move(t),n}; }
struct Style {
    std::vector<Component> columns, rows;
    std::vector<Def> auto_columns, auto_rows;
    float width=std::numeric_limits<float>::quiet_NaN(), height=width, min_width=width, min_height=width;
    float gap_width=0, gap_height=0;
    std::array<float,4> padding{};
    std::vector<PsGridComponent> c,r;
    PsGridStyle abi() {
        c.clear(); r.clear();
        for (const auto& v:columns) c.push_back({v.mode,v.count,v.tracks.data(),v.tracks.size()});
        for (const auto& v:rows) r.push_back({v.mode,v.count,v.tracks.data(),v.tracks.size()});
        return {c.data(),c.size(),r.data(),r.size(),auto_columns.data(),auto_columns.size(),auto_rows.data(),auto_rows.size(),width,height,min_width,min_height,gap_width,gap_height,padding[0],padding[1],padding[2],padding[3]};
    }
};
inline PsGridExplicit explicit_size(Style& style, unsigned axis, float size, unsigned strategy=0, PsGridCalcResolver calc=nullptr) {
    auto input=style.abi(); PsGridExplicit out{};
    REQUIRE(ps_grid_explicit(&input,axis,!std::isnan(size),size,strategy,calc,&out)==0);
    return out;
}
inline void check_explicit(Style& s, float w, float h, unsigned strategy, uint16_t cols, uint16_t rows, uint16_t col_reps, uint16_t row_reps) {
    const auto c=explicit_size(s,0,w,strategy), r=explicit_size(s,1,h,strategy);
    CHECK(c.tracks==cols); CHECK(r.tracks==rows);
    CHECK(c.repetitions==col_reps); CHECK(r.repetitions==row_reps);
}
inline std::vector<PsGridTrack> tracks(Style& s, Counts counts, uint16_t reps=0) {
    auto input=s.abi(); std::vector<PsGridTrack> out((size_t(counts.negative_implicit)+counts.explicit_count+counts.positive_implicit)*2+1);
    size_t n=0;
    REQUIRE(ps_grid_tracks(&input,counts,0,reps,nullptr,0,out.data(),out.size(),&n)==0);
    REQUIRE(n==out.size()); return out;
}
inline PsGridPlacement automatic_placement() { return {0,0}; }
inline PsGridPlacement line(int x) { return {1,x}; }
inline PsGridPlacement span(int x) { return {2,x}; }
inline PsGridChild child(uint64_t id, PsGridPlacement cs, PsGridPlacement ce, PsGridPlacement rs, PsGridPlacement re) { return {id,cs,ce,rs,re}; }
inline void check_counts(Counts a, Counts e) {
    CHECK(a.negative_implicit==e.negative_implicit); CHECK(a.explicit_count==e.explicit_count); CHECK(a.positive_implicit==e.positive_implicit);
}
struct ExpectedChild { PsGridChild input; std::array<int16_t,4> expected; };
inline void placement(uint16_t cols, uint16_t rows, unsigned flow, std::vector<ExpectedChild> children, Counts expected_cols, Counts expected_rows) {
    std::vector<PsGridChild> inputs; for (const auto& c: children) inputs.push_back(c.input);
    std::vector<PsGridPlaced> out(inputs.size()); size_t written=0; Counts c{},r{};
    REQUIRE(ps_grid_place(cols,rows,inputs.data(),inputs.size(),flow,out.data(),out.size(),&written,&c,&r)==0);
    REQUIRE(written==children.size());
    for(size_t i=0;i<written;++i) {
        CHECK(out[i].node==children[i].input.node);
        CHECK(out[i].column_start==children[i].expected[0]); CHECK(out[i].column_end==children[i].expected[1]);
        CHECK(out[i].row_start==children[i].expected[2]); CHECK(out[i].row_end==children[i].expected[3]);
    }
    check_counts(r,expected_rows); check_counts(c,expected_cols);
}
} // namespace ps_grid_test

TEST_CASE("explicit_grid_sizing_no_repeats", "[private-suite][grid]") {
    using namespace ps_grid_test; Style s; s.width=600; s.height=600;
    s.columns={fr(1),fr(1)}; s.rows={fr(1),fr(1),fr(1),fr(1)};
    check_explicit(s,s.width,s.height,0,2,4,0,0);
}
TEST_CASE("explicit_grid_sizing_auto_fill_exact_fit", "[private-suite][grid]") {
    using namespace ps_grid_test; Style s; s.width=120; s.height=80;
    s.columns={fill({length(40)})}; s.rows={fill({length(20)})};
    check_explicit(s,s.width,s.height,0,3,4,3,4);
}
TEST_CASE("explicit_grid_sizing_auto_fill_non_exact_fit", "[private-suite][grid]") {
    using namespace ps_grid_test; Style s; s.width=140; s.height=90;
    s.columns={fill({length(40)})}; s.rows={fill({length(20)})};
    check_explicit(s,s.width,s.height,0,3,4,3,4);
}
TEST_CASE("explicit_grid_sizing_auto_fill_min_size_exact_fit", "[private-suite][grid]") {
    using namespace ps_grid_test; Style s; s.min_width=120; s.min_height=80;
    s.columns={fill({length(40)})}; s.rows={fill({length(20)})};
    check_explicit(s,120,80,1,3,4,3,4);
}
TEST_CASE("explicit_grid_sizing_auto_fill_min_size_non_exact_fit", "[private-suite][grid]") {
    using namespace ps_grid_test; Style s; s.min_width=140; s.min_height=90;
    s.columns={fill({length(40)})}; s.rows={fill({length(20)})};
    check_explicit(s,140,90,1,4,5,4,5);
}
TEST_CASE("explicit_grid_sizing_auto_fill_multiple_repeated_tracks", "[private-suite][grid]") {
    using namespace ps_grid_test; Style s; s.width=140; s.height=100;
    s.columns={fill({length(40),length(20)})}; s.rows={fill({length(20),length(10)})};
    check_explicit(s,s.width,s.height,0,4,6,2,3);
}
TEST_CASE("explicit_grid_sizing_auto_fill_gap", "[private-suite][grid]") {
    using namespace ps_grid_test; Style s; s.width=140; s.height=100; s.gap_width=s.gap_height=20;
    s.columns={fill({length(40)})}; s.rows={fill({length(20)})};
    check_explicit(s,s.width,s.height,0,2,3,2,3);
}
TEST_CASE("explicit_grid_sizing_no_defined_size", "[private-suite][grid]") {
    using namespace ps_grid_test; Style s; s.gap_width=s.gap_height=20;
    s.columns={fill({length(40),percent(.5f),length(20)})}; s.rows={fill({length(20)})};
    check_explicit(s,s.width,s.height,1,3,1,1,1);
}
TEST_CASE("explicit_grid_sizing_mix_repeated_and_non_repeated", "[private-suite][grid]") {
    using namespace ps_grid_test; Style s; s.width=140; s.height=100; s.gap_width=s.gap_height=20;
    s.columns={length(20),fill({length(40)})}; s.rows={length(40),fill({length(20)})};
    check_explicit(s,s.width,s.height,0,3,2,2,1);
}
TEST_CASE("explicit_grid_sizing_mix_with_padding", "[private-suite][grid]") {
    using namespace ps_grid_test; Style s; s.width=s.height=120; s.padding={10,10,20,20};
    s.columns={fill({length(20)})}; s.rows={fill({length(20)})};
    check_explicit(s,100,80,0,5,4,5,4);
}
TEST_CASE("auto_repeat_clamping_preserves_template_source_order", "[private-suite][grid]") {
    using namespace ps_grid_test; Style first; first.columns={fill({length(2)}),repeat(10000,{length(37)})};
    const auto a=explicit_size(first,0,first.width); CHECK(a.repetitions==1); CHECK(a.tracks==10000);
    const auto t=tracks(first,{0,a.tracks,0},a.repetitions);
    CHECK(t[1].min.tag==1); CHECK(t[1].min.value==2); CHECK(t[3].min.tag==1); CHECK(t[3].min.value==37);
    Style last; last.columns={repeat(10000,{length(37)}),fill({length(2)})};
    const auto b=explicit_size(last,0,last.width); CHECK(b.repetitions==0); CHECK(b.tracks==10000);
}
namespace ps_grid_test {
alignas(8) static const uint64_t calc_handle=0; static bool calc_ptr_matches=true;
inline float calc_min_100_percent_300px(const void* ptr, float basis) noexcept {
    calc_ptr_matches = calc_ptr_matches && ptr==&calc_handle; return std::min(basis,300.0f);
}
}
TEST_CASE("explicit_grid_sizing_auto_fit_calc_min_track", "[private-suite][grid]") {
    using namespace ps_grid_test;
    for (unsigned mode: {2u,3u}) {
        Style s; s.width=800; s.gap_width=8;
        s.columns={Component(mode,{{sizing(4,0,&calc_handle),sizing(3,1)}})};
        calc_ptr_matches=true; auto a=explicit_size(s,0,800,0,calc_min_100_percent_300px); CHECK(calc_ptr_matches); CHECK(a.repetitions==2); CHECK(a.tracks==2);
        calc_ptr_matches=true; auto b=explicit_size(s,0,250,0,calc_min_100_percent_300px); CHECK(calc_ptr_matches); CHECK(b.repetitions==1); CHECK(b.tracks==1);
    }
}
TEST_CASE("explicit_track_count_accumulation_saturates", "[private-suite][grid]") {
    using namespace ps_grid_test; const std::vector<Def> repeated_tracks(32769,length(1));
    Style s; s.columns={repeat(65535,repeated_tracks),repeat(65535,repeated_tracks),fill({length(1)})};
    auto a=explicit_size(s,0,s.width); CHECK(a.repetitions==0); CHECK(a.tracks==10000);
}
TEST_CASE("test_initialize_grid_tracks", "[private-suite][grid]") {
    using namespace ps_grid_test; Style s; s.gap_width=s.gap_height=20;
    s.columns={length(100),minmax(length(100),fr(2)),fr(1)}; s.auto_columns={automatic(),length(100)};
    auto actual=tracks(s,{3,static_cast<uint16_t>(s.columns.size()),3});
    struct Expected { unsigned kind; Def def; };
    const std::vector<Expected> expected={
        {1,length(0)}, {0,length(100)}, {1,length(20)}, {0,automatic()}, {1,length(20)},
        {0,length(100)}, {1,length(20)}, {0,length(100)}, {1,length(20)},
        {0,minmax(length(100),fr(2))}, {1,length(20)}, {0,fr(1)}, {1,length(20)},
        {0,automatic()}, {1,length(20)}, {0,length(100)}, {1,length(20)}, {0,automatic()}, {1,length(0)}
    };
    REQUIRE(actual.size()==expected.size());
    for(size_t i=0;i<expected.size();++i) {
        CHECK(actual[i].kind==expected[i].kind);
        CHECK(actual[i].min.tag==expected[i].def.min.tag); CHECK(actual[i].min.value==expected[i].def.min.value);
        CHECK(actual[i].max.tag==expected[i].def.max.tag); CHECK(actual[i].max.value==expected[i].def.max.value);
    }
}
TEST_CASE("child_min_max_line_auto", "[private-suite][grid]") {
    using namespace ps_grid_test; PsGridBounds b{};
    REQUIRE(ps_grid_child_bounds(line(5),span(6),6,&b)==0);
    CHECK(b.min==4); CHECK(b.max==10); CHECK(b.span==1);
}
TEST_CASE("child_min_max_line_negative_track", "[private-suite][grid]") {
    using namespace ps_grid_test; PsGridBounds b{};
    REQUIRE(ps_grid_child_bounds(line(-5),span(3),6,&b)==0);
    CHECK(b.min==2); CHECK(b.max==5); CHECK(b.span==1);
}
TEST_CASE("explicit_grid_sizing_with_children", "[private-suite][grid]") {
    using namespace ps_grid_test;
    const PsGridChild children[]={child(0,line(1),span(2),line(2),automatic_placement()),child(1,line(-4),automatic_placement(),line(-2),automatic_placement())};
    Counts c{},r{}; REQUIRE(ps_grid_estimate(6,8,children,2,&c,&r)==0);
    check_counts(c,{0,6,0}); check_counts(r,{0,8,0});
}
TEST_CASE("negative_implicit_grid_sizing", "[private-suite][grid]") {
    using namespace ps_grid_test;
    const PsGridChild children[]={child(0,line(-6),span(2),line(-8),automatic_placement()),child(1,line(4),automatic_placement(),line(3),automatic_placement())};
    Counts c{},r{}; REQUIRE(ps_grid_estimate(4,4,children,2,&c,&r)==0);
    check_counts(c,{1,4,0}); check_counts(r,{3,4,0});
}
// Generated pending placement constructions below.
TEST_CASE("test_auto_placement_in_negative_tracks", "[private-suite][grid]") {
    using namespace ps_grid_test;
    placement(2,2,2, {
        {child(1,line(-5),automatic_placement(),line(1),automatic_placement()),{-2, -1, 0, 1}},
        {child(2,automatic_placement(),automatic_placement(),line(2),automatic_placement()),{-2, -1, 1, 2}},
        {child(3,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{-1, 0, 0, 1}},
    },{2,2,0},{0,2,0});
}
TEST_CASE("test_definite_in_secondary_axis_with_fully_definite_negative", "[private-suite][grid]") {
    using namespace ps_grid_test;
    placement(2,2,0, {
        {child(2,automatic_placement(),automatic_placement(),line(2),automatic_placement()),{0, 1, 1, 2}},
        {child(1,line(-4),automatic_placement(),line(2),automatic_placement()),{-1, 0, 1, 2}},
        {child(3,automatic_placement(),automatic_placement(),line(1),automatic_placement()),{-1, 0, 0, 1}},
    },{1,2,0},{0,2,0});
}
TEST_CASE("test_dense_packing_algorithm", "[private-suite][grid]") {
    using namespace ps_grid_test;
    placement(4,4,2, {
        {child(1,line(2),automatic_placement(),line(1),automatic_placement()),{1, 2, 0, 1}},
        {child(2,span(2),automatic_placement(),automatic_placement(),automatic_placement()),{2, 4, 0, 1}},
        {child(3,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{0, 1, 0, 1}},
    },{0,4,0},{0,4,0});
}
TEST_CASE("test_fixed_in_secondary_axis", "[private-suite][grid]") {
    using namespace ps_grid_test;
    placement(2,2,0, {
        {child(1,span(2),automatic_placement(),line(1),automatic_placement()),{0, 2, 0, 1}},
        {child(2,automatic_placement(),automatic_placement(),line(2),automatic_placement()),{0, 1, 1, 2}},
        {child(3,automatic_placement(),automatic_placement(),line(1),automatic_placement()),{2, 3, 0, 1}},
        {child(4,automatic_placement(),automatic_placement(),line(4),automatic_placement()),{0, 1, 3, 4}},
    },{0,2,1},{0,2,2});
}
TEST_CASE("test_only_auto_placement_column_flow", "[private-suite][grid]") {
    using namespace ps_grid_test;
    placement(2,2,1, {
        {child(1,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{0, 1, 0, 1}},
        {child(2,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{0, 1, 1, 2}},
        {child(3,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{1, 2, 0, 1}},
        {child(4,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{1, 2, 1, 2}},
        {child(5,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{2, 3, 0, 1}},
        {child(6,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{2, 3, 1, 2}},
        {child(7,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{3, 4, 0, 1}},
        {child(8,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{3, 4, 1, 2}},
    },{0,2,2},{0,2,0});
}
TEST_CASE("test_only_auto_placement_row_flow", "[private-suite][grid]") {
    using namespace ps_grid_test;
    placement(2,2,0, {
        {child(1,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{0, 1, 0, 1}},
        {child(2,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{1, 2, 0, 1}},
        {child(3,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{0, 1, 1, 2}},
        {child(4,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{1, 2, 1, 2}},
        {child(5,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{0, 1, 2, 3}},
        {child(6,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{1, 2, 2, 3}},
        {child(7,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{0, 1, 3, 4}},
        {child(8,automatic_placement(),automatic_placement(),automatic_placement(),automatic_placement()),{1, 2, 3, 4}},
    },{0,2,0},{0,2,2});
}
TEST_CASE("test_only_fixed_placement", "[private-suite][grid]") {
    using namespace ps_grid_test;
    placement(2,2,0, {
        {child(1,line(1),automatic_placement(),line(1),automatic_placement()),{0, 1, 0, 1}},
        {child(2,line(-4),automatic_placement(),line(-3),automatic_placement()),{-1, 0, 0, 1}},
        {child(3,line(-3),automatic_placement(),line(-4),automatic_placement()),{0, 1, -1, 0}},
        {child(4,line(3),span(2),line(5),automatic_placement()),{2, 4, 4, 5}},
    },{1,2,2},{1,2,3});
}
TEST_CASE("test_overlarge_placement_is_clamped", "[private-suite][grid]") {
    using namespace ps_grid_test;
    const PsGridChild input=child(0,line(-19005),automatic_placement(),automatic_placement(),automatic_placement());
    PsGridPlaced item{}; Counts c{},r{}; size_t n=0;
    REQUIRE(ps_grid_place(9000,0,&input,1,0,&item,1,&n,&c,&r)==0);
    REQUIRE(n==1); CHECK(item.column_start==-10000); CHECK(item.column_end==-9999);
}
TEST_CASE("test_oversized_item", "[private-suite][grid]") {
    using namespace ps_grid_test;
    placement(2,2,0, {
        {child(1,span(5),automatic_placement(),automatic_placement(),automatic_placement()),{0, 5, 0, 1}},
    },{0,2,3},{0,2,0});
}
TEST_CASE("test_placement_spanning_origin", "[private-suite][grid]") {
    using namespace ps_grid_test;
    placement(2,2,0, {
        {child(1,line(-1),line(-1),line(-1),line(-1)),{2, 3, 2, 3}},
        {child(2,line(-1),span(2),line(-1),span(2)),{2, 4, 2, 4}},
        {child(3,line(-4),line(-4),line(-4),line(-4)),{-1, 0, -1, 0}},
        {child(4,line(-4),span(2),line(-4),span(2)),{-1, 1, -1, 1}},
    },{1,2,2},{1,2,2});
}
TEST_CASE("test_sparse_packing_algorithm", "[private-suite][grid]") {
    using namespace ps_grid_test;
    placement(4,4,0, {
        {child(1,automatic_placement(),span(3),automatic_placement(),automatic_placement()),{0, 3, 0, 1}},
        {child(2,automatic_placement(),span(3),automatic_placement(),automatic_placement()),{0, 3, 1, 2}},
        {child(3,automatic_placement(),span(1),automatic_placement(),automatic_placement()),{3, 4, 1, 2}},
    },{0,4,0},{0,4,0});
}
