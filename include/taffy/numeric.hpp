#pragma once

/// @file
/// Scalar and optional arithmetic with the layout engine's NaN, rounding and
/// saturation rules. Most applications can use Style without calling these helpers.

#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace taffy {

/// Minimum ignoring a single NaN, following Rust f32::min (both NaN yields NaN).
[[nodiscard]] inline float f32_min(float a, float b) noexcept;
/// Maximum ignoring a single NaN, following Rust f32::max (both NaN yields NaN).
[[nodiscard]] inline float f32_max(float a, float b) noexcept;
/// Round to an integral float, with halfway cases rounded away from zero.
[[nodiscard]] inline float round(float value) noexcept;
/// Explicitly named alias of round(), including its halfway-away-from-zero rule.
[[nodiscard]] inline float round_ties_away(float value) noexcept;
/// Layout pixel rounding: floor(value + 0.5f), including that exact addition order.
/// Unlike round(), negative halfway cases round toward positive infinity.
[[nodiscard]] inline float layout_round(float value) noexcept;
/// Greatest integral float not greater than value; NaN/infinity follow std::floor.
[[nodiscard]] inline float floor(float value) noexcept;
/// Least integral float not less than value; NaN/infinity follow std::ceil.
[[nodiscard]] inline float ceil(float value) noexcept;
/// Floating absolute value, including conversion of negative zero to positive zero.
[[nodiscard]] inline float abs(float value) noexcept;

/** @brief Rust-style float-to-integer conversion (excluding bool).
 * Truncates toward zero, saturates at either integer bound, and maps NaN to zero.
 * Exact powers of two guard the cast: a rounded floating representation of an
 * integer maximum must never reach an out-of-range C++ integer conversion.
 */
template <std::integral I, std::floating_point F>
    requires(!std::same_as<I, bool>)
[[nodiscard]] inline I saturating_cast(F value) noexcept;
/// Alias of saturating_cast(), with identical truncation, saturation and NaN rules.
template <std::integral I, std::floating_point F>
    requires(!std::same_as<I, bool>)
[[nodiscard]] inline I saturating_float_to_int(F value) noexcept;

/// Minimum using f32_min() NaN semantics.
[[nodiscard]] inline float maybe_min(float a, float b) noexcept;
/// Maximum using f32_max() NaN semantics.
[[nodiscard]] inline float maybe_max(float a, float b) noexcept;
/// Floating addition without filtering NaN or infinity.
[[nodiscard]] inline float maybe_add(float a, float b) noexcept;
/// Floating subtraction without filtering NaN or infinity.
[[nodiscard]] inline float maybe_sub(float a, float b) noexcept;
/** @brief Apply the upper bound first, then the lower bound.
 * Exactly f32_max(f32_min(a, maximum), minimum), not std::clamp: inverted
 * bounds are permitted, and a single NaN is ignored by each min/max step.
 */
[[nodiscard]] inline float maybe_clamp(float a, float minimum, float maximum) noexcept;

/** @name Optional scalar arithmetic
 * An absent right operand leaves the left operand unchanged. An absent left
 * operand stays absent, even with a present right operand. A present NaN is a
 * value, not absence; scalar min/max NaN rules still apply.
 * @{ */
/// Minimum with an optional bound; no bound means identity.
[[nodiscard]] inline float maybe_min(float a, std::optional<float> b) noexcept;
/// Minimum preserving an absent left operand.
[[nodiscard]] inline std::optional<float> maybe_min(std::optional<float> a, float b) noexcept;
/// Minimum with optional operands, preserving absence on the left.
[[nodiscard]] inline std::optional<float> maybe_min(std::optional<float> a,
                                                    std::optional<float> b) noexcept;
/// Maximum with an optional bound; no bound means identity.
[[nodiscard]] inline float maybe_max(float a, std::optional<float> b) noexcept;
/// Maximum preserving an absent left operand.
[[nodiscard]] inline std::optional<float> maybe_max(std::optional<float> a, float b) noexcept;
/// Maximum with optional operands, preserving absence on the left.
[[nodiscard]] inline std::optional<float> maybe_max(std::optional<float> a,
                                                    std::optional<float> b) noexcept;
/// Add only when the right operand is present.
[[nodiscard]] inline float maybe_add(float a, std::optional<float> b) noexcept;
/// Addition preserving an absent left operand.
[[nodiscard]] inline std::optional<float> maybe_add(std::optional<float> a, float b) noexcept;
/// Addition with optional operands, preserving absence on the left.
[[nodiscard]] inline std::optional<float> maybe_add(std::optional<float> a,
                                                    std::optional<float> b) noexcept;
/// Subtract only when the right operand is present.
[[nodiscard]] inline float maybe_sub(float a, std::optional<float> b) noexcept;
/// Subtraction preserving an absent left operand.
[[nodiscard]] inline std::optional<float> maybe_sub(std::optional<float> a, float b) noexcept;
/// Subtraction with optional operands, preserving absence on the left.
[[nodiscard]] inline std::optional<float> maybe_sub(std::optional<float> a,
                                                    std::optional<float> b) noexcept;
/// Apply present bounds only, upper first and lower second.
[[nodiscard]] inline float maybe_clamp(float a, std::optional<float> minimum,
                                       std::optional<float> maximum) noexcept;
/// Clamp a present value using present bounds; an absent value remains absent.
[[nodiscard]] inline std::optional<float> maybe_clamp(std::optional<float> a,
                                                      std::optional<float> minimum,
                                                      std::optional<float> maximum) noexcept;
/// Clamp a present value using scalar bounds; an absent value remains absent.
[[nodiscard]] inline std::optional<float> maybe_clamp(std::optional<float> a, float minimum,
                                                      float maximum) noexcept;

/** @} */

// Structural participation avoids a dependency on style.hpp.
namespace detail {
template <class T>
concept available_space_like = requires(T a) {
    { a.is_definite() } -> std::convertible_to<bool>;
    { a.into_option() } -> std::same_as<std::optional<float>>;
};
} // namespace detail

/** @name Available-space arithmetic
 * T must expose is_definite() and into_option() returning optional<float>, plus
 * T::definite(float), or T::from_length(float) when definite() is unavailable.
 * The chosen factory must return a value convertible to T. Intrinsic values are
 * preserved except that maybe_min() with a definite right bound becomes definite.
 * @{ */
/// Bound definite space with f32_min(); intrinsic space becomes definite b.
template <detail::available_space_like T>
[[nodiscard]] T maybe_min(T a, float b);
/// Apply f32_max() to definite space; leave intrinsic space unchanged.
template <detail::available_space_like T>
[[nodiscard]] T maybe_max(T a, float b);
/// Add to definite space; leave intrinsic space unchanged.
template <detail::available_space_like T>
[[nodiscard]] T maybe_add(T a, float b);
/// Subtract from definite space; leave intrinsic space unchanged.
template <detail::available_space_like T>
[[nodiscard]] T maybe_sub(T a, float b);
/// Apply maybe_min() only when b is present, otherwise return a unchanged.
template <detail::available_space_like T>
[[nodiscard]] T maybe_min(T a, std::optional<float> b);
/// Apply maybe_max() only when b is present, otherwise return a unchanged.
template <detail::available_space_like T>
[[nodiscard]] T maybe_max(T a, std::optional<float> b);
/// Apply maybe_add() only when b is present, otherwise return a unchanged.
template <detail::available_space_like T>
[[nodiscard]] T maybe_add(T a, std::optional<float> b);
/// Apply maybe_sub() only when b is present, otherwise return a unchanged.
template <detail::available_space_like T>
[[nodiscard]] T maybe_sub(T a, std::optional<float> b);
/// Clamp definite space; intrinsic space stays unchanged. Bounds must be accepted
/// by scalar maybe_clamp(float, minimum, maximum), e.g. floats or optional<float>s.
template <detail::available_space_like T, class L, class H>
[[nodiscard]] T maybe_clamp(T a, L minimum, H maximum);
/** @} */

} // namespace taffy

// Template bodies must be visible to callers. Unlike nontrivial library methods,
// tiny arithmetic kernels stay inline for hot-loop performance.
#include <taffy/detail/numeric_operations.hpp>
