#pragma once

#include "../../support/oracle_abi.h"
#include <taffy/taffy.hpp>

#include <cmath>
#include <cstdint>
#include <optional>

namespace primary {

struct Status { std::uint32_t code; };

inline bool boolean(std::uint32_t v) {
    switch (v) {
    case 0: return false;
    case 1: return true;
    default: throw Status{ORACLE_UNSUPPORTED};
    }
}
inline std::optional<float> opt(OracleOptional v) {
    return boolean(v.present) ? std::optional<float>{v.value} : std::nullopt;
}
inline OracleOptional co(std::optional<float> v) {
    return {static_cast<std::uint32_t>(v.has_value()), v.value_or(0.0f)};
}
inline taffy::Size<std::optional<float>> known(OracleKnown v) {
    return {opt(v.width), opt(v.height)};
}
inline OracleKnown ck(taffy::Size<std::optional<float>> v) {
    return {co(v.width), co(v.height)};
}
inline OracleSize cs(taffy::Size<float> v) { return {v.width, v.height}; }
inline taffy::Size<float> rs(OracleSize v) { return {v.width, v.height}; }
inline OracleRect cr(taffy::Rect<float> v) { return {v.left, v.right, v.top, v.bottom}; }
inline taffy::Rect<float> rr(OracleRect v) { return {v.left, v.right, v.top, v.bottom}; }

inline taffy::Dimension dim(OracleScalar v) {
    using T = taffy::Dimension;
    switch (v.tag) {
    case 0: return T::auto_();
    case 1: return T::length(v.value);
    case 2: return T::percent(v.value);
    case 3: return T::min_content();
    case 4: return T::max_content();
    case 5: return T::fit_content();
    case 6: return T::fit_content_px(v.value);
    case 7: return T::stretch();
    case 8: return T::content();
    case 9: return T::fit_content_percent(v.value);
    default: throw Status{ORACLE_UNSUPPORTED};
    }
}
inline taffy::LengthPercentageAuto lpa(OracleScalar v) {
    using T = taffy::LengthPercentageAuto;
    switch (v.tag) {
    case 0: return T::auto_();
    case 1: return T::length(v.value);
    case 2: return T::percent(v.value);
    default: throw Status{ORACLE_UNSUPPORTED};
    }
}
inline taffy::LengthPercentage lp(OracleScalar v) {
    using T = taffy::LengthPercentage;
    switch (v.tag) {
    case 1: return T::length(v.value);
    case 2: return T::percent(v.value);
    default: throw Status{ORACLE_UNSUPPORTED};
    }
}
inline taffy::AvailableSpace avail(OracleScalar v) {
    using T = taffy::AvailableSpace;
    switch (v.tag) {
    case 1: return T::definite(v.value);
    case 3: return T::min_content();
    case 4: return T::max_content();
    default: throw Status{ORACLE_UNSUPPORTED};
    }
}
inline OracleScalar ca(taffy::AvailableSpace v) {
    switch (v.kind()) {
    case taffy::AvailableSpace::Kind::Definite: return {1, v.value()};
    case taffy::AvailableSpace::Kind::MinContent: return {3, 0.0f};
    case taffy::AvailableSpace::Kind::MaxContent: return {4, 0.0f};
    }
    throw Status{ORACLE_UNSUPPORTED};
}
inline taffy::Size<taffy::AvailableSpace> avs(OracleSizing v) {
    return {avail(v.width), avail(v.height)};
}
inline OracleSizing cas(taffy::Size<taffy::AvailableSpace> v) {
    return {ca(v.width), ca(v.height)};
}
template<class T> inline taffy::Rect<T> edges(OracleEdges v, T (*f)(OracleScalar)) {
    return {f(v.left), f(v.right), f(v.top), f(v.bottom)};
}
template<class T> inline taffy::Size<T> sizes(OracleSizing v, T (*f)(OracleScalar)) {
    return {f(v.width), f(v.height)};
}
inline taffy::Overflow overflow(std::uint32_t v) {
    switch (v) {
    case 0: return taffy::Overflow::Visible;
    case 1: return taffy::Overflow::Hidden;
    case 2: return taffy::Overflow::Scroll;
    case 3: return taffy::Overflow::Clip;
    default: throw Status{ORACLE_UNSUPPORTED};
    }
}
// Omitted self alignment must remain absent, distinct from explicit NORMAL.
inline std::optional<taffy::AlignItems> item_alignment(std::uint32_t v) {
    using T = taffy::AlignItems;
    switch (v) {
    case 0: return std::nullopt;
    case 1: return T::start();
    case 2: return T::end();
    case 3: return T::flex_start();
    case 4: return T::flex_end();
    case 5: return T::center();
    case 6: return T::baseline();
    case 7: return T::stretch();
    case 8: return T::normal();
    case 9: return T::self_start();
    case 10: return T::self_end();
    default: throw Status{ORACLE_UNSUPPORTED};
    }
}
inline std::optional<taffy::AlignContent> content_alignment(std::uint32_t v) {
    using T = taffy::AlignContent;
    switch (v) {
    case 0: return std::nullopt;
    case 1: return T::start();
    case 2: return T::end();
    case 3: return T::flex_start();
    case 4: return T::flex_end();
    case 5: return T::center();
    case 6: return T::stretch();
    case 7: return T::space_between();
    case 8: return T::space_evenly();
    case 9: return T::space_around();
    case 10: return T::normal();
    default: throw Status{ORACLE_UNSUPPORTED};
    }
}
inline taffy::Position position(std::uint32_t v) {
    switch (v) {
    case 0: return taffy::Position::Relative;
    case 1: return taffy::Position::Absolute;
    case 2: return taffy::Position::Fixed;
    case 3: return taffy::Position::Static;
    case 4: return taffy::Position::Sticky;
    default: throw Status{ORACLE_UNSUPPORTED};
    }
}
inline taffy::Style style(OracleStyle v) {
    taffy::Style s{};
    s.size = sizes(v.size, dim);
    s.min_size = sizes(v.min_size, lpa);
    s.max_size = sizes(v.max_size, lpa);
    s.padding = edges(v.padding, lp);
    s.border = edges(v.border, lp);
    s.margin = edges(v.margin, lpa);
    s.inset = edges(v.inset, lpa);
    s.gap = sizes(v.gap, lp);
    s.align_items = item_alignment(v.align_items).value_or(s.align_items);
    s.align_self = item_alignment(v.align_self);
    s.justify_items = item_alignment(v.justify_items).value_or(s.justify_items);
    s.justify_self = item_alignment(v.justify_self);
    s.align_content = content_alignment(v.align_content).value_or(s.align_content);
    s.justify_content = content_alignment(v.justify_content).value_or(s.justify_content);
    s.aspect_ratio = opt(v.aspect_ratio);
    s.flex_basis = dim(v.flex_basis);
    s.flex_grow = v.flex_grow;
    s.flex_shrink = v.flex_shrink;
    s.scrollbar_width = v.scrollbar_width;
    switch (v.display) {
    case 0: s.display = taffy::Display::Flex; break;
    case 1: s.display = taffy::Display::Grid; break;
    case 2: s.display = taffy::Display::Block; break;
    case 3: s.display = taffy::Display::None; break;
    case 4: s.display = taffy::Display::FlowRoot; break;
    default: throw Status{ORACLE_UNSUPPORTED};
    }
    s.position = position(v.position);
    switch (v.box_sizing) {
    case 0: s.box_sizing = taffy::BoxSizing::BorderBox; break;
    case 1: s.box_sizing = taffy::BoxSizing::ContentBox; break;
    default: throw Status{ORACLE_UNSUPPORTED};
    }
    s.overflow = {overflow(v.overflow_x), overflow(v.overflow_y)};
    switch (v.flex_direction) {
    case 0: s.flex_direction = taffy::FlexDirection::Row; break;
    case 1: s.flex_direction = taffy::FlexDirection::Column; break;
    case 2: s.flex_direction = taffy::FlexDirection::RowReverse; break;
    case 3: s.flex_direction = taffy::FlexDirection::ColumnReverse; break;
    default: throw Status{ORACLE_UNSUPPORTED};
    }
    switch (v.flex_wrap) {
    case 0: s.flex_wrap = taffy::FlexWrap::NoWrap; break;
    case 1: s.flex_wrap = taffy::FlexWrap::Wrap; break;
    case 2: s.flex_wrap = taffy::FlexWrap::WrapReverse; break;
    case 3: s.flex_wrap = taffy::FlexWrap::Balance; break;
    default: throw Status{ORACLE_UNSUPPORTED};
    }
    switch (v.direction) {
    case 0: s.direction = taffy::Direction::Ltr; break;
    case 1: s.direction = taffy::Direction::Rtl; break;
    default: throw Status{ORACLE_UNSUPPORTED};
    }
    return s;
}
inline OracleStyle default_style() {
    OracleStyle s{};
    const OracleScalar zero{1, 0.0f};
    const OracleEdges zero_edges{zero, zero, zero, zero};
    s.padding = zero_edges;
    s.border = zero_edges;
    s.margin = zero_edges;
    s.gap = {zero, zero};
    s.flex_shrink = 1.0f;
    s.position = 3;
    return s;
}
inline taffy::LayoutInput input(OracleLayoutInput v) {
    taffy::LayoutInput i{};
    switch (v.run_mode) {
    case 0: i.run_mode = taffy::RunMode::PerformLayout; break;
    case 1: i.run_mode = taffy::RunMode::ComputeSize; break;
    case 2: i.run_mode = taffy::RunMode::PerformHiddenLayout; break;
    default: throw Status{ORACLE_UNSUPPORTED};
    }
    switch (v.sizing_mode) {
    case 0: i.sizing_mode = taffy::SizingMode::ContentSize; break;
    case 1: i.sizing_mode = taffy::SizingMode::InherentSize; break;
    default: throw Status{ORACLE_UNSUPPORTED};
    }
    switch (v.axis) {
    case 0: i.axis = taffy::RequestedAxis::Horizontal; break;
    case 1: i.axis = taffy::RequestedAxis::Vertical; break;
    case 2: i.axis = taffy::RequestedAxis::Both; break;
    default: throw Status{ORACLE_UNSUPPORTED};
    }
    i.known_dimensions = known(v.known_dimensions);
    i.known_dimensions_are_definite = {boolean(v.definite_width), boolean(v.definite_height)};
    i.parent_size = known(v.parent_size);
    i.available_space = avs(v.available_space);
    i.vertical_margins_are_collapsible = {boolean(v.collapse_start), boolean(v.collapse_end)};
    return i;
}
inline OracleLayoutInput ci(const taffy::LayoutInput& v) {
    OracleLayoutInput i{};
    switch (v.run_mode) {
    case taffy::RunMode::PerformLayout: i.run_mode = 0; break;
    case taffy::RunMode::ComputeSize: i.run_mode = 1; break;
    case taffy::RunMode::PerformHiddenLayout: i.run_mode = 2; break;
    default: throw Status{ORACLE_UNSUPPORTED};
    }
    switch (v.sizing_mode) {
    case taffy::SizingMode::ContentSize: i.sizing_mode = 0; break;
    case taffy::SizingMode::InherentSize: i.sizing_mode = 1; break;
    default: throw Status{ORACLE_UNSUPPORTED};
    }
    switch (v.axis) {
    case taffy::RequestedAxis::Horizontal: i.axis = 0; break;
    case taffy::RequestedAxis::Vertical: i.axis = 1; break;
    case taffy::RequestedAxis::Both: i.axis = 2; break;
    default: throw Status{ORACLE_UNSUPPORTED};
    }
    i.known_dimensions = ck(v.known_dimensions);
    i.definite_width = static_cast<std::uint32_t>(v.known_dimensions_are_definite.width);
    i.definite_height = static_cast<std::uint32_t>(v.known_dimensions_are_definite.height);
    i.parent_size = ck(v.parent_size);
    i.available_space = cas(v.available_space);
    i.collapse_start = static_cast<std::uint32_t>(v.vertical_margins_are_collapsible.start);
    i.collapse_end = static_cast<std::uint32_t>(v.vertical_margins_are_collapsible.end);
    return i;
}
inline taffy::CollapsibleMarginSet margin(float positive, float negative) {
    if (positive < 0.0f || negative > 0.0f || std::isnan(positive) || std::isnan(negative))
        throw Status{ORACLE_INVALID};
    return {positive, negative};
}
inline taffy::LayoutOutput output(OracleLayoutOutput v) {
    if (v.oof_candidates_count != 0 || v.oof_area_present != 0)
        throw Status{ORACLE_UNSUPPORTED};
    taffy::LayoutOutput o{};
    o.size = rs(v.size);
    o.scrollable_overflow_rect = rr(v.scrollable_overflow);
    o.baselines = {opt(v.first_baseline), opt(v.last_baseline)};
    o.top_margin = margin(v.top_positive, v.top_negative);
    o.bottom_margin = margin(v.bottom_positive, v.bottom_negative);
    o.margins_can_collapse_through = boolean(v.margins_can_collapse_through);
    return o;
}
inline OracleLayoutOutput cout(const taffy::LayoutOutput& v) {
    if (!v.oof_candidates.empty() || v.oof_positioning_area.has_value())
        throw Status{ORACLE_UNSUPPORTED};
    // Unlike Rust's private components, native margins can be exported losslessly.
    (void)margin(v.top_margin.positive, v.top_margin.negative);
    (void)margin(v.bottom_margin.positive, v.bottom_margin.negative);
    OracleLayoutOutput o{};
    o.size = cs(v.size);
    o.scrollable_overflow = cr(v.scrollable_overflow_rect);
    o.first_baseline = co(v.baselines.first);
    o.last_baseline = co(v.baselines.last);
    o.top_positive = v.top_margin.positive;
    o.top_negative = v.top_margin.negative;
    o.bottom_positive = v.bottom_margin.positive;
    o.bottom_negative = v.bottom_margin.negative;
    o.margins_can_collapse_through = static_cast<std::uint32_t>(v.margins_can_collapse_through);
    return o;
}

} // namespace primary
