#include <array>
#include <iostream>
#include <stdexcept>
#include <taffy/taffy.hpp>

int main() {
    using namespace taffy;
    Tree tree;
    Style item;
    item.size = {length(40), length(20)};
    item.flex_grow = 1;
    const auto a = tree.new_leaf(item).value();
    const auto b = tree.new_leaf(item).value();
    Style container;
    container.size = {length(200), length(60)};
    container.gap.width = LengthPercentage::length(10);
    const auto root = tree.new_with_children(container, std::array{a, b}).value();
    tree.compute_layout(root, {AvailableSpace::definite(200), AvailableSpace::definite(60)})
        .value();
    for (auto node : std::array{a, b}) {
        const auto& box = tree.layout(node);
        std::cout << "item x=" << box.location.x << " y=" << box.location.y
                  << " width=" << box.size.width << " height=" << box.size.height << '\n';
    }
    if (tree.layout(a).size.width != 95 || tree.layout(b).location.x != 105)
        throw std::runtime_error("unexpected flex geometry");
}
