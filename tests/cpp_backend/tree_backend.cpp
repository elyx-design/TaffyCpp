// Production C++ implementation of the test-only persistent tree ABI.
#include "tree_cache/conversions.hpp"
#include <cstdio>
#include <unordered_map>

using namespace cpp_tree_abi;
namespace {
// Trivial exception payload: recording a foreign failure never allocates a string.
struct CallbackFailure { uint32_t status; const char* message; };
void diagnostic(std::string& out, const char* message) noexcept {
    try { out=message; } catch (...) { /* Status and poison are already committed. */ }
}
struct Cookie final : taffy::NodeContext {
    void* user; TsMeasure measure; TsDrop drop;
    uint64_t calls = 0;
    std::shared_ptr<uint64_t> drops;
    bool owned = false;
    Cookie(void* u,TsMeasure m,TsDrop d,std::shared_ptr<uint64_t> count):user(u),measure(m),drop(d),drops(std::move(count)){}
    ~Cookie() override { if (owned) { ++*drops; if (drop) { try { drop(user); } catch (...) { /* Foreign hooks must not throw. */ } } } }
};
thread_local const taffy::Style* active_style = nullptr;
struct StyleBorrow {
    const taffy::Style* previous = active_style;
    explicit StyleBorrow(const taffy::Style& style) { active_style=&style; }
    ~StyleBorrow() { active_style=previous; }
};
}
struct TsTree {
    // Drops counter outlives Tree, including context destruction during free.
    std::shared_ptr<uint64_t> drops = std::make_shared<uint64_t>(0);
    taffy::Tree tree;
    std::unordered_map<uint64_t,taffy::NodeId> nodes;
    uint64_t next=1;
    bool poisoned=false;
    std::string error;
    explicit TsTree(size_t capacity):tree(capacity==SIZE_MAX?16:capacity){}
    taffy::NodeId id(uint64_t h) const {
        auto it=nodes.find(h);
        require(it!=nodes.end(),ORACLE_STALE,"stale logical handle"); return it->second;
    }
    uint64_t logical(taffy::NodeId n) const {
        for (const auto& [h,id]:nodes) if (id==n) return h;
        throw Failure{ORACLE_STALE,"unmapped native node"};
    }
    uint64_t add(taffy::NodeId n) { require(next!=UINT64_MAX,ORACLE_ERROR,"logical handle exhaustion"); auto h=next++; nodes.emplace(h,n); return h; }
    std::vector<taffy::NodeId> ids(const uint64_t* values,size_t count) const {
        std::vector<taffy::NodeId> result; for(auto h:span(values,count)) result.push_back(id(h)); return result;
    }
};
namespace {
std::string native_error(const taffy::TreeError& e) {
    const char* name="InvalidInputNode";
    switch(e.kind) {
    case taffy::TreeErrorKind::ChildIndexOutOfBounds:name="ChildIndexOutOfBounds";break;
    case taffy::TreeErrorKind::InvalidParentNode:name="InvalidParentNode";break;
    case taffy::TreeErrorKind::InvalidChildNode:name="InvalidChildNode";break;
    case taffy::TreeErrorKind::InvalidInputNode:break;
    }
    return std::string(name)+" { node: "+std::to_string(e.node.value)+", child_index: "+std::to_string(e.child_index)+", child_count: "+std::to_string(e.child_count)+" }: "+e.message();
}
template<class T> T native(std::expected<T,taffy::TreeError> v) { if (!v) throw Failure{ORACLE_ERROR,native_error(v.error())}; return std::move(*v); }
void native(std::expected<void,taffy::TreeError> v) { if (!v) throw Failure{ORACLE_ERROR,native_error(v.error())}; }
template<class F> uint32_t plain(F&& fn) noexcept {
    try { fn(); return ORACLE_OK; } catch(const CallbackFailure& e) { return e.status; } catch(const Failure& e) { return e.status; } catch(const taffy::UnsupportedAlgorithm&) { return ORACLE_UNSUPPORTED; } catch (...) { return ORACLE_PANIC; }
}
template<class F> uint32_t guard(TsTree* p,F&& fn) noexcept {
    if(!p) return ORACLE_INVALID;
    if(p->poisoned) return ORACLE_POISONED;
    p->error.clear();
    try { fn(*p); return ORACLE_OK; }
    catch(const CallbackFailure& e) { p->poisoned=true; diagnostic(p->error,e.message); return e.status; }
    catch(const Failure& e) { if(e.status==ORACLE_CALLBACK_FAILURE || e.status==ORACLE_PANIC) p->poisoned=true; diagnostic(p->error,e.message.c_str()); return e.status; }
    catch(const taffy::NodeAccessError& e) { diagnostic(p->error,e.what()); return ORACLE_STALE; }
    catch(const taffy::UnsupportedAlgorithm& e) { diagnostic(p->error,e.what()); return ORACLE_UNSUPPORTED; }
    catch(const std::exception& e) { p->poisoned=true; diagnostic(p->error,e.what()); return ORACLE_PANIC; }
    catch(...) { p->poisoned=true; diagnostic(p->error,"unknown native exception"); return ORACLE_PANIC; }
}
Cookie* cookie(TsTree& c,taffy::NodeId n) { return dynamic_cast<Cookie*>(c.tree.node_context(n)); }
std::unique_ptr<Cookie> context(TsTree& c,void* user,TsMeasure cb,TsDrop drop) { return std::make_unique<Cookie>(user,cb,drop,c.drops); }
void compute(TsTree& c,uint64_t h,OracleSizing space,bool measured,void* user,TsMeasure fallback) {
    auto id=c.id(h); auto available_space=available(space);
    if(!measured) { native(c.tree.compute_layout(id,available_space)); return; }
    native(c.tree.compute_layout_with_measure(id,available_space,[&](taffy::LayoutTree&,taffy::NodeId node,const taffy::LayoutInput& in) {
        auto* ctx=cookie(c,node);
        auto cb=ctx && ctx->measure ? ctx->measure : fallback;
        auto* data=ctx && ctx->measure ? ctx->user : user;
        const auto& st=c.tree.style(node);
        if(!cb) return taffy::compute_leaf_layout(in,st,[](taffy::Calc,float){return 0.f;},[](const taffy::LayoutInput&){return taffy::Size<float>{};});
        if(ctx && ctx->measure) ++ctx->calls;
        const auto ci=input(in); TsOutput out{}; StyleBorrow borrow(st);
        // Consume the owning output even on failure or a prohibited foreign throw.
        struct OutputOwner { TsOutput& out; ~OutputOwner(){ delete static_cast<taffy::LayoutOutput*>(out.native_output); out.native_output=nullptr; } } owner{out};
        const auto handle=c.logical(node);
        uint32_t status;
        try { status=cb(data,handle,&ci,&st,&out); }
        catch(...) { c.poisoned=true; throw CallbackFailure{ORACLE_CALLBACK_FAILURE,"measurement callback threw"}; }
        if(status) {
            c.poisoned=true;
            throw CallbackFailure{static_cast<std::uint32_t>(status==ORACLE_PANIC?ORACLE_PANIC:ORACLE_CALLBACK_FAILURE),"measurement callback returned failure"};
        }
        if(out.native_output) return std::move(*static_cast<taffy::LayoutOutput*>(out.native_output));
        try { return output(out.value); }
        catch(const Failure&) { c.poisoned=true; throw; }
    }));
}
}
extern "C" {
uint32_t ts_create(size_t capacity,TsTree** out) { return plain([&]{ require(out); *out=nullptr; auto c=std::make_unique<TsTree>(capacity); *out=c.release(); }); }
uint32_t ts_free(TsTree* c) { return plain([&]{delete c;}); }
uint32_t ts_free_report(TsTree* c,uint64_t* out) { return plain([&]{require(c && out); auto drops=c->drops; delete c; *out=*drops;}); }
uint32_t ts_new_leaf(TsTree* p,const TsProperty* props,size_t count,uint64_t* out) { return guard(p,[&](TsTree& c){require(out); auto st=style(props,count); *out=c.add(native(c.tree.new_leaf(std::move(st))));}); }
uint32_t ts_new_leaf_with_context(TsTree* p,const TsProperty* props,size_t count,void* user,TsMeasure cb,TsDrop drop,uint64_t* out) {
    return guard(p,[&](TsTree& c){require(out); auto st=style(props,count); auto ctx=context(c,user,cb,drop); auto* raw=ctx.get(); auto n=native(c.tree.new_leaf_with_context(std::move(st),std::move(ctx))); raw->owned=true; *out=c.add(n);});
}
uint32_t ts_new_with_children(TsTree* p,const TsProperty* props,size_t count,const uint64_t* children,size_t n,uint64_t* out) {return guard(p,[&](TsTree& c){require(out); auto st=style(props,count); auto ids=c.ids(children,n); *out=c.add(native(c.tree.new_with_children(std::move(st),ids)));});}
uint32_t ts_set_context(TsTree* p,uint64_t h,void* user,TsMeasure cb,TsDrop drop) {return guard(p,[&](TsTree& c){auto n=c.id(h); if(!user && !cb && !drop) {native(c.tree.set_node_context(n,nullptr));return;} auto ctx=context(c,user,cb,drop); auto* raw=ctx.get(); native(c.tree.set_node_context(n,std::move(ctx))); raw->owned=true;});}
uint32_t ts_get_context(TsTree* p,uint64_t h,uint32_t* present,void** user,uint64_t* calls) {return guard(p,[&](TsTree& c){auto* ctx=cookie(c,c.id(h)); require(present && user && calls); *present=ctx!=nullptr; *user=ctx?ctx->user:nullptr; *calls=ctx?ctx->calls:0;});}
uint32_t ts_reset_context_calls(TsTree* p,uint64_t h) {return guard(p,[&](TsTree& c){if(auto* ctx=cookie(c,c.id(h)))ctx->calls=0;});}
uint32_t ts_remove(TsTree* p,uint64_t h,uint64_t* out) {return guard(p,[&](TsTree& c){require(out); auto removed=native(c.tree.remove(c.id(h))); *out=c.logical(removed); c.nodes.erase(h);});}
uint32_t ts_clear(TsTree* p) {return guard(p,[](TsTree& c){c.tree.clear();c.nodes.clear();});}
uint32_t ts_count(TsTree* p,size_t* out) {return guard(p,[&](TsTree& c){put(out,c.tree.total_node_count());});}
uint32_t ts_drop_count(TsTree* p,uint64_t* out) {return guard(p,[&](TsTree& c){put(out,*c.drops);});}
uint32_t ts_capacities(TsTree* p,size_t* nodes,size_t* children,size_t* parents) {return guard(p,[&](TsTree& c){
    require(nodes && children && parents);
    // C++ stores all three components in one arena, not three Rust vectors.
    // Observe its actual allocation, never the constructor's requested reserve.
    const auto capacity=c.tree.capacity();
    *nodes=capacity; *children=capacity; *parents=capacity;
});}
uint32_t ts_set_style(TsTree* p,uint64_t h,const TsProperty* props,size_t count) {return guard(p,[&](TsTree& c){auto n=c.id(h);native(c.tree.set_style(n,style(props,count)));});}
uint32_t ts_style_equals(TsTree* p,uint64_t h,const TsProperty* props,size_t count,uint32_t* out) {return guard(p,[&](TsTree& c){auto n=c.id(h);put(out,uint32_t(c.tree.style(n)==style(props,count)));});}
uint32_t ts_style_debug(TsTree* p,uint64_t h,char*,size_t,size_t*) {return guard(p,[&](TsTree& c){(void)c.tree.style(c.id(h));throw Failure{ORACLE_UNSUPPORTED,"production Style has no native Debug formatter"};});}
uint32_t ts_style_display(TsTree* p,uint64_t h,uint32_t* out) {return guard(p,[&](TsTree& c){auto display=c.tree.style(c.id(h)).display;put(out,display==taffy::Display::None?3u:display==taffy::Display::FlowRoot?4u:uint32_t(display));});}
uint32_t ts_error(TsTree* p,char* out,size_t capacity,size_t* required) {return plain([&]{require(p);string_out(p->error,out,capacity,required);});}
uint32_t ts_add_child(TsTree* p,uint64_t parent,uint64_t child) {return guard(p,[&](TsTree& c){native(c.tree.add_child(c.id(parent),c.id(child)));});}
uint32_t ts_insert_child(TsTree* p,uint64_t parent,size_t index,uint64_t child) {return guard(p,[&](TsTree& c){native(c.tree.insert_child_at_index(c.id(parent),index,c.id(child)));});}
uint32_t ts_set_children(TsTree* p,uint64_t parent,const uint64_t* children,size_t count) {return guard(p,[&](TsTree& c){auto n=c.id(parent);auto ids=c.ids(children,count);native(c.tree.set_children(n,ids));});}
uint32_t ts_remove_child(TsTree* p,uint64_t parent,uint64_t child,uint64_t* out) {return guard(p,[&](TsTree& c){require(out);*out=c.logical(native(c.tree.remove_child(c.id(parent),c.id(child))));});}
uint32_t ts_remove_child_at(TsTree* p,uint64_t parent,size_t index,uint64_t* out) {return guard(p,[&](TsTree& c){require(out);*out=c.logical(native(c.tree.remove_child_at_index(c.id(parent),index)));});}
uint32_t ts_replace_child(TsTree* p,uint64_t parent,size_t index,uint64_t child,uint64_t* out) {return guard(p,[&](TsTree& c){require(out);*out=c.logical(native(c.tree.replace_child_at_index(c.id(parent),index,c.id(child))));});}
uint32_t ts_remove_children_range(TsTree* p,uint64_t parent,uint32_t sk,size_t start,uint32_t ek,size_t end) {return guard(p,[&](TsTree& c){auto n=c.id(parent);require(sk<=2 && ek<=2,ORACLE_UNSUPPORTED,"invalid range bound tag"); if(sk==0)start=0; else if(sk==2){if(start==SIZE_MAX)throw taffy::TreePanic("range start overflow");++start;} if(ek==0)end=c.tree.child_count(n);else if(ek==1){if(end==SIZE_MAX)throw taffy::TreePanic("range end overflow");++end;}native(c.tree.remove_children_range(n,start,end));});}
uint32_t ts_parent(TsTree* p,uint64_t h,uint64_t* out) {return guard(p,[&](TsTree& c){auto parent=c.tree.parent(c.id(h));put(out,parent?c.logical(*parent):uint64_t(0));});}
uint32_t ts_child(TsTree* p,uint64_t parent,size_t index,uint64_t* out) {return guard(p,[&](TsTree& c){put(out,c.logical(native(c.tree.child_at_index(c.id(parent),index))));});}
uint32_t ts_child_count(TsTree* p,uint64_t parent,size_t* out) {return guard(p,[&](TsTree& c){put(out,c.tree.child_count(c.id(parent)));});}
uint32_t ts_children(TsTree* p,uint64_t h,uint64_t* out,size_t capacity,size_t* count) {return guard(p,[&](TsTree& c){auto children=native(c.tree.children(c.id(h)));put(count,children.size());if(!out && !capacity)return;require(out && capacity>=children.size());for(size_t i=0;i<children.size();++i)out[i]=c.logical(children[i]);});}
uint32_t ts_dirty(TsTree* p,uint64_t h,uint32_t* out) {return guard(p,[&](TsTree& c){put(out,uint32_t(native(c.tree.dirty(c.id(h)))));});}
uint32_t ts_mark_dirty(TsTree* p,uint64_t h) {return guard(p,[&](TsTree& c){native(c.tree.mark_dirty(c.id(h)));});}
uint32_t ts_rounding(TsTree* p,uint32_t enabled) {return guard(p,[&](TsTree& c){if(boolean(enabled))c.tree.enable_rounding();else c.tree.disable_rounding();});}
uint32_t ts_compute(TsTree* p,uint64_t h,OracleSizing space,uint32_t measured) {return guard(p,[&](TsTree& c){compute(c,h,space,boolean(measured),nullptr,nullptr);});}
uint32_t ts_compute_with_measure(TsTree* p,uint64_t h,OracleSizing space,void* user,TsMeasure cb) {return guard(p,[&](TsTree& c){compute(c,h,space,true,user,cb);});}
uint32_t ts_layout(TsTree* p,uint64_t h,uint32_t unrounded,TsLayout* out) {return guard(p,[&](TsTree& c){auto n=c.id(h);put(out,layout(boolean(unrounded)?c.tree.unrounded_layout(n):c.tree.layout(n)));});}
uint32_t ts_leaf(const void* native_style,const TsInput* in,void* user,TsIntrinsic cb,TsOutput* out) {return plain([&]{
    require(native_style && in && out); require(native_style==active_style,ORACLE_INVALID,"native Style is only borrowed during an active measure callback");
    auto i=input(*in);
    auto result=taffy::compute_leaf_layout(i,*active_style,[](taffy::Calc,float){return 0.f;},[&](const taffy::LayoutInput& measured){
        OracleSize s{};
        if(cb) {
            uint32_t status;
            try { status=cb(user,known(measured.known_dimensions),available(measured.available_space),&s); }
            catch(...) { throw CallbackFailure{ORACLE_CALLBACK_FAILURE,"intrinsic callback threw"}; }
            if(status) throw CallbackFailure{ORACLE_CALLBACK_FAILURE,"intrinsic callback failed"};
        }
        return taffy::Size<float>{s.width,s.height};
    });
    auto token=std::make_unique<taffy::LayoutOutput>(std::move(result));out->value=output(*token);out->native_output=token.release();
});}
uint32_t ts_output_free(TsOutput* out) {return plain([&]{require(out);delete static_cast<taffy::LayoutOutput*>(out->native_output);out->native_output=nullptr;});}
uint32_t ts_test_panic(TsTree* p) {return guard(p,[](TsTree&){throw taffy::TreePanic("bounded native tree panic probe");});}
uint32_t ts_print_tree(TsTree* p,uint64_t h) {return guard(p,[&](TsTree& c){
    // Diagnostic traversal only; every reported geometry value comes from Tree.
    std::function<void(taffy::NodeId,size_t)> print=[&](taffy::NodeId n,size_t depth){const auto& l=c.tree.layout(n);std::printf("%*s%llu: x=%g y=%g width=%g height=%g\n",int(depth*2),"",static_cast<unsigned long long>(c.logical(n)),double(l.location.x),double(l.location.y),double(l.size.width),double(l.size.height));for(auto child:c.tree.child_ids(n))print(child,depth+1);};print(c.id(h),0);
});}
} // extern C
