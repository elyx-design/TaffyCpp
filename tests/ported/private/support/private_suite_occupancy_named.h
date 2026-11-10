#pragma once
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

extern "C" {
struct PsOnInterval { std::int16_t start, end; std::int32_t state; };
struct PsOnOption { std::int32_t present, value; };
struct PsOnCounts { std::uint16_t negative, explicit_count, positive; };
struct PsOnName { std::int32_t axis; std::uint32_t line; const char* text; std::size_t len; };
struct PsOnArea { const char* text; std::size_t len; std::uint16_t row_start, row_end, column_start, column_end; };
struct PsOnPlacement { std::int32_t kind, value; const char* text; std::size_t len; };
std::int32_t ps_on_track_new(std::uint64_t*);
std::int32_t ps_on_drop(std::uint64_t);
std::int32_t ps_on_track_paint(std::uint64_t, std::int16_t, std::int16_t, std::int32_t);
std::int32_t ps_on_track_state(std::uint64_t, std::int16_t, std::int32_t*);
std::int32_t ps_on_track_collision(std::uint64_t, std::int16_t, std::int16_t, PsOnOption*);
std::int32_t ps_on_intervals_info(std::uint64_t, std::int32_t, std::size_t, std::size_t*, std::int32_t*);
std::int32_t ps_on_interval_get(std::uint64_t, std::int32_t, std::size_t, std::size_t, PsOnInterval*);
std::int32_t ps_on_matrix_new(PsOnCounts, PsOnCounts, std::uint64_t*);
std::int32_t ps_on_matrix_mark(std::uint64_t, std::int32_t, std::int16_t, std::int16_t, std::int16_t, std::int16_t, std::int32_t);
std::int32_t ps_on_matrix_counts(std::uint64_t, std::int32_t, PsOnCounts*);
std::int32_t ps_on_matrix_collision(std::uint64_t, std::int32_t, std::int16_t, std::int16_t, std::int16_t, std::int16_t, PsOnOption*);
std::int32_t ps_on_matrix_unoccupied(std::uint64_t, std::int32_t, std::int16_t, std::int16_t, std::int16_t, std::int16_t, std::int32_t*);
std::int32_t ps_on_matrix_cursor(std::uint64_t, std::int32_t, std::int16_t, PsOnOption*);
std::int32_t ps_on_matrix_track_jump(std::uint64_t, std::int32_t, std::int16_t, std::int16_t, PsOnOption*);
std::int32_t ps_on_matrix_occupied(std::uint64_t, std::int32_t, std::size_t, std::int32_t*);
std::int32_t ps_on_named_new(const PsOnName*, std::size_t, const PsOnArea*, std::size_t, std::uint16_t, std::uint16_t, std::int32_t, std::int32_t, std::uint32_t, std::uint32_t, std::uint64_t*);
std::int32_t ps_on_named_resolve(std::uint64_t, std::int32_t, PsOnPlacement, PsOnPlacement, PsOnPlacement*, PsOnPlacement*);
// Test hook: returns 2 and permanently invalidates the target; drop still works.
std::int32_t ps_on_inject_panic(std::uint64_t);
}
inline bool operator==(const PsOnInterval& a, const PsOnInterval& b) { return a.start == b.start && a.end == b.end && a.state == b.state; }
inline bool operator==(const PsOnCounts& a, const PsOnCounts& b) { return a.negative == b.negative && a.explicit_count == b.explicit_count && a.positive == b.positive; }
namespace ps_on {
constexpr int Horizontal = 0, Vertical = 1, Unoccupied = 0, DefinitelyPlaced = 1, AutoPlaced = 2;
inline void ok(int status) { INFO("ps_on native status (0=ok,1=invalid,2=panic): " << status); REQUIRE(status == 0); }
inline std::optional<int> option(PsOnOption p) { REQUIRE((p.present == 0 || p.present == 1)); return p.present ? std::optional<int>(p.value) : std::nullopt; }
struct Owned {
    std::uint64_t id = 0;
    Owned() = default;
    Owned(const Owned&) = delete;
    Owned& operator=(const Owned&) = delete;
    Owned(Owned&& o) noexcept : id(std::exchange(o.id, 0)) {}
    ~Owned() { if (id && ps_on_drop(id) != 0) std::abort(); }
    std::vector<PsOnInterval> intervals(int axis = 0, std::size_t index = 0) const {
        std::size_t n = 0; int spilled = 0; ok(ps_on_intervals_info(id, axis, index, &n, &spilled));
        std::vector<PsOnInterval> result(n);
        for (std::size_t i = 0; i < n; ++i) ok(ps_on_interval_get(id, axis, index, i, &result[i]));
        return result;
    }
    bool spilled(int axis = 0, std::size_t index = 0) const { std::size_t n = 0; int s = 0; ok(ps_on_intervals_info(id,axis,index,&n,&s)); return s != 0; }
};
struct Track : Owned {
    Track() { ok(ps_on_track_new(&id)); }
    void paint(int a, int b, int state) { ok(ps_on_track_paint(id,a,b,state)); }
    int state_at(int at) const { int s = -1; ok(ps_on_track_state(id,at,&s)); return s; }
    std::optional<int> collision(int a, int b) const { PsOnOption p{}; ok(ps_on_track_collision(id,a,b,&p)); return option(p); }
};
struct Matrix : Owned {
    Matrix(PsOnCounts columns, PsOnCounts rows) { ok(ps_on_matrix_new(columns,rows,&id)); }
    void mark(int axis, int a, int b, int c, int d, int state = AutoPlaced) { ok(ps_on_matrix_mark(id,axis,a,b,c,d,state)); }
    PsOnCounts counts(int axis) const { PsOnCounts c{}; ok(ps_on_matrix_counts(id,axis,&c)); return c; }
    bool unoccupied(int axis, int a, int b, int c, int d) const { int r = -1; ok(ps_on_matrix_unoccupied(id,axis,a,b,c,d,&r)); return r != 0; }
    std::optional<int> collision(int axis, int a, int b, int c, int d) const { PsOnOption p{}; ok(ps_on_matrix_collision(id,axis,a,b,c,d,&p)); return option(p); }
    std::optional<int> cursor(int axis, int at) const { PsOnOption p{}; ok(ps_on_matrix_cursor(id,axis,at,&p)); return option(p); }
    std::optional<int> jump(int axis, int a, int b) const { PsOnOption p{}; ok(ps_on_matrix_track_jump(id,axis,a,b,&p)); return option(p); }
    bool occupied(int axis, std::size_t index) const { int r = -1; ok(ps_on_matrix_occupied(id,axis,index,&r)); return r != 0; }
};
inline PsOnPlacement automatic() { return {0,0,nullptr,0}; }
inline PsOnPlacement numeric(int n) { return {1,n,nullptr,0}; }
inline PsOnPlacement named(std::string_view s, int n) { return {3,n,s.data(),s.size()}; }
inline PsOnPlacement span(std::string_view s, int n) { return {4,n,s.data(),s.size()}; }
struct Named : Owned {
    Named(const std::vector<PsOnName>& names, const std::vector<PsOnArea>& areas, int area_rows, int area_cols, int rows, int cols, unsigned column_name_lines = 0, unsigned row_name_lines = 0) {
        ok(ps_on_named_new(names.data(),names.size(),areas.data(),areas.size(),area_rows,area_cols,rows,cols,column_name_lines,row_name_lines,&id));
    }
    std::pair<PsOnPlacement,PsOnPlacement> resolve(PsOnPlacement start, PsOnPlacement end, int axis = Horizontal) const {
        PsOnPlacement a{},b{}; ok(ps_on_named_resolve(id,axis,start,end,&a,&b)); return {a,b};
    }
    int start(std::string_view name, int n) const { auto p = resolve(named(name,n),automatic()).first; REQUIRE(p.kind == 1); return p.value; }
    int end(std::string_view name, int n) const { auto p = resolve(automatic(),named(name,n)).second; REQUIRE(p.kind == 1); return p.value; }
};
inline Named area_resolver(std::initializer_list<std::initializer_list<std::string_view>> line_names) {
    std::vector<PsOnName> names; unsigned line = 1;
    for (auto list : line_names) { for (auto name : list) names.push_back({Horizontal,line,name.data(),name.size()}); ++line; }
    REQUIRE(line == 6);
    return Named(names, {{"area",4,1,2,2,4}}, 1,4,1,4,5,0);
}
inline void line_is(PsOnPlacement p, int expected) { REQUIRE(p.kind == 1); REQUIRE(p.value == expected); }
// Expected-failure regressions must fail ONLY at the deliberately wrong value
// comparison. Abort on setup/status/type failure instead of letting the
// required-failure gate accept an unrelated assertion as fault detection.
inline int expected_failure_value(PsOnPlacement start, PsOnPlacement end, bool select_end = false, bool area = false) {
    Owned object;
    const PsOnArea native_area{"area",4,1,2,65535,65535};
    if (ps_on_named_new(nullptr,0,area ? &native_area : nullptr,area ? 1 : 0,
                        area ? 1 : 0,area ? 65535 : 0,-1,area ? -1 : 10000,0,0,&object.id) != 0)
        std::abort();
    PsOnPlacement a{}, b{};
    if (ps_on_named_resolve(object.id,Horizontal,start,end,&a,&b) != 0) std::abort();
    const auto result = select_end ? b : a;
    if (result.kind != 1) std::abort();
    return result.value;
}
} // namespace ps_on
