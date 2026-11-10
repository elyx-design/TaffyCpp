// Included by private_suite_test.cpp. Operations/assertions are owned by C++; Rust runs no tests.
#include "support/private_suite_occupancy_named.h"

TEST_CASE("private occupancy paint_merges_touching_same_state_intervals", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    Track t;
    t.paint(0,2,AutoPlaced); t.paint(4,6,AutoPlaced); t.paint(2,4,AutoPlaced);
    REQUIRE(t.intervals() == std::vector<PsOnInterval>{{0,6,AutoPlaced}});
}
TEST_CASE("private occupancy paint_does_not_merge_different_states", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    Track t;
    t.paint(0,2,AutoPlaced); t.paint(2,4,DefinitelyPlaced);
    REQUIRE(t.intervals() == std::vector<PsOnInterval>{{0,2,AutoPlaced},{2,4,DefinitelyPlaced}});
}
TEST_CASE("private occupancy paint_overwrites_overlapped_cells", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    Track t;
    t.paint(0,6,AutoPlaced); t.paint(2,4,DefinitelyPlaced);
    REQUIRE(t.intervals() == std::vector<PsOnInterval>{{0,2,AutoPlaced},{2,4,DefinitelyPlaced},{4,6,AutoPlaced}});
    t.paint(-1,7,AutoPlaced);
    REQUIRE(t.intervals() == std::vector<PsOnInterval>{{-1,7,AutoPlaced}});
}
TEST_CASE("private occupancy paint_overwrites_multiple_intervals", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    Track t;
    t.paint(0,2,AutoPlaced); t.paint(3,5,DefinitelyPlaced); t.paint(6,8,AutoPlaced); t.paint(1,7,DefinitelyPlaced);
    REQUIRE(t.intervals() == std::vector<PsOnInterval>{{0,1,AutoPlaced},{1,7,DefinitelyPlaced},{7,8,AutoPlaced}});
}
TEST_CASE("private occupancy collision_extent_finds_extremal_occupied_cell", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    Track t;
    t.paint(2,4,AutoPlaced); t.paint(6,8,DefinitelyPlaced);
    REQUIRE(t.collision(0,10) == std::optional<int>(7));
    REQUIRE(t.collision(0,7) == std::optional<int>(7));
    REQUIRE(t.collision(0,6) == std::optional<int>(3));
    REQUIRE(t.collision(4,6) == std::nullopt);
}
TEST_CASE("private occupancy state_at_distinguishes_occupied_intervals_and_gaps", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    Track t;
    t.paint(0,2,DefinitelyPlaced); t.paint(2,4,AutoPlaced); t.paint(6,8,AutoPlaced); t.paint(8,9,DefinitelyPlaced);
    REQUIRE(t.state_at(0) == DefinitelyPlaced);
    REQUIRE(t.state_at(3) == AutoPlaced);
    REQUIRE(t.state_at(5) == Unoccupied);
    REQUIRE(t.state_at(7) == AutoPlaced);
    REQUIRE(t.state_at(8) == DefinitelyPlaced);
}
TEST_CASE("private occupancy definitely_placed_overwrite_hides_auto_placed_cells", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    Track t;
    t.paint(0,4,AutoPlaced); t.paint(2,4,DefinitelyPlaced);
    REQUIRE(t.state_at(1) == AutoPlaced);
    REQUIRE(t.state_at(2) == DefinitelyPlaced);
    REQUIRE(t.state_at(3) == DefinitelyPlaced);
}
TEST_CASE("private occupancy negative_expansion_preserves_occupancy", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    Matrix m({0,2,0},{0,2,0});
    m.mark(Horizontal,0,1,0,1); m.mark(Horizontal,-2,-1,-1,0);
    REQUIRE(m.counts(Horizontal) == PsOnCounts{2,2,0});
    REQUIRE(m.counts(Vertical) == PsOnCounts{1,2,0});
    REQUIRE_FALSE(m.unoccupied(Horizontal,0,1,0,1));
    REQUIRE_FALSE(m.unoccupied(Horizontal,-2,-1,-1,0));
    REQUIRE(m.unoccupied(Horizontal,-1,0,0,1));
    REQUIRE(m.occupied(Horizontal,0));
    REQUIRE_FALSE(m.occupied(Horizontal,1));
    REQUIRE(m.occupied(Horizontal,2));
    REQUIRE(m.occupied(Vertical,0));
    REQUIRE(m.occupied(Vertical,1));
    REQUIRE_FALSE(m.occupied(Vertical,2));
}
TEST_CASE("private occupancy auto_placed_item_only_advances_cursor_in_first_secondary_track", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    for (int axis : {Horizontal,Vertical}) {
        Matrix m({0,4,0},{0,4,0}); m.mark(axis,1,3,1,4);
        REQUIRE(m.cursor(axis,1) == std::optional<int>(3));
        REQUIRE(m.cursor(axis,2) == std::nullopt);
        REQUIRE(m.cursor(axis,3) == std::nullopt);
        REQUIRE_FALSE(m.unoccupied(axis,1,3,2,4));
    }
}
TEST_CASE("private occupancy cursors_survive_interval_merging_and_grid_expansion", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    for (int axis : {Horizontal,Vertical}) {
        Matrix m({0,4,0},{0,4,0});
        m.mark(axis,2,4,1,2); m.mark(axis,4,8,0,2);
        m.mark(axis,8,10,1,2,DefinitelyPlaced); m.mark(axis,-3,-1,-2,-1);
        m.mark(axis,12,14,7,8); m.mark(axis,0,1,1,2);
        REQUIRE(m.cursor(axis,1) == std::optional<int>(4));
        REQUIRE(m.cursor(axis,0) == std::optional<int>(8));
        REQUIRE(m.cursor(axis,-2) == std::optional<int>(-1));
        REQUIRE(m.cursor(axis,7) == std::optional<int>(14));
        for (int track : {-3,-1,2,8}) REQUIRE(m.cursor(axis,track) == std::nullopt);
    }
}
TEST_CASE("private occupancy alternating_spans_merge_occupancy_and_skip_the_occupied_prefix", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    for (int axis : {Horizontal,Vertical}) {
        Matrix m({0,0,0},{0,0,0});
        for (int i = 0; i < 128; ++i) m.mark(axis,i,i+1,i%2,2);
        REQUIRE(m.intervals(1-axis,1) == std::vector<PsOnInterval>{{0,128,AutoPlaced}});
        REQUIRE_FALSE(m.spilled(1-axis,1));
        REQUIRE(m.collision(axis,0,1,1,2) == std::optional<int>(128));
    }
}
TEST_CASE("private occupancy collision_jump_returns_next_search_position", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    Matrix m({0,4,0},{0,4,0}); m.mark(Horizontal,1,3,0,1);
    REQUIRE(m.collision(Horizontal,0,2,0,1) == std::optional<int>(3));
    REQUIRE(m.collision(Horizontal,0,4,0,1) == std::optional<int>(3));
    REQUIRE(m.collision(Horizontal,0,4,1,2) == std::nullopt);
}
TEST_CASE("private occupancy occupied_track_jump_skips_non_empty_tracks", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    Matrix m({0,4,0},{0,4,0}); m.mark(Horizontal,0,1,1,2);
    REQUIRE(m.jump(Vertical,0,4) == std::optional<int>(2));
    REQUIRE(m.jump(Vertical,2,4) == std::nullopt);
}

TEST_CASE("private named extreme_missing_named_line_indices_do_not_overflow", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    Named n({}, {}, 0,0,-1,10000);
    REQUIRE(n.start("missing",32767) == 10001);
    REQUIRE(n.start("missing",-32768) == -20001);
}
TEST_CASE("private named large_named_span_does_not_wrap_negative", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    Named n({}, {}, 0,0,-1,10000);
    auto r = n.resolve(numeric(1),span("missing",65535));
    line_is(r.second,10001);
}
TEST_CASE("private named area_lines_saturate_when_converted_to_grid_lines", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    Named n({}, {{"area",4,1,2,65535,65535}}, 1,65535,-1,-1);
    REQUIRE(n.start("area-start",1) == 10001);
    REQUIRE(n.start("area",0) == 10001);
}
TEST_CASE("private named bare_ident_resolves_to_area_edge", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    auto n = area_resolver({{},{},{},{},{}});
    REQUIRE(n.start("area",0) == 2);
    REQUIRE(n.end("area",0) == 4);
    auto rows = n.resolve(named("area",0),named("area",0),Vertical);
    line_is(rows.first,1);
    line_is(rows.second,2);
}
TEST_CASE("private named bare_ident_resolves_to_edge_of_implicitly_named_area", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    auto n = area_resolver({{},{"foo-start"},{"foo-start"},{"foo-end"},{"foo-end"}});
    REQUIRE(n.start("foo",0) == 2);
    REQUIRE(n.end("foo",0) == 4);
}
TEST_CASE("private named area_edges_are_lines_named_with_start_and_end_suffixes", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    auto n = area_resolver({{},{},{},{},{}});
    REQUIRE(n.start("area-start",0) == 2);
    REQUIRE(n.start("area-start",1) == 2);
    REQUIRE(n.start("area-end",1) == 4);
    REQUIRE(n.start("area-end",-1) == 4);
    REQUIRE(n.start("area-start",2) == 6);
}
TEST_CASE("private named area_edges_are_merged_with_explicitly_named_lines", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    auto n = area_resolver({{"area-start"},{},{"area-start"},{"area-end"},{"area-end"}});
    REQUIRE(n.start("area-start",1) == 1);
    REQUIRE(n.start("area-start",2) == 2);
    REQUIRE(n.start("area-start",3) == 3);
    REQUIRE(n.start("area-start",-1) == 3);
    REQUIRE(n.start("area-start",4) == 6);
    REQUIRE(n.start("area-end",1) == 4);
    REQUIRE(n.start("area-end",2) == 5);
    REQUIRE(n.start("area-end",3) == 6);
    REQUIRE(n.start("area",0) == 1);
    REQUIRE(n.end("area",0) == 4);
    auto r = n.resolve(numeric(1),span("area-start",2));
    line_is(r.second,3);
}
TEST_CASE("private named area_edges_are_only_matched_by_bare_idents", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    auto n = area_resolver({{},{},{},{},{}});
    REQUIRE(n.start("area",1) == 6);
    REQUIRE(n.start("area",2) == 7);
    auto r = n.resolve(numeric(1),span("area",0));
    line_is(r.second,6);
}
TEST_CASE("private named area_edge_is_the_first_line_with_the_edge_name", "[private_suite][occupancy_named]") {
    using namespace ps_on;
    {
        auto n = area_resolver({{},{},{"area-start"},{},{"area-end"}});
        REQUIRE(n.start("area",0) == 2);
        REQUIRE(n.end("area",0) == 4);
        REQUIRE(n.start("area-start",1) == 2);
        REQUIRE(n.start("area-start",2) == 3);
        REQUIRE(n.start("area-end",2) == 5);
    }
    {
        auto n = area_resolver({{},{"area-start"},{},{"area-end"},{}});
        REQUIRE(n.start("area",0) == 2);
        REQUIRE(n.end("area",0) == 4);
        REQUIRE(n.start("area-start",1) == 2);
        REQUIRE(n.start("area-start",2) == 6);
        REQUIRE(n.start("area-end",2) == 6);
    }
}

TEST_CASE("private occupancy named ABI rejects invalid inputs distinctly", "[private_suite][occupancy_named][abi]") {
    using namespace ps_on;
    REQUIRE(ps_on_track_new(nullptr) == 1);
    REQUIRE(ps_on_drop(0) == 1);
    Track t;
    REQUIRE(ps_on_track_paint(t.id,0,1,99) == 1);
    int result = 0;
    REQUIRE(ps_on_track_state(t.id,0,nullptr) == 1);
    Matrix m({0,1,0},{0,1,0});
    REQUIRE(ps_on_matrix_occupied(m.id,2,0,&result) == 1);
    PsOnInterval interval{};
    REQUIRE(ps_on_interval_get(t.id,0,0,0,&interval) == 1);
    Named n({}, {},0,0,0,4);
    PsOnPlacement a{}, b{};
    REQUIRE(ps_on_named_resolve(n.id,0,{99,0,nullptr,0},automatic(),&a,&b) == 1);
    // Invalid UTF-8 is ABI validation failure, not a native resolution result.
    const char invalid[] = {static_cast<char>(0xff)};
    REQUIRE(ps_on_named_resolve(n.id,0,{3,1,invalid,1},automatic(),&a,&b) == 1);
    // Native track indexing retains its panic for an out-of-bounds matrix track.
    std::size_t count = 0; int spilled = 0;
    REQUIRE(ps_on_intervals_info(m.id,Horizontal,100,&count,&spilled) == 2);
    // The affected matrix is unusable; unrelated and fresh objects remain usable.
    REQUIRE(ps_on_matrix_occupied(m.id,Horizontal,0,&result) == 1);
    REQUIRE(n.start("missing",1) == 6);
    Matrix fresh({0,1,0},{0,1,0});
    REQUIRE_FALSE(fresh.occupied(Horizontal,0));
}

TEST_CASE("private occupancy named panic invalidates every object kind", "[private_suite][occupancy_named][abi]") {
    using namespace ps_on;
    Track track, unrelated_track;
    Matrix matrix({0,1,0},{0,1,0}), unrelated_matrix({0,1,0},{0,1,0});
    Named named_lines({}, {},0,0,0,4), unrelated_named({}, {},0,0,0,4);
    // Invalidate each kind independently and verify that no operation can reuse it.
    for (auto id : {track.id, matrix.id, named_lines.id}) {
        REQUIRE(ps_on_inject_panic(id) == 2);
        REQUIRE(ps_on_inject_panic(id) == 1);
        int result = -1;
        PsOnOption option{};
        PsOnCounts counts{};
        PsOnInterval interval{};
        PsOnPlacement a{}, b{};
        std::size_t count = 0; int spilled = 0;
        REQUIRE(ps_on_track_paint(id,0,1,AutoPlaced) == 1);
        REQUIRE(ps_on_track_state(id,0,&result) == 1);
        REQUIRE(ps_on_track_collision(id,0,1,&option) == 1);
        REQUIRE(ps_on_intervals_info(id,Horizontal,0,&count,&spilled) == 1);
        REQUIRE(ps_on_interval_get(id,Horizontal,0,0,&interval) == 1);
        REQUIRE(ps_on_matrix_mark(id,Horizontal,0,1,0,1,AutoPlaced) == 1);
        REQUIRE(ps_on_matrix_counts(id,Horizontal,&counts) == 1);
        REQUIRE(ps_on_matrix_collision(id,Horizontal,0,1,0,1,&option) == 1);
        REQUIRE(ps_on_matrix_unoccupied(id,Horizontal,0,1,0,1,&result) == 1);
        REQUIRE(ps_on_matrix_cursor(id,Horizontal,0,&option) == 1);
        REQUIRE(ps_on_matrix_track_jump(id,Horizontal,0,1,&option) == 1);
        REQUIRE(ps_on_matrix_occupied(id,Horizontal,0,&result) == 1);
        REQUIRE(ps_on_named_resolve(id,Horizontal,named("missing",1),automatic(),&a,&b) == 1);
        unrelated_track.paint(0,1,DefinitelyPlaced);
        REQUIRE(unrelated_track.state_at(0) == DefinitelyPlaced);
        unrelated_matrix.mark(Horizontal,0,1,0,1);
        REQUIRE_FALSE(unrelated_matrix.unoccupied(Horizontal,0,1,0,1));
        REQUIRE(unrelated_named.start("missing",1) == 6);
    }
    // Tombstones remain destructible exactly once; Owned must not drop twice.
    for (auto* object : {static_cast<Owned*>(&track), static_cast<Owned*>(&matrix), static_cast<Owned*>(&named_lines)}) {
        const auto id = std::exchange(object->id,0);
        REQUIRE(ps_on_drop(id) == 0);
        REQUIRE(ps_on_drop(id) == 1);
    }
    Track fresh_track;
    Matrix fresh_matrix({0,1,0},{0,1,0});
    Named fresh_named({}, {},0,0,0,4);
    REQUIRE(fresh_track.state_at(0) == Unoccupied);
    REQUIRE(fresh_matrix.unoccupied(Horizontal,0,1,0,1));
    REQUIRE(fresh_named.start("missing",1) == 6);
}

// Deliberately corrupt only C++ expectations. The runner's required-failure contract
// makes these regressions fail if the incorrect comparisons ever go undetected.
// Each case has only its injected comparison, so an unrelated earlier failed
// assertion cannot accidentally satisfy the expected-failure contract.
TEST_CASE("private named wrong extreme expected value is detected", "[private_suite][occupancy_named][failure_injection][.]") {
    using namespace ps_on;
    const int actual = expected_failure_value(named("missing",32767),automatic());
    REQUIRE(actual == 10002);
}
TEST_CASE("private named wrong negative expected value is detected", "[private_suite][occupancy_named][failure_injection][.]") {
    using namespace ps_on;
    const int actual = expected_failure_value(named("missing",-32768),automatic());
    REQUIRE(actual == -20002);
}
TEST_CASE("private named wrong span expected value is detected", "[private_suite][occupancy_named][failure_injection][.]") {
    using namespace ps_on;
    const int actual = expected_failure_value(numeric(1),span("missing",65535),true);
    REQUIRE(actual == 10002);
}
TEST_CASE("private named wrong area expected value is detected", "[private_suite][occupancy_named][failure_injection][.]") {
    using namespace ps_on;
    const int actual = expected_failure_value(named("area-start",1),automatic(),false,true);
    REQUIRE(actual == 10002);
}
