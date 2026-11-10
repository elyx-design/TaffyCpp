#pragma once
#include <limits>
#include <taffy/tree.hpp>
#include <unordered_set>

namespace taffy {
struct Tree::Impl {
    struct Node {
        Style style;
        std::vector<NodeId> children;
        std::optional<NodeId> parent;
        std::unique_ptr<NodeContext> context;
        Cache cache;
        Layout unrounded;
        Layout final;
        DetailedLayoutInfo detailed;
        std::vector<NodeId> hoisted;
        // Count actual child-list edges, including shared/duplicate attachments.
        std::size_t incoming_edges = 0;
        mutable std::uint64_t cycle_visit = 0;

        explicit Node(Style value) : style(std::move(value)) {}
    };

    struct Slot {
        std::uint32_t generation = 1;
        std::unique_ptr<Node> node;
    };

    // One shared arena: each live slot owns all node data, including the
    // parent link and children list. slots.capacity() is their common capacity;
    // free is only reusable-index bookkeeping, not another component arena.
    std::vector<Slot> slots;
    std::vector<std::uint32_t> free;
    std::size_t count = 0;
    bool rounding = true;
    bool computing = false;
    bool destroying_context = false;
    mutable std::uint64_t cycle_epoch = 0;
    mutable std::vector<NodeId> cycle_pending;

    struct ContextDestructionGuard {
        bool& active;

        explicit ContextDestructionGuard(Impl& impl) : active(impl.destroying_context) {
            active = true;
        }

        ~ContextDestructionGuard() {
            active = false;
        }
    };

    void destroy_contexts() noexcept {
        destroying_context = true;
        for (auto& slot : slots)
            if (slot.node)
                slot.node->context.reset();
    }

    IntrinsicMeasure intrinsic;
    LeafLayout leaf;
    CalcResolver calc;

    struct LayoutGuard {
        bool& active;
        bool previous;

        explicit LayoutGuard(Impl& impl) : active(impl.computing), previous(active) {
            active = true;
        }

        ~LayoutGuard() {
            active = previous;
        }

        LayoutGuard(const LayoutGuard&) = delete;
        LayoutGuard& operator=(const LayoutGuard&) = delete;
    };

    explicit Impl(std::size_t capacity) {
        slots.reserve(capacity);
        free.reserve(capacity);
    }

    bool contains(NodeId id) const noexcept {
        const auto index = static_cast<std::uint32_t>(id.value);
        return id.value != 0 && index < slots.size() && slots[index].node &&
               slots[index].generation == static_cast<std::uint32_t>(id.value >> 32);
    }

    Node& get(NodeId id) {
        if (!contains(id))
            throw NodeAccessError(id);
        return *slots[static_cast<std::uint32_t>(id.value)].node;
    }

    const Node& get(NodeId id) const {
        if (!contains(id))
            throw NodeAccessError(id);
        return *slots[static_cast<std::uint32_t>(id.value)].node;
    }

    void require_mutable() const {
        if (computing || destroying_context)
            throw TreePanic("mutation during layout or context destruction");
    }

    NodeId create(Style style) {
        auto node = std::make_unique<Node>(std::move(style));
        std::uint32_t index;
        if (free.empty()) {
            if (slots.size() >= std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("Tree node index exhausted");
            index = static_cast<std::uint32_t>(slots.size());
            slots.emplace_back();
        } else {
            index = free.back();
            free.pop_back();
        }
        slots[index].node = std::move(node);
        ++count;
        return {static_cast<std::uint64_t>(slots[index].generation) << 32 | index};
    }

    void erase(NodeId id) {
        auto index = static_cast<std::uint32_t>(id.value);
        auto retired = std::move(slots[index].node);
        --count;
        // Finish bookkeeping before invoking application destruction. Callers
        // reserve the free list before making any topology changes.
        if (slots[index].generation != std::numeric_limits<std::uint32_t>::max()) {
            ++slots[index].generation;
            free.push_back(index);
        }
        ContextDestructionGuard guard(*this);
        retired.reset();
    }

    void invalidate_all() {
        for (auto& slot : slots)
            if (slot.node)
                slot.node->cache.clear();
    }

    void prevent_cycle(NodeId parent, NodeId child) const {
        if (parent == child)
            throw TreePanic("child mutation would create a cycle");
        // A node with no incoming edges cannot be reached from any subtree.
        // Unlike parent links this certificate remains valid for shared children.
        if (get(parent).incoming_edges == 0 || get(child).children.empty())
            return;
        if (++cycle_epoch == 0) {
            for (const auto& slot : slots)
                if (slot.node)
                    slot.node->cycle_visit = 0;
            ++cycle_epoch;
        }
        cycle_pending.clear();
        cycle_pending.push_back(child);
        while (!cycle_pending.empty()) {
            const auto current = cycle_pending.back();
            cycle_pending.pop_back();
            if (current == parent)
                throw TreePanic("child mutation would create a cycle");
            const auto& data = get(current);
            if (data.cycle_visit == cycle_epoch)
                continue;
            data.cycle_visit = cycle_epoch;
            cycle_pending.insert(cycle_pending.end(), data.children.begin(), data.children.end());
        }
    }
};

inline TreeError index_error(NodeId parent, std::size_t index, std::size_t count) {
    return {TreeErrorKind::ChildIndexOutOfBounds, parent, index, count};
}
} // namespace taffy
