#pragma once

// Included by numeric.hpp after the API declarations. These definitions must be
// installed with the header; they are not separate public entry points. Unlike
// nontrivial library methods, tiny arithmetic stays inline for hot-loop performance.
namespace taffy {

[[nodiscard]] inline float f32_min(float a, float b) noexcept {
    return std::fmin(a, b);
}

[[nodiscard]] inline float f32_max(float a, float b) noexcept {
    return std::fmax(a, b);
}

[[nodiscard]] inline float round(float value) noexcept {
    return std::round(value);
}

[[nodiscard]] inline float round_ties_away(float value) noexcept {
    return std::round(value);
}

// Pinned upstream util::sys::round with the std feature enabled.
[[nodiscard]] inline float layout_round(float value) noexcept {
    return std::floor(value + 0.5f);
}

[[nodiscard]] inline float floor(float value) noexcept {
    return std::floor(value);
}

[[nodiscard]] inline float ceil(float value) noexcept {
    return std::ceil(value);
}

[[nodiscard]] inline float abs(float value) noexcept {
    return std::fabs(value);
}

[[nodiscard]] inline float maybe_min(float a, float b) noexcept {
    return f32_min(a, b);
}

[[nodiscard]] inline float maybe_max(float a, float b) noexcept {
    return f32_max(a, b);
}

[[nodiscard]] inline float maybe_add(float a, float b) noexcept {
    return a + b;
}

[[nodiscard]] inline float maybe_sub(float a, float b) noexcept {
    return a - b;
}

[[nodiscard]] inline float maybe_clamp(float a, float minimum, float maximum) noexcept {
    return f32_max(f32_min(a, maximum), minimum);
}

#define TAFFY_OPTIONAL_MATH(NAME)                                                                  \
    [[nodiscard]] inline float NAME(float a, std::optional<float> b) noexcept {                    \
        return b ? NAME(a, *b) : a;                                                                \
    }                                                                                              \
    [[nodiscard]] inline std::optional<float> NAME(std::optional<float> a, float b) noexcept {     \
        return a ? std::optional<float>{NAME(*a, b)} : std::nullopt;                               \
    }                                                                                              \
    [[nodiscard]] inline std::optional<float> NAME(std::optional<float> a,                         \
                                                   std::optional<float> b) noexcept {              \
        return a ? std::optional<float>{NAME(*a, b)} : std::nullopt;                               \
    }
TAFFY_OPTIONAL_MATH(maybe_min)
TAFFY_OPTIONAL_MATH(maybe_max)
TAFFY_OPTIONAL_MATH(maybe_add)
TAFFY_OPTIONAL_MATH(maybe_sub)
#undef TAFFY_OPTIONAL_MATH

[[nodiscard]] inline float maybe_clamp(float a, std::optional<float> minimum,
                                       std::optional<float> maximum) noexcept {
    return maybe_max(maybe_min(a, maximum), minimum);
}

[[nodiscard]] inline std::optional<float> maybe_clamp(std::optional<float> a,
                                                      std::optional<float> minimum,
                                                      std::optional<float> maximum) noexcept {
    return a ? std::optional<float>{maybe_clamp(*a, minimum, maximum)} : std::nullopt;
}

[[nodiscard]] inline std::optional<float> maybe_clamp(std::optional<float> a, float minimum,
                                                      float maximum) noexcept {
    return a ? std::optional<float>{maybe_clamp(*a, minimum, maximum)} : std::nullopt;
}

template <std::integral I, std::floating_point F>
    requires(!std::same_as<I, bool>)
[[nodiscard]] inline I saturating_cast(F value) noexcept {
    if (std::isnan(value))
        return I{0};
    const F upper = std::ldexp(F{1}, std::numeric_limits<I>::digits);
    if (value >= upper)
        return (std::numeric_limits<I>::max)(); // Parenthesized against <windows.h> max().
    if constexpr (std::is_signed_v<I>) {
        if (value <= -upper)
            return std::numeric_limits<I>::lowest();
    } else {
        if (value <= F{0})
            return I{0};
    }
    return static_cast<I>(value);
}

template <std::integral I, std::floating_point F>
    requires(!std::same_as<I, bool>)
[[nodiscard]] inline I saturating_float_to_int(F value) noexcept {
    return saturating_cast<I>(value);
}

namespace detail {
template <class T>
[[nodiscard]] T definite_space(float value) {
    if constexpr (requires { T::definite(value); })
        return T::definite(value);
    else
        return T::from_length(value);
}
} // namespace detail

template <detail::available_space_like T>
[[nodiscard]] T maybe_min(T a, float b) {
    auto value = a.into_option();
    return detail::definite_space<T>(value ? f32_min(*value, b) : b);
}

#define TAFFY_SPACE_MATH(NAME)                                                                     \
    template <detail::available_space_like T>                                                      \
    [[nodiscard]] T NAME(T a, float b) {                                                           \
        auto value = a.into_option();                                                              \
        return value ? detail::definite_space<T>(NAME(*value, b)) : a;                             \
    }
TAFFY_SPACE_MATH(maybe_max)
TAFFY_SPACE_MATH(maybe_add)
TAFFY_SPACE_MATH(maybe_sub)
#undef TAFFY_SPACE_MATH
#define TAFFY_SPACE_OPTIONAL_MATH(NAME)                                                            \
    template <detail::available_space_like T>                                                      \
    [[nodiscard]] T NAME(T a, std::optional<float> b) {                                            \
        return b ? NAME(a, *b) : a;                                                                \
    }
TAFFY_SPACE_OPTIONAL_MATH(maybe_min)
TAFFY_SPACE_OPTIONAL_MATH(maybe_max)
TAFFY_SPACE_OPTIONAL_MATH(maybe_add)
TAFFY_SPACE_OPTIONAL_MATH(maybe_sub)
#undef TAFFY_SPACE_OPTIONAL_MATH

template <detail::available_space_like T, class L, class H>
[[nodiscard]] T maybe_clamp(T a, L minimum, H maximum) {
    auto value = a.into_option();
    return value ? detail::definite_space<T>(maybe_clamp(*value, minimum, maximum)) : a;
}

} // namespace taffy
