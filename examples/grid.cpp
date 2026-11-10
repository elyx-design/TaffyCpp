#include <array>
#include <iostream>
#include <stdexcept>
#include <taffy/taffy.hpp>

int main() {
    using namespace taffy;
    Tree tree;
    const auto a = tree.new_leaf().value();
    const auto b = tree.new_leaf().value();
    Style grid;
    grid.display = Display::Grid;
    grid.size = {length(300), length(80)};
    grid.grid_template_columns = {GridTemplateComponent::length(100), GridTemplateComponent::fr(1)};
    grid.grid_template_rows = {GridTemplateComponent::length(80)};
    grid.gap.width = LengthPercentage::length(10);
    const auto root = tree.new_with_children(grid, std::array{a, b}).value();
    tree.compute_layout(root, {AvailableSpace::definite(300), AvailableSpace::definite(80)})
        .value();
    std::cout << "columns: " << tree.layout(a).size.width << ' ' << tree.layout(b).size.width
              << "; second x=" << tree.layout(b).location.x << '\n';
    const auto& info = std::get<DetailedGridInfo>(tree.detailed_layout_info(root));
    std::cout << "explicit columns=" << info.columns.explicit_tracks << '\n';
    if (tree.layout(b).size.width != 190 || tree.layout(b).location.x != 110)
        throw std::runtime_error("unexpected grid geometry");
}
