#include "cache_access.hpp"
#include <bit>
#include <utility>

namespace taffy {
namespace {
constexpr std::uint32_t infinity_bits = 0x7f800000u;
constexpr std::uint32_t neg_infinity_bits = 0xff800000u;
constexpr std::uint64_t sign_bit_1 = std::uint64_t{1} << 63;
constexpr std::uint64_t sign_bit_2 = std::uint64_t{1} << 31;
constexpr auto sign_bits = sign_bit_1 | sign_bit_2;
constexpr auto non_sign_bits = ~sign_bits;
thread_local std::size_t mode_change_evictions = 0;

std::uint32_t option_key(std::optional<float> value) {
    return value ? std::bit_cast<std::uint32_t>(*value) : infinity_bits;
}

std::uint32_t available_key(AvailableSpace value) {
    switch (value.kind()) {
    case AvailableSpace::Kind::Definite:
        return std::bit_cast<std::uint32_t>(-value.value());
    case AvailableSpace::Kind::MinContent:
        return neg_infinity_bits;
    case AvailableSpace::Kind::MaxContent:
        return infinity_bits;
    }
    return infinity_bits;
}

std::uint64_t option_size_key(Size<std::optional<float>> value) {
    return (std::uint64_t{option_key(value.width)} << 32) | option_key(value.height);
}

std::uint32_t mixed_key(std::optional<float> kd, AvailableSpace avs) {
    return kd ? std::bit_cast<std::uint32_t>(*kd) : available_key(avs);
}
} // namespace

std::size_t cache_mode_change_evictions() {
    return mode_change_evictions;
}

std::uint8_t Cache::mode_from_input(const LayoutInput& input) {
    return static_cast<std::uint8_t>((input.sizing_mode == SizingMode::InherentSize ? 1 : 0) |
                                     (input.vertical_margins_are_collapsible.start ? 2 : 0) |
                                     (input.vertical_margins_are_collapsible.end ? 4 : 0));
}

Cache::Key Cache::Key::from_input(const LayoutInput& input) {
    auto axis_bits = input.axis == RequestedAxis::Horizontal ? sign_bit_1
                     : input.axis == RequestedAxis::Vertical ? sign_bit_2
                                                             : sign_bits;
    return {(std::uint64_t{mixed_key(input.known_dimensions.width, input.available_space.width)}
             << 32) |
                mixed_key(input.known_dimensions.height, input.available_space.height),
            (option_size_key(input.parent_size) & non_sign_bits) | axis_bits,
            {input.known_dimensions_are_definite.width || !input.known_dimensions.width,
             input.known_dimensions_are_definite.height || !input.known_dimensions.height}};
}

std::uint64_t Cache::Key::parent_size_basis() const {
    return parent_size & non_sign_bits;
}

bool Cache::Key::size_is_valid_for(const Key& other) const {
    const auto axis = parent_size & sign_bits;
    return axis == sign_bits || axis == (other.parent_size & sign_bits);
}

std::optional<LayoutOutput> Cache::get(const LayoutInput& input) {
    if (mode_ != mode_from_input(input))
        return std::nullopt;
    const auto key = Key::from_input(input);
    switch (input.run_mode) {
    case RunMode::PerformLayout:
        if (final_layout_entry_ && final_layout_entry_->key == key)
            return final_layout_entry_->content;
        break;
    case RunMode::ComputeSize:
        if (const auto index = matching_measure_entry(key); index != cache_size) {
            recently_used_entries_ |= std::uint16_t{1} << index;
            return LayoutOutput::from_outer_size(measure_entries_[index]->content);
        }
        break;
    case RunMode::PerformHiddenLayout:
        break;
    }
    return std::nullopt;
}

std::size_t Cache::matching_measure_entry(const Key& key) const {
    for (std::size_t index = 0; index < cache_size; ++index) {
        const auto& entry = measure_entries_[index];
        if (entry && entry->key.kd_available_space == key.kd_available_space &&
            entry->key.known_dimensions_are_definite == key.known_dimensions_are_definite &&
            entry->key.parent_size_basis() == key.parent_size_basis() &&
            entry->key.size_is_valid_for(key))
            return index;
    }
    return cache_size;
}

bool Cache::contains(const LayoutInput& input) const {
    if (mode_ != mode_from_input(input))
        return false;
    const auto key = Key::from_input(input);
    if (input.run_mode == RunMode::PerformLayout)
        return final_layout_entry_ && final_layout_entry_->key == key;
    return input.run_mode == RunMode::ComputeSize && matching_measure_entry(key) != cache_size;
}

void Cache::store(const LayoutInput& input, LayoutOutput output) {
    if (input.run_mode == RunMode::PerformHiddenLayout)
        return;
    const auto mode = mode_from_input(input);
    if (mode_ != mode) {
#ifndef NDEBUG
        if (!empty_)
            ++mode_change_evictions;
#endif
        clear();
        mode_ = mode;
    }
    const auto key = Key::from_input(input);
    if (input.run_mode == RunMode::PerformLayout) {
        empty_ = false;
        final_layout_entry_ = Entry<LayoutOutput>{key, std::move(output)};
        return;
    }
    if (output.margins_can_collapse_through || output.top_margin != CollapsibleMarginSet::ZERO ||
        output.bottom_margin != CollapsibleMarginSet::ZERO)
        return;
    empty_ = false;
    for (std::size_t i = 0; i < cache_size; ++i) {
        if (measure_entries_[i] && measure_entries_[i]->key == key) {
            measure_entries_[i]->content = output.size;
            recently_used_entries_ |= std::uint16_t{1} << i;
            return;
        }
    }
    while ((recently_used_entries_ & (std::uint16_t{1} << next_measure_entry_)) != 0) {
        recently_used_entries_ &= ~(std::uint16_t{1} << next_measure_entry_);
        next_measure_entry_ = (next_measure_entry_ + 1) % cache_size;
    }
    const auto index = next_measure_entry_;
    measure_entries_[index] = Entry<Size<float>>{key, output.size};
    recently_used_entries_ |= std::uint16_t{1} << index;
    next_measure_entry_ = (next_measure_entry_ + 1) % cache_size;
}

ClearState Cache::clear() {
    if (empty_)
        return ClearState::AlreadyEmpty;
    empty_ = true;
    final_layout_entry_.reset();
    measure_entries_ = {};
    recently_used_entries_ = 0;
    next_measure_entry_ = 0;
    return ClearState::Cleared;
}

bool Cache::is_empty() const {
    if (final_layout_entry_)
        return false;
    for (const auto& entry : measure_entries_)
        if (entry)
            return false;
    return true;
}

namespace detail {
CacheAccess::MeasureEntriesSnapshot::MeasureEntriesSnapshot(const Cache& cache)
    : entries_(cache.measure_entries_) {}

CacheAccess::MeasureEntriesSnapshot CacheAccess::snapshot(const Cache& cache) {
    return MeasureEntriesSnapshot(cache);
}

bool CacheAccess::measure_entries_equal(const Cache& cache,
                                        const MeasureEntriesSnapshot& snapshot) {
    // Match source measurement-array equality: no final entry, mode, cursor,
    // empty flag or recently-used metadata participates.
    return cache.measure_entries_ == snapshot.entries_;
}

std::size_t CacheAccess::entry_count(const Cache& cache) {
    std::size_t count = 0;
    for (const auto& entry : cache.measure_entries_)
        if (entry)
            ++count;
    return count;
}

std::uint16_t CacheAccess::recent_bits(const Cache& cache) {
    return cache.recently_used_entries_;
}

std::uint8_t CacheAccess::next_cursor(const Cache& cache) {
    return cache.next_measure_entry_;
}

bool CacheAccess::has_final_entry(const Cache& cache) {
    return cache.final_layout_entry_.has_value();
}

std::uint8_t CacheAccess::mode_bits(const LayoutInput& input) {
    return Cache::mode_from_input(input);
}

void CacheAccess::set_recent_bits_for_test(Cache& cache, std::uint16_t bits) {
    cache.recently_used_entries_ = bits;
}
} // namespace detail
} // namespace taffy
