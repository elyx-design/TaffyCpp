#include "support/internal_oracle.h"
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>

namespace {
constexpr auto high = std::numeric_limits<std::int16_t>::max();
constexpr auto low = std::numeric_limits<std::int16_t>::min();

std::int16_t advance(std::int16_t position) {
    const InternalOracleAdvanceInput input{position};
    InternalOracleAdvanceOutput output{};
    REQUIRE(internal_oracle_advance_position(&input, &output) == INTERNAL_ORACLE_OK);
    return output.position;
}
InternalOracleSpanOutput resolve(std::int16_t position, std::uint16_t span) {
    const InternalOracleSpanInput input{position, span};
    InternalOracleSpanOutput output{};
    REQUIRE(internal_oracle_resolve_indefinite_grid_span(&input, &output) == INTERNAL_ORACLE_OK);
    return output;
}
} // namespace

// Exact source-test translations (not supplemental coverage):
// source-unit:
// src/compute/grid/placement.rs::tests::auto_placement_cursor_saturates_at_integer_bounds
TEST_CASE("auto_placement_cursor_saturates_at_integer_bounds", "[internal-oracle][source-unit]") {
    CHECK(advance(high) == high);
}

// source-unit: src/compute/grid/placement.rs::tests::indefinite_spans_saturate_at_integer_bounds
TEST_CASE("indefinite_spans_saturate_at_integer_bounds", "[internal-oracle][source-unit]") {
    const auto line = resolve(high, 1);
    CHECK(line.start == high);
    CHECK(line.end == high);
}

// Supplemental arithmetic/ABI tests: never count these as upstream source tests.
TEST_CASE("private grid helpers supplemental numeric edges", "[internal-oracle][supplemental]") {
    constexpr std::array<std::int16_t, 8> positions{low, low + 1,  -1,       0,
                                                    1,   high - 2, high - 1, high};
    constexpr std::array<std::uint16_t, 7> spans{0, 1, 2, 32767, 32768, 65534, 65535};
    for (const auto position : positions) {
        INFO("position=" << position);
        CHECK(advance(position) ==
              std::min(static_cast<int>(position) + 1, static_cast<int>(high)));
        for (const auto span : spans) {
            INFO("span=" << span);
            const auto line = resolve(position, span);
            CHECK(line.start == position);
            CHECK(line.end == std::clamp(static_cast<int>(position) + static_cast<int>(span),
                                         static_cast<int>(low), static_cast<int>(high)));
        }
    }
}

TEST_CASE("private grid helpers supplemental ABI errors", "[internal-oracle][supplemental]") {
    InternalOracleAdvanceOutput advance_output{42};
    CHECK(internal_oracle_advance_position(nullptr, &advance_output) ==
          INTERNAL_ORACLE_INVALID_ARGUMENT);
    CHECK(advance_output.position == 0);
    const InternalOracleAdvanceInput advance_input{0};
    CHECK(internal_oracle_advance_position(&advance_input, nullptr) ==
          INTERNAL_ORACLE_INVALID_ARGUMENT);
    InternalOracleSpanOutput span_output{42, 43};
    CHECK(internal_oracle_resolve_indefinite_grid_span(nullptr, &span_output) ==
          INTERNAL_ORACLE_INVALID_ARGUMENT);
    CHECK(span_output.start == 0);
    CHECK(span_output.end == 0);
    const InternalOracleSpanInput span_input{0, 0};
    CHECK(internal_oracle_resolve_indefinite_grid_span(&span_input, nullptr) ==
          INTERNAL_ORACLE_INVALID_ARGUMENT);
}
