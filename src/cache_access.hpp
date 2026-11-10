#pragma once

#include <taffy/cache.hpp>

namespace taffy::detail {
// Private observation seam for native cache tests. It reads real cache state;
// no dependency on test headers, adapters or foreign ABI types enters the library.
class CacheAccess {
public:
    class MeasureEntriesSnapshot {
        friend class CacheAccess;
        std::array<std::optional<Cache::Entry<Size<float>>>, Cache::cache_size> entries_;
        explicit MeasureEntriesSnapshot(const Cache& cache);

    public:
        MeasureEntriesSnapshot(const MeasureEntriesSnapshot&) = default;
        MeasureEntriesSnapshot& operator=(const MeasureEntriesSnapshot&) = default;
    };

    static MeasureEntriesSnapshot snapshot(const Cache& cache);
    static bool measure_entries_equal(const Cache& cache, const MeasureEntriesSnapshot& snapshot);
    static std::size_t entry_count(const Cache& cache);
    static std::uint16_t recent_bits(const Cache& cache);
    static std::uint8_t next_cursor(const Cache& cache);
    static bool has_final_entry(const Cache& cache);
    static std::uint8_t mode_bits(const LayoutInput& input);
    static void set_recent_bits_for_test(Cache& cache, std::uint16_t bits);
};
} // namespace taffy::detail
