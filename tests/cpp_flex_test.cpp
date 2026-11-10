#include <catch2/catch_test_macros.hpp>
#include "../src/flex.hpp"
#include <taffy/taffy.hpp>
#include "../src/flex/internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <numeric>
#include <span>
#include <utility>

using namespace taffy;
using namespace taffy::flex_detail;

namespace {
// Native port of upstream balance::tests::naive_line_item_counts, including
// brute-force zero_boundary_valid. This is a DP oracle, not Rust goldens.
std::vector<std::size_t> naive_counts(std::span<const float> items, float limit, float gap_value,
                                     std::size_t minimum) {
    const auto n = items.size();
    const double inf = std::numeric_limits<double>::infinity();
    const auto gap = quantize_size(gap_value);
    std::vector<double> sums;
    std::vector<bool> zero;
    double sum = 0;
    for (float item : items) {
        const auto size = quantize_size(item);
        sum += size + gap;
        sums.push_back(sum);
        zero.push_back(size == 0);
    }
    const auto size = [&](std::size_t start, std::size_t end) {
        return sums[end] - (start == 0 ? 0.0 : sums[start - 1]) - gap;
    };
    const auto cost = [&](std::size_t start, std::size_t end) {
        const auto length = size(start, end);
        return end > start && length > limit ? inf : length * length;
    };
    std::size_t greedy = 1, index = 0;
    while (index < n) {
        auto next = index;
        while (next < n && size(index, next) <= limit) ++next;
        if (next == index) next = index + 1;
        index = next;
        if (index < n) ++greedy;
    }
    const auto count = std::max(greedy, std::clamp(minimum, std::size_t{1}, n));
    std::vector<double> errors(count * n, inf);
    for (std::size_t start = 0; start < n; ++start) errors[start] = cost(start, n - 1);
    const auto zero_valid = [&](std::size_t lines, std::size_t start, std::size_t end) {
        if (!zero[end + 1]) return true;
        for (auto glued = end + 1; glued <= n - lines; ++glued) {
            if (size(start, glued) > limit) break;
            if (std::isfinite(errors[(lines - 2) * n + glued + 1])) return false;
        }
        return true;
    };
    for (std::size_t lines = 2; lines <= count; ++lines) {
        const auto row = (lines - 1) * n;
        for (std::size_t start = 0; start < n; ++start) {
            double best = inf;
            for (auto end = start; end + lines <= n; ++end) {
                const auto line_cost = cost(start, end);
                if (line_cost == inf) break;
                if (zero_valid(lines, start, end)) {
                    const auto score = line_cost + errors[row - n + end + 1];
                    if (score < best) best = score;
                }
            }
            errors[row + start] = best;
        }
    }
    REQUIRE(std::isfinite(errors[(count - 1) * n]));
    std::vector<std::size_t> result;
    std::size_t start = 0;
    for (auto lines = count; lines-- > 0;) {
        const auto target = errors[lines * n + start];
        auto end = n - 1 - lines;
        for (;;) {
            if (cost(start, end) != inf && (lines == 0 || zero_valid(lines + 1, start, end))) {
                const auto remaining = lines == 0 ? 0.0 : errors[(lines - 1) * n + end + 1];
                if (cost(start, end) + remaining == target) break;
            }
            REQUIRE(end > start);
            --end;
        }
        result.push_back(end - start + 1);
        start = end + 1;
    }
    REQUIRE(start == n);
    return result;
}
std::vector<FlexItem> make_items(std::initializer_list<float> values, FlexDirection dir = FlexDirection::Row) {
    std::vector<FlexItem> result(values.size());
    std::size_t i = 0;
    for (float value : values) result[i++].hypothetical_outer_size.set_main(dir, value);
    return result;
}
std::vector<std::size_t> counts(const std::vector<FlexLine>& lines) {
    std::vector<std::size_t> result;
    for (const auto& line : lines) {
        CHECK(line.cross_size == 0);
        CHECK(line.offset_cross == 0);
        result.push_back(line.items.size());
    }
    return result;
}
} // namespace

TEST_CASE("Pinned flex balancing zero-sized-item assertions", "[flex][balance]") {
    const std::vector<float> a{70, 0, 20}, b{150, 0, 30}, c{70, 0, 0};
    CHECK(balance_lines(a, 100, 10, 1) == std::vector<std::size_t>{2, 1});
    CHECK(balance_lines(b, 100, 0, 1) == std::vector<std::size_t>{1, 2});
    CHECK(balance_lines(c, 100, 0, 2) == std::vector<std::size_t>{2, 1});
}

TEST_CASE("Pinned optimized balancing matches naive DP in 5000 cases", "[flex][balance]") {
    std::uint64_t state = 0x243F6A8885A308D3ULL;
    const auto random = [&](std::uint32_t bound) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<std::uint32_t>(state >> 32) % bound;
    };
    for (std::size_t test = 0; test < 5000; ++test) {
        const std::size_t n = test % 20 == 0 ? random(60) + 1 : random(15) + 1;
        std::vector<float> items;
        for (std::size_t i = 0; i < n; ++i) {
            switch (random(8)) {
            case 0: case 1: items.push_back(0); break;
            case 2: items.push_back(-10); break;
            default: items.push_back(static_cast<float>(random(8)) * 25.0f); break;
            }
        }
        const float limit = random(5) == 0 ? std::numeric_limits<float>::infinity()
                                          : static_cast<float>(random(10)) * 30.0f;
        const float gap = static_cast<float>(random(4)) * 5.0f;
        const auto minimum = random(static_cast<std::uint32_t>(n + 2));
        CAPTURE(test, items, limit, gap, minimum);
        CHECK(balance_lines(items, limit, gap, minimum) == naive_counts(items, limit, gap, minimum));
    }
}

TEST_CASE("Flex balancing quantization and minimum line count", "[flex][balance]") {
    const auto inf = std::numeric_limits<float>::infinity();
    CHECK(quantize_size(-inf) == 0.0);
    CHECK(quantize_size(-10) == 0.0);
    CHECK(quantize_size(inf) == static_cast<double>(std::numeric_limits<float>::max()));
    CHECK(quantize_size(0.1f) == static_cast<double>(0.1f));
    CHECK(std::isnan(quantize_size(std::numeric_limits<float>::quiet_NaN())));
    const std::vector<float> items{10, 10, 10, 10, 10};
    CHECK(balance_lines(items, inf, 0, 0) == std::vector<std::size_t>{5});
    CHECK(balance_lines(items, inf, 0, 2) == std::vector<std::size_t>{3, 2});
    CHECK(balance_lines(items, inf, 0, 3) == std::vector<std::size_t>{2, 2, 1});
    CHECK(balance_lines(items, inf, 0, std::numeric_limits<std::size_t>::max()) == std::vector<std::size_t>{1, 1, 1, 1, 1});
    CHECK(balance_lines(items, 15, 0, 1) == std::vector<std::size_t>{1, 1, 1, 1, 1});
    const std::vector<float> floored{70, -10, 20};
    CHECK(balance_lines(floored, 100, 10, 1) == std::vector<std::size_t>{2, 1});
    const std::vector<float> negative_gap{60, 60};
    CHECK(balance_lines(negative_gap, 100, -100, 1) == std::vector<std::size_t>{1, 1});
    const std::vector<float> enormous{inf, 0, 30};
    CHECK(balance_lines(enormous, 100, 0, 1) == std::vector<std::size_t>{1, 2});
    CHECK_THROWS_AS(balance_lines({}, 100, 0, 1), std::invalid_argument);
}

TEST_CASE("Flex line collection constraints and native item spans", "[flex][lines]") {
    AlgoConstants c;
    c.dir = FlexDirection::Row;
    c.is_wrap = true;
    c.known_main_size_is_definite = true;
    c.has_definite_main_size = true;
    auto items = make_items({60, 40, 40});
    Size<AvailableSpace> available{AvailableSpace::definite(100), AvailableSpace::max_content()};
    auto lines = collect_flex_lines(c, available, items);
    CHECK(counts(lines) == std::vector<std::size_t>{2, 1});
    CHECK(lines[0].items.data() == items.data());
    CHECK(lines[1].items.data() == items.data() + 2);
    lines[0].items[0].target_size.width = 42;
    CHECK(items[0].target_size.width == 42);
    c.gap.width = 1;
    CHECK(counts(collect_flex_lines(c, available, items)) == std::vector<std::size_t>{1, 2});
    c.is_wrap = false;
    CHECK(counts(collect_flex_lines(c, available, items)) == std::vector<std::size_t>{3});
    c.is_wrap = true;
    c.known_main_size_is_definite = false;
    CHECK(counts(collect_flex_lines(c, available, items)) == std::vector<std::size_t>{3});
    c.known_main_size_is_definite = true;
    available.width = AvailableSpace::min_content();
    CHECK(counts(collect_flex_lines(c, available, items)) == std::vector<std::size_t>{1, 1, 1});
    CHECK(counts(collect_balanced_flex_lines(c, available, items)) == std::vector<std::size_t>{1, 1, 1});
    available.width = AvailableSpace::max_content();
    CHECK(counts(collect_flex_lines(c, available, items)) == std::vector<std::size_t>{3});
    c.line_count = 2;
    CHECK(counts(collect_balanced_flex_lines(c, available, items)) == std::vector<std::size_t>{1, 2});
    items.clear();
    CHECK(counts(collect_flex_lines(c, available, items)) == std::vector<std::size_t>{0});
    CHECK(collect_balanced_flex_lines(c, available, items).empty());
    available.width = AvailableSpace::min_content();
    CHECK(collect_flex_lines(c, available, items).empty());
}

TEST_CASE("Flex row versus content-based column wrapping and max/min limits", "[flex][lines]") {
    for (const auto dir : {FlexDirection::Row, FlexDirection::RowReverse,
                           FlexDirection::Column, FlexDirection::ColumnReverse}) {
        CAPTURE(dir);
        AlgoConstants c;
        c.dir = dir;
        c.is_wrap = true;
        c.known_main_size_is_definite = true;
        auto items = make_items({60, 40, 40}, dir);
        auto available = Size<AvailableSpace>::splat(AvailableSpace::definite(100));
        const auto expected = is_row(dir) ? std::vector<std::size_t>{2, 1} : std::vector<std::size_t>{3};
        CHECK(counts(collect_flex_lines(c, available, items)) == expected);
        CHECK(counts(collect_balanced_flex_lines(c, available, items)) == (is_row(dir) ? std::vector<std::size_t>{1, 2} : std::vector<std::size_t>{3}));
        c.max_size.set_main(dir, 70);
        CHECK(counts(collect_flex_lines(c, available, items)) == std::vector<std::size_t>{1, 1, 1});
        c.min_size.set_main(dir, 100);
        CHECK(counts(collect_flex_lines(c, available, items)) == std::vector<std::size_t>{2, 1});
        c.min_size.set_main(dir, std::nullopt);
        c.has_definite_main_size = true;
        CHECK(counts(collect_flex_lines(c, available, items)) == std::vector<std::size_t>{2, 1});
        c.known_main_size_is_definite = false;
        c.line_count = 2;
        CHECK(counts(collect_balanced_flex_lines(c, available, items)) == std::vector<std::size_t>{1, 2});
    }
}

TEST_CASE("Flex greedy wrapping preserves float order and NaN comparisons", "[flex][lines]") {
    AlgoConstants c;
    c.dir = FlexDirection::Row;
    c.is_wrap = c.known_main_size_is_definite = c.has_definite_main_size = true;
    auto available = Size<AvailableSpace>::splat(AvailableSpace::definite(100));
    auto items = make_items({150, 0, 30});
    CHECK(counts(collect_flex_lines(c, available, items)) == std::vector<std::size_t>{1, 2});
    items = make_items({60, std::numeric_limits<float>::quiet_NaN(), 100});
    CHECK(counts(collect_flex_lines(c, available, items)) == std::vector<std::size_t>{3});
    items = make_items({60, 40});
    c.max_size.width = std::numeric_limits<float>::quiet_NaN();
    c.has_definite_main_size = false;
    CHECK(counts(collect_flex_lines(c, available, items)) == std::vector<std::size_t>{2});
    // Addition must be length += (child + gap), not (length + child) + gap.
    c.max_size.width.reset();
    c.has_definite_main_size = true;
    c.gap.width = -16777216.0f;
    available.width = AvailableSpace::definite(0.5f);
    items = make_items({16777216.0f, 1.0f});
    CHECK(counts(collect_flex_lines(c, available, items)) == std::vector<std::size_t>{1, 1});
}

namespace {
Style flex_box(float width, float height) {
    Style style;
    style.size = Size<Dimension>::from_lengths(width, height);
    return style;
}
NodeId flex_parent(Tree& tree, Style style, std::initializer_list<NodeId> children) {
    return tree.new_with_children(std::move(style), std::span<const NodeId>{children.begin(), children.size()}).value();
}
void run_flex(Tree& tree, NodeId root) {
    tree.compute_layout(root, Size<AvailableSpace>::max_content()).value();
}
void check_box(const Tree& tree, NodeId node, float x, float y, float width, float height) {
    CHECK(tree.unrounded_layout(node).location == Point<float>{x, y});
    CHECK(tree.unrounded_layout(node).size == Size<float>{width, height});
    CHECK(tree.layout(node).location == Point<float>{x, y});
    CHECK(tree.layout(node).size == Size<float>{width, height});
}
} // namespace

TEST_CASE("Native flex grow distributes free space and freezes at max size", "[flex][integration][freeze]") {
    Tree tree;
    auto a_style = flex_box(50, 20);
    a_style.flex_grow = 1;
    auto b_style = a_style;
    b_style.flex_grow = 3;
    SECTION("weighted growth") {
        const auto a = tree.new_leaf(a_style).value();
        const auto b = tree.new_leaf(b_style).value();
        const auto root = flex_parent(tree, flex_box(300, 40), {a, b});
        run_flex(tree, root);
        check_box(tree, a, 0, 0, 100, 20);
        check_box(tree, b, 100, 0, 200, 20);
    }
    SECTION("max violation freezes and redistributes remaining free space") {
        a_style.max_size.width = LengthPercentageAuto::length(80);
        const auto a = tree.new_leaf(a_style).value();
        const auto b = tree.new_leaf(b_style).value();
        const auto root = flex_parent(tree, flex_box(300, 40), {a, b});
        run_flex(tree, root);
        check_box(tree, a, 0, 0, 80, 20);
        check_box(tree, b, 80, 0, 220, 20);
    }
}

TEST_CASE("Native flex scaled shrink freezes at minimum size", "[flex][integration][freeze]") {
    Tree tree;
    auto a_style = flex_box(100, 20);
    auto b_style = flex_box(200, 20);
    // Explicit zero minima isolate scaled flex-shrink from automatic min-content sizing.
    a_style.min_size.width = b_style.min_size.width = LengthPercentageAuto::length(0);
    float a_width = 60, b_width = 120;
    SECTION("shrink factors are multiplied by the flex base sizes") {}
    SECTION("min violation freezes the first item") {
        a_style.min_size.width = LengthPercentageAuto::length(80);
        a_width = 80;
        b_width = 100;
    }
    const auto a = tree.new_leaf(a_style).value();
    const auto b = tree.new_leaf(b_style).value();
    const auto root = flex_parent(tree, flex_box(180, 40), {a, b});
    run_flex(tree, root);
    check_box(tree, a, 0, 0, a_width, 20);
    check_box(tree, b, a_width, 0, b_width, 20);
}

TEST_CASE("Native flex factors below one leave undistributed free space", "[flex][integration][freeze]") {
    Tree tree;
    auto style = flex_box(50, 20);
    float container_width = 300, child_width = 100;
    SECTION("grow sum below one distributes only that fraction of free space") {
        style.flex_grow = 0.25f;
    }
    SECTION("shrink sum below one limits shrinkage before scaling by base size") {
        style.size.width = Dimension::length(100);
        style.min_size.width = LengthPercentageAuto::length(0);
        style.flex_shrink = 0.25f;
        container_width = 100;
        child_width = 75;
    }
    const auto a = tree.new_leaf(style).value();
    const auto b = tree.new_leaf(style).value();
    const auto root = flex_parent(tree, flex_box(container_width, 40), {a, b});
    run_flex(tree, root);
    check_box(tree, a, 0, 0, child_width, 20);
    check_box(tree, b, child_width, 0, child_width, 20);
}

TEST_CASE("Native flex baseline uses pinned full-output callback assertions", "[flex][integration][baseline]") {
    // Unchanged fixture sizes/baselines and assertions from pinned upstream
    // tests/hand_written/baseline.rs::flex_baseline_alignment_uses_measure_function_baseline.
    Tree tree;
    const auto a = tree.new_leaf().value();
    const auto b = tree.new_leaf().value();
    auto style = flex_box(200, 100);
    style.align_items = AlignItems::baseline();
    const auto root = flex_parent(tree, style, {a, b});
    tree.set_leaf_layout([=](LayoutTree&, NodeId node, const LayoutInput&) {
        return node == a
            ? LayoutOutput::from_sizes_and_baselines({50, 50}, {}, Baselines::from_first(40))
            : LayoutOutput::from_sizes_and_baselines({30, 30}, {}, Baselines::from_first(20));
    });
    run_flex(tree, root);
    CHECK(tree.layout(a).location.y == 0);
    CHECK(tree.layout(b).location.y == 20);
    CHECK(tree.layout(a).location.y + 40 == tree.layout(b).location.y + 20);
}

TEST_CASE("Native flex RTL and row reverse compose physical placement", "[flex][integration][direction]") {
    for (const auto direction : {Direction::Ltr, Direction::Rtl}) {
        for (const auto flex_direction : {FlexDirection::Row, FlexDirection::RowReverse}) {
            CAPTURE(direction, flex_direction);
            Tree tree;
            const auto a = tree.new_leaf(flex_box(30, 10)).value();
            const auto b = tree.new_leaf(flex_box(50, 10)).value();
            auto style = flex_box(200, 40);
            style.direction = direction;
            style.flex_direction = flex_direction;
            const auto root = flex_parent(tree, style, {a, b});
            run_flex(tree, root);
            const bool reversed = (direction == Direction::Rtl) != (flex_direction == FlexDirection::RowReverse);
            check_box(tree, a, reversed ? 170 : 0, 0, 30, 10);
            check_box(tree, b, reversed ? 120 : 30, 0, 50, 10);
        }
    }
}

TEST_CASE("Native flex wrapping and wrap reverse place independent lines", "[flex][integration][wrap]") {
    for (const auto wrap : {FlexWrap::Wrap, FlexWrap::WrapReverse}) {
        Tree tree;
        const auto a = tree.new_leaf(flex_box(60, 20)).value();
        const auto b = tree.new_leaf(flex_box(60, 20)).value();
        auto style = flex_box(100, 80);
        style.flex_wrap = wrap;
        style.align_content = AlignContent::flex_start();
        style.gap.height = LengthPercentage::length(10);
        const auto root = flex_parent(tree, style, {a, b});
        run_flex(tree, root);
        check_box(tree, a, 0, wrap == FlexWrap::Wrap ? 0 : 60, 60, 20);
        check_box(tree, b, 0, 30, 60, 20);
    }
}

TEST_CASE("Native flex pinned line count divides cross available space", "[flex][integration][line-count]") {
    // Pinned tests/hand_written/flex_line_count.rs fixtures and exact assertions.
    // Three 100px Ahem runs separated by zero-width break opportunities, 10px high.
    // This native intrinsic callback models only that fixture's text metrics.
    for (const auto wrap : {FlexWrap::Wrap, FlexWrap::WrapReverse, FlexWrap::NoWrap}) {
        for (const bool fit_content : {false, true}) {
            CAPTURE(wrap, fit_content);
            Tree tree;
            tree.set_intrinsic_measure([](const LayoutInput& input, NodeId, NodeContext*, const Style&) {
                const auto available = input.available_space.width;
                const float width = input.known_dimensions.width.value_or(
                    available.is_min_content() ? 100.0f :
                    std::min(300.0f, available.into_option().value_or(300.0f)));
                const float used_width = std::max(100.0f, width);
                const float runs_per_line = std::max(1.0f, std::floor(used_width / 100));
                return Size<float>{used_width,
                    input.known_dimensions.height.value_or(10 * std::ceil(3 / runs_per_line))};
            });
            Style child_style;
            if (fit_content) child_style.size.width = Dimension::fit_content();
            const auto child = tree.new_leaf(child_style).value();
            auto style = flex_box(210, 100);
            style.flex_direction = FlexDirection::Column;
            style.flex_wrap = wrap;
            style.flex_line_count = 2;
            style.gap.width = LengthPercentage::length(10);
            style.align_items = AlignItems::start();
            style.align_content = AlignContent::start();
            const auto root = flex_parent(tree, style, {child});
            run_flex(tree, root);
            CHECK(tree.layout(child).size == (wrap == FlexWrap::NoWrap ? Size<float>{210, 20} : Size<float>{100, 30}));
        }
    }
}

TEST_CASE("Native flex balanced line count creates real layout lines", "[flex][integration][line-count]") {
    Tree tree;
    std::array<NodeId, 4> children;
    for (auto& child : children) child = tree.new_leaf(flex_box(20, 10)).value();
    auto style = flex_box(100, 40);
    style.flex_wrap = FlexWrap::Balance;
    style.flex_line_count = 2;
    style.align_content = AlignContent::start();
    style.gap = Size<LengthPercentage>::from_lengths(10, 5);
    const auto root = tree.new_with_children(style, children).value();
    run_flex(tree, root);
    check_box(tree, children[0], 0, 0, 20, 10);
    check_box(tree, children[1], 30, 0, 20, 10);
    check_box(tree, children[2], 0, 15, 20, 10);
    check_box(tree, children[3], 30, 15, 20, 10);
}

TEST_CASE("Native flex intrinsic content and aspect ratio transfer", "[flex][integration][intrinsic]") {
    Tree tree;
    tree.set_intrinsic_measure([](const LayoutInput&, NodeId, NodeContext*, const Style&) {
        return Size<float>{70, 15};
    });
    const auto text = tree.new_leaf().value();
    auto image_style = flex_box(0, 30);
    image_style.size.width = Dimension::auto_();
    image_style.aspect_ratio = 2;
    const auto image = tree.new_leaf(image_style).value();
    Style root_style;
    root_style.align_items = AlignItems::start();
    const auto root = flex_parent(tree, root_style, {text, image});
    run_flex(tree, root);
    check_box(tree, root, 0, 0, 130, 30);
    check_box(tree, text, 0, 0, 70, 15);
    check_box(tree, image, 70, 0, 60, 30);
}

TEST_CASE("Native flex auto margins absorb free space before justification", "[flex][integration][margin]") {
    Tree tree;
    auto child_style = flex_box(40, 20);
    child_style.margin.left = child_style.margin.right = LengthPercentageAuto::auto_();
    child_style.margin.top = child_style.margin.bottom = LengthPercentageAuto::auto_();
    const auto child = tree.new_leaf(child_style).value();
    auto style = flex_box(200, 100);
    style.justify_content = JustifyContent::end();
    const auto root = flex_parent(tree, style, {child});
    run_flex(tree, root);
    check_box(tree, child, 80, 40, 40, 20);
    CHECK(tree.layout(child).margin == Rect<float>{80, 80, 40, 40});
}

TEST_CASE("Native flex resolves percentage physical margins against container width", "[flex][integration][margin]") {
    Tree tree;
    auto child_style = flex_box(40, 20);
    child_style.margin = Rect<LengthPercentageAuto>::splat(LengthPercentageAuto::percent(0.1f));
    const auto child = tree.new_leaf(child_style).value();
    const auto root = flex_parent(tree, flex_box(200, 100), {child});
    run_flex(tree, root);
    check_box(tree, child, 20, 20, 40, 20);
    CHECK(tree.layout(child).margin == Rect<float>{20, 20, 20, 20});
}

TEST_CASE("Native flex calc basis gap and padding use installed resolver", "[flex][integration][calc]") {
    Tree tree;
    std::array<unsigned, 3> calls{};
    tree.set_calc_resolver([&](Calc calc, float basis) {
        REQUIRE(calc.handle >= 1);
        REQUIRE(calc.handle <= 3);
        ++calls[calc.handle - 1];
        if (calc.handle == 1) return basis * 0.25f + 5; // child basis: 180/4 + 5 = 50
        if (calc.handle == 2) return basis * 0.05f + 1; // main gap: 180/20 + 1 = 10
        return basis * 0.05f; // root padding: 200/20 = 10
    });
    auto child_style = flex_box(0, 20);
    child_style.flex_basis = Dimension::calc(Calc{1});
    const auto a = tree.new_leaf(child_style).value();
    const auto b = tree.new_leaf(child_style).value();
    auto style = flex_box(200, 100);
    style.padding.left = style.padding.right = LengthPercentage::calc(Calc{3});
    style.gap.width = LengthPercentage::calc(Calc{2});
    const auto root = flex_parent(tree, style, {a, b});
    tree.compute_layout(root, Size<AvailableSpace>::splat(AvailableSpace::definite(200))).value();
    check_box(tree, a, 10, 0, 50, 20);
    check_box(tree, b, 70, 0, 50, 20);
    for (const auto count : calls) CHECK(count > 0);
}

TEST_CASE("Native flex automatic minimum protects min-content unless overflow is scrollable", "[flex][integration][intrinsic]") {
    for (const auto overflow : {Overflow::Visible, Overflow::Hidden}) {
        CAPTURE(overflow);
        Tree tree;
        tree.set_intrinsic_measure([](const LayoutInput& input, NodeId, NodeContext*, const Style&) {
            return Size<float>{input.available_space.width.is_min_content() ? 80.0f : 120.0f, 20};
        });
        Style style;
        style.overflow.x = overflow;
        const auto child = tree.new_leaf(style).value();
        auto root_style = flex_box(50, 40);
        root_style.align_items = AlignItems::start();
        const auto root = flex_parent(tree, root_style, {child});
        run_flex(tree, root);
        check_box(tree, child, 0, 0, overflow == Overflow::Visible ? 80 : 50, 20);
    }
}

TEST_CASE("Native flex relayout invalidates container distribution after child style change", "[flex][integration][cache]") {
    Tree tree;
    auto style = flex_box(50, 20);
    style.flex_grow = 1;
    const auto a = tree.new_leaf(style).value();
    const auto b = tree.new_leaf(style).value();
    const auto root = flex_parent(tree, flex_box(200, 40), {a, b});
    run_flex(tree, root);
    check_box(tree, a, 0, 0, 100, 20);
    check_box(tree, b, 100, 0, 100, 20);
    style.max_size.width = LengthPercentageAuto::length(60);
    tree.set_style(a, style).value();
    run_flex(tree, root);
    check_box(tree, a, 0, 0, 60, 20);
    check_box(tree, b, 60, 0, 140, 20);
    run_flex(tree, root); // A cache hit must retain identical child geometry.
    check_box(tree, a, 0, 0, 60, 20);
    check_box(tree, b, 60, 0, 140, 20);
}

TEST_CASE("Native flex absolute child is excluded from flow and positioned by containing block", "[flex][integration][oof]") {
    Tree tree;
    const auto flow = tree.new_leaf(flex_box(50, 20)).value();
    auto absolute_style = flex_box(30, 10);
    absolute_style.position = Position::Absolute;
    absolute_style.inset.right = LengthPercentageAuto::length(10);
    absolute_style.inset.bottom = LengthPercentageAuto::length(15);
    const auto absolute = tree.new_leaf(absolute_style).value();
    auto style = flex_box(200, 100);
    style.position = Position::Relative;
    const auto root = flex_parent(tree, style, {flow, absolute});
    run_flex(tree, root);
    check_box(tree, flow, 0, 0, 50, 20);
    check_box(tree, absolute, 160, 75, 30, 10);
}

TEST_CASE("Native flex absolute static position honors flex alignment", "[flex][integration][oof]") {
    Tree tree;
    const auto flow = tree.new_leaf(flex_box(50, 20)).value();
    auto absolute_style = flex_box(30, 10);
    absolute_style.position = Position::Absolute;
    const auto absolute = tree.new_leaf(absolute_style).value();
    auto style = flex_box(200, 100);
    style.position = Position::Relative;
    style.justify_content = JustifyContent::center();
    style.align_items = AlignItems::center();
    const auto root = flex_parent(tree, style, {flow, absolute});
    run_flex(tree, root);
    check_box(tree, flow, 75, 40, 50, 20);
    check_box(tree, absolute, 85, 45, 30, 10);
}

namespace {
// Observe the algorithm boundary, not Tree's cache-dependent leaf callback count.
// Storage is native Tree storage; child outputs are deliberately simple, uncached
// full outputs so every requested flex pass remains visible.
struct FlexTraceTree final : LayoutTree {
    Tree storage;
    std::vector<std::pair<NodeId, LayoutInput>> calls;
    std::vector<NodeId> child_ids(NodeId n) const override { return storage.child_ids(n); }
    const Style& style(NodeId n) const override { return storage.style(n); }
    LayoutOutput compute_child_layout(NodeId n, const LayoutInput& input) override {
        calls.emplace_back(n, input);
        return LayoutOutput::from_sizes_and_baselines(
            {input.known_dimensions.width.value_or(20), input.known_dimensions.height.value_or(10)},
            {}, Baselines::from_first(7));
    }
    LayoutOutput measure(NodeId n, const LayoutInput& input) override {
        return compute_child_layout(n, input);
    }
    const Layout& unrounded_layout(NodeId n) const override { return storage.unrounded_layout(n); }
    void set_unrounded_layout(NodeId n, const Layout& l) override { storage.set_unrounded_layout(n, l); }
    void set_final_layout(NodeId n, const Layout& l) override { storage.set_final_layout(n, l); }
    void clear_hoisted_children(NodeId n) override { storage.clear_hoisted_children(n); }
    void add_hoisted_children(NodeId n, std::span<const NodeId> children) override {
        storage.add_hoisted_children(n, children);
    }
    std::vector<NodeId> hoisted_child_ids(NodeId n) const override { return storage.hoisted_child_ids(n); }
};
} // namespace

TEST_CASE("Source-derived flex baseline callbacks preserve pass order and exact inputs", "[flex][callbacks][baseline]") {
    // reference/upstream/src/compute/flexbox.rs: determine_hypothetical_cross_size,
    // calculate_children_base_lines, final_layout_pass: each pass visits a then b.
    FlexTraceTree tree;
    Style child_style;
    child_style.flex_basis = Dimension::length(20);
    child_style.min_size.width = LengthPercentageAuto::length(0);
    const auto a = tree.storage.new_leaf(child_style).value();
    const auto b = tree.storage.new_leaf(child_style).value();
    auto style = flex_box(100, 40);
    style.align_items = AlignItems::baseline();
    const auto root = flex_parent(tree.storage, style, {a, b});
    LayoutInput input;
    const auto output = compute_flex(tree, root, input);
    REQUIRE(tree.calls.size() == 6);
    for (std::size_t i = 0; i < tree.calls.size(); ++i) {
        CAPTURE(i);
        CHECK(tree.calls[i].first == (i % 2 == 0 ? a : b));
        const auto& call = tree.calls[i].second;
        CHECK(call.run_mode == (i < 2 ? RunMode::ComputeSize : RunMode::PerformLayout));
        CHECK(call.axis == (i < 2 ? RequestedAxis::Vertical : RequestedAxis::Both));
        CHECK(call.sizing_mode == SizingMode::ContentSize);
        CHECK(call.known_dimensions == (i < 2 ? Size<std::optional<float>>{20, std::nullopt}
                                             : Size<std::optional<float>>{20, 10}));
        CHECK(call.known_dimensions_are_definite == Size<bool>{true, false});
        CHECK(call.parent_size == Size<std::optional<float>>{100, 40});
        CHECK(call.available_space == Size<AvailableSpace>::from_lengths(100, 40));
        CHECK(call.vertical_margins_are_collapsible == Line<bool>{false, false});
    }
    CHECK(output.size == Size<float>{100, 40});
    CHECK(output.baselines.first == 7);
}

TEST_CASE("Source-derived flex intrinsic probes stay per-item before cross and final passes", "[flex][callbacks][intrinsic]") {
    // Pinned determine_flex_base_size does max-content basis then min-content
    // automatic minimum for EACH item, not all bases followed by all minima.
    FlexTraceTree tree;
    const auto a = tree.storage.new_leaf().value();
    const auto b = tree.storage.new_leaf().value();
    auto style = flex_box(100, 40);
    style.align_items = AlignItems::start();
    const auto root = flex_parent(tree.storage, style, {a, b});
    compute_flex(tree, root, LayoutInput{});
    REQUIRE(tree.calls.size() == 8);
    for (std::size_t i = 0; i < 8; ++i) {
        CAPTURE(i);
        CHECK(tree.calls[i].first == (i < 4 ? (i < 2 ? a : b) : (i % 2 == 0 ? a : b)));
        const auto& call = tree.calls[i].second;
        CHECK(call.run_mode == (i < 6 ? RunMode::ComputeSize : RunMode::PerformLayout));
        CHECK(call.axis == (i < 4 ? RequestedAxis::Horizontal : i < 6 ? RequestedAxis::Vertical : RequestedAxis::Both));
        CHECK(call.sizing_mode == SizingMode::ContentSize);
        // Basis/minimum probes expose only the parent's cross size, avoiding
        // cyclic main-axis percentage resolution (pinned child_parent_size).
        CHECK(call.parent_size == (i < 4 ? Size<std::optional<float>>{std::nullopt, 40}
                                         : Size<std::optional<float>>{100, 40}));
        if (i < 4) {
            CHECK(call.known_dimensions == Size<std::optional<float>>{});
            CHECK(call.known_dimensions_are_definite == Size<bool>{true, true});
            CHECK(call.available_space == Size<AvailableSpace>{
                i % 2 == 0 ? AvailableSpace::max_content() : AvailableSpace::min_content(), AvailableSpace::definite(40)});
        } else {
            CHECK(call.known_dimensions == (i < 6 ? Size<std::optional<float>>{20, std::nullopt}
                                                  : Size<std::optional<float>>{20, 10}));
            CHECK(call.known_dimensions_are_definite == Size<bool>{true, false});
            CHECK(call.available_space == Size<AvailableSpace>::from_lengths(100, 40));
        }
    }
}

TEST_CASE("Source-derived flex own style normalizes flags and cross auto margins disable stretch", "[flex][callbacks][definiteness]") {
    FlexTraceTree tree;
    Style child_style;
    child_style.min_size.width = LengthPercentageAuto::length(0);
    bool cross_definite = true;
    std::size_t expected_calls = 2; // intrinsic basis and final layout
    SECTION("own style dimensions are definite despite incoming false flags") {}
    SECTION("auto cross margins suppress stretch definiteness and require a cross probe") {
        child_style.margin.top = LengthPercentageAuto::auto_();
        cross_definite = false;
        expected_calls = 3;
    }
    SECTION("resolved cross style is definite without stretch") {
        child_style.size.height = Dimension::length(10);
    }
    const auto child = tree.storage.new_leaf(child_style).value();
    const auto root = flex_parent(tree.storage, flex_box(100, 40), {child});
    LayoutInput input;
    input.known_dimensions_are_definite = {false, false};
    compute_flex(tree, root, input);
    REQUIRE(tree.calls.size() == expected_calls);
    CHECK(tree.calls.back().second.known_dimensions_are_definite == Size<bool>{true, cross_definite});
    CHECK(tree.calls.back().second.run_mode == RunMode::PerformLayout);
    CHECK(tree.calls.back().second.known_dimensions.both_axis_defined());
}

TEST_CASE("Source-derived flex known dimension flags distinguish numeric sizes from definiteness", "[flex][callbacks][definiteness]") {
    // Pinned item_known_dimension_definiteness: main = definite container OR
    // definite basis; cross = stretch OR resolved style OR column fit-content
    // against definite available width. Merely passing a number is insufficient.
    for (const auto dir : {FlexDirection::Row, FlexDirection::Column}) {
        for (const bool basis_definite : {false, true}) {
            for (const bool main_definite : {false, true}) {
                for (const bool cross_available_definite : {false, true}) {
                    for (const bool stretch : {false, true}) {
                        CAPTURE(dir, basis_definite, main_definite, cross_available_definite, stretch);
                        FlexTraceTree tree;
                        Style child_style;
                        child_style.min_size = Size<LengthPercentageAuto>::from_lengths(0, 0);
                        if (basis_definite) child_style.flex_basis = Dimension::length(20);
                        const auto child = tree.storage.new_leaf(child_style).value();
                        Style style;
                        style.flex_direction = dir;
                        style.align_items = stretch ? AlignItems::stretch() : AlignItems::start();
                        const auto root = flex_parent(tree.storage, style, {child});
                        LayoutInput input;
                        input.known_dimensions = {100, 40};
                        input.known_dimensions_are_definite = Size<bool>{false, false}.with_main(dir, main_definite);
                        if (cross_available_definite)
                            input.available_space.set_cross(dir, AvailableSpace::definite(100));
                        compute_flex(tree, root, input);
                        // Explicit zero minima eliminate the min-content probe;
                        // a definite basis eliminates the basis probe; stretch
                        // in a single known-size line eliminates the cross probe.
                        REQUIRE(tree.calls.size() == std::size_t{1} + !basis_definite + !stretch);
                        const auto expected = Size<bool>{}.with_main(dir, main_definite || basis_definite)
                            .with_cross(dir, stretch || (!is_row(dir) && cross_available_definite));
                        for (std::size_t i = basis_definite ? 0 : 1; i < tree.calls.size(); ++i) {
                            CHECK(tree.calls[i].first == child);
                            CHECK(tree.calls[i].second.known_dimensions_are_definite == expected);
                        }
                        const auto& final = tree.calls.back().second;
                        CHECK(final.run_mode == RunMode::PerformLayout);
                        CHECK(final.axis == RequestedAxis::Both);
                        CHECK(final.known_dimensions.both_axis_defined());
                        CHECK(final.parent_size == input.known_dimensions);
                    }
                }
            }
        }
    }
}

TEST_CASE("Source-derived flex ComputeSize exits avoid unnecessary callbacks", "[flex][callbacks][short-circuit]") {
    FlexTraceTree tree;
    const auto child = tree.storage.new_leaf().value();
    Style style;
    const auto root = flex_parent(tree.storage, style, {child});
    LayoutInput input;
    input.run_mode = RunMode::ComputeSize;
    SECTION("both known dimensions exit before any item sizing") {
        input.known_dimensions = {100, 40};
        CHECK(compute_flex(tree, root, input).size == Size<float>{100, 40});
        CHECK(tree.calls.empty());
    }
    SECTION("known horizontal request exits even with unknown height") {
        input.axis = RequestedAxis::Horizontal;
        input.known_dimensions.width = 100;
        CHECK(compute_flex(tree, root, input).size == Size<float>{100, 0});
        CHECK(tree.calls.empty());
    }
    SECTION("intrinsic main-only request probes basis and minimum but never cross or final layout") {
        input.axis = RequestedAxis::Horizontal;
        CHECK(compute_flex(tree, root, input).size == Size<float>{20, 0});
        REQUIRE(tree.calls.size() == 2);
        for (std::size_t i = 0; i < 2; ++i) {
            CHECK(tree.calls[i].first == child);
            const auto& call = tree.calls[i].second;
            CHECK(call.run_mode == RunMode::ComputeSize);
            CHECK(call.axis == RequestedAxis::Horizontal);
            CHECK(call.sizing_mode == SizingMode::ContentSize);
            CHECK(call.available_space.width == (i == 0 ? AvailableSpace::max_content() : AvailableSpace::min_content()));
        }
    }
}

TEST_CASE("Native flex hidden descendants reset and empty layout clears stale hoisting", "[flex][integration][hidden][oof]") {
    // Pinned hidden_layout runs after in-flow layout; empty-container output must
    // retain Some(positioning_area) so compute_oof_layout clears stale hoisting.
    Tree tree;
    auto absolute_style = flex_box(30, 10);
    absolute_style.position = Position::Absolute;
    const auto absolute = tree.new_leaf(absolute_style).value();
    const auto descendant = tree.new_leaf(flex_box(12, 8)).value();
    const auto hidden = flex_parent(tree, flex_box(40, 20), {descendant});
    const auto flow = tree.new_leaf(flex_box(20, 10)).value();
    auto style = flex_box(100, 40);
    style.position = Position::Relative;
    const auto root = flex_parent(tree, style, {flow, hidden, absolute});
    run_flex(tree, root);
    CHECK(tree.hoisted_child_ids(root) == std::vector<NodeId>{absolute});
    auto hidden_style = tree.style(hidden);
    hidden_style.display = Display::None;
    tree.set_style(hidden, hidden_style).value();
    run_flex(tree, root);
    check_box(tree, flow, 0, 0, 20, 10);
    check_box(tree, hidden, 0, 0, 0, 0);
    check_box(tree, descendant, 0, 0, 0, 0);
    // compute_hidden_layout overwrites the preliminary sibling order with zero.
    CHECK(tree.unrounded_layout(hidden).order == 0);
    tree.remove_children_range(root, 0, 3).value();
    // Exercise compute_flex directly: persistent Tree dispatch uses compute_leaf
    // for empty nodes, whereas the flex entry point has its own empty fast path.
    const auto output = compute_flex(tree, root, LayoutInput{});
    CHECK(output.size == Size<float>{100, 40});
    CHECK(output.oof_candidates.empty());
    REQUIRE(output.oof_positioning_area.has_value());
    CHECK(output.oof_positioning_area->size == Size<float>{0, 0});
    auto positioned = output;
    compute_oof_layout(tree, root, positioned);
    CHECK(tree.hoisted_child_ids(root).empty());
    run_flex(tree, root);
    check_box(tree, root, 0, 0, 100, 40);
}

TEST_CASE("Native flex OOF candidates interleave direct and bubbled descendants in document order", "[flex][integration][oof]") {
    // Pinned collect_oof_candidates walks document order even when physical
    // placement is reversed. Non-containing-block wrappers bubble descendants.
    for (const auto dir : {FlexDirection::Row, FlexDirection::RowReverse}) {
        CAPTURE(dir);
        Tree tree;
        auto abs_style = flex_box(10, 10);
        abs_style.position = Position::Absolute;
        const auto first = tree.new_leaf(abs_style).value();
        const auto nested = tree.new_leaf(abs_style).value();
        const auto last = tree.new_leaf(abs_style).value();
        const auto flow = tree.new_leaf(flex_box(20, 10)).value();
        const auto wrapper = flex_parent(tree, flex_box(40, 20), {flow, nested});
        auto style = flex_box(100, 40);
        style.position = Position::Relative;
        style.flex_direction = dir;
        const auto root = flex_parent(tree, style, {first, wrapper, last});
        run_flex(tree, root);
        CHECK(tree.hoisted_child_ids(root) == std::vector<NodeId>{first, nested, last});
        CHECK(tree.hoisted_child_ids(wrapper).empty());
        check_box(tree, wrapper, dir == FlexDirection::Row ? 0 : 60, 0, 40, 20);
        // Nested static area is translated from the wrapper, not replaced with
        // the containing block's area, and no OOF box consumes flow space.
        check_box(tree, nested, dir == FlexDirection::Row ? 0 : 60, 0, 10, 10);
        check_box(tree, first, dir == FlexDirection::Row ? 0 : 90, 0, 10, 10);
        check_box(tree, last, dir == FlexDirection::Row ? 0 : 90, 0, 10, 10);
    }
}

TEST_CASE("Native flex RTL scroll gutter mirrors physical placement but not scroll-origin overflow", "[flex][integration][direction][overflow]") {
    // Pinned compute_constants puts the vertical scrollbar on the RTL left;
    // perform_final_layout_on_item mirrors contribution_location around the
    // scroll origin, so physical negative-x overflow has positive scroll extent.
    for (const auto direction : {Direction::Ltr, Direction::Rtl}) {
        CAPTURE(direction);
        Tree tree;
        auto child_style = flex_box(120, 20);
        child_style.flex_shrink = 0;
        const auto child = tree.new_leaf(child_style).value();
        auto style = flex_box(100, 50);
        style.direction = direction;
        style.overflow.y = Overflow::Scroll;
        style.scrollbar_width = 10;
        const auto root = flex_parent(tree, style, {child});
        run_flex(tree, root);
        check_box(tree, child, direction == Direction::Ltr ? 0 : -20, 0, 120, 20);
        CHECK(tree.unrounded_layout(root).scrollbar_size == Size<float>{10, 0});
        CHECK(tree.unrounded_layout(root).scrollable_overflow_rect == Rect<float>{0, 120, 0, 20});
    }
}
