// Dependency-free installed-package consumer. Register separately from Catch tests.
#include <taffy/taffy.hpp>
#include <cstdio>

int main() {
    // Volatile pointer objects force an address reference to every DLL data symbol,
    // even in Release. Do not replace these with default-constructed local values.
    const taffy::CollapsibleMarginSet* volatile zero = &taffy::CollapsibleMarginSet::ZERO;
    const taffy::LayoutInput* volatile input = &taffy::LayoutInput::HIDDEN;
    const taffy::Baselines* volatile baselines = &taffy::Baselines::NONE;
    const taffy::OofCandidates* volatile candidates = &taffy::OofCandidates::NONE;
    const taffy::LayoutOutput* volatile hidden = &taffy::LayoutOutput::HIDDEN;
    const taffy::LayoutOutput* volatile defaults = &taffy::LayoutOutput::DEFAULT;
    if (*zero != taffy::CollapsibleMarginSet{} ||
        input->run_mode != taffy::RunMode::PerformHiddenLayout ||
        *baselines != taffy::Baselines{} || !candidates->is_empty() ||
        *hidden != taffy::LayoutOutput{} || *defaults != taffy::LayoutOutput{}) return 1;

    taffy::Tree tree;
    taffy::Style style;
    style.size = {taffy::Dimension::length(42), taffy::Dimension::length(24)};
    const auto root = tree.new_leaf(style);
    if (!root || !tree.compute_layout(*root,
        {taffy::AvailableSpace::max_content(), taffy::AvailableSpace::max_content()})) return 2;
    const auto& box = tree.layout(*root);
    if (box.size.width != 42 || box.size.height != 24) return 3;
    std::puts("six public constants and root layout 42x24: PASS");
}
