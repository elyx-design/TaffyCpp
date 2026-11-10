#pragma once

/// @file
/// Layout is the box geometry read from Tree::layout(). LayoutInput/LayoutOutput
/// support custom algorithm adapters; detailed grid records provide optional metadata.

#include <compare>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <taffy/export.hpp>
#include <taffy/geometry.hpp>
#include <taffy/style.hpp>
#include <unordered_map>
#include <variant>
#include <vector>

namespace taffy {

namespace grid {
struct DetailedNameIndexes;
}

/** @brief Opaque node identity; does not own or extend the lifetime of a tree node.
 * Zero is invalid. The persistent tree owns the generation/index encoding.
 */
struct NodeId {
    std::uint64_t value = 0; ///< Tree-defined identity bits; zero is the invalid default.
    /// Compare the encoded identities, not node contents.
    auto operator<=>(const NodeId&) const = default;
};

/// Whether to produce layout, measure size only, or recursively clear hidden layout.
enum class RunMode {
    PerformLayout,
    ComputeSize,
    PerformHiddenLayout
};
/// Whether sizing uses content contributions or the node's inherent size constraints.
enum class SizingMode {
    ContentSize,
    InherentSize
};
/// Physical measurement axes; Both requests width and height together.
enum class RequestedAxis {
    Horizontal,
    Vertical,
    Both
};
/// Swap horizontal/vertical; leave Both unchanged.
RequestedAxis transpose(RequestedAxis axis);
/// Convert a physical axis into a single-axis request.
RequestedAxis requested_axis(AbsoluteAxis axis);
/// Return the physical axis, or nullopt for Both.
std::optional<AbsoluteAxis> absolute_axis(RequestedAxis axis);

/** @brief Collapsed block margins in layout units, retaining both signed extrema.
 * Merging uses floating min/max (a single NaN is ignored by those operations).
 * Public fields are not validated; from_margin classifies NaN on the negative path.
 */
struct CollapsibleMarginSet {
    float positive = 0;                               ///< Largest participating nonnegative margin.
    float negative = 0;                               ///< Most negative participating margin.
    static TAFFY_API const CollapsibleMarginSet ZERO; ///< Empty, zero-valued set.
    /// Classify one margin into its positive or negative component.
    static CollapsibleMarginSet from_margin(float margin);
    /// Merge a margin without changing this set.
    CollapsibleMarginSet collapse_with_margin(float margin) const;
    /// Merge the extrema of two sets without changing either input.
    CollapsibleMarginSet collapse_with_set(CollapsibleMarginSet other) const;
    /// Sum the signed extrema to obtain the used margin.
    float resolve() const;
    /// Compare both stored components.
    bool operator==(const CollapsibleMarginSet&) const = default;
};

/** @brief Owned value describing one layout/measurement request in layout units.
 * Sizes use physical width/height. An absent optional is unknown, not zero; a
 * present NaN remains present. Available space may be definite or intrinsic
 * min/max-content and is distinct from known dimensions and percentage bases.
 */
struct LayoutInput {
    RunMode run_mode = RunMode::PerformLayout;         ///< Requested work and side effects.
    SizingMode sizing_mode = SizingMode::InherentSize; ///< Sizing constraints to apply.
    RequestedAxis axis = RequestedAxis::Both;          ///< Physical dimensions to compute.
    Size<std::optional<float>> known_dimensions{}; ///< Already determined outer (border-box) sizes.
    Size<bool> known_dimensions_are_definite{
        true, true};                          ///< Definiteness of known sizes for dependent sizing.
    Size<std::optional<float>> parent_size{}; ///< Containing-block percentage-resolution bases.
    Size<AvailableSpace>
        available_space{}; ///< Space offered to each physical axis, not a forced size.
    Line<bool> vertical_margins_are_collapsible{
        false, false}; ///< Top/start and bottom/end collapse permission.
    static TAFFY_API const LayoutInput
        HIDDEN; ///< Default inputs with PerformHiddenLayout selected.
    /// Swap all size axes and requested axis; reset vertical collapse permissions.
    LayoutInput transpose() const;
    /// Compare all request fields.
    bool operator==(const LayoutInput&) const = default;
};

/// First/last baseline offsets in layout units from the node's top border edge.
struct Baselines {
    std::optional<float> first{};          ///< First baseline, or absent if none is supplied.
    std::optional<float> last{};           ///< Last baseline, independently optional.
    static TAFFY_API const Baselines NONE; ///< Both baselines absent.
    /// Supply only the first baseline (including an absent first baseline).
    static Baselines from_first(std::optional<float> first);
    /// Compare optional offsets; NaN is not treated as absence.
    bool operator==(const Baselines&) const = default;
};

/// Anchor within a physical-axis static-position interval after direction resolution.
enum class AxisStaticEdge {
    Start,
    Center,
    End
};

/// Resolved alignment and overflow fallback for a static-position interval.
struct AxisStaticAlign {
    AxisStaticEdge keyword = AxisStaticEdge::Start;    ///< Preferred resolved edge/center.
    AlignmentSafety safety = AlignmentSafety::Default; ///< Overflow alignment policy.
    AxisStaticEdge fallback = AxisStaticEdge::Start; ///< Resolved fallback when safety requires it.
    /// Use the same edge for the preferred and fallback positions with default safety.
    static AxisStaticAlign from_keyword(AxisStaticEdge keyword);
    /// Compare all alignment fields.
    bool operator==(const AxisStaticAlign&) const = default;
};

/// One-axis static-position area in the candidate's current local coordinate frame.
struct AxisStaticPosition {
    Line<float> area{};      ///< Start/end coordinates in layout units, not a length pair.
    AxisStaticAlign align{}; ///< Alignment within the interval.
    /// Build a zero-length interval anchored at one coordinate.
    static AxisStaticPosition from_edge(float anchor, AxisStaticEdge edge);
    /** @brief Resolve alignment, including RTL and safe-alignment fallback.
     * @throws std::invalid_argument For unresolved self-start/self-end keywords.
     */
    static AxisStaticPosition from_alignment(AlignSelf alignment, Line<float> area,
                                             bool axis_is_rtl);
    /// Compare interval and alignment.
    bool operator==(const AxisStaticPosition&) const = default;
};

/// Deferred out-of-flow positioning record; the referenced tree node is not owned.
struct OofCandidate {
    NodeId node{};                          ///< Node whose positioning is deferred.
    std::uint32_t order = 0;                ///< Layout/paint ordering key.
    Position position = Position::Absolute; ///< Out-of-flow positioning mode.
    Point<AxisStaticPosition>
        static_position{}; ///< Physical x/y intervals in the current local frame.
    /// Compare the node identity and positioning data.
    bool operator==(const OofCandidate&) const = default;
};

/** @brief Owning value collection of deferred candidates (not of their tree nodes).
 * Empty vector storage requires no allocation. Spans/iterators borrow inner;
 * vector reallocation, replacement, erasure, take(), or clear() may invalidate
 * them. Mutating operations may propagate allocation exceptions.
 */
struct OofCandidates {
    std::vector<OofCandidate> inner{};         ///< Owned candidate records in traversal order.
    static TAFFY_API const OofCandidates NONE; ///< Empty collection.

    /// Whether there are no candidates.
    bool is_empty() const {
        return inner.empty();
    }

    /// Standard-container spelling of is_empty().
    bool empty() const {
        return inner.empty();
    }

    /// Number of candidate records.
    std::size_t size() const {
        return inner.size();
    }

    /// Borrow immutable contiguous records; no ownership is transferred.
    std::span<const OofCandidate> as_slice() const {
        return inner;
    }

    /// Borrow mutable contiguous records; no ownership is transferred.
    std::span<OofCandidate> as_mut_slice() {
        return inner;
    }

    /// Append one candidate, potentially reallocating storage.
    void push(OofCandidate candidate);
    /** @brief Move other's records to the end and leave other empty on success.
     * @throws std::invalid_argument If other is this collection.
     */
    void append(OofCandidates& other);
    /// Transfer all storage into the result, leaving this collection empty.
    OofCandidates take();
    /// Remove all records and release vector storage.
    void clear();

    /// Borrow the first mutable iterator.
    auto begin() {
        return inner.begin();
    }

    /// Borrow the mutable past-the-end iterator.
    auto end() {
        return inner.end();
    }

    /// Borrow the first immutable iterator.
    auto begin() const {
        return inner.begin();
    }

    /// Borrow the immutable past-the-end iterator.
    auto end() const {
        return inner.end();
    }

    /// Add offset to both endpoints on each physical axis; leave alignment unchanged.
    void translate(Point<float> offset);
    /// Compare candidate records in order.
    bool operator==(const OofCandidates&) const = default;
};

/// Containing area for deferred positioning, expressed in layout units.
struct OofPositioningArea {
    Size<float> size{};    ///< Dimensions of the positioning area, not the node's border box.
    Point<float> offset{}; ///< Area origin relative to the node's border-box origin.
    /// Compare dimensions and offset.
    bool operator==(const OofPositioningArea&) const = default;
};

/** @brief Owned result of layout/measurement; all geometry is in layout units.
 * The border-box size and scrollable overflow are independent: overflow is a
 * local rectangle of edge coordinates, not four insets or a content-box size.
 */
struct LayoutOutput {
    Size<float> size{}; ///< Physical border-box width/height.
    /// Edge coordinates in a local border-excluded overflow frame. Container
    /// horizontal coordinates are direction-normalized (mirrored for RTL), not
    /// physical border-box coordinates; see Layout::scroll_width()/scroll_height().
    Rect<float> scrollable_overflow_rect{};
    Baselines baselines{};                ///< Baseline offsets in the node's local frame.
    CollapsibleMarginSet top_margin{};    ///< Collapsible margin at the physical top.
    CollapsibleMarginSet bottom_margin{}; ///< Collapsible margin at the physical bottom.
    bool margins_can_collapse_through =
        false;                      ///< Whether top/bottom margin sets may join through this node.
    OofCandidates oof_candidates{}; ///< Owned deferred records in this node's coordinate frame.
    std::optional<OofPositioningArea>
        oof_positioning_area{};                  ///< Optional area for deferred descendants.
    static TAFFY_API const LayoutOutput HIDDEN;  ///< Zero/default result for hidden layout.
    static TAFFY_API const LayoutOutput DEFAULT; ///< Default-initialized result.
    /// Set size, overflow and baselines; leave other fields at their defaults.
    static LayoutOutput from_sizes_and_baselines(Size<float> size, Rect<float> overflow,
                                                 Baselines baselines);
    /// Set size and overflow with no baselines and otherwise default fields.
    static LayoutOutput from_sizes(Size<float> size, Rect<float> overflow);
    /// Set only the border-box size; overflow remains the zero rectangle.
    static LayoutOutput from_outer_size(Size<float> size);
    /** @brief Swap size/overflow axes and optional positioning-area axes.
     * Resets baselines and margin state; copies deferred candidates unchanged
     * (their static-position coordinates are deliberately not transposed).
     */
    LayoutOutput transpose() const;
    /// Compare all stored output fields.
    bool operator==(const LayoutOutput&) const = default;
};

/** @brief Stored physical box geometry in layout units (CSS-pixel-like units).
 * This is a value record, not a node owner. Rect field order is left, right,
 * top, bottom. Border/padding/margin are thicknesses; overflow instead stores
 * edge coordinates. Values, including NaN and negative sizes, are not validated.
 */
struct Layout {
    std::uint32_t order = 0; ///< Relative layout/paint ordering key.
    /// Border-box origin in the layout parent's frame. For hoisted absolute/fixed
    /// nodes this is the containing-block owner, not necessarily the logical parent.
    /// Root locations use the external root coordinate frame.
    Point<float> location{};
    Size<float> size{}; ///< Border-box dimensions, excluding margins.
    /// Local border-excluded overflow edges, with direction-normalized horizontal
    /// coordinates. This frame differs from physical location/size; scroll_width()
    /// and scroll_height() account for borders and scrollbar gutters.
    Rect<float> scrollable_overflow_rect{};
    Size<float> scrollbar_size{}; ///< Reserved gutter widths on x and y (not scrollbar lengths).
    Rect<float> border{};         ///< Physical border thicknesses.
    Rect<float> padding{};        ///< Physical padding thicknesses inside the border.
    Rect<float> margin{};         ///< Physical used margins outside the border.
    /// Set order and leave all box geometry at its zero defaults.
    static Layout with_order(std::uint32_t order);
    /// Subtract left/right padding and border in stored order; no clamping or gutter subtraction.
    float content_box_width() const;
    /// Subtract top/bottom padding and border in stored order; no clamping or gutter subtraction.
    float content_box_height() const;
    /// Combine content_box_width() and content_box_height().
    Size<float> content_box_size() const;
    /// Content-box x in the same frame as location: location.x + border.left + padding.left.
    float content_box_x() const;
    /// Content-box y in the same frame as location: location.y + border.top + padding.top.
    float content_box_y() const;
    /// Nonnegative right-side scroll excess, accounting for borders and the bounded x gutter.
    float scroll_width() const;
    /// Nonnegative bottom-side scroll excess, accounting for borders and the bounded y gutter.
    float scroll_height() const;
    /// Compare all stored fields, without approximate float comparison.
    bool operator==(const Layout&) const = default;
};

/// Half-open [start, end) range of zero-based track indices, including implicit tracks.
struct GridTrackRange {
    std::size_t start = 0; ///< First included track.
    std::size_t end = 0;   ///< First excluded track.
    /// Compare range endpoints.
    bool operator==(const GridTrackRange&) const = default;
};

/** @brief Named-area line numbers relative to the explicit grid (one-based).
 * Zero denotes an unset edge. These indices do not include the leading implicit
 * track offset used by DetailedGridItemsInfo and track positions.
 */
struct GridAreaLines {
    /// Row start/end and column start/end line numbers; end lines bound the area exclusively.
    std::uint32_t row_start = 0, row_end = 0, column_start = 0, column_end = 0;
    /// Compare all four line numbers.
    bool operator==(const GridAreaLines&) const = default;
};

/** @brief Owning snapshot of one physical grid axis; no track/style pointers escape.
 * positions includes negative implicit, explicit, then positive implicit tracks,
 * but excludes separate gutter entries. Coordinates are in layout units in the
 * grid container's local frame. line_names is indexed from the explicit grid's
 * first line; names_for_line() translates from full-grid zero-based line indices.
 */
struct DetailedGridTracksInfo {
    std::uint16_t negative_implicit_tracks = 0; ///< Tracks before the explicit grid.
    std::uint16_t explicit_tracks = 0;          ///< Number of explicit tracks.
    std::uint16_t positive_implicit_tracks = 0; ///< Tracks after the explicit grid.
    std::vector<Line<float>>
        positions{}; ///< Owned start/end coordinates for each track, including collapsed tracks.
    std::vector<GridTrackRange>
        collapsed_tracks{}; ///< Half-open ranges into positions for collapsed tracks.
    std::optional<float>
        empty_axis_line{}; ///< Sole line coordinate when the axis has no tracks, if known.
    std::vector<std::vector<std::string>>
        line_names{}; ///< Owned names per explicit-grid line (including its final line).
    /** @brief Borrow names at a zero-based full-grid line index.
     * Returns an empty span before the explicit grid or outside stored names;
     * no range exception is thrown. The span and strings borrow line_names and
     * are invalidated by destruction or relevant vector/string modification.
     */
    std::span<const std::string> names_for_line(std::size_t line_index) const;
    /** @brief Copy names into full-grid line order, including empty implicit lines.
     * Returns an empty vector if no stored line has names. Otherwise the owned
     * result has at least positions.size() + 1 entries, padded as necessary.
     * May propagate allocation exceptions; does not borrow this snapshot.
     */
    std::vector<std::vector<std::string>> iter_line_names() const;
    /** @brief Serialize track lengths and bracketed names as a CSS-like pixel list.
     * Returns "none" for no positions. Each length is exactly (end - start) / zoom;
     * zoom is not validated (zero, negative, NaN and infinity use float arithmetic).
     * NaN/infinity are rendered as NaN/inf/-inf; allocation exceptions may propagate.
     */
    std::string to_track_list_string(float zoom = 1) const;
    /// Compare all counts, coordinates, ranges and owned names.
    bool operator==(const DetailedGridTracksInfo&) const = default;
};

/** @brief Item placement using one-based full-grid line numbers, including implicit tracks.
 * Start is inclusive and end is the exclusive bounding line. A nonempty valid
 * span has start >= 1 and end >= start + 1; zero defaults are not valid placement.
 */
struct DetailedGridItemsInfo {
    /// Row and column bounding lines; unlike GridAreaLines these include leading implicit tracks.
    std::uint16_t row_start = 0, row_end = 0, column_start = 0, column_end = 0;
    /// Compare all four placement lines.
    bool operator==(const DetailedGridItemsInfo&) const = default;
};

/// Owning grid metadata snapshot, including item placements and named explicit-grid areas.
struct DetailedGridInfo {
    DetailedGridTracksInfo rows{};    ///< Physical vertical-axis tracks.
    DetailedGridTracksInfo columns{}; ///< Physical horizontal-axis tracks.
    std::vector<DetailedGridItemsInfo>
        items{}; ///< Placement records in the metadata producer's item order.
    std::unordered_map<std::string, GridAreaLines>
        areas{}; ///< Owned area names and explicit-grid line numbers.
    /// Immutable placement-name index, preserving explicit and implicit occurrences.
    /// Shared by copies of native snapshots; not part of metadata value equality.
    /// Reset this field after manually editing names or areas in a snapshot.
    std::shared_ptr<const grid::DetailedNameIndexes> name_indexes{};
    /// Serialize row tracks; uses to_track_list_string() zoom and exception rules.
    std::string grid_template_rows(float zoom = 1) const;
    /// Serialize column tracks; uses to_track_list_string() zoom and exception rules.
    std::string grid_template_columns(float zoom = 1) const;
    /** @brief Return an owned (origin, size) pair for a zero-based item index.
     * Coordinates use the same container-local frame as track positions, not the
     * item's own border box. Column endpoints are min/max normalized; row endpoints
     * retain their stored order. A missing item returns nullopt. For a present item,
     * start lines address positions[start - 1] and end lines positions[end - 2].
     * @throws std::out_of_range If a present item's lines do not address stored tracks.
     */
    std::optional<std::pair<Point<float>, Size<float>>>
    item_grid_area(std::size_t item_index) const;

    /// Compare both axes, item placements and named areas.
    bool operator==(const DetailedGridInfo& other) const {
        return rows == other.rows && columns == other.columns && items == other.items &&
               areas == other.areas;
    }
};

/// Optional algorithm-specific owned metadata; monostate means no detailed metadata.
using DetailedLayoutInfo = std::variant<std::monostate, DetailedGridInfo>;

} // namespace taffy

/// Hash node identity bits, without accessing the tree or extending node lifetime.
template <>
struct std::hash<taffy::NodeId> {
    /// Hash the underlying 64-bit identity with the standard integer hash.
    std::size_t operator()(taffy::NodeId id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};
