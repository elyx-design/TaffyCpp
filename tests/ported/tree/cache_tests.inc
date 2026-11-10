// Complete operation/assertion ports of reference/upstream/src/tree/cache.rs.
// This include owns no Catch main and uses only the primitive native cache ABI.
#include "cache_abi.h"
#include <array>
#include <cstdint>
#include <vector>

namespace tree_cache_suite {
constexpr unsigned cache_size = 9;

struct Cache {
    TsCache* handle = nullptr;
    Cache() { REQUIRE(ts_cache_new(&handle) == TS_CACHE_OK); }
    ~Cache() { if (handle) ts_cache_free(handle); }
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;
    void store(const TsCacheInput& i, const TsCacheOutput& o) {
        REQUIRE(ts_cache_store(handle, &i, &o) == TS_CACHE_OK);
    }
    TsCacheState state() const {
        TsCacheState s{};
        REQUIRE(ts_cache_state(handle, &s) == TS_CACHE_OK);
        return s;
    }
};
struct Snapshot {
    TsCacheSnapshot* handle = nullptr;
    explicit Snapshot(const Cache& c) { REQUIRE(ts_cache_snapshot(c.handle, &handle) == TS_CACHE_OK); }
    ~Snapshot() { if (handle) ts_cache_snapshot_free(handle); }
    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;
};
TsCacheInput input(float width) {
    TsCacheInput i{};
    i.run_mode = TS_CACHE_MEASURE;
    i.sizing_mode = TS_CACHE_INHERENT_SIZE;
    i.axis = TS_CACHE_BOTH;
    i.known_dimensions[0] = {width, 1};
    i.known_dimensions_are_definite[0] = 1;
    i.known_dimensions_are_definite[1] = 1;
    i.available_space[0].kind = TS_CACHE_MAX_CONTENT;
    i.available_space[1].kind = TS_CACHE_MAX_CONTENT;
    return i;
}
TsCacheOutput output(float width) {
    TsCacheOutput o{};
    REQUIRE(ts_cache_output_from_outer_size(width, width, &o) == TS_CACHE_OK);
    return o;
}
TsCacheInput perform_layout_input(float width, float height) {
    auto i = input(width);
    i.run_mode = TS_CACHE_LAYOUT;
    i.known_dimensions[1] = {height, 1};
    i.parent_size[0] = {width, 1};
    i.parent_size[1] = {height, 1};
    return i;
}
std::array<TsCacheInput, 4> other_modes(TsCacheInput i) {
    std::array<TsCacheInput, 4> others{{i, i, i, i}};
    others[0].sizing_mode = TS_CACHE_CONTENT_SIZE;
    others[1].vertical_margins_are_collapsible[0] = 1;
    others[1].vertical_margins_are_collapsible[1] = 1;
    others[2].vertical_margins_are_collapsible[0] = 1;
    others[3].vertical_margins_are_collapsible[1] = 1;
    return others;
}
void equal_optional(const TsCacheOptional& actual, const TsCacheOptional& expected) {
    REQUIRE(actual.present == expected.present);
    if (expected.present) REQUIRE(actual.value == expected.value);
}
void equal_output(const TsCacheOutput& actual, const TsCacheOutput& expected) {
    for (unsigned n = 0; n < 2; ++n) {
        REQUIRE(actual.size[n] == expected.size[n]);
        equal_optional(actual.baselines[n], expected.baselines[n]);
        REQUIRE(actual.top_margin[n] == expected.top_margin[n]);
        REQUIRE(actual.bottom_margin[n] == expected.bottom_margin[n]);
    }
    for (unsigned n = 0; n < 4; ++n)
        REQUIRE(actual.scrollable_overflow_rect[n] == expected.scrollable_overflow_rect[n]);
    REQUIRE(actual.margins_can_collapse_through == expected.margins_can_collapse_through);
    REQUIRE(actual.oof_count == expected.oof_count);
    for (std::uint64_t n = 0; n < expected.oof_count; ++n) {
        const auto& a = actual.oof_candidates[n];
        const auto& e = expected.oof_candidates[n];
        REQUIRE(a.node == e.node);
        REQUIRE(a.order == e.order);
        REQUIRE(a.position == e.position);
        for (unsigned axis = 0; axis < 2; ++axis) {
            REQUIRE(a.static_position[axis].area_start == e.static_position[axis].area_start);
            REQUIRE(a.static_position[axis].area_end == e.static_position[axis].area_end);
            REQUIRE(a.static_position[axis].keyword == e.static_position[axis].keyword);
            REQUIRE(a.static_position[axis].safety == e.static_position[axis].safety);
            REQUIRE(a.static_position[axis].fallback == e.static_position[axis].fallback);
        }
    }
    REQUIRE(actual.oof_positioning_area_present == expected.oof_positioning_area_present);
    if (expected.oof_positioning_area_present) {
        for (unsigned n = 0; n < 2; ++n) {
            REQUIRE(actual.oof_positioning_size[n] == expected.oof_positioning_size[n]);
            REQUIRE(actual.oof_positioning_offset[n] == expected.oof_positioning_offset[n]);
        }
    }
}
void hit(Cache& cache, const TsCacheInput& i, const TsCacheOutput& expected) {
    TsCacheOutput actual{};
    std::vector<TsCacheCandidate> candidates(static_cast<std::size_t>(expected.oof_count));
    actual.oof_candidates = candidates.data();
    actual.oof_capacity = candidates.size();
    std::uint32_t found = 0;
    REQUIRE(ts_cache_get(cache.handle, &i, &actual, &found) == TS_CACHE_OK);
    REQUIRE(found == 1);
    equal_output(actual, expected);
}
void miss(Cache& cache, const TsCacheInput& i) {
    TsCacheOutput actual{};
    std::uint32_t found = 1;
    REQUIRE(ts_cache_get(cache.handle, &i, &actual, &found) == TS_CACHE_OK);
    REQUIRE(found == 0);
}
} // namespace tree_cache_suite

TEST_CASE("tree::cache::tests::recently_used_measure_entries_get_a_second_chance", "[tree-suite][cache]") {
    using namespace tree_cache_suite;
    Cache cache;
    for (unsigned width = 0; width < cache_size; ++width)
        cache.store(input(static_cast<float>(width)), output(static_cast<float>(width)));
    cache.store(input(static_cast<float>(cache_size)), output(static_cast<float>(cache_size)));
    hit(cache, input(1.0f), output(1.0f));
    cache.store(input(static_cast<float>(cache_size + 1)), output(static_cast<float>(cache_size + 1)));
    hit(cache, input(1.0f), output(1.0f));
    miss(cache, input(2.0f));
}

TEST_CASE("tree::cache::tests::storing_an_existing_measurement_updates_it_in_place", "[tree-suite][cache]") {
    using namespace tree_cache_suite;
    Cache cache;
    cache.store(input(1.0f), output(1.0f));
    cache.store(input(2.0f), output(2.0f));
    cache.store(input(1.0f), output(3.0f));
    REQUIRE(cache.state().measure_count == 2);
    hit(cache, input(1.0f), output(3.0f));
}

TEST_CASE("tree::cache::tests::measurements_with_margin_collapse_metadata_are_not_cached", "[tree-suite][cache]") {
    using namespace tree_cache_suite;
    Cache cache;
    auto collapse_through = output(1.0f);
    collapse_through.margins_can_collapse_through = 1;
    cache.store(input(1.0f), collapse_through);
    miss(cache, input(1.0f));
    auto carried_margin = output(2.0f);
    REQUIRE(ts_cache_margin_from_margin(10.0f, carried_margin.top_margin) == TS_CACHE_OK);
    cache.store(input(2.0f), carried_margin);
    miss(cache, input(2.0f));
}

TEST_CASE("tree::cache::tests::retrieving_a_measurement_only_marks_its_slot_as_used", "[tree-suite][cache]") {
    using namespace tree_cache_suite;
    Cache cache;
    cache.store(input(1.0f), output(1.0f));
    cache.store(input(2.0f), output(2.0f));
    REQUIRE(ts_cache_set_recently_used(cache.handle, 0) == TS_CACHE_OK);
    Snapshot entries(cache);
    hit(cache, input(1.0f), output(1.0f));
    std::uint32_t equal = 0;
    REQUIRE(ts_cache_measure_entries_equal(cache.handle, entries.handle, &equal) == TS_CACHE_OK);
    REQUIRE(equal == 1);
    REQUIRE(cache.state().recently_used != 0);
}

TEST_CASE("tree::cache::tests::cache_mode_distinguishes_every_combination_of_inputs", "[tree-suite][cache]") {
    using namespace tree_cache_suite;
    std::array<std::uint32_t, 8> modes{};
    for (unsigned index = 0; index < modes.size(); ++index) {
        auto i = input(1.0f);
        i.sizing_mode = (index & 1) == 0 ? TS_CACHE_CONTENT_SIZE : TS_CACHE_INHERENT_SIZE;
        i.vertical_margins_are_collapsible[0] = (index & 2) != 0;
        i.vertical_margins_are_collapsible[1] = (index & 4) != 0;
        REQUIRE(ts_cache_mode_bits(&i, &modes[index]) == TS_CACHE_OK);
    }
    for (unsigned index = 0; index < modes.size(); ++index)
        for (unsigned earlier = 0; earlier < index; ++earlier)
            REQUIRE(modes[earlier] != modes[index]);
}

TEST_CASE("tree::cache::tests::retrieving_with_a_different_mode_misses_and_keeps_the_entries", "[tree-suite][cache]") {
    using namespace tree_cache_suite;
    Cache cache;
    cache.store(perform_layout_input(100.0f, 50.0f), output(1.0f));
    cache.store(input(1.0f), output(2.0f));
    for (const auto& other : other_modes(perform_layout_input(100.0f, 50.0f))) miss(cache, other);
    for (const auto& other : other_modes(input(1.0f))) miss(cache, other);
    hit(cache, perform_layout_input(100.0f, 50.0f), output(1.0f));
    hit(cache, input(1.0f), output(2.0f));
}

TEST_CASE("tree::cache::tests::storing_with_a_different_mode_drops_the_existing_entries", "[tree-suite][cache]") {
    using namespace tree_cache_suite;
    for (const auto& other : other_modes(input(3.0f))) {
        Cache cache;
        cache.store(perform_layout_input(100.0f, 50.0f), output(1.0f));
        cache.store(input(1.0f), output(2.0f));
        cache.store(other, output(3.0f));
        hit(cache, other, output(3.0f));
        REQUIRE(cache.state().has_final == 0);
        REQUIRE(cache.state().measure_count == 1);
        cache.store(input(4.0f), output(4.0f));
        miss(cache, perform_layout_input(100.0f, 50.0f));
        miss(cache, input(1.0f));
        miss(cache, other);
        hit(cache, input(4.0f), output(4.0f));
    }
    for (const auto& other : other_modes(perform_layout_input(100.0f, 50.0f))) {
        Cache cache;
        cache.store(perform_layout_input(100.0f, 50.0f), output(1.0f));
        cache.store(input(1.0f), output(2.0f));
        cache.store(other, output(3.0f));
        hit(cache, other, output(3.0f));
        REQUIRE(cache.state().measure_count == 0); // all entries are None
        miss(cache, perform_layout_input(100.0f, 50.0f));
    }
}

TEST_CASE("tree::cache::tests::hidden_layouts_do_not_affect_the_cache", "[tree-suite][cache]") {
    using namespace tree_cache_suite;
    Cache cache;
    cache.store(input(1.0f), output(1.0f));
    TsCacheInput hidden{};
    REQUIRE(ts_cache_hidden_input(&hidden) == TS_CACHE_OK);
    cache.store(hidden, output(2.0f));
    hit(cache, input(1.0f), output(1.0f));
}

TEST_CASE("tree::cache::tests::mode_change_evictions_are_counted_for_non_empty_caches_only", "[tree-suite][cache]") {
    using namespace tree_cache_suite;
    auto content_size = input(1.0f);
    content_size.sizing_mode = TS_CACHE_CONTENT_SIZE;
    std::uint64_t evictions = 0;
    const auto status = ts_cache_mode_change_evictions(&evictions);
    if (status == TS_CACHE_UNAVAILABLE)
        SKIP("Upstream eviction counter requires debug_assertions and feature=std");
    REQUIRE(status == TS_CACHE_OK);
    Cache cache;
    cache.store(content_size, output(1.0f));
    auto content_size_2 = input(2.0f);
    content_size_2.sizing_mode = TS_CACHE_CONTENT_SIZE;
    cache.store(content_size_2, output(2.0f));
    std::uint64_t current = 0;
    REQUIRE(ts_cache_mode_change_evictions(&current) == TS_CACHE_OK);
    REQUIRE(current == evictions);
    cache.store(input(1.0f), output(1.0f));
    REQUIRE(ts_cache_mode_change_evictions(&current) == TS_CACHE_OK);
    REQUIRE(current == evictions + 1);
    std::uint32_t cleared = 0;
    REQUIRE(ts_cache_clear(cache.handle, &cleared) == TS_CACHE_OK);
    cache.store(content_size, output(1.0f));
    REQUIRE(ts_cache_mode_change_evictions(&current) == TS_CACHE_OK);
    REQUIRE(current == evictions + 1);
}

// Additional transport check, not counted among the nine upstream scenario ports.
TEST_CASE("tree_suite::abi_full_layout_output_roundtrip", "[tree-suite][cache][abi]") {
    using namespace tree_cache_suite;
    Cache cache;
    auto i = perform_layout_input(100.0f, 50.0f);
    i.axis = TS_CACHE_VERTICAL;
    i.known_dimensions_are_definite[1] = 0;
    i.available_space[0] = {21.0f, TS_CACHE_DEFINITE};
    i.available_space[1] = {0.0f, TS_CACHE_MIN_CONTENT};
    i.vertical_margins_are_collapsible[1] = 1;
    auto o = output(3.0f);
    o.size[1] = 7.0f;
    for (unsigned n = 0; n < 4; ++n) o.scrollable_overflow_rect[n] = static_cast<float>(n + 1);
    o.baselines[0] = {2.0f, 1};
    o.baselines[1] = {6.0f, 1};
    o.top_margin[0] = 10.0f; o.top_margin[1] = -4.0f;
    o.bottom_margin[0] = 8.0f; o.bottom_margin[1] = -3.0f;
    o.margins_can_collapse_through = 1;
    o.oof_positioning_area_present = 1;
    o.oof_positioning_size[0] = 98.0f; o.oof_positioning_size[1] = 47.0f;
    o.oof_positioning_offset[0] = 1.0f; o.oof_positioning_offset[1] = 2.0f;
    TsCacheCandidate candidate{};
    candidate.node = 42; candidate.order = 7; candidate.position = TS_CACHE_FIXED;
    candidate.static_position[0] = {2.0f, 5.0f, TS_CACHE_CENTER, TS_CACHE_SAFE, TS_CACHE_START};
    candidate.static_position[1] = {3.0f, 9.0f, TS_CACHE_END, TS_CACHE_UNSAFE, TS_CACHE_CENTER};
    o.oof_candidates = &candidate; o.oof_count = 1; o.oof_capacity = 1;
    cache.store(i, o);
    hit(cache, i, o);
    auto changed = i;
    changed.known_dimensions_are_definite[1] = 1;
    miss(cache, changed);
    changed = i; changed.axis = TS_CACHE_HORIZONTAL;
    miss(cache, changed);
}
