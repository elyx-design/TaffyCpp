#include <stdexcept>
#include <taffy/compute.hpp>
#include <taffy/layout.hpp>
#include <taffy/numeric.hpp>
#include <taffy/tree.hpp>

namespace taffy {

LayoutOutput compute_leaf(LayoutTree& tree, NodeId node, const LayoutInput& inputs) {
    const CalcResolver calc = [&tree](Calc value, float basis) {
        return tree.resolve_calc(value, basis);
    };
    return compute_leaf_layout(inputs, tree.core_container_style(node), calc,
                               [&tree, node](const LayoutInput& input) {
                                   return tree.measure(node, input).size;
                               });
}

LayoutOutput compute_leaf_layout(const LayoutInput& inputs, const Style& style,
                                 const CalcResolver& calc, const IntrinsicSizeFunction& measure) {
    // CSS resolves percentages on every padding/border edge against inline width.
    const auto resolve_edges = [&](const auto& edges) -> Rect<float> {
        return {edges.left.resolve_or_zero(inputs.parent_size.width, calc),
                edges.right.resolve_or_zero(inputs.parent_size.width, calc),
                edges.top.resolve_or_zero(inputs.parent_size.width, calc),
                edges.bottom.resolve_or_zero(inputs.parent_size.width, calc)};
    };
    const auto padding = resolve_edges(style.padding);
    const auto border = resolve_edges(style.border);
    const Rect<float> padding_border{padding.left + border.left, padding.right + border.right,
                                     padding.top + border.top, padding.bottom + border.bottom};
    const Size<float> pb_sum{padding_border.left + padding_border.right,
                             padding_border.top + padding_border.bottom};
    const Size<float> adjustment =
        style.box_sizing == BoxSizing::ContentBox ? pb_sum : Size<float>{};

    Size<std::optional<float>> node_size = inputs.known_dimensions;
    Size<std::optional<float>> node_min_size{};
    Size<std::optional<float>> node_max_size{};
    std::optional<float> aspect_ratio{};
    if (inputs.sizing_mode == SizingMode::InherentSize) {
        aspect_ratio = style.aspect_ratio;
        const auto resolve_size = [&](const auto& dimensions,
                                      bool apply_aspect_ratio) -> Size<std::optional<float>> {
            Size<std::optional<float>> size{
                dimensions.width.maybe_resolve(inputs.parent_size.width, calc),
                dimensions.height.maybe_resolve(inputs.parent_size.height, calc)};
            if (apply_aspect_ratio && aspect_ratio) {
                if (size.width && !size.height)
                    size.height = *size.width / *aspect_ratio;
                else if (!size.width && size.height)
                    size.width = *size.height * *aspect_ratio;
            }
            if (size.width)
                *size.width += adjustment.width;
            if (size.height)
                *size.height += adjustment.height;
            return size;
        };
        const auto style_size = resolve_size(style.size, true);
        node_min_size = resolve_size(style.min_size, true);
        node_max_size = resolve_size(style.max_size, false);
        if (!node_size.width)
            node_size.width = style_size.width;
        if (!node_size.height)
            node_size.height = style_size.height;
    }

    // Scrollbar gutters consume space in the opposite axis.
    auto content_box_inset = padding_border;
    content_box_inset.right += style.overflow.y == Overflow::Scroll ? style.scrollbar_width : 0;
    content_box_inset.bottom += style.overflow.x == Overflow::Scroll ? style.scrollbar_width : 0;
    const Size<float> inset_sum{content_box_inset.left + content_box_inset.right,
                                content_box_inset.top + content_box_inset.bottom};
    const bool scroll_container =
        is_scroll_container(style.overflow.x) || is_scroll_container(style.overflow.y);
    const bool prevents_collapse =
        !style.is_block() || scroll_container || is_out_of_flow(style.position) ||
        style.contain.establishes_independent_formatting_context() || padding.top > 0 ||
        padding.bottom > 0 || border.top > 0 || border.bottom > 0 ||
        (node_size.height && *node_size.height > 0) ||
        (node_min_size.height && *node_min_size.height > 0);

    if (inputs.run_mode == RunMode::ComputeSize && prevents_collapse && node_size.width &&
        node_size.height) {
        return LayoutOutput::from_outer_size(
            {f32_max(maybe_clamp(*node_size.width, node_min_size.width, node_max_size.width),
                     pb_sum.width),
             f32_max(maybe_clamp(*node_size.height, node_min_size.height, node_max_size.height),
                     pb_sum.height)});
    }
    const auto measurement_space = [](AvailableSpace available, std::optional<float> known,
                                      std::optional<float> preferred, std::optional<float> minimum,
                                      std::optional<float> maximum, float inset) {
        return available.maybe_set(known).maybe_set(preferred).map_definite_value([&](float value) {
            return maybe_clamp(value, minimum, maximum) - inset;
        });
    };
    const Size<AvailableSpace> available_space{
        measurement_space(inputs.available_space.width, inputs.known_dimensions.width,
                          node_size.width, node_min_size.width, node_max_size.width,
                          inset_sum.width),
        measurement_space(inputs.available_space.height, inputs.known_dimensions.height,
                          node_size.height, node_min_size.height, node_max_size.height,
                          inset_sum.height)};
    if (inputs.run_mode == RunMode::PerformHiddenLayout)
        throw std::logic_error("compute_leaf cannot perform hidden layout");
    LayoutInput measurement_input = inputs;
    measurement_input.known_dimensions = inputs.run_mode == RunMode::ComputeSize
                                             ? inputs.known_dimensions
                                             : Size<std::optional<float>>{};
    measurement_input.available_space = available_space;
    const auto measured_size = measure(measurement_input);
    const Size<float> clamped_size{
        maybe_clamp(node_size.width.value_or(measured_size.width + inset_sum.width),
                    node_min_size.width, node_max_size.width),
        maybe_clamp(node_size.height.value_or(measured_size.height + inset_sum.height),
                    node_min_size.height, node_max_size.height)};
    const Size<float> size{
        f32_max(clamped_size.width, pb_sum.width),
        f32_max(f32_max(clamped_size.height, aspect_ratio ? clamped_size.width / *aspect_ratio : 0),
                pb_sum.height)};
    const bool rtl = is_rtl(style.direction);
    const float start_padding = rtl ? padding.right : padding.left;
    const float end_padding = rtl ? padding.left : padding.right;
    LayoutOutput result = LayoutOutput::from_sizes(
        size, {0, start_padding + measured_size.width + (scroll_container ? end_padding : 0), 0,
               padding.top + measured_size.height + (scroll_container ? padding.bottom : 0)});
    result.margins_can_collapse_through =
        !prevents_collapse && size.height == 0 && measured_size.height == 0;
    return result;
}

} // namespace taffy
