#include <array>
#include <iostream>
#include <stdexcept>
#include <taffy/taffy.hpp>

int main() {
    using namespace taffy;
    Tree tree;
    float width = 40;
    int calls = 0;
    tree.set_intrinsic_measure([&](const LayoutInput& input, NodeId, NodeContext*, const Style&) {
        ++calls;
        return Size<float>{input.known_dimensions.width.value_or(width), 20};
    });
    const auto child = tree.new_leaf().value();
    const auto root = tree.new_with_children({}, std::array{child}).value();
    const Size<AvailableSpace> space{AvailableSpace::max_content(), AvailableSpace::max_content()};
    tree.compute_layout(root, space).value();
    const int initial_calls = calls;
    tree.compute_layout(root, space).value();
    if (calls != initial_calls)
        throw std::runtime_error("cache not reused");
    width = 72;                     // Captured-state mutation is invisible to the tree.
    tree.mark_dirty(child).value(); // Invalidates child and ancestors.
    tree.compute_layout(root, space).value();
    std::cout << "cached repeat added 0 calls; changed width=" << tree.layout(child).size.width
              << '\n';
    if (tree.layout(child).size.width != 72)
        throw std::runtime_error("dirty propagation failed");

    auto updated = tree.style(child); // Copy; do not retain a borrowed style across mutation.
    updated.size.width = Dimension::length(90);
    tree.set_style(child, updated).value(); // Automatically dirties ancestors.
    tree.compute_layout(root, space).value();
    std::cout << "style width=" << tree.layout(child).size.width << '\n';
    auto bad_index = tree.child_at_index(root, 99);
    if (!bad_index)
        std::cout << "expected error: " << bad_index.error().message() << '\n';
    tree.remove(child).value(); // Removes only this node; its ID is now stale.
    try {
        (void)tree.layout(child);
        throw std::runtime_error("stale ID was accepted");
    } catch (const NodeAccessError&) {
        std::cout << "stale ID throws NodeAccessError\n";
    }
}
