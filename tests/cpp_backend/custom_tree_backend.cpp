// Test-only operation facade over the production LayoutTree algorithms.
// No Rust ABI, high-level Tree storage, or duplicate layout algorithms.
#include "../support/custom_tree_oracle.h"
#include <taffy/compute.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace {
using namespace taffy;
struct InvalidArgument {};
struct DeliberatePanic {};
// Foreign callbacks must not throw. Keep violations distinct from deliberate
// panic probes and ordinary native algorithm exceptions during containment.
struct CallbackFailure { std::exception_ptr cause; };

template<class T> void output(T* p) {
    if (!p || reinterpret_cast<std::uintptr_t>(p) % alignof(T)) throw InvalidArgument{};
}
std::mutex registry_mutex;
std::uint64_t next_id = 1; // accessed only under registry_mutex
std::uint64_t id() {
    if (next_id == std::numeric_limits<std::uint64_t>::max()) throw InvalidArgument{};
    return next_id++;
}
AvailableSpace available(CtAvailable v) {
    switch (v.tag) {
    case 0: if (std::isfinite(v.value)) return AvailableSpace::definite(v.value); break;
    case 1: return AvailableSpace::min_content();
    case 2: return AvailableSpace::max_content();
    }
    throw InvalidArgument{};
}
CtAvailable abi_available(AvailableSpace v) {
    if (v.is_definite()) return {0, v.value()};
    return {v.is_min_content() ? 1u : 2u, 0};
}
struct Coefficients { float px, pct; };
struct Node {
    std::uint32_t kind;
    Style style;
    Cache cache;
    Layout layout{}, final_layout{};
    DetailedLayoutInfo detail{};
    std::vector<NodeId> children, hoisted;
};
class CustomTree final : public LayoutTree {
public:
    bool poisoned = false;
    std::unordered_map<std::uint64_t, Node> nodes;
    std::unordered_map<std::uint64_t, Coefficients> calcs;
    CtResolve resolver;
    CtMeasure measurer;
    void* context;
    mutable CtCounts counts{};
    CustomTree(CtResolve r, CtMeasure m, void* c) : resolver(r), measurer(m), context(c) {}
    Node& node(NodeId n) { return nodes.at(n.value); }
    const Node& node(NodeId n) const { return nodes.at(n.value); }
    Node& required(std::uint64_t n) {
        auto it = nodes.find(n);
        if (it == nodes.end()) throw InvalidArgument{};
        return it->second;
    }
    Calc calc(std::uint64_t handle) const {
        if (!calcs.contains(handle) || handle > std::numeric_limits<std::uintptr_t>::max())
            throw InvalidArgument{};
        return Calc{static_cast<std::uintptr_t>(handle)};
    }
    Dimension dimension(CtValue v) const {
        switch (v.tag) {
        case 0: return Dimension::auto_();
        case 1: if (std::isfinite(v.value)) return Dimension::length(v.value); break;
        case 2: if (std::isfinite(v.value)) return Dimension::percent(v.value); break;
        case 3: return Dimension::fit_content_calc(calc(v.calc));
        }
        throw InvalidArgument{};
    }
    LengthPercentageAuto minimum(CtValue v) const {
        switch (v.tag) {
        case 0: return LengthPercentageAuto::auto_();
        case 1: if (std::isfinite(v.value)) return LengthPercentageAuto::length(v.value); break;
        case 2: if (std::isfinite(v.value)) return LengthPercentageAuto::percent(v.value); break;
        }
        throw InvalidArgument{};
    }
    Style import_style(std::uint32_t kind, CtStyle s) const {
        Style result;
        if (kind == 0) result.display = Display::Block;
        if (kind == 1) result.display = Display::Grid;
        result.size = {dimension(s.width), dimension(s.height)};
        result.min_size = {minimum(s.min_width), minimum(s.min_height)};
        return result;
    }
    void clear_all() { for (auto& [key, unused] : nodes) { (void)unused; cache_clear(NodeId{key}); } }
    std::vector<NodeId> child_ids(NodeId n) const override { return node(n).children; }
    const Style& style(NodeId n) const override { return node(n).style; }
    const Style& core_container_style(NodeId n) const override { return style(n); }
    const Style& block_container_style(NodeId n) const override { return style(n); }
    const Style& block_child_style(NodeId n) const override { return style(n); }
    const Style& grid_container_style(NodeId n) const override { return style(n); }
    const Style& grid_child_style(NodeId n) const override { return style(n); }
    const Style& oof_item_style(NodeId n) const override { return style(n); }
    float resolve_calc(Calc c, float basis) const override {
        ++counts.calc;
        const auto coefficients = calcs.at(c.handle);
        try { return resolver(context, coefficients.px, coefficients.pct, basis); }
        catch (...) { throw CallbackFailure{std::current_exception()}; }
    }
    LayoutOutput measure(NodeId n, const LayoutInput& in) override {
        ++counts.measure;
        CtMeasureInput input{in.known_dimensions.width.has_value(), in.known_dimensions.height.has_value(),
            in.known_dimensions.width.value_or(0), in.known_dimensions.height.value_or(0),
            abi_available(in.available_space.width), abi_available(in.available_space.height)};
        CtSize result;
        try { result = measurer(context, n.value, input); }
        catch (...) { throw CallbackFailure{std::current_exception()}; }
        return LayoutOutput::from_outer_size({result.width, result.height});
    }
    LayoutOutput compute_child_layout(NodeId n, const LayoutInput& in) override {
        return compute_cached_layout(*this, n, in, [](LayoutTree& base, NodeId child, const LayoutInput& input) {
            auto& tree = static_cast<CustomTree&>(base);
            switch (tree.node(child).kind) {
            case 0: return compute_block(tree, child, input, nullptr);
            case 1: return compute_grid(tree, child, input);
            default: return compute_leaf(tree, child, input);
            }
        });
    }
    Cache& cache(NodeId n) override { return node(n).cache; }
    std::optional<LayoutOutput> cache_get(NodeId n, const LayoutInput& in) override {
        ++counts.cache_get;
        auto result = cache(n).get(in);
        if (result) ++counts.cache_hit;
        return result;
    }
    void cache_store(NodeId n, const LayoutInput& in, LayoutOutput out) override {
        ++counts.cache_store;
        cache(n).store(in, std::move(out));
    }
    void cache_clear(NodeId n) override { ++counts.cache_clear; cache(n).clear(); }
    const Layout& unrounded_layout(NodeId n) const override { return node(n).layout; }
    void set_unrounded_layout(NodeId n, const Layout& l) override { node(n).layout = l; }
    void set_final_layout(NodeId n, const Layout& l) override { node(n).final_layout = l; }
    const DetailedLayoutInfo& detailed_layout_info(NodeId n) const override { return node(n).detail; }
    void set_detailed_grid_info(NodeId n, DetailedGridInfo info) override { node(n).detail = std::move(info); }
    void clear_hoisted_children(NodeId n) override { ++counts.hoist_clear; node(n).hoisted.clear(); }
    void add_hoisted_children(NodeId n, std::span<const NodeId> children) override {
        ++counts.hoist_add;
        auto& list = node(n).hoisted;
        list.insert(list.end(), children.begin(), children.end());
    }
    std::vector<NodeId> hoisted_child_ids(NodeId n) const override { return node(n).hoisted; }
};
std::unordered_map<std::uint64_t, std::unique_ptr<CustomTree>> registry;
// Invalid input is not a panic. All other exceptions are contained at the C
// boundary; with_tree poisons while its lock and affected tree are still live.
template<class F> int boundary(F&& f) noexcept {
    try { f(); return 0; }
    catch (const InvalidArgument&) { return 1; }
    catch (const DeliberatePanic&) { return 2; }
    catch (const CallbackFailure&) { return 2; }
    catch (...) { return 2; }
}
template<class F> void with_tree(std::uint64_t handle, F&& f) {
    std::lock_guard lock(registry_mutex);
    auto it = registry.find(handle);
    if (it == registry.end() || it->second->poisoned) throw InvalidArgument{};
    auto& tree = *it->second;
    try { f(tree); }
    catch (const InvalidArgument&) { throw; }
    catch (...) { tree.poisoned = true; throw; }
}
} // namespace

extern "C" int ct_create(CtResolve r, CtMeasure m, void* context, std::uint64_t* out) {
    return boundary([&] {
        output(out);
        if (!r || !m) throw InvalidArgument{};
        std::lock_guard lock(registry_mutex);
        const auto key = id();
        registry.emplace(key, std::make_unique<CustomTree>(r, m, context));
        *out = key;
    });
}
extern "C" int ct_destroy(std::uint64_t tree) {
    return boundary([&] { std::lock_guard lock(registry_mutex); if (!registry.erase(tree)) throw InvalidArgument{}; });
}
extern "C" int ct_calc(std::uint64_t tree, float px, float pct, std::uint64_t* out) {
    return boundary([&] {
        output(out);
        if (!std::isfinite(px) || !std::isfinite(pct)) throw InvalidArgument{};
        with_tree(tree, [&](CustomTree& t) {
            const auto key = id();
            t.calcs.emplace(key, Coefficients{px, pct});
            t.clear_all();
            *out = key;
        });
    });
}
extern "C" int ct_node(std::uint64_t tree, std::uint32_t kind, CtStyle s, std::uint64_t* out) {
    return boundary([&] {
        output(out);
        if (kind > 2) throw InvalidArgument{};
        with_tree(tree, [&](CustomTree& t) {
            auto style = t.import_style(kind, s);
            const auto key = id();
            Node node{}; node.kind = kind; node.style = std::move(style);
            t.nodes.emplace(key, std::move(node));
            t.clear_all();
            *out = key;
        });
    });
}
extern "C" int ct_children(std::uint64_t tree, std::uint64_t root, const std::uint64_t* children, std::uint32_t count) {
    return boundary([&] {
        if (count) {
            output(children);
            if (static_cast<std::size_t>(count) > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) / sizeof(*children))
                throw InvalidArgument{};
        }
        with_tree(tree, [&](CustomTree& t) {
            auto& root_node = t.required(root);
            if (root_node.kind == 2 && count) throw InvalidArgument{};
            std::unordered_set<std::uint64_t> seen;
            std::vector<NodeId> imported;
            imported.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto child = children[i];
                if (child == root || !seen.insert(child).second || t.required(child).kind != 2) throw InvalidArgument{};
                for (const auto& [key, node] : t.nodes)
                    if (key != root && std::find(node.children.begin(), node.children.end(), NodeId{child}) != node.children.end())
                        throw InvalidArgument{};
                imported.push_back(NodeId{child});
            }
            root_node.children = std::move(imported);
            t.clear_all();
        });
    });
}
extern "C" int ct_style(std::uint64_t tree, std::uint64_t node, CtStyle s) {
    return boundary([&] { with_tree(tree, [&](CustomTree& t) {
        auto& old = t.required(node);
        auto style = t.import_style(old.kind, s);
        style.grid_template_rows = old.style.grid_template_rows;
        style.grid_template_columns = old.style.grid_template_columns;
        old.style = std::move(style);
        t.clear_all();
    }); });
}
extern "C" int ct_track(std::uint64_t tree, std::uint64_t node, std::uint32_t axis, CtValue v) {
    return boundary([&] { with_tree(tree, [&](CustomTree& t) {
        if (axis > 1 || t.required(node).kind != 1) throw InvalidArgument{};
        LengthPercentage limit;
        switch (v.tag) {
        case 1: if (!std::isfinite(v.value)) throw InvalidArgument{}; limit = LengthPercentage::length(v.value); break;
        case 2: if (!std::isfinite(v.value)) throw InvalidArgument{}; limit = LengthPercentage::percent(v.value); break;
        case 3: limit = LengthPercentage::calc(t.calc(v.calc)); break;
        default: throw InvalidArgument{};
        }
        auto& style = t.required(node).style;
        auto& tracks = axis == 0 ? style.grid_template_rows : style.grid_template_columns;
        tracks.clear();
        tracks.emplace_back(TrackSizingFunction::fit_content(limit));
        t.clear_all();
    }); });
}
extern "C" int ct_compute(std::uint64_t tree, std::uint64_t root, CtAvailable w, CtAvailable h) {
    return boundary([&] {
        const Size<AvailableSpace> space{available(w), available(h)};
        with_tree(tree, [&](CustomTree& t) { t.required(root); compute_root_layout(t, NodeId{root}, space); });
    });
}
extern "C" int ct_layout(std::uint64_t tree, std::uint64_t node, CtSize* out) {
    return boundary([&] {
        output(out);
        CtSize result{};
        with_tree(tree, [&](CustomTree& t) { const auto size = t.required(node).layout.size; result = {size.width, size.height}; });
        *out = result;
    });
}
extern "C" int ct_counts(std::uint64_t tree, CtCounts* out) {
    return boundary([&] { output(out); CtCounts result{}; with_tree(tree, [&](CustomTree& t) { result = t.counts; }); *out = result; });
}
extern "C" int ct_panic_probe() { return boundary([] { throw DeliberatePanic{}; }); }
extern "C" int ct_tree_panic_probe(std::uint64_t tree, std::uint64_t node) {
    return boundary([&] { with_tree(tree, [&](CustomTree& t) { t.required(node).layout.size.width = -999; throw DeliberatePanic{}; }); });
}
