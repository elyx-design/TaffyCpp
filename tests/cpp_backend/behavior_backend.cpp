// C++-only test operation adapter. All layout/tree algorithms are production.
#include "tree_cache/behavior_grid.hpp"
#include "../behavior/ahem_intrinsic.h"
#include <taffy/compute.hpp>
#include <unordered_map>
#include <limits>

namespace {
struct Failure { uint32_t status; std::string message; };
// No string construction is allowed between observing a foreign failure and
// committing its classification. Diagnostics are strictly best effort.
struct CallbackFailure {};
void diagnostic(std::string& out,const char* message) noexcept {
    try { out=message; } catch (...) {}
}
void require(bool v, const char* why="invalid argument") { if(!v) throw std::invalid_argument(why); }
template<class T> void put(T* p,T v) { require(p); *p=std::move(v); }
template<class T> std::span<const T> span(const T* p,size_t n) { require((p || !n) && n<=size_t(PTRDIFF_MAX)/sizeof(T)); return {p,n}; }
std::string_view text(BehaviorText v) { span(v.data,v.len); std::string_view s(v.data?v.data:"",v.len); behavior_cpp::style_detail::utf8(s); return s; }
void string_out(std::string_view s,char* p,size_t cap,size_t* needed) { put(needed,s.size()+1); require(p || !cap); if(cap) { auto n=std::min(cap-1,s.size()); std::copy_n(s.data(),n,p); p[n]=0; } }
template<class T> T native(std::expected<T,taffy::TreeError> v) { if(!v) throw Failure{BEHAVIOR_ERROR,v.error().message()}; return std::move(*v); }
void native(std::expected<void,taffy::TreeError> v) { if(!v) throw Failure{BEHAVIOR_ERROR,v.error().message()}; }
taffy::AvailableSpace space(BehaviorScalar v) { switch(v.tag) { case 1:return taffy::AvailableSpace::definite(v.value);case 3:return taffy::AvailableSpace::min_content();case 4:return taffy::AvailableSpace::max_content();default:throw std::invalid_argument("invalid available-space tag");} }
BehaviorScalar space(taffy::AvailableSpace v) { return {v.is_definite()?1u:v.is_min_content()?3u:4u,v.into_option().value_or(0)}; }
BehaviorKnown known(taffy::Size<std::optional<float>> k) { return {{uint32_t(k.width.has_value()),k.width.value_or(0)},{uint32_t(k.height.has_value()),k.height.value_or(0)}}; }
BehaviorAvailable available(taffy::Size<taffy::AvailableSpace> s) { return {space(s.width),space(s.height)}; }
BehaviorRect rect(taffy::Rect<float> r) { return {r.left,r.right,r.top,r.bottom}; }
BehaviorSize size(taffy::Size<float> s) { return {s.width,s.height}; }
BehaviorLayout layout(const taffy::Layout& l) { return {l.order,l.location.x,l.location.y,size(l.size),size(l.content_box_size()),size(l.scrollbar_size),rect(l.scrollable_overflow_rect),rect(l.border),rect(l.padding),rect(l.margin),l.scroll_width(),l.scroll_height()}; }
struct Context final:taffy::NodeContext { grid_behavior::AhemContext ahem; bool aspect=false; float width=0,ratio=0; };
}
struct BehaviorOracle {
    taffy::Tree tree;
    std::unordered_map<uint64_t,taffy::NodeId> nodes;
    uint64_t next=1;
    bool poisoned=false;
    std::string error;
    taffy::NodeId id(uint64_t h) const { auto i=nodes.find(h); if(i==nodes.end())throw Failure{BEHAVIOR_STALE,"stale logical handle"};return i->second; }
    uint64_t logical(taffy::NodeId n) const { for(auto [h,id]:nodes)if(id==n)return h;throw Failure{BEHAVIOR_STALE,"unmapped native handle"}; }
    std::vector<taffy::NodeId> ids(const uint64_t* hs,size_t n) const {std::vector<taffy::NodeId> v;for(auto h:span(hs,n))v.push_back(id(h));return v;}
};
namespace {
template<class F> uint32_t call(BehaviorOracle* c,F&& f) noexcept {
    if(!c)return BEHAVIOR_INVALID;
    if(c->poisoned)return BEHAVIOR_POISONED;
    try { f(*c); return BEHAVIOR_OK; }
    catch(const CallbackFailure&){c->poisoned=true;diagnostic(c->error,"measurement callback failed or threw");return BEHAVIOR_CALLBACK_ERROR;}
    catch(const Failure& e){if(e.status==BEHAVIOR_CALLBACK_ERROR || e.status==BEHAVIOR_PANIC)c->poisoned=true;diagnostic(c->error,e.message.c_str());return e.status;}
    catch(const taffy::NodeAccessError& e){diagnostic(c->error,e.what());return BEHAVIOR_STALE;}
    catch(const std::invalid_argument& e){diagnostic(c->error,e.what());return BEHAVIOR_INVALID;}
    catch(const taffy::TreePanic& e){c->poisoned=true;diagnostic(c->error,e.what());return BEHAVIOR_PANIC;}
    catch(const taffy::UnsupportedAlgorithm& e){diagnostic(c->error,e.what());return BEHAVIOR_ERROR;}
    catch(const std::runtime_error& e){diagnostic(c->error,e.what());return BEHAVIOR_ERROR;}
    catch(const std::exception& e){c->poisoned=true;diagnostic(c->error,e.what());return BEHAVIOR_PANIC;}
    catch(...){c->poisoned=true;diagnostic(c->error,"unknown exception");return BEHAVIOR_PANIC;}
}
void check_children(BehaviorOracle& c,taffy::NodeId p,std::span<const taffy::NodeId> children) {
    std::vector<taffy::NodeId> seen;
    for(auto ch:children) {require(std::find(seen.begin(),seen.end(),ch)==seen.end(),"duplicate child");seen.push_back(ch);auto ancestor=std::optional(p);while(ancestor){require(*ancestor!=ch,"cycle in tree");ancestor=c.tree.parent(*ancestor);}}
}
void nodes_out(BehaviorOracle& c,const std::vector<taffy::NodeId>& ids,uint64_t* out,size_t cap,size_t* needed){put(needed,ids.size());require(out || !cap);for(size_t i=0;i<std::min(cap,ids.size());++i)out[i]=c.logical(ids[i]);}
void compute(BehaviorOracle& c,uint64_t h,BehaviorAvailable a,uint32_t rounding,void* user,BehaviorMeasure cb,bool test) {
    require(rounding<=1,"invalid rounding boolean"); auto root=c.id(h); auto av=taffy::Size{space(a.width),space(a.height)};
    if(rounding)c.tree.enable_rounding();else c.tree.disable_rounding();
    try { native(c.tree.compute_layout_with_measure(root,av,[&](taffy::LayoutTree&,taffy::NodeId node,const taffy::LayoutInput& input){
        const auto& s=c.tree.style(node);
        // TaffyTree's built-in resolver is zero. This is not a custom calc evaluator.
        return taffy::compute_leaf_layout(input,s,[](taffy::Calc,float){return 0.f;},[&](const taffy::LayoutInput& in){
            BehaviorSize out{};const auto k=known(in.known_dimensions);const auto a=available(in.available_space);
            if(test) {
                auto* ctx=dynamic_cast<Context*>(c.tree.node_context(node));
                if(!ctx || !ctx->aspect)out=grid_behavior::ahem_intrinsic(ctx?&ctx->ahem:nullptr,k,a);
                else if(k.width.present && k.height.present)out={k.width.value,k.height.value};
                else {++ctx->ahem.count;auto w=k.width.present?k.width.value:ctx->width;out={w,k.height.present?k.height.value:w*ctx->ratio};}
            } else if(cb) {
                const auto snapshot=behavior_cpp::style_json(s);const auto handle=c.logical(node);uint32_t status;
                try {status=cb(user,handle,k,a,{snapshot.data(),snapshot.size()},&out);}catch(...){c.poisoned=true;throw CallbackFailure{};}
                if(status){c.poisoned=true;throw CallbackFailure{};}
            }
            return taffy::Size<float>{out.width,out.height};
        });
    })); } catch(const std::invalid_argument& e) { throw taffy::TreePanic(e.what()); }
}
}
extern "C" {
uint32_t behavior_oracle_create(BehaviorOracle** out){try{require(out);*out=new BehaviorOracle;return BEHAVIOR_OK;}catch(...){return BEHAVIOR_INVALID;}}
void behavior_oracle_free(BehaviorOracle* c){delete c;}
uint32_t behavior_oracle_error(BehaviorOracle* c,char* out,size_t cap,size_t* n){if(!c)return BEHAVIOR_INVALID;try{string_out(c->error,out,cap,n);return BEHAVIOR_OK;}catch(...){return BEHAVIOR_INVALID;}}
uint32_t behavior_oracle_new_node(BehaviorOracle* p,const BehaviorProperty* props,size_t n,uint64_t* out){return call(p,[&](auto& c){require(out);auto s=behavior_cpp::parse_properties(props,n);auto node=native(c.tree.new_leaf(std::move(s)));require(c.next!=UINT64_MAX);auto h=c.next++;c.nodes.emplace(h,node);*out=h;});}
uint32_t behavior_oracle_set_style(BehaviorOracle* p,uint64_t h,const BehaviorProperty* props,size_t n){return call(p,[&](auto& c){native(c.tree.set_style(c.id(h),behavior_cpp::parse_properties(props,n)));});}
uint32_t behavior_oracle_set_style_json(BehaviorOracle* p,uint64_t h,BehaviorText json){return call(p,[&](auto& c){native(c.tree.set_style(c.id(h),behavior_cpp::parse_style_json(text(json))));});}
uint32_t behavior_oracle_set_style_json_inset(BehaviorOracle* p,uint64_t h,BehaviorText json,uint32_t side,uint32_t tag,float v){return call(p,[&](auto& c){require(side<4 && tag<3);auto s=behavior_cpp::parse_style_json(text(json));auto x=tag==0?taffy::LengthPercentageAuto::auto_():tag==1?taffy::LengthPercentageAuto::length(v):taffy::LengthPercentageAuto::percent(v);switch(side){case 0:s.inset.left=x;break;case 1:s.inset.right=x;break;case 2:s.inset.top=x;break;case 3:s.inset.bottom=x;}native(c.tree.set_style(c.id(h),std::move(s)));});}
uint32_t behavior_oracle_set_native_field(BehaviorOracle* p,uint64_t h,BehaviorText name,BehaviorText json){return call(p,[&](auto& c){auto id=c.id(h);auto s=c.tree.style(id);behavior_cpp::set_native_field(s,text(name),text(json));native(c.tree.set_style(id,std::move(s)));});}
uint32_t behavior_oracle_style_json(BehaviorOracle* p,uint64_t h,char* out,size_t cap,size_t* n){return call(p,[&](auto& c){string_out(behavior_cpp::style_json(c.tree.style(c.id(h))),out,cap,n);});}
uint32_t behavior_oracle_set_children(BehaviorOracle* p,uint64_t h,const uint64_t* children,size_t n){return call(p,[&](auto& c){auto id=c.id(h);auto ids=c.ids(children,n);check_children(c,id,ids);native(c.tree.set_children(id,ids));});}
uint32_t behavior_oracle_add_child(BehaviorOracle* p,uint64_t h,uint64_t ch){return call(p,[&](auto& c){auto id=c.id(h),child=c.id(ch);auto ids=c.tree.child_ids(id);ids.push_back(child);check_children(c,id,ids);native(c.tree.add_child(id,child));});}
uint32_t behavior_oracle_insert_child(BehaviorOracle* p,uint64_t h,size_t index,uint64_t ch){return call(p,[&](auto& c){auto id=c.id(h),child=c.id(ch);auto ids=c.tree.child_ids(id);ids.push_back(child);check_children(c,id,ids);native(c.tree.insert_child_at_index(id,index,child));});}
uint32_t behavior_oracle_remove_child(BehaviorOracle* p,uint64_t h,uint64_t ch){return call(p,[&](auto& c){native(c.tree.remove_child(c.id(h),c.id(ch)));});}
uint32_t behavior_oracle_remove_node(BehaviorOracle* p,uint64_t h){return call(p,[&](auto& c){native(c.tree.remove(c.id(h)));c.nodes.erase(h);});}
uint32_t behavior_oracle_parent(BehaviorOracle* p,uint64_t h,uint64_t* out){return call(p,[&](auto& c){auto parent=c.tree.parent(c.id(h));put(out,parent?c.logical(*parent):uint64_t(0));});}
uint32_t behavior_oracle_children(BehaviorOracle* p,uint64_t h,uint64_t* out,size_t cap,size_t* n){return call(p,[&](auto& c){nodes_out(c,native(c.tree.children(c.id(h))),out,cap,n);});}
uint32_t behavior_oracle_mark_dirty(BehaviorOracle* p,uint64_t h){return call(p,[&](auto& c){native(c.tree.mark_dirty(c.id(h)));});}
uint32_t behavior_oracle_dirty(BehaviorOracle* p,uint64_t h,uint32_t* out){return call(p,[&](auto& c){put(out,uint32_t(native(c.tree.dirty(c.id(h)))));});}
uint32_t behavior_oracle_hoisted_children(BehaviorOracle* p,uint64_t h,uint64_t* out,size_t cap,size_t* n){return call(p,[&](auto& c){nodes_out(c,c.tree.hoisted_child_ids(c.id(h)),out,cap,n);});}
uint32_t behavior_oracle_core_flags(BehaviorOracle* p,uint64_t h,uint32_t* replaced,uint32_t* compressible){return call(p,[&](auto& c){require(replaced && compressible);auto& s=c.tree.style(c.id(h));*replaced=s.is_replaced();*compressible=s.is_compressible_replaced();});}
uint32_t behavior_oracle_set_dimension(BehaviorOracle* p,uint64_t h,uint32_t field,uint32_t axis,uint32_t tag,float v){return call(p,[&](auto& c){require(field<=3 && axis<=1 && tag<=5);auto id=c.id(h);auto s=c.tree.style(id);if(field==1 || field==2){require(tag<=2);auto x=tag==0?taffy::LengthPercentageAuto::auto_():tag==1?taffy::LengthPercentageAuto::length(v):taffy::LengthPercentageAuto::percent(v);auto& dim=field==1?s.min_size:s.max_size;if(axis==0)dim.width=x;else dim.height=x;}else{auto x=tag==0?taffy::Dimension::auto_():tag==1?taffy::Dimension::length(v):tag==2?taffy::Dimension::percent(v):tag==3?taffy::Dimension::min_content():tag==4?taffy::Dimension::max_content():taffy::Dimension::fit_content();if(field==3)s.flex_basis=x;else if(axis==0)s.size.width=x;else s.size.height=x;}native(c.tree.set_style(id,std::move(s)));});}
uint32_t behavior_oracle_set_grid_track_fr(BehaviorOracle* p,uint64_t h,uint32_t axis,size_t index,float v){return call(p,[&](auto& c){require(axis<=1);auto id=c.id(h);auto s=c.tree.style(id);auto& tracks=axis==0?s.grid_template_rows:s.grid_template_columns;require(index<tracks.size());tracks[index]=taffy::GridTemplateComponent::fr(v);native(c.tree.set_style(id,std::move(s)));});}
uint32_t behavior_oracle_compute(BehaviorOracle* p,uint64_t h,BehaviorAvailable a,uint32_t r,void* u,BehaviorMeasure cb){return call(p,[&](auto& c){compute(c,h,a,r,u,cb,false);});}
uint32_t behavior_oracle_layout(BehaviorOracle* p,uint64_t h,BehaviorLayout* out){return call(p,[&](auto& c){put(out,layout(c.tree.layout(c.id(h))));});}
uint32_t behavior_oracle_unrounded_layout(BehaviorOracle* p,uint64_t h,BehaviorLayout* out){return call(p,[&](auto& c){put(out,layout(c.tree.unrounded_layout(c.id(h))));});}
uint32_t behavior_oracle_detailed_json(BehaviorOracle* p,uint64_t h,char* out,size_t cap,size_t* n){return call(p,[&](auto& c){string_out(behavior_cpp::detailed_json(behavior_cpp::grid(c.tree,c.id(h))),out,cap,n);});}
uint32_t behavior_oracle_tracks_zoom(BehaviorOracle* p,uint64_t h,uint32_t axis,float zoom,char* out,size_t cap,size_t* n){return call(p,[&](auto& c){string_out(behavior_cpp::tracks(behavior_cpp::grid(c.tree,c.id(h)),axis,zoom),out,cap,n);});}
uint32_t behavior_oracle_tracks(BehaviorOracle* p,uint64_t h,uint32_t axis,char* out,size_t cap,size_t* n){return behavior_oracle_tracks_zoom(p,h,axis,1,out,cap,n);}
uint32_t behavior_oracle_item_grid_area(BehaviorOracle* p,uint64_t h,size_t index,uint32_t* present,BehaviorRect* out){return call(p,[&](auto& c){require(present && out);auto a=behavior_cpp::item_area(behavior_cpp::grid(c.tree,c.id(h)),index);*present=a.has_value();*out=a?rect(*a):BehaviorRect{};});}
uint32_t behavior_oracle_resolve_area(BehaviorOracle* p,uint64_t h,BehaviorText rs,BehaviorText re,BehaviorText cs,BehaviorText ce,uint32_t direction,BehaviorRect padding,uint32_t* present,BehaviorRect* out){return call(p,[&](auto& c){require(present && out);auto a=behavior_cpp::resolve_area(behavior_cpp::grid(c.tree,c.id(h)),rs,re,cs,ce,direction,padding);*present=a.has_value();*out=a?rect(*a):BehaviorRect{};});}
uint32_t behavior_oracle_grid_axis_info(BehaviorOracle* p,uint64_t h,uint32_t axis,BehaviorGridAxisInfo* out){return call(p,[&](auto& c){behavior_cpp::axis_info(behavior_cpp::grid(c.tree,c.id(h)),axis,out);});}
uint32_t behavior_oracle_grid_position(BehaviorOracle* p,uint64_t h,uint32_t axis,size_t index,float* start,float* end){return call(p,[&](auto& c){behavior_cpp::position(behavior_cpp::grid(c.tree,c.id(h)),axis,index,start,end);});}
uint32_t behavior_oracle_grid_names_count(BehaviorOracle* p,uint64_t h,uint32_t axis,size_t line,uint32_t mode,size_t* out){return call(p,[&](auto& c){put(out,behavior_cpp::names(behavior_cpp::grid(c.tree,c.id(h)),axis,line,mode).size());});}
uint32_t behavior_oracle_grid_name(BehaviorOracle* p,uint64_t h,uint32_t axis,size_t line,uint32_t mode,size_t index,char* out,size_t cap,size_t* n){return call(p,[&](auto& c){auto names=behavior_cpp::names(behavior_cpp::grid(c.tree,c.id(h)),axis,line,mode);require(index<names.size());string_out(names[index],out,cap,n);});}
uint32_t behavior_oracle_set_ahem_context(BehaviorOracle* p,uint64_t h,BehaviorText t,uint32_t mode){return call(p,[&](auto& c){require(mode<=1);auto ctx=std::make_unique<Context>();ctx->ahem.text_content=text(t);ctx->ahem.vertical=mode;native(c.tree.set_node_context(c.id(h),std::move(ctx)));});}
uint32_t behavior_oracle_set_aspect_context(BehaviorOracle* p,uint64_t h,float width,float ratio){return call(p,[&](auto& c){auto ctx=std::make_unique<Context>();ctx->aspect=true;ctx->width=width;ctx->ratio=ratio;native(c.tree.set_node_context(c.id(h),std::move(ctx)));});}
uint32_t behavior_oracle_context_count(BehaviorOracle* p,uint64_t h,size_t* out){return call(p,[&](auto& c){auto* ctx=dynamic_cast<Context*>(c.tree.node_context(c.id(h)));if(!ctx)throw Failure{BEHAVIOR_ERROR,"node has no TestNodeContext"};put(out,ctx->ahem.count);});}
uint32_t behavior_oracle_compute_test_measure(BehaviorOracle* p,uint64_t h,BehaviorAvailable a,uint32_t r){return call(p,[&](auto& c){compute(c,h,a,r,nullptr,nullptr,true);});}
uint32_t behavior_oracle_serde_default(BehaviorOracle* p,char* out,size_t cap,size_t* n){return call(p,[&](auto&){string_out(behavior_cpp::style_json(taffy::Style{}),out,cap,n);});}
uint32_t behavior_oracle_serde_parse_value(BehaviorOracle* p,BehaviorText s){return call(p,[&](auto&){behavior_cpp::parse_json_value(text(s));});}
uint32_t behavior_oracle_inject_panic(BehaviorOracle* p){return call(p,[&](auto&){throw Failure{BEHAVIOR_PANIC,"injected native Rust panic (C++ test injection counterpart)"};});}
}
