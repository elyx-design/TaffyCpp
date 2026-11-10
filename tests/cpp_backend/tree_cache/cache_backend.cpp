// Cache ABI primitives. Internal observations use the narrow native detail
// access seam, never a frontend shadow cache or reconstructed operation log.
#include "../../ported/tree/cache_abi.h"
#include "conversions.hpp"
#include "cache_extensions.h"
#include "../../../src/cache_access.hpp"
using namespace cpp_tree_abi;
using CacheAccess = taffy::detail::CacheAccess;
struct TsCache { taffy::Cache value; };
struct TsCacheSnapshot { CacheAccess::MeasureEntriesSnapshot entries; };
namespace {
template<class F> int32_t call(F&& fn) noexcept { try { fn();return TS_CACHE_OK; } catch(const Failure&){return TS_CACHE_INVALID;}catch(...){return TS_CACHE_PANIC;} }
std::optional<float> opt(TsCacheOptional v){return boolean(v.present)?std::optional<float>(v.value):std::nullopt;}
TsCacheOptional opt(std::optional<float> v){return {v.value_or(0),uint32_t(v.has_value())};}
taffy::AvailableSpace av(TsCacheAvailable v){switch(v.kind){case TS_CACHE_DEFINITE:return taffy::AvailableSpace::definite(v.value);case TS_CACHE_MIN_CONTENT:return taffy::AvailableSpace::min_content();case TS_CACHE_MAX_CONTENT:return taffy::AvailableSpace::max_content();}throw Failure{ORACLE_INVALID,"invalid available tag"};}
TsCacheAvailable av(taffy::AvailableSpace v){return {v.into_option().value_or(0),v.is_definite()?0u:v.is_min_content()?1u:2u};}
taffy::LayoutInput convert(const TsCacheInput& v){require(v.run_mode<=2 && v.sizing_mode<=1 && v.axis<=2);return {taffy::RunMode(v.run_mode),taffy::SizingMode(v.sizing_mode),taffy::RequestedAxis(v.axis),{opt(v.known_dimensions[0]),opt(v.known_dimensions[1])},{boolean(v.known_dimensions_are_definite[0]),boolean(v.known_dimensions_are_definite[1])},{opt(v.parent_size[0]),opt(v.parent_size[1])},{av(v.available_space[0]),av(v.available_space[1])},{boolean(v.vertical_margins_are_collapsible[0]),boolean(v.vertical_margins_are_collapsible[1])}};}
TsCacheInput convert(const taffy::LayoutInput& v){return {uint32_t(v.run_mode),uint32_t(v.sizing_mode),uint32_t(v.axis),{opt(v.known_dimensions.width),opt(v.known_dimensions.height)},{uint32_t(v.known_dimensions_are_definite.width),uint32_t(v.known_dimensions_are_definite.height)},{opt(v.parent_size.width),opt(v.parent_size.height)},{av(v.available_space.width),av(v.available_space.height)},{uint32_t(v.vertical_margins_are_collapsible.start),uint32_t(v.vertical_margins_are_collapsible.end)}};}
taffy::Position position(uint32_t v){switch(v){case 0:return taffy::Position::Relative;case 1:return taffy::Position::Absolute;case 2:return taffy::Position::Fixed;case 3:return taffy::Position::Static;case 4:return taffy::Position::Sticky;}throw Failure{ORACLE_INVALID,"invalid position"};}
uint32_t position(taffy::Position v){switch(v){case taffy::Position::Relative:return 0;case taffy::Position::Absolute:return 1;case taffy::Position::Fixed:return 2;case taffy::Position::Static:return 3;case taffy::Position::Sticky:return 4;}throw Failure{ORACLE_INVALID,"invalid position"};}
taffy::AlignmentSafety safety(uint32_t v){require(v<=2);return v==1?taffy::AlignmentSafety::Safe:v==2?taffy::AlignmentSafety::Unsafe:taffy::AlignmentSafety::Default;}
uint32_t safety(taffy::AlignmentSafety v){return v==taffy::AlignmentSafety::Safe?1u:v==taffy::AlignmentSafety::Unsafe?2u:0u;}
taffy::AxisStaticPosition convert(TsCacheStaticPosition v){require(v.keyword<=2 && v.fallback<=2);return {{v.area_start,v.area_end},{taffy::AxisStaticEdge(v.keyword),safety(v.safety),taffy::AxisStaticEdge(v.fallback)}};}
TsCacheStaticPosition convert(taffy::AxisStaticPosition v){return {v.area.start,v.area.end,uint32_t(v.align.keyword),safety(v.align.safety),uint32_t(v.align.fallback)};}
taffy::LayoutOutput convert(const TsCacheOutput& v){
    require(v.oof_count<=v.oof_capacity); auto candidates=span(v.oof_candidates,size_t(v.oof_count));
    taffy::LayoutOutput out;
    out.size={v.size[0],v.size[1]};out.scrollable_overflow_rect={v.scrollable_overflow_rect[0],v.scrollable_overflow_rect[1],v.scrollable_overflow_rect[2],v.scrollable_overflow_rect[3]};out.baselines={opt(v.baselines[0]),opt(v.baselines[1])};out.top_margin={v.top_margin[0],v.top_margin[1]};out.bottom_margin={v.bottom_margin[0],v.bottom_margin[1]};out.margins_can_collapse_through=boolean(v.margins_can_collapse_through);
    for(const auto& c:candidates)out.oof_candidates.push({{c.node},c.order,position(c.position),{convert(c.static_position[0]),convert(c.static_position[1])}});
    if(boolean(v.oof_positioning_area_present))out.oof_positioning_area=taffy::OofPositioningArea{{v.oof_positioning_size[0],v.oof_positioning_size[1]},{v.oof_positioning_offset[0],v.oof_positioning_offset[1]}};
    return out;
}
bool convert(const taffy::LayoutOutput& v,TsCacheOutput& out){
    out.oof_count=v.oof_candidates.size();if(out.oof_count>out.oof_capacity)return false;require(out.oof_candidates || !out.oof_count);
    out.size[0]=v.size.width;out.size[1]=v.size.height;out.scrollable_overflow_rect[0]=v.scrollable_overflow_rect.left;out.scrollable_overflow_rect[1]=v.scrollable_overflow_rect.right;out.scrollable_overflow_rect[2]=v.scrollable_overflow_rect.top;out.scrollable_overflow_rect[3]=v.scrollable_overflow_rect.bottom;out.baselines[0]=opt(v.baselines.first);out.baselines[1]=opt(v.baselines.last);out.top_margin[0]=v.top_margin.positive;out.top_margin[1]=v.top_margin.negative;out.bottom_margin[0]=v.bottom_margin.positive;out.bottom_margin[1]=v.bottom_margin.negative;out.margins_can_collapse_through=v.margins_can_collapse_through;
    for(size_t n=0;n<v.oof_candidates.size();++n){const auto& c=v.oof_candidates.inner[n];out.oof_candidates[n]={c.node.value,c.order,position(c.position),{convert(c.static_position.x),convert(c.static_position.y)}};}
    out.oof_positioning_area_present=v.oof_positioning_area.has_value();if(v.oof_positioning_area){const auto& a=*v.oof_positioning_area;out.oof_positioning_size[0]=a.size.width;out.oof_positioning_size[1]=a.size.height;out.oof_positioning_offset[0]=a.offset.x;out.oof_positioning_offset[1]=a.offset.y;}
    return true;
}
}
extern "C" {
uint64_t ts_cpp_cache_capacity(void){return taffy::Cache::cache_size;}
int32_t ts_cache_new(TsCache** out){return call([&]{require(out);*out=new TsCache;});}
int32_t ts_cache_free(TsCache* c){return call([&]{require(c);delete c;});}
int32_t ts_cache_store(TsCache* c,const TsCacheInput* i,const TsCacheOutput* o){return call([&]{require(c && i && o);c->value.store(convert(*i),convert(*o));});}
int32_t ts_cache_get(TsCache* c,const TsCacheInput* i,TsCacheOutput* out,uint32_t* found){int32_t status=TS_CACHE_OK;auto result=call([&]{require(c && i && out && found);auto v=c->value.get(convert(*i));*found=v.has_value();if(v && !convert(*v,*out))status=TS_CACHE_CAPACITY;});return result==TS_CACHE_OK?status:result;}
int32_t ts_cache_clear(TsCache* c,uint32_t* out){return call([&]{require(c && out);*out=c->value.clear()==taffy::ClearState::Cleared;});}
int32_t ts_cache_state(const TsCache* c,TsCacheState* out){return call([&]{require(c && out);*out={uint32_t(CacheAccess::entry_count(c->value)),uint32_t(CacheAccess::has_final_entry(c->value)),CacheAccess::recent_bits(c->value),CacheAccess::next_cursor(c->value),uint32_t(c->value.is_empty())};});}
int32_t ts_cache_set_recently_used(TsCache* c,uint32_t bits){return call([&]{require(c);CacheAccess::set_recent_bits_for_test(c->value,static_cast<uint16_t>(bits));});}
int32_t ts_cache_snapshot(const TsCache* c,TsCacheSnapshot** out){return call([&]{require(c && out);*out=new TsCacheSnapshot{CacheAccess::snapshot(c->value)};});}
int32_t ts_cache_snapshot_free(TsCacheSnapshot* s){return call([&]{require(s);delete s;});}
int32_t ts_cache_measure_entries_equal(const TsCache* c,const TsCacheSnapshot* s,uint32_t* out){return call([&]{require(c && s && out);*out=CacheAccess::measure_entries_equal(c->value,s->entries);});}
int32_t ts_cache_mode_bits(const TsCacheInput* i,uint32_t* out){return call([&]{require(i && out);*out=CacheAccess::mode_bits(convert(*i));});}
int32_t ts_cache_mode_change_evictions(uint64_t* out){
    if(!out)return TS_CACHE_INVALID;
#ifndef NDEBUG
    return call([&]{*out=taffy::cache_mode_change_evictions();});
#else
    // Production only increments this counter in debug builds. An observed zero
    // in release is not a supported counter and must not masquerade as one.
    return TS_CACHE_UNAVAILABLE;
#endif
}
int32_t ts_cache_hidden_input(TsCacheInput* out){return call([&]{put(out,convert(taffy::LayoutInput::HIDDEN));});}
int32_t ts_cache_output_from_outer_size(float w,float h,TsCacheOutput* out){return call([&]{require(out);(void)convert(taffy::LayoutOutput::from_outer_size({w,h}),*out);});}
int32_t ts_cache_margin_from_margin(float margin,float out[2]){return call([&]{require(out);auto m=taffy::CollapsibleMarginSet::from_margin(margin);out[0]=m.positive;out[1]=m.negative;});}
}
