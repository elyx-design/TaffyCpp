#include "internal.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace taffy::flex_detail {

double quantize_size(float value) noexcept {
    // Comparisons, not fmin/fmax: Rust f64::clamp preserves a NaN input.
    const double widened = value;
    if (widened < 0.0)
        return 0.0;
    const double maximum = std::numeric_limits<float>::max();
    return widened > maximum ? maximum : widened;
}

namespace {
constexpr double infeasible = std::numeric_limits<double>::infinity();

struct LineSizes {
    std::vector<std::pair<double, bool>> sums;
    double gap, limit;

    bool is_zero(std::size_t index) const {
        return sums[index].second;
    }

    double line_size(std::size_t start, std::size_t end) const {
        const double start_sum = start == 0 ? 0.0 : sums[start - 1].first;
        return sums[end].first - start_sum - gap;
    }

    double line_cost(std::size_t start, std::size_t end) const {
        const double size = line_size(start, end);
        if (end > start && size > limit)
            return infeasible;
        return size * size;
    }

    std::vector<std::size_t> suffix_greedy_counts() const {
        const auto n = sums.size();
        std::vector<std::size_t> counts(n + 1, 0);
        auto line_end = n;
        for (auto start = n; start-- > 0;) {
            while (line_end > start + 1 && line_size(start, line_end - 1) > limit)
                --line_end;
            counts[start] = 1 + counts[line_end];
        }
        return counts;
    }

    std::vector<std::size_t> fit_ends() const {
        std::vector<std::size_t> ends;
        ends.reserve(sums.size());
        std::size_t end = 0;
        for (std::size_t start = 0; start < sums.size(); ++start) {
            if (end < start)
                end = start;
            while (end + 1 < sums.size() && line_size(start, end + 1) <= limit)
                ++end;
            ends.push_back(end);
        }
        return ends;
    }
};

struct Row {
    const LineSizes& sizes;
    const std::vector<std::size_t>& fit_ends;
    const std::vector<double>& prev;
    const std::vector<std::size_t>& nonzero_ends;
    std::size_t max_end;
};

void fill_row(const Row& row, std::vector<double>& cur, std::span<std::size_t> opts,
              std::size_t start_lo, std::size_t start_hi, std::size_t col_lo, std::size_t col_hi) {
    if (start_lo > start_hi)
        return;
    const auto start = start_lo + (start_hi - start_lo) / 2;
    const auto first_col =
        static_cast<std::size_t>(std::lower_bound(row.nonzero_ends.begin() + col_lo,
                                                  row.nonzero_ends.begin() + col_hi, start) -
                                 row.nonzero_ends.begin());
    double min_cost = infeasible;
    std::optional<std::size_t> min_col;
    for (auto col = first_col; col < col_hi; ++col) {
        const auto end = row.nonzero_ends[col];
        const auto line_cost = row.sizes.line_cost(start, end);
        if (line_cost == infeasible)
            break;
        const auto cost = line_cost + row.prev[end + 1];
        if (cost <= min_cost) {
            min_cost = cost;
            min_col = col;
        }
    }
    std::optional<std::size_t> min_end;
    if (min_col)
        min_end = row.nonzero_ends[*min_col];
    const auto zero_end = std::min(row.fit_ends[start], row.max_end);
    if (row.sizes.is_zero(zero_end + 1)) {
        const auto cost = row.sizes.line_cost(start, zero_end) + row.prev[zero_end + 1];
        if (cost < min_cost || (cost == min_cost && (!min_end || *min_end < zero_end))) {
            min_cost = cost;
            min_end = zero_end;
        }
    }
    // Upstream only debug-asserts finiteness; do not change release NaN behavior.
    cur[start] = min_cost;
    opts[start] = min_end.value_or(start);
    if (start > start_lo)
        fill_row(row, cur, opts, start_lo, start - 1, col_lo, min_col ? *min_col + 1 : col_hi);
    if (start < start_hi)
        fill_row(row, cur, opts, start + 1, start_hi, min_col.value_or(first_col), col_hi);
}

AvailableSpace wrapping_space(const AlgoConstants& c, Size<AvailableSpace> available) {
    const auto main = available.main(c.dir);
    if (const auto max_size = c.max_size.main(c.dir)) {
        float limit = main.into_option().value_or(*max_size);
        if (!c.has_definite_main_size)
            limit = f32_min(limit, *max_size);
        return AvailableSpace::definite(maybe_max(limit, c.min_size.main(c.dir)));
    }
    if (!is_row(c.dir) && !c.has_definite_main_size && main.is_definite())
        return AvailableSpace::max_content();
    return main;
}

std::vector<FlexLine> singleton_lines(std::span<FlexItem> items) {
    std::vector<FlexLine> lines;
    lines.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
        lines.push_back({items.subspan(i, 1), 0.0f, 0.0f});
    return lines;
}
} // namespace

std::vector<std::size_t> balance_lines(std::span<const float> item_sizes, float line_limit,
                                       float gap_between_items, std::size_t min_line_count) {
    const auto n = item_sizes.size();
    if (n == 0)
        throw std::invalid_argument("balance_lines requires nonempty item_sizes");
    const double gap = quantize_size(gap_between_items);
    LineSizes sizes{{}, gap, static_cast<double>(line_limit)};
    sizes.sums.reserve(n);
    double sum = 0.0;
    for (float value : item_sizes) {
        const double size = quantize_size(value);
        sum += size + gap;
        sizes.sums.emplace_back(sum, size == 0.0);
    }
    const auto suffix_greedy = sizes.suffix_greedy_counts();
    const auto line_count =
        std::max(suffix_greedy[0], std::clamp(min_line_count, std::size_t{1}, n));
    if (line_count == n)
        return std::vector<std::size_t>(n, 1);
    const auto fit_ends = sizes.fit_ends();
    std::vector<std::size_t> nonzero_ends;
    nonzero_ends.reserve(n - 1);
    for (std::size_t end = 0; end < n - 1; ++end)
        if (!sizes.is_zero(end + 1))
            nonzero_ends.push_back(end);
    std::vector<double> prev;
    prev.reserve(n);
    for (std::size_t start = 0; start < n; ++start)
        prev.push_back(sizes.line_cost(start, n - 1));
    std::vector<double> cur(n, infeasible);
    if (line_count - 1 > std::numeric_limits<std::size_t>::max() / n)
        throw std::length_error("balance_lines DP storage overflow");
    std::vector<std::size_t> opts((line_count - 1) * n, 0);
    for (std::size_t lines = 2; lines <= line_count; ++lines) {
        const auto max_end = n - lines;
        std::size_t first_start = 0;
        while (first_start < max_end && suffix_greedy[first_start] > lines)
            cur[first_start++] = infeasible;
        const auto col_hi = static_cast<std::size_t>(
            std::upper_bound(nonzero_ends.begin(), nonzero_ends.end(), max_end) -
            nonzero_ends.begin());
        const Row row{sizes, fit_ends, prev, nonzero_ends, max_end};
        fill_row(row, cur, std::span(opts).subspan((lines - 2) * n, n), first_start, max_end, 0,
                 col_hi);
        prev.swap(cur);
    }
    std::vector<std::size_t> counts;
    counts.reserve(line_count);
    std::size_t start = 0;
    for (auto lines = line_count; lines >= 2; --lines) {
        const auto end = opts[(lines - 2) * n + start];
        counts.push_back(end - start + 1);
        start = end + 1;
    }
    counts.push_back(n - start);
    return counts;
}

std::vector<FlexLine> collect_flex_lines(const AlgoConstants& c, Size<AvailableSpace> available,
                                         std::vector<FlexItem>& items) {
    std::span<FlexItem> rest(items);
    if (!c.is_wrap || !c.known_main_size_is_definite)
        return {{rest, 0.0f, 0.0f}};
    const auto space = wrapping_space(c, available);
    if (space.is_max_content())
        return {{rest, 0.0f, 0.0f}};
    if (space.is_min_content())
        return singleton_lines(rest);
    std::vector<FlexLine> lines;
    lines.reserve(1);
    while (!rest.empty()) {
        float length = 0.0f;
        std::size_t index = 0;
        for (; index < rest.size(); ++index) {
            const float gap = index == 0 ? 0.0f : c.gap.main(c.dir);
            length += rest[index].hypothetical_outer_size.main(c.dir) + gap;
            if (length > space.value() && index != 0)
                break;
        }
        lines.push_back({rest.first(index), 0.0f, 0.0f});
        rest = rest.subspan(index);
    }
    return lines;
}

std::vector<FlexLine> collect_balanced_flex_lines(const AlgoConstants& c,
                                                  Size<AvailableSpace> available,
                                                  std::vector<FlexItem>& items) {
    if (items.empty())
        return {};
    const auto space = c.known_main_size_is_definite ? wrapping_space(c, available)
                                                     : AvailableSpace::max_content();
    if (space.is_min_content())
        return singleton_lines(items);
    std::vector<float> sizes;
    sizes.reserve(items.size());
    for (const auto& item : items)
        sizes.push_back(item.hypothetical_outer_size.main(c.dir));
    const auto counts =
        balance_lines(sizes, space.into_option().value_or(std::numeric_limits<float>::infinity()),
                      c.gap.main(c.dir), c.line_count.value_or(1));
    std::vector<FlexLine> lines;
    lines.reserve(counts.size());
    std::span<FlexItem> rest(items);
    for (const auto count : counts) {
        lines.push_back({rest.first(count), 0.0f, 0.0f});
        rest = rest.subspan(count);
    }
    return lines;
}
} // namespace taffy::flex_detail
