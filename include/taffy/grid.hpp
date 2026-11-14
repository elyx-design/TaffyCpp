#pragma once

/// @file
/// Application input types for CSS grid tracks, templates, and placement.
/// Use these values to configure Style; layout algorithms live separately.

#include "taffy/geometry.hpp"
#include "taffy/values.hpp"

#include <algorithm>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace taffy {

/// CSS grid automatic placement traversal and hole-filling policy.
enum class GridAutoFlow : std::uint8_t {
    Row,        ///< Fill each row before advancing to the next.
    Column,     ///< Fill each column before advancing to the next.
    RowDense,   ///< Row traversal, backfilling earlier holes.
    ColumnDense ///< Column traversal, backfilling earlier holes.
};

/// Whether automatic placement backfills earlier holes.
constexpr bool is_dense(GridAutoFlow value) {
    return value == GridAutoFlow::RowDense || value == GridAutoFlow::ColumnDense;
}

/// Physical automatic-placement traversal axis (horizontal for row flow).
constexpr AbsoluteAxis primary_axis(GridAutoFlow value) {
    return value == GridAutoFlow::Row || value == GridAutoFlow::RowDense ? AbsoluteAxis::Horizontal
                                                                         : AbsoluteAxis::Vertical;
}

/// A grid track's minmax(min, max) sizing pair; defaults to minmax(auto, auto).
struct TrackSizingFunction {
    MinTrackSizingFunction min{}; ///< Minimum track breadth.
    MaxTrackSizingFunction max{}; ///< Maximum track breadth or flexible factor.
    constexpr TrackSizingFunction() = default;

    constexpr TrackSizingFunction(MinTrackSizingFunction minimum, MaxTrackSizingFunction maximum)
        : min{minimum}, max{maximum} {}

    constexpr TrackSizingFunction(LengthPercentage value) : min{value}, max{value} {}

    constexpr TrackSizingFunction(LengthPercentageAuto value) : min{value}, max{value} {}

    constexpr TrackSizingFunction(Dimension value) : min{value}, max{value} {}

    constexpr TrackSizingFunction(MaxTrackSizingFunction value) : min{value}, max{value} {}

    /// minmax(auto, auto).
    static constexpr TrackSizingFunction auto_() {
        return {};
    }

    /// Both minimum and maximum are zero layout units.
    static constexpr TrackSizingFunction zero() {
        return length(0);
    }

    /// Fixed breadth in layout units (CSS px) for both bounds.
    static constexpr TrackSizingFunction length(float value) {
        return {MinTrackSizingFunction::length(value), MaxTrackSizingFunction::length(value)};
    }

    /// Both bounds as a fraction of the corresponding grid container size.
    static constexpr TrackSizingFunction percent(float value) {
        return {MinTrackSizingFunction::percent(value), MaxTrackSizingFunction::percent(value)};
    }

    /// Both bounds use the same application-owned calculation handle.
    static constexpr TrackSizingFunction calc(Calc value) {
        return {MinTrackSizingFunction::calc(value), MaxTrackSizingFunction::calc(value)};
    }

    /// Both bounds use min-content intrinsic sizing.
    static constexpr TrackSizingFunction min_content() {
        return {MinTrackSizingFunction::min_content(), MaxTrackSizingFunction::min_content()};
    }

    /// Both bounds use max-content intrinsic sizing.
    static constexpr TrackSizingFunction max_content() {
        return {MinTrackSizingFunction::max_content(), MaxTrackSizingFunction::max_content()};
    }

    /// Automatic minimum and fit-content(value) maximum.
    static constexpr TrackSizingFunction fit_content(LengthPercentage value) {
        return {MinTrackSizingFunction::auto_(), MaxTrackSizingFunction::fit_content(value)};
    }

    /// minmax(auto, value fr), preserving an automatic minimum.
    static constexpr TrackSizingFunction fr(float value) {
        return {MinTrackSizingFunction::auto_(), MaxTrackSizingFunction::fr(value)};
    }

    /// minmax(0, value fr), unlike fr()'s automatic minimum.
    static constexpr TrackSizingFunction flex(float value) {
        return {MinTrackSizingFunction::zero(), MaxTrackSizingFunction::fr(value)};
    }

    constexpr MinTrackSizingFunction min_sizing_function() const {
        return min;
    }

    constexpr MaxTrackSizingFunction max_sizing_function() const {
        return max;
    }

    constexpr bool has_fixed_component() const {
        return min.is_length_percentage_or_calc() || max.is_length_percentage_or_calc();
    }

    friend constexpr bool operator==(const TrackSizingFunction&,
                                     const TrackSizingFunction&) = default;
};

/// Pair the supplied minimum and maximum without normalization or validation.
constexpr TrackSizingFunction minmax(MinTrackSizingFunction min, MaxTrackSizingFunction max) {
    return {min, max};
}

constexpr TrackSizingFunction flex(float value) {
    return TrackSizingFunction::flex(value);
}

constexpr TrackSizingFunction fr(float value) {
    return TrackSizingFunction::fr(value);
}

/// CSS repeat() count; defaults to one repetition, with auto modes resolved by layout.
class RepetitionCount {
public:
    /// Fixed count or available-space-dependent repetition policy.
    enum class Kind : std::uint8_t {
        Count,    ///< Stored unsigned count, not validated by construction.
        AutoFill, ///< Fill available space with repeated tracks.
        AutoFit   ///< Fill space and collapse empty repeated tracks.
    };

private:
    Kind kind_{Kind::Count};
    std::uint16_t count_{1};

    constexpr RepetitionCount(Kind kind, std::uint16_t count) : kind_(kind), count_(count) {}

public:
    constexpr RepetitionCount() = default;

    constexpr RepetitionCount(std::uint16_t count) : count_(count) {}

    /// Store a fixed repetition count without rejecting zero.
    static constexpr RepetitionCount count(std::uint16_t count) {
        return {Kind::Count, count};
    }

    /// Repeat to fill available space without collapsing empty tracks.
    static constexpr RepetitionCount auto_fill() {
        return {Kind::AutoFill, 0};
    }

    /// Repeat to fill available space, allowing empty tracks to collapse.
    static constexpr RepetitionCount auto_fit() {
        return {Kind::AutoFit, 0};
    }

    constexpr Kind kind() const {
        return kind_;
    }

    constexpr bool is_auto() const {
        return kind_ != Kind::Count;
    }

    /// Stored fixed count; throws logic_error for auto-fill/auto-fit.
    constexpr std::uint16_t value() const {
        if (is_auto())
            throw std::logic_error("auto repetition has no count");
        return count_;
    }

    friend constexpr bool operator==(RepetitionCount, RepetitionCount) = default;
};

/// Owned names for each track boundary; empty or exactly track-count + 1 entries.
using TemplateLineNames = std::vector<std::vector<std::string>>;

/// Owned repeat() component, with optional names at each internal track boundary.
struct GridTemplateRepetition {
    RepetitionCount count{};                   ///< Fixed/automatic repetition policy.
    std::vector<TrackSizingFunction> tracks{}; ///< Tracks in one repetition.
    TemplateLineNames line_names{};            ///< Empty or tracks.size() + 1 positional name sets.
    /// Number of tracks in one repetition, saturated to uint16_t's maximum.
    std::uint16_t track_count() const;
    /// Throws invalid_argument if the positional line-name count is malformed.
    void validate() const;
    friend bool operator==(const GridTemplateRepetition&, const GridTemplateRepetition&) = default;
};

/// One explicit-grid component: a single track or an owned repeat() group.
struct GridTemplateComponent {
    std::variant<TrackSizingFunction, GridTemplateRepetition> value{
        TrackSizingFunction{}}; ///< Component payload.
    GridTemplateComponent() = default;
    /// Store a single track without further validation.
    GridTemplateComponent(TrackSizingFunction track);
    /// Take ownership of a repetition without further validation.
    GridTemplateComponent(GridTemplateRepetition repetition);
    /// Construct a single-track component.
    static GridTemplateComponent single(TrackSizingFunction track);
    /// Take ownership of a repeat component; does not call validate().
    static GridTemplateComponent repeat(GridTemplateRepetition repetition);
    /// Construct a track with automatic minimum and maximum sizing.
    static GridTemplateComponent auto_();
    /// Construct a zero-length track.
    static GridTemplateComponent zero();
    /// Construct a fixed track in layout units (CSS px).
    static GridTemplateComponent length(float value);
    /// Construct a percentage track; 0.5 means 50% of the grid's corresponding size.
    static GridTemplateComponent percent(float value);
    /// Construct an auto-minimum track with a flexible maximum in fr units.
    static GridTemplateComponent fr(float value);
    /// Construct a min-content track.
    static GridTemplateComponent min_content();
    /// Construct a max-content track.
    static GridTemplateComponent max_content();
    /// Construct an auto-minimum track capped by fit-content(value).
    static GridTemplateComponent fit_content(LengthPercentage value);
    /// Whether this component is an auto-fill or auto-fit repetition.
    bool is_auto_repetition() const;
    friend bool operator==(const GridTemplateComponent&, const GridTemplateComponent&) = default;
};

/// Own a repeat() group; throws invalid_argument for malformed positional line names.
GridTemplateComponent repeat(RepetitionCount count, std::vector<TrackSizingFunction> tracks,
                             TemplateLineNames line_names = {});
/// Return one repeat(count, minmax(0, 1fr)) component; zero count is retained.
std::vector<GridTemplateComponent> evenly_sized_tracks(std::uint16_t count);

/// Owned track sequence with optional positional boundary names.
template <class Track = GridTemplateComponent>
struct GridTemplateTracks {
    std::vector<Track> tracks{};    ///< Explicit components or expanded sizing functions.
    TemplateLineNames line_names{}; ///< Empty or tracks.size() + 1 boundary name sets.

    /// Throws invalid_argument when nonempty line_names has the wrong boundary count.
    void validate() const {
        if (!line_names.empty() && line_names.size() != tracks.size() + 1)
            throw std::invalid_argument("grid line names must have tracks.size() + 1 entries");
    }

    friend bool operator==(const GridTemplateTracks&, const GridTemplateTracks&) = default;
};

/// Repeating sequence of sizing functions for implicit grid tracks.
using GridAutoTracks = std::vector<TrackSizingFunction>;
/// Expand repeats and merge names on shared boundaries, bounded by the track limit.
/// Caller supplies the automatic repetition count calculated by the grid sizing algorithm.
GridTemplateTracks<TrackSizingFunction>
expand_grid_template(std::span<const GridTemplateComponent> components,
                     const TemplateLineNames& line_names = {}, std::uint16_t auto_repetitions = 1,
                     std::uint16_t track_limit = 10000);

/// Named rectangular area, bounded by one-based CSS lines (end edges are exclusive).
struct GridTemplateArea {
    std::string name;             ///< Owned, nonempty area identifier.
    std::uint16_t row_start{};    ///< First row line, inclusive.
    std::uint16_t row_end{};      ///< Last row line, exclusive.
    std::uint16_t column_start{}; ///< First column line, inclusive.
    std::uint16_t column_end{};   ///< Last column line, exclusive.
    friend bool operator==(const GridTemplateArea&, const GridTemplateArea&) = default;
};

/// Owned rectangular named-area template and its track dimensions.
struct GridTemplateAreas {
    std::vector<GridTemplateArea> areas{}; ///< Non-overlapping, uniquely named rectangles.
    std::uint16_t row_count{};             ///< Number of row tracks, not grid lines.
    std::uint16_t column_count{};          ///< Number of column tracks, not grid lines.
    /// Throws invalid_argument for empty/duplicate names, invalid bounds, or overlapping
    /// rectangles.
    void validate() const;
    friend bool operator==(const GridTemplateAreas&, const GridTemplateAreas&) = default;
};

/// Owned name and index describing a named grid line.
struct NamedGridLine {
    std::string name;      ///< Owned line identifier.
    std::uint16_t index{}; ///< Line index in the associated grid-line coordinate system.
    friend bool operator==(const NamedGridLine&, const NamedGridLine&) = default;
};

/// CSS line indices are one-based; negative indices count from the explicit grid's end.
/// Origin-zero coordinates are signed 32-bit values to avoid overflow during span arithmetic.
struct GridLine {
    std::int16_t value{}; ///< Stored CSS index; zero is rejected only when resolving.
    constexpr GridLine() = default;

    explicit constexpr GridLine(std::int16_t index) : value(index) {}

    constexpr std::int16_t as_i16() const {
        return value;
    }

    /// Convert using explicit track count, clamping to [-10000, 10000].
    /// Throws invalid_argument for CSS line zero.
    constexpr std::int32_t into_origin_zero_line(std::uint16_t explicit_tracks) const {
        if (value == 0)
            throw std::invalid_argument("CSS grid line zero is invalid");
        const auto index =
            value > 0 ? value - 1 : static_cast<std::int32_t>(explicit_tracks) + 1 + value;
        return std::clamp<std::int32_t>(index, -10000, 10000);
    }

    friend constexpr auto operator<=>(GridLine, GridLine) = default;
};

/// Maximum explicit/expanded track count used by grid resolution.
inline constexpr std::uint16_t max_grid_tracks = 10000;

/// CSS grid placement with owned optional names; defaults to auto.
class GridPlacement {
public:
    /// Stored placement form, before named-line and origin-zero resolution.
    enum class Kind : std::uint8_t {
        Auto,      ///< Automatic placement.
        Line,      ///< Numeric CSS grid line.
        NamedLine, ///< Named line and optional occurrence.
        Span,      ///< Numeric track span.
        NamedSpan  ///< Named-line span and optional count.
    };

private:
    Kind kind_{Kind::Auto};
    std::int16_t line_{};
    std::uint16_t span_{};
    std::string name_{};

public:
    GridPlacement() = default;
    /// Use automatic placement.
    static GridPlacement auto_();
    /// Store a one-based CSS line (negative counts from the end); zero acts as auto.
    static GridPlacement line(std::int16_t index);
    /// Store the given CSS grid line without validation.
    static GridPlacement line(GridLine index);
    /// Store a track span; zero is normalized by subsequent placement resolution.
    static GridPlacement span(std::uint16_t count);
    /// Own a named line; occurrence zero denotes a bare area/name reference.
    static GridPlacement named_line(std::string name, std::int16_t occurrence = 0);
    /// Own a named span; count zero denotes the omitted CSS count.
    static GridPlacement named_span(std::string name, std::uint16_t count = 0);

    Kind kind() const {
        return kind_;
    }

    bool is_auto() const {
        return kind_ == Kind::Auto || (kind_ == Kind::Line && line_ == 0);
    }

    bool is_definite() const {
        return kind_ == Kind::NamedLine || (kind_ == Kind::Line && line_ != 0);
    }

    bool is_named() const {
        return kind_ == Kind::NamedLine || kind_ == Kind::NamedSpan;
    }

    /// Return the stored CSS index/occurrence; throws logic_error for non-line kinds.
    std::int16_t line_index() const;
    /// Return the stored count; throws logic_error for non-span kinds.
    std::uint16_t span_count() const;
    /// Borrow the owned name; throws logic_error for unnamed kinds.
    const std::string& name() const;
    friend bool operator==(const GridPlacement&, const GridPlacement&) = default;
};

/// Construct a numeric CSS line placement (zero acts as auto).
GridPlacement line(std::int16_t index);
/// Construct a numeric track span without immediate normalization.
GridPlacement span(std::uint16_t count);
/// Whether either edge supplies a definite numeric or named line.
bool is_definite(const Line<GridPlacement>& placement);

} // namespace taffy
