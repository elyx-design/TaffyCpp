// Pinned upstream compute/grid/types/named.rs: owning maps of sorted occurrences.
#include "names.hpp"
#include "placement.hpp"
#include <memory>

namespace taffy::grid {
namespace {
// Walk components even when the optional outer name groups are absent. Empty
// repeats share a single boundary; their names need only be inserted once.
void expand_names(const std::vector<GridTemplateComponent>& components,
                  const TemplateLineNames& outer, std::uint16_t auto_repetitions,
                  TemplateLineNames& names,
                  std::vector<std::pair<std::uint32_t, std::string>>& pairs) {
    std::uint32_t current_line = 1;
    // Only boundaries with many aliases need a membership index.
    std::unordered_map<std::uint32_t, std::unordered_set<std::string>> membership;
    const auto add = [&](std::uint32_t line, const std::vector<std::string>& group) {
        for (const auto& name : group) {
            pairs.emplace_back(line, name);
            if (line == 0 || line > std::uint32_t(max_grid_tracks) + 1)
                continue;
            if (names.size() < line)
                names.resize(line);
            auto& target = names[line - 1];
            if (target.size() < 8) {
                if (std::find(target.begin(), target.end(), name) == target.end())
                    target.push_back(name);
                continue;
            }
            auto& indexed = membership[line];
            if (indexed.empty())
                indexed.insert(target.begin(), target.end());
            if (indexed.insert(name).second)
                target.push_back(name);
        }
    };
    // Permit name-only internal snapshots too (the public Style validator still
    // requires components.size() + 1 groups when an outer list is provided).
    for (std::size_t i = 0; i < std::max(components.size() + 1, outer.size()); ++i) {
        if (i < outer.size())
            add(current_line, outer[i]);
        if (i >= components.size()) {
            ++current_line;
            continue;
        }
        const auto* repetition = std::get_if<GridTemplateRepetition>(&components[i].value);
        if (!repetition) {
            ++current_line;
            continue;
        }
        repetition->validate();
        const auto repeat_count =
            repetition->count.is_auto() ? auto_repetitions : repetition->count.value();
        const auto lines_per_repeat = repetition->track_count();
        const auto iterations =
            lines_per_repeat == 0 ? std::min<std::uint32_t>(repeat_count, 1) : repeat_count;
        for (std::uint32_t n = 0; n < iterations; ++n) {
            for (std::size_t j = 0; j < repetition->line_names.size(); ++j)
                add(current_line + static_cast<std::uint32_t>(j), repetition->line_names[j]);
            current_line += lines_per_repeat;
            if (current_line > max_grid_tracks)
                break;
        }
    }
}
} // namespace

NamedLineResolver::NamedLineResolver(const Style& style, std::uint16_t columns,
                                     std::uint16_t rows) {
    if (style.grid_template_areas) {
        areas_ = style.grid_template_areas->areas;
        area_counts_ = {style.grid_template_areas->column_count,
                        style.grid_template_areas->row_count};
    }
    expand_names(style.grid_template_columns, style.grid_template_column_names, columns,
                 column_names_, column_pairs_);
    expand_names(style.grid_template_rows, style.grid_template_row_names, rows, row_names_,
                 row_pairs_);
    auto columns_index = std::make_shared<NameIndex>(column_names_);
    columns_index->add_areas(areas_, GridAreaAxis::Column);
    columns_index->finish();
    column_index_ = std::move(columns_index);
    auto rows_index = std::make_shared<NameIndex>(row_names_);
    rows_index->add_areas(areas_, GridAreaAxis::Row);
    rows_index->finish();
    row_index_ = std::move(rows_index);
}

std::shared_ptr<const NameIndex> NamedLineResolver::name_index(AbsoluteAxis axis) const {
    return axis == AbsoluteAxis::Horizontal ? column_index_ : row_index_;
}

const TemplateLineNames& NamedLineResolver::line_names(AbsoluteAxis axis) const {
    return axis == AbsoluteAxis::Horizontal ? column_names_ : row_names_;
}

TemplateLineNames NamedLineResolver::detailed_line_names(AbsoluteAxis axis) const {
    if (line_name_pairs(axis).empty())
        return {};
    auto result = line_names(axis);
    result.resize(std::size_t(axis == AbsoluteAxis::Horizontal ? explicit_counts_.width
                                                               : explicit_counts_.height) +
                  1);
    return result;
}

const std::vector<std::pair<std::uint32_t, std::string>>&
NamedLineResolver::line_name_pairs(AbsoluteAxis axis) const {
    return axis == AbsoluteAxis::Horizontal ? column_pairs_ : row_pairs_;
}

Line<GridPlacement> NamedLineResolver::resolve_line_names(const Line<GridPlacement>& line,
                                                          AbsoluteAxis axis) const {
    if (!has_names(line))
        return line;
    const auto& index = axis == AbsoluteAxis::Horizontal ? column_index_ : row_index_;
    return index->resolve(line, axis == AbsoluteAxis::Horizontal ? explicit_counts_.width
                                                                 : explicit_counts_.height);
}

Line<GridPlacement> NamedLineResolver::resolve_column_names(const Line<GridPlacement>& line) const {
    return resolve_line_names(line, AbsoluteAxis::Horizontal);
}

Line<GridPlacement> NamedLineResolver::resolve_row_names(const Line<GridPlacement>& line) const {
    return resolve_line_names(line, AbsoluteAxis::Vertical);
}

Line<OriginZeroGridPlacement> NamedLineResolver::resolve(const Line<GridPlacement>& line,
                                                         AbsoluteAxis axis) const {
    return into_origin_zero_ignoring_named(
        resolve_line_names(line, axis),
        axis == AbsoluteAxis::Horizontal ? explicit_counts_.width : explicit_counts_.height);
}

ItemPlacement NamedLineResolver::resolve(const Style& style) const {
    return {resolve(style.grid_column, AbsoluteAxis::Horizontal),
            resolve(style.grid_row, AbsoluteAxis::Vertical)};
}
} // namespace taffy::grid
