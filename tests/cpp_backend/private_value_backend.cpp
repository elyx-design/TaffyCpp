// Native, test-only implementation of the miscellaneous private-suite ABI.
// This file deliberately defines NO grid, occupancy, or balance entry points.
// Representation-only Rust operations return explicit unsupported diagnostics;
// semantic operations use production C++ values and the bounded two-ended builder.
#include "../ported/private/support/private_suite_misc.h"
#include <taffy/taffy.hpp>
#include "../../src/front_back_builder.hpp"
#include "../../src/oof.hpp"
#include <variant>
#include <mutex>
#include <memory>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// TEST-ONLY semantic JSON adapter. The parent may supply a compatible 3.x
// header; production Taffy remains stdlib-only. No handwritten JSON parser.
#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
static_assert(NLOHMANN_JSON_VERSION_MAJOR == 3 &&
              (NLOHMANN_JSON_VERSION_MINOR > 11 ||
               (NLOHMANN_JSON_VERSION_MINOR == 11 && NLOHMANN_JSON_VERSION_PATCH >= 3)),
              "test JSON adapter requires nlohmann/json 3.x, version 3.11.3 or newer");
#define TAFFY_PRIVATE_VALUE_JSON 1
#else
#define TAFFY_PRIVATE_VALUE_JSON 0
#endif

namespace {
using namespace taffy;
thread_local std::string error;
thread_local uint32_t injected_fault = 0;
struct InjectedPanic : std::runtime_error { using std::runtime_error::runtime_error; };

void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}
[[noreturn]] void pending(const char* message) { throw std::invalid_argument(message); }
template<class F> int32_t run(F&& operation) noexcept {
    try { operation(); error.clear(); return 0; }
    catch (const InjectedPanic& e) { error = e.what(); return 2; }
    catch (const std::exception& e) { error = e.what(); return 1; }
    catch (...) { error = "unknown native exception"; return 2; }
}
bool fault(uint32_t code) {
    if (injected_fault != code) return false;
    injected_fault = 0;
    return true;
}
template<class T> void put(T* output, T value) {
    require(output != nullptr, "null output");
    *output = std::move(value);
}
template<class T> void input(const T* values, size_t length) {
    require(length <= 1000000 && (length == 0 || values != nullptr), "invalid input span");
}
AxisStaticEdge edge(uint32_t code) {
    constexpr std::array values{AxisStaticEdge::Start, AxisStaticEdge::End, AxisStaticEdge::Center};
    require(code < values.size(), "invalid edge"); return values[code];
}
AlignmentSafety safety(uint32_t code) {
    constexpr std::array values{AlignmentSafety::Default, AlignmentSafety::Safe, AlignmentSafety::Unsafe};
    require(code < values.size(), "invalid safety"); return values[code];
}
// Native C++ ceilings, independent of Rust's tagged/niche representation.
// Select by pointer width, not LP64: Windows x64 is LLP64, while Emscripten
// -sMEMORY64=1 defines __wasm64__ (not a C++ MEMORY64 macro).
static_assert(sizeof(void*) == 4 || sizeof(void*) == 8, "review native budgets for this pointer width");
#if defined(_WIN64) || defined(__wasm64__)
static_assert(sizeof(void*) == 8, "64-bit target must have 64-bit pointers");
#elif defined(_WIN32) || defined(__wasm32__)
static_assert(sizeof(void*) == 4, "32-bit target must have 32-bit pointers");
#endif
constexpr std::array<size_t, 32> native_budgets_32{
    1,1,1,1,4,16,16,16,32,32,32,64,64,64,1,1,1,2,2,3,
    1,1,1,16,16,32,16,16,80,32,64,1024};
constexpr std::array<size_t, 32> native_budgets_64{
    1,1,1,1,4,32,32,32,64,64,64,128,128,128,1,1,1,2,2,3,
    1,1,1,32,32,64,32,32,160,64,128,2048};
constexpr auto native_size_budgets = sizeof(void*) == 4 ? native_budgets_32 : native_budgets_64;

constexpr std::array content_keywords{
    AlignContentKeyword::Normal, AlignContentKeyword::Start, AlignContentKeyword::End,
    AlignContentKeyword::FlexStart, AlignContentKeyword::FlexEnd, AlignContentKeyword::Center,
    AlignContentKeyword::Stretch, AlignContentKeyword::SpaceBetween,
    AlignContentKeyword::SpaceEvenly, AlignContentKeyword::SpaceAround};
#if TAFFY_PRIVATE_VALUE_JSON
constexpr std::array item_keywords{
    AlignItemsKeyword::Normal, AlignItemsKeyword::Start, AlignItemsKeyword::End,
    AlignItemsKeyword::FlexStart, AlignItemsKeyword::FlexEnd, AlignItemsKeyword::Center,
    AlignItemsKeyword::Baseline, AlignItemsKeyword::Stretch,
    AlignItemsKeyword::SelfStart, AlignItemsKeyword::SelfEnd};
#endif
float offset(AxisStaticPosition x, float width, float left, float right) {
    return resolve_static_offset(
        {x, AxisStaticPosition::from_edge(0, AxisStaticEdge::Start)},
        {width, 10}, {left, right, 0, 0}).x;
}

// Thread-local, monotonically allocated handles cannot accidentally revive a
// removed object. Tombstones are still destroyable once after injected panics.
struct StyleSlot { std::optional<Style> value; bool destroyable = true; };
thread_local std::vector<StyleSlot> styles;
StyleSlot& slot(uint64_t handle) {
    require(handle > 0 && handle <= styles.size(), "invalid style");
    return styles[static_cast<size_t>(handle - 1)];
}
Style& style(uint64_t handle) {
    auto& s = slot(handle);
    require(s.destroyable && s.value.has_value(), "invalid style");
    return *s.value;
}
void invalidate(uint64_t handle) {
    if (handle > 0 && handle <= styles.size()) styles[static_cast<size_t>(handle - 1)].value.reset();
}

using ScalarBuilder = FrontBackVecBuilder<int32_t>;
using OwnedBuilder = FrontBackVecBuilder<std::vector<int32_t>>;
using ByteBuilder = FrontBackVecBuilder<uint8_t>;
using Builder = std::variant<ScalarBuilder, OwnedBuilder, ByteBuilder>;
struct BuilderSlot { std::optional<Builder> value; bool destroyable = true; };
std::mutex builder_mutex;
std::vector<BuilderSlot> builders;
BuilderSlot& builder_slot(uint64_t h) {
    require(h > 0 && h <= builders.size(), "invalid builder");
    return builders[static_cast<size_t>(h - 1)];
}
template<class F> int32_t builder_run(uint64_t h, F&& f) noexcept {
    std::lock_guard<std::mutex> lock(builder_mutex);
    return run([&] {
        try { f(); }
        catch (const ScalarBuilder::Panic& e) {
            auto& s = builder_slot(h); s.value.reset(); s.destroyable = true;
            throw InjectedPanic(e.what());
        }
        catch (const OwnedBuilder::Panic& e) {
            auto& s = builder_slot(h); s.value.reset(); s.destroyable = true;
            throw InjectedPanic(e.what());
        }
        catch (const ByteBuilder::Panic& e) {
            auto& s = builder_slot(h); s.value.reset(); s.destroyable = true;
            throw InjectedPanic(e.what());
        }
        catch (const InjectedPanic&) {
            auto& s = builder_slot(h); s.value.reset(); s.destroyable = true;
            throw;
        }
    });
}

// Use the actual native domain factories/predicates. Fr belongs to the max-track
// domain; every other compact semantic value is expressible by Dimension.
template<class F> auto with_length(uint32_t kind, float value, const void* pointer, F&& f) {
    const Calc calc{reinterpret_cast<std::uintptr_t>(pointer)};
    switch (kind) {
    case 0: return f(Dimension::length(value));
    case 1: return f(Dimension::auto_());
    case 2: return f(MaxTrackSizingFunction::fr(value));
    case 3: return f(Dimension::min_content());
    case 4: return f(Dimension::max_content());
    case 5: return f(Dimension::fit_content_px(value));
    case 6: return f(Dimension::fit_content());
    case 7: return f(Dimension::stretch());
    case 8: return f(Dimension::content());
    case 9: return f(Dimension::percent(value));
    case 10: return f(Dimension::fit_content_percent(value));
    case 11: return f(Dimension::calc(calc));
    case 12: return f(Dimension::fit_content_calc(calc));
    case 13: pending("unsupported-representation: raw Rust compact tag construction has no native counterpart");
    default: throw std::invalid_argument("invalid kind");
    }
}

#if TAFFY_PRIVATE_VALUE_JSON
// A test-only wire-format codec for actual public AlignItems/AlignContent values,
// matching upstream's documented legacy tag strings, not Rust object storage.
constexpr std::array<std::string_view, 10> item_names{
    "Normal", "Start", "End", "FlexStart", "FlexEnd", "Center", "Baseline", "Stretch", "SelfStart", "SelfEnd"};
constexpr std::array<std::string_view, 10> content_names{
    "Normal", "Start", "End", "FlexStart", "FlexEnd", "Center", "Stretch", "SpaceBetween", "SpaceEvenly", "SpaceAround"};
template<class Alignment, size_t N>
std::string wire_name(Alignment value, const std::array<decltype(value.keyword), N>& keywords,
                      const std::array<std::string_view, N>& names) {
    const auto it = std::find(keywords.begin(), keywords.end(), value.keyword);
    require(it != keywords.end(), "invalid keyword");
    const auto code = static_cast<size_t>(it - keywords.begin());
    const bool positional = (code >= 1 && code <= 5) ||
        (std::is_same_v<Alignment, AlignItems> && code >= 8);
    const auto prefix = !positional || value.safety == AlignmentSafety::Default ? "" :
        (value.safety == AlignmentSafety::Safe ? "Safe" : "Unsafe");
    return std::string(prefix) + std::string(names[code]);
}
#endif
} // namespace

extern "C" {
int32_t ps_misc_error(uint8_t* output, size_t capacity, size_t* written) {
    // Diagnostic retrieval must not clear the diagnostic.
    try {
        if (!written) return 1;
        *written = error.size();
        if (capacity < error.size() || (!error.empty() && !output)) return 1;
        if (!error.empty()) std::memcpy(output, error.data(), error.size());
        return 0;
    } catch (...) { return 2; }
}
int32_t ps_misc_inject_fault(uint32_t code) {
    return run([&] { require(code <= 6, "invalid fault"); injected_fault = code; });
}

int32_t ps_misc_builder_new(size_t capacity, uint32_t kind, uint64_t* output) {
    std::lock_guard<std::mutex> lock(builder_mutex);
    return run([&] {
        require(output && capacity <= 100000 && kind <= 2, "invalid capacity/kind/output");
        Builder b = kind == 0 ? Builder(std::in_place_type<ScalarBuilder>, capacity) :
            kind == 1 ? Builder(std::in_place_type<OwnedBuilder>, capacity) :
                        Builder(std::in_place_type<ByteBuilder>, capacity);
        builders.push_back({std::move(b), true});
        *output = builders.size();
    });
}
int32_t ps_misc_builder_push(uint64_t h, uint32_t back, const int64_t* values, size_t n) {
    return builder_run(h, [&] {
        require(back <= 1, "invalid end"); input(values, n);
        auto& s = builder_slot(h);
        require(s.value.has_value() && s.destroyable, "invalid builder");
        if (fault(4)) throw InjectedPanic("injected builder panic");
        std::visit([&](auto& b) {
            using B = std::decay_t<decltype(b)>;
            if constexpr (std::is_same_v<B, OwnedBuilder>) {
                std::vector<int32_t> v; v.reserve(n);
                for (size_t i = 0; i < n; ++i) {
                    require(values[i] >= INT32_MIN && values[i] <= INT32_MAX, "owned scalar out of range");
                    v.push_back(static_cast<int32_t>(values[i]));
                }
                if (back) b.push_back(std::move(v)); else b.push_front(std::move(v));
            } else {
                require(n == 1, "scalar payload requires one value");
                constexpr bool byte = std::is_same_v<B, ByteBuilder>;
                require(values[0] >= (byte ? 0 : INT32_MIN) && values[0] <= (byte ? 255 : INT32_MAX), "scalar out of range");
                if constexpr (byte) {
                    if (back) b.push_back(static_cast<uint8_t>(values[0])); else b.push_front(static_cast<uint8_t>(values[0]));
                } else {
                    if (back) b.push_back(static_cast<int32_t>(values[0])); else b.push_front(static_cast<int32_t>(values[0]));
                }
            }
        }, *s.value);
    });
}
int32_t ps_misc_builder_finish(uint64_t h, int64_t* values, size_t capacity, size_t* offsets,
                               size_t offset_capacity, size_t* items, size_t* front) {
    return builder_run(h, [&] {
        require(items && front, "null output");
        auto& s = builder_slot(h);
        require(s.value.has_value() && s.destroyable, "invalid builder");
        Builder b = std::move(*s.value); s.value.reset(); s.destroyable = false;
        std::vector<std::vector<int64_t>> rows;
        size_t f = 0;
        std::visit([&](auto& builder) {
            auto result = builder.into_vec(); f = result.second;
            for (auto& value : result.first) {
                if constexpr (std::is_same_v<std::decay_t<decltype(builder)>, OwnedBuilder>)
                    rows.emplace_back(value.begin(), value.end());
                else rows.push_back({static_cast<int64_t>(value)});
            }
        }, b);
        size_t total = 0; for (const auto& row : rows) total += row.size();
        require(capacity >= total && offset_capacity > rows.size() && offsets && (!total || values), "output capacity");
        size_t pos = 0; offsets[0] = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
            for (auto value : rows[i]) values[pos++] = value;
            offsets[i + 1] = pos;
        }
        *items = rows.size(); *front = f;
    });
}
int32_t ps_misc_builder_drop(uint64_t h) {
    return builder_run(h, [&] {
        auto& s = builder_slot(h); require(s.destroyable, "invalid builder");
        s.value.reset(); s.destroyable = false;
    });
}

int32_t ps_misc_static_offset(float start, float end, uint32_t keyword, uint32_t safety_code,
                              uint32_t fallback, float width, float left, float right, float* output) {
    return run([&] {
        put(output, offset({{start, end}, {edge(keyword), safety(safety_code), edge(fallback)}}, width, left, right));
    });
}
int32_t ps_misc_static_from_edge(float anchor, uint32_t keyword, float width, float left, float right, float* output) {
    return run([&] {
        if (fault(2)) anchor += 1;
        put(output, offset(AxisStaticPosition::from_edge(anchor, edge(keyword)), width, left, right));
    });
}
int32_t ps_misc_flex_direction(uint32_t code, uint32_t* output) {
    return run([&] {
        constexpr std::array values{FlexDirection::Row, FlexDirection::RowReverse,
                                    FlexDirection::Column, FlexDirection::ColumnReverse};
        require(code < values.size(), "invalid direction");
        const auto d = values[code];
        put(output, uint32_t(is_row(d)) | (uint32_t(is_column(d)) << 1) | (uint32_t(is_reverse(d)) << 2));
    });
}
int32_t ps_misc_reverse_content(uint32_t code, uint32_t* output) {
    return run([&] {
        require(code < content_keywords.size(), "invalid keyword");
        const auto result = reversed(content_keywords[code]);
        put(output, static_cast<uint32_t>(std::find(content_keywords.begin(), content_keywords.end(), result) - content_keywords.begin()));
    });
}
int32_t ps_misc_hidden_with_counts(const int32_t* parents, const uint32_t* displays,
                                  const float* widths, const float* heights, size_t n,
                                  size_t root, float* output, size_t capacity, size_t* child_counts) {
    return run([&] {
        input(parents, n); input(displays, n); input(widths, n); input(heights, n);
        require(n > 0 && n <= 10000 && root < n && output && capacity >= n * 4, "invalid tree/output");
        std::vector<std::vector<size_t>> children(n);
        for (size_t i = 0; i < n; ++i) {
            require(parents[i] >= -1 && parents[i] < static_cast<int32_t>(n) && parents[i] != static_cast<int32_t>(i), "invalid parent");
            require(displays[i] <= 1, "invalid display");
            if (parents[i] >= 0) children[static_cast<size_t>(parents[i])].push_back(i);
        }
        Tree tree;
        std::vector<NodeId> nodes(n);
        std::vector<size_t> remaining(n), ready;
        for (size_t i = 0; i < n; ++i) {
            remaining[i] = children[i].size();
            if (!remaining[i]) ready.push_back(i);
        }
        const bool omit_links = fault(3);
        for (size_t next = 0; next < ready.size(); ++next) {
            const auto i = ready[next];
            Style s;
            s.display = displays[i] ? Display::Flex : Display::None;
            s.size = Size<Dimension>::from_lengths(widths[i], heights[i]);
            std::vector<NodeId> ids;
            if (!omit_links) for (auto c : children[i]) ids.push_back(nodes[c]);
            auto node = children[i].empty() ? tree.new_leaf(s) : tree.new_with_children(s, ids);
            require(node.has_value(), "native tree construction failed");
            nodes[i] = *node;
            if (parents[i] >= 0) {
                const auto p = static_cast<size_t>(parents[i]);
                if (--remaining[p] == 0) ready.push_back(p);
            }
        }
        require(ready.size() == n, "cyclic tree");
        if (child_counts) for (size_t i = 0; i < n; ++i) child_counts[i] = tree.child_count(nodes[i]);
        (void)compute_hidden_layout(tree, nodes[root]);
        for (size_t i = 0; i < n; ++i) {
            const auto& l = tree.layout(nodes[i]);
            output[i * 4] = l.size.width; output[i * 4 + 1] = l.size.height;
            output[i * 4 + 2] = l.location.x; output[i * 4 + 3] = l.location.y;
        }
    });
}
int32_t ps_misc_hidden(const int32_t* parents, const uint32_t* displays, const float* widths,
                       const float* heights, size_t n, size_t root, float* output, size_t capacity) {
    return ps_misc_hidden_with_counts(parents, displays, widths, heights, n, root, output, capacity, nullptr);
}

int32_t ps_misc_style_new(uint32_t factory, uint64_t* output) {
    return run([&] {
        require(factory <= 1 && output, "invalid factory/output");
        // C++ exposes value/default initialization, not Rust's associated DEFAULT.
        // Both native factories are real Style construction; no ABI layout copy.
        styles.push_back({factory == 0 ? Style() : Style{}, true});
        *output = styles.size();
    });
}
int32_t ps_misc_style_property(uint64_t handle, uint32_t property, float value) {
    return run([&] {
        auto& s = style(handle);
        if (fault(5)) { invalidate(handle); throw InjectedPanic("injected style property panic"); }
        switch (property) {
        case 0: s.display = Display{}; break;
        case 1: s.item_is_table = value != 0; break;
        case 2: s.item_is_replaced = value != 0; break;
        case 3: s.item_is_compressible_replaced = value != 0; break;
        case 4: s.box_sizing = BoxSizing{}; break;
        case 5: s.float_ = Float{}; break;
        case 6: s.clear = Clear{}; break;
        case 7: s.direction = Direction{}; break;
        case 8: s.overflow = {}; break;
        case 9: s.scrollbar_width = value; break;
        case 10: s.contain = {}; break;
        case 11: s.position = Position{}; break;
        case 12: s.flex_direction = FlexDirection{}; break;
        case 13: s.flex_wrap = FlexWrap{}; break;
        case 14:
            require(std::isfinite(value) && value >= 0 && value <= std::numeric_limits<uint16_t>::max() && std::trunc(value) == value, "invalid line count");
            s.flex_line_count = static_cast<uint16_t>(value); break;
        case 15: s.align_items = {}; break;
        case 16: s.align_self = {}; break;
        case 17: s.justify_items = {}; break;
        case 18: s.justify_self = {}; break;
        case 19: s.align_content = {}; break;
        case 20: s.justify_content = {}; break;
        case 21: s.inset = value == 1 ? Rect<LengthPercentageAuto>::auto_() : Rect<LengthPercentageAuto>::zero(); break;
        case 22: s.margin = value == 1 ? Rect<LengthPercentageAuto>::auto_() : Rect<LengthPercentageAuto>::zero(); break;
        case 23: s.padding = Rect<LengthPercentage>::zero(); break;
        case 24: s.border = Rect<LengthPercentage>::zero(); break;
        case 25: s.gap = Size<LengthPercentage>::zero(); break;
        case 26: s.text_align = TextAlign{}; break;
        case 27: s.flex_grow = value; break;
        case 28: s.flex_shrink = value; break;
        case 29: s.flex_basis = value == 1 ? Dimension::auto_() : Dimension::length(value); break;
        case 30: s.size = value == 1 ? Size<Dimension>::auto_() : Size<Dimension>::from_lengths(value, value); break;
        case 31: s.min_size = value == 1 ? Size<LengthPercentageAuto>::auto_() : Size<LengthPercentageAuto>::from_lengths(value, value); break;
        case 32: s.max_size = value == 1 ? Size<LengthPercentageAuto>::auto_() : Size<LengthPercentageAuto>::from_lengths(value, value); break;
        case 33: s.aspect_ratio = value == 0 ? std::nullopt : std::optional<float>(value); break;
        case 34: s.grid_template_rows = {}; break;
        case 35: s.grid_template_columns = {}; break;
        case 36: s.grid_template_row_names = {}; break;
        case 37: s.grid_template_column_names = {}; break;
        case 38: s.grid_template_areas = {}; break;
        case 39: s.grid_auto_rows = {}; break;
        case 40: s.grid_auto_columns = {}; break;
        case 41: s.grid_auto_flow = GridAutoFlow{}; break;
        case 42: s.grid_row = {GridPlacement::auto_(), GridPlacement::auto_()}; break;
        case 43: s.grid_column = {GridPlacement::auto_(), GridPlacement::auto_()}; break;
        default: throw std::invalid_argument("invalid property");
        }
    });
}
int32_t ps_misc_style_equal(uint64_t a, uint64_t b, uint32_t* output) {
    return run([&] {
        const auto& lhs = style(a); const auto& rhs = style(b);
        require(output, "null output");
        if (fault(6)) { invalidate(a); invalidate(b); throw InjectedPanic("injected style equality panic"); }
        *output = lhs == rhs;
    });
}
int32_t ps_misc_style_drop(uint64_t handle) {
    return run([&] {
        auto& s = slot(handle); require(s.destroyable, "invalid style");
        s.value.reset(); s.destroyable = false;
    });
}
int32_t ps_misc_type_size(uint32_t id, size_t* output) {
    return run([&] {
        // These are ACTUAL C++ sizeof results. Exact Rust-size assertions remain
        // incompatible evidence, not values to copy into this adapter.
        constexpr std::array sizes{
            sizeof(Display), sizeof(BoxSizing), sizeof(Position), sizeof(Overflow), sizeof(float),
            sizeof(LengthPercentage), sizeof(LengthPercentageAuto), sizeof(Dimension),
            sizeof(Size<LengthPercentage>), sizeof(Size<LengthPercentageAuto>), sizeof(Size<Dimension>),
            sizeof(Rect<LengthPercentage>), sizeof(Rect<LengthPercentageAuto>), sizeof(Rect<Dimension>),
            sizeof(AlignContentKeyword), sizeof(AlignItemsKeyword), sizeof(AlignmentSafety),
            sizeof(AlignContent), sizeof(AlignItems), sizeof(std::optional<AlignItems>),
            sizeof(FlexDirection), sizeof(FlexWrap), sizeof(GridAutoFlow),
            sizeof(MinTrackSizingFunction), sizeof(MaxTrackSizingFunction), sizeof(TrackSizingFunction),
            sizeof(std::vector<TrackSizingFunction>), sizeof(std::vector<GridTemplateComponent>),
            sizeof(GridTemplateComponent), sizeof(GridPlacement), sizeof(Line<GridPlacement>), sizeof(Style)};
        require(id < 36, "invalid type ID");
        if (id >= sizes.size()) pending("incompatible: native public Style has no Arc<str> specialization");
        // Reviewed native regression snapshot, never an ABI return substitution.
        // Other standard libraries may use different layouts: use independently
        // curated pointer-width ceilings and exact snapshots only where measured.
        static_assert([&] {
            for (size_t i = 0; i < sizes.size(); ++i) {
                if (sizes[i] == 0 || sizes[i] > native_size_budgets[i]) return false;
            }
            return true;
        }(),
            "native type exceeded independently curated size budget");
#if defined(__EMSCRIPTEN__) && defined(__wasm32__) && defined(_LIBCPP_VERSION)
        static_assert(sizes == std::array<size_t,32>{
            1,1,1,1,4,12,12,12,24,24,24,48,48,48,1,1,1,2,2,3,
            1,1,1,12,12,24,12,12,32,20,40,532},
            "review intentional Emscripten wasm32 libc++ representation changes");
#elif (defined(__APPLE__) && defined(__aarch64__) && defined(_LIBCPP_VERSION)) || \
      (defined(__EMSCRIPTEN__) && defined(__wasm64__) && defined(_LIBCPP_VERSION))
        static_assert(sizes == std::array<size_t,32>{
            1,1,1,1,4,16,16,16,32,32,32,64,64,64,1,1,1,2,2,3,
            1,1,1,16,16,32,24,24,64,32,64,776},
            "review intentional Apple arm64 / Emscripten wasm64 libc++ representation changes");
#endif
        put(output, sizes[id]);
    });
}
int32_t ps_misc_compact_bits(size_t*, size_t) {
    return run([] { pending("unsupported-representation: native lengths have no Rust compact tag bit masks"); });
}
int32_t ps_misc_compact_tag(uint32_t kind, size_t* output) {
    return run([&] {
        // This is the actual native discriminant, NOT a Rust pointer tag. Bitmask
        // tests must remain incompatible even if uniqueness happens to hold.
        with_length(kind, 0, nullptr, [&](const auto& v) { put(output, static_cast<size_t>(v.kind())); return 0; });
    });
}
int32_t ps_misc_compact(uint32_t kind, float payload, size_t, const void* pointer,
                        size_t* tag, uint32_t* flags, const void** extracted) {
    return run([&] {
        require(tag && flags && extracted, "null output");
        if (kind == 11 || kind == 12)
            require(pointer && (reinterpret_cast<std::uintptr_t>(pointer) & 7) == 0, "calc pointer must be nonnull and 8-byte aligned");
        with_length(kind, payload, pointer, [&](const auto& v) {
            const std::array predicates{v.is_length_or_percentage(), v.is_sizing_keyword(), v.uses_percentage(),
                v.is_plain_calc(), v.is_fit_content_calc(), v.has_calc(), v.is_fit_content(), v.is_intrinsic(),
                v.is_max_or_fit_content(), v.is_max_content_alike(), v.is_auto()};
            *tag = static_cast<size_t>(v.kind()); *flags = 0;
            for (size_t i = 0; i < predicates.size(); ++i) *flags |= uint32_t(predicates[i]) << i;
            *extracted = v.has_calc() ? reinterpret_cast<const void*>(v.calc_value().handle) : nullptr;
            if (fault(1)) *extracted = nullptr;
            return 0;
        });
    });
}
int32_t ps_misc_has_serde(uint32_t* output) {
    return run([&] { put(output, uint32_t(TAFFY_PRIVATE_VALUE_JSON)); });
}
int32_t ps_misc_alignment_json(uint32_t content, uint32_t operation, uint32_t keyword, uint32_t safety_code,
                               const uint8_t* json, size_t len, uint8_t* output, size_t capacity,
                               size_t* written, uint32_t* out_keyword, uint32_t* out_safety) {
    return run([&] {
        require(content <= 1 && operation <= 1, "invalid JSON operation");
        input(json, len);
        require(output && written && out_keyword && out_safety, "null output");
#if TAFFY_PRIVATE_VALUE_JSON
        const auto name = [&](uint32_t k, uint32_t s) {
            require(k < 10, "invalid keyword");
            return content ? wire_name(AlignContent{content_keywords[k], safety(s)}, content_keywords, content_names) :
                wire_name(AlignItems{item_keywords[k], safety(s)}, item_keywords, item_names);
        };
        std::string text;
        if (operation == 0) {
            text = name(keyword, safety_code);
        } else {
            require(len > 0, "empty JSON input");
            const auto decoded = nlohmann::json::parse(json, json + len).get<std::string>();
            bool found = false;
            // Only canonical upstream spellings decode. Non-position keywords
            // canonicalize to Default safety rather than inventing SafeNormal.
            for (uint32_t s = 0; s < 3 && !found; ++s) for (uint32_t k = 0; k < 10; ++k) {
                if (name(k, s) == decoded) { keyword = k; safety_code = s; text = decoded; found = true; break; }
            }
            require(found, "unknown alignment variant");
        }
        const auto serialized = nlohmann::json(text).dump();
        require(capacity >= serialized.size(), "output capacity");
        std::memcpy(output, serialized.data(), serialized.size());
        *written = serialized.size(); *out_keyword = keyword; *out_safety = safety_code;
#else
        (void)keyword; (void)safety_code; (void)capacity;
        pending("pending: test-only serde counterpart requires nlohmann/json v3.11.3 include path");
#endif
    });
}
} // extern C

// Optional supplemental checks, separate from the unchanged source-suite cases.
// They do not turn a Rust-storage assertion into a C++ pass. Enable in a native
// Catch2 test target with -DTAFFY_PRIVATE_NATIVE_COUNTERCHECKS.
#ifdef TAFFY_PRIVATE_NATIVE_COUNTERCHECKS
#include <catch2/catch_test_macros.hpp>
TEST_CASE("native private values::representation budgets and predicates", "[native-counterpart]") {
    // Same independent native ceilings as the adapter's compile-time checks.
    constexpr auto budgets = native_size_budgets;
    for (uint32_t id = 0; id < budgets.size(); ++id) {
        CAPTURE(id); size_t bytes = 0;
        REQUIRE(ps_misc_type_size(id, &bytes) == 0);
        REQUIRE(bytes > 0); REQUIRE(bytes <= budgets[id]);
    }
    REQUIRE(alignof(taffy::Dimension) >= alignof(taffy::Calc));
    REQUIRE(alignof(taffy::LengthPercentage) >= alignof(float));
    alignas(8) std::array<unsigned char, 8> storage{};
    for (uint32_t kind = 0; kind < 13; ++kind) {
        CAPTURE(kind); size_t tag = 0; uint32_t flags = 0; const void* extracted = nullptr;
        REQUIRE(ps_misc_compact(kind, .5f, 0, storage.data(), &tag, &flags, &extracted) == 0);
        REQUIRE(bool(flags & 1) == (kind == 0 || kind == 9));
        REQUIRE(bool(flags & 2) == (kind == 3 || kind == 4 || kind == 5 || kind == 6 || kind == 7 || kind == 10 || kind == 12));
        REQUIRE(bool(flags & 4) == (kind == 9 || kind == 10 || kind == 11 || kind == 12));
        REQUIRE(extracted == ((kind == 11 || kind == 12) ? static_cast<const void*>(storage.data()) : nullptr));
    }
    size_t tag = 0; uint32_t flags = 0; const void* extracted = nullptr;
    REQUIRE(ps_misc_compact(11, 0, 0, storage.data() + 1, &tag, &flags, &extracted) == 1);
    REQUIRE(ps_misc_compact(11, 0, 0, nullptr, &tag, &flags, &extracted) == 1);
}
TEST_CASE("native private values::builder move-only ownership and poison", "[native-counterpart]") {
    taffy::FrontBackVecBuilder<std::unique_ptr<int>> owned(2);
    owned.push_back(std::make_unique<int>(9)); owned.push_front(std::make_unique<int>(3));
    auto [values, front] = owned.into_vec();
    REQUIRE(front == 1); REQUIRE(*values[0] == 3); REQUIRE(*values[1] == 9);
    REQUIRE_THROWS_AS(owned.into_vec(), taffy::FrontBackVecBuilder<std::unique_ptr<int>>::Panic);
    taffy::FrontBackVecBuilder<int> overlap(1);
    overlap.push_back(1); overlap.push_front(2);
    REQUIRE_THROWS_AS(overlap.into_vec(), taffy::FrontBackVecBuilder<int>::Panic);
    REQUIRE_THROWS_AS(overlap.push_front(3), taffy::FrontBackVecBuilder<int>::Panic);
    taffy::FrontBackVecBuilder<int> zero(0);
    REQUIRE_THROWS_AS(zero.push_back(1), taffy::FrontBackVecBuilder<int>::Panic);
}
#endif
