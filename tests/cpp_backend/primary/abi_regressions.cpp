// Native-only ABI regressions: link with primary_backend.cpp, never a Rust adapter.
#include "../../support/oracle_abi.h"
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
constexpr OracleSizing max_space{{4, 0}, {4, 0}};
struct Context {
    OracleContext* p = nullptr;
    Context() { REQUIRE(oracle_create(&p) == ORACLE_OK); REQUIRE(p != nullptr); }
    ~Context() { if (p) (void)oracle_free(p); }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    void free() { auto* old = p; p = nullptr; REQUIRE(oracle_free(old) == ORACLE_OK); }
};
OracleStyle defaults() {
    OracleStyle s{};
    REQUIRE(oracle_default_style(&s) == ORACLE_OK);
    return s;
}
uint64_t node(Context& c, const OracleStyle& s) {
    uint64_t id = 0;
    REQUIRE(oracle_new_node(c.p, &s, nullptr, nullptr, nullptr, &id) == ORACLE_OK);
    REQUIRE(id != 0);
    return id;
}
void scalar(OracleScalar a, OracleScalar b) {
    REQUIRE(a.tag == b.tag); REQUIRE(a.value == b.value);
}
void sizing(OracleSizing a, OracleSizing b) { scalar(a.width, b.width); scalar(a.height, b.height); }
void edges(OracleEdges a, OracleEdges b) {
    scalar(a.left, b.left); scalar(a.right, b.right); scalar(a.top, b.top); scalar(a.bottom, b.bottom);
}
void same_style(const OracleStyle& a, const OracleStyle& b) {
    sizing(a.size, b.size); sizing(a.min_size, b.min_size); sizing(a.max_size, b.max_size);
    edges(a.padding, b.padding); edges(a.border, b.border); edges(a.margin, b.margin); edges(a.inset, b.inset);
    sizing(a.gap, b.gap); scalar(a.flex_basis, b.flex_basis);
    REQUIRE(a.aspect_ratio.present == b.aspect_ratio.present); REQUIRE(a.aspect_ratio.value == b.aspect_ratio.value);
    REQUIRE(a.flex_grow == b.flex_grow); REQUIRE(a.flex_shrink == b.flex_shrink);
    REQUIRE(a.scrollbar_width == b.scrollbar_width);
    REQUIRE(a.display == b.display); REQUIRE(a.position == b.position); REQUIRE(a.box_sizing == b.box_sizing);
    REQUIRE(a.overflow_x == b.overflow_x); REQUIRE(a.overflow_y == b.overflow_y);
    REQUIRE(a.flex_direction == b.flex_direction); REQUIRE(a.flex_wrap == b.flex_wrap); REQUIRE(a.direction == b.direction);
    REQUIRE(a.align_items == b.align_items); REQUIRE(a.align_self == b.align_self);
    REQUIRE(a.justify_items == b.justify_items); REQUIRE(a.justify_self == b.justify_self);
    REQUIRE(a.align_content == b.align_content); REQUIRE(a.justify_content == b.justify_content);
}
struct Probe {
    int drops = 0;
    unsigned calls = 0;
    uint32_t status = 0;
    bool throws = false;
    OracleLayoutOutput result{};
    std::vector<OracleLayoutInput> inputs;
    std::vector<OracleStyle> styles;
    std::vector<uint64_t> ids;
};
void drop(void* p) { ++static_cast<Probe*>(p)->drops; }
uint32_t measure(void* p, uint64_t id, const OracleLayoutInput* in, const OracleStyle* style,
                 OracleLayoutOutput* out) {
    auto& probe = *static_cast<Probe*>(p);
    ++probe.calls;
    probe.inputs.push_back(*in); probe.styles.push_back(*style); probe.ids.push_back(id);
    if (probe.throws) throw std::runtime_error("intentional ABI exception capture probe");
    *out = probe.result;
    return probe.status;
}
uint64_t measured(Context& c, const OracleStyle& s, Probe& p) {
    uint64_t id = 0;
    REQUIRE(oracle_new_node(c.p, &s, &p, measure, drop, &id) == ORACLE_OK);
    return id;
}
OracleLayout layout(Context& c, uint64_t id, uint32_t unrounded = 1) {
    OracleLayout l{}; REQUIRE(oracle_layout(c.p, id, unrounded, &l) == ORACLE_OK); return l;
}
OracleLayoutInput leaf_input() {
    OracleLayoutInput i{}; i.sizing_mode = 1; i.axis = 2; i.available_space = max_space; return i;
}
struct Intrinsic {
    unsigned calls = 0;
    uint32_t status = 0;
    bool throws = false;
    OracleKnown known{};
    OracleSizing available{};
};
uint32_t intrinsic(void* p, OracleKnown known, OracleSizing available, OracleSize* out) {
    auto& data = *static_cast<Intrinsic*>(p);
    ++data.calls; data.known = known; data.available = available;
    if (data.throws) throw std::runtime_error("intentional intrinsic exception");
    out->width = known.width.present ? known.width.value :
        available.width.tag == 1 ? available.width.value / 2 : available.width.tag == 3 ? 7.0f : 19.0f;
    out->height = known.height.present ? known.height.value :
        available.height.tag == 1 ? available.height.value / 2 : available.height.tag == 3 ? 11.0f : 23.0f;
    return data.status;
}
uint32_t leaf_measure(void* p, uint64_t, const OracleLayoutInput* in, const OracleStyle* s,
                      OracleLayoutOutput* out) {
    return oracle_leaf(s, in, p, intrinsic, out);
}
} // namespace

TEST_CASE("primary ABI native columns are transactional and scalar replacement clears them", "[primary][abi]") {
    Context c;
    auto s = defaults(); s.display = 1; s.size = {{1, 120}, {1, 30}};
    const auto root = node(c, s);
    auto child_style = defaults(); child_style.size.height = {1, 12};
    const auto child = node(c, child_style);
    REQUIRE(oracle_set_children(c.p, root, &child, 1) == ORACLE_OK);
    const std::array<OracleGridTrack, 2> tracks{{{{1, 35}, {1, 35}}, {{2, .25f}, {2, .25f}}}};
    REQUIRE(oracle_compute(c.p, root, max_space) == ORACLE_OK);
    REQUIRE(oracle_set_grid_template_columns(c.p, root, tracks.data(), tracks.size()) == ORACLE_OK);
    uint32_t dirty = 0;
    REQUIRE(oracle_dirty(c.p, root, &dirty) == ORACLE_OK); REQUIRE(dirty == 1);
    REQUIRE(oracle_compute(c.p, root, max_space) == ORACLE_OK);
    REQUIRE(layout(c, child).size.width == 35);
    REQUIRE(oracle_dirty(c.p, root, &dirty) == ORACLE_OK); REQUIRE(dirty == 0);
    for (const uint32_t tag : {0u, 3u, 4u, 10u, 11u, 99u}) {
        CAPTURE(tag);
        auto bad = tracks; bad[1].max.tag = tag;
        REQUIRE(oracle_set_grid_template_columns(c.p, root, bad.data(), bad.size()) == ORACLE_UNSUPPORTED);
        bad = tracks; bad[1].min.tag = tag;
        REQUIRE(oracle_set_grid_template_columns(c.p, root, bad.data(), bad.size()) == ORACLE_UNSUPPORTED);
    }
    REQUIRE(oracle_set_grid_template_columns(c.p, root, nullptr, 1) == ORACLE_INVALID);
    REQUIRE(oracle_set_grid_template_columns(c.p, root, tracks.data(), std::numeric_limits<size_t>::max()) == ORACLE_INVALID);
    REQUIRE(oracle_dirty(c.p, root, &dirty) == ORACLE_OK); REQUIRE(dirty == 0);
    size_t count = 99;
    REQUIRE(oracle_get_grid_template_columns(c.p, root, nullptr, 0, &count) == ORACLE_OK); REQUIRE(count == 2);
    OracleGridTrack sentinel{{1, 901}, {2, 902}};
    REQUIRE(oracle_get_grid_template_columns(c.p, root, &sentinel, 1, &count) == ORACLE_INVALID);
    REQUIRE(sentinel.min.value == 901); REQUIRE(sentinel.max.value == 902);
    REQUIRE(oracle_get_grid_template_columns(c.p, root, nullptr, 2, &count) == ORACLE_INVALID);
    REQUIRE(oracle_get_grid_template_columns(c.p, root, &sentinel, 1, nullptr) == ORACLE_INVALID);
    std::array<OracleGridTrack, 3> copied{};
    REQUIRE(oracle_get_grid_template_columns(c.p, root, copied.data(), copied.size(), &count) == ORACLE_OK);
    REQUIRE(count == 2);
    for (size_t i = 0; i != count; ++i) { scalar(copied[i].min, tracks[i].min); scalar(copied[i].max, tracks[i].max); }
    OracleStyle snapshot{};
    REQUIRE(oracle_get_style(c.p, root, &snapshot) == ORACLE_OK); same_style(snapshot, s);
    auto invalid = s; invalid.direction = 99;
    REQUIRE(oracle_set_style(c.p, root, &invalid) == ORACLE_UNSUPPORTED);
    REQUIRE(oracle_get_grid_template_columns(c.p, root, nullptr, 0, &count) == ORACLE_OK); REQUIRE(count == 2);
    REQUIRE(oracle_set_style(c.p, root, &s) == ORACLE_OK);
    REQUIRE(oracle_get_grid_template_columns(c.p, root, nullptr, 0, &count) == ORACLE_OK); REQUIRE(count == 0);
    REQUIRE(oracle_dirty(c.p, root, &dirty) == ORACLE_OK); REQUIRE(dirty == 1);
    REQUIRE(oracle_get_style(c.p, root, &snapshot) == ORACLE_OK); same_style(snapshot, s);
    REQUIRE(oracle_set_grid_template_columns(c.p, root, tracks.data(), 1) == ORACLE_OK);
    REQUIRE(oracle_set_grid_template_columns(c.p, root, nullptr, 0) == ORACLE_OK);
    REQUIRE(oracle_get_grid_template_columns(c.p, root, nullptr, 0, &count) == ORACLE_OK); REQUIRE(count == 0);
}

TEST_CASE("primary ABI ownership transfers only on success and handles never recycle", "[primary][abi]") {
    Probe a, b, failed, removed, cleared;
    Context c;
    auto s = defaults(); auto bad = s; bad.size.width.tag = 10;
    uint64_t out = 777;
    REQUIRE(oracle_new_node(c.p, &bad, &failed, measure, drop, &out) == ORACLE_UNSUPPORTED);
    REQUIRE(out == 777); REQUIRE(failed.drops == 0);
    REQUIRE(oracle_new_node(c.p, &s, &failed, measure, drop, nullptr) == ORACLE_INVALID);
    REQUIRE(oracle_new_node(nullptr, &s, &failed, measure, drop, &out) == ORACLE_INVALID);
    REQUIRE(failed.drops == 0);
    const auto first = measured(c, s, a);
    REQUIRE(oracle_set_context(c.p, first + 100, &failed, measure, drop) == ORACLE_STALE);
    REQUIRE(failed.drops == 0); REQUIRE(a.drops == 0);
    REQUIRE(oracle_set_context(c.p, first, &b, measure, drop) == ORACLE_OK);
    REQUIRE(a.drops == 1); REQUIRE(b.drops == 0);
    REQUIRE(oracle_set_context(c.p, first, nullptr, nullptr, nullptr) == ORACLE_OK); REQUIRE(b.drops == 1);
    REQUIRE(oracle_set_context(c.p, first, nullptr, nullptr, nullptr) == ORACLE_OK); REQUIRE(b.drops == 1);
    const auto second = measured(c, s, removed); REQUIRE(second > first);
    REQUIRE(oracle_remove(c.p, second) == ORACLE_OK); REQUIRE(removed.drops == 1);
    REQUIRE(oracle_remove(c.p, second) == ORACLE_STALE);
    const auto third = measured(c, s, cleared); REQUIRE(third > second);
    REQUIRE(oracle_clear(c.p) == ORACLE_OK); REQUIRE(cleared.drops == 1);
    REQUIRE(oracle_set_context(c.p, third, &failed, measure, drop) == ORACLE_STALE);
    const auto fourth = measured(c, s, a); REQUIRE(fourth > third);
    REQUIRE(oracle_clear(c.p) == ORACLE_OK); REQUIRE(a.drops == 2);
    const auto fifth = measured(c, s, b); REQUIRE(fifth > fourth);
    const auto parent = measured(c, s, removed);
    REQUIRE(oracle_set_children(c.p, parent, &fifth, 1) == ORACLE_OK);
    REQUIRE(oracle_remove(c.p, parent) == ORACLE_OK); REQUIRE(removed.drops == 2);
    REQUIRE(b.drops == 1); // Removing a parent detaches, but does not destroy, its children.
    uint64_t parent_id = 999;
    REQUIRE(oracle_parent(c.p, fifth, &parent_id) == ORACLE_OK); REQUIRE(parent_id == 0);
    c.free(); REQUIRE(b.drops == 2); REQUIRE(removed.drops == 2); REQUIRE(cleared.drops == 1); REQUIRE(failed.drops == 0);
}

TEST_CASE("primary ABI topology cache rounding and position predicates are direct C calls", "[primary][abi]") {
    Context c; auto s = defaults(); s.size = {{1, 10.25f}, {1, 5.75f}};
    const auto root = node(c, defaults()); const auto child = node(c, s);
    REQUIRE(oracle_set_children(c.p, root, &child, 1) == ORACLE_OK);
    uint64_t id = 999; size_t count = 99;
    REQUIRE(oracle_parent(c.p, root, &id) == ORACLE_OK); REQUIRE(id == 0);
    REQUIRE(oracle_parent(c.p, child, &id) == ORACLE_OK); REQUIRE(id == root);
    REQUIRE(oracle_child(c.p, root, 0, &id) == ORACLE_OK); REQUIRE(id == child);
    REQUIRE(oracle_child_count(c.p, root, &count) == ORACLE_OK); REQUIRE(count == 1);
    REQUIRE(oracle_child(c.p, root, 1, &id) == ORACLE_ERROR);
    const std::array<uint64_t, 2> duplicate{child, child};
    REQUIRE(oracle_set_children(c.p, root, duplicate.data(), duplicate.size()) == ORACLE_ERROR);
    REQUIRE(oracle_set_children(c.p, child, &root, 1) == ORACLE_ERROR);
    REQUIRE(oracle_rounding(c.p, 0) == ORACLE_OK);
    REQUIRE(oracle_compute(c.p, root, max_space) == ORACLE_OK);
    REQUIRE(layout(c, child, 0).size.width == 10.25f);
    REQUIRE(oracle_rounding(c.p, 1) == ORACLE_OK);
    REQUIRE(oracle_compute(c.p, root, max_space) == ORACLE_OK);
    REQUIRE(layout(c, child, 0).size.width == 10); REQUIRE(layout(c, child).size.width == 10.25f);
    uint32_t dirty = 99;
    REQUIRE(oracle_dirty(c.p, root, &dirty) == ORACLE_OK); REQUIRE(dirty == 0);
    REQUIRE(oracle_mark_dirty(c.p, child) == ORACLE_OK);
    REQUIRE(oracle_dirty(c.p, root, &dirty) == ORACLE_OK); REQUIRE(dirty == 1);
    REQUIRE(oracle_remove(c.p, child) == ORACLE_OK);
    REQUIRE(oracle_child_count(c.p, root, &count) == ORACLE_OK); REQUIRE(count == 0);
    REQUIRE(oracle_layout(c.p, child, 1, nullptr) == ORACLE_STALE);
    for (uint32_t tag = 0; tag != 5; ++tag) {
        uint32_t positioned = 9, oof = 9;
        REQUIRE(oracle_position_is_positioned(tag, &positioned) == ORACLE_OK);
        REQUIRE(oracle_position_is_out_of_flow(tag, &oof) == ORACLE_OK);
        REQUIRE(positioned == (tag == 3 ? 0 : 1)); REQUIRE(oof == (tag == 1 || tag == 2 ? 1 : 0));
    }
}

TEST_CASE("primary ABI callback receives full root input and lossless scalar snapshot", "[primary][abi]") {
    Probe probe; Context c;
    auto s = defaults(); s.display = 2; s.size.width = {1, 80};
    s.margin = {{1, 2}, {1, 3}, {1, 4}, {1, 5}};
    s.padding = {{1, 6}, {1, 7}, {1, 8}, {1, 9}};
    s.border = {{1, 1}, {1, 2}, {1, 3}, {1, 4}};
    s.inset = {{2, .1f}, {0, 0}, {1, 2}, {0, 0}};
    s.gap = {{1, 11}, {2, .2f}}; s.flex_basis = {6, 42};
    s.flex_grow = 2; s.flex_shrink = 3; s.scrollbar_width = 5;
    s.align_self = ORACLE_ALIGN_NORMAL; s.justify_self = ORACLE_ALIGN_SELF_END;
    probe.result.size = {80.25f, 40.5f};
    probe.result.scrollable_overflow = {-2, 101, -3, 73};
    probe.result.first_baseline = {1, 13}; probe.result.last_baseline = {1, 29};
    probe.result.top_positive = 11; probe.result.top_negative = -4;
    probe.result.bottom_positive = 17; probe.result.bottom_negative = -6;
    const auto root = measured(c, s, probe);
    REQUIRE(oracle_rounding(c.p, 0) == ORACLE_OK);
    REQUIRE(oracle_compute(c.p, root, {{1, 200}, {1, 100}}) == ORACLE_OK);
    REQUIRE(probe.calls == 1); REQUIRE(probe.ids.front() == root); same_style(probe.styles.front(), s);
    const auto& in = probe.inputs.front();
    REQUIRE(in.run_mode == 0); REQUIRE(in.sizing_mode == 1); REQUIRE(in.axis == 2);
    REQUIRE(in.known_dimensions.width.present == 1); REQUIRE(in.known_dimensions.width.value == 80);
    REQUIRE(in.known_dimensions.height.present == 0);
    REQUIRE(in.definite_width == 1); REQUIRE(in.definite_height == 1);
    REQUIRE(in.parent_size.width.present == 1); REQUIRE(in.parent_size.width.value == 200);
    REQUIRE(in.parent_size.height.present == 1); REQUIRE(in.parent_size.height.value == 100);
    sizing(in.available_space, {{1, 195}, {1, 91}});
    REQUIRE(in.collapse_start == 0); REQUIRE(in.collapse_end == 0);
    const auto l = layout(c, root);
    REQUIRE(l.size.width == 80.25f); REQUIRE(l.size.height == 40.5f);
    REQUIRE(l.scrollable_overflow.left == -2); REQUIRE(l.scrollable_overflow.right == 101);
    REQUIRE(l.scrollable_overflow.top == -3); REQUIRE(l.scrollable_overflow.bottom == 73);
    REQUIRE(l.padding.left == 6); REQUIRE(l.border.bottom == 4); REQUIRE(l.margin.top == 4);
    REQUIRE(l.order == 0); REQUIRE(l.x == 2); REQUIRE(l.y == 4);
    REQUIRE(l.content_size.width == 64.25f); REQUIRE(l.content_size.height == 16.5f);
    REQUIRE(l.scrollbar_size.width == 0); REQUIRE(l.scrollbar_size.height == 0);
    REQUIRE(l.scroll_width == 23.75f); REQUIRE(l.scroll_height == 39.5f);
    REQUIRE(oracle_compute(c.p, root, {{1, 200}, {1, 100}}) == ORACLE_OK); REQUIRE(probe.calls == 1);
}

TEST_CASE("primary ABI callback first baselines affect flex alignment", "[primary][abi]") {
    Probe a, b; Context c;
    a.result.size = {20, 30}; a.result.first_baseline = {1, 7}; a.result.last_baseline = {1, 27};
    b.result.size = {20, 30}; b.result.first_baseline = {1, 19}; b.result.last_baseline = {1, 21};
    auto s = defaults(); s.align_items = ORACLE_ALIGN_BASELINE;
    const auto root = node(c, s); const auto x = measured(c, defaults(), a); const auto y = measured(c, defaults(), b);
    const std::array<uint64_t, 2> children{x, y};
    REQUIRE(oracle_set_children(c.p, root, children.data(), children.size()) == ORACLE_OK);
    REQUIRE(oracle_compute(c.p, root, max_space) == ORACLE_OK);
    REQUIRE(layout(c, x).y == 12); REQUIRE(layout(c, y).y == 0);
}

TEST_CASE("primary ABI callback margin components collapse without premature summing", "[primary][abi]") {
    Probe a, b; Context c;
    a.result.size = {20, 10}; a.result.top_positive = 12; a.result.top_negative = -5;
    a.result.bottom_positive = 18; a.result.bottom_negative = -4;
    b.result.size = {20, 10}; b.result.top_positive = 10; b.result.top_negative = -9;
    b.result.bottom_positive = 8; b.result.bottom_negative = -2;
    auto s = defaults(); s.display = 2; s.size.width = {1, 100};
    s.padding.top = {1, 1}; s.padding.bottom = {1, 1};
    const auto root = node(c, s);
    auto leaf = defaults(); leaf.display = 2;
    const auto x = measured(c, leaf, a); const auto y = measured(c, leaf, b);
    const std::array<uint64_t, 2> children{x, y};
    REQUIRE(oracle_set_children(c.p, root, children.data(), children.size()) == ORACLE_OK);
    REQUIRE(oracle_compute(c.p, root, max_space) == ORACLE_OK);
    REQUIRE(layout(c, x).y == 8);
    // max(18,10) + min(-4,-9) = 9, not max(14,1) = 14.
    REQUIRE(layout(c, y).y == 27);
    REQUIRE(layout(c, root).size.height == 44);
    REQUIRE_FALSE(a.inputs.empty());
    REQUIRE(a.inputs.back().collapse_start == 1); REQUIRE(a.inputs.back().collapse_end == 1);
    // A zero-height, collapsing-through leaf must retain both adjoining sets.
    a.result.size.height = 0; a.result.margins_can_collapse_through = 1;
    REQUIRE(oracle_mark_dirty(c.p, x) == ORACLE_OK);
    REQUIRE(oracle_compute(c.p, root, max_space) == ORACLE_OK);
    REQUIRE(layout(c, y).y == 10);
    REQUIRE(layout(c, root).size.height == 27);
}

TEST_CASE("primary ABI invalid callback output poisons only its tree and preserves ownership", "[primary][abi]") {
    for (unsigned variant = 0; variant != 7; ++variant) {
        CAPTURE(variant);
        Probe p, retained, rejected; Context bad, good;
        if (variant == 0) p.result.oof_candidates_count = 1;
        if (variant == 1) p.result.oof_area_present = 1;
        if (variant == 2) p.result.first_baseline.present = 2;
        if (variant == 3) p.result.last_baseline.present = 2;
        if (variant == 4) p.result.margins_can_collapse_through = 2;
        if (variant == 5) p.result.top_positive = -1;
        if (variant == 6) p.result.bottom_negative = 1;
        const auto s = defaults(); const auto id = measured(bad, s, p); (void)measured(bad, s, retained);
        const auto healthy = node(good, s);
        const uint32_t expected = variant < 5 ? ORACLE_UNSUPPORTED : ORACLE_INVALID;
        REQUIRE(oracle_compute(bad.p, id, max_space) == expected);
        REQUIRE(oracle_clear(bad.p) == ORACLE_POISONED);
        REQUIRE(oracle_remove(bad.p, id) == ORACLE_POISONED);
        REQUIRE(oracle_set_context(bad.p, id, &rejected, measure, drop) == ORACLE_POISONED);
        REQUIRE(p.drops == 0); REQUIRE(retained.drops == 0); REQUIRE(rejected.drops == 0);
        REQUIRE(oracle_compute(good.p, healthy, max_space) == ORACLE_OK);
        bad.free(); REQUIRE(p.drops == 1); REQUIRE(retained.drops == 1); REQUIRE(rejected.drops == 0);
    }
}

TEST_CASE("primary ABI callback failures and deliberate panic have distinct poison boundaries", "[primary][abi]") {
    for (unsigned variant = 0; variant != 3; ++variant) {
        CAPTURE(variant);
        Probe p, retained, rejected; Context bad, good;
        p.status = variant == 0 ? ORACLE_UNSUPPORTED : 0; p.throws = variant == 1;
        auto s = defaults(); const auto id = measured(bad, s, p); (void)measured(bad, s, retained);
        const auto healthy = node(good, s);
        if (variant == 2) REQUIRE(oracle_test_panic(bad.p) == ORACLE_PANIC);
        else REQUIRE(oracle_compute(bad.p, id, max_space) == ORACLE_CALLBACK_FAILURE);
        uint64_t out = 987; uint32_t dirty = 99; OracleStyle style{}; OracleLayout l{}; size_t count = 0;
        REQUIRE(oracle_new_node(bad.p, &s, &rejected, measure, drop, &out) == ORACLE_POISONED); REQUIRE(out == 987);
        REQUIRE(oracle_set_context(bad.p, id, &rejected, measure, drop) == ORACLE_POISONED);
        REQUIRE(oracle_set_style(bad.p, id, &s) == ORACLE_POISONED);
        REQUIRE(oracle_set_grid_template_columns(bad.p, id, nullptr, 0) == ORACLE_POISONED);
        REQUIRE(oracle_get_grid_template_columns(bad.p, id, nullptr, 0, &count) == ORACLE_POISONED);
        REQUIRE(oracle_get_style(bad.p, id, &style) == ORACLE_POISONED);
        REQUIRE(oracle_parent(bad.p, id, &out) == ORACLE_POISONED);
        REQUIRE(oracle_child(bad.p, id, 0, &out) == ORACLE_POISONED);
        REQUIRE(oracle_child_count(bad.p, id, &count) == ORACLE_POISONED);
        REQUIRE(oracle_set_children(bad.p, id, nullptr, 0) == ORACLE_POISONED);
        REQUIRE(oracle_dirty(bad.p, id, &dirty) == ORACLE_POISONED);
        REQUIRE(oracle_mark_dirty(bad.p, id) == ORACLE_POISONED);
        REQUIRE(oracle_rounding(bad.p, 0) == ORACLE_POISONED);
        REQUIRE(oracle_compute(bad.p, id, max_space) == ORACLE_POISONED);
        REQUIRE(oracle_layout(bad.p, id, 1, &l) == ORACLE_POISONED);
        REQUIRE(oracle_test_panic(bad.p) == ORACLE_POISONED);
        REQUIRE(oracle_remove(bad.p, id) == ORACLE_POISONED); REQUIRE(oracle_clear(bad.p) == ORACLE_POISONED);
        REQUIRE(p.drops == 0); REQUIRE(retained.drops == 0); REQUIRE(rejected.drops == 0);
        REQUIRE(oracle_compute(good.p, healthy, max_space) == ORACLE_OK);
        REQUIRE(oracle_default_style(&style) == ORACLE_OK);
        bad.free(); REQUIRE(p.drops == 1); REQUIRE(retained.drops == 1); REQUIRE(rejected.drops == 0);
    }
}

TEST_CASE("primary ABI stateless leaf uses dynamic intrinsic known and available input", "[primary][abi]") {
    auto s = defaults(); auto in = leaf_input(); Intrinsic data; OracleLayoutOutput out{};
    REQUIRE(oracle_leaf(&s, &in, &data, intrinsic, &out) == ORACLE_OK);
    REQUIRE(out.size.width == 19); REQUIRE(out.size.height == 23);
    REQUIRE(data.known.width.present == 0); REQUIRE(data.known.height.present == 0);
    sizing(data.available, max_space);
    in.available_space = {{3, 0}, {1, 80}};
    REQUIRE(oracle_leaf(&s, &in, &data, intrinsic, &out) == ORACLE_OK);
    REQUIRE(out.size.width == 7); REQUIRE(out.size.height == 40); sizing(data.available, in.available_space);
    in.run_mode = 1; in.known_dimensions.width = {1, 33};
    REQUIRE(oracle_leaf(&s, &in, &data, intrinsic, &out) == ORACLE_OK);
    REQUIRE(data.known.width.present == 1); REQUIRE(data.known.width.value == 33);
    REQUIRE(data.known.height.present == 0); sizing(data.available, {{1, 33}, {1, 80}});
    REQUIRE(out.size.width == 33); REQUIRE(out.size.height == 40);
    in.run_mode = 0;
    REQUIRE(oracle_leaf(&s, &in, &data, intrinsic, &out) == ORACLE_OK);
    REQUIRE(data.known.width.present == 0); REQUIRE(out.size.width == 33);
    data.status = 42;
    REQUIRE(oracle_leaf(&s, &in, &data, intrinsic, &out) == ORACLE_CALLBACK_FAILURE);
    data.status = 0; data.throws = true;
    REQUIRE(oracle_leaf(&s, &in, &data, intrinsic, &out) == ORACLE_CALLBACK_FAILURE);
    data.throws = false; in = leaf_input();
    REQUIRE(oracle_leaf(&s, &in, &data, intrinsic, &out) == ORACLE_OK); REQUIRE(out.size.width == 19);
    REQUIRE(oracle_leaf(&s, &in, nullptr, nullptr, &out) == ORACLE_OK); REQUIRE(out.size.width == 0); REQUIRE(out.size.height == 0);
    // The callback observes content-box availability, not the caller's raw outer space.
    s.padding = {{1, 2}, {1, 3}, {1, 4}, {1, 5}};
    in.available_space = {{1, 105}, {1, 89}};
    REQUIRE(oracle_leaf(&s, &in, &data, intrinsic, &out) == ORACLE_OK);
    sizing(data.available, {{1, 100}, {1, 80}});
    REQUIRE(out.size.width == 55); REQUIRE(out.size.height == 49);
    REQUIRE(out.first_baseline.present == 0); REQUIRE(out.last_baseline.present == 0);
    REQUIRE(out.top_positive == 0); REQUIRE(out.top_negative == 0);
    REQUIRE(out.bottom_positive == 0); REQUIRE(out.bottom_negative == 0);
    REQUIRE(out.oof_candidates_count == 0); REQUIRE(out.oof_area_present == 0);
}

TEST_CASE("primary ABI stateless leaf is callable inside a tree callback and after tree poison", "[primary][abi]") {
    Intrinsic data; Context c; auto s = defaults(); uint64_t id = 0;
    REQUIRE(oracle_new_node(c.p, &s, &data, leaf_measure, nullptr, &id) == ORACLE_OK);
    REQUIRE(oracle_compute(c.p, id, {{1, 100}, {1, 80}}) == ORACLE_OK);
    REQUIRE(data.calls == 1); REQUIRE(layout(c, id).size.width == 50); REQUIRE(layout(c, id).size.height == 40);
    REQUIRE(oracle_test_panic(c.p) == ORACLE_PANIC);
    auto in = leaf_input(); OracleLayoutOutput out{};
    REQUIRE(oracle_leaf(&s, &in, &data, intrinsic, &out) == ORACLE_OK);
    REQUIRE(out.size.width == 19); REQUIRE(out.size.height == 23);
}

TEST_CASE("primary ABI rejects invalid booleans and null pointers without poisoning", "[primary][abi]") {
    Context c; auto s = defaults(); const auto id = node(c, s);
    REQUIRE(oracle_create(nullptr) == ORACLE_INVALID); REQUIRE(oracle_free(nullptr) == ORACLE_OK);
    REQUIRE(oracle_default_style(nullptr) == ORACLE_INVALID);
    uint32_t value = 99;
    REQUIRE(oracle_position_is_positioned(99, &value) == ORACLE_UNSUPPORTED); REQUIRE(value == 99);
    REQUIRE(oracle_position_is_out_of_flow(99, &value) == ORACLE_UNSUPPORTED);
    REQUIRE(oracle_position_is_positioned(0, nullptr) == ORACLE_INVALID);
    REQUIRE(oracle_position_is_out_of_flow(0, nullptr) == ORACLE_INVALID);
    REQUIRE(oracle_rounding(c.p, 2) == ORACLE_UNSUPPORTED);
    OracleLayout l{};
    REQUIRE(oracle_layout(c.p, id, 2, &l) == ORACLE_UNSUPPORTED);
    REQUIRE(oracle_layout(c.p, id, 0, nullptr) == ORACLE_INVALID);
    REQUIRE(oracle_get_style(c.p, id, nullptr) == ORACLE_INVALID);
    REQUIRE(oracle_set_style(c.p, id, nullptr) == ORACLE_INVALID);
    REQUIRE(oracle_parent(c.p, id, nullptr) == ORACLE_INVALID);
    REQUIRE(oracle_child_count(c.p, id, nullptr) == ORACLE_INVALID);
    REQUIRE(oracle_dirty(c.p, id, nullptr) == ORACLE_INVALID);
    REQUIRE(oracle_set_children(c.p, id, nullptr, 1) == ORACLE_INVALID);
    REQUIRE(oracle_set_children(c.p, id, &id, std::numeric_limits<size_t>::max()) == ORACLE_INVALID);
    uint64_t output = 0;
    REQUIRE(oracle_new_node(c.p, nullptr, nullptr, nullptr, nullptr, &output) == ORACLE_INVALID);
    REQUIRE(oracle_clear(nullptr) == ORACLE_INVALID); REQUIRE(oracle_compute(nullptr, id, max_space) == ORACLE_INVALID);
    size_t count = 0; OracleStyle snapshot{};
    REQUIRE(oracle_set_context(nullptr, id, nullptr, nullptr, nullptr) == ORACLE_INVALID);
    REQUIRE(oracle_remove(nullptr, id) == ORACLE_INVALID);
    REQUIRE(oracle_set_style(nullptr, id, &s) == ORACLE_INVALID);
    REQUIRE(oracle_get_style(nullptr, id, &snapshot) == ORACLE_INVALID);
    REQUIRE(oracle_set_grid_template_columns(nullptr, id, nullptr, 0) == ORACLE_INVALID);
    REQUIRE(oracle_get_grid_template_columns(nullptr, id, nullptr, 0, &count) == ORACLE_INVALID);
    REQUIRE(oracle_set_children(nullptr, id, nullptr, 0) == ORACLE_INVALID);
    REQUIRE(oracle_parent(nullptr, id, &output) == ORACLE_INVALID);
    REQUIRE(oracle_child(nullptr, id, 0, &output) == ORACLE_INVALID);
    REQUIRE(oracle_child_count(nullptr, id, &count) == ORACLE_INVALID);
    REQUIRE(oracle_dirty(nullptr, id, &value) == ORACLE_INVALID);
    REQUIRE(oracle_mark_dirty(nullptr, id) == ORACLE_INVALID);
    REQUIRE(oracle_rounding(nullptr, 0) == ORACLE_INVALID);
    REQUIRE(oracle_layout(nullptr, id, 0, &l) == ORACLE_INVALID);
    REQUIRE(oracle_test_panic(nullptr) == ORACLE_INVALID);
    OracleLayoutOutput out{}; auto in = leaf_input();
    REQUIRE(oracle_leaf(nullptr, &in, nullptr, nullptr, &out) == ORACLE_INVALID);
    REQUIRE(oracle_leaf(&s, nullptr, nullptr, nullptr, &out) == ORACLE_INVALID);
    REQUIRE(oracle_leaf(&s, &in, nullptr, nullptr, nullptr) == ORACLE_INVALID);
    for (unsigned field = 0; field != 13; ++field) {
        CAPTURE(field); auto invalid = in;
        switch (field) {
        case 0: invalid.known_dimensions.width.present = 2; break;
        case 1: invalid.known_dimensions.height.present = 2; break;
        case 2: invalid.parent_size.width.present = 2; break;
        case 3: invalid.parent_size.height.present = 2; break;
        case 4: invalid.definite_width = 2; break;
        case 5: invalid.definite_height = 2; break;
        case 6: invalid.collapse_start = 2; break;
        case 7: invalid.collapse_end = 2; break;
        case 8: invalid.run_mode = 99; break;
        case 9: invalid.sizing_mode = 99; break;
        case 10: invalid.axis = 99; break;
        case 11: invalid.available_space.width.tag = 0; break;
        default: invalid.available_space.height.tag = 2; break;
        }
        REQUIRE(oracle_leaf(&s, &invalid, nullptr, nullptr, &out) == ORACLE_UNSUPPORTED);
    }
    s.aspect_ratio.present = 2;
    REQUIRE(oracle_set_style(c.p, id, &s) == ORACLE_UNSUPPORTED);
    REQUIRE(oracle_leaf(&s, &in, nullptr, nullptr, &out) == ORACLE_UNSUPPORTED);
    REQUIRE(oracle_compute(c.p, id, max_space) == ORACLE_OK);
}
