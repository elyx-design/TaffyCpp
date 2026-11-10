#include "tree_internal.hpp"
#include <algorithm>
#include <exception>

namespace taffy {
NodeAccessError::NodeAccessError(NodeId id)
    : std::out_of_range("invalid or stale NodeId " + std::to_string(id.value)), node(id) {}

std::string TreeError::message() const {
    switch (kind) {
    case TreeErrorKind::ChildIndexOutOfBounds:
        return "child index " + std::to_string(child_index) + " out of bounds for " +
               std::to_string(child_count) + " children";
    case TreeErrorKind::InvalidParentNode:
        return "invalid parent node";
    case TreeErrorKind::InvalidChildNode:
        return "invalid child node";
    case TreeErrorKind::InvalidInputNode:
        return "invalid input node";
    }
    throw TreePanic("invalid TreeErrorKind");
}

Tree::Tree(std::size_t capacity) : impl_(std::make_unique<Impl>(capacity)) {}

Tree::~Tree() {
    if (impl_)
        impl_->destroy_contexts();
}

Tree::Tree(Tree&& other) noexcept {
    if (other.impl_ && (other.impl_->destroying_context || other.impl_->computing))
        std::terminate();
    impl_ = std::move(other.impl_);
}

Tree& Tree::operator=(Tree&& other) noexcept {
    if (this == &other)
        return *this;
    if ((impl_ && (impl_->destroying_context || impl_->computing)) ||
        (other.impl_ && (other.impl_->destroying_context || other.impl_->computing)))
        std::terminate();
    if (impl_)
        impl_->destroy_contexts();
    impl_ = std::move(other.impl_);
    return *this;
}

bool Tree::contains(NodeId id) const noexcept {
    return impl_ && impl_->contains(id);
}

std::size_t Tree::total_node_count() const noexcept {
    return impl_ ? impl_->count : 0;
}

std::size_t Tree::capacity() const noexcept {
    return impl_ ? impl_->slots.capacity() : 0;
}

std::expected<NodeId, TreeError> Tree::new_leaf(Style style) {
    impl_->require_mutable();
    return impl_->create(std::move(style));
}

std::expected<NodeId, TreeError> Tree::new_leaf_with_context(Style style,
                                                             std::unique_ptr<NodeContext> context) {
    const auto id = new_leaf(std::move(style)).value();
    impl_->get(id).context = std::move(context);
    return id;
}

std::expected<NodeId, TreeError> Tree::new_with_children(Style style,
                                                         std::span<const NodeId> children) {
    impl_->require_mutable();
    for (auto child : children)
        (void)impl_->get(child);
    std::vector<NodeId> copied(children.begin(), children.end());
    auto id = impl_->create(std::move(style));
    impl_->get(id).children = std::move(copied);
    for (auto child : children) {
        auto& data = impl_->get(child);
        data.parent = id;
        ++data.incoming_edges;
    }
    return id;
}

void Tree::clear() {
    impl_->require_mutable();
    // Keep slots/generations: clearing storage must not revive stale IDs.
    impl_->free.reserve(impl_->slots.size());
    for (std::size_t i = 0; i < impl_->slots.size(); ++i) {
        if (impl_->slots[i].node)
            impl_->erase({static_cast<std::uint64_t>(impl_->slots[i].generation) << 32 | i});
    }
}

std::expected<NodeId, TreeError> Tree::remove(NodeId node) {
    impl_->require_mutable();
    auto& data = impl_->get(node);
    // Reserve before detaching anything: allocation failure leaves topology intact.
    impl_->free.reserve(impl_->slots.size());
    if (data.parent && contains(*data.parent)) {
        auto& siblings = impl_->get(*data.parent).children;
        std::erase(siblings, node);
        (void)mark_dirty(*data.parent);
    }
    for (auto child : data.children) {
        auto& child_data = impl_->get(child);
        child_data.parent.reset();
        --child_data.incoming_edges;
    }
    impl_->erase(node);
    return node;
}

std::expected<void, TreeError> Tree::set_node_context(NodeId node,
                                                      std::unique_ptr<NodeContext> context) {
    impl_->require_mutable();
    auto retired = std::exchange(impl_->get(node).context, std::move(context));
    (void)mark_dirty(node);
    Impl::ContextDestructionGuard guard(*impl_);
    retired.reset();
    return {};
}

NodeContext* Tree::node_context(NodeId node) {
    return contains(node) ? impl_->get(node).context.get() : nullptr;
}

const NodeContext* Tree::node_context(NodeId node) const {
    return contains(node) ? impl_->get(node).context.get() : nullptr;
}

std::optional<std::vector<NodeContext*>>
Tree::disjoint_node_contexts(std::span<const NodeId> nodes) {
    std::unordered_set<NodeId> seen;
    std::vector<NodeContext*> contexts;
    contexts.reserve(nodes.size());
    for (auto id : nodes) {
        auto* context = node_context(id);
        if (!context || !seen.insert(id).second)
            return std::nullopt;
        contexts.push_back(context);
    }
    return contexts;
}

std::expected<void, TreeError> Tree::add_child(NodeId parent, NodeId child) {
    return insert_child_at_index(parent, impl_->get(parent).children.size(), child);
}

std::expected<void, TreeError> Tree::insert_child_at_index(NodeId parent, std::size_t index,
                                                           NodeId child) {
    impl_->require_mutable();
    auto& data = impl_->get(parent);
    if (index > data.children.size())
        return std::unexpected(index_error(parent, index, data.children.size()));
    auto& child_data = impl_->get(child);
    impl_->prevent_cycle(parent, child);
    data.children.insert(data.children.begin() + static_cast<std::ptrdiff_t>(index), child);
    child_data.parent = parent;
    ++child_data.incoming_edges;
    return mark_dirty(parent);
}

std::expected<void, TreeError> Tree::set_children(NodeId parent, std::span<const NodeId> children) {
    impl_->require_mutable();
    auto& data = impl_->get(parent);
    std::vector<NodeId> copied(children.begin(), children.end());
    for (auto child : copied) {
        (void)impl_->get(child);
        impl_->prevent_cycle(parent, child);
    }
    for (auto child : data.children)
        impl_->get(child).parent.reset();
    for (auto child : copied) {
        auto& c = impl_->get(child);
        if (c.parent) {
            auto previous = *c.parent;
            // Upstream removes the first occurrence, not every duplicate.
            (void)remove_child(previous, child);
        }
        c.parent = parent;
    }
    for (auto child : data.children)
        --impl_->get(child).incoming_edges;
    for (auto child : copied)
        ++impl_->get(child).incoming_edges;
    data.children = std::move(copied);
    return mark_dirty(parent);
}

std::expected<NodeId, TreeError> Tree::remove_child(NodeId parent, NodeId child) {
    impl_->require_mutable();
    auto& children = impl_->get(parent).children;
    const auto it = std::find(children.begin(), children.end(), child);
    if (it == children.end())
        throw TreePanic("remove_child: node is not a child of parent");
    return remove_child_at_index(parent, static_cast<std::size_t>(it - children.begin()));
}

std::expected<NodeId, TreeError> Tree::remove_child_at_index(NodeId parent, std::size_t index) {
    impl_->require_mutable();
    auto& children = impl_->get(parent).children;
    if (index >= children.size())
        return std::unexpected(index_error(parent, index, children.size()));
    const auto child = children[index];
    children.erase(children.begin() + static_cast<std::ptrdiff_t>(index));
    auto& child_data = impl_->get(child);
    child_data.parent.reset();
    --child_data.incoming_edges;
    (void)mark_dirty(parent);
    return child;
}

std::expected<void, TreeError> Tree::remove_children_range(NodeId parent, std::size_t start,
                                                           std::size_t end) {
    impl_->require_mutable();
    auto& children = impl_->get(parent).children;
    if (start > end || end > children.size())
        throw TreePanic("remove_children_range: invalid half-open range");
    for (std::size_t i = start; i < end; ++i) {
        auto& child = impl_->get(children[i]);
        child.parent.reset();
        --child.incoming_edges;
    }
    children.erase(children.begin() + static_cast<std::ptrdiff_t>(start),
                   children.begin() + static_cast<std::ptrdiff_t>(end));
    return mark_dirty(parent);
}

std::expected<NodeId, TreeError> Tree::replace_child_at_index(NodeId parent, std::size_t index,
                                                              NodeId child) {
    impl_->require_mutable();
    auto& children = impl_->get(parent).children;
    if (index >= children.size())
        return std::unexpected(index_error(parent, index, children.size()));
    auto& next = impl_->get(child);
    impl_->prevent_cycle(parent, child);
    next.parent = parent;
    const auto old = std::exchange(children[index], child);
    ++next.incoming_edges;
    auto& old_data = impl_->get(old);
    --old_data.incoming_edges;
    old_data.parent.reset(); // Matches upstream even when old == child.
    (void)mark_dirty(parent);
    return old;
}

std::expected<NodeId, TreeError> Tree::child_at_index(NodeId parent, std::size_t index) const {
    const auto& children = impl_->get(parent).children;
    if (index >= children.size())
        return std::unexpected(index_error(parent, index, children.size()));
    return children[index];
}

std::optional<NodeId> Tree::parent(NodeId node) const {
    return impl_->get(node).parent;
}

std::expected<std::vector<NodeId>, TreeError> Tree::children(NodeId node) const {
    return impl_->get(node).children;
}

std::vector<NodeId> Tree::child_ids(NodeId node) const {
    return impl_->get(node).children;
}

std::size_t Tree::child_count(NodeId node) const {
    return impl_->get(node).children.size();
}

NodeId Tree::child_id(NodeId node, std::size_t index) const {
    return impl_->get(node).children.at(index);
}

const Style& Tree::style(NodeId node) const {
    return impl_->get(node).style;
}

std::expected<void, TreeError> Tree::set_style(NodeId node, Style style) {
    impl_->require_mutable();
    impl_->get(node).style = std::move(style);
    return mark_dirty(node);
}

const Layout& Tree::layout(NodeId node) const {
    return impl_->rounding ? impl_->get(node).final : impl_->get(node).unrounded;
}

std::expected<void, TreeError> Tree::mark_dirty(NodeId node) {
    impl_->require_mutable();
    // Stop at an already-empty cache, exactly as upstream does. Mutation APIs
    // preserve the ancestor dirty invariant for valid single-parent trees.
    for (;;) {
        auto& data = impl_->get(node);
        if (data.cache.clear() == ClearState::AlreadyEmpty || !data.parent)
            break;
        node = *data.parent;
    }
    return {};
}

std::expected<bool, TreeError> Tree::dirty(NodeId node) const {
    return impl_->get(node).cache.is_empty();
}

void Tree::enable_rounding() noexcept {
    impl_->rounding = true;
}

void Tree::disable_rounding() noexcept {
    impl_->rounding = false;
}

bool Tree::rounding_enabled() const noexcept {
    return impl_->rounding;
}

Cache& Tree::cache(NodeId node) {
    return impl_->get(node).cache;
}

const Layout& Tree::unrounded_layout(NodeId node) const {
    return impl_->get(node).unrounded;
}

void Tree::set_unrounded_layout(NodeId node, const Layout& layout) {
    impl_->get(node).unrounded = layout;
}

void Tree::set_final_layout(NodeId node, const Layout& layout) {
    impl_->get(node).final = layout;
}

const DetailedLayoutInfo& Tree::detailed_layout_info(NodeId node) const {
    return impl_->get(node).detailed;
}

void Tree::set_detailed_grid_info(NodeId node, DetailedGridInfo info) {
    impl_->get(node).detailed = std::move(info);
}

void Tree::clear_hoisted_children(NodeId node) {
    impl_->get(node).hoisted.clear();
}

void Tree::add_hoisted_children(NodeId node, std::span<const NodeId> children) {
    auto& hoisted = impl_->get(node).hoisted;
    hoisted.insert(hoisted.end(), children.begin(), children.end());
}

std::vector<NodeId> Tree::hoisted_child_ids(NodeId node) const {
    return impl_->get(node).hoisted;
}
} // namespace taffy
