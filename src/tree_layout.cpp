#include "tree_internal.hpp"
#include <taffy/compute.hpp>

namespace taffy {
const Style& LayoutTree::core_container_style(NodeId node) const {
    return style(node);
}

const Style& LayoutTree::block_container_style(NodeId node) const {
    return style(node);
}

const Style& LayoutTree::block_child_style(NodeId node) const {
    return style(node);
}

const Style& LayoutTree::flex_container_style(NodeId node) const {
    return style(node);
}

const Style& LayoutTree::flex_child_style(NodeId node) const {
    return style(node);
}

const Style& LayoutTree::grid_container_style(NodeId node) const {
    return style(node);
}

const Style& LayoutTree::grid_child_style(NodeId node) const {
    return style(node);
}

const Style& LayoutTree::oof_item_style(NodeId node) const {
    return style(node);
}

std::size_t LayoutTree::child_count(NodeId node) const {
    return child_ids(node).size();
}

NodeId LayoutTree::child_id(NodeId node, std::size_t index) const {
    return child_ids(node).at(index);
}

LayoutOutput LayoutTree::compute_block_child_layout(NodeId node, const LayoutInput& input,
                                                    BlockContext*) {
    return compute_child_layout(node, input);
}

// Pinned LayoutPartialTree and TaffyTree both resolve unhandled calc values to zero.
float LayoutTree::resolve_calc(Calc, float) const {
    return 0.0f;
}

ContainingBlockClaims LayoutTree::containing_block_claims(NodeId node) const {
    return core_container_style(node).is_containing_block();
}

Cache& LayoutTree::cache(NodeId) {
    throw UnsupportedAlgorithm("custom tree has no native Cache storage");
}

std::optional<LayoutOutput> LayoutTree::cache_get(NodeId node, const LayoutInput& input) {
    return cache(node).get(input);
}

bool LayoutTree::cache_contains(NodeId node, const LayoutInput& input) {
    return cache_get(node, input).has_value();
}

bool Tree::cache_contains(NodeId node, const LayoutInput& input) {
    return impl_->get(node).cache.contains(input);
}

void LayoutTree::cache_store(NodeId node, const LayoutInput& input, LayoutOutput output) {
    cache(node).store(input, std::move(output));
}

void LayoutTree::cache_clear(NodeId node) {
    cache(node).clear();
}

const DetailedLayoutInfo& LayoutTree::detailed_layout_info(NodeId) const {
    static const DetailedLayoutInfo none;
    return none;
}

void LayoutTree::set_detailed_grid_info(NodeId, DetailedGridInfo) {}

bool LayoutTree::is_out_of_flow(NodeId node) const {
    const auto& s = style(node);
    return s.display != Display::None && taffy::is_out_of_flow(s.position);
}

namespace {
LayoutInput child_input(Size<std::optional<float>> known, Size<std::optional<float>> parent,
                        Size<AvailableSpace> available, SizingMode mode, Line<bool> collapsible,
                        RunMode run, RequestedAxis axis) {
    LayoutInput input;
    input.known_dimensions = known;
    input.parent_size = parent;
    input.available_space = available;
    input.sizing_mode = mode;
    input.vertical_margins_are_collapsible = collapsible;
    input.run_mode = run;
    input.axis = axis;
    return input;
}
} // namespace

float LayoutTree::measure_child_size(NodeId node, Size<std::optional<float>> known,
                                     Size<std::optional<float>> parent,
                                     Size<AvailableSpace> available, SizingMode mode,
                                     AbsoluteAxis axis, Line<bool> collapsible) {
    return compute_child_layout(node, child_input(known, parent, available, mode, collapsible,
                                                  RunMode::ComputeSize, requested_axis(axis)))
        .size.get_abs(axis);
}

Size<float> LayoutTree::measure_child_size_both(NodeId node, Size<std::optional<float>> known,
                                                Size<std::optional<float>> parent,
                                                Size<AvailableSpace> available, SizingMode mode,
                                                Line<bool> collapsible) {
    return compute_child_layout(node, child_input(known, parent, available, mode, collapsible,
                                                  RunMode::ComputeSize, RequestedAxis::Both))
        .size;
}

LayoutOutput LayoutTree::perform_child_layout(NodeId node, Size<std::optional<float>> known,
                                              Size<std::optional<float>> parent,
                                              Size<AvailableSpace> available, SizingMode mode,
                                              Line<bool> collapsible) {
    return compute_child_layout(node, child_input(known, parent, available, mode, collapsible,
                                                  RunMode::PerformLayout, RequestedAxis::Both));
}

void Tree::set_intrinsic_measure(IntrinsicMeasure callback) {
    impl_->require_mutable();
    impl_->intrinsic = std::move(callback);
    impl_->invalidate_all();
}

void Tree::set_leaf_layout(LeafLayout callback) {
    impl_->require_mutable();
    impl_->leaf = std::move(callback);
    impl_->invalidate_all();
}

void Tree::set_calc_resolver(CalcResolver callback) {
    impl_->require_mutable();
    impl_->calc = std::move(callback);
    impl_->invalidate_all();
}

float Tree::resolve_calc(Calc value, float basis) const {
    if (!impl_->calc)
        return LayoutTree::resolve_calc(value, basis);
    Impl::LayoutGuard guard(*impl_);
    return impl_->calc(value, basis);
}

LayoutOutput Tree::measure(NodeId node, const LayoutInput& input) {
    Impl::LayoutGuard guard(*impl_);
    auto& data = impl_->get(node);
    return LayoutOutput::from_outer_size(
        impl_->intrinsic ? impl_->intrinsic(input, node, data.context.get(), data.style)
                         : Size<float>{});
}

LayoutOutput Tree::compute_child_layout(NodeId node, const LayoutInput& input) {
    return compute_block_child_layout(node, input, nullptr);
}

LayoutOutput Tree::compute_block_child_layout(NodeId node, const LayoutInput& input,
                                              BlockContext* context) {
    Impl::LayoutGuard guard(*impl_);
    (void)impl_->get(node);
    if (input.run_mode == RunMode::PerformHiddenLayout) {
        set_final_layout(node, Layout{});
        return compute_hidden_layout(*this, node);
    }
    return compute_cached_layout(
        *this, node, input, [this, context](LayoutTree& tree, NodeId id, const LayoutInput& in) {
            LayoutOutput output;
            const auto display = style(id).display;
            if (display == Display::None)
                output = compute_hidden_layout(tree, id);
            else if (child_count(id) == 0)
                output = impl_->leaf ? impl_->leaf(tree, id, in) : compute_leaf(tree, id, in);
            else if (display == Display::Block)
                output = compute_block(tree, id, in, context);
            else if (display == Display::FlowRoot)
                output = compute_block(tree, id, in, nullptr);
            else if (display == Display::Flex)
                output = compute_flex(tree, id, in);
            else if (display == Display::Grid)
                output = compute_grid(tree, id, in);
            else
                throw UnsupportedAlgorithm("unknown display mode");
            if (in.run_mode == RunMode::PerformLayout)
                compute_oof_layout(tree, id, output);
            return output;
        });
}

std::expected<void, TreeError> Tree::compute_layout(NodeId root, Size<AvailableSpace> space) {
    impl_->require_mutable();
    (void)impl_->get(root);
    // Rust's mutable borrow excludes structural mutation/reentrant layout. Keep
    // that invariant at runtime rather than permitting C++ callback UB.
    Impl::LayoutGuard guard(*impl_);
    compute_root_layout(*this, root, space);
    if (impl_->rounding)
        round_layout(*this, root);
    return {};
}

std::expected<void, TreeError>
Tree::compute_layout_with_measure(NodeId root, Size<AvailableSpace> space, LeafLayout callback) {
    impl_->require_mutable();
    // The temporary callback is deliberately not keyed by std::function identity:
    // caller owns invalidation when captured data changes, matching upstream.
    auto saved = std::move(impl_->leaf);
    impl_->leaf = std::move(callback);
    try {
        auto result = compute_layout(root, space);
        impl_->leaf = std::move(saved);
        return result;
    } catch (...) {
        impl_->leaf = std::move(saved);
        throw;
    }
}
} // namespace taffy
