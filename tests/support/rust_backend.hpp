#ifndef TAFFY_TEST_RUST_BACKEND_HPP
#define TAFFY_TEST_RUST_BACKEND_HPP

#include "oracle_abi.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>

namespace taffy::test {

// An ABI error, including a caught Rust panic. C++ callback exceptions retain
// their original type instead of being translated into this exception.
class RustBackendError final : public std::runtime_error {
  public:
    RustBackendError(std::uint32_t status, const char* operation);
    [[nodiscard]] std::uint32_t status() const noexcept {
        return status_;
    }

  private:
    std::uint32_t status_;
};

// Test-only oracle. Calls on a context must be serialized and must not reenter
// that context from a callback. The independent static leaf() is safe there.
class RustBackend final {
  public:
    using NodeId = std::uint64_t;
    using Measure =
        std::function<OracleLayoutOutput(NodeId, const OracleLayoutInput&, const OracleStyle&)>;
    using Intrinsic = std::function<OracleSize(OracleKnown, OracleSizing)>;

    RustBackend();
    ~RustBackend() noexcept;
    RustBackend(const RustBackend&) = delete;
    RustBackend& operator=(const RustBackend&) = delete;
    RustBackend(RustBackend&&) = delete;
    RustBackend& operator=(RustBackend&&) = delete;

    [[nodiscard]] static OracleStyle default_style();
    [[nodiscard]] NodeId new_node(const OracleStyle& style, Measure measure = {});
    // An empty callback clears the node's context.
    void set_context(NodeId node, Measure measure = {});
    void remove(NodeId node);
    void clear();
    // Replaces the scalar style and clears separately configured columns.
    void set_style(NodeId node, const OracleStyle& style);
    void set_grid_template_columns(NodeId node, std::span<const OracleGridTrack> columns);
    [[nodiscard]] std::vector<OracleGridTrack> grid_template_columns(NodeId node);
    [[nodiscard]] static bool position_is_positioned(std::uint32_t position);
    [[nodiscard]] static bool position_is_out_of_flow(std::uint32_t position);
    [[nodiscard]] OracleStyle get_style(NodeId node);
    void set_children(NodeId node, std::span<const NodeId> children);
    // The ABI uses zero for "no parent".
    [[nodiscard]] NodeId parent(NodeId node);
    [[nodiscard]] NodeId child(NodeId node, std::size_t index);
    [[nodiscard]] std::size_t child_count(NodeId node);
    void mark_dirty(NodeId node);
    [[nodiscard]] bool dirty(NodeId node);
    void rounding(bool enabled);
    void compute(NodeId root, OracleSizing available_space);
    [[nodiscard]] OracleLayout layout(NodeId node, bool unrounded = false);
    [[nodiscard]] float scroll_width(NodeId node);
    [[nodiscard]] float scroll_height(NodeId node);
    [[nodiscard]] static OracleLayoutOutput
    leaf(const OracleStyle& style, const OracleLayoutInput& input, Intrinsic intrinsic);
    void test_panic();

  private:
    struct Cookie;
    struct LeafState;
    OracleContext* context_ = nullptr;
    std::exception_ptr callback_exception_;

    void check(std::uint32_t status, const char* operation);
    void capture_exception() noexcept;
    static std::uint32_t measure_callback(void* user, std::uint64_t node,
                                          const OracleLayoutInput* input, const OracleStyle* style,
                                          OracleLayoutOutput* output) noexcept;
    static void drop_callback(void* user) noexcept;
    static std::uint32_t intrinsic_callback(void* user, OracleKnown known, OracleSizing available,
                                            OracleSize* output) noexcept;
};

} // namespace taffy::test

#endif
