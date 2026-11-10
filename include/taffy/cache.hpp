#pragma once

/// @file
/// Per-node layout memoization. Tree manages these caches automatically;
/// custom LayoutTree adapters can own one Cache per node or supply their own hooks.
#include <array>
#include <cstdint>
#include <optional>
#include <taffy/layout.hpp>

namespace taffy {
namespace detail {
class CacheAccess;
}

/// Whether clear() discarded a populated cache or found it already empty.
enum class ClearState {
    Cleared,
    AlreadyEmpty
};

/// Thread-local diagnostic count of mode-changing stores into nonempty caches.
/// Incremented only in debug builds; not a memory-usage or global eviction counter.
std::size_t cache_mode_change_evictions();

/// Value-owned memoization for one node: one full layout and bounded size queries.
/// Keys include constraints, requested axes, sizing mode and margin-collapse flags.
/// Style/context/topology are not keys: clear() after changing those inputs.
/// Not internally synchronized; even get() updates measurement recency metadata.
class Cache {
public:
    /// Maximum retained measurement entries (separate from the full-layout entry).
    static constexpr std::size_t cache_size = 9;
    /// Return a reusable result or nullopt. Size-only hits contain size, not the
    /// full baselines/overflow output. Hidden-layout inputs always miss.
    std::optional<LayoutOutput> get(const LayoutInput& input);
    /// Test compatibility without copying output or changing recency metadata.
    bool contains(const LayoutInput& input) const;
    /// Store a full result or its size according to input.run_mode. A mode change
    /// clears existing entries; hidden runs and noncacheable margin outputs are ignored.
    void store(const LayoutInput& input, LayoutOutput output);
    /// Invalidate retained entries and report whether there was anything to clear.
    ClearState clear();
    /// True if neither a full-layout result nor a measurement result is stored.
    bool is_empty() const;
    /// Compare complete native cache state, including recency and cursor metadata.
    bool operator==(const Cache&) const = default;

private:
    friend class detail::CacheAccess;

    struct Key {
        std::uint64_t kd_available_space = 0;
        std::uint64_t parent_size = 0;
        Size<bool> known_dimensions_are_definite{true, true};
        static Key from_input(const LayoutInput& input);
        bool size_is_valid_for(const Key& other) const;
        std::uint64_t parent_size_basis() const;
        bool operator==(const Key&) const = default;
    };

    template <class T>
    struct Entry {
        Key key;
        T content;
        bool operator==(const Entry&) const = default;
    };

    static std::uint8_t mode_from_input(const LayoutInput& input);
    std::size_t matching_measure_entry(const Key& key) const;
    std::optional<Entry<LayoutOutput>> final_layout_entry_{};
    std::array<std::optional<Entry<Size<float>>>, cache_size> measure_entries_{};
    std::uint16_t recently_used_entries_ = 0;
    std::uint8_t next_measure_entry_ = 0;
    bool empty_ = true;
    std::uint8_t mode_ = 0;
};

} // namespace taffy
