#pragma once

/// @file
/// Exceptions and structured errors for tree operations.
#include <cstddef>
#include <stdexcept>
#include <string>
#include <taffy/layout.hpp>

namespace taffy {

/// A node lookup used a zero, removed, stale, or otherwise invalid handle.
/// Tree methods can throw this even when their return type is std::expected.
class NodeAccessError : public std::out_of_range {
public:
    explicit NodeAccessError(NodeId node);
    NodeId node; ///< The rejected handle.
};

/// A topology/precondition violation, or prohibited mutation/reentrancy during layout.
class TreePanic : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

/// An unavailable custom-tree operation or an unsupported layout-mode value.
class UnsupportedAlgorithm : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

/// Categories carried by the value-returning tree error interface.
/// Invalid node lookups in Tree currently throw NodeAccessError instead.
enum class TreeErrorKind {
    ChildIndexOutOfBounds, ///< Index does not address the requested child operation.
    InvalidParentNode,     ///< Invalid parent handle (available to adapters).
    InvalidChildNode,      ///< Invalid child handle (available to adapters).
    InvalidInputNode       ///< Invalid general input handle (available to adapters).
};

/// Structured error, notably returned for checked child-index operations.
struct TreeError {
    TreeErrorKind kind = TreeErrorKind::InvalidInputNode;
    NodeId node{};               ///< Parent/subject associated with the error.
    std::size_t child_index = 0; ///< Requested index, when applicable.
    std::size_t child_count = 0; ///< Child count at the failed operation.
    /// Human-readable description; an invalid kind throws TreePanic.
    std::string message() const;
    bool operator==(const TreeError&) const = default;
};

} // namespace taffy
