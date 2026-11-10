// Test-only C facade over the production stdlib-only C++ library.
#include "primary/conversions.hpp"
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
using namespace taffy;
using namespace primary;
namespace {
struct Cookie final : NodeContext {
    void* user; OracleMeasure measure; OracleDrop drop; bool owned=false;
    Cookie(void* u, OracleMeasure m, OracleDrop d):user(u),measure(m),drop(d){}
    ~Cookie() override { if(owned && drop) drop(user); }
};
template<class T> T get(const T* p) { if(!p) throw Status{ORACLE_INVALID}; return *p; }
template<class T> void out(T* p,T v) { if(!p) throw Status{ORACLE_INVALID}; *p=std::move(v); }
template<class T> T require(std::expected<T,TreeError> r) { if(!r) throw Status{ORACLE_ERROR}; return std::move(*r); }
void require(std::expected<void,TreeError> r) { if(!r) throw Status{ORACLE_ERROR}; }
template<class F> uint32_t plain(F f) noexcept {
    try { f(); return ORACLE_OK; }
    catch(const Status& s) { return s.code; }
    catch(const UnsupportedAlgorithm&) { return ORACLE_UNSUPPORTED; }
    catch(const TreePanic&) { return ORACLE_PANIC; }
    catch(const NodeAccessError&) { return ORACLE_PANIC; }
    catch(...) { return ORACLE_PANIC; }
}
}
struct OracleContext {
    Tree tree;
    std::unordered_map<uint64_t,NodeId> nodes;
    std::unordered_map<uint64_t,OracleStyle> styles;
    uint64_t next=1;
    bool poisoned=false;
    std::mutex mutex;
    NodeId id(uint64_t n) const { auto i=nodes.find(n); if(i==nodes.end()) throw Status{ORACLE_STALE}; return i->second; }
    uint64_t logical(NodeId n) const { for(auto [k,v]:nodes) if(v==n) return k; throw Status{ORACLE_STALE}; }
};
namespace {
template<class F> uint32_t guard(OracleContext* c,F f) noexcept {
    if(!c) return ORACLE_INVALID;
    std::lock_guard lock(c->mutex);
    if(c->poisoned) return ORACLE_POISONED;
    auto status=plain([&]{f(*c);});
    if(status==ORACLE_PANIC || status==ORACLE_CALLBACK_FAILURE) c->poisoned=true;
    return status;
}
}
extern "C" {
uint32_t oracle_create(OracleContext** p) { return plain([&]{ if(!p) throw Status{ORACLE_INVALID}; *p=new OracleContext; }); }
uint32_t oracle_free(OracleContext* p) { return plain([&]{ delete p; }); }
uint32_t oracle_default_style(OracleStyle* p) { return plain([&]{out(p,default_style());}); }
uint32_t oracle_position_is_positioned(uint32_t tag,uint32_t* p) { return plain([&]{out(p,uint32_t(is_positioned(position(tag))));}); }
uint32_t oracle_position_is_out_of_flow(uint32_t tag,uint32_t* p) { return plain([&]{out(p,uint32_t(is_out_of_flow(position(tag))));}); }
uint32_t oracle_new_node(OracleContext* p,const OracleStyle* s,void* u,OracleMeasure m,OracleDrop d,uint64_t* o) {
 return guard(p,[&](auto& c){
    if (!o) {
        throw Status{ORACLE_INVALID};
    }
    auto raw=get(s); auto st=style(raw);
    if(c.next==std::numeric_limits<uint64_t>::max()) throw Status{ORACLE_ERROR};
    auto cookie=std::make_unique<Cookie>(u,m,d); auto* cookie_ptr=cookie.get();
    auto n=require(c.tree.new_leaf(std::move(st))); auto logical=c.next;
    try { c.nodes.emplace(logical,n); c.styles.emplace(logical,raw); require(c.tree.set_node_context(n,std::move(cookie))); }
    catch(...) { c.nodes.erase(logical); c.styles.erase(logical); require(c.tree.remove(n)); throw; }
    cookie_ptr->owned=true; ++c.next; *o=logical;
 });
}
uint32_t oracle_set_context(OracleContext* p,uint64_t id,void* u,OracleMeasure m,OracleDrop d) {
 return guard(p,[&](auto& c){ auto n=c.id(id); std::unique_ptr<Cookie> cookie;
    if (u || m || d) {
        cookie=std::make_unique<Cookie>(u,m,d);
    }
    auto* ptr=cookie.get();
    require(c.tree.set_node_context(n,std::move(cookie))); if(ptr) ptr->owned=true;
 });
}
uint32_t oracle_remove(OracleContext* p,uint64_t id) { return guard(p,[&](auto& c){require(c.tree.remove(c.id(id))); c.nodes.erase(id); c.styles.erase(id);}); }
uint32_t oracle_clear(OracleContext* p) { return guard(p,[](auto& c){c.tree.clear();c.nodes.clear();c.styles.clear();}); }
uint32_t oracle_set_style(OracleContext* p,uint64_t id,const OracleStyle* s) { return guard(p,[&](auto& c){auto n=c.id(id);auto raw=get(s);require(c.tree.set_style(n,style(raw)));c.styles.at(id)=raw;}); }
uint32_t oracle_get_style(OracleContext* p,uint64_t id,OracleStyle* s) { return guard(p,[&](auto& c){c.id(id);out(s,c.styles.at(id));}); }
uint32_t oracle_set_grid_template_columns(OracleContext* p,uint64_t id,const OracleGridTrack* tracks,size_t count) {
 return guard(p,[&](auto& c){auto n=c.id(id); if((count&&!tracks)||count>size_t(PTRDIFF_MAX)/sizeof(*tracks)) throw Status{ORACLE_INVALID};
    std::vector<GridTemplateComponent> columns; columns.reserve(count);
    for(size_t i=0;i<count;++i) { auto t=tracks[i];
      columns.emplace_back(TrackSizingFunction{MinTrackSizingFunction(lp(t.min)),MaxTrackSizingFunction(lp(t.max))}); }
    auto st=c.tree.style(n);st.grid_template_columns=std::move(columns);require(c.tree.set_style(n,std::move(st)));
 });
}
uint32_t oracle_get_grid_template_columns(OracleContext* p,uint64_t id,OracleGridTrack* tracks,size_t capacity,size_t* count) {
 return guard(p,[&](auto& c){auto n=c.id(id);if(!count) throw Status{ORACLE_INVALID};std::vector<OracleGridTrack> result;
    for(const auto& component:c.tree.style(n).grid_template_columns) { auto t=std::get_if<TrackSizingFunction>(&component.value);if(!t) throw Status{ORACLE_UNSUPPORTED};
      auto scalar=[](const auto& v)->OracleScalar {switch(v.kind()){case LengthKind::Length:return {1,v.value()};case LengthKind::Percent:return {2,v.value()};default:throw Status{ORACLE_UNSUPPORTED};}};
      result.push_back({scalar(t->min),scalar(t->max)}); }
    if(!tracks&&capacity==0) {*count=result.size();return;}
    if (!tracks || capacity < result.size()) {
        throw Status{ORACLE_INVALID};
    }
    std::copy(result.begin(),result.end(),tracks);*count=result.size();
 });
}
uint32_t oracle_set_children(OracleContext* p,uint64_t id,const uint64_t* ids,size_t count) {
 return guard(p,[&](auto& c){auto n=c.id(id);if((count&&!ids)||count>size_t(PTRDIFF_MAX)/sizeof(*ids)) throw Status{ORACLE_INVALID};std::vector<NodeId> children;std::unordered_set<NodeId> unique;
    for(size_t i=0;i<count;++i) {auto child=c.id(ids[i]);if(!unique.insert(child).second) throw Status{ORACLE_ERROR};
      for (auto ancestor=std::optional<NodeId>(n); ancestor; ancestor=c.tree.parent(*ancestor)) {
          if (*ancestor == child) {
              throw Status{ORACLE_ERROR};
          }
      }
      children.push_back(child);}
    require(c.tree.set_children(n,children));
 });
}
uint32_t oracle_parent(OracleContext* p,uint64_t id,uint64_t* o) {return guard(p,[&](auto& c){auto n=c.tree.parent(c.id(id));out(o,n?c.logical(*n):uint64_t(0));});}
uint32_t oracle_child(OracleContext* p,uint64_t id,size_t index,uint64_t* o) {return guard(p,[&](auto& c){out(o,c.logical(require(c.tree.child_at_index(c.id(id),index))));});}
uint32_t oracle_child_count(OracleContext* p,uint64_t id,size_t* o) {return guard(p,[&](auto& c){out(o,require(c.tree.children(c.id(id))).size());});}
uint32_t oracle_mark_dirty(OracleContext* p,uint64_t id) {return guard(p,[&](auto& c){require(c.tree.mark_dirty(c.id(id)));});}
uint32_t oracle_dirty(OracleContext* p,uint64_t id,uint32_t* o) {return guard(p,[&](auto& c){out(o,uint32_t(require(c.tree.dirty(c.id(id)))));});}
uint32_t oracle_rounding(OracleContext* p,uint32_t v) {return guard(p,[&](auto& c){if(boolean(v)) c.tree.enable_rounding();else c.tree.disable_rounding();});}
uint32_t oracle_compute(OracleContext* p,uint64_t id,OracleSizing space) {
 return guard(p,[&](auto& c){auto n=c.id(id);auto available=avs(space);
    try { require(c.tree.compute_layout_with_measure(n,available,[&](LayoutTree&,NodeId node,const LayoutInput& i){
        auto logical=c.logical(node);auto* cookie=dynamic_cast<Cookie*>(c.tree.node_context(node));
        if(cookie&&cookie->measure) {OracleLayoutOutput o{};auto inputs=ci(i);auto scalar_style=c.styles.at(logical);uint32_t status;
          try {status=cookie->measure(cookie->user,logical,&inputs,&scalar_style,&o);} catch(...) {throw Status{ORACLE_CALLBACK_FAILURE};}
          if (status) {
              throw Status{ORACLE_CALLBACK_FAILURE};
          }
          return output(o);}
        return compute_leaf_layout(i,c.tree.style(node),{},[](const LayoutInput&){return Size<float>{};});
    })); } catch(const Status&) {c.poisoned=true;throw;}
 });
}
uint32_t oracle_layout(OracleContext* p,uint64_t id,uint32_t unrounded,OracleLayout* o) {
 return guard(p,[&](auto& c){auto n=c.id(id);const auto& l=boolean(unrounded)?c.tree.unrounded_layout(n):c.tree.layout(n);
 out(o,OracleLayout{l.order,l.location.x,l.location.y,cs(l.size),cs(l.content_box_size()),cs(l.scrollbar_size),cr(l.scrollable_overflow_rect),cr(l.border),cr(l.padding),cr(l.margin),l.scroll_width(),l.scroll_height()});});
}
uint32_t oracle_leaf(const OracleStyle* s,const OracleLayoutInput* i,void* user,OracleIntrinsic cb,OracleLayoutOutput* o) {
 return plain([&]{if(!o) throw Status{ORACLE_INVALID};auto st=style(get(s));auto inputs=input(get(i));
 auto result=compute_leaf_layout(inputs,st,{},[&](const LayoutInput& measured){OracleSize size{};if(cb) {uint32_t status;
    try {status=cb(user,ck(measured.known_dimensions),cas(measured.available_space),&size);}catch(...) {throw Status{ORACLE_CALLBACK_FAILURE};}
    if(status) throw Status{ORACLE_CALLBACK_FAILURE};}return rs(size);});out(o,cout(result));});
}
uint32_t oracle_test_panic(OracleContext* p) {return guard(p,[](auto&){throw TreePanic("test-only bounded native panic probe");});}
}
