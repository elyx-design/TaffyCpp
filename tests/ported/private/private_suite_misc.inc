// Included by private_suite_test.cpp. All expectations and constructions live here;
// the Rust side exports operations, not test runners or baked-in scenarios.
#include "support/private_suite_misc.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace private_misc {
inline std::string diagnostic() {
    std::array<uint8_t,4096> b{}; size_t n=0;
    if(ps_misc_error(b.data(),b.size(),&n)!=0) return "diagnostic unavailable";
    return {reinterpret_cast<const char*>(b.data()),n};
}
inline void ok(int32_t status) { INFO(diagnostic()); REQUIRE(status==0); }
inline void panic(int32_t status, const char* contains="") {
    REQUIRE(status==2); REQUIRE(diagnostic().find(contains)!=std::string::npos);
}
struct Builder {
    uint64_t h=0;
    explicit Builder(size_t n,uint32_t payload_kind=0) {ok(ps_misc_builder_new(n,payload_kind,&h));}
    Builder(const Builder&)=delete;
    ~Builder(){if(h) (void)ps_misc_builder_drop(h);}
    int32_t push(bool back,std::initializer_list<int64_t> v){return ps_misc_builder_push(h,back,v.begin(),v.size());}
    int32_t finish_raw(std::vector<std::vector<int64_t>>& result,size_t& front){
        std::array<int64_t,128> data{};std::array<size_t,129> offsets{};size_t n=0;
        auto status=ps_misc_builder_finish(h,data.data(),data.size(),offsets.data(),offsets.size(),&n,&front);if(status==0)h=0;
        if(status==0){for(size_t i=0;i<n;++i)result.emplace_back(data.begin()+offsets[i],data.begin()+offsets[i+1]);}return status;
    }
    std::pair<std::vector<std::vector<int64_t>>,size_t> finish(){std::vector<std::vector<int64_t>> v;size_t f=0;ok(finish_raw(v,f));return {v,f};}
};
inline std::vector<size_t> balance(const std::vector<float>& s,float limit,float gap,size_t min_lines){
    std::vector<size_t> out(s.size());size_t n=0;ok(ps_misc_balance(s.data(),s.size(),limit,gap,min_lines,out.data(),out.size(),&n));out.resize(n);return out;
}
// Literal translation of the test's independent quadratic dynamic program.
// It intentionally does not call native LineSizes, greedy counting, or readback.
inline std::vector<size_t> naive(const std::vector<float>& items,float limit,float gap,size_t min_lines){
    const auto n=items.size();const double inf=std::numeric_limits<double>::infinity();
    const auto to_size=[](float v){return std::clamp(double(v),0.0,double(std::numeric_limits<float>::max()));};
    std::vector<std::pair<double,bool>> sums;double sum=0;const double g=to_size(gap);
    for(float v:items){double s=to_size(v);sum+=s+g;sums.emplace_back(sum,s==0.0);}
    auto line_size=[&](size_t a,size_t b){return sums[b].first-(a==0?0.0:sums[a-1].first)-g;};
    auto cost=[&](size_t a,size_t b){double s=line_size(a,b);return b>a&&s>double(limit)?inf:s*s;};
    size_t greedy=1,index=0;
    while(index<n){size_t next=index;while(next<n&&line_size(index,next)<=double(limit))++next;if(next==index)next=index+1;index=next;if(index<n)++greedy;}
    size_t lines=std::max(greedy,std::clamp(min_lines,size_t(1),n));
    std::vector<double> errors;errors.reserve(lines*n);
    for(size_t start=0;start<n;++start)errors.push_back(cost(start,n-1));
    auto zero_valid=[&](size_t count,size_t start,size_t end){
        if(!sums[end+1].second)return true;
        for(size_t glued=end+1;glued<=n-count;++glued){if(line_size(start,glued)>double(limit))break;if(std::isfinite(errors[(count-2)*n+glued+1]))return false;}return true;
    };
    for(size_t count=2;count<=lines;++count){size_t row=(count-1)*n;for(size_t start=0;start<n;++start){double best=inf;size_t end=start;while(end+count<=n){double c=cost(start,end);if(c==inf)break;if(zero_valid(count,start,end)){double e=c+errors[row-n+end+1];if(e<best)best=e;}++end;}errors.push_back(best);}}
    REQUIRE(std::isfinite(errors[(lines-1)*n]));std::vector<size_t> counts;size_t start=0;
    for(size_t after=lines;after-->0;){double target=errors[after*n+start];size_t end=n-1-after;for(;;){if(cost(start,end)!=inf&&(after==0||zero_valid(after+1,start,end))){double rest=after==0?0.0:errors[(after-1)*n+end+1];if(cost(start,end)+rest==target)break;}REQUIRE(end>start);--end;}counts.push_back(end-start+1);start=end+1;}
    REQUIRE(start==n);return counts;
}
inline float offset(float start,float end,uint32_t edge,uint32_t safety,uint32_t fallback,float width,float left=0,float right=0){float v=0;ok(ps_misc_static_offset(start,end,edge,safety,fallback,width,left,right,&v));return v;}
inline uint32_t direction(uint32_t v){uint32_t r=0;ok(ps_misc_flex_direction(v,&r));return r;}
inline size_t tag(uint32_t kind){size_t t=0;ok(ps_misc_compact_tag(kind,&t));return t;}
inline std::array<size_t,4> bits(){std::array<size_t,4>b{};ok(ps_misc_compact_bits(b.data(),b.size()));return b;}
struct CompactResult {uint32_t flags;const void* pointer;};
inline CompactResult compact_result(uint32_t kind,float value,size_t raw,const void* pointer){
    size_t t=0;uint32_t f=0;const void* extracted=nullptr;
    ok(ps_misc_compact(kind,value,raw,pointer,&t,&f,&extracted));
    if(kind==13)REQUIRE(t==raw);else if(kind<11)REQUIRE(t==tag(kind));return {f,extracted};
}
inline uint32_t compact(uint32_t kind,float value=10.0f,size_t raw=0){
    alignas(8) std::array<unsigned char,8> storage{};
    auto result=compact_result(kind,value,raw,storage.data());
    if(kind==11||kind==12)REQUIRE(result.pointer==static_cast<const void*>(storage.data()));
    return result.flags;
}
inline float from_edge(float anchor,uint32_t edge,float width){float v=0;ok(ps_misc_static_from_edge(anchor,edge,width,0,0,&v));return v;}
inline bool flag(uint32_t flags,unsigned bit){return (flags&(uint32_t(1)<<bit))!=0;}
inline size_t type_size(uint32_t type){size_t n=0;ok(ps_misc_type_size(type,&n));return n;}
struct StyleValue {
    uint64_t h=0;explicit StyleValue(uint32_t factory){ok(ps_misc_style_new(factory,&h));}
    StyleValue(const StyleValue&)=delete;~StyleValue(){if(h)(void)ps_misc_style_drop(h);}
    void property(uint32_t p,float v=0){ok(ps_misc_style_property(h,p,v));}
    bool equal(const StyleValue& other)const{uint32_t b=0;ok(ps_misc_style_equal(h,other.h,&b));return b!=0;}
};
struct JsonValue {std::string text;uint32_t keyword,safety;};
inline JsonValue json(uint32_t content,uint32_t operation,uint32_t keyword,uint32_t safety,const std::string& input=""){
    std::array<uint8_t,256> out{};size_t n=0;uint32_t k=99,s=99;
    ok(ps_misc_alignment_json(content,operation,keyword,safety,reinterpret_cast<const uint8_t*>(input.data()),input.size(),out.data(),out.size(),&n,&k,&s));
    return {{reinterpret_cast<const char*>(out.data()),n},k,s};
}
inline void json_case(uint32_t content,uint32_t keyword,uint32_t safety,const char* expected){
    auto encoded=json(content,0,keyword,safety);REQUIRE(encoded.text==expected);
    auto decoded=json(content,1,0,0,expected);REQUIRE(decoded.keyword==keyword);REQUIRE(decoded.safety==safety);
}
inline void invalid_json(uint32_t content){
    const std::string input="\"NotAVariant\"";std::array<uint8_t,256> out{};size_t n=0;uint32_t k=0,s=0;
    REQUIRE(ps_misc_alignment_json(content,1,0,0,reinterpret_cast<const uint8_t*>(input.data()),input.size(),out.data(),out.size(),&n,&k,&s)==1);
}
}

TEST_CASE("private_misc::front_items_keep_their_order_and_back_items_are_reversed","[private-suite][misc]"){
    private_misc::Builder b(6);for(int64_t v=0;v<6;++v)private_misc::ok(b.push(v%2!=0,{v}));auto result=b.finish();
    REQUIRE(result.first==std::vector<std::vector<int64_t>>{{0},{2},{4},{5},{3},{1}});REQUIRE(result.second==3);
}
TEST_CASE("private_misc::all_items_pushed_to_one_end","[private-suite][misc]"){
    private_misc::Builder a(3);for(int64_t v=0;v<3;++v)private_misc::ok(a.push(false,{v}));
    REQUIRE(a.finish()==std::make_pair(std::vector<std::vector<int64_t>>{{0},{1},{2}},size_t(3)));
    private_misc::Builder b(3);for(int64_t v=0;v<3;++v)private_misc::ok(b.push(true,{v}));
    REQUIRE(b.finish()==std::make_pair(std::vector<std::vector<int64_t>>{{2},{1},{0}},size_t(0)));
}
TEST_CASE("private_misc::zero_capacity","[private-suite][misc]"){
    private_misc::Builder b(0,2);auto [v,n]=b.finish();REQUIRE(v.empty());REQUIRE(n==0);
}
TEST_CASE("private_misc::owned_items_are_moved_into_the_vec","[private-suite][misc]"){
    private_misc::Builder b(2,1);private_misc::ok(b.push(true,{1,2}));private_misc::ok(b.push(false,{3}));auto [v,n]=b.finish();
    REQUIRE(v==std::vector<std::vector<int64_t>>{{3},{1,2}});REQUIRE(n==1);
}
TEST_CASE("private_misc::pushing_too_many_items_to_the_front_panics_in_into_vec","[private-suite][misc]"){
    private_misc::Builder b(1);private_misc::ok(b.push(true,{1}));private_misc::ok(b.push(false,{2}));std::vector<std::vector<int64_t>> v;size_t n=0;
    private_misc::panic(b.finish_raw(v,n),"must equal its capacity");
}
TEST_CASE("private_misc::pushing_too_many_items_to_the_back_panics_in_into_vec","[private-suite][misc]"){
    private_misc::Builder b(1);private_misc::ok(b.push(false,{1}));private_misc::ok(b.push(true,{2}));std::vector<std::vector<int64_t>> v;size_t n=0;
    private_misc::panic(b.finish_raw(v,n),"must equal its capacity");
}
TEST_CASE("private_misc::push_front_out_of_bounds_panics","[private-suite][misc]"){
    private_misc::Builder b(0);private_misc::panic(b.push(false,{1}));
}
TEST_CASE("private_misc::push_back_out_of_bounds_panics","[private-suite][misc]"){
    private_misc::Builder b(1);private_misc::ok(b.push(true,{1}));private_misc::panic(b.push(true,{2}));
}
TEST_CASE("private_misc::into_vec_before_full_panics","[private-suite][misc]"){
    private_misc::Builder b(2);private_misc::ok(b.push(false,{1}));std::vector<std::vector<int64_t>> v;size_t n=0;
    private_misc::panic(b.finish_raw(v,n),"must equal its capacity");
}
TEST_CASE("private_misc::zero_sized_item_rule","[private-suite][misc]"){
    REQUIRE(private_misc::balance({70,0,20},100,10,1)==std::vector<size_t>{2,1});
    REQUIRE(private_misc::balance({150,0,30},100,0,1)==std::vector<size_t>{1,2});
    REQUIRE(private_misc::balance({70,0,0},100,0,2)==std::vector<size_t>{2,1});
}
TEST_CASE("private_misc::matches_naive_dp","[private-suite][misc]"){
    uint64_t state=UINT64_C(0x243F6A8885A308D3);auto random=[&](uint32_t bound){state^=state<<13;state^=state>>7;state^=state<<17;return uint32_t(state>>32)%bound;};
    for(size_t c=0;c<5000;++c){size_t n=c%20==0?random(60)+1:random(15)+1;std::vector<float> items;for(size_t i=0;i<n;++i){float value;switch(random(8)){case 0:case 1:value=0;break;case 2:value=-10;break;default:value=float(random(8))*25;break;}items.push_back(value);}float limit=random(5)==0?std::numeric_limits<float>::infinity():float(random(10))*30;float gap=float(random(4))*5;size_t minimum=random(uint32_t(n)+2);
        CAPTURE(c,items,limit,gap,minimum);auto expected=private_misc::naive(items,limit,gap,minimum);REQUIRE(private_misc::balance(items,limit,gap,minimum)==expected);
    }
}
TEST_CASE("private_misc::hidden_layout_should_hide_recursively","[private-suite][misc]"){
    // Index order: grandchild_00, grandchild_01, child_00, grandchild_02,
    // child_01, root. Every style has 50x50 size, root alone is display:none.
    const int32_t parents[]={2,2,5,4,5,-1};const uint32_t display[]={1,1,1,1,1,0};const float sizes[]={50,50,50,50,50,50};std::array<float,24> result{};
    std::array<size_t,6> child_counts{};
    private_misc::ok(ps_misc_hidden_with_counts(parents,display,sizes,sizes,6,5,result.data(),result.size(),child_counts.data()));
    REQUIRE(child_counts==std::array<size_t,6>{0,0,2,0,1,2});
    for(size_t node:{size_t(5),size_t(2),size_t(4),size_t(0),size_t(1),size_t(3)}){CAPTURE(node);REQUIRE(result[node*4]==0);REQUIRE(result[node*4+1]==0);REQUIRE(result[node*4+2]==0);REQUIRE(result[node*4+3]==0);}
}
TEST_CASE("private_misc::start_alignment","[private-suite][misc]"){
    REQUIRE(private_misc::offset(10,110,0,2,0,20)==10);REQUIRE(private_misc::offset(10,110,0,2,0,20,5,15)==15);
}
TEST_CASE("private_misc::end_alignment","[private-suite][misc]"){
    REQUIRE(private_misc::offset(10,110,1,2,1,20)==90);REQUIRE(private_misc::offset(10,110,1,2,1,20,5,15)==75);
}
TEST_CASE("private_misc::center_alignment","[private-suite][misc]"){
    REQUIRE(private_misc::offset(10,110,2,2,2,20)==50);REQUIRE(private_misc::offset(10,110,2,2,2,20,5,15)==45);
}
TEST_CASE("private_misc::safe_alignment_falls_back_only_on_overflow","[private-suite][misc]"){
    REQUIRE(private_misc::offset(10,110,1,1,0,20)==90);REQUIRE(private_misc::offset(10,110,1,1,0,100)==10);
    REQUIRE(private_misc::offset(10,110,1,1,0,120)==10);REQUIRE(private_misc::offset(10,110,1,1,0,90,5,15)==15);
}
TEST_CASE("private_misc::unsafe_alignment_never_falls_back","[private-suite][misc]"){
    REQUIRE(private_misc::offset(10,110,1,2,0,120)==-10);
}
TEST_CASE("private_misc::degenerate_area","[private-suite][misc]"){
    REQUIRE(private_misc::from_edge(40,0,20)==40);REQUIRE(private_misc::from_edge(40,1,20)==20);
}
TEST_CASE("private_misc::flex_direction_is_row","[private-suite][misc]"){
    REQUIRE((private_misc::direction(0)&1)!=0);REQUIRE((private_misc::direction(1)&1)!=0);REQUIRE((private_misc::direction(2)&1)==0);REQUIRE((private_misc::direction(3)&1)==0);
}
TEST_CASE("private_misc::flex_direction_is_column","[private-suite][misc]"){
    REQUIRE((private_misc::direction(0)&2)==0);REQUIRE((private_misc::direction(1)&2)==0);REQUIRE((private_misc::direction(2)&2)!=0);REQUIRE((private_misc::direction(3)&2)!=0);
}
TEST_CASE("private_misc::flex_direction_is_reverse","[private-suite][misc]"){
    REQUIRE((private_misc::direction(0)&4)==0);REQUIRE((private_misc::direction(1)&4)!=0);REQUIRE((private_misc::direction(2)&4)==0);REQUIRE((private_misc::direction(3)&4)!=0);
}
TEST_CASE("private_misc::align_content_keyword_reversed_swaps_start_end","[private-suite][misc]"){
    const std::array<uint32_t,10> reversed={0,2,1,4,3,5,2,7,8,9};
    for(uint32_t k:{1u,2u,3u,4u,6u,0u,5u,7u,8u,9u}){uint32_t actual=99;private_misc::ok(ps_misc_reverse_content(k,&actual));REQUIRE(actual==reversed[k]);}
}
TEST_CASE("private_misc::align_types_within_size_budget","[private-suite][misc]"){
    // These are native representation budget checks, NOT C++/Rust byte identity.
    REQUIRE(private_misc::type_size(18)<=2);REQUIRE(private_misc::type_size(17)<=2);REQUIRE(private_misc::type_size(19)<=3);
}
TEST_CASE("private_misc::tags_are_unique_and_fit_in_a_byte","[private-suite][misc]"){
    std::vector<size_t> all;for(uint32_t kind=0;kind<13;++kind)all.push_back(private_misc::tag(kind));
    REQUIRE(std::all_of(all.begin(),all.end(),[](size_t t){return t<=0xff;}));std::sort(all.begin(),all.end());all.erase(std::unique(all.begin(),all.end()),all.end());REQUIRE(all.size()==13);
}
TEST_CASE("private_misc::non_pointer_tags_have_non_pointer_bit_set","[private-suite][misc]"){
#ifdef TAFFY_NATIVE_CPP_REPRESENTATION
    // Rust packed-tag assertions are N/A; separately counted native domain checks.
    for(uint32_t k=0;k<13;++k){
        alignas(8) std::array<unsigned char,8> storage{};
        auto result=private_misc::compact_result(k,.5f,0,storage.data());
        REQUIRE(private_misc::flag(result.flags,5)==(k==11||k==12));
        REQUIRE(result.pointer==((k==11||k==12)?static_cast<const void*>(storage.data()):nullptr));
    }
#else
    auto b=private_misc::bits();for(uint32_t k=0;k<11;++k)REQUIRE((private_misc::tag(k)&b[0])!=0);REQUIRE((private_misc::tag(11)&b[0])==0);
#endif
}
TEST_CASE("private_misc::keyword_bits_match_predicates","[private-suite][misc]"){
#ifndef TAFFY_NATIVE_CPP_REPRESENTATION
    auto b=private_misc::bits();
#endif
    for(uint32_t k=0;k<11;++k){size_t t=private_misc::tag(k);
#ifdef TAFFY_NATIVE_CPP_REPRESENTATION
        uint32_t flags=private_misc::compact(k,0);
#else
        uint32_t flags=private_misc::compact(13,0,t);
#endif
        bool plain=k==0||k==9;bool sizing=k==3||k==4||k==5||k==6||k==7||k==10;
        CAPTURE(k,t);
#ifndef TAFFY_NATIVE_CPP_REPRESENTATION
        REQUIRE(((t&b[1])==0)==plain);
#endif
        REQUIRE(private_misc::flag(flags,0)==plain);REQUIRE(private_misc::flag(flags,1)==sizing);}
#ifdef TAFFY_NATIVE_CPP_REPRESENTATION
    auto calc=private_misc::compact(11,0),fit=private_misc::compact(12,0);
    REQUIRE(private_misc::flag(calc,3));REQUIRE(private_misc::flag(fit,4));
    REQUIRE(private_misc::flag(fit,5));REQUIRE(private_misc::flag(fit,2));
#else
    REQUIRE((private_misc::tag(11)&b[1])==0);REQUIRE((private_misc::tag(12)&b[1])!=0);REQUIRE((private_misc::tag(12)&b[0])==0);REQUIRE((private_misc::tag(12)&b[2])!=0);
#endif
}
TEST_CASE("private_misc::percentage_bit_matches_uses_percentage","[private-suite][misc]"){
#ifdef TAFFY_NATIVE_CPP_REPRESENTATION
    for(uint32_t k:{11u,12u})REQUIRE(private_misc::flag(private_misc::compact(k,0),2));
#else
    auto b=private_misc::bits();for(uint32_t k=0;k<9;++k)REQUIRE((private_misc::tag(k)&b[2])==0);for(uint32_t k:{9u,10u})REQUIRE((private_misc::tag(k)&b[2])!=0);REQUIRE((private_misc::tag(11)&b[2])!=0);
#endif
    for(uint32_t k=0;k<11;++k){float value=k==2?1.f:(k==9||k==10?.5f:10.f);REQUIRE(private_misc::flag(private_misc::compact(k,value),2)==(k==9||k==10));}
}
TEST_CASE("private_misc::fit_content_calc_round_trips_pointer","[private-suite][misc]"){
    alignas(8) std::array<unsigned char,8> storage{};
    auto result=private_misc::compact_result(12,0,0,storage.data());uint32_t value=result.flags;REQUIRE(private_misc::flag(value,4));REQUIRE(private_misc::flag(value,5));REQUIRE_FALSE(private_misc::flag(value,3));REQUIRE(private_misc::flag(value,6));REQUIRE(private_misc::flag(value,1));REQUIRE(private_misc::flag(value,7));REQUIRE(private_misc::flag(value,8));REQUIRE(private_misc::flag(value,9));REQUIRE(private_misc::flag(value,2));REQUIRE_FALSE(private_misc::flag(value,0));REQUIRE(result.pointer==static_cast<const void*>(storage.data()));
    uint32_t calc=private_misc::compact(11,0);REQUIRE(private_misc::flag(calc,3));REQUIRE(private_misc::flag(calc,5));REQUIRE_FALSE(private_misc::flag(calc,4));REQUIRE_FALSE(private_misc::flag(calc,6));REQUIRE_FALSE(private_misc::flag(calc,1));
}
TEST_CASE("private_misc::calc_round_trips_pointer","[private-suite][misc]"){
    alignas(8) std::array<unsigned char,8> storage{};
    auto result=private_misc::compact_result(11,42,0,storage.data());uint32_t value=result.flags;REQUIRE(private_misc::flag(value,3));REQUIRE(private_misc::flag(value,2));REQUIRE_FALSE(private_misc::flag(value,0));REQUIRE_FALSE(private_misc::flag(value,10));REQUIRE_FALSE(private_misc::flag(value,7));REQUIRE(result.pointer==static_cast<const void*>(storage.data()));
}
TEST_CASE("private_misc::defaults_match","[private-suite][misc]"){
    private_misc::StyleValue constant(1),defaults(0),old_defaults(0);
    // Construct EVERY property in the old struct literal, independently of Style
    // defaults. Enum/list fields use their own Default; scalar/box fields below
    // are the explicit original values. dummy is zero-sized PhantomData.
    for(uint32_t p=0;p<44;++p){float value=0;switch(p){case 14:case 21:case 28:case 29:case 30:case 31:case 32:value=1;break;default:break;}old_defaults.property(p,value);}
    REQUIRE(constant.equal(defaults));REQUIRE(constant.equal(old_defaults));
    // Sanity: equality is semantic and the construction operation is not a no-op.
    old_defaults.property(27,7);REQUIRE_FALSE(constant.equal(old_defaults));
}
TEST_CASE("private_misc::style_sizes","[private-suite][misc]"){
#ifdef TAFFY_NATIVE_CPP_REPRESENTATION
    // Independent LP64 upper budgets, not Rust exact sizes or measured returns.
    // IDs32..35 are Rust-only Arc<str> specializations, not native Style types.
    const std::array<size_t,32> budgets={1,1,1,1,4,32,32,32,64,64,64,128,128,128,1,1,1,2,2,3,1,1,1,32,32,64,32,32,160,64,128,2048};
    for(uint32_t type=0;type<budgets.size();++type){CAPTURE(type);auto bytes=private_misc::type_size(type);REQUIRE(bytes>0);REQUIRE(bytes<=budgets[type]);}
#else
    // Exact native Rust budgets from source, no dependency on C++ object layouts.
    const std::array<size_t,36> expected={1,1,1,1,4,8,8,8,16,16,16,32,32,32,1,1,1,2,2,2,1,1,1,8,8,16,24,24,56,32,64,560,56,24,48,528};
    for(uint32_t type=0;type<expected.size();++type){CAPTURE(type);REQUIRE(private_misc::type_size(type)==expected[type]);}
#endif
}
TEST_CASE("private_misc::serde_align_items_round_trip","[private-suite][misc][serde]"){
    uint32_t enabled=0;private_misc::ok(ps_misc_has_serde(&enabled));if(!enabled)SKIP("native serde feature disabled");
    private_misc::json_case(0,0,0,"\"Normal\"");private_misc::json_case(0,1,0,"\"Start\"");private_misc::json_case(0,2,0,"\"End\"");private_misc::json_case(0,3,0,"\"FlexStart\"");private_misc::json_case(0,4,0,"\"FlexEnd\"");private_misc::json_case(0,5,0,"\"Center\"");private_misc::json_case(0,6,0,"\"Baseline\"");private_misc::json_case(0,7,0,"\"Stretch\"");
    private_misc::json_case(0,1,1,"\"SafeStart\"");private_misc::json_case(0,2,1,"\"SafeEnd\"");private_misc::json_case(0,3,1,"\"SafeFlexStart\"");private_misc::json_case(0,4,1,"\"SafeFlexEnd\"");private_misc::json_case(0,5,1,"\"SafeCenter\"");private_misc::json_case(0,8,0,"\"SelfStart\"");private_misc::json_case(0,9,0,"\"SelfEnd\"");private_misc::json_case(0,8,1,"\"SafeSelfStart\"");private_misc::json_case(0,9,1,"\"SafeSelfEnd\"");private_misc::invalid_json(0);
}
TEST_CASE("private_misc::serde_align_content_round_trip","[private-suite][misc][serde]"){
    uint32_t enabled=0;private_misc::ok(ps_misc_has_serde(&enabled));if(!enabled)SKIP("native serde feature disabled");
    private_misc::json_case(1,0,0,"\"Normal\"");private_misc::json_case(1,1,0,"\"Start\"");private_misc::json_case(1,2,0,"\"End\"");private_misc::json_case(1,3,0,"\"FlexStart\"");private_misc::json_case(1,4,0,"\"FlexEnd\"");private_misc::json_case(1,5,0,"\"Center\"");private_misc::json_case(1,6,0,"\"Stretch\"");private_misc::json_case(1,7,0,"\"SpaceBetween\"");private_misc::json_case(1,8,0,"\"SpaceEvenly\"");private_misc::json_case(1,9,0,"\"SpaceAround\"");
    private_misc::json_case(1,1,1,"\"SafeStart\"");private_misc::json_case(1,2,1,"\"SafeEnd\"");private_misc::json_case(1,3,1,"\"SafeFlexStart\"");private_misc::json_case(1,4,1,"\"SafeFlexEnd\"");private_misc::json_case(1,5,1,"\"SafeCenter\"");private_misc::invalid_json(1);
}
// Supplemental regressions below are not additional source-suite ports.
// Expected failures exercise the same C++ comparisons as the scoped ports.
// Abort on setup errors so only a wrong comparison can satisfy the required-failure gate.
TEST_CASE("private_misc::injected_wrong_pointer_fails_identity_assertion","[private-suite][misc][failure_injection][.]"){
    alignas(8) std::array<unsigned char,8> storage{};
    size_t tag=0;uint32_t flags=0;const void* extracted=nullptr;
    if(ps_misc_inject_fault(1)!=0 || ps_misc_compact(11,0,0,storage.data(),&tag,&flags,&extracted)!=0)std::abort();
    REQUIRE(extracted==static_cast<const void*>(storage.data()));
}
TEST_CASE("private_misc::injected_wrong_constructor_fails_offset_assertion","[private-suite][misc][failure_injection][.]"){
    float offset=0;
    if(ps_misc_inject_fault(2)!=0 || ps_misc_static_from_edge(40,0,20,0,0,&offset)!=0)std::abort();
    REQUIRE(offset==40);
}
TEST_CASE("private_misc::calc_pointer_fault_is_observable_in_cpp","[private-suite][misc][adversarial]"){
    alignas(8) std::array<unsigned char,8> first{},second{};
    for(uint32_t kind:{11u,12u}){
        auto a=private_misc::compact_result(kind,0,0,first.data());
        auto b=private_misc::compact_result(kind,0,0,second.data());
        REQUIRE(a.pointer==static_cast<const void*>(first.data()));
        REQUIRE(b.pointer==static_cast<const void*>(second.data()));
        REQUIRE(a.pointer!=b.pointer);
        private_misc::ok(ps_misc_inject_fault(1));
        auto broken=private_misc::compact_result(kind,0,0,first.data());
        REQUIRE(broken.flags==a.flags); // Predicate bits cannot hide wrong identity.
        REQUIRE(broken.pointer!=static_cast<const void*>(first.data()));
        REQUIRE(private_misc::compact_result(kind,0,0,first.data()).pointer==static_cast<const void*>(first.data()));
    }
}
TEST_CASE("private_misc::from_edge_constructor_fault_is_observable_in_cpp","[private-suite][misc][adversarial]"){
    for(uint32_t edge:{0u,1u}){
        const float expected=edge==0?40.f:20.f;
        REQUIRE(private_misc::from_edge(40,edge,20)==expected);
        private_misc::ok(ps_misc_inject_fault(2));
        REQUIRE(private_misc::from_edge(40,edge,20)!=expected);
        REQUIRE(private_misc::from_edge(40,edge,20)==expected);
    }
}
TEST_CASE("private_misc::hidden_constructor_links_are_observable_in_cpp","[private-suite][misc][adversarial]"){
    // Deliberately non-topological input order: constructor dispatch must not
    // depend on the original fixture's child-before-parent index ordering.
    const int32_t parents[]={-1,0,1};const uint32_t displays[]={0,1,1};const float sizes[]={50,50,50};
    std::array<float,12> layouts{};std::array<size_t,3> counts{};
    const std::array<size_t,3> expected={1,1,0};
    auto construct=[&]{private_misc::ok(ps_misc_hidden_with_counts(parents,displays,sizes,sizes,3,0,layouts.data(),layouts.size(),counts.data()));};
    construct();REQUIRE(counts==expected);
    private_misc::ok(ps_misc_inject_fault(3));construct();REQUIRE(counts!=expected);
    // Zero layout assertions alone cannot detect detached, initially-zero leaves.
    REQUIRE(std::all_of(layouts.begin(),layouts.end(),[](float v){return v==0;}));
    construct();REQUIRE(counts==expected);
}
TEST_CASE("private_misc::builder_panics_quarantine_only_the_affected_handle","[private-suite][misc][adversarial]"){
    for(uint32_t kind:{0u,1u,2u}){
        private_misc::Builder affected(1,kind),unrelated(1,kind);
        private_misc::ok(ps_misc_inject_fault(4));private_misc::panic(affected.push(false,{7}),"injected builder panic");
        REQUIRE(affected.push(false,{8})==1);
        std::vector<std::vector<int64_t>> values;size_t front=0;
        REQUIRE(affected.finish_raw(values,front)==1); // Must not consume tombstone.
        private_misc::ok(ps_misc_builder_drop(affected.h));affected.h=0;
        private_misc::ok(unrelated.push(false,{42}));
        REQUIRE(unrelated.finish().first==std::vector<std::vector<int64_t>>{{42}});
    }
    // Real upstream mutation panics and a consuming finish panic obey the same
    // quarantine contract, not only our injected failures.
    for(bool back:{false,true}){
        private_misc::Builder affected(0),unrelated(1);
        private_misc::panic(affected.push(back,{1}));REQUIRE(affected.push(back,{2})==1);
        private_misc::ok(ps_misc_builder_drop(affected.h));affected.h=0;
        private_misc::ok(unrelated.push(false,{9}));REQUIRE(unrelated.finish().first==std::vector<std::vector<int64_t>>{{9}});
    }
    private_misc::Builder unfinished(1);std::vector<std::vector<int64_t>> values;size_t front=0;
    private_misc::panic(unfinished.finish_raw(values,front),"must equal its capacity");
    REQUIRE(unfinished.push(false,{1})==1);
    const auto h=unfinished.h;
    REQUIRE(unfinished.finish_raw(values,front)==1);
    private_misc::ok(ps_misc_builder_drop(h));unfinished.h=0;
    REQUIRE(ps_misc_builder_drop(h)==1);
}
TEST_CASE("private_misc::style_panics_quarantine_operands_not_unrelated_styles","[private-suite][misc][adversarial]"){
    private_misc::StyleValue affected(0),other(0),unrelated(0);
    private_misc::ok(ps_misc_inject_fault(5));private_misc::panic(ps_misc_style_property(affected.h,27,7),"injected style property panic");
    REQUIRE(ps_misc_style_property(affected.h,27,8)==1);
    uint32_t equal=99;REQUIRE(ps_misc_style_equal(affected.h,other.h,&equal)==1);
    REQUIRE(other.equal(unrelated));
    private_misc::ok(ps_misc_style_drop(affected.h));affected.h=0;
    private_misc::StyleValue lhs(0),rhs(0);
    private_misc::ok(ps_misc_inject_fault(6));private_misc::panic(ps_misc_style_equal(lhs.h,rhs.h,&equal),"injected style equality panic");
    REQUIRE(ps_misc_style_property(lhs.h,27,1)==1);REQUIRE(ps_misc_style_property(rhs.h,27,1)==1);
    REQUIRE(ps_misc_style_equal(lhs.h,lhs.h,&equal)==1);REQUIRE(ps_misc_style_equal(rhs.h,rhs.h,&equal)==1);
    private_misc::ok(ps_misc_style_drop(lhs.h));lhs.h=0;private_misc::ok(ps_misc_style_drop(rhs.h));rhs.h=0;
    other.property(27,2);unrelated.property(27,2);REQUIRE(other.equal(unrelated));
}
TEST_CASE("private_misc::adversarial_abi_errors_are_not_panics","[private-suite][misc][adversarial]"){
    uint32_t u=0;size_t n=0;float x=0;uint64_t h=0;
    REQUIRE(ps_misc_flex_direction(99,&u)==1);REQUIRE(ps_misc_flex_direction(0,nullptr)==1);
    REQUIRE(ps_misc_reverse_content(99,&u)==1);REQUIRE(ps_misc_type_size(99,&n)==1);
    const void* extracted=nullptr;
    REQUIRE(ps_misc_compact_tag(99,&n)==1);REQUIRE(ps_misc_compact(13,0,0,nullptr,&n,&u,&extracted)==1);
    REQUIRE(ps_misc_static_offset(0,10,99,0,0,1,0,0,&x)==1);
    REQUIRE(ps_misc_balance(nullptr,0,100,0,1,&n,1,&n)==1);
    REQUIRE(ps_misc_builder_new(100001,0,&h)==1);REQUIRE(ps_misc_builder_push(0,0,nullptr,0)==1);
    REQUIRE(ps_misc_builder_drop(0)==1);REQUIRE(ps_misc_style_equal(0,0,&u)==1);
    const int32_t parents[]={1,0};const uint32_t displays[]={1,1};const float sizes[]={1,1};float output[8]{};
    REQUIRE(ps_misc_hidden(parents,displays,sizes,sizes,2,0,output,8)==1);
    private_misc::Builder b(1);private_misc::ok(b.push(true,{1}));private_misc::panic(b.push(true,{2}));
    // Panic poisoning must not disable subsequent valid operations.
    private_misc::Builder after(1);private_misc::ok(after.push(false,{42}));REQUIRE(after.finish().first==std::vector<std::vector<int64_t>>{{42}});
}
