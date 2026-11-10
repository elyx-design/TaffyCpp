#pragma once

/// @file
/// Advanced layout adapter interface for externally owned tree storage.
#include <cstddef>
#include <optional>
#include <span>
#include <taffy/cache.hpp>
#include <taffy/error.hpp>
#include <vector>

namespace taffy {

/// Opaque borrowed block-layout state; custom adapters can forward this pointer
/// through compute_block_child_layout() without depending on float internals.
class BlockContext;

/// Advanced adapter interface used by the layout algorithms.
///
/// The adapter owns storage, invalidation, topology validation and recursive
/// dispatch. Style/layout references are borrowed: keep them valid for their
/// synchronous use, and copy any view before calling code that can replace it.
/// The default algorithm-specific style views forward to style(). For custom
/// caching, override all of cache_get(), cache_store() and cache_clear().
class LayoutTree {
public:
    virtual ~LayoutTree() = default;

    /// Return an owned snapshot of direct children in source order.
    virtual std::vector<NodeId> child_ids(NodeId node) const = 0;
    /// Borrow a node's complete style until replacement or node destruction.
    virtual const Style& style(NodeId node) const = 0;
    /// Core sizing/positioning view; defaults to style(node).
    virtual const Style& core_container_style(NodeId node) const;
    /// Block container view; defaults to style(node).
    virtual const Style& block_container_style(NodeId node) const;
    /// Block item view; defaults to style(node).
    virtual const Style& block_child_style(NodeId node) const;
    /// Flex container view; defaults to style(node).
    virtual const Style& flex_container_style(NodeId node) const;
    /// Flex item view; defaults to style(node).
    virtual const Style& flex_child_style(NodeId node) const;
    /// Grid container view; defaults to style(node).
    virtual const Style& grid_container_style(NodeId node) const;
    /// Grid item view; defaults to style(node).
    virtual const Style& grid_child_style(NodeId node) const;
    /// Out-of-flow item view; defaults to style(node).
    virtual const Style& oof_item_style(NodeId node) const;

    /// Recursively measure or lay out a node according to input.run_mode.
    /// A custom adapter must implement dispatch and its desired cache policy.
    virtual LayoutOutput compute_child_layout(NodeId node, const LayoutInput& input) = 0;
    /// Block recursion with synchronously borrowed float/margin context.
    /// The default ignores context and calls compute_child_layout().
    virtual LayoutOutput compute_block_child_layout(NodeId node, const LayoutInput& input,
                                                    BlockContext* context);
    /// Raw intrinsic content measurement; compute_leaf() applies CSS around it.
    virtual LayoutOutput measure(NodeId node, const LayoutInput& input) = 0;
    /// Resolve an application-owned calc handle against a layout-unit basis.
    /// The default returns zero. Resolver exceptions propagate to the caller.
    virtual float resolve_calc(Calc value, float basis) const;
    /// Containing-block capabilities derived from core_container_style().
    virtual ContainingBlockClaims containing_block_claims(NodeId node) const;

    /// Borrow native cache storage; the default throws UnsupportedAlgorithm.
    virtual Cache& cache(NodeId node);
    /// Query a cached result; the default delegates to cache(node).get().
    virtual std::optional<LayoutOutput> cache_get(NodeId node, const LayoutInput& input);
    /// Store a result; the default delegates to cache(node).store().
    virtual void cache_store(NodeId node, const LayoutInput& input, LayoutOutput output);
    /// Invalidate a node's cache; the default delegates to cache(node).clear().
    virtual void cache_clear(NodeId node);

    /// Borrow the most recently stored, unrounded border-box layout.
    virtual const Layout& unrounded_layout(NodeId node) const = 0;
    /// Store algorithm output before pixel rounding.
    virtual void set_unrounded_layout(NodeId node, const Layout& layout) = 0;
    /// Store final output after rounding; separate from unrounded storage.
    virtual void set_final_layout(NodeId node, const Layout& layout) = 0;
    /// Borrow optional detailed metadata; the default returns an empty variant.
    virtual const DetailedLayoutInfo& detailed_layout_info(NodeId node) const;
    /// Store grid metadata; the default discards it.
    virtual void set_detailed_grid_info(NodeId node, DetailedGridInfo info);
    /// Reset the node's list of hoisted out-of-flow descendants.
    virtual void clear_hoisted_children(NodeId node) = 0;
    /// Append IDs; the adapter must copy, not retain, this borrowed span.
    virtual void add_hoisted_children(NodeId node, std::span<const NodeId> children) = 0;
    /// Return an owned snapshot of hoisted descendants for traversal/rounding.
    virtual std::vector<NodeId> hoisted_child_ids(NodeId node) const = 0;
    /// True for a generated box with absolute or fixed positioning.
    virtual bool is_out_of_flow(NodeId node) const;

    /// Direct child count. Override for cheap access: the default copies child_ids().
    virtual std::size_t child_count(NodeId node) const;
    /// Child at a zero-based index. Override to avoid the default snapshot copy.
    /// Algorithms making repeated accesses should take one child_ids() snapshot
    /// unless indexed access is known to be O(1); these compatibility defaults
    /// otherwise make a complete indexed traversal quadratic.
    /// The default throws std::out_of_range when index is outside the snapshot.
    virtual NodeId child_id(NodeId node, std::size_t index) const;

    /// ComputeSize query for one physical axis, returning its border-box extent.
    /// collapsible marks top/bottom margins allowed to collapse with the parent.
    float measure_child_size(NodeId node, Size<std::optional<float>> known_dimensions,
                             Size<std::optional<float>> parent_size,
                             Size<AvailableSpace> available_space, SizingMode mode,
                             AbsoluteAxis axis, Line<bool> collapsible = {});
    /// ComputeSize query for both border-box dimensions, with the same input rules.
    Size<float> measure_child_size_both(NodeId node, Size<std::optional<float>> known_dimensions,
                                        Size<std::optional<float>> parent_size,
                                        Size<AvailableSpace> available_space, SizingMode mode,
                                        Line<bool> collapsible = {});
    /// PerformLayout query: compute descendants and return the node's full output.
    LayoutOutput perform_child_layout(NodeId node, Size<std::optional<float>> known_dimensions,
                                      Size<std::optional<float>> parent_size,
                                      Size<AvailableSpace> available_space, SizingMode mode,
                                      Line<bool> collapsible = {});

    /// Check compatibility. Override to avoid copying cached output; the default
    /// calls cache_get() to preserve adapters with custom cache hooks.
    virtual bool cache_contains(NodeId node, const LayoutInput& input);
};

} // namespace taffy
