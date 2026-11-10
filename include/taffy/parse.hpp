#pragma once

/// @file
/// Optional CSS-value/declaration parsing conveniences. Construct Style directly
/// if parsing is unnecessary; no XML parser or external dependency is used here.

#include "taffy/style.hpp"

#include <charconv>
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <type_traits>

namespace taffy {

/// Classification of a rejected CSS value or declaration.
enum class ParseErrorCode {
    InvalidSyntax, ///< Malformed tokens, missing components, or unexpected trailing input.
    InvalidValue,  ///< Recognized input with an invalid keyword, range, or property name.
    Unsupported    ///< The requested C++ type has no supported CSS value grammar.
};

/// Owned diagnostic; no references into the parsed input are retained.
struct ParseError {
    ParseErrorCode code{ParseErrorCode::InvalidSyntax}; ///< Reason category.
    std::size_t
        offset{}; ///< Zero-based byte offset in the original UTF-8 argument, not a character index.
    std::string message; ///< Owned human-readable explanation.
    friend bool operator==(const ParseError&, const ParseError&) = default;
};

/// A parsed, owned value or an owned diagnostic (ordinary allocation exceptions may propagate).
template <class T>
using ParseResult = std::expected<T, ParseError>;

namespace detail {
// Views borrow input for the duration of generic parsing. Comments are skipped,
// never concatenated; original spelling and byte positions are preserved.
ParseResult<std::vector<std::string_view>> css_components(std::string_view input);
ParseResult<std::vector<std::string_view>> css_function_arguments(std::string_view input,
                                                                  std::string_view name);
bool css_auto(std::string_view input);
ParseResult<std::string_view> css_integer_token(std::string_view input);
template <class T>
struct CssValueParser;
} // namespace detail

/// Parse an entire value using the pinned upstream optional CSS grammar.
/// Length units/keywords have upstream case sensitivity. Most keyword enums are
/// ASCII-insensitive; grid-auto-flow instead requires lowercase tokens. Lengths
/// use px, except AvailableSpace accepts dimension suffixes without conversion
/// (see parse_available_space()). Percentages are fractions (50% becomes 0.5).
/// Numeric overflow and integer saturation follow cssparser; values are
/// not silently normalized. calc()/var() are errors: Calc is an application-owned
/// opaque handle, not an expression owned by the parser.
///
/// Types with no CSS grammar (opaque handles and algorithm metadata) return
/// Unsupported. Generic optional values accept auto as nullopt; vectors parse
/// nonempty component lists. Generic Size/Point/Line/axis pairs accept one or two
/// values. Line<GridPlacement> instead accepts start optionally followed by / end;
/// omitted ends follow grid-shorthand defaults. Rect uses CSS top/right/bottom/left
/// shorthand order. Native integer scalar
/// conveniences are range checked, unlike saturating grid coordinates.
///
/// Input is borrowed only during this call; returned public values and diagnostics
/// own their strings. Error offsets include original trivia/escapes and may equal
/// input.size() for an error at end of input. Parsing does not invoke CalcResolver.
template <class T>
ParseResult<T> parse_css(std::string_view input) {
    return detail::CssValueParser<T>::parse(input);
}

/// @name Concrete CSS value grammars
/// Specializations share parse_css() ownership, whole-input, and diagnostic rules.
/// @{
template <>
ParseResult<float> parse_css<float>(std::string_view input);
template <>
ParseResult<double> parse_css<double>(std::string_view input);
template <>
ParseResult<bool> parse_css<bool>(std::string_view input);
template <>
ParseResult<std::int16_t> parse_css<std::int16_t>(std::string_view input);
template <>
ParseResult<std::uint16_t> parse_css<std::uint16_t>(std::string_view input);
template <>
ParseResult<std::int32_t> parse_css<std::int32_t>(std::string_view input);
template <>
ParseResult<std::uint32_t> parse_css<std::uint32_t>(std::string_view input);
template <>
ParseResult<std::int64_t> parse_css<std::int64_t>(std::string_view input);
template <>
ParseResult<std::uint64_t> parse_css<std::uint64_t>(std::string_view input);
template <>
ParseResult<std::string> parse_css<std::string>(std::string_view input);
template <>
ParseResult<LengthPercentage> parse_css<LengthPercentage>(std::string_view input);
template <>
ParseResult<LengthPercentageAuto> parse_css<LengthPercentageAuto>(std::string_view input);
template <>
ParseResult<Dimension> parse_css<Dimension>(std::string_view input);
template <>
ParseResult<AvailableSpace> parse_css<AvailableSpace>(std::string_view input);
template <>
ParseResult<Display> parse_css<Display>(std::string_view input);
template <>
ParseResult<Position> parse_css<Position>(std::string_view input);
template <>
ParseResult<BoxSizing> parse_css<BoxSizing>(std::string_view input);
template <>
ParseResult<Direction> parse_css<Direction>(std::string_view input);
template <>
ParseResult<Overflow> parse_css<Overflow>(std::string_view input);
template <>
ParseResult<Contain> parse_css<Contain>(std::string_view input);
template <>
ParseResult<AlignItems> parse_css<AlignItems>(std::string_view input);
template <>
ParseResult<AlignContent> parse_css<AlignContent>(std::string_view input);
template <>
ParseResult<TextAlign> parse_css<TextAlign>(std::string_view input);
template <>
ParseResult<Float> parse_css<Float>(std::string_view input);
template <>
ParseResult<Clear> parse_css<Clear>(std::string_view input);
template <>
ParseResult<FlexDirection> parse_css<FlexDirection>(std::string_view input);
template <>
ParseResult<FlexWrap> parse_css<FlexWrap>(std::string_view input);
template <>
ParseResult<GridAutoFlow> parse_css<GridAutoFlow>(std::string_view input);
template <>
ParseResult<MinTrackSizingFunction> parse_css<MinTrackSizingFunction>(std::string_view input);
template <>
ParseResult<MaxTrackSizingFunction> parse_css<MaxTrackSizingFunction>(std::string_view input);
template <>
ParseResult<TrackSizingFunction> parse_css<TrackSizingFunction>(std::string_view input);
template <>
ParseResult<RepetitionCount> parse_css<RepetitionCount>(std::string_view input);
template <>
ParseResult<GridPlacement> parse_css<GridPlacement>(std::string_view input);
template <>
ParseResult<GridLine> parse_css<GridLine>(std::string_view input);
template <>
ParseResult<GridTemplateRepetition> parse_css<GridTemplateRepetition>(std::string_view input);
template <>
ParseResult<GridTemplateComponent> parse_css<GridTemplateComponent>(std::string_view input);
template <>
ParseResult<GridTemplateTracks<>> parse_css<GridTemplateTracks<>>(std::string_view input);
template <>
ParseResult<GridTemplateTracks<TrackSizingFunction>>
parse_css<GridTemplateTracks<TrackSizingFunction>>(std::string_view input);
template <>
ParseResult<GridAutoTracks> parse_css<GridAutoTracks>(std::string_view input);
template <>
ParseResult<GridTemplateAreas> parse_css<GridTemplateAreas>(std::string_view input);
template <>
ParseResult<TemplateLineNames> parse_css<TemplateLineNames>(std::string_view input);
template <>
ParseResult<Line<GridPlacement>> parse_css<Line<GridPlacement>>(std::string_view input);
template <>
ParseResult<Style> parse_css<Style>(std::string_view input);
/// @}

/// @name Named value parsers
/// Convenience entry points equivalent to parse_css<ReturnType>(input).
/// @{
/// Parse a unitless CSS number as float.
ParseResult<float> parse_number(std::string_view input);
/// Parse the ASCII-insensitive true/false keywords.
ParseResult<bool> parse_boolean(std::string_view input);
/// Parse a dimension, including its supported intrinsic sizing keywords.
ParseResult<Dimension> parse_dimension(std::string_view input);
/// Parse a px length or percentage fraction; auto is not accepted.
ParseResult<LengthPercentage> parse_length_percentage(std::string_view input);
/// Parse a px length, percentage fraction, or auto.
ParseResult<LengthPercentageAuto> parse_length_percentage_auto(std::string_view input);
/// Parse min-content/max-content or a nonnegative number/dimension. Dimension
/// suffixes are accepted without validation or conversion: 2em becomes definite(2).
ParseResult<AvailableSpace> parse_available_space(std::string_view input);
/// Parse the CSS display mode.
ParseResult<Display> parse_display(std::string_view input);
/// Parse the CSS positioning scheme.
ParseResult<Position> parse_position(std::string_view input);
/// Parse border-box or content-box.
ParseResult<BoxSizing> parse_box_sizing(std::string_view input);
/// Parse ltr or rtl.
ParseResult<Direction> parse_direction(std::string_view input);
/// Parse an overflow keyword.
ParseResult<Overflow> parse_overflow(std::string_view input);
/// Parse supported containment flags; style containment has no stored layout bit.
ParseResult<Contain> parse_contain(std::string_view input);
/// Parse item alignment and an optional valid safe/unsafe modifier.
ParseResult<AlignItems> parse_align_items(std::string_view input);
/// Parse an AlignSelf value (not the optional property's auto spelling).
ParseResult<AlignSelf> parse_align_self(std::string_view input);
/// Parse inline-axis item alignment.
ParseResult<JustifyItems> parse_justify_items(std::string_view input);
/// Parse a JustifySelf value (not the optional property's auto spelling).
ParseResult<JustifySelf> parse_justify_self(std::string_view input);
/// Parse content alignment/distribution and optional safety.
ParseResult<AlignContent> parse_align_content(std::string_view input);
/// Parse inline-axis content alignment/distribution.
ParseResult<JustifyContent> parse_justify_content(std::string_view input);
/// Parse auto or supported legacy -webkit alignment keywords.
ParseResult<TextAlign> parse_text_align(std::string_view input);
/// Parse none, left, or right floating.
ParseResult<Float> parse_float(std::string_view input);
/// Parse the sides on which floats must be cleared.
ParseResult<Clear> parse_clear(std::string_view input);
/// Parse the flex main-axis direction.
ParseResult<FlexDirection> parse_flex_direction(std::string_view input);
/// Parse nowrap/wrap/wrap-reverse and the supported balance modifier.
ParseResult<FlexWrap> parse_flex_wrap(std::string_view input);
/// Parse row/column automatic placement with optional dense packing.
ParseResult<GridAutoFlow> parse_grid_auto_flow(std::string_view input);
/// Parse a grid minimum track breadth.
ParseResult<MinTrackSizingFunction> parse_min_track_sizing_function(std::string_view input);
/// Parse a grid maximum track breadth, including fr or fit-content(...).
ParseResult<MaxTrackSizingFunction> parse_max_track_sizing_function(std::string_view input);
/// Parse one track breadth or minmax(...).
ParseResult<TrackSizingFunction> parse_track_sizing_function(std::string_view input);
/// Parse a positive repeat count (saturated to uint16_t), auto-fit, or auto-fill.
ParseResult<RepetitionCount> parse_repetition_count(std::string_view input);
/// Parse a grid line, name, span, or auto; returned names are owned.
ParseResult<GridPlacement> parse_grid_placement(std::string_view input);
/// Parse a nonzero CSS line integer, saturated to int16_t.
ParseResult<GridLine> parse_grid_line(std::string_view input);
/// Parse repeat(...) with owned tracks and positional line names.
ParseResult<GridTemplateRepetition> parse_grid_template_repetition(std::string_view input);
/// Parse one track or repeat(...) component.
ParseResult<GridTemplateComponent> parse_grid_template_component(std::string_view input);
/// Parse a nonempty explicit track list with optional bracketed line names.
ParseResult<GridTemplateTracks<>> parse_grid_template_tracks(std::string_view input);
/// Parse a nonempty list of implicit grid track sizes.
ParseResult<GridAutoTracks> parse_grid_auto_tracks(std::string_view input);
/// Parse quoted rectangular area rows or none; returned area names are owned.
ParseResult<GridTemplateAreas> parse_grid_template_areas(std::string_view input);
/// @}

/// Apply one property atomically: style is unchanged on a reported parse error.
/// Unknown properties and invalid values are errors. Native scalar fields use
/// direct Rust-like f32 conversion after skipping only surrounding CSS trivia;
/// interior tokens are never concatenated. Value errors refer to byte offsets in
/// the original value argument, including comments/escapes. Unknown-property
/// diagnostics refer to the property argument. Inputs are not retained.
ParseResult<void> parse_style_property(Style& style, std::string_view property,
                                       std::string_view value);
/// Parse a declaration list (not selectors/stylesheets) into an owned Style.
/// Invalid declarations are reported rather than silently skipped. Source order
/// and !important are honored, including overlapping shorthands/longhands.
/// Custom properties, selectors, and stylesheets are rejected. Error byte offsets
/// refer to input; no references to that input survive the call.
ParseResult<Style> parse_style(std::string_view input);

} // namespace taffy

#include "taffy/detail/parse_impl.hpp"
