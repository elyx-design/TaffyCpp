#ifndef TAFFY_TEST_BEHAVIOR_GRID_NATIVE_H
#define TAFFY_TEST_BEHAVIOR_GRID_NATIVE_H

#include "../support/behavior_suite_oracle.h"
#include "ahem_intrinsic.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <initializer_list>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

// Transport/lifetime helpers plus raw translated Ahem intrinsic measurement.
// Layout, grid resolution, track names and serialization execute natively.
namespace grid_behavior {
inline BehaviorText text(std::string_view value) { return {value.data(), value.size()}; }
inline BehaviorScalar definite(float value) { return {1, value}; }
inline constexpr BehaviorScalar max_content{4, 0};
using Node = std::uint64_t;
using Property = std::pair<std::string_view, std::string_view>;
using Names = std::vector<std::string>;
// Placement syntax is parsed by native GridPlacement::from_str, not resolved here.
inline std::string automatic() { return "auto"; }
inline std::string line(int index) { return std::to_string(index); }
inline std::string span(unsigned count) { return "span " + std::to_string(count); }
inline std::string named(std::string_view name, int occurrence) { return std::string(name) + " " + std::to_string(occurrence); }
inline std::string named_span(std::string_view name, unsigned count) { return "span " + std::string(name) + " " + std::to_string(count); }
inline std::string pixels(float value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    REQUIRE(result.ec == std::errc{});
    return {buffer, result.ptr};
}
struct Area { float x, y; BehaviorSize size; };
struct Position { float start, end; };
struct Axis {
    bool names_empty;
    std::size_t line_count;
    unsigned negative_implicit, explicit_tracks, positive_implicit;
    std::size_t position_count;
};

class Native {
    BehaviorOracle* handle_ = nullptr;
    // Own text and counters across computes; callbacks borrow only during the
    // synchronous native call. Native IDs are never reused.
    std::unordered_map<Node, AhemContext> ahem_contexts_;
public:
    Native() { REQUIRE(behavior_oracle_create(&handle_) == BEHAVIOR_OK); REQUIRE(handle_ != nullptr); }
    ~Native() { behavior_oracle_free(handle_); }
    Native(const Native&) = delete;
    Native& operator=(const Native&) = delete;
    BehaviorOracle* handle() const { return handle_; }
    void check(std::uint32_t status) const {
        if (status != BEHAVIOR_OK) {
            char message[4096]{};
            std::size_t needed = 0;
            behavior_oracle_error(handle_, message, sizeof(message), &needed);
            INFO("native status " << status << ": " << message);
            REQUIRE(status == BEHAVIOR_OK);
        }
    }
    Node node(std::initializer_list<Property> fields = {}, std::initializer_list<Node> children = {}) {
        std::vector<BehaviorProperty> props;
        for (const auto& field : fields) props.push_back({text(field.first), text(field.second)});
        Node id = 0;
        check(behavior_oracle_new_node(handle_, props.data(), props.size(), &id));
        REQUIRE(id != 0);
        if (children.size()) check(behavior_oracle_set_children(handle_, id, children.begin(), children.size()));
        return id;
    }
    void field(Node id, std::string_view name, std::string_view json) {
        check(behavior_oracle_set_native_field(handle_, id, text(name), text(json)));
    }
    void compute(Node root, BehaviorScalar width = max_content, BehaviorScalar height = max_content) {
        check(behavior_oracle_compute(handle_, root, {width, height}, 1, nullptr, nullptr));
    }
    BehaviorLayout layout(Node node) {
        BehaviorLayout result{};
        check(behavior_oracle_layout(handle_, node, &result));
        return result;
    }
    template<class Query> std::string string_query(Query query) {
        std::size_t required = 0;
        check(query(nullptr, 0, &required));
        REQUIRE(required > 0);
        std::vector<char> buffer(required);
        check(query(buffer.data(), buffer.size(), &required));
        REQUIRE(required <= buffer.size());
        REQUIRE(buffer.at(required - 1) == '\0');
        return {buffer.data(), required - 1};
    }
    Axis axis(Node node, unsigned which) {
        BehaviorGridAxisInfo info{};
        // This accessor fails unless the actual native variant is Grid.
        check(behavior_oracle_grid_axis_info(handle_, node, which, &info));
        REQUIRE(info.stored_names_empty <= 1);
        return {info.stored_names_empty != 0, info.iterator_count,
            info.negative_implicit_tracks, info.explicit_tracks,
            info.positive_implicit_tracks, info.position_count};
    }
    void require_grid(Node node) {
        // Never let absent detailed info masquerade as an empty collection or
        // an out-of-range optional item. Native axis_info rejects non-Grid.
        (void)axis(node, 0);
    }
    Names names(Node node, unsigned which, std::size_t line_index, unsigned mode) {
        std::size_t count = 0;
        check(behavior_oracle_grid_names_count(handle_, node, which, line_index, mode, &count));
        Names result;
        for (std::size_t i = 0; i < count; ++i) {
            result.push_back(string_query([&](char* out, std::size_t cap, std::size_t* needed) {
                return behavior_oracle_grid_name(handle_, node, which, line_index, mode, i, out, cap, needed);
            }));
        }
        return result;
    }
    Names line_names(Node node, unsigned which, std::size_t index) { return names(node, which, index, 1); }
    Names names_for_line(Node node, unsigned which, std::size_t index) { return names(node, which, index, 2); }
    Position position(Node node, unsigned which, std::size_t index) {
        Position result{};
        check(behavior_oracle_grid_position(handle_, node, which, index, &result.start, &result.end));
        return result;
    }
    void ahem(Node node, std::string_view content, bool vertical = false) {
        // Keep the independent actual upstream helper available for comparison;
        // this setter also marks the native node dirty.
        check(behavior_oracle_set_ahem_context(handle_, node, text(content), vertical ? 1 : 0));
        ahem_contexts_.insert_or_assign(node, AhemContext{0, std::string(content), vertical});
    }
    std::size_t cpp_context_count(Node node) const { return ahem_contexts_.at(node).count; }
    void compute_text(Node node, BehaviorScalar width = max_content, BehaviorScalar height = max_content) {
        auto intrinsic = [this](Node measured, BehaviorKnown known, BehaviorAvailable available,
                                BehaviorText actual_style, BehaviorSize* out) -> std::uint32_t {
            // The ABI supplies the complete actual Style used by native
            // compute_leaf_layout, not a reconstructed fixture style.
            REQUIRE(actual_style.data != nullptr);
            REQUIRE(actual_style.len > 2);
            const std::string_view style(actual_style.data, actual_style.len);
            REQUIRE(style.front() == '{');
            REQUIRE(style.back() == '}');
            const auto found = ahem_contexts_.find(measured);
            *out = ahem_intrinsic(found == ahem_contexts_.end() ? nullptr : &found->second,
                                  known, available);
            return BEHAVIOR_OK;
        };
        // Native compute_leaf_layout applies full Style constraints once;
        // behavior_measure_guard prevents any exception crossing Rust frames.
        check(behavior_oracle_compute(handle_, node, {width, height}, 1, &intrinsic,
                                      behavior_measure_guard<decltype(intrinsic)>));
    }
    std::string serialize_default_style() {
        return string_query([&](char* out, std::size_t cap, std::size_t* needed) {
            return behavior_oracle_serde_default(handle_, out, cap, needed);
        });
    }
    void parse_value(std::string_view json) {
        check(behavior_oracle_serde_parse_value(handle_, text(json)));
    }
    std::string tracks(Node node, unsigned axis, float zoom) {
        require_grid(node);
        return string_query([&](char* out, std::size_t cap, std::size_t* needed) {
            return behavior_oracle_tracks_zoom(handle_, node, axis, zoom, out, cap, needed);
        });
    }
    std::optional<Area> item(Node node, std::size_t index) {
        require_grid(node);
        std::uint32_t present = 0;
        BehaviorRect result{};
        check(behavior_oracle_item_grid_area(handle_, node, index, &present, &result));
        REQUIRE(present <= 1);
        if (!present) return std::nullopt;
        return Area{result.left, result.top, {result.right - result.left, result.bottom - result.top}};
    }
    BehaviorRect absolute(Node node, std::string_view row_start, std::string_view row_end,
        std::string_view col_start, std::string_view col_end, bool rtl, BehaviorRect padding) {
        require_grid(node);
        std::uint32_t present = 0;
        BehaviorRect result{};
        check(behavior_oracle_resolve_area(handle_, node, text(row_start), text(row_end),
            text(col_start), text(col_end), rtl ? 1 : 0, padding, &present, &result));
        REQUIRE(present == 1);
        return result;
    }
};
inline void area_is(const std::optional<Area>& actual, float x, float y, float width, float height) {
    REQUIRE(actual.has_value());
    REQUIRE(actual->x == x);
    REQUIRE(actual->y == y);
    REQUIRE(actual->size.width == width);
    REQUIRE(actual->size.height == height);
}
inline void size_is(BehaviorSize actual, float width, float height) {
    REQUIRE(actual.width == width);
    REQUIRE(actual.height == height);
}
inline void rect_is(BehaviorRect actual, BehaviorRect expected) {
    REQUIRE(actual.left == expected.left);
    REQUIRE(actual.right == expected.right);
    REQUIRE(actual.top == expected.top);
    REQUIRE(actual.bottom == expected.bottom);
}
} // namespace grid_behavior
#endif
