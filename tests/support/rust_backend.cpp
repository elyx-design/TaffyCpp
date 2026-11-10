#include "rust_backend.hpp"

#include <memory>
#include <string>
#include <utility>

namespace taffy::test {
namespace {

void check_status(std::uint32_t status, const char* operation) {
    if (status != ORACLE_OK) {
        throw RustBackendError(status, operation);
    }
}

} // namespace

struct RustBackend::Cookie {
    RustBackend* owner;
    Measure measure;
};

struct RustBackend::LeafState {
    Intrinsic intrinsic;
    std::exception_ptr exception;
};

RustBackendError::RustBackendError(std::uint32_t status, const char* operation)
    : std::runtime_error(std::string(operation) + " failed with oracle status " +
                         std::to_string(status)),
      status_(status) {}

RustBackend::RustBackend() {
    check_status(oracle_create(&context_), "oracle_create");
}

RustBackend::~RustBackend() noexcept {
    // Free also drops node cookies on poisoned contexts. The owner and its
    // exception slot remain alive until all drop callbacks have completed.
    if (context_ != nullptr) {
        (void)oracle_free(context_);
    }
}

void RustBackend::capture_exception() noexcept {
    // Keep the original failure if Rust invokes further callbacks on its way out.
    if (!callback_exception_) {
        callback_exception_ = std::current_exception();
    }
}

void RustBackend::check(std::uint32_t status, const char* operation) {
    if (callback_exception_) {
        std::rethrow_exception(std::exchange(callback_exception_, {}));
    }
    check_status(status, operation);
}

std::uint32_t RustBackend::measure_callback(void* user, std::uint64_t node,
                                            const OracleLayoutInput* input,
                                            const OracleStyle* style,
                                            OracleLayoutOutput* output) noexcept {
    auto& cookie = *static_cast<Cookie*>(user);
    try {
        *output = cookie.measure(node, *input, *style);
        return ORACLE_OK;
    } catch (...) {
        cookie.owner->capture_exception();
        return ORACLE_CALLBACK_FAILURE;
    }
}

void RustBackend::drop_callback(void* user) noexcept {
    auto* cookie = static_cast<Cookie*>(user);
    if (cookie == nullptr) {
        return;
    }
    auto* owner = cookie->owner;
    try {
        delete cookie;
    } catch (...) {
        owner->capture_exception();
    }
}

std::uint32_t RustBackend::intrinsic_callback(void* user, OracleKnown known, OracleSizing available,
                                              OracleSize* output) noexcept {
    auto& state = *static_cast<LeafState*>(user);
    try {
        *output = state.intrinsic(known, available);
        return ORACLE_OK;
    } catch (...) {
        if (!state.exception) {
            state.exception = std::current_exception();
        }
        return ORACLE_CALLBACK_FAILURE;
    }
}

OracleStyle RustBackend::default_style() {
    OracleStyle result{};
    check_status(oracle_default_style(&result), "oracle_default_style");
    return result;
}

RustBackend::NodeId RustBackend::new_node(const OracleStyle& style, Measure measure) {
    std::unique_ptr<Cookie> cookie;
    if (measure) {
        cookie = std::make_unique<Cookie>(this, std::move(measure));
    }
    NodeId result{};
    const auto status =
        oracle_new_node(context_, &style, cookie.get(), cookie ? measure_callback : nullptr,
                        cookie ? drop_callback : nullptr, &result);
    // Transfer before checking exceptions: a successful ABI call owns the
    // cookie even if a callback reported a C++ exception during that call.
    if (status == ORACLE_OK) {
        (void)cookie.release();
    }
    check(status, "oracle_new_node");
    return result;
}

void RustBackend::set_context(NodeId node, Measure measure) {
    std::unique_ptr<Cookie> cookie;
    if (measure) {
        cookie = std::make_unique<Cookie>(this, std::move(measure));
    }
    const auto status =
        oracle_set_context(context_, node, cookie.get(), cookie ? measure_callback : nullptr,
                           cookie ? drop_callback : nullptr);
    if (status == ORACLE_OK) {
        (void)cookie.release();
    }
    check(status, "oracle_set_context");
}

void RustBackend::remove(NodeId node) {
    check(oracle_remove(context_, node), "oracle_remove");
}

void RustBackend::clear() {
    check(oracle_clear(context_), "oracle_clear");
}

void RustBackend::set_style(NodeId node, const OracleStyle& style) {
    check(oracle_set_style(context_, node, &style), "oracle_set_style");
}

void RustBackend::set_grid_template_columns(NodeId node, std::span<const OracleGridTrack> columns) {
    check(oracle_set_grid_template_columns(context_, node, columns.data(), columns.size()),
          "oracle_set_grid_template_columns");
}

std::vector<OracleGridTrack> RustBackend::grid_template_columns(NodeId node) {
    std::size_t count{};
    check(oracle_get_grid_template_columns(context_, node, nullptr, 0, &count),
          "oracle_get_grid_template_columns");
    std::vector<OracleGridTrack> result(count);
    check(oracle_get_grid_template_columns(context_, node, result.data(), result.size(), &count),
          "oracle_get_grid_template_columns");
    return result;
}

bool RustBackend::position_is_positioned(std::uint32_t position) {
    std::uint32_t result{};
    check_status(oracle_position_is_positioned(position, &result), "oracle_position_is_positioned");
    return result != 0;
}

bool RustBackend::position_is_out_of_flow(std::uint32_t position) {
    std::uint32_t result{};
    check_status(oracle_position_is_out_of_flow(position, &result),
                 "oracle_position_is_out_of_flow");
    return result != 0;
}

OracleStyle RustBackend::get_style(NodeId node) {
    OracleStyle result{};
    check(oracle_get_style(context_, node, &result), "oracle_get_style");
    return result;
}

void RustBackend::set_children(NodeId node, std::span<const NodeId> children) {
    check(oracle_set_children(context_, node, children.data(), children.size()),
          "oracle_set_children");
}

RustBackend::NodeId RustBackend::parent(NodeId node) {
    NodeId result{};
    check(oracle_parent(context_, node, &result), "oracle_parent");
    return result;
}

RustBackend::NodeId RustBackend::child(NodeId node, std::size_t index) {
    NodeId result{};
    check(oracle_child(context_, node, index, &result), "oracle_child");
    return result;
}

std::size_t RustBackend::child_count(NodeId node) {
    std::size_t result{};
    check(oracle_child_count(context_, node, &result), "oracle_child_count");
    return result;
}

void RustBackend::mark_dirty(NodeId node) {
    check(oracle_mark_dirty(context_, node), "oracle_mark_dirty");
}

bool RustBackend::dirty(NodeId node) {
    std::uint32_t result{};
    check(oracle_dirty(context_, node, &result), "oracle_dirty");
    return result != 0;
}

void RustBackend::rounding(bool enabled) {
    check(oracle_rounding(context_, enabled ? 1u : 0u), "oracle_rounding");
}

void RustBackend::compute(NodeId root, OracleSizing available_space) {
    check(oracle_compute(context_, root, available_space), "oracle_compute");
}

OracleLayout RustBackend::layout(NodeId node, bool unrounded) {
    OracleLayout result{};
    check(oracle_layout(context_, node, unrounded ? 1u : 0u, &result), "oracle_layout");
    return result;
}

float RustBackend::scroll_width(NodeId node) {
    return layout(node).scroll_width;
}

float RustBackend::scroll_height(NodeId node) {
    return layout(node).scroll_height;
}

OracleLayoutOutput RustBackend::leaf(const OracleStyle& style, const OracleLayoutInput& input,
                                     Intrinsic intrinsic) {
    LeafState state{std::move(intrinsic), {}};
    OracleLayoutOutput result{};
    const auto status = oracle_leaf(&style, &input, &state, intrinsic_callback, &result);
    if (state.exception) {
        std::rethrow_exception(state.exception);
    }
    check_status(status, "oracle_leaf");
    return result;
}

void RustBackend::test_panic() {
    check(oracle_test_panic(context_), "oracle_test_panic");
}

} // namespace taffy::test
