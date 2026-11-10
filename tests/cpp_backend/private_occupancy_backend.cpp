// Test-only occupancy/named and flex balance C ABI adapter.
// Algorithms are the production grid.hpp/flex.hpp implementations, not oracle copies.
// Known native/source differences deliberately NOT patched here: native names
// above line 10001 are discarded; native occupancy expansion is limited to
// max_grid_tracks; raw count arithmetic is widened rather than Rust's u16/i16
// debug-overflow/release-wrap arithmetic. The ABI's "spilled" observation reports
// native vector capacity > 2 or ordered-index promotion, NOT Rust SmallVec spill state.
#include "../ported/private/support/private_suite_occupancy_named.h"
#include "../ported/private/support/private_suite_misc.h"
#include "../../src/grid.hpp"
#include "../../src/flex.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace {
using namespace taffy;
using namespace taffy::grid;

// Visibility-only test instrumentation, analogous to occupancy_named_instrument.py.
// Explicit instantiation may name private members (C++ [temp.explicit]); this
// obtains the actual native track lists without changing class definitions/ODR,
// redefining private, copying layouts, or introducing a second occupancy engine.
struct TrackListsTag { friend auto native_track_lists(TrackListsTag); };
template<auto Member> struct ExposeTrackLists {
    friend auto native_track_lists(TrackListsTag) { return Member; }
};
template struct ExposeTrackLists<&CellOccupancy::track_lists>;
const auto& lists(const CellOccupancy& matrix, AbsoluteAxis axis) {
    return (matrix.*native_track_lists(TrackListsTag{}))(axis);
}
using NativeTrack = typename std::remove_cvref_t<decltype(lists(
    std::declval<const CellOccupancy&>(), AbsoluteAxis::Horizontal))>::value_type;

struct InvalidInput {};
void require(bool value) { if (!value) throw InvalidInput{}; }
AbsoluteAxis axis(int value) {
    require(value == 0 || value == 1);
    return value == 0 ? AbsoluteAxis::Horizontal : AbsoluteAxis::Vertical;
}
CellOccupancyState state(int value) {
    require(value >= 0 && value <= 2);
    return static_cast<CellOccupancyState>(value);
}
template<class T> void put(T* out, T value) { require(out != nullptr); *out = value; }
template<class T> std::span<const T> input(const T* data, std::size_t count) {
    require(count <= std::size_t(std::numeric_limits<std::ptrdiff_t>::max()) / sizeof(T));
    require(count == 0 || data != nullptr);
    return count ? std::span<const T>(data, count) : std::span<const T>{};
}
// Rust String accepts all valid UTF-8, including embedded NULs. Do not silently
// accept invalid UTF-8, overlong forms, surrogates or values above U+10FFFF.
std::string text(const char* data, std::size_t count) {
    const auto bytes = input(data, count);
    for (std::size_t i = 0; i < count;) {
        const auto lead = static_cast<unsigned char>(bytes[i++]);
        if (lead < 0x80) continue;
        unsigned remaining, code, minimum;
        if (lead >= 0xc2 && lead <= 0xdf) { remaining = 1; code = lead & 31; minimum = 0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { remaining = 2; code = lead & 15; minimum = 0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { remaining = 3; code = lead & 7; minimum = 0x10000; }
        else throw InvalidInput{};
        require(remaining <= count - i);
        while (remaining--) {
            const auto byte = static_cast<unsigned char>(bytes[i++]);
            require((byte & 0xc0) == 0x80); code = (code << 6) | (byte & 63);
        }
        require(code >= minimum && code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff));
    }
    return count ? std::string(data, count) : std::string{};
}

struct Track { NativeTrack native; };
struct Matrix { CellOccupancy native; };
using Object = std::variant<std::monostate, Track, Matrix, NamedLineResolver>;
struct Registry { std::uint64_t next = 0; std::unordered_map<std::uint64_t, Object> objects; std::mutex mutex; };
Registry& registry() { static Registry value; return value; }
template<class F> int boundary(F&& f) noexcept {
    try { f(); return 0; } catch (const InvalidInput&) { return 1; } catch (...) { return 2; }
}
template<class F> int handle(std::uint64_t id, F&& f) noexcept {
    return boundary([&] {
        auto& r = registry(); std::lock_guard lock(r.mutex);
        auto it = r.objects.find(id);
        require(it != r.objects.end() && !std::holds_alternative<std::monostate>(it->second));
        try { f(it->second); }
        catch (const InvalidInput&) { throw; }
        catch (...) { it->second.emplace<std::monostate>(); throw; }
    });
}
template<class T> T& object(Object& value) {
    auto* result = std::get_if<T>(&value); require(result != nullptr); return *result;
}
template<class T> void add(T value, std::uint64_t* out) {
    require(out != nullptr);
    auto& r = registry(); std::lock_guard lock(r.mutex);
    if (r.next == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("handle exhaustion");
    const auto id = ++r.next; r.objects.emplace(id, std::move(value)); *out = id;
}
PsOnOption optional(std::optional<std::int32_t> value) { return {value ? 1 : 0, value.value_or(0)}; }
const NativeTrack& track(Object& value, int ax, std::size_t index) {
    if (auto* t = std::get_if<Track>(&value)) return t->native; // Rust ignores axis/index for Track.
    return lists(object<Matrix>(value).native, axis(ax)).at(index); // Native panic, not invalid ABI.
}
GridPlacement placement(PsOnPlacement p) {
    switch (p.kind) {
    case 0: return GridPlacement::auto_();
    case 1: case 3:
        require(p.value >= -32768 && p.value <= 32767);
        return p.kind == 1 ? GridPlacement::line(static_cast<std::int16_t>(p.value))
                           : GridPlacement::named_line(text(p.text, p.len), static_cast<std::int16_t>(p.value));
    case 2: case 4:
        require(p.value >= 0 && p.value <= 65535);
        return p.kind == 2 ? GridPlacement::span(static_cast<std::uint16_t>(p.value))
                           : GridPlacement::named_span(text(p.text, p.len), static_cast<std::uint16_t>(p.value));
    default: throw InvalidInput{};
    }
}
PsOnPlacement encoded(const GridPlacement& p) {
    switch (p.kind()) {
    case GridPlacement::Kind::Auto: return {0,0,nullptr,0};
    case GridPlacement::Kind::Line: return {1,p.line_index(),nullptr,0};
    case GridPlacement::Kind::Span: return {2,p.span_count(),nullptr,0};
    default: throw std::logic_error("native resolver returned a named placement");
    }
}
} // namespace

extern "C" {
std::int32_t ps_on_track_new(std::uint64_t* out) { return boundary([&] { add(Track{}, out); }); }
std::int32_t ps_on_drop(std::uint64_t id) {
    return boundary([&] { auto& r = registry(); std::lock_guard lock(r.mutex); require(r.objects.erase(id) != 0); });
}
std::int32_t ps_on_track_paint(std::uint64_t id, std::int16_t a, std::int16_t b, std::int32_t st) {
    return handle(id, [&](Object& o) {
        const auto s = state(st); auto& t = object<Track>(o);
        t.native.paint({a,b}, s);
    });
}
std::int32_t ps_on_track_state(std::uint64_t id, std::int16_t at, std::int32_t* out) {
    return handle(id, [&](Object& o) {
        const auto& t = object<Track>(o).native;
        put(out, static_cast<std::int32_t>(t.state_at(at)));
    });
}
std::int32_t ps_on_track_collision(std::uint64_t id, std::int16_t a, std::int16_t b, PsOnOption* out) {
    return handle(id, [&](Object& o) { put(out, optional(object<Track>(o).native.collision_extent({a,b}))); });
}
std::int32_t ps_on_intervals_info(std::uint64_t id, std::int32_t ax, std::size_t index, std::size_t* count, std::int32_t* spilled) {
    return handle(id, [&](Object& o) {
        require(count && spilled); const auto& t = track(o, ax, index);
        // Report actual native storage: vector capacity or ordered-index promotion.
        // Do not project Rust's SmallVec allocation history onto either representation.
        std::visit([&](const auto& intervals) {
            *count = intervals.size();
            if constexpr (requires { intervals.capacity(); })
                *spilled = intervals.capacity() > 2 ? 1 : 0;
            else
                *spilled = 1;
        }, t.intervals);
    });
}
std::int32_t ps_on_interval_get(std::uint64_t id, std::int32_t ax, std::size_t index, std::size_t interval, PsOnInterval* out) {
    return handle(id, [&](Object& o) {
        const auto& t = track(o, ax, index);
        std::visit([&](const auto& intervals) {
            require(interval < intervals.size());
            const auto& entry = *std::next(intervals.begin(), static_cast<std::ptrdiff_t>(interval));
            const auto& i = [&]() -> const auto& {
                if constexpr (requires { entry.second; }) return entry.second;
                else return entry;
            }();
            put(out, PsOnInterval{static_cast<std::int16_t>(i.range.start), static_cast<std::int16_t>(i.range.end), static_cast<std::int32_t>(i.state)});
        }, t.intervals);
    });
}
std::int32_t ps_on_matrix_new(PsOnCounts columns, PsOnCounts rows, std::uint64_t* out) {
    return boundary([&] {
        require(out != nullptr);
        add(Matrix{CellOccupancy::with_track_counts({columns.negative,columns.explicit_count,columns.positive},
                                                   {rows.negative,rows.explicit_count,rows.positive})}, out);
    });
}
std::int32_t ps_on_matrix_mark(std::uint64_t id, std::int32_t ax, std::int16_t a, std::int16_t b, std::int16_t c, std::int16_t d, std::int32_t st) {
    return handle(id, [&](Object& o) {
        const auto primary = axis(ax); const auto s = state(st); auto& m = object<Matrix>(o);
        m.native.mark_area_as(primary, {a,b}, {c,d}, s);
    });
}
std::int32_t ps_on_matrix_counts(std::uint64_t id, std::int32_t ax, PsOnCounts* out) {
    return handle(id, [&](Object& o) { const auto a = axis(ax); const auto& c = object<Matrix>(o).native.track_counts(a); put(out, PsOnCounts{c.negative_implicit,c.explicit_,c.positive_implicit}); });
}
std::int32_t ps_on_matrix_collision(std::uint64_t id, std::int32_t ax, std::int16_t a, std::int16_t b, std::int16_t c, std::int16_t d, PsOnOption* out) {
    return handle(id, [&](Object& o) { const auto x = axis(ax); put(out, optional(object<Matrix>(o).native.line_area_collision_jump(x,{a,b},{c,d}))); });
}
std::int32_t ps_on_matrix_unoccupied(std::uint64_t id, std::int32_t ax, std::int16_t a, std::int16_t b, std::int16_t c, std::int16_t d, std::int32_t* out) {
    return handle(id, [&](Object& o) { const auto x = axis(ax); put(out, std::int32_t(object<Matrix>(o).native.line_area_is_unoccupied(x,{a,b},{c,d}))); });
}
std::int32_t ps_on_matrix_cursor(std::uint64_t id, std::int32_t ax, std::int16_t at, PsOnOption* out) {
    return handle(id, [&](Object& o) { const auto x = axis(ax); put(out, optional(object<Matrix>(o).native.auto_placement_cursor(x,at))); });
}
std::int32_t ps_on_matrix_track_jump(std::uint64_t id, std::int32_t ax, std::int16_t a, std::int16_t b, PsOnOption* out) {
    return handle(id, [&](Object& o) { const auto x = axis(ax); put(out, optional(object<Matrix>(o).native.occupied_track_jump(x,{a,b}))); });
}
std::int32_t ps_on_matrix_occupied(std::uint64_t id, std::int32_t ax, std::size_t index, std::int32_t* out) {
    return handle(id, [&](Object& o) { const auto x = axis(ax); const auto& m = object<Matrix>(o).native; put(out, std::int32_t(x == AbsoluteAxis::Horizontal ? m.column_is_occupied(index) : m.row_is_occupied(index))); });
}
std::int32_t ps_on_named_new(const PsOnName* names, std::size_t name_count, const PsOnArea* areas, std::size_t area_count, std::uint16_t area_rows, std::uint16_t area_cols, std::int32_t explicit_rows, std::int32_t explicit_cols, std::uint32_t column_name_lines, std::uint32_t row_name_lines, std::uint64_t* out) {
    return boundary([&] {
        require(out && column_name_lines <= 65536 && row_name_lines <= 65536);
        require((explicit_rows == -1 || (explicit_rows >= 0 && explicit_rows <= 65535)) &&
                (explicit_cols == -1 || (explicit_cols >= 0 && explicit_cols <= 65535)));
        Style style;
        style.grid_template_column_names.resize(column_name_lines);
        style.grid_template_row_names.resize(row_name_lines);
        for (const auto& name : input(names,name_count)) {
            const auto x = axis(name.axis); require(name.line >= 1 && name.line <= 65536);
            auto& group = x == AbsoluteAxis::Horizontal ? style.grid_template_column_names : style.grid_template_row_names;
            group.resize(std::max(group.size(),std::size_t(name.line)));
            group[name.line-1].push_back(text(name.text,name.len));
        }
        if (area_count) {
            GridTemplateAreas result; result.row_count = area_rows; result.column_count = area_cols;
            for (const auto& area : input(areas,area_count))
                result.areas.push_back({text(area.text,area.len),area.row_start,area.row_end,area.column_start,area.column_end});
            style.grid_template_areas = std::move(result);
        }
        NamedLineResolver resolver(style,0,0);
        if (explicit_rows != -1) resolver.set_explicit_row_count(static_cast<std::uint16_t>(explicit_rows));
        if (explicit_cols != -1) resolver.set_explicit_column_count(static_cast<std::uint16_t>(explicit_cols));
        add(std::move(resolver),out);
    });
}
std::int32_t ps_on_named_resolve(std::uint64_t id, std::int32_t ax, PsOnPlacement start, PsOnPlacement end, PsOnPlacement* out_start, PsOnPlacement* out_end) {
    return handle(id, [&](Object& o) {
        require(out_start && out_end); const auto x = axis(ax);
        const Line<GridPlacement> p{placement(start),placement(end)};
        const auto result = object<NamedLineResolver>(o).resolve_line_names(p,x);
        *out_start = encoded(result.start); *out_end = encoded(result.end);
    });
}
std::int32_t ps_on_inject_panic(std::uint64_t id) {
    return handle(id, [](Object&) { throw std::runtime_error("injected occupancy/named panic"); });
}
std::int32_t ps_misc_balance(const float* sizes, std::size_t n, float limit, float gap, std::size_t min_lines, std::size_t* out, std::size_t cap, std::size_t* written) {
    return boundary([&] {
        require(n > 0 && n <= 10000 && !std::isnan(limit) && std::isfinite(gap));
        const auto values = input(sizes,n);
        require(std::none_of(values.begin(),values.end(),[](float x) { return std::isnan(x); }));
        const auto result = flex_detail::balance_lines(values,limit,gap,min_lines);
        require(out && cap >= result.size());
        std::copy(result.begin(),result.end(),out);
        put(written,result.size());
    });
}
} // extern "C"
