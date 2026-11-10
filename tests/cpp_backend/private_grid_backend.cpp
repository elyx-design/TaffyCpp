// Test-only C ABI adapter. Algorithms remain in the production grid APIs.
#include "../ported/private/support/private_suite_grid.h"
#include "../../src/grid.hpp"
#include "../../src/grid/placement_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace {
using namespace taffy;
using namespace taffy::grid;
struct Status { int32_t code; };
void require(bool valid) { if (!valid) throw Status{1}; }
template<class F> int32_t boundary(F&& f) noexcept {
    try { f(); return 0; }
    catch (Status s) { return s.code; }
    catch (...) { return 2; }
}
template<class T> bool valid_ptr(const T* p) {
    return p && reinterpret_cast<std::uintptr_t>(p) % alignof(T) == 0;
}
template<class T> std::span<const T> input(const T* p, size_t n) {
    if (!n) return {};
    require(n <= 1000000 && valid_ptr(p));
    return {p, n};
}
template<class T> T load(const T* p) { require(valid_ptr(p)); return *p; }
template<class T> void store(T* p, T value) { require(valid_ptr(p)); *p = value; }
template<class T> void output(T* p, size_t capacity, const std::vector<T>& values) {
    if (capacity < values.size()) throw Status{3};
    if (!values.empty()) {
        require(valid_ptr(p));
        std::copy(values.begin(), values.end(), p);
    }
}
AbsoluteAxis axis(uint32_t a) {
    require(a <= 1);
    return a == 0 ? AbsoluteAxis::Horizontal : AbsoluteAxis::Vertical;
}
Calc calc_handle(const void* p) {
    const auto handle = reinterpret_cast<std::uintptr_t>(p);
    require(handle && handle % 8 == 0);
    return Calc{handle};
}
MinTrackSizingFunction min_sizing(PsGridSizing s) {
    switch (s.tag) {
    case 0: return MinTrackSizingFunction::auto_();
    case 1: return MinTrackSizingFunction::length(s.value);
    case 2: return MinTrackSizingFunction::percent(s.value);
    case 4: return MinTrackSizingFunction::calc(calc_handle(s.calc));
    case 5: return MinTrackSizingFunction::min_content();
    case 6: return MinTrackSizingFunction::max_content();
    default: throw Status{1};
    }
}
MaxTrackSizingFunction max_sizing(PsGridSizing s) {
    switch (s.tag) {
    case 0: return MaxTrackSizingFunction::auto_();
    case 1: return MaxTrackSizingFunction::length(s.value);
    case 2: return MaxTrackSizingFunction::percent(s.value);
    case 3: return MaxTrackSizingFunction::fr(s.value);
    case 4: return MaxTrackSizingFunction::calc(calc_handle(s.calc));
    case 5: return MaxTrackSizingFunction::min_content();
    case 6: return MaxTrackSizingFunction::max_content();
    default: throw Status{1};
    }
}
std::vector<TrackSizingFunction> tracks(const PsGridTrackDef* p, size_t n) {
    std::vector<TrackSizingFunction> result;
    for (auto t : input(p, n)) result.emplace_back(min_sizing(t.min), max_sizing(t.max));
    return result;
}
std::vector<GridTemplateComponent> components(const PsGridComponent* p, size_t n) {
    std::vector<GridTemplateComponent> result;
    for (auto c : input(p, n)) {
        auto definitions = tracks(c.tracks, c.len);
        if (c.mode == 0) {
            require(definitions.size() == 1);
            result.emplace_back(definitions.front());
        } else {
            require(c.mode <= 3);
            if (c.mode == 1) require(c.count <= UINT16_MAX);
            auto count = c.mode == 1 ? RepetitionCount::count(static_cast<uint16_t>(c.count))
                         : c.mode == 2 ? RepetitionCount::auto_fit() : RepetitionCount::auto_fill();
            result.emplace_back(GridTemplateRepetition{count, std::move(definitions), {}});
        }
    }
    return result;
}
Style style(PsGridStyle v) {
    Style s;
    s.display = Display::Grid;
    const auto dimension = [](float x) { return std::isnan(x) ? Dimension::auto_() : Dimension::length(x); };
    const auto minimum = [](float x) { return std::isnan(x) ? LengthPercentageAuto::auto_() : LengthPercentageAuto::length(x); };
    s.size = {dimension(v.width), dimension(v.height)};
    s.min_size = {minimum(v.min_width), minimum(v.min_height)};
    s.grid_template_columns = components(v.columns, v.columns_len);
    s.grid_template_rows = components(v.rows, v.rows_len);
    s.grid_auto_columns = tracks(v.auto_columns, v.auto_columns_len);
    s.grid_auto_rows = tracks(v.auto_rows, v.auto_rows_len);
    s.gap = {LengthPercentage::length(v.gap_width), LengthPercentage::length(v.gap_height)};
    s.padding = {LengthPercentage::length(v.padding_left), LengthPercentage::length(v.padding_right),
                 LengthPercentage::length(v.padding_top), LengthPercentage::length(v.padding_bottom)};
    return s;
}
GridPlacement placement(PsGridPlacement p) {
    switch (p.kind) {
    case 0: return GridPlacement::auto_();
    case 1:
        require(p.value >= INT16_MIN && p.value <= INT16_MAX && p.value != 0);
        return GridPlacement::line(static_cast<int16_t>(p.value));
    case 2:
        require(p.value > 0 && p.value <= UINT16_MAX);
        return GridPlacement::span(static_cast<uint16_t>(p.value));
    default: throw Status{1};
    }
}
std::vector<ItemPlacement> resolve(std::span<const PsGridChild> children, uint16_t cols, uint16_t rows) {
    NamedLineResolver resolver(Style{}, 0, 0);
    resolver.set_explicit_column_count(cols);
    resolver.set_explicit_row_count(rows);
    std::vector<ItemPlacement> result;
    for (auto c : children) {
        Style s;
        s.display = Display::Grid;
        s.grid_column = {placement(c.column_start), placement(c.column_end)};
        s.grid_row = {placement(c.row_start), placement(c.row_end)};
        result.push_back(resolver.resolve(s));
    }
    return result;
}
PsGridCounts encode(TrackCounts c) { return {c.negative_implicit, c.explicit_, c.positive_implicit}; }
TrackCounts counts(PsGridCounts c) {
    require(uint32_t(c.negative_implicit) + c.explicit_count + c.positive_implicit <= 30000);
    return TrackCounts::from_raw(c.negative_implicit, c.explicit_count, c.positive_implicit);
}
template<class T> PsGridSizing encode_sizing(T s) {
    switch (s.kind()) {
    case LengthKind::Auto: return {0, 0, nullptr};
    case LengthKind::Length: return {1, s.value(), nullptr};
    case LengthKind::Percent: return {2, s.value(), nullptr};
    case LengthKind::Fr: return {3, s.value(), nullptr};
    case LengthKind::Calc: return {4, 0, reinterpret_cast<const void*>(s.calc_value().handle)};
    case LengthKind::MinContent: return {5, 0, nullptr};
    case LengthKind::MaxContent: return {6, 0, nullptr};
    case LengthKind::FitContentPx: return {7, s.value(), nullptr};
    case LengthKind::FitContentPercent: return {8, s.value(), nullptr};
    case LengthKind::FitContentCalc: return {9, 0, reinterpret_cast<const void*>(s.calc_value().handle)};
    default: throw Status{2};
    }
}
} // namespace

extern "C" int32_t ps_grid_explicit(const PsGridStyle* raw, uint32_t a, uint32_t has_size,
    float size, uint32_t strategy, PsGridCalcResolver calc, PsGridExplicit* out) {
    return boundary([&] {
        auto v = load(raw);
        // The pinned Rust export rejects calc templates without a callback,
        // despite the older header comment advertising a default resolver.
        if (!calc) {
            for (auto list : {input(v.columns, v.columns_len), input(v.rows, v.rows_len)})
                for (auto c : list)
                    for (auto t : input(c.tracks, c.len))
                        require(t.min.tag != 4 && t.max.tag != 4);
        }
        auto s = style(v);
        require(strategy <= 1 && has_size <= 1);
        CalcResolver resolver = [calc](Calc p, float basis) {
            if (!calc) throw Status{2};
            return calc(reinterpret_cast<const void*>(p.handle), basis);
        };
        auto result = compute_explicit_grid_size_in_axis(s, has_size ? std::optional<float>(size) : std::nullopt,
            strategy == 0 ? AutoRepeatStrategy::MaxRepetitionsThatDoNotOverflow
                          : AutoRepeatStrategy::MinRepetitionsThatDoOverflow, resolver, axis(a));
        store(out, PsGridExplicit{result.auto_repetitions, result.explicit_tracks});
    });
}
extern "C" int32_t ps_grid_tracks(const PsGridStyle* raw, PsGridCounts c, uint32_t a,
    uint16_t repetitions, const uint8_t* has_items, size_t has_items_len,
    PsGridTrack* out, size_t capacity, size_t* written) {
    return boundary([&] {
        auto s = style(load(raw));
        auto track_counts = counts(c);
        auto occupied = input(has_items, has_items_len);
        require(occupied.empty() || occupied.size() >= track_counts.len());
        require(std::all_of(occupied.begin(), occupied.end(), [](uint8_t x) { return x <= 1; }));
        auto direction = axis(a);
        std::vector<GridTrack> native;
        initialize_grid_tracks(native, track_counts,
            direction == AbsoluteAxis::Horizontal ? s.grid_template_columns : s.grid_template_rows,
            direction == AbsoluteAxis::Horizontal ? s.grid_auto_columns : s.grid_auto_rows,
            direction == AbsoluteAxis::Horizontal ? s.gap.width : s.gap.height, repetitions,
            [&](int32_t i) { return i >= 0 && size_t(i) < occupied.size() && occupied[size_t(i)] != 0; });
        std::vector<PsGridTrack> result;
        for (const auto& t : native)
            result.push_back({t.kind == GridTrackKind::Track ? 0u : 1u, uint32_t(t.is_collapsed),
                              encode_sizing(t.min_track_sizing_function), encode_sizing(t.max_track_sizing_function)});
        store(written, result.size());
        output(out, capacity, result);
    });
}
extern "C" int32_t ps_grid_child_bounds(PsGridPlacement start, PsGridPlacement end,
    uint16_t explicit_count, PsGridBounds* out) {
    return boundary([&] {
        const auto p = into_origin_zero_ignoring_named({placement(start), placement(end)}, explicit_count);
        // Compose the same production line resolver and grid limits used by the
        // estimate API; there is no separately exported native child-bounds helper.
        if (is_definite(p)) {
            auto lines = resolve_definite_grid_lines(p);
            store(out, PsGridBounds{static_cast<int16_t>(std::max(lines.start, -int32_t(max_grid_tracks))),
                                    static_cast<int16_t>(std::min(lines.end, int32_t(max_grid_tracks))), 1});
        } else {
            store(out, PsGridBounds{0, 0, indefinite_span(p)});
        }
    });
}
extern "C" int32_t ps_grid_estimate(uint16_t cols, uint16_t rows, const PsGridChild* children,
    size_t len, PsGridCounts* col_out, PsGridCounts* row_out) {
    return boundary([&] {
        require(cols <= 10000 && rows <= 10000);
        auto placements = resolve(input(children, len), cols, rows);
        auto result = compute_grid_size_estimate({cols, rows}, placements);
        store(col_out, encode(result.width));
        store(row_out, encode(result.height));
    });
}
extern "C" int32_t ps_grid_place(uint16_t cols, uint16_t rows, const PsGridChild* children,
    size_t len, uint32_t flow, PsGridPlaced* out, size_t capacity, size_t* written,
    PsGridCounts* col_out, PsGridCounts* row_out) {
    return boundary([&] {
        require(cols <= 10000 && rows <= 10000 && flow <= 3);
        auto values = input(children, len);
        auto placements = resolve(values, cols, rows);
        std::vector<PlacedGridItem> items;
        for (auto c : values) {
            require(c.node <= std::numeric_limits<size_t>::max());
            // The production placement-only record avoids fabricating a tree
            // or GridItem sizing state that the placement operation never reads.
            items.push_back({NodeId{c.node}, static_cast<uint16_t>(c.node), {}, {}});
        }
        auto estimate = compute_grid_size_estimate({cols, rows}, placements);
        CellOccupancy occupancy(estimate.width, estimate.height);
        constexpr GridAutoFlow flows[] = {GridAutoFlow::Row, GridAutoFlow::Column,
                                         GridAutoFlow::RowDense, GridAutoFlow::ColumnDense};
        place_grid_items(occupancy, items, placements, flows[flow]);
        std::vector<PsGridPlaced> result;
        for (auto i : items)
            result.push_back({i.node.value, static_cast<int16_t>(i.column.start), static_cast<int16_t>(i.column.end),
                              static_cast<int16_t>(i.row.start), static_cast<int16_t>(i.row.end)});
        store(written, result.size());
        output(out, capacity, result);
        store(col_out, encode(occupancy.track_counts(AbsoluteAxis::Horizontal)));
        store(row_out, encode(occupancy.track_counts(AbsoluteAxis::Vertical)));
    });
}
