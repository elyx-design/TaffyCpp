#pragma once

/// @file
/// Owned, type-erased application data for tree nodes.
#include <memory>
#include <typeinfo>
#include <utility>

namespace taffy {

/// Application data uniquely owned by one Tree node; derive or use make_context().
/// Destroyed on replacement, removal, clear(), or Tree destruction. Destructors
/// must not throw or mutate the owning Tree; mutation is rejected with TreePanic.
/// Borrowed pointers must not outlive their owning context.
class NodeContext {
public:
    virtual ~NodeContext() = default;
};

/// NodeContext containing an application-defined, possibly move-only value.
template <class T>
class ValueContext final : public NodeContext {
public:
    /// Forward arguments to T's constructor.
    template <class... Args>
    explicit ValueContext(Args&&... args) : value(std::forward<Args>(args)...) {}

    T value; ///< Owned application value; mutations affecting measurement need mark_dirty().
};

/// Construct type-erased context storage, forwarding arguments to T.
template <class T, class... Args>
std::unique_ptr<NodeContext> make_context(Args&&... args) {
    return std::make_unique<ValueContext<T>>(std::forward<Args>(args)...);
}

/// Borrow a ValueContext<T>'s value; return nullptr for null or a different type.
/// Requires RTTI. The result has the same lifetime as the context.
template <class T>
T* context_cast(NodeContext* context) {
    auto* value = dynamic_cast<ValueContext<T>*>(context);
    return value ? std::addressof(value->value) : nullptr;
}

/// Const overload of context_cast(); does not copy or transfer ownership.
template <class T>
const T* context_cast(const NodeContext* context) {
    auto* value = dynamic_cast<const ValueContext<T>*>(context);
    return value ? std::addressof(value->value) : nullptr;
}

} // namespace taffy
