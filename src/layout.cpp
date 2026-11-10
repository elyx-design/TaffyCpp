#include "sizing.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <taffy/compute.hpp>
#include <taffy/layout.hpp>
#include <taffy/tree.hpp>
#include <utility>

namespace taffy {
float CollapsibleMarginSet::resolve() const {
    return positive + negative;
}

Baselines Baselines::from_first(std::optional<float> first) {
    return {first, std::nullopt};
}

AxisStaticAlign AxisStaticAlign::from_keyword(AxisStaticEdge keyword) {
    return {keyword, AlignmentSafety::Default, keyword};
}

AxisStaticPosition AxisStaticPosition::from_edge(float anchor, AxisStaticEdge edge) {
    return {{anchor, anchor}, AxisStaticAlign::from_keyword(edge)};
}

void OofCandidates::push(OofCandidate candidate) {
    inner.push_back(candidate);
}

void OofCandidates::clear() {
    std::vector<OofCandidate>().swap(inner);
}

Layout Layout::with_order(std::uint32_t order) {
    Layout result;
    result.order = order;
    return result;
}

std::string DetailedGridInfo::grid_template_rows(float zoom) const {
    return rows.to_track_list_string(zoom);
}

std::string DetailedGridInfo::grid_template_columns(float zoom) const {
    return columns.to_track_list_string(zoom);
}

const CollapsibleMarginSet CollapsibleMarginSet::ZERO{};
const Baselines Baselines::NONE{};
const OofCandidates OofCandidates::NONE{};
const LayoutOutput LayoutOutput::HIDDEN{};
const LayoutOutput LayoutOutput::DEFAULT{};
const LayoutInput LayoutInput::HIDDEN = [] {
    LayoutInput result;
    result.run_mode = RunMode::PerformHiddenLayout;
    return result;
}();

RequestedAxis transpose(RequestedAxis axis) {
    return axis == RequestedAxis::Horizontal ? RequestedAxis::Vertical
           : axis == RequestedAxis::Vertical ? RequestedAxis::Horizontal
                                             : RequestedAxis::Both;
}

RequestedAxis requested_axis(AbsoluteAxis axis) {
    return axis == AbsoluteAxis::Horizontal ? RequestedAxis::Horizontal : RequestedAxis::Vertical;
}

std::optional<AbsoluteAxis> absolute_axis(RequestedAxis axis) {
    if (axis == RequestedAxis::Both)
        return std::nullopt;
    return axis == RequestedAxis::Horizontal ? AbsoluteAxis::Horizontal : AbsoluteAxis::Vertical;
}

CollapsibleMarginSet CollapsibleMarginSet::from_margin(float margin) {
    return margin >= 0 ? CollapsibleMarginSet{margin, 0} : CollapsibleMarginSet{0, margin};
}

CollapsibleMarginSet CollapsibleMarginSet::collapse_with_margin(float margin) const {
    return margin >= 0 ? CollapsibleMarginSet{std::fmax(positive, margin), negative}
                       : CollapsibleMarginSet{positive, std::fmin(negative, margin)};
}

CollapsibleMarginSet CollapsibleMarginSet::collapse_with_set(CollapsibleMarginSet other) const {
    return {std::fmax(positive, other.positive), std::fmin(negative, other.negative)};
}

LayoutInput LayoutInput::transpose() const {
    LayoutInput result = *this;
    result.axis = taffy::transpose(axis);
    result.known_dimensions = {known_dimensions.height, known_dimensions.width};
    result.known_dimensions_are_definite = {known_dimensions_are_definite.height,
                                            known_dimensions_are_definite.width};
    result.parent_size = {parent_size.height, parent_size.width};
    result.available_space = {available_space.height, available_space.width};
    result.vertical_margins_are_collapsible = {false, false};
    return result;
}

AxisStaticPosition AxisStaticPosition::from_alignment(AlignSelf alignment, Line<float> area,
                                                      bool axis_is_rtl) {
    if (alignment.keyword == AlignItemsKeyword::SelfStart ||
        alignment.keyword == AlignItemsKeyword::SelfEnd)
        throw std::invalid_argument(
            "self-start/self-end must be resolved before computing static position");
    auto edge_for = [axis_is_rtl](AlignItemsKeyword keyword) {
        if (keyword == AlignItemsKeyword::Center)
            return AxisStaticEdge::Center;
        const bool start = (keyword != AlignItemsKeyword::End &&
                            keyword != AlignItemsKeyword::FlexEnd) != axis_is_rtl;
        return start ? AxisStaticEdge::Start : AxisStaticEdge::End;
    };
    const auto fallback =
        alignment.safety == AlignmentSafety::Safe ? AlignItemsKeyword::Start : alignment.keyword;
    return {area, {edge_for(alignment.keyword), alignment.safety, edge_for(fallback)}};
}

void OofCandidates::append(OofCandidates& other) {
    if (&other == this)
        throw std::invalid_argument("cannot append out-of-flow candidates to themselves");
    if (other.is_empty())
        return;
    if (inner.empty())
        inner = std::move(other.inner);
    else
        inner.insert(inner.end(), std::make_move_iterator(other.inner.begin()),
                     std::make_move_iterator(other.inner.end()));
    other.inner.clear();
}

OofCandidates OofCandidates::take() {
    OofCandidates result;
    result.inner.swap(inner);
    return result;
}

void OofCandidates::translate(Point<float> offset) {
    for (auto& candidate : inner) {
        candidate.static_position.x.area.start += offset.x;
        candidate.static_position.x.area.end += offset.x;
        candidate.static_position.y.area.start += offset.y;
        candidate.static_position.y.area.end += offset.y;
    }
}

LayoutOutput LayoutOutput::from_sizes_and_baselines(Size<float> size, Rect<float> overflow,
                                                    Baselines baselines) {
    LayoutOutput result;
    result.size = size;
    result.scrollable_overflow_rect = overflow;
    result.baselines = baselines;
    return result;
}

LayoutOutput LayoutOutput::from_sizes(Size<float> size, Rect<float> overflow) {
    return from_sizes_and_baselines(size, overflow, Baselines::NONE);
}

LayoutOutput LayoutOutput::from_outer_size(Size<float> size) {
    return from_sizes(size, {});
}

LayoutOutput LayoutOutput::transpose() const {
    auto result = from_sizes({size.height, size.width},
                             {scrollable_overflow_rect.top, scrollable_overflow_rect.bottom,
                              scrollable_overflow_rect.left, scrollable_overflow_rect.right});
    result.oof_candidates = oof_candidates;
    if (oof_positioning_area)
        result.oof_positioning_area = OofPositioningArea{
            {oof_positioning_area->size.height, oof_positioning_area->size.width},
            {oof_positioning_area->offset.y, oof_positioning_area->offset.x}};
    return result;
}

float Layout::content_box_width() const {
    return size.width - padding.left - padding.right - border.left - border.right;
}

float Layout::content_box_height() const {
    return size.height - padding.top - padding.bottom - border.top - border.bottom;
}

Size<float> Layout::content_box_size() const {
    return {content_box_width(), content_box_height()};
}

float Layout::content_box_x() const {
    return location.x + border.left + padding.left;
}

float Layout::content_box_y() const {
    return location.y + border.top + padding.top;
}

float Layout::scroll_width() const {
    return std::fmax(0.0f, scrollable_overflow_rect.right +
                               std::fmin(scrollbar_size.width, size.width) - size.width +
                               border.left + border.right);
}

float Layout::scroll_height() const {
    return std::fmax(0.0f, scrollable_overflow_rect.bottom +
                               std::fmin(scrollbar_size.height, size.height) - size.height +
                               border.top + border.bottom);
}

std::span<const std::string> DetailedGridTracksInfo::names_for_line(std::size_t index) const {
    if (index < negative_implicit_tracks)
        return {};
    index -= negative_implicit_tracks;
    return index < line_names.size() ? std::span<const std::string>(line_names[index])
                                     : std::span<const std::string>{};
}

std::vector<std::vector<std::string>> DetailedGridTracksInfo::iter_line_names() const {
    if (std::none_of(line_names.begin(), line_names.end(), [](const auto& names) {
            return !names.empty();
        }))
        return {};
    auto result = std::vector<std::vector<std::string>>(negative_implicit_tracks);
    result.insert(result.end(), line_names.begin(), line_names.end());
    result.resize(std::max(result.size(), positions.size() + 1));
    return result;
}

namespace {
// Rust's f32 Display uses shortest significant digits, but never exponent notation.
// Do not use fixed to_chars: it can expose extra exact digits of large integers.
void append_track_size(std::string& out, float value) {
    if (std::isnan(value)) {
        out += "NaN";
        return;
    }
    if (std::isinf(value)) {
        out += std::signbit(value) ? "-inf" : "inf";
        return;
    }
    // A float needs at most 9 significant digits; sign, decimal point and exponent
    // fit comfortably here. Expansion goes straight to the dynamically sized string
    // (including the 45 leading fractional places of the smallest subnormal).
    char buffer[64];
    const auto converted =
        std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::general);
    const char* exponent = std::find(buffer, converted.ptr, 'e');
    const char* first = buffer;
    if (*first == '-') {
        out += '-';
        ++first;
    }
    int decimal_shift = 0;
    if (exponent != converted.ptr) {
        const char* power = exponent + 1;
        const bool negative_power = *power == '-';
        if (*power == '+' || *power == '-')
            ++power;
        while (power != converted.ptr)
            decimal_shift = decimal_shift * 10 + (*power++ - '0');
        if (negative_power)
            decimal_shift = -decimal_shift;
    }
    char digits[16];
    int count = 0;
    int point = 0;
    bool saw_point = false;
    for (; first != exponent; ++first) {
        if (*first == '.') {
            saw_point = true;
            continue;
        }
        digits[count++] = *first;
        if (!saw_point)
            ++point;
    }
    point += decimal_shift;
    // Rust Display breaks an exactly halfway shortest-decimal tie upward in
    // magnitude; to_chars breaks it to even. Compare the midpoint exactly as
    // odd_integer * 2^exponent, without floating arithmetic or wide integers.
    if (value != 0 && (digits[count - 1] - '0') % 2 == 0) {
        unsigned coefficient = 0;
        for (int i = 0; i < count; ++i)
            coefficient = coefficient * 10 + (digits[i] - '0');
        unsigned midpoint = coefficient * 2 + 1;
        const int decimal_power = point - count;
        bool possible = true;
        for (int i = 0; i < -decimal_power; ++i) {
            if (midpoint % 5 != 0) {
                possible = false;
                break;
            }
            midpoint /= 5;
        }
        for (int i = 0; possible && i < decimal_power; ++i) {
            if (midpoint > 0xffffffu / 5) {
                possible = false;
                break;
            }
            midpoint *= 5;
        }
        if (possible) {
            int binary_power = 0;
            const float fraction = std::frexp(std::fabs(value), &binary_power);
            unsigned significand = static_cast<unsigned>(std::ldexp(fraction, 24));
            binary_power -= 24;
            while (significand % 2 == 0) {
                significand /= 2;
                ++binary_power;
            }
            if (significand == midpoint && binary_power == decimal_power - 1)
                ++digits[count - 1];
        }
    }
    if (point <= 0) {
        out += "0.";
        out.append(static_cast<std::size_t>(-point), '0');
        out.append(digits, digits + count);
    } else if (point >= count) {
        out.append(digits, digits + count);
        out.append(static_cast<std::size_t>(point - count), '0');
    } else {
        out.append(digits, digits + point);
        out += '.';
        out.append(digits + point, digits + count);
    }
}
} // namespace

std::string DetailedGridTracksInfo::to_track_list_string(float zoom) const {
    if (positions.empty())
        return "none";
    std::string result;
    auto append_names = [&](std::span<const std::string> names) {
        if (names.empty())
            return;
        if (!result.empty())
            result += ' ';
        result += '[';
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i)
                result += ' ';
            result += names[i];
        }
        result += ']';
    };
    for (std::size_t i = 0; i < positions.size(); ++i) {
        append_names(names_for_line(i));
        if (!result.empty())
            result += ' ';
        append_track_size(result, (positions[i].end - positions[i].start) / zoom);
        result += "px";
    }
    append_names(names_for_line(positions.size()));
    return result;
}

std::optional<std::pair<Point<float>, Size<float>>>
DetailedGridInfo::item_grid_area(std::size_t index) const {
    if (index >= items.size())
        return std::nullopt;
    const auto& item = items[index];
    const auto start_col = columns.positions.at(item.column_start - 1);
    const auto end_col = columns.positions.at(item.column_end - 2);
    const float left = std::fmin(start_col.start, end_col.start);
    const float right = std::fmax(start_col.end, end_col.end);
    const float top = rows.positions.at(item.row_start - 1).start;
    const float bottom = rows.positions.at(item.row_end - 2).end;
    return std::pair{Point<float>{left, top}, Size<float>{right - left, bottom - top}};
}

std::optional<SizingKeywordResolution>
resolve_sizing_keyword(const Dimension& style, std::optional<float> stretch_size,
                       std::optional<float> percent_resolution_basis,
                       const CalcResolver& calc_resolver) {
    const auto measure = [](AvailableSpace space) {
        return SizingKeywordResolution{SizingKeywordResolution::Kind::Measure, space, 0};
    };
    switch (style.kind()) {
    case LengthKind::MinContent:
        return measure(AvailableSpace::min_content());
    case LengthKind::MaxContent:
        return measure(AvailableSpace::max_content());
    case LengthKind::FitContentPx:
        return measure(AvailableSpace::definite(style.value()));
    case LengthKind::FitContentPercent:
        if (percent_resolution_basis)
            return measure(AvailableSpace::definite(*percent_resolution_basis * style.value()));
        break;
    case LengthKind::FitContent:
        if (stretch_size)
            return measure(AvailableSpace::definite(*stretch_size));
        break;
    case LengthKind::FitContentCalc:
        if (percent_resolution_basis)
            return measure(AvailableSpace::definite(
                calc_resolver(style.calc_value(), *percent_resolution_basis)));
        break;
    case LengthKind::Stretch:
        if (stretch_size)
            return SizingKeywordResolution{SizingKeywordResolution::Kind::Exact, {}, *stretch_size};
        break;
    default:
        break;
    }
    return std::nullopt;
}

void resolve_absolute_sizing_keywords(LayoutTree& tree, NodeId node,
                                      Size<std::optional<float>>& known_dimensions,
                                      Size<Dimension> size_style, Size<float> area_size,
                                      Rect<std::optional<float>> inset,
                                      Rect<std::optional<float>> margin, SizingMode sizing_mode) {
    const Size<float> stretch_size{
        std::fmax(area_size.width - inset.left.value_or(0) - inset.right.value_or(0) -
                      margin.left.value_or(0) - margin.right.value_or(0),
                  0.0f),
        std::fmax(area_size.height - inset.top.value_or(0) - inset.bottom.value_or(0) -
                      margin.top.value_or(0) - margin.bottom.value_or(0),
                  0.0f)};
    const CalcResolver calc = [&tree](Calc value, float basis) {
        return tree.resolve_calc(value, basis);
    };
    // Avoid GCC's conditional-optional uninitialized diagnostic without changing resolution.
    std::optional<SizingKeywordResolution> width{};
    std::optional<SizingKeywordResolution> height{};
    if (!known_dimensions.width) {
        if (const auto resolved =
                resolve_sizing_keyword(size_style.width, stretch_size.width, area_size.width, calc))
            width.emplace(*resolved);
    }
    if (!known_dimensions.height) {
        if (const auto resolved = resolve_sizing_keyword(size_style.height, stretch_size.height,
                                                         area_size.height, calc))
            height.emplace(*resolved);
    }
    const auto measure = [&](Size<std::optional<float>> known, Size<AvailableSpace> available,
                             RequestedAxis axis) {
        LayoutInput input;
        input.run_mode = RunMode::ComputeSize;
        input.sizing_mode = sizing_mode;
        input.axis = axis;
        input.known_dimensions = known;
        input.parent_size = {area_size.width, area_size.height};
        input.available_space = available;
        return compute_cached_layout(tree, node, input).size;
    };
    if (width && height && width->kind == SizingKeywordResolution::Kind::Measure &&
        height->kind == SizingKeywordResolution::Kind::Measure) {
        const auto measured =
            measure({}, {width->available_space, height->available_space}, RequestedAxis::Both);
        known_dimensions = {measured.width, measured.height};
        return;
    }
    if (width)
        known_dimensions.width =
            width->kind == SizingKeywordResolution::Kind::Exact
                ? width->exact
                : measure(known_dimensions,
                          {width->available_space, AvailableSpace::definite(stretch_size.height)},
                          RequestedAxis::Horizontal)
                      .width;
    if (height)
        known_dimensions.height =
            height->kind == SizingKeywordResolution::Kind::Exact
                ? height->exact
                : measure(known_dimensions,
                          {known_dimensions.width
                               ? AvailableSpace::definite(*known_dimensions.width)
                               : AvailableSpace::definite(stretch_size.width),
                           height->available_space},
                          RequestedAxis::Vertical)
                      .height;
}
} // namespace taffy
