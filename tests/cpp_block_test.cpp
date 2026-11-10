#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "../src/block.hpp"
#include "../src/oof.hpp"
#include <taffy/compute.hpp>
#include "../src/compute_common.hpp"

#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <span>
#include <vector>

using namespace taffy;

namespace {
// Expectations are source-level CSS equations, not captured Rust layouts.
// Pinned upstream: src/compute/{block,float,oof}.rs, revision 0064bd54.
// CSS absolute sizing/margin equations: https://www.w3.org/TR/css-position-3/#abspos-layout
// Collapsing margin rule: https://www.w3.org/TR/CSS22/box.html#collapsing-margins
// Float avoidance/clearance: https://www.w3.org/TR/CSS22/visuren.html#floats
void near(float actual, float expected) { REQUIRE(actual == Catch::Approx(expected).margin(0.0001)); }
Style block(float width = -1, float height = -1) {
    Style s; s.display = Display::Block;
    if (width >= 0) s.size.width = Dimension::length(width);
    if (height >= 0) s.size.height = Dimension::length(height);
    return s;
}
NodeId leaf(Tree& tree, Style s) {
    auto result = tree.new_leaf(s); REQUIRE(result.has_value()); return *result;
}
NodeId parent(Tree& tree, Style s, std::initializer_list<NodeId> children) {
    auto result = tree.new_with_children(s, std::span(children.begin(), children.size()));
    REQUIRE(result.has_value()); return *result;
}
void layout(Tree& tree, NodeId root, float width = 200, float height = 100) {
    REQUIRE(tree.compute_layout(root, {AvailableSpace::definite(width), AvailableSpace::definite(height)}).has_value());
}
LayoutInput input(float width = 200) {
    LayoutInput i; i.known_dimensions.width = width;
    i.parent_size = {width, 100};
    i.available_space = {AvailableSpace::definite(width), AvailableSpace::max_content()};
    return i;
}
// A deliberately non-recursive custom tree isolates the production OOF solver
// and records its frozen child inputs. Intrinsic content is independently 40x30.
struct OofProbe final : LayoutTree {
    std::array<Style, 8> styles{};
    std::array<Layout, 8> layouts{};
    std::array<LayoutOutput, 8> outputs{};
    std::array<Cache, 8> caches{};
    std::array<std::vector<NodeId>, 8> hoisted{};
    DetailedLayoutInfo info{};
    std::vector<LayoutInput> calls;
    std::vector<NodeId> nodes;
    std::vector<NodeId> child_ids(NodeId) const override { return {}; }
    const Style& style(NodeId n) const override { return styles.at(n.value); }
    LayoutOutput compute_child_layout(NodeId n, const LayoutInput& i) override {
        calls.push_back(i); nodes.push_back(n);
        REQUIRE(i.sizing_mode == SizingMode::ContentSize);
        REQUIRE(i.known_dimensions_are_definite == Size<bool>{true, true});
        REQUIRE(i.vertical_margins_are_collapsible == Line<bool>{false, false});
        auto out = outputs.at(n.value);
        out.size = i.known_dimensions.unwrap_or(Size<float>{40, 30});
        return out;
    }
    LayoutOutput measure(NodeId, const LayoutInput&) override { FAIL("unexpected raw measurement"); return {}; }
    Cache& cache(NodeId n) override { return caches.at(n.value); }
    float resolve_calc(Calc c, float basis) const override { return basis * 0.1f + static_cast<float>(c.handle); }
    const Layout& unrounded_layout(NodeId n) const override { return layouts.at(n.value); }
    void set_unrounded_layout(NodeId n, const Layout& l) override { layouts.at(n.value) = l; }
    void set_final_layout(NodeId, const Layout&) override { FAIL("OOF must not round"); }
    const DetailedLayoutInfo& detailed_layout_info(NodeId) const override { return info; }
    void clear_hoisted_children(NodeId n) override { hoisted.at(n.value).clear(); }
    void add_hoisted_children(NodeId n, std::span<const NodeId> ids) override {
        auto& h = hoisted.at(n.value); h.insert(h.end(), ids.begin(), ids.end());
    }
    std::vector<NodeId> hoisted_child_ids(NodeId n) const override { return hoisted.at(n.value); }
};
OofCandidate candidate(unsigned n = 1, Position p = Position::Absolute) {
    return {NodeId{n}, 17, p, {AxisStaticPosition::from_edge(0, AxisStaticEdge::Start),
        AxisStaticPosition::from_edge(0, AxisStaticEdge::Start)}};
}
OofLayoutResult oof(OofProbe& tree, Direction direction = Direction::Ltr,
    ContainingBlockClaims claims = ContainingBlockClaims::all()) {
    const auto c = candidate();
    return compute_oof_layout_for_area(tree, NodeId{0}, std::span(&c, 1),
        {{200, 100}, {10, 20}}, direction, claims);
}
} // namespace

TEST_CASE("Block root normal available space and physical box sizing", "[block][native]") {
    Tree t; t.disable_rounding();
    auto s = block(); s.margin = Rect<LengthPercentageAuto>::from_length(10, 20, 3, 7);
    s.padding = Rect<LengthPercentage>::splat(LengthPercentage::length(5));
    s.border = Rect<LengthPercentage>::splat(LengthPercentage::length(2));
    auto a = leaf(t, block(40, 20)); auto r = parent(t, s, {a});
    layout(t, r);
    REQUIRE(t.layout(r).size == Size<float>{170, 34});
    REQUIRE(t.layout(r).location == Point<float>{10, 3});
    REQUIRE(t.layout(a).location == Point<float>{7, 7});
    s.box_sizing = BoxSizing::ContentBox; s.size = Size<Dimension>::from_lengths(100, 50);
    REQUIRE(t.set_style(r, s)); layout(t, r);
    REQUIRE(t.layout(r).size == Size<float>{114, 64});
    s.direction = Direction::Rtl; REQUIRE(t.set_style(r, s)); layout(t, r);
    near(t.layout(r).location.x, 66); near(t.layout(a).location.x, 67);
}

TEST_CASE("Block sibling margins collapse positive negative and empty struts", "[block][margin][native]") {
    Tree t; t.disable_rounding();
    auto sa = block(40, 20); sa.margin.bottom = LengthPercentageAuto::length(15);
    auto se = block(0, 0); se.margin.top = LengthPercentageAuto::length(25);
    se.margin.bottom = LengthPercentageAuto::length(-8);
    auto sb = block(40, 10); sb.margin.top = LengthPercentageAuto::length(10);
    auto a = leaf(t, sa), e = leaf(t, se), b = leaf(t, sb);
    auto r = parent(t, block(100), {a, e, b}); layout(t, r);
    // All adjoining margins share one strut: max(15,25,10)+min(0,-8)=17.
    near(t.layout(b).location.y, 37); near(t.layout(r).size.height, 47);
    sb.margin.top = LengthPercentageAuto::length(-20); REQUIRE(t.set_style(b, sb));
    layout(t, r); near(t.layout(b).location.y, 25); near(t.layout(r).size.height, 35);
}

TEST_CASE("Block parent child margins escape unless padding or FlowRoot intervenes", "[block][margin][native]") {
    for (bool barrier : {false, true}) {
        Tree t; t.disable_rounding();
        auto cs = block(30, 10); cs.margin.top = LengthPercentageAuto::length(20);
        cs.margin.bottom = LengthPercentageAuto::length(15);
        auto c = leaf(t, cs); auto ps = block();
        if (barrier) ps.display = Display::FlowRoot;
        auto p = parent(t, ps, {c}); auto r = parent(t, block(100), {p}); layout(t, r);
        near(t.layout(p).location.y, barrier ? 0 : 20);
        near(t.layout(c).location.y, barrier ? 20 : 0);
        near(t.layout(p).size.height, barrier ? 45 : 10);
        near(t.layout(r).size.height, 45);
    }
    Tree t; auto c = leaf(t, block(30, 10)); auto s = block();
    s.padding.top = LengthPercentage::length(2); auto p = parent(t, s, {c});
    auto r = parent(t, block(100), {p}); layout(t, r); near(t.layout(c).location.y, 2);
}

TEST_CASE("Block shared BFC floats escape nested blocks but FlowRoot contains them", "[block][float][native]") {
    for (bool isolated : {false, true}) {
        Tree t; t.disable_rounding();
        auto fs = block(60, 40); fs.float_ = Float::Left; auto f = leaf(t, fs);
        auto ps = block(); if (isolated) ps.display = Display::FlowRoot;
        auto p = parent(t, ps, {f});
        auto bs = block(100, 10); bs.display = Display::FlowRoot; auto b = leaf(t, bs);
        auto r = parent(t, block(200), {p, b}); layout(t, r);
        near(t.layout(p).size.height, isolated ? 40 : 0);
        near(t.layout(b).location.x, isolated ? 0 : 60);
        near(t.layout(b).location.y, isolated ? 40 : 0);
        near(t.layout(r).size.height, isolated ? 50 : 40);
    }
}

TEST_CASE("Block clearance and wide independent formatting contexts move below floats", "[block][float][clear][native]") {
    for (bool clear : {false, true}) {
        Tree t; t.disable_rounding();
        auto fs = block(80, 40); fs.float_ = Float::Left; auto f = leaf(t, fs);
        auto bs = block(clear ? 40 : 150, 10);
        if (clear) { bs.clear = Clear::Left; bs.margin.top = LengthPercentageAuto::length(5); }
        else bs.display = Display::FlowRoot;
        auto b = leaf(t, bs); auto r = parent(t, block(200), {f, b}); layout(t, r);
        near(t.layout(b).location.y, 40); near(t.layout(b).location.x, 0);
        near(t.layout(r).size.height, 50);
    }
}

TEST_CASE("Block negative available width floors stretch without swallowing margins", "[block][native]") {
    Tree t; t.disable_rounding();
    auto cs = block(-1, 10); cs.margin.left = LengthPercentageAuto::length(30);
    cs.margin.right = LengthPercentageAuto::length(40);
    auto c = leaf(t, cs); auto r = parent(t, block(20), {c}); layout(t, r, -50, -20);
    near(t.layout(c).size.width, 0); near(t.layout(c).location.x, 30);
    near(t.layout(r).size.width, 20);
    // A zero-width child contributes no scrollable overflow rectangle.
    near(t.layout(r).scrollable_overflow_rect.right, 0);
}

TEST_CASE("Block auto margins RTL relative offsets and justification precedence", "[block][rtl][native]") {
    for (auto d : {Direction::Ltr, Direction::Rtl}) {
        Tree t; t.disable_rounding();
        auto cs = block(40, 10); cs.margin.left = cs.margin.right = LengthPercentageAuto::auto_();
        cs.position = Position::Relative; cs.inset.left = LengthPercentageAuto::length(7);
        cs.inset.right = LengthPercentageAuto::length(11); cs.justify_self = AlignSelf::end();
        auto c = leaf(t, cs); auto ps = block(200); ps.direction = d;
        ps.text_align = TextAlign::LegacyRight; auto r = parent(t, ps, {c}); layout(t, r);
        near(t.layout(c).margin.left, 80); near(t.layout(c).margin.right, 80);
        near(t.layout(c).location.x, d == Direction::Ltr ? 87 : 69);
        cs.margin = Rect<LengthPercentageAuto>::zero(); cs.position = Position::Static;
        cs.justify_self = AlignSelf::center(); REQUIRE(t.set_style(c, cs)); layout(t, r);
        near(t.layout(c).location.x, 80);
    }
}

TEST_CASE("Block intrinsic keywords and replaced compressible children measure per node", "[block][intrinsic][native]") {
    for (auto keyword : {Dimension::min_content(), Dimension::max_content(), Dimension::fit_content(),
                          Dimension::fit_content_percent(0.5f), Dimension::stretch()}) {
        Tree t; t.disable_rounding(); std::vector<LayoutInput> seen;
        auto cs = block(-1, 10); cs.size.width = keyword;
        auto c = leaf(t, cs); auto r = parent(t, block(200), {c});
        t.set_intrinsic_measure([&](const LayoutInput& i, NodeId n, NodeContext*, const Style&) {
            REQUIRE(n == c); seen.push_back(i);
            return Size<float>{i.available_space.width.is_min_content() ? 30.0f : 70.0f, 10};
        });
        layout(t, r); near(t.layout(c).size.width, keyword.is_stretch() ? 200 : keyword.is_min_content() ? 30 : 70);
        if (!keyword.is_stretch()) { REQUIRE_FALSE(seen.empty()); REQUIRE(seen.front().run_mode == RunMode::ComputeSize); }
    }
    for (bool compressible : {false, true}) {
        Tree t; t.disable_rounding(); std::array<unsigned, 2> counts{};
        auto s = block(-1, 10); s.item_is_replaced = !compressible; s.item_is_compressible_replaced = compressible;
        auto a = leaf(t, s), b = leaf(t, s); auto r = parent(t, block(200), {a, b});
        t.set_intrinsic_measure([&](const LayoutInput&, NodeId n, NodeContext*, const Style&) {
            REQUIRE((n == a || n == b)); ++counts[n == a ? 0 : 1];
            return Size<float>{n == a ? 35.0f : 65.0f, 10};
        });
        layout(t, r); near(t.layout(a).size.width, 35); near(t.layout(b).size.width, 65);
        REQUIRE(counts[0] > 0); REQUIRE(counts[1] > 0);
        auto saved = counts; layout(t, r); REQUIRE(counts == saved);
        REQUIRE(t.mark_dirty(a)); layout(t, r); REQUIRE(counts[0] > saved[0]); REQUIRE(counts[1] == saved[1]);
    }
}

TEST_CASE("Block percentages aspect ratio minmax content box and calc use correct bases", "[block][native]") {
    Tree t; t.disable_rounding();
    t.set_calc_resolver([](Calc c, float basis) { return basis * 0.1f + static_cast<float>(c.handle); });
    auto s = block(); s.size.width = Dimension::percent(0.5f); s.aspect_ratio = 2;
    s.min_size.width = LengthPercentageAuto::length(120); s.max_size.width = LengthPercentageAuto::length(160);
    s.padding = Rect<LengthPercentage>::splat(LengthPercentage::percent(0.05f));
    s.box_sizing = BoxSizing::ContentBox; s.margin.left = LengthPercentageAuto::calc(Calc{3});
    auto c = leaf(t, s); auto r = parent(t, block(200, 100), {c}); layout(t, r);
    // Preferred content 100x50; minimum width 120 implies min-height 60 by aspect ratio.
    REQUIRE(t.layout(c).size == Size<float>{140, 80}); near(t.layout(c).padding.top, 10);
    near(t.layout(c).location.x, 23);
    s.size.height = Dimension::percent(0.5f); s.aspect_ratio.reset();
    REQUIRE(t.set_style(c, s)); layout(t, r); near(t.layout(c).size.height, 70);
}

TEST_CASE("Block align content moves stack baseline and overflow together", "[block][alignment][native]") {
    for (auto align : {AlignContent::start(), AlignContent::center(), AlignContent::end(),
                       AlignContent::space_between(), AlignContent::space_around(), AlignContent::stretch()}) {
        Tree t; t.disable_rounding(); auto c = leaf(t, block(40, 20));
        auto s = block(100, 100); s.align_content = align; auto r = parent(t, s, {c});
        t.set_leaf_layout([](LayoutTree&, NodeId, const LayoutInput& i) {
            auto out = LayoutOutput::from_outer_size(i.known_dimensions.unwrap_or(Size<float>{40, 20}));
            out.baselines = Baselines::from_first(7); out.scrollable_overflow_rect = {0, 60, 0, 30}; return out;
        });
        auto out = t.compute_child_layout(r, input(100));
        const float offset = align.keyword == AlignContentKeyword::Center || align.keyword == AlignContentKeyword::SpaceAround
            ? 40 : align.keyword == AlignContentKeyword::End ? 80 : 0;
        near(t.unrounded_layout(c).location.y, offset);
        REQUIRE(out.baselines.first); near(*out.baselines.first, offset + 7);
        near(out.scrollable_overflow_rect.bottom, offset + 30);
    }
    for (auto safety : {AlignmentSafety::Safe, AlignmentSafety::Unsafe}) {
        Tree t; t.disable_rounding(); auto c = leaf(t, block(40, 40));
        auto s = block(100, 20); s.align_content = AlignContent::center().with_safety(safety);
        auto r = parent(t, s, {c}); layout(t, r);
        near(t.layout(c).location.y, safety == AlignmentSafety::Safe ? 0 : -10);
    }
}

TEST_CASE("Block size-only margins and frozen deferred OOF metadata survive caching", "[block][oof][cache][native]") {
    Tree t; t.disable_rounding();
    auto flow = leaf(t, block(30, 20)); auto abs_style = block(40, 10); abs_style.position = Position::Absolute;
    abs_style.justify_self = AlignSelf::center(); auto abs = leaf(t, abs_style);
    auto ps = block(200); ps.padding = Rect<LengthPercentage>::splat(LengthPercentage::length(5));
    ps.border = Rect<LengthPercentage>::splat(LengthPercentage::length(2));
    auto r = parent(t, ps, {flow, abs}); auto i = input();
    // Raw production block computation freezes metadata without claiming it.
    auto out = compute_block(t, r, i);
    REQUIRE(out.size == Size<float>{200, 34});
    REQUIRE(out.oof_positioning_area == OofPositioningArea{{196, 30}, {2, 2}});
    REQUIRE(out.oof_candidates.size() == 1);
    const auto frozen = out.oof_candidates.inner.front();
    REQUIRE(frozen.node == abs); REQUIRE(frozen.order == 1);
    REQUIRE(frozen.static_position.x.area == Line<float>{7, 193});
    REQUIRE(frozen.static_position.x.align.keyword == AxisStaticEdge::Center);
    REQUIRE(frozen.static_position.y.area == Line<float>{27, 27});
    auto cached = compute_cached_layout(t, r, i, [&](LayoutTree&, NodeId, const LayoutInput&) { return out; });
    REQUIRE(cached == out);
    auto again = compute_cached_layout(t, r, i, [](LayoutTree&, NodeId, const LayoutInput&) -> LayoutOutput {
        FAIL("same input must hit cache"); return {};
    });
    REQUIRE(again == out); REQUIRE(out.oof_candidates.inner.front() == frozen);
    i.run_mode = RunMode::ComputeSize; auto measured = compute_block(t, r, i);
    REQUIRE(measured.oof_candidates.empty()); REQUIRE_FALSE(measured.oof_positioning_area);
    REQUIRE(measured.size == out.size);
}

TEST_CASE("Block positioned ancestors claim absolute but hoist fixed with stable cache metadata", "[block][oof][native]") {
    Tree t; t.disable_rounding();
    auto as = block(30, 10); as.position = Position::Absolute; as.inset.left = LengthPercentageAuto::length(4);
    auto fs = block(20, 10); fs.position = Position::Fixed; fs.inset.left = LengthPercentageAuto::length(9);
    auto a = leaf(t, as), f = leaf(t, fs); auto ps = block(100, 50); ps.position = Position::Relative;
    auto p = parent(t, ps, {a, f}); auto r = parent(t, block(200), {p}); layout(t, r);
    REQUIRE(t.hoisted_child_ids(p) == std::vector<NodeId>{a});
    REQUIRE(t.hoisted_child_ids(r) == std::vector<NodeId>{f});
    near(t.layout(a).location.x, 4); near(t.layout(f).location.x, 9);
    const auto al = t.layout(a), fl = t.layout(f); layout(t, r);
    REQUIRE(t.layout(a) == al); REQUIRE(t.layout(f) == fl);
    REQUIRE(t.hoisted_child_ids(p) == std::vector<NodeId>{a});
    REQUIRE(t.hoisted_child_ids(r) == std::vector<NodeId>{f});
    auto remaining = leaf(t, block(10, 10));
    const std::array kept{remaining}; REQUIRE(t.set_children(p, kept)); layout(t, r);
    REQUIRE(t.hoisted_child_ids(p).empty()); REQUIRE(t.hoisted_child_ids(r).empty());
}

TEST_CASE("OOF inset and auto margin equations for every inline combination and direction", "[oof][native]") {
    for (bool rtl : {false, true}) for (int im = 0; im < 4; ++im) for (int mm = 0; mm < 4; ++mm) {
        CAPTURE(rtl, im, mm); OofProbe t; auto& s = t.styles[1];
        s.size = Size<Dimension>::from_lengths(40, 30);
        s.inset.left = im & 1 ? LengthPercentageAuto::length(15) : LengthPercentageAuto::auto_();
        s.inset.right = im & 2 ? LengthPercentageAuto::length(25) : LengthPercentageAuto::auto_();
        s.margin.left = mm & 1 ? LengthPercentageAuto::length(5) : LengthPercentageAuto::auto_();
        s.margin.right = mm & 2 ? LengthPercentageAuto::length(7) : LengthPercentageAuto::auto_();
        auto result = oof(t, rtl ? Direction::Rtl : Direction::Ltr);
        REQUIRE(result.hoisted == std::vector<NodeId>{NodeId{1}}); REQUIRE(result.unclaimed.empty());
        REQUIRE(t.calls.size() == 1); REQUIRE(t.layouts[1].order == 17);
        float ml = mm & 1 ? 5.0f : 0.0f, mr = mm & 2 ? 7.0f : 0.0f;
        // left + margins + width + right = containing-block width.
        if (im == 3) { const float free = 200 - 15 - 25 - 40;
            if (mm == 0) ml = mr = free / 2; else if (mm == 1) mr = free - ml; else if (mm == 2) ml = free - mr;
        }
        near(t.layouts[1].margin.left, ml); near(t.layouts[1].margin.right, mr);
        const float x = im == 0 ? ml : im == 1 ? 10 + 15 + ml : im == 2 ? 10 + 200 - 25 - 40 - mr
            : 10 + ((mm == 3 && rtl) ? 200 - 25 - 40 - mr : 15 + ml);
        near(t.layouts[1].location.x, x); near(t.layouts[1].location.y, 0);
    }
}

TEST_CASE("OOF stretch aspect replaced table minmax and negative auto margins", "[oof][native]") {
    OofProbe t; auto& s = t.styles[1]; s.inset = Rect<LengthPercentageAuto>::from_length(10, 20, 5, 15);
    oof(t); REQUIRE(t.layouts[1].size == Size<float>{170, 80});
    REQUIRE(t.layouts[1].location == Point<float>{20, 25});
    s.aspect_ratio = 2; oof(t); REQUIRE(t.layouts[1].size == Size<float>{170, 85});
    s.align_self = s.justify_self = AlignSelf::stretch(); oof(t); near(t.layouts[1].size.height, 80);
    s = Style{}; s.inset = Rect<LengthPercentageAuto>::from_length(10, 20, 5, 15); s.item_is_replaced = true;
    t.calls.clear(); oof(t); REQUIRE(t.layouts[1].size == Size<float>{40, 30}); REQUIRE(t.calls.size() == 2);
    s.item_is_replaced = false; s.item_is_table = true; s.align_self = s.justify_self = AlignSelf::stretch();
    oof(t); REQUIRE(t.layouts[1].size == Size<float>{170, 80});
    s.item_is_table = false; s.max_size = Size<LengthPercentageAuto>::from_lengths(90, 50); oof(t); REQUIRE(t.layouts[1].size == Size<float>{90, 50});
    s = Style{}; s.size = Size<Dimension>::from_lengths(250, 130);
    s.inset = Rect<LengthPercentageAuto>::from_length(10, 20, 5, 15); s.margin = Rect<LengthPercentageAuto>::auto_();
    oof(t); REQUIRE(t.layouts[1].margin == Rect<float>{0, -80, -25, -25});
    oof(t, Direction::Rtl); REQUIRE(t.layouts[1].margin == Rect<float>{-80, 0, -25, -25});
}

TEST_CASE("OOF percentage padding calc content box and scrollbars", "[oof][native]") {
    OofProbe t; auto& s = t.styles[1]; s.size = Size<Dimension>::from_lengths(30, 20);
    s.box_sizing = BoxSizing::ContentBox; s.padding = Rect<LengthPercentage>::splat(LengthPercentage::percent(0.1f));
    s.border = Rect<LengthPercentage>::splat(LengthPercentage::length(2));
    s.inset.left = LengthPercentageAuto::calc(Calc{3}); s.overflow = {Overflow::Scroll, Overflow::Scroll}; s.scrollbar_width = 7;
    oof(t); REQUIRE(t.layouts[1].size == Size<float>{74, 64}); near(t.layouts[1].location.x, 33);
    near(t.layouts[1].padding.top, 20); REQUIRE(t.layouts[1].scrollbar_size == Size<float>{7, 7});
    REQUIRE(t.calls.back().parent_size == Size<std::optional<float>>{200, 100});
}

TEST_CASE("OOF safe unsafe and default overflow alignment", "[oof][alignment][native]") {
    for (auto safety : {AlignmentSafety::Default, AlignmentSafety::Safe, AlignmentSafety::Unsafe}) {
        OofProbe t; auto& s = t.styles[1]; s.size = Size<Dimension>::from_lengths(180, 30);
        s.inset.left = s.inset.right = LengthPercentageAuto::length(50);
        s.justify_self = AlignSelf::end().with_safety(safety); oof(t);
        near(t.layouts[1].location.x, safety == AlignmentSafety::Safe ? 60 : safety == AlignmentSafety::Unsafe ? -20 : 10);
        s.size.width = Dimension::length(250); oof(t);
        near(t.layouts[1].location.x, safety == AlignmentSafety::Safe ? 60 : safety == AlignmentSafety::Unsafe ? -90 : 10);
        t.styles[0].overflow.x = Overflow::Scroll; oof(t);
        near(t.layouts[1].location.x, safety == AlignmentSafety::Safe ? 60 : safety == AlignmentSafety::Unsafe ? -90 : 10);
    }
}

TEST_CASE("OOF claims recursively surface translated fixed candidates without mutating source", "[oof][metadata][native]") {
    OofProbe t; auto& s = t.styles[1]; s.size = Size<Dimension>::from_lengths(40, 30);
    s.inset.left = LengthPercentageAuto::length(15); s.inset.top = LengthPercentageAuto::length(5);
    auto fixed = candidate(2, Position::Fixed); fixed.static_position.x.area = {7, 7}; fixed.static_position.y.area = {9, 9};
    t.outputs[1].oof_candidates.push(fixed); t.outputs[1].oof_candidates.push(candidate(3));
    const auto frozen = t.outputs[1]; auto r = oof(t, Direction::Ltr, {true, false});
    REQUIRE(r.hoisted == std::vector<NodeId>{NodeId{1}, NodeId{3}}); REQUIRE(r.unclaimed.size() == 1);
    REQUIRE(r.unclaimed.inner.front().static_position.x.area == Line<float>{32, 32});
    REQUIRE(r.unclaimed.inner.front().static_position.y.area == Line<float>{34, 34});
    REQUIRE(t.outputs[1] == frozen); r = oof(t);
    REQUIRE(r.hoisted == std::vector<NodeId>{NodeId{1}, NodeId{2}, NodeId{3}}); REQUIRE(r.unclaimed.empty());
    near(t.layouts[2].location.x, 32); REQUIRE(t.outputs[1] == frozen);
    t.calls.clear(); r = oof(t, Direction::Ltr, ContainingBlockClaims::none());
    REQUIRE(r.hoisted.empty()); REQUIRE(r.unclaimed.size() == 1); REQUIRE(t.calls.empty());
    r = compute_oof_layout_for_area(t, NodeId{0}, {}, {{200, 100}, {10, 20}}, Direction::Ltr, ContainingBlockClaims::all());
    REQUIRE(r.hoisted.empty()); REQUIRE(r.unclaimed.empty()); REQUIRE(r.scrollable_overflow_rect == Rect<float>{});
}

TEST_CASE("OOF detailed grid areas ordinary named reversed outside collapsed and empty axes", "[oof][grid][native]") {
    OofProbe t; DetailedGridInfo g; g.columns.explicit_tracks = 3; g.columns.positions = {{30,60},{80,110},{130,160}};
    g.rows.explicit_tracks = 2; g.rows.positions = {{25,45},{55,75}};
    g.columns.line_names = {{"a"},{"b"},{"c"},{"d"}}; g.areas["hero"] = {1,3,2,4}; t.info = g;
    auto& s = t.styles[1]; s.grid_column = {GridPlacement::line(2), GridPlacement::line(4)};
    s.grid_row = {GridPlacement::line(1), GridPlacement::line(3)};
    s.inset = Rect<LengthPercentageAuto>::splat(LengthPercentageAuto::length(0)); oof(t);
    REQUIRE(t.layouts[1].location == Point<float>{80, 25}); REQUIRE(t.layouts[1].size == Size<float>{80, 50});
    s.grid_column = {GridPlacement::named_line("hero"), GridPlacement::named_line("hero")}; oof(t);
    near(t.layouts[1].location.x, 80); near(t.layouts[1].size.width, 80);
    s.grid_column = {GridPlacement::line(4), GridPlacement::line(2)}; oof(t); near(t.layouts[1].size.width, 80);
    s.grid_column = {GridPlacement::line(2), GridPlacement::span(2)}; oof(t); near(t.layouts[1].size.width, 80);
    s.grid_column = {GridPlacement::line(3), GridPlacement::line(7)}; oof(t);
    near(t.layouts[1].location.x, 130); near(t.layouts[1].size.width, 80);
    auto& tracks = std::get<DetailedGridInfo>(t.info).columns; tracks.positions = {{130,160},{80,110},{30,60}};
    s.grid_column = {GridPlacement::line(1), GridPlacement::line(3)}; oof(t, Direction::Rtl);
    near(t.layouts[1].location.x, 80); near(t.layouts[1].size.width, 80);
    tracks.positions = {{30,60},{80,80},{130,160}}; tracks.collapsed_tracks = {{1,2}};
    s.grid_column = {GridPlacement::line(2), GridPlacement::line(3)}; oof(t);
    near(t.layouts[1].location.x, 130); near(t.layouts[1].size.width, 0);
    tracks = {}; tracks.empty_axis_line = 42; s.grid_column = {GridPlacement::line(1), GridPlacement::line(1)}; oof(t);
    near(t.layouts[1].location.x, 42); near(t.layouts[1].size.width, 168);
}

TEST_CASE("OOF intrinsic keywords preserve requested measurement axes and modes", "[oof][intrinsic][native]") {
    for (auto keyword : {Dimension::min_content(), Dimension::max_content(), Dimension::fit_content(),
                          Dimension::fit_content_percent(0.5f), Dimension::stretch()}) {
        OofProbe t; auto& s = t.styles[1]; s.size = {keyword, Dimension::length(20)};
        s.inset.left = LengthPercentageAuto::length(10); s.inset.right = LengthPercentageAuto::length(20); oof(t);
        near(t.layouts[1].size.width, keyword.is_stretch() ? 170 : 40);
        REQUIRE(t.calls.back().run_mode == RunMode::PerformLayout);
        if (!keyword.is_stretch()) { REQUIRE(t.calls.front().run_mode == RunMode::ComputeSize);
            REQUIRE(t.calls.front().axis == RequestedAxis::Horizontal); }
    }
    OofProbe t; t.styles[1].size = {Dimension::min_content(), Dimension::max_content()}; oof(t);
    REQUIRE(t.calls.size() == 2); REQUIRE(t.calls.front().axis == RequestedAxis::Both);
    REQUIRE(t.calls.front().available_space.width.is_min_content()); REQUIRE(t.calls.front().available_space.height.is_max_content());
}

TEST_CASE("OOF overflow honors child clipping paint containment and RTL origin", "[oof][overflow][native]") {
    OofProbe t; auto& s = t.styles[1]; s.size = Size<Dimension>::from_lengths(40, 30);
    s.inset.left = LengthPercentageAuto::length(15); s.inset.top = LengthPercentageAuto::length(5);
    t.outputs[1].scrollable_overflow_rect = {-5, 100, -10, 80}; auto r = oof(t);
    REQUIRE(r.scrollable_overflow_rect == Rect<float>{0, 115, -5, 85});
    s.overflow.x = Overflow::Hidden; r = oof(t); REQUIRE(r.scrollable_overflow_rect == Rect<float>{0, 55, 0, 35});
    s.overflow.x = Overflow::Visible; s.contain = Contain::paint(); r = oof(t); near(r.scrollable_overflow_rect.right, 55);
    s.contain = {}; s.inset.left = LengthPercentageAuto::length(-200); t.styles[0].overflow.x = Overflow::Scroll;
    r = oof(t); REQUIRE(r.scrollable_overflow_rect == Rect<float>{});
    OofProbe rtl; rtl.styles[1].size = Size<Dimension>::from_lengths(40, 30);
    rtl.styles[1].inset.right = LengthPercentageAuto::length(10); r = oof(rtl, Direction::Rtl); near(r.scrollable_overflow_rect.right, 50);
}

TEST_CASE("Float band placement splits bands unions heights and advances content slots", "[float][native]") {
    FloatContext f; f.set_width(200); REQUIRE_FALSE(f.has_floats());
    REQUIRE(f.place_floated_box({60,40}, 0, {0,0}, FloatDirection::Left, Clear::None) == Point<float>{0,0});
    REQUIRE(f.place_floated_box({50,20}, 0, {0,0}, FloatDirection::Right, Clear::None) == Point<float>{150,0});
    auto s = f.find_content_slot(0, {0,0}, Clear::None);
    REQUIRE(s.segment_id); near(s.x, 60); near(s.y, 0); near(s.width, 90); REQUIRE(std::isinf(s.height));
    auto next = f.find_content_slot(0, {0,0}, Clear::None, s.segment_id);
    REQUIRE(next.segment_id); near(next.x, 60); near(next.y, 20); near(next.width, 140); REQUIRE(std::isinf(next.height));
    REQUIRE(f.place_floated_box({100,30}, 0, {0,0}, FloatDirection::Left, Clear::None) == Point<float>{60,20});
    near(*f.cleared_threshold(Clear::Left), 50); near(*f.cleared_threshold(Clear::Right), 20);
    auto cleared = f.find_content_slot(0, {0,0}, Clear::Both); near(cleared.y, 50); near(cleared.width, 200);
    REQUIRE_FALSE(cleared.segment_id); REQUIRE(f.left_floats().size() == 2); REQUIRE(f.right_floats().size() == 1);
    REQUIRE(f.has_active_floats(49)); REQUIRE_FALSE(f.has_active_floats(50));
    REQUIRE(f.place_floated_box({250,10}, 0, {0,0}, FloatDirection::Right, Clear::Both) == Point<float>{-50,50});
}

TEST_CASE("Float band fit tolerance half open segments and double accumulated height", "[float][native]") {
    using namespace block_detail;
    REQUIRE(float_fits_horizontally(150, FloatDirection::Left, 100, {0,0}, {10,10}));
    REQUIRE_FALSE(float_fits_horizontally(90, FloatDirection::Left, 100, {20,0}, {0,0}));
    REQUIRE_FALSE(float_fits_horizontally(90, FloatDirection::Right, 100, {20,0}, {0,0}));
    REQUIRE(float_fits_horizontally(80.0005f, FloatDirection::Left, 100, {20,0}, {0,0}));
    Segment seg{{-10,10}, {20,30}, {true,true}};
    REQUIRE(seg.y.contains(-10)); REQUIRE_FALSE(seg.y.contains(10));
    REQUIRE(seg.fits_float_width({50,100}, FloatDirection::Left, 100, {0,0}));
    FloatFitter fitter(100, 16777216, {0,0}); fitter.add_height(1);
    REQUIRE(fitter.slot_height == 16777217.0); REQUIRE_FALSE(fitter.fits_vertically(16777218));
    fitter.union_insets({20,10}); fitter.union_insets({10,30}); near(fitter.placed_inset(FloatDirection::Right), 30);
    REQUIRE(fitter.fits_horizontally(50, FloatDirection::Left)); REQUIRE_FALSE(fitter.fits_horiontally(51, FloatDirection::Left));
}

TEST_CASE("Float intrinsic width sums opposing sides resets clear and floors at widest float", "[float][intrinsic][native]") {
    for (auto mode : {AvailableSpace::max_content(), AvailableSpace::min_content(), AvailableSpace::definite(50)}) {
        FloatIntrinsicWidthCalculator c(mode); c.add_float(30, FloatDirection::Left, Clear::None);
        c.add_float(40, FloatDirection::Right, Clear::None); c.add_float(20, FloatDirection::Left, Clear::Left);
        near(c.result(), mode.is_min_content() ? 40 : mode.is_definite() ? 50 : 70);
    }
    FloatIntrinsicWidthCalculator c(AvailableSpace::definite(10)); c.add_float(30, FloatDirection::Left, Clear::None); near(c.result(), 30);
}

TEST_CASE("Block context shares coordinates and freezes first adjoining float strut", "[block][float][native]") {
    BlockFormattingContext bfc; auto root = bfc.root_block_context(); REQUIRE(root.is_bfc_root());
    REQUIRE(std::isinf(root.floated_content_height_contribution())); REQUIRE(root.floated_content_height_contribution() < 0);
    root.set_width(200); root.apply_content_box_inset({10,20}); auto child = root.sub_context(30, {10,20});
    REQUIRE_FALSE(child.is_bfc_root()); child.apply_content_box_inset({5,5});
    REQUIRE(child.place_floated_box({20,40}, 5, FloatDirection::Left, Clear::None, true) == Point<float>{5,5});
    near(child.floated_content_height_contribution(), 45); REQUIRE(root.has_floats());
    REQUIRE(root.has_active_floats(74)); REQUIRE_FALSE(root.has_active_floats(75));
    near(*root.cleared_threshold(Clear::Left), 75); near(*child.cleared_threshold(Clear::Left), 45);
    auto slot = child.find_content_slot(5, Clear::None); near(slot.x, 25); near(slot.y, 5); near(slot.width, 140);
    auto bs = child.find_bfc_slot(5, {-20,30}, Direction::Ltr, Clear::None);
    near(bs.x, 25); near(bs.y, 5); near(bs.border_width, 140); near(bs.stretch_width, 110);
    REQUIRE(child.has_adjoining_float(Clear::Left)); root.merge_adjoining_floats(child.top_adjoining_floats());
    REQUIRE(root.has_adjoining_float(Clear::Left)); child.commit_strut(); REQUIRE_FALSE(child.has_adjoining_float(Clear::Both));
    REQUIRE(child.top_adjoining_floats()[0]); child.merge_adjoining_floats({false,true}); child.commit_strut();
    REQUIRE(child.top_adjoining_floats()[0]); REQUIRE_FALSE(child.top_adjoining_floats()[1]);
    root.add_child_floated_content_height_contribution(30 + child.floated_content_height_contribution());
    near(root.floated_content_height_contribution(), 75);
}

TEST_CASE("Block size passes preserve escaping mixed margin sets without writing layouts", "[block][margin][native]") {
    Tree t; t.disable_rounding();
    auto cs = block(30, 10); cs.margin.top = LengthPercentageAuto::length(20);
    cs.margin.bottom = LengthPercentageAuto::length(-7); auto c = leaf(t, cs);
    auto ps = block(); auto p = parent(t, ps, {c}); auto i = input(100);
    i.vertical_margins_are_collapsible = {true, true};
    i.run_mode = RunMode::ComputeSize;
    const Layout sentinel{.order = 99, .location = {123, 456}}; t.set_unrounded_layout(c, sentinel);
    auto out = compute_block(t, p, i);
    REQUIRE(out.size == Size<float>{100, 10});
    REQUIRE(out.top_margin == CollapsibleMarginSet{20, 0});
    REQUIRE(out.bottom_margin == CollapsibleMarginSet{0, -7});
    REQUIRE_FALSE(out.margins_can_collapse_through);
    REQUIRE(out.oof_candidates.empty()); REQUIRE_FALSE(out.oof_positioning_area);
    REQUIRE(t.unrounded_layout(c) == sentinel);
    i.run_mode = RunMode::PerformLayout; auto full = compute_block(t, p, i);
    REQUIRE(full.top_margin == out.top_margin); REQUIRE(full.bottom_margin == out.bottom_margin);
    REQUIRE(full.size == out.size); near(t.unrounded_layout(c).location.y, 0);
    // A positive minimum height blocks bottom-margin escape.
    ps.min_size.height = LengthPercentageAuto::length(30); REQUIRE(t.set_style(p, ps));
    full = compute_block(t, p, i); near(full.size.height, 30); near(full.bottom_margin.resolve(), 0);
}

TEST_CASE("Block hidden children have zero layout and do not consume filtered paint order", "[block][native]") {
    Tree t; t.disable_rounding(); auto hs = block(999, 999); hs.display = Display::None;
    auto h = leaf(t, hs), a = leaf(t, block(30, 10)), b = leaf(t, block(40, 20));
    auto r = parent(t, block(100), {h, a, b}); layout(t, r);
    REQUIRE(t.layout(h).size == Size<float>{}); REQUIRE(t.layout(h).location == Point<float>{});
    REQUIRE(t.layout(a).order == 0); REQUIRE(t.layout(b).order == 1);
    near(t.layout(b).location.y, 10); near(t.layout(r).size.height, 30);
}

TEST_CASE("Block intrinsic container widths count float sides and omit absolute children", "[block][float][intrinsic][native]") {
    for (bool minimum : {false, true}) {
        Tree t; auto ls = block(30, 10); ls.float_ = Float::Left;
        auto rs = block(40, 10); rs.float_ = Float::Right;
        auto os = block(999, 999); os.position = Position::Absolute;
        auto l = leaf(t, ls), r = leaf(t, rs), o = leaf(t, os); auto p = parent(t, block(), {l,r,o});
        LayoutInput i; i.run_mode = RunMode::ComputeSize; i.axis = RequestedAxis::Horizontal;
        i.available_space = minimum ? Size<AvailableSpace>::min_content() : Size<AvailableSpace>::max_content();
        auto out = compute_block(t, p, i); near(out.size.width, minimum ? 40 : 70);
        near(out.size.height, 0); REQUIRE(out.oof_candidates.empty());
    }
}

TEST_CASE("Block baselines clamp scroll children ignore relative translation and honor containment", "[block][baseline][native]") {
    for (auto overflow : {Overflow::Visible, Overflow::Hidden}) {
        Tree t; auto cs = block(30, 20); cs.position = Position::Relative;
        cs.inset.top = LengthPercentageAuto::length(11); cs.overflow.y = overflow;
        auto c = leaf(t, cs); auto p = parent(t, block(100), {c});
        t.set_leaf_layout([](LayoutTree&, NodeId, const LayoutInput&) {
            auto out = LayoutOutput::from_outer_size({30, 20}); out.baselines = Baselines::from_first(50); return out;
        });
        auto out = compute_block(t, p, input(100)); REQUIRE(out.baselines.first);
        near(*out.baselines.first, overflow == Overflow::Visible ? 50 : 20);
        near(t.unrounded_layout(c).location.y, 11); near(out.size.height, 20);
        auto ps = block(100); ps.contain = Contain::layout(); REQUIRE(t.set_style(p, ps));
        out = compute_block(t, p, input(100)); REQUIRE(out.baselines == Baselines::NONE);
    }
}

TEST_CASE("Block RTL scrollbar gutter and padding box freeze the OOF resolution area", "[block][rtl][oof][native]") {
    Tree t; auto c = leaf(t, block(30, 10)); auto as = block(20, 10); as.position = Position::Absolute;
    auto a = leaf(t, as); auto s = block(200, 100); s.direction = Direction::Rtl;
    s.padding = Rect<LengthPercentage>::splat(LengthPercentage::length(5));
    s.border = Rect<LengthPercentage>::splat(LengthPercentage::length(2));
    s.overflow = {Overflow::Scroll, Overflow::Scroll}; s.scrollbar_width = 7;
    auto r = parent(t, s, {c,a}); auto out = compute_block(t, r, input());
    REQUIRE(out.oof_positioning_area == OofPositioningArea{{189,89}, {9,2}});
    REQUIRE(out.oof_candidates.size() == 1); const auto& frozen = out.oof_candidates.inner.front();
    REQUIRE(frozen.static_position.x.area == Line<float>{14,193});
    REQUIRE(frozen.static_position.x.align.keyword == AxisStaticEdge::End);
    REQUIRE(frozen.static_position.y.area == Line<float>{17,17});
    near(t.unrounded_layout(c).location.x, 163);
}

TEST_CASE("Block bubbled fixed static areas translate through relative in-flow ancestors", "[block][oof][metadata][native]") {
    Tree t; auto fs = block(20, 10); fs.position = Position::Fixed; auto f = leaf(t, fs);
    auto ps = block(100, 40); ps.position = Position::Relative;
    ps.inset.left = LengthPercentageAuto::length(13); ps.inset.top = LengthPercentageAuto::length(7);
    ps.padding = Rect<LengthPercentage>::splat(LengthPercentage::length(5));
    auto p = parent(t, ps, {f}); auto r = parent(t, block(200), {p});
    auto out = compute_block(t, r, input()); REQUIRE(out.oof_candidates.size() == 1);
    const auto& frozen = out.oof_candidates.inner.front(); REQUIRE(frozen.node == f);
    REQUIRE(frozen.static_position.x.area == Line<float>{18,108});
    REQUIRE(frozen.static_position.y.area == Line<float>{12,12});
    REQUIRE(t.hoisted_child_ids(p).empty());
    near(out.size.height, 40); // Relative offsets do not affect the flow stack.
}

TEST_CASE("OOF static alignment resolves margin boxes and safe overflow fallback", "[oof][alignment][native]") {
    auto x = AxisStaticPosition::from_alignment(AlignSelf::center(), {10,110}, false);
    auto y = AxisStaticPosition::from_alignment(AlignSelf::end(), {20,100}, false);
    REQUIRE(resolve_static_offset({x,y}, {40,30}, {3,7,5,9}) == Point<float>{38,61});
    x = AxisStaticPosition::from_alignment(AlignSelf::center().with_safety(AlignmentSafety::Safe), {10,30}, false);
    near(resolve_static_offset({x,y}, {40,30}, {3,7,5,9}).x, 13);
    x = AxisStaticPosition::from_alignment(AlignSelf::center().with_safety(AlignmentSafety::Safe), {10,30}, true);
    near(resolve_static_offset({x,y}, {40,30}, {3,7,5,9}).x, -17);
}

TEST_CASE("OOF positioning wrapper replaces hoisted IDs and unions existing overflow", "[oof][metadata][native]") {
    OofProbe t; t.styles[0].position = Position::Relative;
    t.styles[1].size = Size<Dimension>::from_lengths(40,30);
    t.hoisted[0] = {NodeId{7}};
    LayoutOutput out; out.oof_positioning_area = OofPositioningArea{{200,100}, {10,20}};
    out.oof_candidates.push(candidate()); out.scrollable_overflow_rect = {-3,5,-4,6};
    compute_oof_layout(t, NodeId{0}, out);
    REQUIRE(out.oof_candidates.empty()); REQUIRE(t.hoisted[0] == std::vector<NodeId>{NodeId{1}});
    REQUIRE(out.scrollable_overflow_rect == Rect<float>{-10,30,-20,10});
    compute_oof_layout(t, NodeId{0}, out); REQUIRE(t.hoisted[0].empty());
}

TEST_CASE("Block intrinsic measurement forwards negative availability rather than unsigned wrap", "[block][intrinsic][native]") {
    Tree t; auto s = block(-1, 10); s.margin.left = LengthPercentageAuto::length(7);
    s.margin.right = LengthPercentageAuto::length(3); auto c = leaf(t, s); auto p = parent(t, block(), {c});
    unsigned calls = 0;
    t.set_intrinsic_measure([&](const LayoutInput& measured, NodeId n, NodeContext*, const Style&) {
        REQUIRE(n == c); REQUIRE(measured.run_mode == RunMode::ComputeSize);
        REQUIRE(measured.axis == RequestedAxis::Horizontal);
        REQUIRE(measured.available_space.width == AvailableSpace::definite(-60));
        ++calls; return Size<float>{25,10};
    });
    LayoutInput i; i.run_mode = RunMode::ComputeSize; i.axis = RequestedAxis::Horizontal;
    i.available_space = {AvailableSpace::definite(-50), AvailableSpace::max_content()};
    const auto out = compute_block(t, p, i); REQUIRE(calls == 1);
    near(out.size.width, 35); near(out.size.height, 0);
}

TEST_CASE("Block size-only pass still fully lays out floated children but defers parent writes", "[block][float][native]") {
    Tree t; auto fs = block(30,20); fs.float_ = Float::Left; auto f = leaf(t, fs);
    auto p = parent(t, block(100), {f}); unsigned calls = 0;
    t.set_leaf_layout([&](LayoutTree&, NodeId n, const LayoutInput& i) {
        REQUIRE(n == f); REQUIRE(i.run_mode == RunMode::PerformLayout);
        REQUIRE(i.sizing_mode == SizingMode::InherentSize);
        REQUIRE(i.vertical_margins_are_collapsible == Line<bool>{false,false});
        ++calls; return LayoutOutput::from_outer_size({30,20});
    });
    auto i = input(100); i.run_mode = RunMode::ComputeSize;
    const Layout sentinel{.order = 77, .location = {99,88}}; t.set_unrounded_layout(f, sentinel);
    auto out = compute_block(t, p, i); REQUIRE(calls == 1); REQUIRE(out.size == Size<float>{100,20});
    REQUIRE(t.unrounded_layout(f) == sentinel); REQUIRE(out.oof_candidates.empty());
    // Fully known size-only queries short circuit without invoking any child.
    i.known_dimensions.height = 50; out = compute_block(t, p, i);
    REQUIRE(out.size == Size<float>{100,50}); REQUIRE(calls == 1);
}

TEST_CASE("OOF axis helpers are the production inset margin and positioning operations", "[oof][native]") {
    using namespace block_detail;
    OofAxis axis{100, {10,20}, {std::nullopt,std::nullopt},
        AxisStaticPosition::from_edge(0, AxisStaticEdge::Start),
        OofAlignment::Normal, AlignmentSafety::Default, true, false, false};
    REQUIRE(axis.inset_modified_containing_block() == Line<float>{10,80});
    near(axis.inset_modified_containing_block_size(), 70);
    near(axis.stretch_fit_size(), 70);
    REQUIRE(axis.is_stretch_sized(false, false));
    REQUIRE_FALSE(axis.is_stretch_sized(true, false));
    REQUIRE(axis.resolve_margins(90) == Line<float>{0,-20});
    near(axis.position(90, axis.resolve_margins(90)), 10);
    axis.reversed = true;
    REQUIRE(axis.resolve_margins(90) == Line<float>{-20,0});
    near(axis.position(90, axis.resolve_margins(90)), -10);
    axis.inset = {120,20};
    REQUIRE(axis.inset_modified_containing_block() == Line<float>{80,80});
    REQUIRE(oof_alignment(AlignSelf::baseline()) == OofAlignment::Start);
}

TEST_CASE("Block and OOF propagate callback and bounds failures in release too", "[block][oof][fatal][native]") {
    Tree t;
    auto child = leaf(t, block()); auto root = parent(t, block(), {child});
    t.set_intrinsic_measure([](const LayoutInput&, NodeId, NodeContext*, const Style&) -> Size<float> {
        throw std::runtime_error("intrinsic failure");
    });
    REQUIRE_THROWS_AS(t.compute_layout(root, {AvailableSpace::max_content(), AvailableSpace::max_content()}), std::runtime_error);
    REQUIRE(t.cache(root).is_empty());
    t.set_intrinsic_measure([](const LayoutInput&, NodeId, NodeContext*, const Style&) { return Size<float>{20,10}; });
    REQUIRE(t.compute_layout(root, {AvailableSpace::max_content(), AvailableSpace::max_content()}));
    OofProbe probe;
    const auto bad = candidate(100);
    REQUIRE_THROWS_AS(compute_oof_layout_for_area(probe, NodeId{0}, std::span(&bad, 1),
        {{100,100},{0,0}}, Direction::Ltr, ContainingBlockClaims::all()), std::out_of_range);
}

TEST_CASE("Zero height floats retain ceiling and clearance without occupying bands", "[float][native]") {
    FloatContext f; f.set_width(100); f.place_floated_box({20,0}, 10, {0,0}, FloatDirection::Left, Clear::None);
    REQUIRE(f.has_floats()); REQUIRE_FALSE(f.has_active_floats(0)); near(*f.cleared_threshold(Clear::Both), 10);
    REQUIRE(f.place_floated_box({20,10}, 0, {0,0}, FloatDirection::Right, Clear::None) == Point<float>{80,10});
}
