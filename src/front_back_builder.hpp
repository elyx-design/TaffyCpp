#pragma once

/// @file
/// Private fixed-size front/back output builder used by grid placement.

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace taffy {

/// A fixed-length, two-ended partition builder. Front insertion preserves order;
/// back insertion reverses order. Each end is independently bounds checked: an
/// insertion may overwrite the other end, but finish then rejects the overlap.
/// Unlike Rust's spare-capacity implementation, C++ destroys overwritten and
/// unfinished owned values. Slots hold engaged values before successful consumption.
/// @pre If T construction/move throws during insertion or consumption, discard or
/// replace the builder before further operations: stored values may be moved from,
/// and a failed overwriting emplacement may leave a previously filled slot empty.
template <class T>
class FrontBackVecBuilder {
public:
    /// Contract violation: bounds, incorrect final count, or use after invalidation.
    class Panic : public std::logic_error {
    public:
        using std::logic_error::logic_error;
    };

    /// Allocate exactly capacity optional slots; T need not be default constructible.
    /// Allocation exceptions propagate without constructing a builder.
    explicit FrontBackVecBuilder(std::size_t capacity) : slots_(capacity) {}

    /// Values are uniquely owned; copying is disabled.
    FrontBackVecBuilder(const FrontBackVecBuilder&) = delete;
    FrontBackVecBuilder& operator=(const FrontBackVecBuilder&) = delete;
    /// Transfer storage/state. A moved-from builder is only intended for destruction
    /// or assignment; default member moves do not reset its insertion counters.
    FrontBackVecBuilder(FrontBackVecBuilder&&) = default;
    FrontBackVecBuilder& operator=(FrontBackVecBuilder&&) = default;

    /// Move a value into the next front slot. May replace a back-inserted value.
    /// Throws Panic and invalidates the builder when this end has reached capacity.
    /// T construction exceptions propagate; the counter advances only on success.
    void push_front(T value);

    /// Move a value into the next back slot, reversing back insertion order.
    /// Bounds/exception behavior matches push_front(), independently for this end.
    void push_back(T value);

    /// Consume a completely filled, non-overlapping partition and return its values
    /// plus the front partition length. Empty capacity is valid. Throws Panic and
    /// invalidates on wrong total count or prior invalidation. Allocation/T move
    /// exceptions propagate; some stored values may already have been moved from.
    /// Successful consumption invalidates even an empty builder.
    std::pair<std::vector<T>, std::size_t> into_vec();

private:
    [[noreturn]] void panic(const char* message);
    void check_live();
    std::vector<std::optional<T>> slots_;
    std::size_t front_ = 0;
    std::size_t back_ = 0;
    bool poisoned_ = false;
};

} // namespace taffy

// Template definitions for the private builder.
#include "builder_operations.hpp"
