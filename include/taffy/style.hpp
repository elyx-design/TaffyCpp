#pragma once

/// @file
/// CSS-like Style properties, keywords, and construction helpers.
/// Includes value and grid input types; percentages are fractions, not whole percentages.

#include "taffy/geometry.hpp"
#include "taffy/grid.hpp"
#include "taffy/values.hpp"

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace taffy {

/// CSS display mode determining box generation and the child layout algorithm.
enum class Display : std::uint8_t {
    Flex,     ///< Flex formatting context.
    Grid,     ///< Grid formatting context.
    Block,    ///< Block layout.
    FlowRoot, ///< Block layout with an independent formatting context.
    None      ///< No generated layout box.
};
/// Whether the style generates a layout box.
enum class BoxGenerationMode : std::uint8_t {
    Normal, ///< Generate the ordinary box.
    None    ///< Suppress box generation.
};
/// CSS positioning scheme; absolute/fixed are out of normal flow.
enum class Position : std::uint8_t {
    Static,   ///< Ordinary in-flow positioning.
    Relative, ///< In-flow box offset relative to its normal position.
    Absolute, ///< Out-of-flow positioning against an absolute containing block.
    Fixed,    ///< Out-of-flow positioning against a fixed containing block.
    Sticky    ///< In-flow sticky positioning scheme.
};
/// Edge box to which specified dimensions apply.
enum class BoxSizing : std::uint8_t {
    BorderBox, ///< Dimensions include padding and border.
    ContentBox ///< Dimensions describe the content box.
};
/// Inline progression direction.
enum class Direction : std::uint8_t {
    Ltr, ///< Left to right.
    Rtl  ///< Right to left.
};
/// CSS overflow behavior on one physical axis.
enum class Overflow : std::uint8_t {
    Visible, ///< Overflow remains visible.
    Clip,    ///< Clip without establishing a scroll container.
    Hidden,  ///< Clip and establish a scroll container.
    Scroll   ///< Establish a scroll container with scrollbar space.
};

/// True for absolute/fixed positioning, which do not participate in normal flow.
constexpr bool is_out_of_flow(Position value) {
    return value == Position::Absolute || value == Position::Fixed;
}

/// True for every non-static positioning scheme.
constexpr bool is_positioned(Position value) {
    return value != Position::Static;
}

/// Whether inline progression is right-to-left.
constexpr bool is_rtl(Direction value) {
    return value == Direction::Rtl;
}

/// Hidden and scroll establish scroll containers; visible and clip do not.
constexpr bool is_scroll_container(Overflow value) {
    return value == Overflow::Hidden || value == Overflow::Scroll;
}

/// Scroll containers have zero automatic minimum size; others defer to content sizing.
constexpr std::optional<float> maybe_into_automatic_min_size(Overflow value) {
    return is_scroll_container(value) ? std::optional<float>(0.0f) : std::nullopt;
}

/// Which out-of-flow positioning schemes may use this box as their containing block.
struct ContainingBlockClaims {
    bool absolute{}; ///< Claims absolutely positioned descendants.
    bool fixed{};    ///< Claims fixed-position descendants.

    static constexpr ContainingBlockClaims none() {
        return {};
    }

    static constexpr ContainingBlockClaims all() {
        return {true, true};
    }

    constexpr bool for_position(Position value) const {
        return value == Position::Absolute ? absolute : value == Position::Fixed && fixed;
    }

    friend constexpr bool operator==(ContainingBlockClaims, ContainingBlockClaims) = default;
};

/// Supported CSS containment flags: layout and paint; content is their union.
/// This representation stores layout effects only, not style/size containment.
class Contain {
    std::uint8_t bits_{};

    explicit constexpr Contain(std::uint8_t bits) : bits_(bits) {}

public:
    constexpr Contain() = default;

    /// No supported containment effects.
    static constexpr Contain none() {
        return Contain(0);
    }

    /// CSS layout containment, including baseline suppression.
    static constexpr Contain layout() {
        return Contain(1);
    }

    /// CSS paint containment.
    static constexpr Contain paint() {
        return Contain(2);
    }

    /// Combined layout and paint containment.
    static constexpr Contain content() {
        return Contain(3);
    }

    constexpr bool contains(Contain other) const {
        return (bits_ & other.bits_) == other.bits_;
    }

    constexpr bool intersects(Contain other) const {
        return (bits_ & other.bits_) != 0;
    }

    constexpr Contain union_(Contain other) const {
        return Contain(bits_ | other.bits_);
    }

    constexpr bool establishes_independent_formatting_context() const {
        return bits_ != 0;
    }

    constexpr bool suppresses_baseline() const {
        return contains(layout());
    }

    constexpr bool contains_scrollable_overflow() const {
        return bits_ != 0;
    }

    friend constexpr Contain operator|(Contain a, Contain b) {
        return a.union_(b);
    }

    constexpr Contain& operator|=(Contain other) {
        *this = *this | other;
        return *this;
    }

    friend constexpr bool operator==(Contain, Contain) = default;
};

/// CSS item/self positional alignment keywords.
enum class AlignItemsKeyword : std::uint8_t {
    Normal,    ///< Formatting-context default behavior.
    Start,     ///< Container's logical start edge.
    End,       ///< Container's logical end edge.
    FlexStart, ///< Flex main/cross start edge.
    FlexEnd,   ///< Flex main/cross end edge.
    SelfStart, ///< Item's own logical start edge.
    SelfEnd,   ///< Item's own logical end edge.
    Center,    ///< Center in the alignment container.
    Baseline,  ///< Align item baselines.
    Stretch    ///< Stretch an eligible auto-sized item.
};
/// CSS content alignment and free-space distribution keywords.
enum class AlignContentKeyword : std::uint8_t {
    Normal,       ///< Formatting-context default behavior.
    Start,        ///< Pack at the logical start.
    End,          ///< Pack at the logical end.
    FlexStart,    ///< Pack at flex start.
    FlexEnd,      ///< Pack at flex end.
    Center,       ///< Center the content.
    Stretch,      ///< Stretch eligible tracks/lines.
    SpaceBetween, ///< Equal inter-item spaces; no outer spaces.
    SpaceEvenly,  ///< Equal inter-item and outer spaces.
    SpaceAround   ///< Outer spaces are half the inter-item spaces.
};
/// Overflow alignment policy accompanying a positional alignment keyword.
enum class AlignmentSafety : std::uint8_t {
    Default, ///< No explicit overflow policy.
    Unsafe,  ///< Honor alignment even when it overflows.
    Safe     ///< Fall back toward start to avoid inaccessible overflow.
};

/// Item/self alignment; factories spell CSS keywords, safe_* adds safe overflow alignment.
struct AlignItems {
    AlignItemsKeyword keyword{AlignItemsKeyword::Normal}; ///< Positional alignment.
    AlignmentSafety safety{AlignmentSafety::Default};     ///< Overflow alignment policy.

    static constexpr AlignItems normal() {
        return {};
    }

    static constexpr AlignItems start() {
        return {AlignItemsKeyword::Start};
    }

    static constexpr AlignItems end() {
        return {AlignItemsKeyword::End};
    }

    static constexpr AlignItems flex_start() {
        return {AlignItemsKeyword::FlexStart};
    }

    static constexpr AlignItems flex_end() {
        return {AlignItemsKeyword::FlexEnd};
    }

    static constexpr AlignItems self_start() {
        return {AlignItemsKeyword::SelfStart};
    }

    static constexpr AlignItems self_end() {
        return {AlignItemsKeyword::SelfEnd};
    }

    static constexpr AlignItems center() {
        return {AlignItemsKeyword::Center};
    }

    static constexpr AlignItems baseline() {
        return {AlignItemsKeyword::Baseline};
    }

    static constexpr AlignItems stretch() {
        return {AlignItemsKeyword::Stretch};
    }

    static constexpr AlignItems safe_start() {
        return {AlignItemsKeyword::Start, AlignmentSafety::Safe};
    }

    static constexpr AlignItems safe_end() {
        return {AlignItemsKeyword::End, AlignmentSafety::Safe};
    }

    static constexpr AlignItems safe_flex_start() {
        return {AlignItemsKeyword::FlexStart, AlignmentSafety::Safe};
    }

    static constexpr AlignItems safe_flex_end() {
        return {AlignItemsKeyword::FlexEnd, AlignmentSafety::Safe};
    }

    static constexpr AlignItems safe_self_start() {
        return {AlignItemsKeyword::SelfStart, AlignmentSafety::Safe};
    }

    static constexpr AlignItems safe_self_end() {
        return {AlignItemsKeyword::SelfEnd, AlignmentSafety::Safe};
    }

    static constexpr AlignItems safe_center() {
        return {AlignItemsKeyword::Center, AlignmentSafety::Safe};
    }

    constexpr bool is_safe() const {
        return safety == AlignmentSafety::Safe;
    }

    constexpr AlignItems with_safety(AlignmentSafety value) const {
        return {keyword, value};
    }

    constexpr bool is_stretch_or_normal() const {
        return keyword == AlignItemsKeyword::Normal || keyword == AlignItemsKeyword::Stretch;
    }

    /// Resolve self-start/end against item/container directions, flipping only on the inline axis.
    constexpr AlignItems resolve_self_relative(Direction item, Direction container,
                                               bool axis_is_inline) const {
        const bool flip = axis_is_inline && item != container;
        auto result = *this;
        if (keyword == AlignItemsKeyword::SelfStart)
            result.keyword = flip ? AlignItemsKeyword::End : AlignItemsKeyword::Start;
        if (keyword == AlignItemsKeyword::SelfEnd)
            result.keyword = flip ? AlignItemsKeyword::Start : AlignItemsKeyword::End;
        return result;
    }

    friend constexpr bool operator==(AlignItems, AlignItems) = default;
};

using AlignSelf = AlignItems;
using JustifyItems = AlignItems;
using JustifySelf = AlignItems;

/// Content alignment/distribution; safe_* factories add safe overflow alignment.
struct AlignContent {
    AlignContentKeyword keyword{AlignContentKeyword::Normal}; ///< Packing/distribution mode.
    AlignmentSafety safety{AlignmentSafety::Default};         ///< Overflow alignment policy.

    static constexpr AlignContent normal() {
        return {};
    }

    static constexpr AlignContent start() {
        return {AlignContentKeyword::Start};
    }

    static constexpr AlignContent end() {
        return {AlignContentKeyword::End};
    }

    static constexpr AlignContent flex_start() {
        return {AlignContentKeyword::FlexStart};
    }

    static constexpr AlignContent flex_end() {
        return {AlignContentKeyword::FlexEnd};
    }

    static constexpr AlignContent center() {
        return {AlignContentKeyword::Center};
    }

    static constexpr AlignContent stretch() {
        return {AlignContentKeyword::Stretch};
    }

    static constexpr AlignContent space_between() {
        return {AlignContentKeyword::SpaceBetween};
    }

    static constexpr AlignContent space_evenly() {
        return {AlignContentKeyword::SpaceEvenly};
    }

    static constexpr AlignContent space_around() {
        return {AlignContentKeyword::SpaceAround};
    }

    static constexpr AlignContent safe_start() {
        return {AlignContentKeyword::Start, AlignmentSafety::Safe};
    }

    static constexpr AlignContent safe_end() {
        return {AlignContentKeyword::End, AlignmentSafety::Safe};
    }

    static constexpr AlignContent safe_flex_start() {
        return {AlignContentKeyword::FlexStart, AlignmentSafety::Safe};
    }

    static constexpr AlignContent safe_flex_end() {
        return {AlignContentKeyword::FlexEnd, AlignmentSafety::Safe};
    }

    static constexpr AlignContent safe_center() {
        return {AlignContentKeyword::Center, AlignmentSafety::Safe};
    }

    constexpr bool is_safe() const {
        return safety == AlignmentSafety::Safe;
    }

    constexpr AlignContent with_safety(AlignmentSafety value) const {
        return {keyword, value};
    }

    friend constexpr bool operator==(AlignContent, AlignContent) = default;
};

using JustifyContent = AlignContent;

/// Reverse start/end and flex-start/end; stretch becomes end, other keywords remain unchanged.
constexpr AlignContentKeyword reversed(AlignContentKeyword value) {
    switch (value) {
    case AlignContentKeyword::Start:
        return AlignContentKeyword::End;
    case AlignContentKeyword::End:
        return AlignContentKeyword::Start;
    case AlignContentKeyword::FlexStart:
        return AlignContentKeyword::FlexEnd;
    case AlignContentKeyword::FlexEnd:
        return AlignContentKeyword::FlexStart;
    case AlignContentKeyword::Stretch:
        return AlignContentKeyword::End;
    default:
        return value;
    }
}

/// Legacy block-child alignment used by the supported text-align extension.
enum class TextAlign : std::uint8_t {
    Auto,        ///< No legacy alignment override.
    LegacyLeft,  ///< -webkit-left physical alignment.
    LegacyRight, ///< -webkit-right physical alignment.
    LegacyCenter ///< -webkit-center alignment.
};
/// CSS float side in physical coordinates.
enum class Float : std::uint8_t {
    None, ///< No floating.
    Left, ///< Float toward the left edge.
    Right ///< Float toward the right edge.
};
/// Physical float side after excluding Float::None.
enum class FloatDirection : std::uint8_t {
    Left, ///< Left edge.
    Right ///< Right edge.
};
/// Sides whose preceding floats must be cleared.
enum class Clear : std::uint8_t {
    None,  ///< No clearance.
    Left,  ///< Clear left floats.
    Right, ///< Clear right floats.
    Both   ///< Clear floats on both sides.
};

/// Whether the float property requests a physical side.
constexpr bool is_floated(Float value) {
    return value != Float::None;
}

/// Convert a float side; Float::None yields nullopt.
constexpr std::optional<FloatDirection> float_direction(Float value) {
    if (value == Float::Left)
        return FloatDirection::Left;
    if (value == Float::Right)
        return FloatDirection::Right;
    return std::nullopt;
}
/// CSS flex main-axis orientation and ordering.
enum class FlexDirection : std::uint8_t {
    Row,          ///< Inline/horizontal main axis.
    Column,       ///< Block/vertical main axis.
    RowReverse,   ///< Reversed row ordering.
    ColumnReverse ///< Reversed column ordering.
};
/// Flex line breaking, including the supported balanced-line extension.
enum class FlexWrap : std::uint8_t {
    NoWrap,        ///< Single flex line.
    Wrap,          ///< Permit multiple lines.
    WrapReverse,   ///< Multiple lines with reversed cross-axis order.
    Balance,       ///< Balance items across flex_line_count lines.
    BalanceReverse ///< Balanced lines with reversed cross-axis order.
};

/// Whether the flex main axis is horizontal, including reverse ordering.
constexpr bool is_row(FlexDirection value) {
    return value == FlexDirection::Row || value == FlexDirection::RowReverse;
}

/// Whether the flex main axis is vertical, including reverse ordering.
constexpr bool is_column(FlexDirection value) {
    return !is_row(value);
}

/// Whether flex main-axis ordering is reversed.
constexpr bool is_reverse(FlexDirection value) {
    return value == FlexDirection::RowReverse || value == FlexDirection::ColumnReverse;
}

/// Physical main axis: horizontal for rows, vertical for columns.
constexpr AbsoluteAxis main_axis(FlexDirection value) {
    return is_row(value) ? AbsoluteAxis::Horizontal : AbsoluteAxis::Vertical;
}

/// Physical axis perpendicular to the flex main axis.
constexpr AbsoluteAxis cross_axis(FlexDirection value) {
    return is_row(value) ? AbsoluteAxis::Vertical : AbsoluteAxis::Horizontal;
}

/// Accepts the native RequestedAxis enum without coupling style.hpp to layout.hpp.
template <class RequestedAxisLike>
constexpr bool main_axis_is_requested_axis(FlexDirection direction, RequestedAxisLike requested) {
    if (requested == RequestedAxisLike::Horizontal)
        return is_row(direction);
    if (requested == RequestedAxisLike::Vertical)
        return is_column(direction);
    return false;
}

/// Whether wrapping/balancing permits more than one flex line.
constexpr bool is_multi_line(FlexWrap value) {
    return value != FlexWrap::NoWrap;
}

/// Whether flex line order on the cross axis is reversed.
constexpr bool is_reverse(FlexWrap value) {
    return value == FlexWrap::WrapReverse || value == FlexWrap::BalanceReverse;
}

/// Whether balanced flex line breaking is requested.
constexpr bool is_balance(FlexWrap value) {
    return value == FlexWrap::Balance || value == FlexWrap::BalanceReverse;
}

/// Layout properties and native item metadata, with the pinned reference's defaults.
/// Absolute lengths use layout units (CSS px); all percentage arguments are
/// fractions, not numbers of percent. Resolution depends on each property's CSS
/// containing-block/container basis. Values are stored without implicit validation;
/// validate() checks structural invariants, not every numeric CSS range.
/// `float` is a C++ keyword, so the corresponding CSS property is named `float_`.
struct Style {
    Display display{Display::Flex};             ///< Box generation and child formatting mode.
    bool item_is_table{false};                  ///< Apply table-item sizing behavior.
    bool item_is_replaced{false};               ///< Item represents externally replaced content.
    bool item_is_compressible_replaced{false};  ///< Additional compressible-replaced sizing flag.
    BoxSizing box_sizing{BoxSizing::BorderBox}; ///< Box measured by specified dimensions.
    Direction direction{Direction::Ltr}; ///< Inline direction used by directional layout rules.
    Point<Overflow> overflow{Overflow::Visible,
                             Overflow::Visible}; ///< Physical x/y overflow modes.
    float scrollbar_width{0};            ///< Reserved scrollbar thickness in layout units (CSS px).
    Contain contain{};                   ///< Supported layout/paint containment effects.
    Float float_{Float::None};           ///< Physical float side (CSS float).
    Clear clear{Clear::None};            ///< Preceding float sides to clear.
    Position position{Position::Static}; ///< CSS positioning scheme.
    /// Physical offsets; horizontal/vertical percentages use the containing block's respective
    /// axis.
    Rect<LengthPercentageAuto> inset{LengthPercentageAuto::auto_(), LengthPercentageAuto::auto_(),
                                     LengthPercentageAuto::auto_(), LengthPercentageAuto::auto_()};
    /// Preferred width/height; percentages use the corresponding containing-block size.
    Size<Dimension> size{Dimension::auto_(), Dimension::auto_()};
    /// Minimum width/height, with percentages based on the corresponding containing-block axis.
    Size<LengthPercentageAuto> min_size{LengthPercentageAuto::auto_(),
                                        LengthPercentageAuto::auto_()};
    /// Maximum width/height; auto means no explicit maximum constraint.
    Size<LengthPercentageAuto> max_size{LengthPercentageAuto::auto_(),
                                        LengthPercentageAuto::auto_()};
    std::optional<float> aspect_ratio{}; ///< Preferred width divided by height; nullopt means auto.
    /// Physical margins; percentage margins use the containing block's inline size (width).
    Rect<LengthPercentageAuto> margin{LengthPercentageAuto::zero(), LengthPercentageAuto::zero(),
                                      LengthPercentageAuto::zero(), LengthPercentageAuto::zero()};
    Rect<LengthPercentage> padding{}; ///< Physical padding; percentages use containing-block width.
    Rect<LengthPercentage>
        border{}; ///< Physical border widths; percentage extension uses containing-block width.
    AlignItems align_items{};              ///< Default cross/block-axis item alignment.
    std::optional<AlignSelf> align_self{}; ///< Item override; nullopt inherits container alignment.
    JustifyItems justify_items{};          ///< Default inline-axis grid item alignment.
    std::optional<JustifySelf> justify_self{}; ///< Inline-axis item override; nullopt inherits.
    AlignContent align_content{};              ///< Cross/block-axis content distribution.
    JustifyContent justify_content{};          ///< Main/inline-axis content distribution.
    Size<LengthPercentage> gap{}; ///< Column gap (width), row gap (height); percentages use
                                  ///< corresponding container size.
    TextAlign text_align{TextAlign::Auto};            ///< Legacy block-child alignment override.
    FlexDirection flex_direction{FlexDirection::Row}; ///< Flex main-axis direction/order.
    FlexWrap flex_wrap{FlexWrap::NoWrap};             ///< Flex line-breaking and ordering policy.
    std::uint16_t flex_line_count{1}; ///< Requested balanced flex line count; must be nonzero.
    Dimension flex_basis{Dimension::auto_()}; ///< Initial main-axis size; percentage basis is
                                              ///< container inner main size.
    float flex_grow{0};   ///< Dimensionless positive-free-space distribution factor.
    float flex_shrink{1}; ///< Dimensionless negative-free-space shrink factor.
    std::vector<GridTemplateComponent>
        grid_template_rows{}; ///< Owned explicit row-track components.
    std::vector<GridTemplateComponent>
        grid_template_columns{};                       ///< Owned explicit column-track components.
    std::vector<TrackSizingFunction> grid_auto_rows{}; ///< Repeating sizes for implicit rows.
    std::vector<TrackSizingFunction> grid_auto_columns{}; ///< Repeating sizes for implicit columns.
    GridAutoFlow grid_auto_flow{GridAutoFlow::Row}; ///< Automatic placement traversal/packing.
    std::optional<GridTemplateAreas>
        grid_template_areas{}; ///< Owned named-area rectangles, if specified.
    TemplateLineNames
        grid_template_column_names{}; ///< Empty or column-component-count + 1 boundary name sets.
    TemplateLineNames
        grid_template_row_names{};     ///< Empty or row-component-count + 1 boundary name sets.
    Line<GridPlacement> grid_row{};    ///< Row start/end placement, with owned names.
    Line<GridPlacement> grid_column{}; ///< Column start/end placement, with owned names.

    BoxGenerationMode box_generation_mode() const {
        return display == Display::None ? BoxGenerationMode::None : BoxGenerationMode::Normal;
    }

    bool is_block() const {
        return display == Display::Block;
    }

    bool is_compressible_replaced() const {
        return item_is_replaced || item_is_compressible_replaced;
    }

    /// Whether external content (e.g. an image) uses the replaced-element sizing rules.
    bool is_replaced() const {
        return item_is_replaced;
    }

    /// Whether to apply the table-specific sizing exceptions indicated by the caller.
    bool is_table() const {
        return item_is_table;
    }

    /// Positioned boxes claim absolute descendants; this style alone does not claim fixed ones.
    ContainingBlockClaims is_containing_block() const {
        return {is_positioned(position), false};
    }

    /// Borrow column placement for Horizontal, row placement for Vertical.
    /// The reference does not extend this Style's lifetime; assignment changes its value.
    const Line<GridPlacement>& grid_placement(AbsoluteAxis axis) const {
        return axis == AbsoluteAxis::Horizontal ? grid_column : grid_row;
    }

    /// Borrow column/row templates by physical axis; no expansion or copying.
    /// The span is invalidated by vector replacement/reallocation or owner destruction.
    std::span<const GridTemplateComponent> grid_template_tracks(AbsoluteAxis axis) const {
        return axis == AbsoluteAxis::Horizontal ? grid_template_columns : grid_template_rows;
    }

    /// Grid content alignment using the naive inline-horizontal/block-vertical mapping.
    AlignContent grid_align_content(AbstractAxis axis) const;
    /// Grid content alignment for an axis, resolving normal to stretch with default safety.
    AlignContent grid_align_content(AbsoluteAxis axis) const;

    /// Rows required by named areas, or zero when no template areas are present.
    std::uint16_t grid_template_area_row_count() const {
        return grid_template_areas ? grid_template_areas->row_count : 0;
    }

    /// Columns required by named areas, or zero when no template areas are present.
    std::uint16_t grid_template_area_column_count() const {
        return grid_template_areas ? grid_template_areas->column_count : 0;
    }

    /// Check structural invariants required by layout; throws invalid_argument on failure.
    void validate() const;
    friend bool operator==(const Style&, const Style&) = default;
};

/// @name Native construction helpers
/// Select an explicit output type when a scalar is ambiguous. Absolute lengths
/// use layout units (CSS px); percentages are fractions. Aggregate helpers copy
/// the same value into every component, preserving its units and domain.
/// @{
template <class T = Dimension>
constexpr T length(float value) {
    return T::length(value);
}

template <class T = Dimension>
constexpr T percent(float value) {
    return T::percent(value);
}

template <class T = Dimension>
constexpr T zero() {
    return T::zero();
}

template <class T = Dimension>
constexpr T auto_() {
    return T::auto_();
}

template <class T = Dimension>
constexpr T min_content() {
    return T::min_content();
}

template <class T = Dimension>
constexpr T max_content() {
    return T::max_content();
}

template <class T = TrackSizingFunction>
constexpr T fit_content(LengthPercentage limit) {
    return T::fit_content(limit);
}

template <class T>
constexpr Size<T> size_all(T value) {
    return {value, value};
}

template <class T>
constexpr Point<T> point_all(T value) {
    return {value, value};
}

template <class T>
constexpr Line<T> line_all(T value) {
    return {value, value};
}

template <class T>
constexpr Rect<T> rect_all(T value) {
    return {value, value, value, value};
}

/// @}

} // namespace taffy
