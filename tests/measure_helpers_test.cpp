#include "support/helper_oracle.h"
#include "support/measure_helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace taffy::test {
namespace {

OracleLayoutInput request_input(OracleKnown known = {}, OracleScalar available = {4, 0}) {
    OracleLayoutInput input{};
    input.run_mode = 0;
    input.sizing_mode = 1;
    input.axis = 2;
    input.known_dimensions = known;
    input.available_space = {available, available};
    return input;
}

TestNodeContext context_for(const HelperOracleRequest& request) {
    switch (request.kind) {
    case 1:
        return TestNodeContext::zero();
    case 2:
        return TestNodeContext::fixed(request.width, request.height_ratio_or_height);
    case 3:
        return TestNodeContext::aspect_ratio(request.width, request.height_ratio_or_height);
    case 4:
        return TestNodeContext::ahem_text(
            request.text_length == 0
                ? std::string{}
                : std::string(reinterpret_cast<const char*>(request.text), request.text_length),
            request.vertical ? WritingMode::Vertical : WritingMode::Horizontal);
    default:
        return TestNodeContext::zero();
    }
}

void same_float(float actual, float expected) {
    if (std::isnan(expected))
        CHECK(std::isnan(actual));
    else
        CHECK(actual == expected);
}

void compare_one_request(const HelperOracleRequest& request) {
    INFO("run=" << request.input.run_mode << " sizing=" << request.input.sizing_mode
                << " kind=" << request.kind << " vertical=" << request.vertical << " bytes="
                << request.text_length << " known=" << request.input.known_dimensions.width.present
                << ',' << request.input.known_dimensions.height.present
                << " available=" << request.input.available_space.width.tag << ':'
                << request.input.available_space.width.value);
    HelperOracleResult expected{};
    REQUIRE(helper_oracle_measure(&request, &expected) == ORACLE_OK);
    auto context = context_for(request);
    const auto actual = test_measure_function(
        request.input, 0, request.kind == 0 ? nullptr : &context, RustBackend::default_style());
    same_float(actual.size.width, expected.size.width);
    same_float(actual.size.height, expected.size.height);
    CHECK(context.count == expected.count);
}

void compare_with_upstream(HelperOracleRequest request) {
    for (std::uint32_t run_mode = 0; run_mode != 2; ++run_mode) {
        for (std::uint32_t sizing_mode = 0; sizing_mode != 2; ++sizing_mode) {
            request.input.run_mode = run_mode;
            request.input.sizing_mode = sizing_mode;
            compare_one_request(request);
        }
    }
}

std::vector<OracleKnown> known_dimensions() {
    return {{},
            {{1, 25}, {}},
            {{}, {1, 35}},
            {{1, 25}, {1, 35}},
            {{1, 0}, {}},
            {{}, {1, 0}},
            {{1, 1000}, {}},
            {{1, std::numeric_limits<float>::infinity()}, {}},
            {{}, {1, std::numeric_limits<float>::infinity()}},
            {{1, std::numeric_limits<float>::quiet_NaN()}, {}},
            {{}, {1, std::numeric_limits<float>::quiet_NaN()}},
            {{1, -20}, {}},
            {{}, {1, -20}}};
}

void compare_text(const std::string& text, bool vertical) {
    for (const auto known : known_dimensions()) {
        for (const auto available :
             std::array<OracleScalar, 9>{{{3, 0},
                                          {4, 0},
                                          {1, 0},
                                          {1, -20},
                                          {1, 19},
                                          {1, 35},
                                          {1, 1000},
                                          {1, std::numeric_limits<float>::infinity()},
                                          {1, std::numeric_limits<float>::quiet_NaN()}}}) {
            HelperOracleRequest request{};
            request.kind = 4;
            request.text = reinterpret_cast<const std::uint8_t*>(text.data());
            request.text_length = text.size();
            request.vertical = vertical;
            request.input = request_input(known, available);
            compare_with_upstream(request);
        }
    }
}

} // namespace

TEST_CASE("Zero, fixed, aspect and absent context match upstream", "[measure_helpers]") {
    for (std::uint32_t kind = 0; kind != 4; ++kind) {
        for (const auto known : known_dimensions()) {
            HelperOracleRequest request{};
            request.kind = kind;
            request.width = 73;
            request.height_ratio_or_height = kind == 3 ? 0.375f : 41;
            request.input = request_input(known);
            compare_with_upstream(request);
        }
    }
}

TEST_CASE("Ahem wraps ZWS segments horizontally like upstream", "[measure_helpers]") {
    compare_text("abc\xE2\x80\x8B"
                 "de\xE2\x80\x8B"
                 "f",
                 false);
}

TEST_CASE("Ahem swaps inline and block axes vertically", "[measure_helpers]") {
    compare_text("abc\xE2\x80\x8B"
                 "de\xE2\x80\x8B"
                 "f",
                 true);
}

TEST_CASE("Ahem uses only the writing mode inline available axis", "[measure_helpers]") {
    const std::string text = "abc\xE2\x80\x8B"
                             "de\xE2\x80\x8B"
                             "f";
    for (std::uint32_t vertical = 0; vertical != 2; ++vertical) {
        for (const auto available : std::array<OracleSizing, 4>{{{{1, 19}, {1, 1000}},
                                                                 {{1, 1000}, {1, 19}},
                                                                 {{3, 0}, {4, 0}},
                                                                 {{4, 0}, {3, 0}}}}) {
            HelperOracleRequest request{};
            request.kind = 4;
            request.text = reinterpret_cast<const std::uint8_t*>(text.data());
            request.text_length = text.size();
            request.vertical = vertical;
            request.input = request_input();
            request.input.available_space = available;
            compare_with_upstream(request);
        }
    }
}

TEST_CASE("Ahem preserves empty and adjacent ZWS segments", "[measure_helpers]") {
    for (const auto& text :
         std::array<std::string, 4>{"", "\xE2\x80\x8B", "\xE2\x80\x8B\xE2\x80\x8B",
                                    "\xE2\x80\x8B"
                                    "a\xE2\x80\x8B\xE2\x80\x8B"}) {
        compare_text(text, false);
        compare_text(text, true);
    }
}

TEST_CASE("Ahem uses UTF8 byte lengths and preserves embedded NUL", "[measure_helpers]") {
    const std::string text = "\xC3\xA9\xE2\x80\x8B\xF0\x9F\x98\x80";
    compare_text(text, false);
    compare_text(text, true);
    compare_text(std::string("a\0b", 3), false);
}

TEST_CASE("Counts accumulate only when native leaf requests unknown intrinsic size",
          "[measure_helpers]") {
    auto context = TestNodeContext::fixed(73, 41);
    const auto style = RustBackend::default_style();
    auto input = request_input();
    (void)test_measure_function(input, 0, &context, style);
    const auto first = context.count;
    REQUIRE(first > 0);
    (void)test_measure_function(input, 0, &context, style);
    CHECK(context.count == 2 * first);
    input.known_dimensions = {{1, 25}, {1, 35}};
    // PerformLayout intentionally passes NONE to the intrinsic callback, even
    // with known outer dimensions. ComputeSize can skip the callback entirely.
    (void)test_measure_function(input, 0, &context, style);
    CHECK(context.count == 3 * first);
    input.run_mode = 1;
    (void)test_measure_function(input, 0, &context, style);
    CHECK(context.count == 3 * first);
}

TEST_CASE("Native leaf resolves padding and border around translated intrinsic size",
          "[measure_helpers]") {
    auto style = RustBackend::default_style();
    style.padding = {{1, 2}, {1, 3}, {1, 4}, {1, 5}};
    style.border = {{1, 1}, {1, 1}, {1, 1}, {1, 1}};
    const auto input = request_input();
    auto context = TestNodeContext::fixed(73, 41);
    const auto actual = test_measure_function(input, 0, &context, style);
    const auto native = RustBackend::leaf(
        style, input, [](OracleKnown, OracleSizing) { return OracleSize{73, 41}; });
    CHECK(actual.size.width == native.size.width);
    CHECK(actual.size.height == native.size.height);
    REQUIRE(context.count > 0);
}

TEST_CASE("Translated helper is usable as a RustBackend node callback", "[measure_helpers]") {
    RustBackend tree;
    tree.rounding(false);
    auto context = TestNodeContext::aspect_ratio(73, 0.5f);
    const auto node =
        tree.new_node(RustBackend::default_style(),
                      [&context](RustBackend::NodeId id, const OracleLayoutInput& input,
                                 const OracleStyle& style) {
                          return test_measure_function(input, id, &context, style);
                      });
    tree.compute(node, {{4, 0}, {4, 0}});
    const auto layout = tree.layout(node);
    CHECK(layout.size.width == 73);
    CHECK(layout.size.height == 36.5f);
    REQUIRE(context.count > 0);
    const auto count = context.count;
    tree.compute(node, {{4, 0}, {4, 0}});
    CHECK(context.count == count);
}

} // namespace taffy::test
