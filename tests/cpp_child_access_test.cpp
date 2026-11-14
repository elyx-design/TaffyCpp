// Production-only regression: indexed traversal must not copy child vectors.
#include <cstdio>
#include <cstdlib>
#include <new>
#include <taffy/tree.hpp>
#include <vector>

namespace {
bool reject_allocations = false;
std::size_t rejected = 0;
std::size_t allocations = 0;
} // namespace

void* operator new(std::size_t size) {
    ++allocations;
    if (reject_allocations) {
        ++rejected;
        throw std::bad_alloc{};
    }
    if (void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc{};
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* p) noexcept {
    std::free(p);
}

void operator delete[](void* p) noexcept {
    std::free(p);
}
#if defined(__cpp_sized_deallocation)
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
#endif

int main() {
    taffy::Tree tree;
    std::vector<taffy::NodeId> children;
    children.reserve(4096);
    for (std::size_t i = 0; i < 4096; ++i)
        children.push_back(tree.new_leaf().value());
    const auto parent = tree.new_with_children({}, children).value();
    const taffy::LayoutTree& view = tree;
    std::size_t count = 0;
    bool ordered = true;
    try {
        reject_allocations = true;
        count = view.child_count(parent);
        for (std::size_t i = 0; i < children.size(); ++i)
            ordered = (view.child_id(parent, i) == children[i]) && ordered;
        reject_allocations = false;
    } catch (...) {
        reject_allocations = false;
        std::fputs("FAIL: indexed traversal allocated or threw\n", stderr);
        return 1;
    }
    if (count != children.size() || !ordered || rejected != 0) {
        std::fputs("FAIL: incorrect indexed traversal\n", stderr);
        return 1;
    }
    std::puts("PASS: 4096 indexed child reads without allocation");

    // Building a nested tree bottom-up must not allocate a visited hash set for
    // every descendant at every attachment. Count work, rather than wall time.
    constexpr std::size_t chain_size = 2000;
    taffy::Tree chain(chain_size);
    const auto before = allocations;
    auto child = chain.new_leaf().value();
    for (std::size_t i = 1; i < chain_size; ++i) {
        auto next = chain.new_leaf().value();
        chain.add_child(next, child).value();
        child = next;
    }
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
    // MSVC debug iterators heap-allocate a proxy for every container (~54 allocations
    // per node, still linear). A quadratic visited set would need millions here.
    constexpr std::size_t allocations_per_node = 64;
#else
    constexpr std::size_t allocations_per_node = 3;
#endif
    if (allocations - before > allocations_per_node * chain_size) {
        std::fputs("FAIL: bottom-up construction exceeded linear allocation budget\n", stderr);
        return 1;
    }
    std::puts("PASS: bottom-up construction has linear allocation growth");

    // Force free-list growth during removal: no links may change if it fails.
    taffy::Tree topology(1);
    const auto grandchild = topology.new_leaf().value();
    const auto middle = topology.new_with_children({}, std::span(&grandchild, 1)).value();
    const auto root = topology.new_with_children({}, std::span(&middle, 1)).value();
    bool failed = false;
    try {
        reject_allocations = true;
        (void)topology.remove(middle).value();
        reject_allocations = false;
    } catch (const std::bad_alloc&) {
        reject_allocations = false;
        failed = true;
    } catch (...) {
        reject_allocations = false;
        std::fputs("FAIL: unexpected removal exception\n", stderr);
        return 1;
    }
    if (!failed || topology.total_node_count() != 3 || topology.parent(middle) != root ||
        topology.parent(grandchild) != middle || topology.child_count(root) != 1 ||
        topology.child_id(root, 0) != middle || topology.child_count(middle) != 1 ||
        topology.child_id(middle, 0) != grandchild) {
        std::fputs("FAIL: allocation failure corrupted removal topology\n", stderr);
        return 1;
    }
    (void)topology.remove(middle).value();
    if (topology.contains(middle) || topology.parent(grandchild) || topology.child_count(root)) {
        std::fputs("FAIL: removal did not recover after allocation failure\n", stderr);
        return 1;
    }
    std::puts("PASS: allocation failure leaves removal topology unchanged");
}
