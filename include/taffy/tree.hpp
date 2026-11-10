#pragma once

/// @file
/// Owned layout trees for application use.
/// Advanced custom storage adapters are declared in tree_adapter.hpp.
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <taffy/context.hpp>
#include <taffy/error.hpp>
#include <taffy/tree_adapter.hpp>

namespace taffy {

/// Owning, move-only tree with generational handles, styles and cached layouts.
///
/// Create nodes, attach them, call compute_layout(), then read layout(). Handles
/// are meaningful only for their owning tree; do not use one tree's IDs in another.
/// Removed/cleared handles remain stale. Child spans are copied, never retained.
///
/// Tree is not internally synchronized. Do not concurrently access the same tree.
/// Structural/style/context/callback mutation and reentrant root layout during
/// computation throw TreePanic. Callback exceptions propagate; layout is not a
/// transaction, so do not rely on partially updated results after an exception.
/// Invalid node lookups generally throw NodeAccessError, not unexpected<TreeError>.
/// Context destructors may inspect the tree, but structural/style/callback mutation
/// during context destruction throws TreePanic (as during layout). Destructors must
/// catch that exception. Moving a tree during layout/context destruction terminates;
/// destroying the owning Tree from one of its callbacks is not supported.
/// A moved-from tree supports destruction, reassignment, contains(), capacity()
/// and total_node_count(); assign a new Tree before other operations.
class Tree final : public LayoutTree {
public:
    /// Content-size callback for leaves; compute_leaf() still applies CSS sizing.
    /// Arguments are borrowed for the call. Return size in layout units, respecting
    /// known dimensions/available-space constraints as appropriate for the content.
    /// Layout can measure a node several times, or skip it on a cache hit. Stored
    /// callbacks own their captures, but any captured references must remain valid.
    using IntrinsicMeasure =
        std::function<Size<float>(const LayoutInput&, NodeId, NodeContext*, const Style&)>;
    /// Full leaf-algorithm replacement, including baselines/overflow/margin output.
    /// Call compute_leaf() explicitly if CSS sizing around raw measurement is wanted.
    /// May run for size queries and full layout; cache hits can skip invocation.
    using LeafLayout = std::function<LayoutOutput(LayoutTree&, NodeId, const LayoutInput&)>;

    /// Create an empty tree, reserving at least capacity node-arena slots.
    explicit Tree(std::size_t capacity = 16);
    ~Tree() override;
    Tree(const Tree&) = delete;
    Tree& operator=(const Tree&) = delete;
    /// Transfer node/context ownership; existing IDs address the destination tree.
    Tree(Tree&&) noexcept;
    /// Replace this tree (destroying its nodes), then transfer ownership from another.
    Tree& operator=(Tree&&) noexcept;

    /// Create a childless node, owning a copy/move of style, with no context.
    std::expected<NodeId, TreeError> new_leaf(Style style = {});
    /// Create a childless node and take unique ownership of context (null is allowed).
    std::expected<NodeId, TreeError> new_leaf_with_context(Style style,
                                                           std::unique_ptr<NodeContext> context);
    /// Create a node with a copied child-ID list and assign each child's parent.
    /// Detach already-parented children first: this does not edit old parents' lists.
    std::expected<NodeId, TreeError> new_with_children(Style style,
                                                       std::span<const NodeId> children);
    /// Destroy all nodes/contexts; retain arena capacity without reviving old handles.
    void clear();
    /// Remove only node, detach its children (without deleting them), and return its ID.
    std::expected<NodeId, TreeError> remove(NodeId node);

    /// Replace/destroy owned context and invalidate the node's cached layout.
    std::expected<void, TreeError> set_node_context(NodeId node,
                                                    std::unique_ptr<NodeContext> context);
    /// Borrow context; nullptr means no context or an invalid node handle.
    NodeContext* node_context(NodeId node);
    /// Const context lookup with the same null/lifetime rules.
    const NodeContext* node_context(NodeId node) const;
    /// Borrow contexts in input order; nullopt for duplicate IDs, missing context
    /// or invalid nodes. Pointers survive only while their contexts remain owned.
    std::optional<std::vector<NodeContext*>> disjoint_node_contexts(std::span<const NodeId> nodes);

    /// Append child and invalidate parent; does not detach it from an old parent.
    /// Attach a node to itself or an ancestor and TreePanic is thrown.
    std::expected<void, TreeError> add_child(NodeId parent, NodeId child);
    /// Insert before index; index == child_count is append, larger indices return
    /// ChildIndexOutOfBounds. Other parentage rules are the same as add_child().
    std::expected<void, TreeError> insert_child_at_index(NodeId parent, std::size_t index,
                                                         NodeId child);
    /// Replace the child list, detaching previous children and reparenting incoming
    /// children from their recorded parents. Cycles throw TreePanic.
    std::expected<void, TreeError> set_children(NodeId parent, std::span<const NodeId> children);
    /// Remove the first occurrence of child, reset its parent, and return its ID.
    /// Throws TreePanic if it is absent; does not delete the child node.
    std::expected<NodeId, TreeError> remove_child(NodeId parent, NodeId child);
    /// Detach/return the indexed child; out-of-bounds returns ChildIndexOutOfBounds.
    std::expected<NodeId, TreeError> remove_child_at_index(NodeId parent, std::size_t index);
    /// Detach the half-open range [start,end); invalid/reversed ranges throw TreePanic.
    std::expected<void, TreeError> remove_children_range(NodeId parent, std::size_t start,
                                                         std::size_t end);
    /// Replace/return the old child; out-of-bounds returns ChildIndexOutOfBounds.
    /// Does not detach the replacement from an old parent; cycles throw TreePanic.
    std::expected<NodeId, TreeError> replace_child_at_index(NodeId parent, std::size_t index,
                                                            NodeId child);
    /// O(1) checked lookup returning ChildIndexOutOfBounds rather than throwing for
    /// an invalid index. An invalid parent still throws NodeAccessError.
    std::expected<NodeId, TreeError> child_at_index(NodeId parent, std::size_t index) const;
    /// Recorded parent or nullopt for a root/detached node; invalid nodes throw.
    std::optional<NodeId> parent(NodeId node) const;
    /// Return an owned copy of direct child IDs; invalid nodes throw NodeAccessError.
    std::expected<std::vector<NodeId>, TreeError> children(NodeId node) const;
    /// Nonthrowing handle validation (including generation); false for zero/stale IDs.
    bool contains(NodeId node) const noexcept;
    /// Number of live nodes, excluding free/reserved slots.
    std::size_t total_node_count() const noexcept;
    /// Allocated slot capacity of the shared node arena, not any child vector's capacity.
    std::size_t capacity() const noexcept;

    /// Replace style and invalidate node/ancestor caches; borrowed old styles change.
    std::expected<void, TreeError> set_style(NodeId node, Style style);
    /// Borrow rounded output when rounding is enabled, otherwise unrounded output.
    /// Compute layout first; the reference changes on subsequent layout/storage writes
    /// and becomes invalid when the node is removed or the tree is cleared/destroyed.
    const Layout& layout(NodeId node) const;
    /// Clear this node's cache and walk ancestors until an already-empty cache.
    /// Does not dirty descendants. Call after changing captured measurement data.
    std::expected<void, TreeError> mark_dirty(NodeId node);
    /// True when this node has no cached results; not a recursive subtree query.
    std::expected<bool, TreeError> dirty(NodeId node) const;
    /// Enable final pixel rounding (the default); recompute to populate rounded output.
    void enable_rounding() noexcept;
    /// Expose unrounded results through layout(); preserves the unrounded storage.
    void disable_rounding() noexcept;
    /// Whether compute_layout() performs the final rounding traversal.
    bool rounding_enabled() const noexcept;

    /// Install raw measurement and invalidate every node. Empty callback measures zero.
    void set_intrinsic_measure(IntrinsicMeasure measure);
    /// Install a full leaf-output callback and invalidate every node.
    /// An empty callback restores the built-in leaf algorithm.
    void set_leaf_layout(LeafLayout compute);
    /// Install calc resolution and invalidate every node. Empty callback resolves to zero.
    void set_calc_resolver(CalcResolver resolver);
    /// Compute root and descendants within available_space, then optionally round.
    /// Use definite(width/height) for viewport constraints or max_content() for intrinsic
    /// sizing. The root's own Style still participates. Exceptions propagate.
    std::expected<void, TreeError> compute_layout(NodeId root,
                                                  Size<AvailableSpace> available_space);
    /// Temporarily install a full leaf callback for one synchronous root computation.
    /// The prior callback is restored on success or exception. No automatic cache
    /// invalidation occurs: call mark_dirty() when captured state/callback results change.
    std::expected<void, TreeError>
    compute_layout_with_measure(NodeId root, Size<AvailableSpace> space, LeafLayout compute);

    /// Owned source-order child snapshot; O(child_count) copying/allocation.
    std::vector<NodeId> child_ids(NodeId node) const override;
    /// Allocation-free O(1) direct child count.
    std::size_t child_count(NodeId node) const override;
    /// Allocation-free O(1) access; invalid indices throw std::out_of_range.
    NodeId child_id(NodeId node, std::size_t index) const override;
    /// Borrow style until set_style(), node removal or tree destruction.
    const Style& style(NodeId node) const override;
    /// Advanced: cached node recursion without the root positioning/rounding pass.
    LayoutOutput compute_child_layout(NodeId node, const LayoutInput& input) override;
    /// Advanced: the same recursion, sharing a borrowed block context where applicable.
    LayoutOutput compute_block_child_layout(NodeId node, const LayoutInput& input,
                                            BlockContext* context) override;
    /// Advanced: invoke raw content measurement (or return zero), without CSS sizing.
    LayoutOutput measure(NodeId node, const LayoutInput& input) override;
    /// Invoke the installed calc callback, or return zero when none is installed.
    float resolve_calc(Calc value, float basis) const override;
    /// Advanced: borrow this node's cache; manual changes bypass ancestor invalidation.
    Cache& cache(NodeId node) override;
    bool cache_contains(NodeId node, const LayoutInput& input) override;
    /// Borrow the latest unrounded layout, regardless of rounding_enabled().
    const Layout& unrounded_layout(NodeId node) const override;
    /// Advanced: overwrite unrounded storage without cache/ancestor invalidation.
    void set_unrounded_layout(NodeId node, const Layout& layout) override;
    /// Advanced: overwrite rounded storage without cache/ancestor invalidation.
    void set_final_layout(NodeId node, const Layout& layout) override;
    /// Borrow detailed metadata; monostate means no detailed information is present.
    const DetailedLayoutInfo& detailed_layout_info(NodeId node) const override;
    /// Advanced: replace the node's detailed grid metadata.
    void set_detailed_grid_info(NodeId node, DetailedGridInfo info) override;
    /// Advanced: reset hoisted descendant IDs without deleting nodes.
    void clear_hoisted_children(NodeId node) override;
    /// Advanced: copy/append hoisted IDs; does not change ordinary parent-child links.
    void add_hoisted_children(NodeId node, std::span<const NodeId> children) override;
    /// Advanced: return a copy of the hoisted descendant list.
    std::vector<NodeId> hoisted_child_ids(NodeId node) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace taffy
