#include <iostream>
#include <stdexcept>
#include <string>
#include <taffy/taffy.hpp>

struct Text {
    std::string value;

    explicit Text(std::string text) : value(std::move(text)) {}
};

int main() {
    using namespace taffy;
    Tree tree;
    tree.set_intrinsic_measure(
        [](const LayoutInput& input, NodeId, NodeContext* context, const Style&) {
            const auto* text = context_cast<Text>(context);
            const float natural_width = text ? static_cast<float>(text->value.size()) * 8 : 0;
            return Size<float>{input.known_dimensions.width.value_or(natural_width),
                               input.known_dimensions.height.value_or(16)};
        });
    Style style;
    style.padding = rect_all(LengthPercentage::length(2));
    const auto text = tree.new_leaf_with_context(style, make_context<Text>("Hello")).value();
    const Size<AvailableSpace> space{AvailableSpace::max_content(), AvailableSpace::max_content()};
    tree.compute_layout(text, space).value();
    std::cout << "intrinsic + padding=" << tree.layout(text).size.width << 'x'
              << tree.layout(text).size.height << '\n';
    if (tree.layout(text).size.width != 44)
        throw std::runtime_error("intrinsic sizing failed");

    // Full-output callbacks replace leaf CSS sizing. Calling compute_leaf opts
    // back into it and allows adding baseline/overflow metadata afterwards.
    tree.mark_dirty(text).value();
    tree.compute_layout_with_measure(
            text, space,
            [](LayoutTree& layout_tree, NodeId node, const LayoutInput& input) {
                auto output = compute_leaf(layout_tree, node, input);
                output.baselines.first = 14;
                return output;
            })
        .value();
    std::cout << "full-output callback width=" << tree.layout(text).size.width << '\n';
    // The Context and its string are destroyed automatically with the tree.
}
