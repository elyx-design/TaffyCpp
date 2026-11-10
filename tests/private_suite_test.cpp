// Standalone Catch2 3.8.1 suite; no production C++ layout implementation.
#include "support/private_suite_oracle.h"
#include "ported/private/private_suite_grid.inc"
#include "ported/private/private_suite_occupancy_named.inc"
#include "ported/private/private_suite_misc.inc"

TEST_CASE("private suite rejects malformed operation requests", "[private-suite][adversarial]") {
    uint64_t style = 0;
    REQUIRE(ps_misc_style_new(0, &style) == 0);
    CHECK(ps_misc_style_property(style, 14, 65536.0f) == 1);
    CHECK(ps_misc_style_property(style, 14, std::numeric_limits<float>::infinity()) == 1);
    REQUIRE(ps_misc_style_drop(style) == 0);
    const int32_t parents[] = {-2};
    const uint32_t displays[] = {0};
    const float sizes[] = {10};
    float result[4] = {};
    CHECK(ps_misc_hidden(parents, displays, sizes, sizes, 1, 0, result, 4) == 1);
    PsGridStyle grid{};
    PsGridTrackDef track{{4, 0, reinterpret_cast<const void*>(uintptr_t(8))}, {1, 5, nullptr}};
    PsGridComponent component{0, 0, &track, 1};
    grid.columns = &component;
    grid.columns_len = 1;
    PsGridExplicit explicit_result{};
    CHECK(ps_grid_explicit(&grid, 0, 1, 100, 0, nullptr, &explicit_result) == 1);
    grid.columns = nullptr;
    grid.columns_len = 0;
    uint8_t occupied[] = {1};
    PsGridTrack tracks[5]{};
    size_t written = 0;
    CHECK(ps_grid_tracks(&grid, {0, 2, 0}, 0, 0, occupied, 1, tracks, 5, &written) == 1);
    occupied[0] = 2;
    CHECK(ps_grid_tracks(&grid, {0, 1, 0}, 0, 0, occupied, 1, tracks, 5, &written) == 1);
}
