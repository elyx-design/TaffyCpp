#pragma once

/// @file
/// CSS lengths, application-owned calculations, and available-space constraints.
/// Percentages are fractions: 0.5 means 50%.

#include "taffy/geometry.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace taffy {

/// An application-owned opaque calculation identifier. No alignment or pointer tagging required.
struct Calc {
    std::uintptr_t handle{}; ///< Identifier interpreted exclusively by the application resolver.
    friend constexpr bool operator==(Calc, Calc) = default;
};

/// Resolve an opaque calculation in layout units using the supplied percentage basis.
/// The library neither owns nor parses the expression. Callback exceptions propagate;
/// invoking an empty resolver for a resolvable calc throws std::bad_function_call.
using CalcResolver = std::function<float(Calc, float)>;

/// Discriminant shared by the constrained CSS length domains.
enum class LengthKind : std::uint8_t {
    Length,            ///< Absolute length in layout units (CSS px).
    Percent,           ///< Fraction of the caller-supplied resolution basis.
    Auto,              ///< Automatic sizing or placement-dependent length.
    MinContent,        ///< Intrinsic minimum-content size.
    MaxContent,        ///< Intrinsic maximum-content size.
    FitContent,        ///< Bare fit-content sizing keyword, not a numeric limit.
    FitContentPx,      ///< fit-content() capped by a length in layout units.
    FitContentPercent, ///< fit-content() capped by a percentage fraction.
    Stretch,           ///< Stretch to the available size under the sizing algorithm.
    Content,           ///< Content-based flex basis/sizing keyword.
    Calc,              ///< Application-resolved opaque calculation.
    FitContentCalc,    ///< fit-content() capped by an opaque calculation.
    Fr                 ///< Flexible grid-track factor in fr units.
};

namespace detail {
enum class LengthDomain {
    Percentage,
    PercentageAuto,
    Dimension,
    MinTrack,
    MaxTrack
};
} // namespace detail

/// Native, strongly typed length domain. Percentages use fractions (0.5 means 50%).
/// Only factories valid for the particular domain are available. Inspect with kind(),
/// value(), and calc_value(); there is intentionally no Rust tagged-pointer ABI.
template <detail::LengthDomain Domain>
class BasicLength {
    template <detail::LengthDomain>
    friend class BasicLength;
    LengthKind kind_ =
        Domain == detail::LengthDomain::Percentage ? LengthKind::Length : LengthKind::Auto;
    float value_{};
    Calc calc_{};

    constexpr BasicLength(LengthKind kind, float value = 0, Calc calc = {})
        : kind_(kind), value_(value), calc_(calc) {}

    static constexpr bool has_auto = Domain != detail::LengthDomain::Percentage;
    static constexpr bool has_intrinsic = Domain == detail::LengthDomain::Dimension ||
                                          Domain == detail::LengthDomain::MinTrack ||
                                          Domain == detail::LengthDomain::MaxTrack;
    static constexpr bool has_fit =
        Domain == detail::LengthDomain::Dimension || Domain == detail::LengthDomain::MaxTrack;

public:
    /// Defaults to zero length for LengthPercentage, otherwise auto.
    constexpr BasicLength() = default;

    /// Convert compatible domains; unsupported intrinsic/flex track values become auto.
    /// The constraints intentionally prohibit conversions that could discard auto.
    template <detail::LengthDomain Other>
        requires(Other != Domain && (Other == detail::LengthDomain::Percentage ||
                                     (Other == detail::LengthDomain::PercentageAuto &&
                                      (Domain == detail::LengthDomain::Dimension ||
                                       Domain == detail::LengthDomain::MinTrack ||
                                       Domain == detail::LengthDomain::MaxTrack)) ||
                                     ((Other == detail::LengthDomain::Dimension ||
                                       Other == detail::LengthDomain::MinTrack ||
                                       Other == detail::LengthDomain::MaxTrack) &&
                                      (Domain == detail::LengthDomain::MinTrack ||
                                       Domain == detail::LengthDomain::MaxTrack))))
    constexpr BasicLength(BasicLength<Other> other)
        : kind_(other.kind_), value_(other.value_), calc_(other.calc_) {
        if constexpr (Domain == detail::LengthDomain::MinTrack) {
            if (other.is_fit_content() || other.kind_ == LengthKind::FitContent ||
                other.is_stretch() || other.is_content() || other.is_fr())
                *this = auto_();
        } else if constexpr (Domain == detail::LengthDomain::MaxTrack) {
            if (other.kind_ == LengthKind::FitContent || other.is_stretch() || other.is_content())
                *this = auto_();
        }
    }

    /// Absolute length in layout units (CSS px), stored without normalization.
    static constexpr BasicLength length(float value) {
        return {LengthKind::Length, value};
    }

    /// Percentage fraction of the resolution basis: 0.5 represents 50%.
    static constexpr BasicLength percent(float value) {
        return {LengthKind::Percent, value};
    }

    /// Alias of length().
    static constexpr BasicLength from_length(float value) {
        return length(value);
    }

    /// Alias of percent(); the argument is a fraction, not a number of percent.
    static constexpr BasicLength from_percent(float value) {
        return percent(value);
    }

    /// Zero absolute length, independent of a percentage basis.
    static constexpr BasicLength zero() {
        return length(0);
    }

    /// Store an application-owned calculation handle; no expression is retained.
    static constexpr BasicLength calc(Calc handle) {
        return {LengthKind::Calc, 0, handle};
    }

    /// CSS auto (unavailable in the non-auto length/percentage domain).
    static constexpr BasicLength auto_()
        requires has_auto
    {
        return {LengthKind::Auto};
    }

    /// CSS intrinsic min-content size.
    static constexpr BasicLength min_content()
        requires has_intrinsic
    {
        return {LengthKind::MinContent};
    }

    /// CSS intrinsic max-content size.
    static constexpr BasicLength max_content()
        requires has_intrinsic
    {
        return {LengthKind::MaxContent};
    }

    /// Bare CSS fit-content keyword (only for Dimension).
    static constexpr BasicLength fit_content()
        requires(Domain == detail::LengthDomain::Dimension)
    {
        return {LengthKind::FitContent};
    }

    /// fit-content() limit in layout units (CSS px).
    static constexpr BasicLength fit_content_px(float limit)
        requires has_fit
    {
        return {LengthKind::FitContentPx, limit};
    }

    /// fit-content() limit as a fraction of the resolution basis.
    static constexpr BasicLength fit_content_percent(float limit)
        requires has_fit
    {
        return {LengthKind::FitContentPercent, limit};
    }

    /// fit-content() limit evaluated by the application resolver.
    static constexpr BasicLength fit_content_calc(Calc limit)
        requires has_fit
    {
        return {LengthKind::FitContentCalc, 0, limit};
    }

    /// Preserve a length, percentage, or calc as a fit-content() cap.
    static constexpr BasicLength fit_content(BasicLength<detail::LengthDomain::Percentage> limit)
        requires has_fit
    {
        if (limit.kind() == LengthKind::Length)
            return fit_content_px(limit.value());
        if (limit.kind() == LengthKind::Percent)
            return fit_content_percent(limit.value());
        return fit_content_calc(limit.calc_value());
    }

    /// CSS stretch sizing keyword.
    static constexpr BasicLength stretch()
        requires(Domain == detail::LengthDomain::Dimension)
    {
        return {LengthKind::Stretch};
    }

    /// CSS content sizing keyword.
    static constexpr BasicLength content()
        requires(Domain == detail::LengthDomain::Dimension)
    {
        return {LengthKind::Content};
    }

    /// Flexible maximum grid-track factor in fr units, without validation.
    static constexpr BasicLength fr(float value)
        requires(Domain == detail::LengthDomain::MaxTrack)
    {
        return {LengthKind::Fr, value};
    }

    /// Alias of fr().
    static constexpr BasicLength from_fr(float value)
        requires(Domain == detail::LengthDomain::MaxTrack)
    {
        return fr(value);
    }

    /// Stored domain-independent discriminator; querying it never resolves a value.
    constexpr LengthKind kind() const {
        return kind_;
    }

    /// Numeric argument; throws if this is a keyword or calc handle.
    constexpr float value() const {
        if (kind_ != LengthKind::Length && kind_ != LengthKind::Percent &&
            kind_ != LengthKind::FitContentPx && kind_ != LengthKind::FitContentPercent &&
            kind_ != LengthKind::Fr)
            throw std::logic_error("length has no numeric argument");
        return value_;
    }

    /// Opaque calculation handle; throws logic_error for non-calc kinds.
    constexpr Calc calc_value() const {
        if (!has_calc())
            throw std::logic_error("length has no calc handle");
        return calc_;
    }

    constexpr bool is_auto() const {
        return kind_ == LengthKind::Auto;
    }

    /// True only for an absolute zero length; 0% is not classified as zero.
    constexpr bool is_zero() const {
        return kind_ == LengthKind::Length && value_ == 0;
    }

    constexpr bool is_min_content() const {
        return kind_ == LengthKind::MinContent;
    }

    constexpr bool is_max_content() const {
        return kind_ == LengthKind::MaxContent;
    }

    constexpr bool is_content() const {
        return kind_ == LengthKind::Content;
    }

    constexpr bool is_stretch() const {
        return kind_ == LengthKind::Stretch;
    }

    constexpr bool is_fr() const {
        return kind_ == LengthKind::Fr;
    }

    constexpr bool is_plain_calc() const {
        return kind_ == LengthKind::Calc;
    }

    constexpr bool is_fit_content_calc() const {
        return kind_ == LengthKind::FitContentCalc;
    }

    constexpr bool has_calc() const {
        return is_plain_calc() || is_fit_content_calc();
    }

    /// True for fit-content(...), not the bare fit-content keyword.
    constexpr bool is_fit_content() const {
        return kind_ == LengthKind::FitContentPx || kind_ == LengthKind::FitContentPercent ||
               is_fit_content_calc();
    }

    constexpr bool is_length_or_percentage() const {
        return kind_ == LengthKind::Length || kind_ == LengthKind::Percent;
    }

    constexpr bool is_length_percentage_or_calc() const {
        return is_length_or_percentage() || is_plain_calc();
    }

    /// Min/max-content, any fit-content form, or stretch; excludes auto/content.
    constexpr bool is_sizing_keyword() const {
        return is_min_or_max_content() || is_fit_content() || kind_ == LengthKind::FitContent ||
               is_stretch();
    }

    constexpr bool is_min_or_max_content() const {
        return is_min_content() || is_max_content();
    }

    constexpr bool is_max_or_fit_content() const {
        return is_max_content() || is_fit_content();
    }

    /// Auto, max-content, or a fit-content cap (not the bare fit-content keyword).
    constexpr bool is_max_content_alike() const {
        return is_auto() || is_max_or_fit_content();
    }

    /// Track-sizing classification: auto, min/max-content, or a fit-content cap.
    /// Not a general CSS keyword test; bare fit-content/content/stretch are excluded.
    constexpr bool is_intrinsic() const {
        return is_auto() || is_min_or_max_content() || is_fit_content();
    }

    /// All calc handles conservatively depend on the percentage basis.
    constexpr bool uses_percentage() const {
        return kind_ == LengthKind::Percent || kind_ == LengthKind::FitContentPercent || has_calc();
    }

    /// Auto or a percentage-dependent value whose parent basis is indefinite.
    constexpr bool behaves_as_auto(std::optional<float> parent_size) const {
        return is_auto() || (!parent_size && uses_percentage());
    }

    /// True for lengths, or plain percentages/calcs with a definite parent basis.
    constexpr bool has_definite_value(std::optional<float> parent_size) const {
        return kind_ == LengthKind::Length ||
               ((kind_ == LengthKind::Percent || is_plain_calc()) && parent_size.has_value());
    }

    /// Resolve length, percentage, or plain calc against parent_size in layout units.
    /// Keywords/fit-content caps and percentages/calcs without a basis yield nullopt.
    /// A plain calc with a basis invokes resolver exactly once; callback exceptions
    /// propagate, and an empty resolver throws std::bad_function_call.
    std::optional<float> definite_value(std::optional<float> parent_size,
                                        const CalcResolver& resolver = {}) const;
    /// Resolve against a definite basis; same callback/error rules as definite_value().
    std::optional<float> resolve_to_option(float basis, const CalcResolver& resolver = {}) const;
    /// Alias of definite_value() with a possibly indefinite basis.
    std::optional<float> maybe_resolve(std::optional<float> basis,
                                       const CalcResolver& resolver = {}) const;
    /// Resolve or return zero when indefinite; resolver exceptions are not suppressed.
    float resolve_or_zero(std::optional<float> basis, const CalcResolver& resolver = {}) const;
    /// Resolve the length/percentage/calc domain against a definite basis.
    float resolve(float basis, const CalcResolver& resolver = {}) const
        requires(Domain == detail::LengthDomain::Percentage);
    /// Extract only an absolute length, without invoking a resolver.
    std::optional<float> into_option() const;
    /// Resolve only plain percentages/calcs; absolute lengths and other kinds yield nullopt.
    /// Plain calcs invoke resolver once with basis; callback/empty-resolver errors propagate.
    std::optional<float> resolved_percentage_size(float basis,
                                                  const CalcResolver& resolver = {}) const;
    /// Resolve a fit-content() cap; percentage/calc caps need a definite basis.
    /// Other kinds yield nullopt. Calc caps share definite_value()'s exception rules.
    std::optional<float> fit_content_limit(std::optional<float> basis,
                                           const CalcResolver& resolver = {}) const;
    /// Resolve a fit-content() cap or ordinary definite value using the same basis.
    std::optional<float> definite_limit(std::optional<float> basis,
                                        const CalcResolver& resolver = {}) const;
    friend constexpr bool operator==(const BasicLength&, const BasicLength&) = default;
};

/// Non-auto absolute length, percentage fraction, or opaque calc; defaults to zero.
using LengthPercentage = BasicLength<detail::LengthDomain::Percentage>;
/// Length/percentage/calc with auto; defaults to auto.
using LengthPercentageAuto = BasicLength<detail::LengthDomain::PercentageAuto>;
/// Preferred size/flex-basis domain, including intrinsic sizing keywords.
using Dimension = BasicLength<detail::LengthDomain::Dimension>;
/// Minimum grid-track breadth; excludes fit-content limits and fr units.
using MinTrackSizingFunction = BasicLength<detail::LengthDomain::MinTrack>;
/// Maximum grid-track breadth, including fit-content limits and fr units.
using MaxTrackSizingFunction = BasicLength<detail::LengthDomain::MaxTrack>;

/// A definite layout-unit constraint or intrinsic sizing request; defaults to max-content.
class AvailableSpace {
public:
    /// Definite carries a length; intrinsic kinds request minimum/maximum-content sizing.
    enum class Kind : std::uint8_t {
        Definite,   ///< Definite available length in layout units.
        MinContent, ///< Intrinsic minimum-content constraint.
        MaxContent  ///< Intrinsic maximum-content constraint.
    };

private:
    Kind kind_{Kind::MaxContent};
    float value_{};

    constexpr AvailableSpace(Kind kind, float value) : kind_(kind), value_(value) {}

public:
    /// Default to a max-content constraint.
    constexpr AvailableSpace() = default;

    /// Construct a definite constraint in layout units, without validation.
    constexpr AvailableSpace(float value) : kind_(Kind::Definite), value_(value) {}

    /// Convert nullopt to max-content, otherwise use the definite layout-unit value.
    constexpr AvailableSpace(std::optional<float> value)
        : kind_(value ? Kind::Definite : Kind::MaxContent), value_(value.value_or(0)) {}

    /// Definite available length in layout units (CSS px).
    static constexpr AvailableSpace definite(float value) {
        return {Kind::Definite, value};
    }

    /// Alias of definite().
    static constexpr AvailableSpace length(float value) {
        return definite(value);
    }

    /// Zero definite available length.
    static constexpr AvailableSpace zero() {
        return definite(0);
    }

    /// Intrinsic minimum-content sizing constraint.
    static constexpr AvailableSpace min_content() {
        return {Kind::MinContent, 0};
    }

    /// Intrinsic maximum-content sizing constraint.
    static constexpr AvailableSpace max_content() {
        return {Kind::MaxContent, 0};
    }

    constexpr Kind kind() const {
        return kind_;
    }

    constexpr bool is_definite() const {
        return kind_ == Kind::Definite;
    }

    constexpr bool is_min_content() const {
        return kind_ == Kind::MinContent;
    }

    constexpr bool is_max_content() const {
        return kind_ == Kind::MaxContent;
    }

    constexpr std::optional<float> into_option() const {
        return is_definite() ? std::optional<float>(value_) : std::nullopt;
    }

    /// Definite length; throws std::bad_optional_access for an intrinsic constraint.
    constexpr float value() const {
        return into_option().value();
    }

    constexpr float unwrap() const {
        return value();
    }

    constexpr float unwrap_or(float fallback) const {
        return is_definite() ? value_ : fallback;
    }

    constexpr AvailableSpace or_(AvailableSpace fallback) const {
        return is_definite() ? *this : fallback;
    }

    /// Invoke fallback exactly once only for an intrinsic constraint; exceptions propagate.
    template <class F>
    constexpr AvailableSpace or_else(F&& fallback) const {
        return is_definite() ? *this : std::invoke(std::forward<F>(fallback));
    }

    /// Return the definite length or invoke fallback once; callback exceptions propagate.
    template <class F>
    constexpr float unwrap_or_else(F&& fallback) const {
        return is_definite() ? value_ : std::invoke(std::forward<F>(fallback));
    }

    constexpr AvailableSpace maybe_set(std::optional<float> value) const {
        return value ? definite(*value) : *this;
    }

    /// Invoke fn once for a definite value only; preserve intrinsic kinds and propagate exceptions.
    template <class F>
    constexpr AvailableSpace map_definite_value(F&& fn) const {
        return is_definite() ? definite(std::invoke(std::forward<F>(fn), value_)) : *this;
    }

    /// Definite minus used, zero for min-content, or positive infinity for max-content.
    constexpr float compute_free_space(float used) const {
        return kind_ == Kind::Definite     ? value_ - used
               : kind_ == Kind::MinContent ? 0
                                           : std::numeric_limits<float>::infinity();
    }

    /// Compare matching kinds; definite values differ by less than float epsilon.
    bool is_roughly_equal(AvailableSpace other) const;
    friend constexpr bool operator==(AvailableSpace, AvailableSpace) = default;
};

/// Extract definite width/height constraints; intrinsic constraints become nullopt.
Size<std::optional<float>> into_options(Size<AvailableSpace> value);
/// Replace each axis only when its replacement has a definite value.
Size<AvailableSpace> maybe_set(Size<AvailableSpace> value, Size<std::optional<float>> replacement);

} // namespace taffy

// Installed with the public headers so BasicLength template definitions remain visible
// to consumers; kept separate to leave the public class focused on its API.
#include "taffy/detail/style_length_operations.hpp"
