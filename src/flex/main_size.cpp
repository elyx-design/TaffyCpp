#include "../sizing.hpp"
#include "internal.hpp"

// Pinned upstream flexbox.rs: 947-1231 and 1397-1944.
// Measurement calls deliberately retain the original single-axis wrapper;
// final-layout callers must use item_known_dimension_definiteness in LayoutInput.

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace taffy::flex_detail {
namespace {
// Rust f32::total_cmp, including signed zero and both signs of NaN. Used only
// where upstream uses Iterator::max_by(total_cmp), not for ordinary float max.
bool total_less(float a, float b) {
    auto key = [](float value) {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        return (bits & 0x80000000u) ? ~bits : (bits ^ 0x80000000u);
    };
    return key(a) < key(b);
}
} // namespace

void determine_flex_base_size(LayoutTree& tree, const AlgoConstants& constants,
                              Size<AvailableSpace> available_space,
                              std::vector<FlexItem>& flex_items) {
    const auto dir = constants.dir;
    const CalcResolver calc = [&](Calc value, float basis) {
        return tree.resolve_calc(value, basis);
    };
    for (auto& child : flex_items) {
        const auto [box_sizing_adjustment, flex_basis_style] = [&] {
            const auto& style = tree.flex_child_style(child.node);
            return std::pair{child.box_sizing_adjustment(style).main(dir), style.flex_basis};
        }();
        const auto cross_axis_parent_size = constants.node_inner_size.cross(dir);
        const auto child_parent_size =
            Size<std::optional<float>>::from_cross(dir, cross_axis_parent_size);
        // Transfer cross constraints even for replaced/compressible items; final
        // flexible-length clamping below intentionally does not use these transfers.
        const auto transferred_min_size =
            child.min_size.maybe_apply_aspect_ratio(child.aspect_ratio);
        const auto transferred_max_size =
            child.max_size.maybe_apply_aspect_ratio(child.aspect_ratio);
        const auto child_min_cross = transferred_min_size.cross(dir);
        const auto child_max_cross = transferred_max_size.cross(dir);
        const auto cross_axis_available_space = [&] {
            const auto space = available_space.cross(dir);
            if (space.is_definite()) {
                return AvailableSpace::definite(
                    maybe_clamp(f32_max(constants.divided_cross_space(
                                            cross_axis_parent_size.value_or(space.value())) -
                                            child.margin.cross_axis_sum(dir),
                                        0.0f),
                                child_min_cross, child_max_cross));
            }
            if (space.is_min_content()) {
                return child_min_cross ? AvailableSpace::definite(*child_min_cross)
                                       : AvailableSpace::min_content();
            }
            return child_max_cross ? AvailableSpace::definite(*child_max_cross)
                                   : AvailableSpace::max_content();
        }();

        bool child_cross_size_is_definite = child.size.cross(dir).has_value();
        auto child_known_dimensions = child.size.with_main(dir, std::nullopt);
        child_known_dimensions.set_cross(dir, maybe_clamp(child_known_dimensions.cross(dir),
                                                          transferred_min_size.cross(dir),
                                                          transferred_max_size.cross(dir)));
        if (child.align_self == AlignSelf::stretch() && !child.margin_is_auto.cross_start(dir) &&
            !child.margin_is_auto.cross_end(dir) && !child_known_dimensions.cross(dir)) {
            child_known_dimensions.set_cross(dir, cross_axis_available_space.into_option());
            child_cross_size_is_definite = !constants.is_wrap &&
                                           constants.has_definite_cross_size &&
                                           cross_axis_parent_size.has_value();
        }

        const auto percent_resolution_main_size = constants.known_main_size_is_definite
                                                      ? constants.node_inner_size.main(dir)
                                                      : std::nullopt;
        // Both style inputs were copied before any calc/measurement callbacks.
        const auto flex_basis =
            maybe_add(flex_basis_style.maybe_resolve(percent_resolution_main_size, calc),
                      box_sizing_adjustment);
        child.flex_basis = [&]() -> float {
            const auto main_size = child.size.main(dir);
            const auto main_stretch_size = maybe_max(
                maybe_sub(percent_resolution_main_size, child.margin.main_axis_sum(dir)), 0.0f);
            std::optional<AvailableSpace> keyword_main_available_space;
            if (flex_basis_style.is_content()) {
                // Content ignores the preferred main size and uses the default constraint.
            } else if (flex_basis_style.is_sizing_keyword()) {
                const auto resolution = resolve_sizing_keyword(flex_basis_style, main_stretch_size,
                                                               percent_resolution_main_size, calc);
                if (resolution) {
                    if (resolution->kind == SizingKeywordResolution::Kind::Exact) {
                        child.flex_basis_is_definite = true;
                        return resolution->exact;
                    }
                    keyword_main_available_space = resolution->available_space;
                }
            } else {
                const auto basis = flex_basis
                                       ? flex_basis
                                       : (flex_basis_style.is_auto() ? main_size : std::nullopt);
                if (basis) {
                    child.flex_basis_is_definite = true;
                    return *basis;
                }
                if (flex_basis_style.is_auto()) {
                    const auto resolution =
                        resolve_sizing_keyword(child.size_style.main(dir), main_stretch_size,
                                               percent_resolution_main_size, calc);
                    if (resolution) {
                        if (resolution->kind == SizingKeywordResolution::Kind::Exact) {
                            child.flex_basis_is_definite = true;
                            return resolution->exact;
                        }
                        keyword_main_available_space = resolution->available_space;
                    }
                }
            }
            if (child_cross_size_is_definite && child.aspect_ratio &&
                child_known_dimensions.cross(dir)) {
                child.flex_basis_is_definite = true;
                const float cross = *child_known_dimensions.cross(dir);
                return is_row(dir) ? cross * *child.aspect_ratio : cross / *child.aspect_ratio;
            }
            const auto child_available_space =
                Size<AvailableSpace>::max_content()
                    .with_main(dir, keyword_main_available_space.value_or(
                                        available_space.main(dir).is_min_content()
                                            ? AvailableSpace::min_content()
                                            : AvailableSpace::max_content()))
                    .with_cross(dir, cross_axis_available_space);
            return tree.measure_child_size(child.node, child_known_dimensions, child_parent_size,
                                           child_available_space, SizingMode::ContentSize,
                                           main_axis(dir), Line<bool>::FALSE);
        }();

        const float padding_border_sum =
            child.padding.main_axis_sum(dir) + child.border.main_axis_sum(dir);
        child.flex_basis = f32_max(child.flex_basis, padding_border_sum);
        child.inner_flex_basis = child.flex_basis - padding_border_sum;
        const auto padding_border_axes_sums =
            (child.padding + child.border).sum_axes().map([](float value) {
                return std::optional<float>{value};
            });
        const auto overflow_min =
            Size<std::optional<float>>{maybe_into_automatic_min_size(child.overflow.x),
                                       maybe_into_automatic_min_size(child.overflow.y)};
        const auto style_min_main_size = child.min_size.or_(overflow_min).main(dir);
        if (style_min_main_size) {
            child.resolved_minimum_main_size = *style_min_main_size;
        } else {
            const auto child_available_space =
                Size<AvailableSpace>::min_content().with_cross(dir, cross_axis_available_space);
            const float min_content_main_size = tree.measure_child_size(
                child.node, child_known_dimensions, child_parent_size, child_available_space,
                SizingMode::ContentSize, main_axis(dir), Line<bool>::FALSE);
            const float clamped_min_content_size =
                maybe_min(maybe_min(min_content_main_size, child.size.main(dir)),
                          transferred_max_size.main(dir));
            child.resolved_minimum_main_size =
                maybe_max(clamped_min_content_size, padding_border_axes_sums.main(dir));
        }
        const float hypothetical_inner_min_main =
            maybe_max(maybe_max(child.resolved_minimum_main_size, transferred_min_size.main(dir)),
                      padding_border_axes_sums.main(dir));
        const float hypothetical_inner_size =
            maybe_clamp(child.flex_basis, std::optional<float>{hypothetical_inner_min_main},
                        transferred_max_size.main(dir));
        child.hypothetical_inner_size.set_main(dir, hypothetical_inner_size);
        child.hypothetical_outer_size.set_main(dir, hypothetical_inner_size +
                                                        child.margin.main_axis_sum(dir));
    }
}

Size<bool> item_known_dimension_definiteness(const AlgoConstants& constants, const FlexItem& item) {
    const auto dir = constants.dir;
    const bool main_is_definite = constants.has_definite_main_size || item.flex_basis_is_definite;
    const bool has_cross_auto_margins =
        item.margin_is_auto.cross_start(dir) || item.margin_is_auto.cross_end(dir);
    const auto cross_size = item.size_style.cross(dir);
    const bool is_stretched = !has_cross_auto_margins &&
                              (cross_size.is_stretch() ||
                               (item.align_self == AlignSelf::stretch() && cross_size.is_auto()));
    const bool cross_is_definite =
        is_stretched || item.size.cross(dir).has_value() ||
        (!is_row(dir) && constants.cross_axis_available_space_is_definite);
    return Size<bool>{true, true}
        .with_main(dir, main_is_definite)
        .with_cross(dir, cross_is_definite);
}

void determine_container_main_size(LayoutTree& tree, Size<AvailableSpace> available_space,
                                   std::vector<FlexLine>& lines, AlgoConstants& constants) {
    const auto dir = constants.dir;
    const float main_content_box_inset = constants.content_box_inset.main_axis_sum(dir);
    const float outer_main_size_unclamped = [&]() -> float {
        if (const auto known = constants.node_outer_size.main(dir))
            return *known;
        const auto main_available_space = available_space.main(dir);
        const auto item_main_length = [&](const FlexItem& child) {
            const float padding_border_sum = (child.padding + child.border).main_axis_sum(dir);
            return f32_max(maybe_max(child.flex_basis, child.min_size.main(dir)) +
                               child.margin.main_axis_sum(dir),
                           padding_border_sum);
        };
        if (main_available_space.is_definite() ||
            (main_available_space.is_min_content() && constants.is_wrap)) {
            std::optional<float> longest_line_length;
            for (const auto& line : lines) {
                // Rust Sum<f32> starts at negative zero (unlike explicit folds).
                float total_target_size = -0.0f;
                for (const auto& child : line.items)
                    total_target_size += item_main_length(child);
                const float length =
                    total_target_size + sum_axis_gaps(constants.gap.main(dir), line.items.size());
                if (!longest_line_length || !total_less(length, *longest_line_length))
                    longest_line_length = length;
            }
            const float size = longest_line_length.value_or(0.0f) + main_content_box_inset;
            if (!main_available_space.is_definite())
                return size;
            if (constants.is_balance) {
                std::size_t item_count = 0;
                for (const auto& line : lines)
                    item_count += line.items.size();
                if (item_count == 0)
                    return size;
                std::vector<float> item_lengths;
                item_lengths.reserve(item_count);
                for (const auto& line : lines)
                    for (const auto& child : line.items)
                        item_lengths.push_back(item_main_length(child));
                const auto min_line_count = constants.line_count.value_or(1);
                const auto counts =
                    balance_lines(item_lengths, std::numeric_limits<float>::infinity(),
                                  constants.gap.main(dir), min_line_count > 1 ? min_line_count : 1);
                float widest_line_length = 0.0f;
                std::size_t index = 0;
                for (const auto count : counts) {
                    float line_length = -0.0f;
                    for (std::size_t end = index + count; index < end; ++index)
                        line_length += item_lengths[index];
                    line_length += sum_axis_gaps(constants.gap.main(dir), count);
                    widest_line_length = f32_max(widest_line_length, line_length);
                }
                const float max_content_size = widest_line_length + main_content_box_inset;
                return f32_max(size, f32_min(max_content_size, main_available_space.value()));
            }
            return lines.size() > 1 ? f32_max(size, main_available_space.value()) : size;
        }

        float main_size = 0.0f;
        for (auto& line : lines) {
            for (auto& item : line.items) {
                const auto style_min = item.min_size.main(dir);
                const float margin_sum = item.margin.main_axis_sum(dir);
                const auto style_preferred = item.size.main(dir);
                const auto style_max = item.max_size.main(dir);
                const std::optional<float> clamping_basis =
                    constants.is_row
                        ? maybe_max(std::optional<float>{item.flex_basis}, style_preferred)
                        : std::optional<float>{item.flex_basis};
                const auto flex_basis_min =
                    item.flex_shrink == 0.0f ? clamping_basis : std::nullopt;
                const auto flex_basis_max = item.flex_grow == 0.0f ? clamping_basis : std::nullopt;
                auto min_bound = maybe_max(style_min, flex_basis_min);
                if (!min_bound)
                    min_bound = flex_basis_min;
                const float min_main_size =
                    f32_max(min_bound.value_or(item.resolved_minimum_main_size),
                            item.resolved_minimum_main_size);
                auto max_bound = maybe_min(style_max, flex_basis_max);
                if (!max_bound)
                    max_bound = flex_basis_max;
                const float max_main_size =
                    max_bound.value_or(std::numeric_limits<float>::infinity());
                const float content_contribution = [&]() -> float {
                    if (style_preferred &&
                        (max_main_size <= min_main_size || max_main_size <= *style_preferred)) {
                        return f32_max(f32_min(*style_preferred, max_main_size), min_main_size) +
                               margin_sum;
                    }
                    if (max_main_size <= min_main_size)
                        return min_main_size + margin_sum;
                    if (item.is_scroll_container())
                        return item.flex_basis + margin_sum;
                    if (style_preferred) {
                        const float item_pb_main =
                            item.padding.main_axis_sum(dir) + item.border.main_axis_sum(dir);
                        const float inner_main_size = f32_max(*style_preferred, item_pb_main);
                        return maybe_clamp((constants.is_row
                                                ? inner_main_size
                                                : f32_max(inner_main_size, item.flex_basis)) +
                                               margin_sum,
                                           style_min, style_max);
                    }
                    const auto cross_axis_parent_size = constants.node_inner_size.cross(dir);
                    const auto cross_axis_available_space =
                        maybe_clamp(available_space.cross(dir).map_definite_value([&](float val) {
                            return f32_max(constants.divided_cross_space(
                                               cross_axis_parent_size.value_or(val)) -
                                               item.margin.cross_axis_sum(dir),
                                           0.0f);
                        }),
                                    item.min_size.cross(dir), item.max_size.cross(dir));
                    const auto child_available_space =
                        available_space.with_cross(dir, cross_axis_available_space);
                    auto child_known_dimensions = item.size.with_main(dir, std::nullopt);
                    const auto transferred_min_size =
                        item.min_size.maybe_apply_aspect_ratio(item.aspect_ratio);
                    const auto transferred_max_size =
                        item.max_size.maybe_apply_aspect_ratio(item.aspect_ratio);
                    child_known_dimensions.set_cross(dir,
                                                     maybe_clamp(child_known_dimensions.cross(dir),
                                                                 transferred_min_size.cross(dir),
                                                                 transferred_max_size.cross(dir)));
                    if (item.align_self == AlignSelf::stretch() &&
                        !child_known_dimensions.cross(dir)) {
                        child_known_dimensions.set_cross(dir,
                                                         cross_axis_available_space.into_option());
                    }
                    const float measured_main_size = tree.measure_child_size(
                        item.node, child_known_dimensions, constants.node_inner_size,
                        child_available_space, SizingMode::ContentSize, main_axis(dir),
                        Line<bool>::FALSE);
                    std::optional<float> transferred_main_size;
                    if (item.aspect_ratio && child_known_dimensions.cross(dir)) {
                        const float cross = *child_known_dimensions.cross(dir);
                        transferred_main_size = constants.is_row ? cross * *item.aspect_ratio
                                                                 : cross / *item.aspect_ratio;
                    }
                    const float inner_main_size =
                        maybe_max(measured_main_size, transferred_main_size);
                    return maybe_clamp((constants.is_row
                                            ? inner_main_size
                                            : f32_max(inner_main_size, item.flex_basis)) +
                                           margin_sum,
                                       style_min, style_max);
                }();
                const float diff = content_contribution - margin_sum - item.flex_basis;
                if (diff > 0.0f)
                    item.content_flex_fraction = diff / f32_max(1.0f, item.flex_grow);
                else if (diff < 0.0f) {
                    const float scaled_shrink_factor =
                        f32_max(1.0f, item.flex_shrink) * item.inner_flex_basis;
                    item.content_flex_fraction = diff / scaled_shrink_factor;
                } else
                    item.content_flex_fraction = 0.0f;
            }
            const bool floor_by_largest_item = constants.is_wrap && constants.is_row;
            float largest_item_size = 0.0f;
            float item_main_size_sum = -0.0f;
            for (const auto& item : line.items) {
                const float fraction = item.content_flex_fraction;
                float flex_contribution = 0.0f;
                if (fraction > 0.0f)
                    flex_contribution = f32_max(1.0f, item.flex_grow) * fraction;
                else if (fraction < 0.0f) {
                    const float scaled_shrink_factor =
                        f32_max(1.0f, item.flex_shrink) * item.inner_flex_basis;
                    flex_contribution =
                        scaled_shrink_factor == 0.0f ? 0.0f : scaled_shrink_factor * fraction;
                }
                const float size =
                    item.flex_basis + flex_contribution + item.margin.main_axis_sum(dir);
                if (floor_by_largest_item)
                    largest_item_size = f32_max(largest_item_size, size);
                item_main_size_sum += size;
            }
            const float gap_sum = sum_axis_gaps(constants.gap.main(dir), line.items.size());
            main_size =
                f32_max(main_size, f32_max(largest_item_size, item_main_size_sum + gap_sum));
        }
        return main_size + main_content_box_inset;
    }();
    const float outer_main_size =
        f32_max(maybe_clamp(outer_main_size_unclamped, constants.min_size.main(dir),
                            constants.max_size.main(dir)),
                main_content_box_inset - constants.scrollbar_gutter.main(dir));
    const float inner_main_size = f32_max(outer_main_size - main_content_box_inset, 0.0f);
    constants.container_size.set_main(dir, outer_main_size);
    constants.inner_container_size.set_main(dir, inner_main_size);
    constants.node_inner_size.set_main(dir, inner_main_size);
}

void resolve_flexible_lengths(FlexLine& line, const AlgoConstants& constants) {
    const auto dir = constants.dir;
    const float total_main_axis_gap = sum_axis_gaps(constants.gap.main(dir), line.items.size());
    float total_hypothetical_outer_main_size = -0.0f;
    for (const auto& child : line.items)
        total_hypothetical_outer_main_size += child.hypothetical_outer_size.main(dir);
    const float used_flex_factor = total_main_axis_gap + total_hypothetical_outer_main_size;
    const bool growing = used_flex_factor < constants.node_inner_size.main(dir).value_or(0.0f);
    const bool shrinking = used_flex_factor > constants.node_inner_size.main(dir).value_or(0.0f);
    const bool exactly_sized = !growing & !shrinking;
    for (auto& child : line.items) {
        const float inner_target_size = child.hypothetical_inner_size.main(dir);
        child.target_size.set_main(dir, inner_target_size);
        if (exactly_sized || (child.flex_grow == 0.0f && child.flex_shrink == 0.0f) ||
            (growing && child.flex_basis > child.hypothetical_inner_size.main(dir)) ||
            (shrinking && child.flex_basis < child.hypothetical_inner_size.main(dir))) {
            child.frozen = true;
            child.outer_target_size.set_main(dir,
                                             inner_target_size + child.margin.main_axis_sum(dir));
        }
    }
    if (exactly_sized)
        return;
    float item_space = -0.0f;
    for (const auto& child : line.items)
        item_space += child.frozen ? child.outer_target_size.main(dir)
                                   : child.flex_basis + child.margin.main_axis_sum(dir);
    const float used_space_initial = total_main_axis_gap + item_space;
    const float initial_free_space =
        maybe_sub(constants.node_inner_size.main(dir), used_space_initial).value_or(0.0f);
    for (;;) {
        bool all_frozen = true;
        float items_space = 0.0f, sum_flex_grow = 0.0f, sum_flex_shrink = 0.0f;
        for (const auto& child : line.items) {
            if (child.frozen)
                items_space += child.outer_target_size.main(dir);
            else {
                all_frozen = false;
                items_space += child.flex_basis + child.margin.main_axis_sum(dir);
                sum_flex_grow += child.flex_grow;
                sum_flex_shrink += child.flex_shrink;
            }
        }
        if (all_frozen)
            break;
        const float used_space = total_main_axis_gap + items_space;
        const float free_space = [&] {
            if (growing && sum_flex_grow < 1.0f)
                return maybe_min(initial_free_space * sum_flex_grow,
                                 maybe_sub(constants.node_inner_size.main(dir), used_space));
            if (shrinking && sum_flex_shrink < 1.0f)
                return maybe_max(initial_free_space * sum_flex_shrink,
                                 maybe_sub(constants.node_inner_size.main(dir), used_space));
            return maybe_sub(constants.node_inner_size.main(dir), used_space)
                .value_or(used_flex_factor - used_space);
        }();
        if (std::isnormal(free_space)) {
            if (growing && sum_flex_grow > 0.0f) {
                for (auto& child : line.items)
                    if (!child.frozen) {
                        child.target_size.set_main(
                            dir, child.flex_basis + free_space * (child.flex_grow / sum_flex_grow));
                    }
            } else if (shrinking && sum_flex_shrink > 0.0f) {
                float sum_scaled_shrink_factor = -0.0f;
                for (const auto& child : line.items)
                    if (!child.frozen)
                        sum_scaled_shrink_factor += child.inner_flex_basis * child.flex_shrink;
                if (sum_scaled_shrink_factor > 0.0f) {
                    for (auto& child : line.items)
                        if (!child.frozen) {
                            const float scaled_shrink_factor =
                                child.inner_flex_basis * child.flex_shrink;
                            child.target_size.set_main(
                                dir, child.flex_basis + free_space * (scaled_shrink_factor /
                                                                      sum_scaled_shrink_factor));
                        }
                }
            }
        }
        float total_violation = 0.0f;
        for (auto& child : line.items)
            if (!child.frozen) {
                const float clamped =
                    f32_max(maybe_clamp(child.target_size.main(dir),
                                        std::optional<float>{child.resolved_minimum_main_size},
                                        child.max_size.main(dir)),
                            0.0f);
                child.violation = clamped - child.target_size.main(dir);
                child.target_size.set_main(dir, clamped);
                child.outer_target_size.set_main(dir, child.target_size.main(dir) +
                                                          child.margin.main_axis_sum(dir));
                total_violation += child.violation;
            }
        for (auto& child : line.items)
            if (!child.frozen) {
                child.frozen = total_violation > 0.0f   ? child.violation > 0.0f
                               : total_violation < 0.0f ? child.violation < 0.0f
                                                        : true;
            }
    }
}

} // namespace taffy::flex_detail
