#pragma once
/** @file
 * Private CSS Grid records and reusable algorithm operations.
 * Unless stated otherwise, track spans borrow caller storage for the duration of a call.
 * Mutable spans update that storage in place. Callbacks run synchronously and must not
 * invalidate borrowed storage; allocation and callback exceptions propagate.
 *
 * Origin-zero grid lines place line 0 at the explicit grid's start; negative lines
 * precede it. Track vectors interleave gutters at even indexes and tracks at odd
 * indexes, including a gutter at each outer edge (2 * track_count + 1 entries).
 * Logical vector order is retained even when physical offsets are reversed for RTL.
 */
#include "grid/placement_types.hpp"

#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <taffy/tree.hpp>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

// types

namespace taffy::grid {
/// Per-axis size; nullopt denotes an indefinite dimension.
using OptionalSize = Size<std::optional<float>>;
/// Distinguishes a content track from an interleaved gap/outer line gutter.
enum class GridTrackKind {
    Track, ///< A row or column, including collapsed auto-fit tracks.
    Gutter ///< A gap between tracks or a collapsed outer-edge gutter.
};

/** Mutable state for one track-sizing pass, in layout length units.
 * offset is border-box-relative after alignment; base_size and growth_limit are
 * the current sizing bounds. Planned/item increases are scratch state consumed
 * by the distribution/flush operations. Collapse changes sizing functions, not
 * already-computed base sizes or offsets: reinitialize sizes after collapsing.
 */
struct GridTrack {
    GridTrackKind kind = GridTrackKind::Track;
    bool is_collapsed = false;
    MinTrackSizingFunction min_track_sizing_function{};
    MaxTrackSizingFunction max_track_sizing_function{};
    float offset = 0;
    float base_size = 0;
    float growth_limit = 0;
    float content_alignment_adjustment = 0;
    float item_incurred_increase = 0;
    float base_size_planned_increase = 0;
    float growth_limit_planned_increase = 0;
    bool infinitely_growable = false;
    float resolved_fit_content_limit = std::numeric_limits<float>::infinity();
    /// Construct default sizing-pass state.
    GridTrack() = default;

    /// Construct a content track with the supplied minimum/maximum functions.
    GridTrack(MinTrackSizingFunction min, MaxTrackSizingFunction max)
        : min_track_sizing_function(min), max_track_sizing_function(max) {}

    /// Factory equivalent to the two-function constructor.
    static GridTrack new_(MinTrackSizingFunction min, MaxTrackSizingFunction max) {
        return {min, max};
    }

    /// Construct a gutter whose minimum and maximum both resolve from size.
    static GridTrack gutter(LengthPercentage size);
    /// Mark collapsed, zero both sizing functions, and reset the fit-content cap.
    void collapse();

    /// Whether the maximum sizing function is an fr value.
    bool is_flexible() const {
        return max_track_sizing_function.is_fr();
    }

    /// Whether either sizing function contains a percentage.
    bool uses_percentage() const;
    /// Whether either sizing function is intrinsic.
    bool has_intrinsic_sizing_function() const;
    /// Resolve and store the fit-content cap; unresolved/no cap becomes infinity.
    void resolve_fit_content_limit(std::optional<float> basis, const CalcResolver& calc);

    /// Last resolved cap (infinity before resolution or when unconstrained).
    float fit_content_limit() const {
        return resolved_fit_content_limit;
    }

    /// Current growth limit capped by the resolved fit-content limit.
    float fit_content_limited_growth_limit() const {
        return f32_min(growth_limit, resolved_fit_content_limit);
    }

    /// fr factor, or zero for a non-flexible track.
    float flex_factor() const {
        return is_flexible() ? max_track_sizing_function.value() : 0;
    }
};

/// Synchronous size estimator; nullopt means this track cannot yet be resolved.
using TrackSizeEstimate =
    std::function<std::optional<float>(const GridTrack&, std::optional<float>)>;

/// Known dimensions keyed by the grid-area size used to resolve them.
struct KnownDimensionsCacheEntry {
    OptionalSize grid_area_size;
    OptionalSize known_dimensions;
};

/** Style snapshot, placement and mutable measurement state for a grid child.
 * row/column are half-open origin-zero line ranges. row_indexes/column_indexes
 * identify bounding gutters in interleaved vectors, not content-track indexes.
 * Initialize those indexes before span-based queries. Cache fields belong to a
 * single sizing pass: callers must clear them when styles, tracks, available
 * space or measurement inputs change. Only known_dimensions_cache keys by area.
 * Measurement operations may call into and mutate the supplied LayoutTree.
 */
struct GridItem {
    NodeId node{};
    std::size_t source_order = 0;
    Line<std::int32_t> row{};
    Line<std::int32_t> column{};
    bool is_compressible_replaced = false;
    bool is_replaced = false;
    Point<Overflow> overflow{};
    BoxSizing box_sizing{};
    Size<Dimension> size{};
    Size<LengthPercentageAuto> min_size{};
    Size<LengthPercentageAuto> max_size{};
    std::optional<float> aspect_ratio{};
    Rect<LengthPercentage> padding{};
    Rect<LengthPercentage> border{};
    Rect<LengthPercentageAuto> margin{};
    AlignSelf align_self{};
    AlignSelf justify_self{};
    std::optional<float> baseline{};
    float baseline_shim = 0;
    Line<std::uint16_t> row_indexes{};
    Line<std::uint16_t> column_indexes{};
    bool crosses_flexible_row = false;
    bool crosses_flexible_column = false;
    bool crosses_intrinsic_row = false;
    bool crosses_intrinsic_column = false;
    std::optional<OptionalSize> grid_area_size_cache{};
    std::optional<KnownDimensionsCacheEntry> known_dimensions_cache{};
    OptionalSize min_content_contribution_cache{};
    OptionalSize minimum_contribution_cache{};
    OptionalSize max_content_contribution_cache{};
    float y_position = 0;
    float height = 0;
    OofCandidates oof_candidates{};

    /// Construct empty placement and measurement state.
    GridItem() = default;
    /// Snapshot style, inheriting missing self-alignment from the parent values.
    GridItem(NodeId node, const Style& style, AlignItems parent_align_items,
             AlignItems parent_justify_items, std::size_t source_order);

    /// Factory equivalent to the style-snapshot constructor; order is source order.
    static GridItem new_with_style_and_order(NodeId node, const Style& style, AlignItems align,
                                             AlignItems justify, std::size_t order) {
        return {node, style, align, justify, order};
    }

    /// Whether either top or bottom margin is auto.
    bool has_auto_block_margin() const;
    /// Percentage block size crossing an intrinsic/flexible row (uses cached flags).
    bool has_cyclic_block_size_dependency() const;
    /// Baseline self-alignment without auto block margins or cyclic block sizing.
    bool participates_in_baseline_alignment() const;
    /// Origin-zero bounding lines: Block selects rows, Inline selects columns.
    Line<std::int32_t> placement(AbstractAxis axis) const;
    /// Bounding gutter indexes in the corresponding interleaved track vector.
    Line<std::uint16_t> placement_indexes(AbstractAxis axis) const;
    /// Half-open [start gutter + 1, end gutter) range, including interior gutters.
    std::pair<std::size_t, std::size_t> track_range_excluding_lines(AbstractAxis axis) const;
    /// Borrow the spanned tracks and interior gutters; throws std::out_of_range
    /// if the cached bounding indexes do not describe a valid subspan.
    std::span<const GridTrack> spanned_tracks(AbstractAxis axis,
                                              std::span<const GridTrack> tracks) const;
    /// Mutable borrowed version; same range checks and lifetime as the input span.
    std::span<GridTrack> spanned_tracks(AbstractAxis axis, std::span<GridTrack> tracks) const;
    /// Test any spanned entry, including interior gutters; propagates range/callback errors.
    bool spans_track_matching(AbstractAxis axis, std::span<const GridTrack> tracks,
                              const std::function<bool(const GridTrack&)>& predicate) const;
    /// End minus start line; throws std::overflow_error for negative/oversized spans.
    std::uint16_t span(AbstractAxis axis) const;
    /// Cached flexible-track crossing flag; does not inspect track storage.
    bool crosses_flexible_track(AbstractAxis axis) const;
    /// Cached intrinsic-track crossing flag; does not inspect track storage.
    bool crosses_intrinsic_track(AbstractAxis axis) const;
    /// Sum definite maximum limits (including gutters), or nullopt if any is indefinite.
    /// Uses spanned_tracks() range checks and basis for percentage/calc resolution.
    std::optional<float> spanned_track_limit(AbstractAxis axis, std::span<const GridTrack> tracks,
                                             std::optional<float> basis,
                                             const CalcResolver& calc) const;
    /// Sum definite fixed maximum values, or nullopt; same range checks as spanned_tracks().
    std::optional<float> spanned_fixed_track_limit(AbstractAxis axis,
                                                   std::span<const GridTrack> tracks,
                                                   std::optional<float> basis,
                                                   const CalcResolver& calc) const;
    /// Resolve dimensions, reusing/replacing the cache keyed by grid_area_size.
    OptionalSize known_dimensions_cached(LayoutTree& tree, OptionalSize grid_area_size);
    /// Stretch, or normal alignment on a non-replaced item.
    bool is_stretch_aligned(AlignSelf alignment) const;
    /// Resolve border-box dimensions from area, style, aspect ratio and alignment.
    OptionalSize known_dimensions(LayoutTree& tree, OptionalSize grid_area_size) const;
    /// Sum fixed tracks on axis and estimated tracks on the other axis; an unresolved
    /// entry makes that dimension indefinite. Borrows spans and invokes estimate/calc.
    OptionalSize grid_area_size(AbstractAxis axis, std::span<const GridTrack> tracks,
                                std::span<const GridTrack> other_tracks, OptionalSize available,
                                const TrackSizeEstimate& estimate, const CalcResolver& calc) const;
    /// Cache the first grid_area_size() result, without keying by arguments; reset
    /// grid_area_size_cache before changing track sizes or measurement conditions.
    OptionalSize grid_area_size_cached(AbstractAxis axis, std::span<const GridTrack> tracks,
                                       std::span<const GridTrack> other_tracks,
                                       OptionalSize available, const TrackSizeEstimate& estimate,
                                       const CalcResolver& calc);
    /// Margin sums plus block-start baseline shim; inline percentages use zero,
    /// block percentages use width. Auto/unresolved margins resolve to zero.
    Size<float> margins_axis_sums_with_baseline_shims(std::optional<float> width,
                                                      const LayoutTree& tree) const;
    /// Measure min-content border-box contribution (excluding margins); may fill known cache.
    float min_content_contribution(AbstractAxis axis, LayoutTree& tree, OptionalSize area,
                                   OptionalSize available);
    /// Reuse first min-content result per axis; caller invalidates on input changes.
    float min_content_contribution_cached(AbstractAxis axis, LayoutTree& tree, OptionalSize area,
                                          OptionalSize available);
    /// Measure max-content border-box contribution (excluding margins); may fill known cache.
    float max_content_contribution(AbstractAxis axis, LayoutTree& tree, OptionalSize area,
                                   OptionalSize available);
    /// Reuse first max-content result per axis; caller invalidates on input changes.
    float max_content_contribution_cached(AbstractAxis axis, LayoutTree& tree, OptionalSize area,
                                          OptionalSize available);
    /// Subtract resolved margins and baseline shim; clamp definite remainders to zero.
    OptionalSize available_space_minus_margins(OptionalSize area, OptionalSize available,
                                               const LayoutTree& tree) const;
    /// Replace measurement constraints selected by intrinsic sizing keywords.
    Size<AvailableSpace> keyword_adjusted_available_space(OptionalSize area,
                                                          Size<AvailableSpace> available,
                                                          const LayoutTree& tree) const;
    /// Resolve CSS automatic/minimum contribution, measuring when needed; tracks
    /// must contain the item's cached span. Range/callback/measurement errors propagate.
    float minimum_contribution(LayoutTree& tree, AbstractAxis axis,
                               std::span<const GridTrack> tracks, OptionalSize area,
                               OptionalSize inner);
    /// Reuse first minimum contribution per axis; caller invalidates on input changes.
    float minimum_contribution_cached(LayoutTree& tree, AbstractAxis axis,
                                      std::span<const GridTrack> tracks, OptionalSize area,
                                      OptionalSize inner);
};
} // namespace taffy::grid

// placement

namespace taffy::grid {

struct GridTrack;
struct GridItem;

/// Advance one line, capped above at INT16_MAX (not max_grid_tracks).
/// @pre position + 1 must be representable as int32; no lower-bound clamp is applied.
std::int32_t advance_position(std::int32_t position);
/// Form [position, position + span], clamping each endpoint to int16 bounds.
/// @pre position + span must be representable as int32.
Line<std::int32_t> resolve_indefinite_grid_span(std::int32_t position, std::uint16_t span);

/** Track counts on each side of the explicit grid (gutters excluded).
 * Conversion arithmetic is unchecked and does not clamp to max_grid_tracks.
 * Line-range and track-range conversions preserve half-open endpoints.
 */
struct TrackCounts {
    std::uint16_t negative_implicit{};
    std::uint16_t explicit_{};
    std::uint16_t positive_implicit{};

    /// Construct counts without validation or clamping.
    static constexpr TrackCounts from_raw(std::uint16_t negative, std::uint16_t explicit_count,
                                          std::uint16_t positive) {
        return {negative, explicit_count, positive};
    }

    /// Total content tracks (no gutters).
    constexpr std::size_t len() const {
        return std::size_t(negative_implicit) + explicit_ + positive_implicit;
    }

    /// First implicit-grid line, at minus the negative implicit track count.
    constexpr std::int32_t implicit_start_line() const {
        return -std::int32_t(negative_implicit);
    }

    /// Last implicit-grid line, explicit count plus positive implicit count.
    constexpr std::int32_t implicit_end_line() const {
        return std::int32_t(explicit_) + positive_implicit;
    }

    /// Alias for implicit_start_line().
    constexpr std::int32_t implicit_start() const {
        return implicit_start_line();
    }

    /// Alias for implicit_end_line().
    constexpr std::int32_t implicit_end() const {
        return implicit_end_line();
    }

    /// Origin-zero line to the zero-based content track following it (end may be len()).
    /// @pre The addition must be representable as int32.
    constexpr std::int32_t oz_line_to_next_track(std::int32_t line) const {
        return line + negative_implicit;
    }

    /// Zero-based content track to its preceding origin-zero line (unchecked).
    /// @pre The subtraction must be representable as int32.
    constexpr std::int32_t track_to_prev_oz_line(std::int32_t track) const {
        return track - negative_implicit;
    }

    /// Convert both origin-zero line endpoints to content-track endpoints.
    constexpr Line<std::int32_t> oz_line_range_to_track_range(Line<std::int32_t> lines) const {
        return {oz_line_to_next_track(lines.start), oz_line_to_next_track(lines.end)};
    }

    /// Convert both content-track endpoints back to origin-zero lines.
    constexpr Line<std::int32_t> track_range_to_oz_line_range(Line<std::int32_t> tracks) const {
        return {track_to_prev_oz_line(tracks.start), track_to_prev_oz_line(tracks.end)};
    }

    /// Bounding gutter index (2 * content-track endpoint), or nullopt outside the grid.
    std::optional<std::size_t> try_track_vec_index(std::int32_t line) const;
    /// Checked gutter-index conversion; throws std::out_of_range outside the grid.
    std::size_t track_vec_index(std::int32_t line) const;
    /// Compare all three counts.
    friend bool operator==(const TrackCounts&, const TrackCounts&) = default;
};

/** Placement provenance. Interval collision queries treat any painted interval
 * as occupied, even one painted Unoccupied; this enum is not an erase operation.
 */
enum class CellOccupancyState : std::uint8_t {
    Unoccupied,       ///< Default for cells with no recorded placement.
    DefinitelyPlaced, ///< Placed using definite lines on both axes.
    AutoPlaced        ///< At least one axis was auto-placed; updates placement cursors.
};

/** Sparse occupancy in half-open origin-zero line ranges.
 * Mutations may expand implicit counts and invalidate previously derived indexes.
 * Queries outside recorded tracks see no occupancy. No thread synchronization.
 */
class CellOccupancy {
    struct Interval {
        Line<std::int32_t> range;
        CellOccupancyState state;
    };

    struct TrackIntervals {
        // Compact append-friendly storage for ordinary grids; promote fragmented
        // out-of-order tracks to an ordered index instead of repeatedly copying them.
        using IndexedIntervals = std::map<std::int32_t, Interval>;
        std::variant<std::vector<Interval>, IndexedIntervals> intervals;
        std::optional<std::int32_t> cursor;
        bool empty() const;
        CellOccupancyState state_at(std::int32_t) const;
        void paint(Line<std::int32_t>, CellOccupancyState);
        std::optional<std::int32_t> collision_extent(Line<std::int32_t>) const;
    };

    TrackCounts columns_, rows_;
    std::vector<TrackIntervals> row_intervals_, column_intervals_;
    const std::vector<TrackIntervals>& track_lists(AbsoluteAxis) const;
    void expand_to_fit_range(Line<std::int32_t> row, Line<std::int32_t> column);

public:
    /// Allocate empty track lists for the supplied counts.
    CellOccupancy(TrackCounts columns = {}, TrackCounts rows = {});

    /// Factory equivalent to the count constructor.
    static CellOccupancy with_track_counts(TrackCounts columns, TrackCounts rows) {
        return {columns, rows};
    }

    /// Borrow counts; Horizontal selects columns, Vertical selects rows.
    const TrackCounts& track_counts(AbsoluteAxis axis) const;
    /// Paint half-open origin-zero ranges, expanding counts as needed. Throws
    /// std::out_of_range beyond the limited grid; mutation is not transactional.
    /// @pre Endpoints are ordered and all coordinate differences fit int32.
    void mark_area_as(AbsoluteAxis primary, Line<std::int32_t> primary_span,
                      Line<std::int32_t> secondary_span, CellOccupancyState state);
    /// Line just beyond the furthest collision on primary, or nullopt if clear.
    /// Secondary/primary ranges are half-open origin-zero lines; no mutation.
    std::optional<std::int32_t> line_area_collision_jump(AbsoluteAxis primary,
                                                         Line<std::int32_t> primary_span,
                                                         Line<std::int32_t> secondary_span) const;

    /// Whether no recorded interval intersects the specified area.
    bool line_area_is_unoccupied(AbsoluteAxis axis, Line<std::int32_t> primary,
                                 Line<std::int32_t> secondary) const {
        return !line_area_collision_jump(axis, primary, secondary);
    }

    /// Origin-zero line after the last occupied track in span, or nullopt.
    std::optional<std::int32_t> occupied_track_jump(AbsoluteAxis axis,
                                                    Line<std::int32_t> span) const;
    /// Furthest auto-placed primary end on secondary_start's track; nullopt if absent.
    std::optional<std::int32_t> auto_placement_cursor(AbsoluteAxis primary,
                                                      std::int32_t secondary_start) const;
    /// Test a zero-based content-row index in the complete implicit grid; false outside.
    bool row_is_occupied(std::size_t index) const;
    /// Test a zero-based content-column index; false outside the complete implicit grid.
    bool column_is_occupied(std::size_t index) const;
    /// Recorded state at an origin-zero cell coordinate; Unoccupied outside storage.
    CellOccupancyState state_at(std::int32_t row, std::int32_t column) const;
};

/// Compatibility name for the sparse occupancy representation.
using CellOccupancyMatrix = CellOccupancy;
/// Unresolved/definite origin-zero placements on physical horizontal/vertical axes.
using ItemPlacement = InBothAbsAxis<Line<OriginZeroGridPlacement>>;

/// Placement output: source-order child identity and half-open origin-zero line ranges.
struct PlacedGridItem {
    NodeId node;
    std::size_t source_order{};
    Line<std::int32_t> row{}, column{};
};

class NameIndex;

/** Owning snapshot of expanded template line names and named areas.
 * Supply computed auto-repetition counts at construction, then set both explicit
 * counts (including template areas) before resolving placements. Name groups use
 * one-based CSS line numbers; resolve() converts to origin-zero placements.
 */
class NamedLineResolver {
    TemplateLineNames column_names_, row_names_;
    std::vector<GridTemplateArea> areas_;
    Size<std::uint16_t> area_counts_{}, explicit_counts_{};
    std::vector<std::pair<std::uint32_t, std::string>> column_pairs_, row_pairs_;
    std::shared_ptr<const NameIndex> column_index_, row_index_;

public:
    /// Copy areas and expand template names; repetition validation exceptions propagate.
    NamedLineResolver(const Style& style, std::uint16_t column_auto_repetitions = 0,
                      std::uint16_t row_auto_repetitions = 0);

    /// Columns required by grid-template-areas (independent of track templates).
    std::uint16_t area_column_count() const {
        return area_counts_.width;
    }

    /// Rows required by grid-template-areas.
    std::uint16_t area_row_count() const {
        return area_counts_.height;
    }

    /// Set final explicit column count, including any tracks required by named areas.
    void set_explicit_column_count(std::uint16_t count) {
        explicit_counts_.width = count;
    }

    /// Set final explicit row count, including any tracks required by named areas.
    void set_explicit_row_count(std::uint16_t count) {
        explicit_counts_.height = count;
    }

    /// Share the immutable name index with a detailed metadata snapshot.
    std::shared_ptr<const NameIndex> name_index(AbsoluteAxis axis) const;

    /// Current explicit counts (width = columns, height = rows).
    Size<std::uint16_t> explicit_counts() const {
        return explicit_counts_;
    }

    /// Resolve named column lines/spans while retaining CSS line coordinates.
    Line<GridPlacement> resolve_column_names(const Line<GridPlacement>& placement) const;
    /// Resolve named row lines/spans while retaining CSS line coordinates.
    Line<GridPlacement> resolve_row_names(const Line<GridPlacement>& placement) const;
    /// Axis-selected name resolution, using current explicit counts and named areas.
    Line<GridPlacement> resolve_line_names(const Line<GridPlacement>& placement,
                                           AbsoluteAxis axis) const;
    /// Resolve names and convert CSS lines to origin-zero lines; unresolved names
    /// follow the foundation placement resolver's fallback rules.
    Line<OriginZeroGridPlacement> resolve(const Line<GridPlacement>& placement,
                                          AbsoluteAxis axis) const;
    /// Resolve both grid_row and grid_column from a child's style.
    ItemPlacement resolve(const Style& style) const;
    /// Borrow expanded names until resolver destruction/assignment; groups are CSS line - 1.
    const TemplateLineNames& line_names(AbsoluteAxis axis) const;
    /// Own a copy padded/truncated to explicit count + 1; empty when no names exist.
    TemplateLineNames detailed_line_names(AbsoluteAxis axis) const;
    /// Borrow raw (one-based CSS line, name) expansion pairs; may include duplicate names
    /// and lines beyond the stored limited-grid groups. Lifetime is that of this resolver.
    const std::vector<std::pair<std::uint32_t, std::string>>&
    line_name_pairs(AbsoluteAxis axis) const;
};

/// Rounding policy when deriving auto-repeat counts from a definite content size.
enum class AutoRepeatStrategy {
    MaxRepetitionsThatDoNotOverflow, ///< Floor the additional fitting repeats.
    MinRepetitionsThatDoOverflow     ///< Ceil the additional repeats to meet a minimum.
};

/// Auto-repeat count and template track count for one axis, clamped to grid limits.
struct ExplicitGridAxis {
    std::uint16_t auto_repetitions{};
    std::uint16_t explicit_tracks{};
};

/** Two-axis repetition/template counts (width = columns, height = rows).
 * explicit_tracks counts only the track template. Combine each axis with
 * NamedLineResolver::area_*_count(), clamp to max_grid_tracks, then set resolver counts.
 */
struct ExplicitGridSize {
    Size<std::uint16_t> auto_repetitions{};
    Size<std::uint16_t> explicit_tracks{};
};

/// Compute one template axis using a content-box basis (nullopt = indefinite).
/// Invalid auto-repeat structure yields zero counts; results clamp to max_grid_tracks.
ExplicitGridAxis compute_explicit_grid_size_in_axis(const Style& style,
                                                    std::optional<float> content_size,
                                                    AutoRepeatStrategy strategy,
                                                    const CalcResolver& calc, AbsoluteAxis axis);
/** Compute both explicit template sizes from content-box size constraints.
 * All sizes are content-box sizes; nullopt means indefinite. Handle padding,
 * border and scrollbar floors/insets first, or use the axis operation with the
 * exact auto-fit content-box basis. Retain repetition counts for the resolver
 * and initialize_grid_tracks(). Invalid auto-repeat templates yield zero counts.
 */
ExplicitGridSize compute_explicit_grid_size(const Style& style,
                                            Size<std::optional<float>> preferred_size,
                                            Size<std::optional<float>> min_size,
                                            Size<std::optional<float>> max_size,
                                            const CalcResolver& calc);
/// Estimate complete implicit-grid counts from explicit counts and resolved placements.
/// Width selects columns, height rows; later auto-placement can expand this estimate.
Size<TrackCounts> compute_grid_size_estimate(Size<std::uint16_t> explicit_counts,
                                             std::span<const ItemPlacement> placements);
/// Estimate from in-flow, displayed children; borrows tree styles synchronously.
Size<TrackCounts> estimate_implicit_grid_tracks(LayoutTree& tree, NodeId node,
                                                Size<std::uint16_t> explicit_counts,
                                                const NamedLineResolver& resolver);
/// Place displayed in-flow children in source order; mutates occupancy and returns
/// owned item records. Source order retains the full child index.
std::vector<PlacedGridItem> place_grid_items(LayoutTree& tree, NodeId node,
                                             CellOccupancy& occupancy,
                                             const NamedLineResolver& resolver);
/// Place parallel item/placement spans, overwriting item row/column ranges and occupancy.
/// Throws std::invalid_argument for unequal lengths; occupancy range errors propagate.
/// No strong exception guarantee: earlier placements may already have been recorded.
void place_grid_items(CellOccupancy& occupancy, std::span<PlacedGridItem> items,
                      std::span<const ItemPlacement> placements, GridAutoFlow flow);
/** Replace tracks with the complete interleaved grid, collapsing outer gutters
 * and empty auto-fit tracks. track_has_items receives a zero-based track index
 * in the complete implicit grid, NOT an origin-zero line or interleaved index.
 * The callback is synchronous; it must not mutate tracks or the input templates.
 */
void initialize_grid_tracks(std::vector<GridTrack>& tracks, TrackCounts counts,
                            const std::vector<GridTemplateComponent>& components,
                            const std::vector<TrackSizingFunction>& automatic, LengthPercentage gap,
                            std::uint16_t auto_repetitions,
                            const std::function<bool(std::int32_t)>& track_has_items);

} // namespace taffy::grid

// track sizing

namespace taffy::grid {
struct TrackCounts;
/// Synchronous estimator with tree access; nullopt denotes an unresolved track size.
using TrackSizeEstimateWithTree =
    std::function<std::optional<float>(const GridTrack&, std::optional<float>, const LayoutTree&)>;
/// Synchronous selection predicate over a borrowed track.
using TrackPredicate = std::function<bool(const GridTrack&)>;
/// Synchronous numeric projection over a borrowed track (for sizing/distribution).
using TrackProperty = std::function<float(const GridTrack&)>;
/// Selects which intrinsic-growth preference applies during base-size distribution.
enum class IntrinsicContributionType {
    Minimum, ///< Prefer tracks with intrinsic maximum sizing functions.
    Maximum  ///< Prefer max-content/fit-content maximum sizing functions.
};
/// Estimate extra spacing per interior gutter from content distribution; zero when
/// the inner size/any estimate is indefinite or alignment does not distribute space.
float compute_alignment_gutter_adjustment(AlignContent alignment, std::optional<float> inner_size,
                                          const TrackSizeEstimate& estimate,
                                          std::span<const GridTrack> tracks);
/// Write bounding-gutter indexes from origin-zero placements. Throws std::out_of_range
/// outside counts or std::overflow_error if an index exceeds uint16; may partially update.
void resolve_item_track_indexes(std::span<GridItem> items, TrackCounts columns, TrackCounts rows);
/// Refresh crossing flags from initialized item indexes; propagates span range errors.
void determine_if_item_crosses_flexible_or_intrinsic_tracks(std::span<GridItem> items,
                                                            std::span<const GridTrack> columns,
                                                            std::span<const GridTrack> rows);
/** Run one axis's track-sizing pass, mutating sizes, item measurements/caches and
 * other-axis gutter alignment adjustments. axis_tracks/other_axis_tracks are complete
 * interleaved vectors; items need valid bounding indexes and crossing flags. Cached
 * measurements must match this pass's inputs. Sizes are content-box constraints.
 * Callbacks/tree measurement run synchronously; all errors propagate and partial
 * updates remain visible. Track storage must not be invalidated during callbacks.
 */
void track_sizing_algorithm(LayoutTree& tree, AbstractAxis axis, std::optional<float> axis_min_size,
                            std::optional<float> axis_max_size, AlignContent axis_alignment,
                            AlignContent other_axis_alignment,
                            Size<AvailableSpace> available_grid_space, OptionalSize inner_node_size,
                            std::span<GridTrack> axis_tracks,
                            std::span<GridTrack> other_axis_tracks, std::span<GridItem> items,
                            const TrackSizeEstimateWithTree& estimate,
                            bool has_baseline_aligned_item);
/// Reset base sizes/growth limits from sizing functions and basis; indefinite minima
/// become zero and indefinite maxima infinity. Does not reset other scratch fields.
void initialize_track_sizes(const LayoutTree& tree, std::span<GridTrack> tracks,
                            std::optional<float> basis);
/// Measure baseline-sharing groups, updating baselines/shims and invalidating affected
/// known-dimension caches. Does not reorder the caller's item span.
void resolve_item_baselines(LayoutTree& tree, AbstractAxis axis, std::span<GridItem> items,
                            OptionalSize inner_size);
/// Resolve intrinsic base sizes/growth limits using item contributions; requires valid
/// indexes/crossing flags and pass-local caches, as for track_sizing_algorithm().
void resolve_intrinsic_track_sizes(LayoutTree& tree, AbstractAxis axis, std::span<GridTrack> tracks,
                                   std::span<const GridTrack> other_tracks,
                                   std::span<GridItem> items, AvailableSpace available,
                                   OptionalSize inner_size,
                                   const TrackSizeEstimateWithTree& estimate);
/// Add planned base-size increases and clear those plans.
void flush_planned_base_size_increases(std::span<GridTrack> tracks);
/// Apply positive planned growth increases (infinite limits start at base size),
/// set growability for changed tracks, clear it for others, then clear all plans.
void flush_planned_growth_limit_increases(std::span<GridTrack> tracks,
                                          bool set_infinitely_growable);
/// Sum fr factors over the supplied span, ignoring non-flexible entries.
float crossed_flex_factor_sum(std::span<const GridTrack> tracks);
/// Distribute space into item_incurred_increase, respecting projected limits;
/// return undistributed space. The current property itself is not updated.
/// @pre Projections are stable during the call; eligible proportions are positive.
float distribute_space_up_to_limits(float space, std::span<GridTrack> tracks,
                                    const TrackPredicate& affected, const TrackProperty& proportion,
                                    const TrackProperty& property, const TrackProperty& limit);
/// Plan base-size increases for an item's total space (not just its free space),
/// then clear item-incurred scratch increases; is_flex restricts eligible tracks to fr.
void distribute_item_space_to_base_size(bool is_flex, float space, std::span<GridTrack> tracks,
                                        const TrackPredicate& affected, const TrackProperty& limit,
                                        IntrinsicContributionType contribution_type);
/// Plan growth-limit increases for an item's total space; clear item-incurred scratch.
void distribute_item_space_to_growth_limit(float space, std::span<GridTrack> tracks,
                                           const TrackPredicate& affected);
/// Grow base sizes toward growth limits using available free space.
void maximise_tracks(std::span<GridTrack> tracks, AvailableSpace available);
/// Expand fr base sizes subject to container constraints and item max-content
/// contributions; requires valid item indexes/flags and current measurement caches.
void expand_flexible_tracks(LayoutTree& tree, AbstractAxis axis, std::span<GridTrack> tracks,
                            std::span<const GridTrack> other_tracks, std::span<GridItem> items,
                            std::optional<float> min_size, std::optional<float> max_size,
                            AvailableSpace available, OptionalSize inner_size,
                            const TrackSizeEstimateWithTree& estimate);
/// Find the fr unit for total space, freezing tracks below their base-size floor.
float find_size_of_fr(std::span<const GridTrack> tracks, float space);
/// Divide positive free space equally among tracks with auto maximum sizing;
/// use minimum as the free-space basis when available is indefinite.
void stretch_auto_tracks(std::span<GridTrack> tracks, std::optional<float> minimum,
                         AvailableSpace available);
} // namespace taffy::grid

// alignment

namespace taffy::grid {
struct GridTrack;

/// Write physical border-box-relative offsets without reordering the logical vector.
/// Input is a complete interleaved vector with sized tracks; padding/border are
/// physical start/end insets, and axis_is_reversed selects reversed traversal.
void align_tracks(float grid_container_content_box_size, Line<float> padding, Line<float> border,
                  std::span<GridTrack> tracks, AlignContent track_alignment_style,
                  bool axis_is_reversed);

/** Measure/layout a child in a physical border-box-relative grid area, write its
 * unrounded Layout to tree and append translated descendant OOF candidates.
 * Returns (scrollable overflow contribution, y before relative offset, height,
 * first baseline). containing_block_size supplies percentage bases; direction
 * applies to the inline axis. Tree/callback errors propagate after possible mutation.
 */
std::tuple<Rect<float>, float, float, std::optional<float>>
align_and_position_item(LayoutTree& tree, NodeId node, std::uint32_t order, Rect<float> grid_area,
                        Size<float> containing_block_size,
                        InBothAbsAxis<AlignItems> container_alignment_styles, float baseline_shim,
                        Direction direction, float container_border_box_width,
                        Rect<float> container_border, bool container_is_scroll_container,
                        OofCandidates& bubbled_candidates);

/// Return (physical border-box start, resolved margins) in an axis area. nullopt
/// margins are auto; nullopt insets are unspecified. Baseline shim augments start
/// margin. Relative-position deltas are NOT applied here. Throws std::logic_error
/// for unresolved alignment keywords (e.g. Normal); resolve them before calling.
std::pair<float, Line<float>>
align_item_within_area(Line<float> grid_area, AlignSelf alignment_style, float resolved_size,
                       Position position, Line<std::optional<float>> inset,
                       Line<std::optional<float>> margin, float baseline_shim, Direction direction);
} // namespace taffy::grid

// metadata
namespace taffy::grid {
/// Own a metadata snapshot of aligned tracks (gutters omitted), moved line names,
/// and half-open collapsed-track index ranges. No borrowed storage escapes.
DetailedGridTracksInfo make_detailed_tracks(TrackCounts counts, std::span<const GridTrack> tracks,
                                            TemplateLineNames names);
/// Snapshot both aligned axes, item placements and named areas; item indexes must
/// already be resolved. Item line numbers are one-based in the complete implicit
/// grid; named-area/name coordinates remain one-based relative to the explicit grid.
DetailedGridInfo make_detailed_grid_info(TrackCounts rows_count, TrackCounts column_count,
                                         std::span<const GridTrack> rows,
                                         std::span<const GridTrack> columns,
                                         std::span<const GridItem> items,
                                         const NamedLineResolver& resolver, const Style& style);
/// Physical border-box-relative area for an OOF child's static position; auto or
/// out-of-grid edges use the padding-box/scrollbar edges. Requires complete aligned
/// track vectors matching the counts; valid indexed edges are not bounds-checked.
Rect<float> resolve_static_position_grid_area(const Style& style, const NamedLineResolver& resolver,
                                              TrackCounts rows_count, TrackCounts column_count,
                                              std::span<const GridTrack> rows,
                                              std::span<const GridTrack> columns,
                                              Direction direction, Rect<float> border,
                                              Point<float> scrollbar, Size<float> border_box);
/** Resolve an OOF containing block from owned detailed metadata, with no track borrow.
 * padding_box and returned physical edges are border-box-relative. Auto or
 * out-of-grid edges fall back to padding_box; RTL reverses column interpretation.
 * Throws std::overflow_error if a stored named-area line exceeds uint16 range.
 */
Rect<float> resolve_absolute_grid_area(const DetailedGridInfo& info, Line<GridPlacement> row,
                                       Line<GridPlacement> column, Direction direction,
                                       Rect<float> padding_box);
} // namespace taffy::grid
