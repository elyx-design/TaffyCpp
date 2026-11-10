// Native all-feature port of pinned Taffy compute/block.rs (0064bd54).
#include "block.hpp"
#include "compute_common.hpp"
#include "sizing.hpp"
#include <taffy/compute.hpp>

#include <algorithm>
#include <limits>

namespace taffy {

float compute_block_align_content_offset(AlignContent alignment, float free_space) {
    const auto keyword = apply_alignment_fallback(free_space, 1, alignment);
    return compute_alignment_offset(free_space, 1, 0.0f, keyword, false, true);
}

namespace {
using OptionalSize = Size<std::optional<float>>;
constexpr float negative_infinity = -std::numeric_limits<float>::infinity();

bool establishes_bfc(const Style& style) {
    return is_scroll_container(style.overflow.x) || is_scroll_container(style.overflow.y) ||
           style.align_content.keyword != AlignContentKeyword::Normal ||
           style.contain.establishes_independent_formatting_context();
}

struct BlockItem {
    NodeId node_id{};
    std::uint32_t order{};
    bool is_table{}, is_compressible_replaced{}, is_in_same_bfc{};
    Float float_{};
    Clear clear{};
    Size<Dimension> size_style{};
    OptionalSize size{}, min_size{}, max_size{};
    Point<Overflow> overflow{};
    Contain contain{};
    float scrollbar_width{};
    Position position{};
    Direction direction{};
    std::optional<AlignSelf> align_self{}, justify_self{};
    Rect<LengthPercentageAuto> inset{}, margin{};
    Rect<float> padding{}, border{};
    Size<float> padding_border_sum{}, computed_size{};
    Point<float> static_position{};
    bool can_be_collapsed_through{};
    std::optional<Layout> final_layout{};
    OofCandidates oof_candidates{};
};

std::vector<BlockItem> generate_item_list(LayoutTree& tree, NodeId node, OptionalSize inner_size) {
    const CalcResolver calc = [&](Calc value, float basis) {
        return tree.resolve_calc(value, basis);
    };
    std::vector<BlockItem> items;
    for (auto child : tree.child_ids(node)) {
        const auto style = tree.block_child_style(child);
        if (style.box_generation_mode() == BoxGenerationMode::None)
            continue;
        BlockItem item;
        item.node_id = child;
        // Upstream enumerates after filtering hidden boxes.
        item.order = static_cast<std::uint32_t>(items.size());
        item.padding = style.padding.resolve_or_zero(inner_size.width, calc);
        item.border = style.border.resolve_or_zero(inner_size.width, calc);
        item.padding_border_sum = (item.padding + item.border).sum_axes();
        const auto adjustment =
            style.box_sizing == BoxSizing::ContentBox ? item.padding_border_sum : Size<float>{};
        item.is_table = style.is_table();
        item.is_compressible_replaced = style.is_compressible_replaced();
        item.is_in_same_bfc = style.is_block() && !item.is_table &&
                              !is_out_of_flow(style.position) && style.float_ == Float::None &&
                              !establishes_bfc(style);
        item.float_ = style.float_;
        item.clear = style.clear;
        item.size_style = style.size;
        item.size = style.size.maybe_resolve(inner_size, calc)
                        .maybe_apply_aspect_ratio(style.aspect_ratio)
                        .maybe_add(adjustment);
        item.min_size = style.min_size.maybe_resolve(inner_size, calc)
                            .maybe_apply_aspect_ratio(style.aspect_ratio)
                            .maybe_add(adjustment);
        item.max_size = style.max_size.maybe_resolve(inner_size, calc)
                            .maybe_apply_aspect_ratio(style.aspect_ratio)
                            .maybe_add(adjustment);
        item.overflow = style.overflow;
        item.contain = style.contain;
        item.scrollbar_width = style.scrollbar_width;
        item.position = style.position;
        item.direction = style.direction;
        item.align_self = style.align_self;
        item.justify_self = style.justify_self;
        item.inset = style.inset;
        item.margin = style.margin;
        items.push_back(std::move(item));
    }
    return items;
}

std::optional<float> resolve_stretch_height(Dimension style, std::optional<float> inner_height,
                                            Rect<float> margin,
                                            Line<bool> margins_treated_as_zero) {
    if (!style.is_stretch() || !inner_height)
        return std::nullopt;
    float height = *inner_height;
    if (!margins_treated_as_zero.start)
        height -= margin.top;
    if (!margins_treated_as_zero.end)
        height -= margin.bottom;
    return height;
}

Point<float> resolve_relative_offset(const BlockItem& item, OptionalSize parent_size,
                                     Direction direction, const CalcResolver& calc) {
    if (item.position != Position::Relative)
        return {};
    const auto inset = item.inset.maybe_resolve(parent_size, calc);
    const auto negative = [](std::optional<float> value) -> std::optional<float> {
        return value ? std::optional<float>(-*value) : std::nullopt;
    };
    const auto x = is_rtl(direction) ? (inset.right ? negative(inset.right) : inset.left)
                                     : (inset.left ? inset.left : negative(inset.right));
    const auto y = inset.top ? inset.top : negative(inset.bottom);
    return {x.value_or(0.0f), y.value_or(0.0f)};
}

float determine_content_based_container_width(LayoutTree& tree, const std::vector<BlockItem>& items,
                                              AvailableSpace available_width) {
    const CalcResolver calc = [&](Calc value, float basis) {
        return tree.resolve_calc(value, basis);
    };
    float max_child_width = 0.0f;
    FloatIntrinsicWidthCalculator float_contribution(available_width);
    for (const auto& item : items) {
        if (is_out_of_flow(item.position))
            continue;
        const auto known = item.size.maybe_clamp(item.min_size, item.max_size);
        const float margin_sum =
            item.margin.resolve_or_zero(available_width.into_option(), calc).horizontal_axis_sum();
        float width;
        if (known.width)
            width = *known.width;
        else {
            const auto keyword =
                resolve_sizing_keyword(item.size_style.width, std::nullopt, std::nullopt, calc);
            const auto item_available_width =
                keyword ? (keyword->kind == SizingKeywordResolution::Kind::Measure
                               ? keyword->available_space
                               : AvailableSpace::definite(keyword->exact))
                        : maybe_sub(available_width, margin_sum);
            width = tree.measure_child_size(item.node_id, known, {},
                                            {item_available_width, AvailableSpace::min_content()},
                                            SizingMode::InherentSize, AbsoluteAxis::Horizontal,
                                            Line<bool>::splat(item.is_in_same_bfc));
        }
        width = f32_max(width, item.padding_border_sum.width) + margin_sum;
        if (auto direction = float_direction(item.float_))
            float_contribution.add_float(width, *direction, item.clear);
        else
            max_child_width = f32_max(max_child_width, width);
    }
    return f32_max(max_child_width, float_contribution.result());
}

// Geometry that remains fixed throughout the in-flow pass.
struct FlowGeometry {
    float outer_width{};
    std::optional<float> percentage_height{};
    Rect<float> content_inset{}, resolved_inset{}, border{};
    TextAlign text_align{};
    AlignItems justify_items{};
    Direction direction{};
    Line<bool> collapse_with_children{}, children_adjoin_edges{};
    bool scroll_container{};
};

struct FlowResult {
    Rect<float> overflow{};
    float height{};
    CollapsibleMarginSet first_margin{}, last_margin{};
    std::optional<float> baseline{};
};

Rect<float> overflow_contribution(const BlockItem& item, const Layout& layout,
                                  const FlowGeometry& geometry) {
    const Point<float> location{
        is_rtl(geometry.direction)
            ? geometry.outer_width - (layout.location.x + layout.size.width) - geometry.border.right
            : layout.location.x - geometry.border.left,
        layout.location.y - geometry.border.top};
    return compute_scrollable_overflow_contribution(location, layout.size,
                                                    layout.scrollable_overflow_rect, item.overflow,
                                                    item.contain, geometry.scroll_container);
}

void defer_layout(BlockItem& item, const LayoutOutput& output, Size<float> scrollbar_size,
                  Point<float> location, Rect<float> margin, const FlowGeometry& geometry,
                  FlowResult& result) {
    item.final_layout = Layout{
        .order = item.order,
        .location = location,
        .size = output.size,
        .scrollable_overflow_rect = output.scrollable_overflow_rect,
        .scrollbar_size = scrollbar_size,
        .border = item.border,
        .padding = item.padding,
        .margin = margin,
    };
    result.overflow =
        result.overflow.union_(overflow_contribution(item, *item.final_layout, geometry));
}

FlowResult perform_final_layout(LayoutTree& tree, RunMode run_mode, std::vector<BlockItem>& items,
                                const FlowGeometry& g, BlockContext& context) {
    const CalcResolver calc = [&](Calc value, float basis) {
        return tree.resolve_calc(value, basis);
    };
    const float inner_width = f32_max(g.outer_width - g.resolved_inset.horizontal_axis_sum(), 0.0f);
    const auto inner_height = maybe_sub(g.percentage_height, g.resolved_inset.vertical_axis_sum());
    const OptionalSize parent_size{inner_width, inner_height};
    if (context.is_bfc_root()) {
        context.set_width(g.outer_width);
        context.apply_content_box_inset({g.resolved_inset.left, g.resolved_inset.right});
    }
    if (!g.collapse_with_children.start)
        context.commit_strut();

    FlowResult result;
    float committed_y = g.resolved_inset.top;
    float absolute_y = g.resolved_inset.top;
    CollapsibleMarginSet active_margin;
    bool collapsing_with_first = true;
    bool active_margin_has_clearance = false;
    bool has_active_floats = context.has_active_floats(committed_y);

    for (auto& item : items) {
        if (is_out_of_flow(item.position)) {
            item.static_position = {is_rtl(g.direction) ? g.outer_width - g.resolved_inset.right
                                                        : g.resolved_inset.left,
                                    absolute_y};
            continue;
        }
        const auto item_margin = item.margin.maybe_resolve(inner_width, calc);
        const auto non_auto_margin = item_margin.map([](auto value) {
            return value.value_or(0.0f);
        });
        const float margin_x_sum = non_auto_margin.horizontal_axis_sum();
        const Size<float> scrollbar_size{
            item.overflow.y == Overflow::Scroll ? item.scrollbar_width : 0.0f,
            item.overflow.x == Overflow::Scroll ? item.scrollbar_width : 0.0f};

        if (const auto floated = float_direction(item.float_)) {
            has_active_floats = true;
            const float available_width = f32_max(inner_width - margin_x_sum, 0.0f);
            const auto keyword =
                resolve_sizing_keyword(item.size_style.width, available_width, inner_width, calc);
            std::optional<float> known_width;
            auto item_available_width = AvailableSpace::definite(available_width);
            if (keyword) {
                if (keyword->kind == SizingKeywordResolution::Kind::Measure)
                    item_available_width = keyword->available_space;
                else {
                    known_width = keyword->exact;
                    item_available_width = AvailableSpace::definite(keyword->exact);
                }
            }
            const auto known_height =
                resolve_stretch_height(item.size_style.height, inner_height, non_auto_margin, {});
            // Upstream deliberately performs full child layout for floats even during a size pass.
            auto output =
                tree.perform_child_layout(item.node_id, {known_width, known_height}, parent_size,
                                          {item_available_width, AvailableSpace::max_content()},
                                          SizingMode::InherentSize, {});
            item.oof_candidates = output.oof_candidates.take();
            const auto margin_box = output.size + non_auto_margin.sum_axes();
            const bool adjoins_strut = collapsing_with_first && g.collapse_with_children.start;
            const float minimum_y =
                adjoins_strut ? committed_y : committed_y + active_margin.resolve();
            auto location = context.place_floated_box(margin_box, minimum_y, *floated, item.clear,
                                                      adjoins_strut);
            location.y += non_auto_margin.top;
            location.x += non_auto_margin.left;
            const auto relative = resolve_relative_offset(item, parent_size, g.direction, calc);
            location.x += relative.x;
            location.y += relative.y;
            defer_layout(item, output, scrollbar_size, location, non_auto_margin, g, result);
            continue;
        }

        float y_margin_offset = 0.0f;
        bool avoids_floats = false;
        bool pushed_below_float = false;
        float stretch_width;
        Point<float> avoiding_position{};
        float avoiding_width = 0.0f;
        if (item.is_in_same_bfc)
            stretch_width = f32_max(inner_width - margin_x_sum, 0.0f);
        else {
            if (!collapsing_with_first || !g.collapse_with_children.start)
                y_margin_offset = active_margin.collapse_with_margin(non_auto_margin.top).resolve();
            const float min_y = committed_y + y_margin_offset;
            if (has_active_floats || context.has_active_floats(min_y)) {
                const std::array margins{non_auto_margin.left, non_auto_margin.right};
                const float min_auto_width = -margin_x_sum;
                std::optional<std::size_t> after;
                BfcSlot slot;
                for (;;) {
                    slot = context.find_bfc_slot(min_y, margins, g.direction, item.clear, after);
                    if (!slot.segment_id)
                        break;
                    const float width =
                        maybe_clamp(item.size.width.value_or(
                                        f32_max(f32_max(slot.stretch_width, min_auto_width), 0.0f)),
                                    item.min_size.width, item.max_size.width);
                    if (width <= slot.border_width + FIT_TOLERANCE)
                        break;
                    after = slot.segment_id;
                }
                pushed_below_float = slot.y > min_y;
                has_active_floats = slot.segment_id.has_value();
                avoids_floats = true;
                stretch_width = f32_max(f32_max(slot.stretch_width, min_auto_width), 0.0f);
                avoiding_position = {slot.x, slot.y};
                avoiding_width = slot.border_width;
            } else {
                stretch_width = f32_max(inner_width - margin_x_sum, 0.0f);
                avoiding_position = {g.resolved_inset.left, min_y};
                avoiding_width = inner_width;
            }
        }

        const auto justify = item.justify_self.value_or(g.justify_items)
                                 .resolve_self_relative(item.direction, g.direction, true);
        const bool stretch = justify.keyword == AlignItemsKeyword::Stretch;
        const std::optional<AlignSelf> non_stretch_justify =
            justify.is_stretch_or_normal() ? std::nullopt : std::optional<AlignSelf>(justify);
        OptionalSize known;
        if (!(item.is_table || item.is_compressible_replaced) || stretch) {
            const auto width_style = non_stretch_justify && item.size_style.width.is_auto()
                                         ? Dimension::fit_content()
                                         : item.size_style.width;
            const auto keyword =
                resolve_sizing_keyword(width_style, stretch_width, inner_width, calc);
            std::optional<float> keyword_width;
            if (keyword) {
                if (keyword->kind == SizingKeywordResolution::Kind::Exact)
                    keyword_width = keyword->exact;
                else
                    keyword_width = tree.measure_child_size(
                        item.node_id, {}, parent_size,
                        {keyword->available_space, AvailableSpace::max_content()},
                        SizingMode::InherentSize, AbsoluteAxis::Horizontal,
                        Line<bool>::splat(item.is_in_same_bfc));
            }
            const auto keyword_height = resolve_stretch_height(
                item.size_style.height, inner_height, non_auto_margin, g.children_adjoin_edges);
            known = item.size;
            known.width = maybe_clamp(item.size.width ? *item.size.width
                                                      : keyword_width.value_or(stretch_width),
                                      item.min_size.width, item.max_size.width);
            known.height = item.size.height ? item.size.height : keyword_height;
            known = known.maybe_clamp(item.min_size, item.max_size);
        }
        const LayoutInput child_input{
            .run_mode = run_mode,
            .sizing_mode = SizingMode::InherentSize,
            .axis = RequestedAxis::Both,
            .known_dimensions = known,
            .known_dimensions_are_definite = {true, true},
            .parent_size = parent_size,
            .available_space = {AvailableSpace::definite(stretch_width),
                                AvailableSpace::max_content()},
            .vertical_margins_are_collapsible = Line<bool>::splat(item.is_in_same_bfc),
        };
        const auto clear_threshold = context.cleared_threshold(item.clear);
        const float clear_pos = clear_threshold.value_or(negative_infinity);
        float child_float_contribution = negative_infinity;
        LayoutOutput output;
        if (item.is_in_same_bfc) {
            const float width =
                f32_max(known.width.value_or(stretch_width), item.padding_border_sum.width);
            const float inset_left = non_auto_margin.left + g.content_inset.left;
            const float inset_right = g.outer_width - width - inset_left;
            const float estimated_y =
                committed_y + active_margin.collapse_with_margin(non_auto_margin.top).resolve();
            auto child_context =
                context.sub_context(f32_max(estimated_y, clear_pos), {inset_left, inset_right});
            output = tree.compute_block_child_layout(item.node_id, child_input, &child_context);
            child_float_contribution = child_context.floated_content_height_contribution();
            context.merge_adjoining_floats(child_context.top_adjoining_floats());
        } else
            output = tree.compute_child_layout(item.node_id, child_input);
        item.oof_candidates = output.oof_candidates.take();
        const auto final_size = output.size;
        const auto top_margin =
            output.top_margin.collapse_with_margin(item_margin.top.value_or(0.0f));
        const auto bottom_margin =
            output.bottom_margin.collapse_with_margin(item_margin.bottom.value_or(0.0f));

        const float free_x = f32_max(0.0f, stretch_width - final_size.width);
        const unsigned auto_margin_count =
            static_cast<unsigned>(!item_margin.left) + static_cast<unsigned>(!item_margin.right);
        const float auto_margin =
            auto_margin_count > 0 ? free_x / static_cast<float>(auto_margin_count) : 0.0f;
        Rect<float> resolved_margin{
            item_margin.left.value_or(auto_margin), item_margin.right.value_or(auto_margin),
            item_margin.top.value_or(0.0f), item_margin.bottom.value_or(0.0f)};
        const auto relative = resolve_relative_offset(item, parent_size, g.direction, calc);
        if (item.is_in_same_bfc && (!collapsing_with_first || !g.collapse_with_children.start))
            y_margin_offset = active_margin.collapse_with_set(top_margin).resolve();

        bool has_clearance = false;
        if (item.is_in_same_bfc && clear_threshold) {
            const float hypothetical_y =
                committed_y + active_margin.collapse_with_set(top_margin).resolve();
            if (context.has_adjoining_float(item.clear) || hypothetical_y < *clear_threshold) {
                has_clearance = true;
                const float escaped_margin = collapsing_with_first && g.collapse_with_children.start
                                                 ? active_margin.resolve()
                                                 : 0.0f;
                y_margin_offset = *clear_threshold - committed_y - escaped_margin;
            }
        }
        item.computed_size = output.size;
        item.can_be_collapsed_through = output.margins_can_collapse_through && !has_clearance;
        if (item.is_in_same_bfc) {
            item.static_position = {is_rtl(g.direction)
                                        ? g.outer_width - g.resolved_inset.right - final_size.width
                                        : g.resolved_inset.left,
                                    f32_max(committed_y + active_margin.resolve(), clear_pos)};
        } else {
            item.static_position = {is_rtl(g.direction)
                                        ? avoiding_position.x + avoiding_width - final_size.width
                                        : avoiding_position.x,
                                    avoiding_position.y};
        }
        Point<float> location;
        if (item.is_in_same_bfc) {
            location = {is_rtl(g.direction)
                            ? g.outer_width - g.resolved_inset.right - final_size.width -
                                  resolved_margin.right + relative.x
                            : g.resolved_inset.left + relative.x + resolved_margin.left,
                        committed_y + y_margin_offset + relative.y};
        } else {
            const float extra_left =
                avoids_floats ? resolved_margin.left - non_auto_margin.left : resolved_margin.left;
            const float extra_right = avoids_floats ? resolved_margin.right - non_auto_margin.right
                                                    : resolved_margin.right;
            location = {is_rtl(g.direction) ? avoiding_position.x + avoiding_width -
                                                  final_size.width - extra_right + relative.x
                                            : avoiding_position.x + extra_left + relative.x,
                        avoiding_position.y + relative.y};
        }
        if (item.is_in_same_bfc)
            context.add_child_floated_content_height_contribution(location.y - relative.y +
                                                                  child_float_contribution);

        // Auto margins win over self-alignment; self-alignment wins over legacy text-align.
        const bool has_auto_margin = !item_margin.left || !item_margin.right;
        const float outer_width = output.size.width + resolved_margin.horizontal_axis_sum();
        if (non_stretch_justify && !has_auto_margin) {
            const float container_start =
                avoids_floats ? avoiding_position.x - resolved_margin.left : g.resolved_inset.left;
            const float alignment_free_space =
                avoids_floats ? avoiding_width - output.size.width : inner_width - outer_width;
            const auto keyword =
                resolve_self_alignment_safety(*non_stretch_justify, alignment_free_space < 0.0f);
            float offset = 0.0f;
            if (keyword == AlignItemsKeyword::Center)
                offset = alignment_free_space / 2.0f;
            else {
                const bool end = keyword == AlignItemsKeyword::End ||
                                 keyword == AlignItemsKeyword::FlexEnd ||
                                 keyword == AlignItemsKeyword::SelfEnd;
                if (end != is_rtl(g.direction))
                    offset = alignment_free_space;
            }
            location.x = container_start + resolved_margin.left + relative.x + offset;
        } else if (!non_stretch_justify && outer_width < inner_width) {
            const float alignment_free_space = inner_width - outer_width;
            switch (g.text_align) {
            case TextAlign::Auto:
                break;
            case TextAlign::LegacyLeft:
                if (is_rtl(g.direction))
                    location.x -= alignment_free_space;
                break;
            case TextAlign::LegacyRight:
                if (!is_rtl(g.direction))
                    location.x += alignment_free_space;
                break;
            case TextAlign::LegacyCenter:
                location.x += is_rtl(g.direction) ? -alignment_free_space / 2.0f
                                                  : alignment_free_space / 2.0f;
                break;
            }
        }
        if (!result.baseline) {
            const auto baseline =
                child_first_baseline(output.baselines.first, output.size.height, item.overflow.y);
            if (baseline)
                result.baseline = location.y - relative.y + *baseline;
        }
        // Used auto margins additionally include the float-avoidance offsets.
        if (avoids_floats) {
            if (!item_margin.left)
                resolved_margin.left += avoiding_position.x - g.resolved_inset.left;
            if (!item_margin.right)
                resolved_margin.right +=
                    g.outer_width - g.resolved_inset.right - avoiding_position.x - avoiding_width;
        }
        defer_layout(item, output, scrollbar_size, location, resolved_margin, g, result);

        if (collapsing_with_first && pushed_below_float)
            collapsing_with_first = false;
        if (collapsing_with_first && has_clearance)
            collapsing_with_first = false;
        else if (collapsing_with_first) {
            if (item.can_be_collapsed_through)
                result.first_margin = result.first_margin.collapse_with_set(top_margin)
                                          .collapse_with_set(bottom_margin);
            else {
                result.first_margin = result.first_margin.collapse_with_set(top_margin);
                collapsing_with_first = false;
            }
        }
        if (item.can_be_collapsed_through) {
            active_margin =
                active_margin.collapse_with_set(top_margin).collapse_with_set(bottom_margin);
            absolute_y = committed_y + output.size.height + y_margin_offset;
        } else {
            committed_y = location.y - relative.y + output.size.height;
            if (has_clearance && output.margins_can_collapse_through) {
                committed_y -= top_margin.resolve();
                active_margin = top_margin.collapse_with_set(bottom_margin);
                active_margin_has_clearance = true;
            } else {
                active_margin = bottom_margin;
                active_margin_has_clearance = false;
            }
            absolute_y = committed_y + active_margin.resolve();
            context.commit_strut();
        }
    }
    result.last_margin = active_margin_has_clearance ? CollapsibleMarginSet{} : active_margin;
    const float bottom_margin_offset = active_margin_has_clearance ? active_margin.resolve()
                                       : g.collapse_with_children.end
                                           ? 0.0f
                                           : result.last_margin.resolve();
    committed_y += g.resolved_inset.bottom + bottom_margin_offset;
    result.height = f32_max(0.0f, committed_y);
    return result;
}

LayoutOutput compute_inner(LayoutTree& tree, NodeId node, const LayoutInput& input,
                           BlockContext& context) {
    const auto style = tree.block_container_style(node);
    const CalcResolver calc = [&](Calc value, float basis) {
        return tree.resolve_calc(value, basis);
    };
    const auto padding = style.padding.resolve_or_zero(input.parent_size.width, calc);
    const auto border = style.border.resolve_or_zero(input.parent_size.width, calc);
    const bool rtl = is_rtl(style.direction);
    const Size<float> scrollbar{style.overflow.y == Overflow::Scroll ? style.scrollbar_width : 0.0f,
                                style.overflow.x == Overflow::Scroll ? style.scrollbar_width
                                                                     : 0.0f};
    const Rect<float> gutter{rtl ? scrollbar.width : 0.0f, rtl ? 0.0f : scrollbar.width, 0.0f,
                             scrollbar.height};
    const auto padding_border_size = (padding + border).sum_axes();
    const auto content_inset = padding + border + gutter;
    context.apply_content_box_inset({content_inset.left, content_inset.right});
    const auto adjustment =
        style.box_sizing == BoxSizing::ContentBox ? padding_border_size : Size<float>{};
    const auto size = style.size.maybe_resolve(input.parent_size, calc)
                          .maybe_apply_aspect_ratio(style.aspect_ratio)
                          .maybe_add(adjustment);
    const auto min_size = style.min_size.maybe_resolve(input.parent_size, calc)
                              .maybe_apply_aspect_ratio(style.aspect_ratio)
                              .maybe_add(adjustment);
    const auto max_size = style.max_size.maybe_resolve(input.parent_size, calc)
                              .maybe_apply_aspect_ratio(style.aspect_ratio)
                              .maybe_add(adjustment);
    // Adopt only newly derived aspect-ratio axes: incoming dimensions have already
    // been resolved by the parent, possibly with padding/border overrides.
    const auto derived = input.known_dimensions.maybe_apply_aspect_ratio(style.aspect_ratio)
                             .maybe_clamp(min_size, max_size);
    const auto known = input.known_dimensions.or_(derived);
    const OptionalSize percentage_basis{
        known.width, input.known_dimensions_are_definite.height ? known.height : std::nullopt};
    const auto initial_content_size = percentage_basis.maybe_sub(content_inset.sum_axes());
    const bool new_bfc = establishes_bfc(style);
    const bool scroll_container =
        is_scroll_container(style.overflow.x) || is_scroll_container(style.overflow.y);
    const Line<bool> children_adjoin_edges{
        input.vertical_margins_are_collapsible.start && !new_bfc &&
            !is_out_of_flow(style.position) && padding.top == 0.0f && border.top == 0.0f,
        input.vertical_margins_are_collapsible.end && !new_bfc && !is_out_of_flow(style.position) &&
            padding.bottom == 0.0f && border.bottom == 0.0f};
    const Line<bool> collapse_with_children{children_adjoin_edges.start,
                                            children_adjoin_edges.end && !size.height};
    const bool prevents_collapse_through =
        !style.is_block() || context.is_bfc_root() || new_bfc || is_out_of_flow(style.position) ||
        padding.top > 0.0f || padding.bottom > 0.0f || border.top > 0.0f || border.bottom > 0.0f;
    auto items = generate_item_list(tree, node, initial_content_size);
    float outer_width;
    if (known.width)
        outer_width = *known.width;
    else {
        const auto available_width =
            maybe_sub(input.available_space.width, content_inset.horizontal_axis_sum());
        const float intrinsic_width =
            determine_content_based_container_width(tree, items, available_width) +
            content_inset.horizontal_axis_sum();
        outer_width = f32_max(maybe_clamp(intrinsic_width, min_size.width, max_size.width),
                              padding_border_size.width);
    }
    if (input.run_mode == RunMode::ComputeSize) {
        if (known.height)
            return LayoutOutput::from_outer_size({outer_width, *known.height});
        if (input.axis == RequestedAxis::Horizontal)
            return LayoutOutput::from_outer_size({outer_width, 0.0f});
    }
    const auto percentage_height = percentage_basis.height ? percentage_basis.height
                                   : input.sizing_mode == SizingMode::InherentSize
                                       ? maybe_max(size.height, min_size.height)
                                       : std::nullopt;
    const float percentage_width = input.parent_size.width.value_or(outer_width);
    const auto resolved_padding = style.padding.resolve_or_zero(percentage_width, calc);
    const auto resolved_border = style.border.resolve_or_zero(percentage_width, calc);
    const auto resolved_inset = resolved_padding + resolved_border + gutter;
    const FlowGeometry geometry{outer_width,           percentage_height, content_inset,
                                resolved_inset,        resolved_border,   style.text_align,
                                style.justify_items,   style.direction,   collapse_with_children,
                                children_adjoin_edges, scroll_container};
    auto flow = perform_final_layout(tree, input.run_mode, items, geometry, context);
    if (context.is_bfc_root() || new_bfc)
        flow.height = f32_max(flow.height, context.floated_content_height_contribution());
    const float outer_height =
        f32_max(known.height.value_or(maybe_clamp(flow.height, min_size.height, max_size.height)),
                padding_border_size.height);
    const Size<float> final_size{outer_width, outer_height};
    const bool min_height_constrained =
        min_size.height && *min_size.height > 0.0f && *min_size.height >= outer_height;
    const bool bottom_collapses = collapse_with_children.end && !min_height_constrained;

    if (style.align_content.keyword != AlignContentKeyword::Normal) {
        const float inner_height = outer_height - resolved_inset.vertical_axis_sum();
        const float inflow_height = flow.height - resolved_inset.vertical_axis_sum();
        const float offset =
            compute_block_align_content_offset(style.align_content, inner_height - inflow_height);
        if (flow.baseline)
            *flow.baseline += offset;
        bool any_in_flow = false;
        for (auto& item : items) {
            if (item.final_layout) {
                item.final_layout->location.y += offset;
                any_in_flow = true;
            }
            if (is_out_of_flow(item.position))
                item.static_position.y += offset;
        }
        if (any_in_flow) {
            flow.overflow = {};
            for (const auto& item : items)
                if (item.final_layout)
                    flow.overflow = flow.overflow.union_(
                        overflow_contribution(item, *item.final_layout, geometry));
        }
    }
    const bool all_collapsible = std::ranges::all_of(items, [](const BlockItem& item) {
        return is_floated(item.float_) || is_out_of_flow(item.position) ||
               item.can_be_collapsed_through;
    });
    LayoutOutput output{
        .size = final_size,
        .scrollable_overflow_rect = {},
        .baselines = Baselines::from_first(flow.baseline),
        .top_margin = collapse_with_children.start
                          ? flow.first_margin
                          : CollapsibleMarginSet::from_margin(
                                style.margin.top.resolve_or_zero(input.parent_size.width, calc)),
        .bottom_margin =
            bottom_collapses
                ? flow.last_margin
                : CollapsibleMarginSet::from_margin(
                      style.margin.bottom.resolve_or_zero(input.parent_size.width, calc)),
        .margins_can_collapse_through =
            !prevents_collapse_through && outer_height == 0.0f && all_collapsible,
        .oof_candidates = {},
        .oof_positioning_area = std::nullopt,
    };
    // Margin metadata must survive size-only passes; the parent relies on it.
    if (input.run_mode == RunMode::ComputeSize)
        return output;
    for (const auto& item : items)
        if (item.final_layout)
            tree.set_unrounded_layout(item.node_id, *item.final_layout);
    for (auto& item : items) {
        if (is_out_of_flow(item.position)) {
            const Line<float> inline_area{resolved_inset.left, outer_width - resolved_inset.right};
            const Line<float> block_area{item.static_position.y, item.static_position.y};
            const auto justify = item.justify_self.value_or(style.justify_items)
                                     .resolve_self_relative(item.direction, style.direction, true);
            const auto align = item.align_self.value_or(AlignItems::normal())
                                   .resolve_self_relative(item.direction, style.direction, false);
            output.oof_candidates.push(
                {item.node_id,
                 item.order,
                 item.position,
                 {AxisStaticPosition::from_alignment(justify, inline_area, rtl),
                  AxisStaticPosition::from_alignment(align, block_area, false)}});
        } else if (!item.oof_candidates.empty()) {
            if (item.final_layout)
                item.oof_candidates.translate(item.final_layout->location);
            output.oof_candidates.append(item.oof_candidates);
        }
    }
    const auto absolute_inset = resolved_border + gutter;
    output.oof_positioning_area = OofPositioningArea{final_size - absolute_inset.sum_axes(),
                                                     {absolute_inset.left, absolute_inset.top}};
    if (scroll_container) {
        flow.overflow.right += rtl ? resolved_padding.left : resolved_padding.right;
        flow.overflow.bottom += resolved_padding.bottom;
    }
    output.scrollable_overflow_rect = flow.overflow;
    const auto children = tree.child_ids(node);
    for (std::size_t order = 0; order < children.size(); ++order) {
        const auto child = children[order];
        if (tree.block_child_style(child).box_generation_mode() == BoxGenerationMode::None) {
            tree.set_unrounded_layout(child, Layout::with_order(static_cast<std::uint32_t>(order)));
            tree.perform_child_layout(child, {}, {}, Size<AvailableSpace>::max_content(),
                                      SizingMode::InherentSize, {});
        }
    }
    return output;
}
} // namespace

LayoutOutput compute_block(LayoutTree& tree, NodeId node, const LayoutInput& input,
                           BlockContext* context) {
    const auto style = tree.block_container_style(node);
    const CalcResolver calc = [&](Calc value, float basis) {
        return tree.resolve_calc(value, basis);
    };
    const auto padding = style.padding.resolve_or_zero(input.parent_size.width, calc);
    const auto border = style.border.resolve_or_zero(input.parent_size.width, calc);
    const auto padding_border = (padding + border).sum_axes();
    const auto adjustment =
        style.box_sizing == BoxSizing::ContentBox ? padding_border : Size<float>{};
    const auto min_size = style.min_size.maybe_resolve(input.parent_size, calc)
                              .maybe_apply_aspect_ratio(style.aspect_ratio)
                              .maybe_add(adjustment);
    const auto max_size = style.max_size.maybe_resolve(input.parent_size, calc)
                              .maybe_apply_aspect_ratio(style.aspect_ratio)
                              .maybe_add(adjustment);
    const auto style_size = input.sizing_mode == SizingMode::InherentSize
                                ? style.size.maybe_resolve(input.parent_size, calc)
                                      .maybe_apply_aspect_ratio(style.aspect_ratio)
                                      .maybe_add(adjustment)
                                      .maybe_clamp(min_size, max_size)
                                : OptionalSize{};
    const auto min_max_size =
        min_size.zip_map(max_size, [](auto min, auto max) -> std::optional<float> {
            return min && max && *max <= *min ? min : std::nullopt;
        });
    auto inner_input = input;
    inner_input.known_dimensions =
        input.known_dimensions.or_(min_max_size).or_(style_size).maybe_max(padding_border);
    const auto known = inner_input.known_dimensions;
    if (input.run_mode == RunMode::ComputeSize) {
        if (known.both_axis_defined())
            return LayoutOutput::from_outer_size({*known.width, *known.height});
        if (input.axis == RequestedAxis::Horizontal && known.width)
            return LayoutOutput::from_outer_size({*known.width, 0.0f});
    }
    LayoutOutput output;
    if (context && !establishes_bfc(style))
        output = compute_inner(tree, node, inner_input, *context);
    else {
        BlockFormattingContext bfc;
        auto root_context = bfc.root_block_context();
        output = compute_inner(tree, node, inner_input, root_context);
    }
    if (style.contain.suppresses_baseline())
        output.baselines = Baselines::NONE;
    return output;
}

} // namespace taffy
