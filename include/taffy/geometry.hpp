#pragma once

/// @file
/// Generic sizes, points, edge rectangles and axis selectors used by the public API.
/// Geometry uses physical x/y and width/height, not writing-mode-dependent axes.

#include "taffy/numeric.hpp"

#include <concepts>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace taffy {

/// Physical axes: Horizontal is x/width/left-right, Vertical is y/height/top-bottom.
enum class AbsoluteAxis {
    Horizontal,
    Vertical
};
/// Abstract axes; naive conversions assume horizontal writing (Inline=x, Block=y).
enum class AbstractAxis {
    Inline,
    Block
};

/// Return the perpendicular physical axis.
[[nodiscard]] constexpr AbsoluteAxis other_axis(AbsoluteAxis axis) noexcept {
    return axis == AbsoluteAxis::Horizontal ? AbsoluteAxis::Vertical : AbsoluteAxis::Horizontal;
}

/// Return the other abstract axis (Inline versus Block).
[[nodiscard]] constexpr AbstractAxis other(AbstractAxis axis) noexcept {
    return axis == AbstractAxis::Inline ? AbstractAxis::Block : AbstractAxis::Inline;
}

/// Map Inline to Horizontal and Block to Vertical without consulting writing mode.
[[nodiscard]] constexpr AbsoluteAxis as_abs_naive(AbstractAxis axis) noexcept {
    return axis == AbstractAxis::Inline ? AbsoluteAxis::Horizontal : AbsoluteAxis::Vertical;
}

namespace detail {
template <class T>
struct optional_value {
    static constexpr bool is_optional = false;
};

template <class T>
struct optional_value<std::optional<T>> {
    static constexpr bool is_optional = true;
    using type = T;
};
template <class T>
concept optional_type = optional_value<T>::is_optional;
template <class T>
using mapped_t = std::remove_cvref_t<T>;

template <class T>
[[nodiscard]] constexpr T zero_value() {
    if constexpr (requires { T::zero(); })
        return T::zero();
    else
        return T{0};
}
template <class T>
concept length_constructible = requires { T::from_length(0.0f); } || requires { T::length(0.0f); };

template <class T>
[[nodiscard]] constexpr T make_length(float value)
    requires length_constructible<T>
{
    if constexpr (requires { T::from_length(value); })
        return T::from_length(value);
    else
        return T::length(value);
}
template <class T>
concept percent_constructible =
    requires { T::from_percent(0.0f); } || requires { T::percent(0.0f); };

template <class T>
[[nodiscard]] constexpr T make_percent(float value)
    requires percent_constructible<T>
{
    if constexpr (requires { T::from_percent(value); })
        return T::from_percent(value);
    else
        return T::percent(value);
}

// Accept the style enum without including style.hpp; reverse directions intentionally
// do not swap edges (the upstream helpers operate on physical, not logical, edges).
template <class D>
[[nodiscard]] constexpr bool direction_is_row(D direction) {
    if constexpr (requires { direction.is_row(); })
        return direction.is_row();
    else
        return direction == D::Row || direction == D::RowReverse;
}
} // namespace detail

template <class T>
struct Size;
template <class T>
struct Point;
template <class T>
struct Line;
template <class T>
struct Rect;

namespace detail {
template <class T>
inline constexpr Size<T> size_zero{zero_value<T>(), zero_value<T>()};
template <class T>
inline constexpr Size<T> size_none{};
template <class T>
inline constexpr Point<T> point_zero{zero_value<T>(), zero_value<T>()};
template <class T>
inline constexpr Point<T> point_none{};
template <class T>
inline constexpr Rect<T> rect_zero{zero_value<T>(), zero_value<T>(), zero_value<T>(),
                                   zero_value<T>()};
template <class T>
inline constexpr Rect<T> rect_none{};
template <class T>
inline constexpr Line<T> line_true{T{true}, T{true}};
template <class T>
inline constexpr Line<T> line_false{T{false}, T{false}};
} // namespace detail

/** @brief Forward optional resolution to the scalar domain type.
 * The domain controls units, percentage bases, unresolved values and exceptions;
 * context and additional arguments are borrowed only for this call.
 */
template <class T, class Context, class... Args>
    requires requires(const T& value, const Context& context, const Args&... args) {
        value.maybe_resolve(context, args...);
    }
[[nodiscard]] auto maybe_resolve(const T& value, const Context& context, const Args&... args) {
    return value.maybe_resolve(context, args...);
}

/// Forward zero-fallback resolution to the scalar domain type, borrowing all inputs.
template <class T, class Context, class... Args>
    requires requires(const T& value, const Context& context, const Args&... args) {
        value.resolve_or_zero(context, args...);
    }
[[nodiscard]] auto resolve_or_zero(const T& value, const Context& context, const Args&... args) {
    return value.resolve_or_zero(context, args...);
}

/** @brief Independent owned values for both physical axes.
 * Default construction value-initializes T; this container imposes no units.
 */
template <class T>
struct InBothAbsAxis {
    T horizontal{}; ///< Value for the x/width axis.
    T vertical{};   ///< Value for the y/height axis.

    /// Borrow the component on the selected physical axis.
    [[nodiscard]] constexpr T& get(AbsoluteAxis axis) noexcept {
        return axis == AbsoluteAxis::Horizontal ? horizontal : vertical;
    }

    /// Borrow the immutable component on the selected physical axis.
    [[nodiscard]] constexpr const T& get(AbsoluteAxis axis) const noexcept {
        return axis == AbsoluteAxis::Horizontal ? horizontal : vertical;
    }

    /// Replace the component on the selected axis.
    constexpr void set(AbsoluteAxis axis, T value) {
        get(axis) = std::move(value);
    }

    /// Compare both components using T's equality.
    [[nodiscard]] constexpr bool operator==(const InBothAbsAxis&) const = default;
};

/** @brief Physical width/height pair with owned components and no imposed units.
 * Float layout sizes normally use layout units; style domains may instead hold
 * lengths, fractions (1 = 100%), or intrinsic constraints. Default fields are
 * value-initialized, not universally numeric zero. Optional absence is unknown,
 * not zero; a present NaN stays present. No sign/finite-value validation occurs.
 * Direction helpers select width for row and height for column; reverse direction
 * does not change that selection. Abstract-axis access assumes horizontal writing.
 * Mapping/resolution callbacks run synchronously and exceptions propagate; references
 * passed to callbacks borrow the original components for the duration of the call.
 */
template <class T>
struct Size {
    T width{};  ///< Horizontal extent/value.
    T height{}; ///< Vertical extent/value.
    /// Compare components exactly using T's equality (no approximate float comparison).
    [[nodiscard]] constexpr bool operator==(const Size&) const = default;
    static constexpr const Size& ZERO =
        detail::size_zero<T>; ///< Shared zero_value components (T::zero() or T{0}).
    static constexpr const Size& NONE =
        detail::size_none<T>; ///< Shared value-initialized components; empty for optional T.

    /// Construct two domain zeros, using T::zero() when available, otherwise T{0}.
    [[nodiscard]] static constexpr Size zero() {
        return {detail::zero_value<T>(), detail::zero_value<T>()};
    }

    /// Set both axes to the domain's intrinsic min-content constraint.
    [[nodiscard]] static constexpr Size min_content()
        requires requires { T::min_content(); }
    {
        return splat(T::min_content());
    }

    /// Set both axes to the domain's intrinsic max-content constraint.
    [[nodiscard]] static constexpr Size max_content()
        requires requires { T::max_content(); }
    {
        return splat(T::max_content());
    }

    /// Construct two absent optional components.
    [[nodiscard]] static constexpr Size none()
        requires detail::optional_type<T>
    {
        return {};
    }

    /// Construct width then height, moving each supplied value.
    [[nodiscard]] static constexpr Size new_(T width, T height) {
        return {std::move(width), std::move(height)};
    }

    /// Copy one value to both physical axes.
    [[nodiscard]] static constexpr Size splat(T value) {
        return {value, value};
    }

    /// Copy x to width and y to height without changing units.
    [[nodiscard]] static constexpr Size from_point(const Point<T>& point) {
        return {point.x, point.y};
    }

    /// Copy width to x and height to y without choosing a coordinate frame.
    [[nodiscard]] constexpr Point<T> to_point() const {
        return {width, height};
    }

    /// Swap width and height.
    [[nodiscard]] constexpr Size transpose() const {
        return {height, width};
    }

    /// Borrow width for Horizontal or height for Vertical.
    [[nodiscard]] constexpr T& get_abs(AbsoluteAxis axis) noexcept {
        return axis == AbsoluteAxis::Horizontal ? width : height;
    }

    /// Borrow the immutable component on the selected physical axis.
    [[nodiscard]] constexpr const T& get_abs(AbsoluteAxis axis) const noexcept {
        return axis == AbsoluteAxis::Horizontal ? width : height;
    }

    /// Mutable physical-axis alias of get_abs().
    [[nodiscard]] constexpr T& get(AbsoluteAxis axis) noexcept {
        return get_abs(axis);
    }

    /// Immutable physical-axis alias of get_abs().
    [[nodiscard]] constexpr const T& get(AbsoluteAxis axis) const noexcept {
        return get_abs(axis);
    }

    /// Borrow width for Inline or height for Block (horizontal writing assumption).
    [[nodiscard]] constexpr T& get(AbstractAxis axis) noexcept {
        return get_abs(as_abs_naive(axis));
    }

    /// Borrow an immutable component using the naive abstract-to-physical mapping.
    [[nodiscard]] constexpr const T& get(AbstractAxis axis) const noexcept {
        return get_abs(as_abs_naive(axis));
    }

    /// Replace one physical-axis component.
    constexpr void set(AbsoluteAxis axis, T value) {
        get_abs(axis) = std::move(value);
    }

    /// Replace one component using the naive abstract-to-physical mapping.
    constexpr void set(AbstractAxis axis, T value) {
        get(axis) = std::move(value);
    }

    /// Return a copy with one physical/abstract-axis component replaced.
    template <class Axis>
    [[nodiscard]] constexpr Size with(Axis axis, T value) const {
        auto result = *this;
        result.set(axis, std::move(value));
        return result;
    }

    /// Invoke f on width then height; own the decayed result values in a new size.
    template <class F>
    [[nodiscard]] constexpr auto map(F f) const {
        return Size<detail::mapped_t<std::invoke_result_t<F&, const T&>>>{std::invoke(f, width),
                                                                          std::invoke(f, height)};
    }

    /// Transform width only, retaining the original component type and height.
    template <class F>
    [[nodiscard]] constexpr Size map_width(F f) const {
        return {std::invoke(f, width), height};
    }

    /// Transform height only, retaining the original component type and width.
    template <class F>
    [[nodiscard]] constexpr Size map_height(F f) const {
        return {width, std::invoke(f, height)};
    }

    /// Invoke f on matching width/height pairs, owning the decayed result values.
    template <class U, class F>
    [[nodiscard]] constexpr auto zip_map(const Size<U>& rhs, F f) const {
        return Size<detail::mapped_t<std::invoke_result_t<F&, const T&, const U&>>>{
            std::invoke(f, width, rhs.width), std::invoke(f, height, rhs.height)};
    }

    /// Borrow width for row directions, height for column directions.
    template <class D>
    [[nodiscard]] constexpr const T& main(D direction) const {
        return detail::direction_is_row(direction) ? width : height;
    }

    /// Borrow height for row directions, width for column directions.
    template <class D>
    [[nodiscard]] constexpr const T& cross(D direction) const {
        return detail::direction_is_row(direction) ? height : width;
    }

    /// Replace the component on the direction's main axis.
    template <class D>
    constexpr void set_main(D direction, T value) {
        (detail::direction_is_row(direction) ? width : height) = std::move(value);
    }

    /// Replace the component on the direction's cross axis.
    template <class D>
    constexpr void set_cross(D direction, T value) {
        (detail::direction_is_row(direction) ? height : width) = std::move(value);
    }

    /// Copy this size and replace its main-axis component.
    template <class D>
    [[nodiscard]] constexpr Size with_main(D direction, T value) const {
        auto result = *this;
        result.set_main(direction, std::move(value));
        return result;
    }

    /// Copy this size and replace its cross-axis component.
    template <class D>
    [[nodiscard]] constexpr Size with_cross(D direction, T value) const {
        auto result = *this;
        result.set_cross(direction, std::move(value));
        return result;
    }

    /// Transform only the main-axis component, preserving the component type.
    template <class D, class F>
    [[nodiscard]] constexpr Size map_main(D direction, F f) const {
        return with_main(direction, std::invoke(f, main(direction)));
    }

    /// Transform only the cross-axis component, preserving the component type.
    template <class D, class F>
    [[nodiscard]] constexpr Size map_cross(D direction, F f) const {
        return with_cross(direction, std::invoke(f, cross(direction)));
    }

    /// Define only the optional cross-axis component, leaving the main axis absent.
    template <class D>
    [[nodiscard]] static constexpr Size from_cross(D direction, T value)
        requires detail::optional_type<T>
    {
        return Size{}.with_cross(direction, std::move(value));
    }

    /// Component-wise float minimum, ignoring a single NaN in each pair.
    [[nodiscard]] Size f32_min(const Size& rhs) const
        requires std::same_as<T, float>
    {
        return {taffy::f32_min(width, rhs.width), taffy::f32_min(height, rhs.height)};
    }

    /// Component-wise float maximum, ignoring a single NaN in each pair.
    [[nodiscard]] Size f32_max(const Size& rhs) const
        requires std::same_as<T, float>
    {
        return {taffy::f32_max(width, rhs.width), taffy::f32_max(height, rhs.height)};
    }

    /// True only if both components are strictly positive (NaN therefore returns false).
    [[nodiscard]] constexpr bool has_non_zero_area() const
        requires std::is_arithmetic_v<T>
    {
        return width > 0 && height > 0;
    }

    /// True if both optional components are present, even when their values are NaN.
    [[nodiscard]] constexpr bool both_axis_defined() const
        requires detail::optional_type<T>
    {
        return width.has_value() && height.has_value();
    }

    /// Unwrap each present component or copy the corresponding fallback component.
    template <class U>
    [[nodiscard]] constexpr Size<U> unwrap_or(const Size<U>& alt) const
        requires std::same_as<T, std::optional<U>>
    {
        return {width.value_or(alt.width), height.value_or(alt.height)};
    }

    /// Keep present components, filling absent ones from alt ('or' is a C++ keyword).
    [[nodiscard]] constexpr Size or_else(const Size& alt) const
        requires detail::optional_type<T>
    {
        return {width ? width : alt.width, height ? height : alt.height};
    }

    /// Alias of or_else() with component-wise, left-biased optional fallback.
    [[nodiscard]] constexpr Size or_(const Size& alt) const
        requires detail::optional_type<T>
    {
        return or_else(alt);
    }

    /** @brief Fill exactly one missing dimension using ratio = width / height.
     * No ratio, both present, or both absent leaves the size unchanged. The ratio
     * is not validated: zero, negative, NaN and infinity use ordinary float arithmetic.
     */
    [[nodiscard]] constexpr Size maybe_apply_aspect_ratio(std::optional<float> ratio) const
        requires std::same_as<T, std::optional<float>>
    {
        if (!ratio)
            return *this;
        if (width && !height)
            return {width, *width / *ratio};
        if (!width && height)
            return {*height * *ratio, height};
        return *this;
    }

    /// Construct domain lengths from physical width/height in the domain's length units.
    [[nodiscard]] static constexpr Size from_lengths(float width, float height)
        requires detail::length_constructible<T>
    {
        return {detail::make_length<T>(width), detail::make_length<T>(height)};
    }

    /// Construct domain percentages from fractions (1 = 100%), without validation.
    [[nodiscard]] static constexpr Size from_percent(float width, float height)
        requires detail::percent_constructible<T>
    {
        return {detail::make_percent<T>(width), detail::make_percent<T>(height)};
    }

    /// Set both components to the domain's auto value.
    [[nodiscard]] static constexpr Size auto_()
        requires requires { T::auto_(); }
    {
        return {T::auto_(), T::auto_()};
    }

    /// Convert each domain value through into_option(); intrinsic/auto rules belong to T.
    [[nodiscard]] constexpr auto into_options() const
        requires requires(T t) { t.into_option(); }
    {
        return map([](const T& t) {
            return t.into_option();
        });
    }

    /// Forward each optional replacement to the corresponding domain value's maybe_set().
    [[nodiscard]] constexpr Size maybe_set(const Size<std::optional<float>>& values) const
        requires requires(T t, std::optional<float> value) { t.maybe_set(value); }
    {
        return {width.maybe_set(values.width), height.maybe_set(values.height)};
    }

    /// Resolve each component using its matching physical-axis context, forwarding args.
    template <class U, class... Args>
    [[nodiscard]] auto maybe_resolve(const Size<U>& context, const Args&... args) const {
        return zip_map(context, [&](const auto& value, const auto& basis) {
            return taffy::maybe_resolve(value, basis, args...);
        });
    }

    /// Resolve each component with its matching axis context and the domain's zero fallback.
    template <class U, class... Args>
    [[nodiscard]] auto resolve_or_zero(const Size<U>& context, const Args&... args) const {
        return zip_map(context, [&](const auto& value, const auto& basis) {
            return taffy::resolve_or_zero(value, basis, args...);
        });
    }

#define TAFFY_SIZE_MAYBE(NAME)                                                                     \
    /** @brief Apply scalar optional/domain arithmetic to each matching component pair. */         \
    template <class U>                                                                             \
    [[nodiscard]] auto NAME(const Size<U>& rhs) const {                                            \
        return zip_map(rhs, [](const auto& a, const auto& b) {                                     \
            return taffy::NAME(a, b);                                                              \
        });                                                                                        \
    }
    TAFFY_SIZE_MAYBE(maybe_min)
    TAFFY_SIZE_MAYBE(maybe_max)
    TAFFY_SIZE_MAYBE(maybe_add)
    TAFFY_SIZE_MAYBE(maybe_sub)
#undef TAFFY_SIZE_MAYBE

    /// Clamp each component upper-first then lower; scalar optional and NaN rules apply.
    template <class U, class V>
    [[nodiscard]] auto maybe_clamp(const Size<U>& minimum, const Size<V>& maximum) const {
        using R =
            detail::mapped_t<decltype(taffy::maybe_clamp(width, minimum.width, maximum.width))>;
        return Size<R>{taffy::maybe_clamp(width, minimum.width, maximum.width),
                       taffy::maybe_clamp(height, minimum.height, maximum.height)};
    }
};

/** @brief Owned physical x/y coordinate pair; the caller defines origin and units.
 * Default fields are value-initialized; optional absence is not zero. Abstract
 * axes assume horizontal writing. Row/column select x/y even in reverse directions.
 * Mapping callbacks run synchronously on borrowed components; their exceptions propagate.
 */
template <class T>
struct Point {
    T x{};                                                      ///< Horizontal coordinate/value.
    T y{};                                                      ///< Vertical coordinate/value.
    static constexpr const Point& ZERO = detail::point_zero<T>; ///< Shared domain-zero coordinates.
    static constexpr const Point& NONE =
        detail::point_none<T>; ///< Shared value-initialized coordinates (empty if optional).
    /// Compare x/y exactly using T's equality.
    [[nodiscard]] constexpr bool operator==(const Point&) const = default;

    /// Construct domain-zero coordinates using T::zero() or T{0}.
    [[nodiscard]] static constexpr Point zero() {
        return {detail::zero_value<T>(), detail::zero_value<T>()};
    }

    /// Construct two absent optional coordinates.
    [[nodiscard]] static constexpr Point none()
        requires detail::optional_type<T>
    {
        return {};
    }

    /// Construct x then y, moving the supplied coordinate values.
    [[nodiscard]] static constexpr Point new_(T x, T y) {
        return {std::move(x), std::move(y)};
    }

    /// Swap x and y without translating the origin.
    [[nodiscard]] constexpr Point transpose() const {
        return {y, x};
    }

    /// Copy x to width and y to height without changing units.
    [[nodiscard]] constexpr Size<T> to_size() const {
        return {x, y};
    }

    /// Borrow x for Horizontal or y for Vertical.
    [[nodiscard]] constexpr T& get(AbsoluteAxis axis) noexcept {
        return axis == AbsoluteAxis::Horizontal ? x : y;
    }

    /// Borrow the immutable coordinate on the selected physical axis.
    [[nodiscard]] constexpr const T& get(AbsoluteAxis axis) const noexcept {
        return axis == AbsoluteAxis::Horizontal ? x : y;
    }

    /// Borrow x for Inline or y for Block (horizontal writing assumption).
    [[nodiscard]] constexpr T& get(AbstractAxis axis) noexcept {
        return get(as_abs_naive(axis));
    }

    /// Borrow an immutable coordinate using the naive abstract-axis mapping.
    [[nodiscard]] constexpr const T& get(AbstractAxis axis) const noexcept {
        return get(as_abs_naive(axis));
    }

    /// Immutable physical-axis alias of get().
    [[nodiscard]] constexpr const T& get_abs(AbsoluteAxis axis) const noexcept {
        return get(axis);
    }

    /// Replace the selected physical/abstract-axis coordinate.
    template <class Axis>
    constexpr void set(Axis axis, T value) {
        get(axis) = std::move(value);
    }

    /// Invoke f on x then y, owning the decayed coordinate results.
    template <class F>
    [[nodiscard]] constexpr auto map(F f) const {
        return Point<detail::mapped_t<std::invoke_result_t<F&, const T&>>>{std::invoke(f, x),
                                                                           std::invoke(f, y)};
    }

    /// Transform corresponding x/y pairs, owning the decayed results.
    template <class U, class F>
    [[nodiscard]] constexpr auto zip_map(const Point<U>& rhs, F f) const {
        return Point<detail::mapped_t<std::invoke_result_t<F&, const T&, const U&>>>{
            std::invoke(f, x, rhs.x), std::invoke(f, y, rhs.y)};
    }

    /// Borrow x for row directions or y for column directions, including reverse.
    template <class D>
    [[nodiscard]] constexpr const T& main(D direction) const {
        return detail::direction_is_row(direction) ? x : y;
    }

    /// Borrow y for row directions or x for column directions, including reverse.
    template <class D>
    [[nodiscard]] constexpr const T& cross(D direction) const {
        return detail::direction_is_row(direction) ? y : x;
    }
};

/** @brief Owned start/end pair on a caller-selected axis, with caller-defined units.
 * This can represent endpoints, edge thicknesses, or flags; sum() is arithmetic
 * addition, not an endpoint distance. Components default to value-initialized T.
 * Mapping callbacks run synchronously on borrowed components; their exceptions propagate.
 */
template <class T>
struct Line {
    T start{};                                                  ///< Start endpoint/value.
    T end{};                                                    ///< End endpoint/value.
    static constexpr const Line& TRUE = detail::line_true<T>;   ///< Shared pair of T{true} values.
    static constexpr const Line& FALSE = detail::line_false<T>; ///< Shared pair of T{false} values.
    /// Compare both endpoints/values using T's equality.
    [[nodiscard]] constexpr bool operator==(const Line&) const = default;

    /// Add start + end, preserving the scalar addition result type.
    [[nodiscard]] constexpr auto sum() const {
        return start + end;
    }

    /// Copy one value into both start and end.
    [[nodiscard]] static constexpr Line splat(T value) {
        return {value, value};
    }

    /// Invoke f on start then end, owning the decayed results.
    template <class F>
    [[nodiscard]] constexpr auto map(F f) const {
        return Line<detail::mapped_t<std::invoke_result_t<F&, const T&>>>{std::invoke(f, start),
                                                                          std::invoke(f, end)};
    }

    /// Transform corresponding start/end pairs, owning the decayed results.
    template <class U, class F>
    [[nodiscard]] constexpr auto zip_map(const Line<U>& rhs, F f) const {
        return Line<detail::mapped_t<std::invoke_result_t<F&, const T&, const U&>>>{
            std::invoke(f, start, rhs.start), std::invoke(f, end, rhs.end)};
    }
};

/** @brief Owned physical edges in left, right, top, bottom order (not CSS shorthand).
 * Context determines whether fields are edge coordinates or inset thicknesses,
 * and determines their units/origin. union_() specifically requires coordinates;
 * axis sums normally apply to thicknesses. Fields are value-initialized; optional
 * absence is unknown and NaN is not absence. Direction helpers select physical
 * edges and intentionally do not swap them for row/column reverse directions.
 * Mapping/resolution callbacks borrow components synchronously; their exceptions propagate.
 */
template <class T>
struct Rect {
    T left{};   ///< Physical left edge coordinate or thickness.
    T right{};  ///< Physical right edge coordinate or thickness.
    T top{};    ///< Physical top edge coordinate or thickness.
    T bottom{}; ///< Physical bottom edge coordinate or thickness.
    static constexpr const Rect& ZERO = detail::rect_zero<T>; ///< Shared domain-zero edges.
    static constexpr const Rect& NONE =
        detail::rect_none<T>; ///< Shared value-initialized edges (empty if optional).
    /// Compare all four edges exactly using T's equality.
    [[nodiscard]] constexpr bool operator==(const Rect&) const = default;

    /// Set every edge to T::zero() when available, otherwise T{0}.
    [[nodiscard]] static constexpr Rect zero() {
        return splat(detail::zero_value<T>());
    }

    /// Construct four absent optional edges.
    [[nodiscard]] static constexpr Rect none()
        requires detail::optional_type<T>
    {
        return {};
    }

    /// Construct physical edges in left, right, top, bottom order, moving values.
    [[nodiscard]] static constexpr Rect new_(T left, T right, T top, T bottom) {
        return {std::move(left), std::move(right), std::move(top), std::move(bottom)};
    }

    /// Copy one value to all four edges.
    [[nodiscard]] static constexpr Rect splat(T value) {
        return {value, value, value, value};
    }

    /// Exchange horizontal/vertical edge pairs: {top, bottom, left, right}.
    [[nodiscard]] constexpr Rect transpose() const {
        return {top, bottom, left, right};
    }

    /// Invoke f in left, right, top, bottom order, owning the decayed results.
    template <class F>
    [[nodiscard]] constexpr auto map(F f) const {
        return Rect<detail::mapped_t<std::invoke_result_t<F&, const T&>>>{
            std::invoke(f, left), std::invoke(f, right), std::invoke(f, top),
            std::invoke(f, bottom)};
    }

    /// Transform corresponding physical edges in left, right, top, bottom order.
    template <class U, class F>
    [[nodiscard]] constexpr auto zip_map(const Rect<U>& rhs, F f) const {
        return Rect<detail::mapped_t<std::invoke_result_t<F&, const T&, const U&>>>{
            std::invoke(f, left, rhs.left), std::invoke(f, right, rhs.right),
            std::invoke(f, top, rhs.top), std::invoke(f, bottom, rhs.bottom)};
    }

    /// Pair left/right with size.width and top/bottom with size.height, invoking f per edge.
    template <class U, class F>
    [[nodiscard]] constexpr auto zip_size(const Size<U>& size, F f) const {
        return Rect<detail::mapped_t<std::invoke_result_t<F&, const T&, const U&>>>{
            std::invoke(f, left, size.width), std::invoke(f, right, size.width),
            std::invoke(f, top, size.height), std::invoke(f, bottom, size.height)};
    }

    /// Copy left/right as a start/end pair.
    [[nodiscard]] constexpr Line<T> horizontal_components() const {
        return {left, right};
    }

    /// Copy top/bottom as a start/end pair.
    [[nodiscard]] constexpr Line<T> vertical_components() const {
        return {top, bottom};
    }

    /// Return left + right (a thickness sum, not a coordinate distance).
    [[nodiscard]] constexpr auto horizontal_axis_sum() const {
        return left + right;
    }

    /// Return top + bottom (a thickness sum, not a coordinate distance).
    [[nodiscard]] constexpr auto vertical_axis_sum() const {
        return top + bottom;
    }

    /// Return horizontal and vertical sums as width/height, preserving the horizontal sum type.
    [[nodiscard]] constexpr auto sum_axes() const {
        return Size<detail::mapped_t<decltype(left + right)>>{horizontal_axis_sum(),
                                                              vertical_axis_sum()};
    }

    /// Sum the two edges on a physical grid axis.
    [[nodiscard]] constexpr auto grid_axis_sum(AbsoluteAxis axis) const {
        return axis == AbsoluteAxis::Horizontal ? horizontal_axis_sum() : vertical_axis_sum();
    }

    /// Physical-axis alias of grid_axis_sum().
    [[nodiscard]] constexpr auto axis_sum(AbsoluteAxis axis) const {
        return grid_axis_sum(axis);
    }

    /// Sum edges using the naive Inline=Horizontal, Block=Vertical mapping.
    [[nodiscard]] constexpr auto axis_sum(AbstractAxis axis) const {
        return grid_axis_sum(as_abs_naive(axis));
    }

    /// Sum horizontal edges for row directions, vertical edges for column directions.
    template <class D>
    [[nodiscard]] constexpr auto main_axis_sum(D direction) const {
        return detail::direction_is_row(direction) ? horizontal_axis_sum() : vertical_axis_sum();
    }

    /// Sum vertical edges for row directions, horizontal edges for column directions.
    template <class D>
    [[nodiscard]] constexpr auto cross_axis_sum(D direction) const {
        return detail::direction_is_row(direction) ? vertical_axis_sum() : horizontal_axis_sum();
    }

    /// Borrow left for row directions, top for columns; reverse does not swap edges.
    template <class D>
    [[nodiscard]] constexpr const T& main_start(D direction) const {
        return detail::direction_is_row(direction) ? left : top;
    }

    /// Borrow right for row directions, bottom for columns; reverse does not swap edges.
    template <class D>
    [[nodiscard]] constexpr const T& main_end(D direction) const {
        return detail::direction_is_row(direction) ? right : bottom;
    }

    /// Borrow top for row directions, left for columns; reverse does not swap edges.
    template <class D>
    [[nodiscard]] constexpr const T& cross_start(D direction) const {
        return detail::direction_is_row(direction) ? top : left;
    }

    /// Borrow bottom for row directions, right for columns; reverse does not swap edges.
    template <class D>
    [[nodiscard]] constexpr const T& cross_end(D direction) const {
        return detail::direction_is_row(direction) ? bottom : right;
    }

    /// Bounding union of edge-coordinate rectangles, ignoring a single NaN per min/max pair.
    [[nodiscard]] Rect union_(const Rect& rhs) const
        requires std::same_as<T, float>
    {
        return {f32_min(left, rhs.left), f32_max(right, rhs.right), f32_min(top, rhs.top),
                f32_max(bottom, rhs.bottom)};
    }

    /// Alias of union_(); these fields must represent edge coordinates, not insets.
    [[nodiscard]] Rect united(const Rect& rhs) const
        requires std::same_as<T, float>
    {
        return union_(rhs);
    }

    /// Construct domain lengths in left, right, top, bottom order, without validation.
    [[nodiscard]] static constexpr Rect from_length(float left, float right, float top,
                                                    float bottom)
        requires detail::length_constructible<T>
    {
        return {detail::make_length<T>(left), detail::make_length<T>(right),
                detail::make_length<T>(top), detail::make_length<T>(bottom)};
    }

    /// Construct domain percentage fractions (1 = 100%) in left, right, top, bottom order.
    [[nodiscard]] static constexpr Rect from_percent(float left, float right, float top,
                                                     float bottom)
        requires detail::percent_constructible<T>
    {
        return {detail::make_percent<T>(left), detail::make_percent<T>(right),
                detail::make_percent<T>(top), detail::make_percent<T>(bottom)};
    }

    /// Resolve left/right against context.width and top/bottom against context.height.
    template <class U, class... Args>
    [[nodiscard]] auto maybe_resolve(const Size<U>& context, const Args&... args) const {
        return zip_size(context, [&](const auto& value, const auto& basis) {
            return taffy::maybe_resolve(value, basis, args...);
        });
    }

    /// Resolve all four edges against the same context; unresolved-value rules belong to T.
    template <class Context, class... Args>
    [[nodiscard]] auto maybe_resolve(const Context& context, const Args&... args) const {
        return map([&](const auto& value) {
            return taffy::maybe_resolve(value, context, args...);
        });
    }

    /// Resolve with a per-axis context and the scalar domain's zero fallback.
    template <class U, class... Args>
    [[nodiscard]] auto resolve_or_zero(const Size<U>& context, const Args&... args) const {
        return zip_size(context, [&](const auto& value, const auto& basis) {
            return taffy::resolve_or_zero(value, basis, args...);
        });
    }

    /** @brief Resolve every edge against one scalar context, with domain zero fallback.
     * Supplying the parent's inline size implements the CSS margin/padding basis
     * rule, including top/bottom percentages. Arguments are borrowed for the call.
     */
    template <class Context, class... Args>
    [[nodiscard]] auto resolve_or_zero(const Context& context, const Args&... args) const {
        return map([&](const auto& value) {
            return taffy::resolve_or_zero(value, context, args...);
        });
    }

    /// Set all four edges to the domain's auto value.
    [[nodiscard]] static constexpr Rect auto_()
        requires requires { T::auto_(); }
    {
        return splat(T::auto_());
    }
};

/// Owned minimum/maximum values, possibly of different types; ordering is not validated.
template <class Min, class Max = Min>
struct MinMax {
    Min min{}; ///< Value-initialized lower bound/domain value.
    Max max{}; ///< Value-initialized upper bound/domain value.
    /// Compare both bound values using their own equality operators.
    [[nodiscard]] constexpr bool operator==(const MinMax&) const = default;
};

// Component-wise arithmetic preserves the actual result type (e.g. mixed int/float).
#define TAFFY_GEOMETRY_BINARY(TYPE, OP, ...)                                                       \
    /** @brief Component-wise arithmetic, preserving the scalar expression's result type. */       \
    template <class T, class U>                                                                    \
        requires requires(T a, U b) { a OP b; }                                                    \
    [[nodiscard]] constexpr auto operator OP(const TYPE<T>& a, const TYPE<U>& b) {                 \
        using R = detail::mapped_t<decltype(std::declval<T>() OP std::declval<U>())>;              \
        return TYPE<R>{__VA_ARGS__};                                                               \
    }
TAFFY_GEOMETRY_BINARY(Size, +, a.width + b.width, a.height + b.height)
TAFFY_GEOMETRY_BINARY(Size, -, a.width - b.width, a.height - b.height)
TAFFY_GEOMETRY_BINARY(Point, +, a.x + b.x, a.y + b.y)
TAFFY_GEOMETRY_BINARY(Point, -, a.x - b.x, a.y - b.y)
TAFFY_GEOMETRY_BINARY(Rect, +, a.left + b.left, a.right + b.right, a.top + b.top,
                      a.bottom + b.bottom)
TAFFY_GEOMETRY_BINARY(Rect, -, a.left - b.left, a.right - b.right, a.top - b.top,
                      a.bottom - b.bottom)
TAFFY_GEOMETRY_BINARY(Line, +, a.start + b.start, a.end + b.end)
TAFFY_GEOMETRY_BINARY(Line, -, a.start - b.start, a.end - b.end)
#undef TAFFY_GEOMETRY_BINARY

/// Multiply both size components by a scalar, preserving each expression's result type.
template <class T, class S>
    requires std::is_arithmetic_v<S>
[[nodiscard]] constexpr auto operator*(const Size<T>& a, S scalar) {
    return a.map([scalar](const auto& value) {
        return value * scalar;
    });
}

/// Scalar-first spelling of size multiplication (delegates to size * scalar).
template <class S, class T>
    requires std::is_arithmetic_v<S>
[[nodiscard]] constexpr auto operator*(S scalar, const Size<T>& a) {
    return a * scalar;
}

/// Divide both size components by a scalar; zero/NaN behavior belongs to the scalar types.
template <class T, class S>
    requires std::is_arithmetic_v<S>
[[nodiscard]] constexpr auto operator/(const Size<T>& a, S scalar) {
    return a.map([scalar](const auto& value) {
        return value / scalar;
    });
}

/// Multiply both physical point coordinates by a scalar.
template <class T, class S>
    requires std::is_arithmetic_v<S>
[[nodiscard]] constexpr auto operator*(const Point<T>& a, S scalar) {
    return a.map([scalar](const auto& value) {
        return value * scalar;
    });
}

/// Divide both physical point coordinates by a scalar without validating the divisor.
template <class T, class S>
    requires std::is_arithmetic_v<S>
[[nodiscard]] constexpr auto operator/(const Point<T>& a, S scalar) {
    return a.map([scalar](const auto& value) {
        return value / scalar;
    });
}

#define TAFFY_FREE_SIZE_MAYBE(NAME)                                                                \
    /** @brief Apply scalar optional/domain arithmetic independently to width and height. */       \
    template <class T, class U>                                                                    \
    [[nodiscard]] auto NAME(const Size<T>& a, const Size<U>& b) {                                  \
        return a.NAME(b);                                                                          \
    }
TAFFY_FREE_SIZE_MAYBE(maybe_min)
TAFFY_FREE_SIZE_MAYBE(maybe_max)
TAFFY_FREE_SIZE_MAYBE(maybe_add)
TAFFY_FREE_SIZE_MAYBE(maybe_sub)
#undef TAFFY_FREE_SIZE_MAYBE

/// Clamp each size component, upper bound first; scalar optional/NaN rules apply.
template <class T, class U, class V>
[[nodiscard]] auto maybe_clamp(const Size<T>& a, const Size<U>& minimum, const Size<V>& maximum) {
    return a.maybe_clamp(minimum, maximum);
}

} // namespace taffy
