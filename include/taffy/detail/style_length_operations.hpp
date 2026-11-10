#pragma once

// Implementation detail of style.hpp; included there after BasicLength is declared.
// Installed because consumers instantiate these template members in their own TUs.

namespace taffy {

template <detail::LengthDomain Domain>
inline std::optional<float>
BasicLength<Domain>::definite_value(std::optional<float> parent_size,
                                    const CalcResolver& resolver) const {
    if (kind_ == LengthKind::Length)
        return value_;
    if (!parent_size)
        return std::nullopt;
    if (kind_ == LengthKind::Percent)
        return value_ * *parent_size;
    if (is_plain_calc())
        return resolver(calc_, *parent_size); // missing resolver throws bad_function_call
    return std::nullopt;
}

template <detail::LengthDomain Domain>
inline std::optional<float>
BasicLength<Domain>::resolve_to_option(float basis, const CalcResolver& resolver) const {
    return definite_value(basis, resolver);
}

template <detail::LengthDomain Domain>
inline std::optional<float> BasicLength<Domain>::maybe_resolve(std::optional<float> basis,
                                                               const CalcResolver& resolver) const {
    return definite_value(basis, resolver);
}

template <detail::LengthDomain Domain>
inline float BasicLength<Domain>::resolve_or_zero(std::optional<float> basis,
                                                  const CalcResolver& resolver) const {
    return definite_value(basis, resolver).value_or(0);
}

template <detail::LengthDomain Domain>
inline float BasicLength<Domain>::resolve(float basis, const CalcResolver& resolver) const
    requires(Domain == detail::LengthDomain::Percentage)
{
    return *definite_value(basis, resolver);
}

template <detail::LengthDomain Domain>
inline std::optional<float> BasicLength<Domain>::into_option() const {
    return kind_ == LengthKind::Length ? std::optional<float>(value_) : std::nullopt;
}

template <detail::LengthDomain Domain>
inline std::optional<float>
BasicLength<Domain>::resolved_percentage_size(float basis, const CalcResolver& resolver) const {
    if (kind_ == LengthKind::Percent)
        return value_ * basis;
    if (is_plain_calc())
        return resolver(calc_, basis);
    return std::nullopt;
}

template <detail::LengthDomain Domain>
inline std::optional<float>
BasicLength<Domain>::fit_content_limit(std::optional<float> basis,
                                       const CalcResolver& resolver) const {
    if (kind_ == LengthKind::FitContentPx)
        return value_;
    if (!basis)
        return std::nullopt;
    if (kind_ == LengthKind::FitContentPercent)
        return value_ * *basis;
    if (is_fit_content_calc())
        return resolver(calc_, *basis);
    return std::nullopt;
}

template <detail::LengthDomain Domain>
inline std::optional<float>
BasicLength<Domain>::definite_limit(std::optional<float> basis,
                                    const CalcResolver& resolver) const {
    return is_fit_content() ? fit_content_limit(basis, resolver) : definite_value(basis, resolver);
}

} // namespace taffy
