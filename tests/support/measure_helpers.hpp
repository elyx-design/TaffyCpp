#ifndef TAFFY_TEST_MEASURE_HELPERS_HPP
#define TAFFY_TEST_MEASURE_HELPERS_HPP

#include "rust_backend.hpp"

#include <cstddef>
#include <string>
#include <variant>

namespace taffy::test {

enum class WritingMode { Horizontal, Vertical };
struct ZeroMeasureData {};
struct AspectRatioMeasureData {
    float width;
    float height_ratio;
};
// Caller supplies valid UTF-8, as with Rust String; lengths are bytes, not
// Unicode scalar counts. Embedded NUL is preserved; no C-string semantics.
struct AhemTextMeasureData {
    std::string text_content;
    WritingMode writing_mode;
};
using TestMeasureData =
    std::variant<ZeroMeasureData, OracleSize, AspectRatioMeasureData, AhemTextMeasureData>;

struct TestNodeContext {
    std::size_t count = 0;
    TestMeasureData measure_data = ZeroMeasureData{};

    [[nodiscard]] static TestNodeContext zero();
    [[nodiscard]] static TestNodeContext fixed(float width, float height);
    [[nodiscard]] static TestNodeContext aspect_ratio(float width, float height_ratio);
    [[nodiscard]] static TestNodeContext ahem_text(std::string text,
                                                   WritingMode mode = WritingMode::Horizontal);
};

// Context is borrowed and must outlive each call (including callbacks bound by
// the caller). Count increments only inside the native leaf intrinsic callback,
// after the both-known and absent-context short circuits, exactly as upstream.
[[nodiscard]] OracleLayoutOutput test_measure_function(const OracleLayoutInput& input,
                                                       RustBackend::NodeId node,
                                                       TestNodeContext* context,
                                                       const OracleStyle& style);

} // namespace taffy::test
#endif
