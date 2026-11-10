#include "internal.hpp"
#include <algorithm>

namespace taffy::flex_detail {
AlignItems resolve_normal_alignment(AlignItems alignment) {
    return alignment.keyword == AlignItemsKeyword::Normal ? AlignItems::stretch() : alignment;
}

AlgoConstants compute_constants(LayoutTree& tree, const Style& borrowed_style,
                                Size<std::optional<float>> known, Size<bool> definite,
                                Size<std::optional<float>> parent, Size<AvailableSpace> available) {
    // Custom adapters can replace style storage from resolve_calc(). Snapshot only
    // the fields used here before the first callback (not unrelated grid vectors).
    Style style;
    style.flex_direction = borrowed_style.flex_direction;
    style.direction = borrowed_style.direction;
    style.flex_wrap = borrowed_style.flex_wrap;
    style.flex_line_count = borrowed_style.flex_line_count;
    style.padding = borrowed_style.padding;
    style.border = borrowed_style.border;
    style.box_sizing = borrowed_style.box_sizing;
    style.align_items = borrowed_style.align_items;
    style.align_content = borrowed_style.align_content;
    style.justify_content = borrowed_style.justify_content;
    style.overflow = borrowed_style.overflow;
    style.scrollbar_width = borrowed_style.scrollbar_width;
    style.gap = borrowed_style.gap;
    style.min_size = borrowed_style.min_size;
    style.max_size = borrowed_style.max_size;
    style.aspect_ratio = borrowed_style.aspect_ratio;
    const auto calc = [&](Calc value, float basis) {
        return tree.resolve_calc(value, basis);
    };
    AlgoConstants c;
    c.dir = style.flex_direction;
    c.layout_direction = style.direction;
    c.is_row = is_row(c.dir);
    c.is_column = is_column(c.dir);
    c.is_wrap = is_multi_line(style.flex_wrap);
    c.is_wrap_reverse = is_reverse(style.flex_wrap);
    c.is_balance = is_balance(style.flex_wrap);
    if (c.is_wrap)
        c.line_count = std::max<std::uint16_t>(style.flex_line_count, 1);
    const auto padding = style.padding.resolve_or_zero(parent.width, calc);
    c.border = style.border.resolve_or_zero(parent.width, calc);
    const auto adjustment = style.box_sizing == BoxSizing::ContentBox
                                ? padding.sum_axes() + c.border.sum_axes()
                                : Size<float>{};
    c.align_items = resolve_normal_alignment(style.align_items);
    c.align_content = style.align_content.keyword == AlignContentKeyword::Normal
                          ? AlignContent::stretch()
                          : style.align_content;
    c.justify_content = style.justify_content.keyword == AlignContentKeyword::Normal
                            ? JustifyContent::flex_start()
                            : style.justify_content;
    c.scrollbar_gutter = style.overflow.transpose().map([&](Overflow o) {
        return o == Overflow::Scroll ? style.scrollbar_width : 0.0f;
    });
    c.is_scroll_container = taffy::is_scroll_container(style.overflow.x) ||
                            taffy::is_scroll_container(style.overflow.y);
    c.content_box_inset = padding + c.border;
    c.content_box_inset.bottom += c.scrollbar_gutter.y;
    if (is_rtl(c.layout_direction))
        c.content_box_inset.left += c.scrollbar_gutter.x;
    else
        c.content_box_inset.right += c.scrollbar_gutter.x;
    c.node_outer_size = known;
    c.node_inner_size = known.maybe_sub(c.content_box_inset.sum_axes());
    c.known_main_size_is_definite = definite.main(c.dir);
    c.has_definite_main_size = c.known_main_size_is_definite && known.main(c.dir).has_value();
    c.has_definite_cross_size = definite.cross(c.dir) && known.cross(c.dir).has_value();
    c.cross_axis_available_space_is_definite =
        c.has_definite_cross_size || available.cross(c.dir).is_definite();
    c.gap = style.gap.resolve_or_zero(c.node_inner_size.or_(Size<std::optional<float>>{0.0f, 0.0f}),
                                      calc);
    c.min_size = style.min_size.maybe_resolve(parent, calc)
                     .maybe_apply_aspect_ratio(style.aspect_ratio)
                     .maybe_add(adjustment);
    c.max_size = style.max_size.maybe_resolve(parent, calc)
                     .maybe_apply_aspect_ratio(style.aspect_ratio)
                     .maybe_add(adjustment);
    return c;
}

Size<float> resolve_relative_inset(Rect<std::optional<float>> inset, const AlgoConstants& c) {
    const auto negate = [](std::optional<float> v) -> std::optional<float> {
        return v ? std::optional<float>{-*v} : std::nullopt;
    };
    const auto choose = [](std::optional<float> a, std::optional<float> b) {
        return a ? *a : b.value_or(0.0f);
    };
    const auto dir = c.dir;
    const auto main = c.is_row && is_rtl(c.layout_direction)
                          ? choose(inset.main_end(dir), negate(inset.main_start(dir)))
                          : choose(inset.main_start(dir), negate(inset.main_end(dir)));
    const auto cross = c.is_column && is_rtl(c.layout_direction)
                           ? choose(negate(inset.cross_end(dir)), inset.cross_start(dir))
                           : choose(inset.cross_start(dir), negate(inset.cross_end(dir)));
    return Size<float>{}.with_main(dir, main).with_cross(dir, cross);
}

std::vector<FlexItem> generate_anonymous_flex_items(LayoutTree& tree, NodeId node,
                                                    const AlgoConstants& c) {
    const auto percent_size = c.known_main_size_is_definite
                                  ? c.node_inner_size
                                  : c.node_inner_size.with_main(c.dir, std::nullopt);
    const auto children = tree.child_ids(node);
    std::vector<FlexItem> items;
    const auto calc = [&](Calc value, float basis) {
        return tree.resolve_calc(value, basis);
    };
    for (std::size_t index = 0; index < children.size(); ++index) {
        const auto child = children[index];
        const auto style = tree.flex_child_style(child);
        if (is_out_of_flow(style.position) ||
            style.box_generation_mode() == BoxGenerationMode::None)
            continue;
        FlexItem item;
        item.node = child;
        // Rust's `index as u32` truncates modulo 2^32, as does this unsigned conversion.
        item.order = static_cast<std::uint32_t>(index);
        item.aspect_ratio = style.aspect_ratio;
        item.padding = style.padding.resolve_or_zero(c.node_inner_size.width, calc);
        item.border = style.border.resolve_or_zero(c.node_inner_size.width, calc);
        const auto adjustment = item.box_sizing_adjustment(style);
        item.size = style.size.maybe_resolve(percent_size, calc)
                        .maybe_apply_aspect_ratio(item.aspect_ratio)
                        .maybe_add(adjustment);
        item.size_style = style.size;
        item.min_size = style.min_size.maybe_resolve(percent_size, calc).maybe_add(adjustment);
        item.max_size = style.max_size.maybe_resolve(percent_size, calc).maybe_add(adjustment);
        if (style.position == Position::Relative)
            item.relative_inset =
                resolve_relative_inset(style.inset.maybe_resolve(c.node_inner_size, calc), c);
        item.margin = style.margin.resolve_or_zero(c.node_inner_size.width, calc);
        item.margin_is_auto = style.margin.map([](const auto& value) {
            return value.is_auto();
        });
        item.align_self =
            resolve_normal_alignment(style.align_self.value_or(c.align_items))
                .resolve_self_relative(style.direction, c.layout_direction, c.is_column);
        item.overflow = style.overflow;
        item.contain = style.contain;
        item.scrollbar_width = style.scrollbar_width;
        item.flex_grow = style.flex_grow;
        item.flex_shrink = style.flex_shrink;
        if (items.capacity() == 0)
            items.reserve(children.size());
        items.push_back(std::move(item));
    }
    return items;
}

Size<AvailableSpace> determine_available_space(Size<std::optional<float>> known,
                                               Size<AvailableSpace> available,
                                               const AlgoConstants& c) {
    const auto axis_space = [](std::optional<float> size, AvailableSpace outer, float inset) {
        return size ? AvailableSpace::definite(f32_max(*size - inset, 0.0f))
                    : maybe_max(maybe_sub(outer, inset), 0.0f);
    };
    return {axis_space(known.width, available.width, c.content_box_inset.horizontal_axis_sum()),
            axis_space(known.height, available.height, c.content_box_inset.vertical_axis_sum())};
}
} // namespace taffy::flex_detail
