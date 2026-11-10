#pragma once

/// @file
/// Advanced algorithm entry points for custom LayoutTree adapters.
/// Ordinary Tree users should call Tree::compute_layout() instead.
#include <taffy/tree.hpp>

namespace taffy {

/// Synchronous uncached computation supplied to the four-argument cache helper.
using ComputeFunction = std::function<LayoutOutput(LayoutTree&, NodeId, const LayoutInput&)>;

/// Apply leaf CSS sizing around tree.measure(), without root placement or rounding.
LayoutOutput compute_leaf(LayoutTree& tree, NodeId node, const LayoutInput& input);

/// Raw content-size measurement used by compute_leaf_layout(); input is borrowed.
using IntrinsicSizeFunction = std::function<Size<float>(const LayoutInput&)>;

/// Standalone leaf sizing without a Tree. Applies style constraints to measurement.
/// Callbacks are synchronous; when invoked they must be callable, and their
/// exceptions propagate. All sizes and percentage bases are in layout units.
LayoutOutput compute_leaf_layout(const LayoutInput& input, const Style& style,
                                 const CalcResolver& calc_resolver,
                                 const IntrinsicSizeFunction& measure);

/// Compute a block container in a new block formatting context.
LayoutOutput compute_block(LayoutTree& tree, NodeId node, const LayoutInput& input);
/// Compute a block container, optionally sharing synchronously borrowed float state.
/// A null context starts a new block formatting context.
LayoutOutput compute_block(LayoutTree& tree, NodeId node, const LayoutInput& input,
                           BlockContext* context);
/// Compute a flex container; recursive measurements/layout use the adapter's hooks.
LayoutOutput compute_flex(LayoutTree& tree, NodeId node, const LayoutInput& input);
/// Compute a grid container; metadata storage uses the adapter's optional grid hook.
LayoutOutput compute_grid(LayoutTree& tree, NodeId node, const LayoutInput& input);

/// Uncached hidden/leaf/block/flex/grid dispatcher; does not position the root,
/// round the tree, or perform a separate deferred out-of-flow pass.
LayoutOutput compute_dispatch(LayoutTree& tree, NodeId node, const LayoutInput& input);
/// Forward to tree.compute_child_layout(); the adapter owns caching/dispatch.
/// Do not call this overload from that hook itself: it would recurse indefinitely.
LayoutOutput compute_cached_layout(LayoutTree& tree, NodeId node, const LayoutInput& input);
/// Return a cache hit or invoke compute and store its successful result.
/// Exceptions propagate and no result is stored on a throwing computation.
LayoutOutput compute_cached_layout(LayoutTree& tree, NodeId node, const LayoutInput& input,
                                   const ComputeFunction& compute);
/// Clear the node's cache/layout and recursively request hidden layout for children.
LayoutOutput compute_hidden_layout(LayoutTree& tree, NodeId node);
/// Lay out a root and handle root positioning/out-of-flow descendants; does not round.
/// available_space is the external constraint, not a replacement for root style.
void compute_root_layout(LayoutTree& tree, NodeId root, Size<AvailableSpace> available_space);
/// Traverse ordinary and hoisted descendants, writing pixel-rounded final layouts
/// from unrounded geometry. Call after root computation; preserves unrounded data.
void round_layout(LayoutTree& tree, NodeId root);

/// Position output's candidates in its positioning area, update hoisted children,
/// merge overflow and retain unclaimed candidates. No-op without a positioning area.
void compute_oof_layout(LayoutTree& tree, NodeId node, LayoutOutput& output);

} // namespace taffy
