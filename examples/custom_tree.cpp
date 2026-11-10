#include <iostream>
#include <stdexcept>
#include <taffy/taffy.hpp>

// A minimal externally owned one-node tree. A multi-node adapter must also
// maintain topology, per-node storage, invalidation, and hoisted OOF children.
class SingleNodeTree final : public taffy::LayoutTree {
public:
    taffy::Style node_style;
    taffy::Cache node_cache;
    taffy::Layout raw, final;
    static constexpr taffy::NodeId root{1};

    std::vector<taffy::NodeId> child_ids(taffy::NodeId) const override {
        return {};
    }

    const taffy::Style& style(taffy::NodeId) const override {
        return node_style;
    }

    taffy::Cache& cache(taffy::NodeId) override {
        return node_cache;
    }

    taffy::LayoutOutput measure(taffy::NodeId, const taffy::LayoutInput& input) override {
        return taffy::LayoutOutput::from_outer_size({input.known_dimensions.width.value_or(48),
                                                     input.known_dimensions.height.value_or(18)});
    }

    taffy::LayoutOutput compute_child_layout(taffy::NodeId node,
                                             const taffy::LayoutInput& input) override {
        // The 3-argument overload calls this virtual method; do not recurse into it.
        return taffy::compute_cached_layout(
            *this, node, input,
            [](taffy::LayoutTree& tree, taffy::NodeId id, const taffy::LayoutInput& in) {
                auto output = taffy::compute_dispatch(tree, id, in);
                if (in.run_mode == taffy::RunMode::PerformLayout)
                    taffy::compute_oof_layout(tree, id, output);
                return output;
            });
    }

    const taffy::Layout& unrounded_layout(taffy::NodeId) const override {
        return raw;
    }

    void set_unrounded_layout(taffy::NodeId, const taffy::Layout& value) override {
        raw = value;
    }

    void set_final_layout(taffy::NodeId, const taffy::Layout& value) override {
        final = value;
    }

    void clear_hoisted_children(taffy::NodeId) override {}

    void add_hoisted_children(taffy::NodeId, std::span<const taffy::NodeId> nodes) override {
        if (!nodes.empty())
            throw std::logic_error("one-node tree cannot hoist children");
    }

    std::vector<taffy::NodeId> hoisted_child_ids(taffy::NodeId) const override {
        return {};
    }
};

int main() {
    SingleNodeTree tree;
    tree.node_style.padding = taffy::rect_all(taffy::LengthPercentage::length(1));
    taffy::compute_root_layout(
        tree, SingleNodeTree::root,
        {taffy::AvailableSpace::max_content(), taffy::AvailableSpace::max_content()});
    taffy::round_layout(tree, SingleNodeTree::root);
    std::cout << "custom tree=" << tree.final.size.width << 'x' << tree.final.size.height << '\n';
    if (tree.final.size.width != 50)
        throw std::runtime_error("custom layout failed");
}
