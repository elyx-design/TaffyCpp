#pragma once

/** @file
 * Private block, float and out-of-flow positioning operations.
 * Lengths are physical layout units. Two-element inset/margin/flag arrays use
 * [left, right], independent of writing direction. FloatContext coordinates are
 * relative to its BFC; BlockContext translates positions to its block coordinates.
 * Operations are synchronous and unsynchronized; allocation/tree callback exceptions
 * propagate. Unless stated otherwise, mutations have no strong exception guarantee.
 */

#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <taffy/layout.hpp>
#include <vector>

namespace taffy {

class LayoutTree;

/// Single-subject CSS block alignment offset for free_space (also for external
/// line layout); resolves safe/overflow alignment using the supplied free space.
[[nodiscard]] float compute_block_align_content_offset(AlignContent alignment, float free_space);

/** Layout one out-of-flow candidate in an area's physical coordinates.
 * area_offset translates the area's origin into the parent's coordinates. The
 * synchronous before_layout callback observes the exact final-layout input before
 * child computation. Stores the final unrounded Layout in tree and returns it;
 * replaces surfaced with descendants still relative to this box's border box.
 * grid_owner optionally supplies detailed grid metadata for its positioning area.
 * Callback/tree exceptions propagate; references passed to the callback must not
 * be retained.
 */
Layout layout_oof_box(LayoutTree& tree, OofCandidate candidate, Size<float> area_size,
                      Point<float> area_offset, Direction direction,
                      Point<Overflow> container_overflow, std::optional<NodeId> grid_owner,
                      const std::function<void(LayoutTree&, const LayoutInput&)>& before_layout,
                      OofCandidates& surfaced);

/// Horizontal float-fit comparison tolerance, in layout length units.
inline constexpr float FIT_TOLERANCE = 0.001f;

/** Candidate content position between floats.
 * segment_id is a zero-based float-band index, or nullopt outside recorded bands;
 * it is only a continuation token until the float context is next mutated.
 * x/y are the queried context's coordinates; height may be positive infinity.
 */
struct ContentSlot {
    std::optional<std::size_t> segment_id{};
    float x{}, y{}, width{}, height{};
};

/** Candidate independent-BFC position. border_width is the fit constraint;
 * stretch_width is the auto/stretch width accounting for both margins.
 * Coordinates and segment_id lifetime follow ContentSlot.
 */
struct BfcSlot {
    std::optional<std::size_t> segment_id{};
    float x{}, y{}, border_width{}, stretch_width{};
};

/// Placed float dimensions, BFC-relative y, and inset from its own left/right side.
struct PlacedFloatedBox {
    float width{}, height{}, x_inset{}, y{};
};

namespace block_detail {
/** Physical-axis CSS absolute alignment after self-relative keyword resolution.
 * Used by the OOF pass and exposed for custom axis-layout integrations.
 */
enum class OofAlignment {
    Normal,  ///< CSS normal; stretching depends on replaced/table status and insets.
    Stretch, ///< Request stretch sizing when both insets permit it.
    Start,   ///< Align to the direction-sensitive start edge.
    Center,  ///< Center in the inset-modified containing block.
    End      ///< Align to the direction-sensitive end edge.
};
/// Convert self-alignment to the OOF axis model (baseline maps to Start).
/// Throws TreePanic for an invalid keyword value.
[[nodiscard]] OofAlignment oof_alignment(AlignSelf alignment);

/** Inputs to CSS absolute positioning on one physical axis.
 * cb_size is the containing-block length; inset and margin are physical start/end,
 * with nullopt meaning auto. is_inline/reversed select direction-sensitive edge
 * precedence; is_scroll_axis controls overflow alignment. Queries do not mutate.
 */
struct OofAxis {
    float cb_size;
    Line<std::optional<float>> inset;
    Line<std::optional<float>> margin;
    AxisStaticPosition static_position;
    OofAlignment alignment;
    AlignmentSafety safety;
    bool is_inline;
    bool reversed;
    bool is_scroll_axis;
    /// Direction-sensitive static start edge for this axis.
    [[nodiscard]] AxisStaticEdge start_edge() const;
    /// Direction-sensitive static end edge for this axis.
    [[nodiscard]] AxisStaticEdge end_edge() const;
    /// Edge selected by resolved alignment and axis reversal.
    [[nodiscard]] AxisStaticEdge alignment_edge() const;
    /// Physical start/end coordinates of the inset-modified containing block.
    [[nodiscard]] Line<float> inset_modified_containing_block() const;
    /// Length after overconstrained insets have been adjusted to a nonnegative area.
    [[nodiscard]] float inset_modified_containing_block_size() const;
    /// Nonnegative available border-box size after insets and non-auto margins.
    [[nodiscard]] float stretch_fit_size() const;
    /// Whether inset/alignment rules stretch an auto size for this box category.
    [[nodiscard]] bool is_stretch_sized(bool is_replaced, bool is_table) const;
    /// Resolve auto margins for a final border-box length, including overflow rules.
    [[nodiscard]] Line<float> resolve_margins(float size) const;
    /// Border-box start coordinate for a final size and already resolved margins.
    [[nodiscard]] float position(float size, Line<float> resolved_margin) const;
};

/// Half-open [start, end) vertical-coordinate or segment-index range; no validation.
template <class T>
struct Range {
    T start{}, end{};

    /// Test membership; an empty or reversed range contains no values.
    [[nodiscard]] bool contains(T value) const {
        return start <= value && value < end;
    }
};

/// Apply CSS float-fit constraints with FIT_TOLERANCE. Insets are distances from
/// the physical left/right BFC edges; oversized floats may fit when no float blocks them.
[[nodiscard]] bool float_fits_horizontally(float width, FloatDirection direction, float bfc_width,
                                           std::array<float, 2> float_insets,
                                           std::array<float, 2> cb_insets);

/// Float band covering half-open BFC y coordinates, with edge insets and float flags.
struct Segment {
    Range<float> y{};
    std::array<float, 2> insets{};
    std::array<bool, 2> has_float{};
    /// Test horizontal fit against this band's insets; box height is not inspected.
    [[nodiscard]] bool fits_float_width(Size<float> floated_box, FloatDirection direction,
                                        float bfc_width, std::array<float, 2> cb_insets) const;
};

/// Incremental fit state over consecutive float bands; height accumulates in double
/// to preserve the pinned layout arithmetic while inputs remain float lengths.
struct FloatFitter {
    float bfc_width{};
    double slot_height{};
    std::array<float, 2> float_insets{};
    std::array<float, 2> cb_insets{};
    /// Start a candidate slot with initial band height and containing-block insets.
    FloatFitter(float bfc_width, float slot_height, std::array<float, 2> cb_insets);
    /// Merge each side by maximum, narrowing the candidate horizontal space.
    void union_insets(std::array<float, 2> insets);
    /// Distance from the chosen physical BFC side to the candidate float.
    [[nodiscard]] float placed_inset(FloatDirection direction) const;
    /// Horizontal fit under the accumulated insets and FIT_TOLERANCE.
    [[nodiscard]] bool fits_horizontally(float width, FloatDirection direction) const;

    /// Compatibility spelling of fits_horizontally(); identical behavior.
    [[nodiscard]] bool fits_horiontally(float width, FloatDirection direction) const {
        return fits_horizontally(width, direction);
    }

    /// Accumulate an additional band's height without rounding the running sum to float.
    void add_height(float height);
    /// Exact comparison against accumulated double height (no fit tolerance).
    [[nodiscard]] bool fits_vertically(float height) const;
};

using ::taffy::FIT_TOLERANCE;
} // namespace block_detail

/** Owns float placements and the sparse vertical bands in one BFC.
 * Set the available BFC width before placement/query. Width changes do not reflow
 * existing placements. Slot searches do not reserve space; placement mutates bands.
 * Insets are distances inward from the BFC's physical left/right edges.
 */
class FloatContext {
public:
    /// Construct an empty context of width zero.
    FloatContext() = default;
    /// Whether a float has been submitted, including a zero-height float.
    [[nodiscard]] bool has_floats() const;
    /// Whether recorded float bands extend below the BFC-relative min_y.
    [[nodiscard]] bool has_active_floats(float min_y) const;
    /// Set BFC width without modifying any existing bands/placements.
    void set_width(float available_width);
    /// Borrow left placements in encounter order; invalidated by same-side placement,
    /// assignment or destruction. Stored x_inset measures distance from the left edge.
    [[nodiscard]] std::span<const PlacedFloatedBox> left_floats() const;
    /// Borrow right placements; same lifetime rules, x_inset measures from the right edge.
    [[nodiscard]] std::span<const PlacedFloatedBox> right_floats() const;
    /// Place at/after min_y and the float/clear ceilings, recording occupied bands;
    /// return physical BFC-relative top-left. Zero height still records a placement.
    /// @pre Box dimensions are nonnegative; inputs must support ordered band comparisons.
    Point<float> place_floated_box(Size<float> floated_box, float min_y,
                                   std::array<float, 2> containing_block_insets,
                                   FloatDirection direction, Clear clear);
    /// Maximum bottom of the sides selected by clear; nullopt for None/no such float.
    [[nodiscard]] std::optional<float> cleared_threshold(Clear clear) const;
    /// Find a candidate content position without mutation. after skips through a
    /// prior segment_id; use only tokens obtained before the next context mutation.
    /// Result height is infinity; this operation does not fit a requested box height.
    [[nodiscard]] ContentSlot
    find_content_slot(float min_y, std::array<float, 2> containing_block_insets, Clear clear,
                      std::optional<std::size_t> after = std::nullopt) const;
    /// Find an independent BFC candidate, distinguishing fit and stretch widths.
    /// direction selects margin precedence; after has find_content_slot() token rules.
    [[nodiscard]] BfcSlot find_bfc_slot(float min_y, std::array<float, 2> containing_block_insets,
                                        std::array<float, 2> margins, Direction direction,
                                        Clear clear,
                                        std::optional<std::size_t> after = std::nullopt) const;

private:
    float available_width_{};
    bool has_floats_{};
    std::vector<PlacedFloatedBox> left_floats_{}, right_floats_{};
    std::vector<block_detail::Segment> segments_{};
    std::array<block_detail::Range<std::size_t>, 2> last_placed_floats_{};
    std::array<std::optional<float>, 2> clear_bottoms_{};
    std::optional<float> float_ceiling_{};
    void subdivide_segment(std::size_t idx, float divide_at_y);
    void update_last_placed_float(FloatDirection direction,
                                  block_detail::Range<std::size_t> placement);
    PlacedFloatedBox place_floated_box_inner(Size<float> floated_box, float min_y,
                                             std::array<float, 2> containing_block_insets,
                                             FloatDirection direction, Clear clear);
    [[nodiscard]] std::optional<std::size_t> cleared_segment(Clear clear) const;
};

/** Streaming float intrinsic-width contribution in source order.
 * Min-content takes the widest float; other modes accumulate each side, resetting
 * selected side sums on clear. Definite available width caps the accumulated
 * contribution but never below the widest float.
 */
class FloatIntrinsicWidthCalculator {
public:
    /// Begin an empty contribution calculation under the given width constraint.
    explicit FloatIntrinsicWidthCalculator(AvailableSpace available_width);
    /// Incorporate a nonnegative float width and its physical-side clear behavior.
    void add_float(float width, FloatDirection direction, Clear clear);
    /// Current contribution (zero before any float); does not consume the calculator.
    [[nodiscard]] float result() const;

private:
    AvailableSpace available_width_;
    std::array<float, 2> side_sums_{};
    float contribution_{}, widest_{};
};

class BlockContext;

/// Owner of shared float state for a block formatting context. Do not move or
/// destroy it while any borrowing BlockContext remains in use.
class BlockFormattingContext {
public:
    /// Construct an empty BFC; set its width through the root BlockContext.
    BlockFormattingContext() = default;
    /// Borrow a root view; this owner must outlive the view and all its sub-contexts.
    [[nodiscard]] BlockContext root_block_context();

private:
    FloatContext float_context_{};
    friend class BlockContext;
};

/** Non-owning per-block coordinates and margin-strut state in a shared BFC.
 * Copying copies local strut/coordinate state, but still shares float placements.
 * y parameters/results are block-relative; x results are relative to the block's
 * physical left edge. The owner must stay at a stable address and outlive this view.
 */
class BlockContext {
public:
    /// Derive a child view by adding y offset and left/right insets; inherits current
    /// adjoining-float flags but starts independent local strut/contribution state.
    [[nodiscard]] BlockContext sub_context(float additional_y_offset, std::array<float, 2> insets);
    /// Whether this view was obtained directly as the BFC root.
    [[nodiscard]] bool is_bfc_root() const;
    /// Set shared BFC width, not this block's local content width; does not reflow floats.
    void set_width(float available_width);
    /// Replace content-box insets relative to this block's inherited edge insets.
    void apply_content_box_inset(std::array<float, 2> content_box_x_insets);
    /// Whether any float was submitted anywhere in the shared BFC.
    [[nodiscard]] bool has_floats() const;
    /// Whether shared float bands extend below this block-relative y.
    [[nodiscard]] bool has_active_floats(float min_y) const;
    /// Place in the shared BFC and return block-relative top-left. Records this
    /// float's bottom contribution and, when requested, its unresolved-strut side.
    Point<float> place_floated_box(Size<float> floated_box, float min_y, FloatDirection direction,
                                   Clear clear, bool adjoins_unresolved_strut);
    /// Translate a shared float slot into local coordinates; no reservation/mutation.
    /// after is a shared BFC band token, invalid after any shared placement mutation.
    [[nodiscard]] ContentSlot
    find_content_slot(float min_y, Clear clear,
                      std::optional<std::size_t> after = std::nullopt) const;
    /// Local-coordinate independent-BFC candidate; after follows content-slot token rules.
    [[nodiscard]] BfcSlot find_bfc_slot(float min_y, std::array<float, 2> margins,
                                        Direction direction, Clear clear,
                                        std::optional<std::size_t> after = std::nullopt) const;
    /// Selected shared float bottoms in local y coordinates, or nullopt if absent.
    [[nodiscard]] std::optional<float> cleared_threshold(Clear clear) const;
    /// Whether the unresolved margin strut adjoins a float on a side selected by clear.
    [[nodiscard]] bool has_adjoining_float(Clear clear) const;
    /// OR left/right adjoining-float flags into the current unresolved strut.
    void merge_adjoining_floats(std::array<bool, 2> flags);
    /// Snapshot initial top-adjoining flags once, then clear current adjoining flags.
    void commit_strut();
    /// Initial committed flags, or current flags if no strut has been committed yet.
    [[nodiscard]] std::array<bool, 2> top_adjoining_floats() const;
    /// Merge an already translated child bottom contribution by maximum.
    void add_child_floated_content_height_contribution(float child_contribution);
    /// Maximum recorded local float/child bottom, or negative infinity when absent.
    [[nodiscard]] float floated_content_height_contribution() const;

private:
    explicit BlockContext(BlockFormattingContext& bfc) : bfc_(&bfc) {}

    BlockFormattingContext* bfc_;
    float y_offset_{};
    std::array<float, 2> insets_{}, content_box_insets_{};
    float float_content_contribution_ = -std::numeric_limits<float>::infinity();
    bool is_root_ = true;
    std::array<bool, 2> adjoining_floats_{};
    std::optional<std::array<bool, 2>> top_adjoining_floats_{};
    friend class BlockFormattingContext;
};

namespace block_detail {
using ::taffy::BfcSlot;
using ::taffy::BlockFormattingContext;
using ::taffy::ContentSlot;
using ::taffy::FloatContext;
using ::taffy::FloatIntrinsicWidthCalculator;
using ::taffy::PlacedFloatedBox;
} // namespace block_detail
} // namespace taffy
