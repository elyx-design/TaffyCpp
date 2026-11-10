// Supplemental ABI contract tests. Source fixtures are separate translation units
// in tests/behavior/. This is a test-only native oracle, NOT production parity.
#include "support/behavior_suite_oracle.h"
#include "behavior/grid_native.h"
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace behavior_contract {
BehaviorText text(const char* s) { return {s, std::strlen(s)}; }
constexpr BehaviorAvailable max_content{{4, 0}, {4, 0}};
struct Tree {
    BehaviorOracle* p = nullptr;
    Tree() { REQUIRE(behavior_oracle_create(&p) == BEHAVIOR_OK); }
    ~Tree() { behavior_oracle_free(p); }
    Tree(const Tree&) = delete;
    Tree& operator=(const Tree&) = delete;
    uint64_t node(std::initializer_list<std::pair<const char*, const char*>> props = {}) {
        std::vector<BehaviorProperty> fields;
        for (const auto& [name, value] : props) fields.push_back({text(name), text(value)});
        uint64_t id = 0;
        REQUIRE(behavior_oracle_new_node(p, fields.data(), fields.size(), &id) == BEHAVIOR_OK);
        return id;
    }
    std::vector<uint64_t> children(uint64_t id) {
        size_t length = 0;
        REQUIRE(behavior_oracle_children(p, id, nullptr, 0, &length) == BEHAVIOR_OK);
        std::vector<uint64_t> result(length);
        REQUIRE(behavior_oracle_children(p, id, result.data(), result.size(), &length) == BEHAVIOR_OK);
        REQUIRE(result.size() == length);
        return result;
    }
};

TEST_CASE("behavior_contract::persistent_logical_ids_parent_children", "[behavior][contract][native-oracle]") {
    Tree tree;
    auto a = tree.node(), b = tree.node(), child = tree.node();
    REQUIRE(behavior_oracle_add_child(tree.p, a, child) == BEHAVIOR_OK);
    uint64_t parent = 0;
    REQUIRE(behavior_oracle_parent(tree.p, child, &parent) == BEHAVIOR_OK);
    REQUIRE(parent == a);
    REQUIRE(tree.children(a) == std::vector<uint64_t>{child});
    REQUIRE(behavior_oracle_set_children(tree.p, b, &child, 1) == BEHAVIOR_OK);
    REQUIRE(tree.children(a).empty());
    REQUIRE(tree.children(b) == std::vector<uint64_t>{child});
    REQUIRE(behavior_oracle_parent(tree.p, child, &parent) == BEHAVIOR_OK);
    REQUIRE(parent == b);
    // Invalid operations cannot silently replace or drop an existing relation.
    REQUIRE(behavior_oracle_add_child(tree.p, child, b) == BEHAVIOR_INVALID);
    REQUIRE(tree.children(b) == std::vector<uint64_t>{child});
    REQUIRE(behavior_oracle_remove_node(tree.p, b) == BEHAVIOR_OK);
    REQUIRE(behavior_oracle_parent(tree.p, child, &parent) == BEHAVIOR_OK);
    REQUIRE(parent == 0);
    BehaviorLayout layout{};
    REQUIRE(behavior_oracle_layout(tree.p, b, &layout) == BEHAVIOR_STALE);
    const auto next = tree.node();
    REQUIRE(next > child);
}

TEST_CASE("behavior_contract::full_native_style_raw_intrinsic_cache_counts", "[behavior][contract][native-oracle]") {
    Tree tree;
    const auto leaf = tree.node({{"item-is-replaced", "true"}, {"padding-left", "7px"},
        {"padding-right", "3px"}, {"min-width", "50px"}});
    REQUIRE(behavior_oracle_set_native_field(tree.p, leaf, text("grid_template_column_names"),
        text(R"([["alpha"],["omega"]])")) == BEHAVIOR_OK);
    size_t count = 0;
    bool full_style = true;
    auto callback = [&](uint64_t id, BehaviorKnown known, BehaviorAvailable,
                        BehaviorText style, BehaviorSize* out) -> uint32_t {
        ++count;
        const std::string actual(style.data, style.len);
        full_style = full_style && id == leaf &&
            actual.find("\"item_is_replaced\":true") != std::string::npos &&
            actual.find("\"grid_template_column_names\":[[\"alpha\"],[\"omega\"]]") != std::string::npos &&
            actual.find("\"padding\":") != std::string::npos &&
            actual.find("\"contain\":") != std::string::npos &&
            actual.find("\"grid_template_rows\":") != std::string::npos;
        // Intrinsic response only: native compute_leaf_layout owns all Style constraints.
        *out = {known.width.present ? known.width.value : 20,
                known.height.present ? known.height.value : 15};
        return BEHAVIOR_OK;
    };
    auto compute = [&] {
        REQUIRE(behavior_oracle_compute(tree.p, leaf, max_content, 1, &callback,
            behavior_measure_guard<decltype(callback)>) == BEHAVIOR_OK);
    };
    compute();
    REQUIRE(full_style);
    REQUIRE(count > 0);
    const auto first_count = count;
    BehaviorLayout layout{};
    REQUIRE(behavior_oracle_layout(tree.p, leaf, &layout) == BEHAVIOR_OK);
    REQUIRE(layout.size.width == 50);
    REQUIRE(layout.size.height == 15);
    REQUIRE(layout.padding.left == 7);
    REQUIRE(layout.padding.right == 3);
    compute();
    REQUIRE(count == first_count); // Actual native cached recompute calls no intrinsic.
    REQUIRE(behavior_oracle_mark_dirty(tree.p, leaf) == BEHAVIOR_OK);
    compute();
    REQUIRE(count == first_count * 2);
    REQUIRE(full_style);
}

TEST_CASE("behavior_contract::cpp_exception_is_caught_and_tree_poisoned", "[behavior][contract][native-oracle]") {
    Tree tree;
    const auto leaf = tree.node();
    auto callback = [](uint64_t, BehaviorKnown, BehaviorAvailable, BehaviorText, BehaviorSize*) -> uint32_t {
        throw std::runtime_error("C++ callback injection");
    };
    REQUIRE(behavior_oracle_compute(tree.p, leaf, max_content, 1, &callback,
        behavior_measure_guard<decltype(callback)>) == BEHAVIOR_CALLBACK_ERROR);
    REQUIRE(behavior_oracle_mark_dirty(tree.p, leaf) == BEHAVIOR_POISONED);
    BehaviorLayout layout{};
    REQUIRE(behavior_oracle_layout(tree.p, leaf, &layout) == BEHAVIOR_POISONED);
    size_t needed = 0;
    REQUIRE(behavior_oracle_error(tree.p, nullptr, 0, &needed) == BEHAVIOR_OK);
    REQUIRE(needed > 1);
}

TEST_CASE("behavior_contract::rust_panic_is_distinct_and_poison_is_local", "[behavior][contract][native-oracle]") {
    Tree poisoned, healthy;
    const auto first = poisoned.node(), second = healthy.node();
    REQUIRE(behavior_oracle_inject_panic(poisoned.p) == BEHAVIOR_PANIC);
    REQUIRE(behavior_oracle_mark_dirty(poisoned.p, first) == BEHAVIOR_POISONED);
    REQUIRE(behavior_oracle_compute(healthy.p, second, max_content, 1, nullptr, nullptr) == BEHAVIOR_OK);
    size_t needed = 0;
    REQUIRE(behavior_oracle_error(poisoned.p, nullptr, 0, &needed) == BEHAVIOR_OK);
    std::vector<char> error(needed);
    REQUIRE(behavior_oracle_error(poisoned.p, error.data(), error.size(), &needed) == BEHAVIOR_OK);
    REQUIRE(std::string(error.data()).find("injected native Rust panic") != std::string::npos);
}

TEST_CASE("behavior_contract::optional_detailed_metadata_absence_is_not_empty_grid", "[behavior][contract][native-oracle]") {
    Tree tree;
    const auto root = tree.node({{"display", "grid"}});
    REQUIRE(behavior_oracle_compute(tree.p, root, max_content, 1, nullptr, nullptr) == BEHAVIOR_OK);
    BehaviorGridAxisInfo axis{};
    REQUIRE(behavior_oracle_grid_axis_info(tree.p, root, 1, &axis) == BEHAVIOR_ERROR);
    uint32_t present = 99;
    BehaviorRect area{};
    REQUIRE(behavior_oracle_item_grid_area(tree.p, root, 0, &present, &area) == BEHAVIOR_OK);
    REQUIRE(present == 0);
    const auto hidden = tree.node({{"display", "none"}});
    REQUIRE(behavior_oracle_add_child(tree.p, root, hidden) == BEHAVIOR_OK);
    REQUIRE(behavior_oracle_compute(tree.p, root, max_content, 1, nullptr, nullptr) == BEHAVIOR_OK);
    REQUIRE(behavior_oracle_grid_axis_info(tree.p, root, 1, &axis) == BEHAVIOR_OK);
    REQUIRE(axis.position_count == 0);
    REQUIRE(axis.explicit_tracks == 0);
    REQUIRE(axis.empty_axis_line.present == 1);
    REQUIRE(axis.empty_axis_line.value == 0);
}

TEST_CASE("behavior_contract::snapshot_mutation_consumes_actual_clone", "[behavior][contract][native-oracle]") {
    Tree tree;
    const auto leaf = tree.node({{"width", "20px"}, {"height", "10px"}, {"position", "absolute"},
        {"item-is-replaced", "true"}});
    size_t needed = 0;
    REQUIRE(behavior_oracle_style_json(tree.p, leaf, nullptr, 0, &needed) == BEHAVIOR_OK);
    std::vector<char> snapshot(needed);
    REQUIRE(behavior_oracle_style_json(tree.p, leaf, snapshot.data(), snapshot.size(), &needed) == BEHAVIOR_OK);
    REQUIRE(behavior_oracle_set_dimension(tree.p, leaf, 0, 0, 1, 99) == BEHAVIOR_OK);
    REQUIRE(behavior_oracle_set_style_json_inset(tree.p, leaf, {snapshot.data(), needed - 1}, 0, 1, 11) == BEHAVIOR_OK);
    REQUIRE(behavior_oracle_compute(tree.p, leaf, max_content, 1, nullptr, nullptr) == BEHAVIOR_OK);
    BehaviorLayout layout{};
    REQUIRE(behavior_oracle_layout(tree.p, leaf, &layout) == BEHAVIOR_OK);
    REQUIRE(layout.size.width == 20); // Snapshot, not the current 99px tree Style.
    REQUIRE(layout.size.height == 10);
    uint32_t replaced = 0, compressible = 0;
    REQUIRE(behavior_oracle_core_flags(tree.p, leaf, &replaced, &compressible) == BEHAVIOR_OK);
    REQUIRE(replaced == 1);
}

TEST_CASE("behavior_contract::cpp_ahem_matches_native_helper_and_counts", "[behavior][contract][native-oracle]") {
    for (bool vertical : {false, true}) {
        grid_behavior::Native cpp, native;
        const auto a = cpp.node({{"padding-left", "3px"}, {"min-width", "20px"}});
        const auto b = native.node({{"padding-left", "3px"}, {"min-width", "20px"}});
        cpp.ahem(a, "HH\xe2\x80\x8bHH\xe2\x80\x8bHH", vertical);
        native.ahem(b, "HH\xe2\x80\x8bHH\xe2\x80\x8bHH", vertical);
        const auto ra = cpp.node({{"display", "grid"}, {"grid-template-columns", "fit-content(50%)"},
            {"grid-template-rows", "40px"}}, {a});
        const auto rb = native.node({{"display", "grid"}, {"grid-template-columns", "fit-content(50%)"},
            {"grid-template-rows", "40px"}}, {b});
        cpp.compute_text(ra);
        native.check(behavior_oracle_compute_test_measure(native.handle(), rb, max_content, 1));
        const auto ca = cpp.layout(a), nb = native.layout(b);
        REQUIRE(ca.size.width == nb.size.width);
        REQUIRE(ca.size.height == nb.size.height);
        REQUIRE(ca.x == nb.x);
        REQUIRE(ca.y == nb.y);
        REQUIRE(cpp.layout(ra).size.width == native.layout(rb).size.width);
        size_t native_count = 0;
        native.check(behavior_oracle_context_count(native.handle(), b, &native_count));
        REQUIRE(cpp.cpp_context_count(a) == native_count);
        REQUIRE(native_count > 0);
        cpp.compute_text(ra);
        REQUIRE(cpp.cpp_context_count(a) == native_count); // Native cache skips CPP callbacks too.
    }
}

TEST_CASE("behavior_contract::ahem_known_absent_context_count_short_circuits", "[behavior][contract][native-oracle]") {
    grid_behavior::AhemContext context{0, "HH", false};
    const BehaviorKnown both{{1, 50}, {1, 70}};
    const auto known = grid_behavior::ahem_intrinsic(&context, both, max_content);
    REQUIRE(known.width == 50);
    REQUIRE(known.height == 70);
    REQUIRE(context.count == 0);
    const auto absent = grid_behavior::ahem_intrinsic(nullptr, {{1, 30}, {0, 0}}, max_content);
    REQUIRE(absent.width == 30);
    REQUIRE(absent.height == 0);
    const auto measured = grid_behavior::ahem_intrinsic(&context, {}, max_content);
    REQUIRE(measured.width == 20);
    REQUIRE(measured.height == 10);
    REQUIRE(context.count == 1);
}

TEST_CASE("behavior_contract::unknown_and_duplicate_style_fields_fail_visible", "[behavior][contract][native-oracle]") {
    Tree tree;
    const auto leaf = tree.node();
    REQUIRE(behavior_oracle_set_native_field(tree.p, leaf, text("not_a_native_field"), text("0")) == BEHAVIOR_INVALID);
    // Native serde accepts sequence-form structs with defaulted missing fields;
    // the full-Style ABI must reject that lossy alternate representation.
    REQUIRE(behavior_oracle_set_style_json(tree.p, leaf, text("[]")) == BEHAVIOR_INVALID);
    REQUIRE(behavior_oracle_set_native_field(tree.p, leaf, text("grid_row"), text("[]")) == BEHAVIOR_INVALID);
    BehaviorProperty fields[] = {{text("width"), text("10px")}, {text("width"), text("20px")}};
    REQUIRE(behavior_oracle_set_style(tree.p, leaf, fields, 2) == BEHAVIOR_INVALID);
    REQUIRE(behavior_oracle_compute(tree.p, leaf, max_content, 1, nullptr, nullptr) == BEHAVIOR_OK);
    BehaviorLayout layout{};
    REQUIRE(behavior_oracle_layout(tree.p, leaf, &layout) == BEHAVIOR_OK);
    REQUIRE(layout.size.width == 0); // Rejected mutation leaves the original Style intact.
}
} // namespace behavior_contract
