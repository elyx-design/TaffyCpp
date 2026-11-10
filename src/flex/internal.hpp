#pragma once
#include "../compute_common.hpp"
#include "../flex.hpp"
#include <span>
#include <taffy/compute.hpp>

namespace taffy::flex_detail {
struct FlexItem {
    NodeId node{};
    std::uint32_t order = 0;
    Size<std::optional<float>> size{}, min_size{}, max_size{};
    Size<Dimension> size_style{};
    std::optional<float> aspect_ratio{};
    AlignSelf align_self{};
    Point<Overflow> overflow{};
    Contain contain{};
    float scrollbar_width = 0, flex_shrink = 0, flex_grow = 0;
    bool flex_basis_is_definite = false;
    float resolved_minimum_main_size = 0;
    Size<float> relative_inset{};
    Rect<float> margin{}, padding{}, border{};
    Rect<bool> margin_is_auto{};
    float flex_basis = 0, inner_flex_basis = 0, violation = 0;
    bool frozen = false;
    float content_flex_fraction = 0;
    Size<float> hypothetical_inner_size{}, hypothetical_outer_size{}, target_size{},
        outer_target_size{};
    float baseline = 0, offset_main = 0, offset_cross = 0;
    OofCandidates oof_candidates{};

    Size<float> box_sizing_adjustment(const Style& style) const {
        return style.box_sizing == BoxSizing::ContentBox ? (padding + border).sum_axes()
                                                         : Size<float>{};
    }

    bool is_scroll_container() const {
        return taffy::is_scroll_container(overflow.x) || taffy::is_scroll_container(overflow.y);
    }

    bool is_stretched(FlexDirection dir) const {
        const auto cross = size_style.cross(dir);
        return !margin_is_auto.cross_start(dir) && !margin_is_auto.cross_end(dir) &&
               (cross.is_stretch() || (align_self == AlignSelf::stretch() && cross.is_auto()));
    }

    bool participates_in_baseline_alignment(FlexDirection dir) const {
        return align_self == AlignSelf::baseline() && !margin_is_auto.cross_start(dir) &&
               !margin_is_auto.cross_end(dir);
    }
};

struct FlexLine {
    std::span<FlexItem> items{};
    float cross_size = 0, offset_cross = 0;
};

struct AlgoConstants {
    FlexDirection dir{};
    Direction layout_direction{};
    bool is_row = false, is_column = false, is_wrap = false, is_wrap_reverse = false,
         is_balance = false;
    std::optional<std::uint16_t> line_count{};
    Size<std::optional<float>> min_size{}, max_size{};
    Rect<float> border{}, content_box_inset{};
    Point<float> scrollbar_gutter{};
    bool is_scroll_container = false;
    Size<float> gap{};
    AlignItems align_items{};
    AlignContent align_content{};
    JustifyContent justify_content{};
    Size<std::optional<float>> node_outer_size{}, node_inner_size{};
    bool known_main_size_is_definite = false, has_definite_main_size = false,
         has_definite_cross_size = false;
    bool cross_axis_available_space_is_definite = false;
    Size<float> container_size{}, inner_container_size{};

    float divided_cross_space(float space) const {
        if (line_count && *line_count > 1) {
            const float count = *line_count;
            return (space - (count - 1.0f) * gap.cross(dir)) / count;
        }
        return space;
    }
};

AlignItems resolve_normal_alignment(AlignItems alignment);
AlgoConstants compute_constants(LayoutTree&, const Style&, Size<std::optional<float>>, Size<bool>,
                                Size<std::optional<float>>, Size<AvailableSpace>);
Size<float> resolve_relative_inset(Rect<std::optional<float>>, const AlgoConstants&);
std::vector<FlexItem> generate_anonymous_flex_items(LayoutTree&, NodeId, const AlgoConstants&);
Size<AvailableSpace> determine_available_space(Size<std::optional<float>>, Size<AvailableSpace>,
                                               const AlgoConstants&);
void determine_flex_base_size(LayoutTree&, const AlgoConstants&, Size<AvailableSpace>,
                              std::vector<FlexItem>&);
std::vector<FlexLine> collect_flex_lines(const AlgoConstants&, Size<AvailableSpace>,
                                         std::vector<FlexItem>&);
std::vector<FlexLine> collect_balanced_flex_lines(const AlgoConstants&, Size<AvailableSpace>,
                                                  std::vector<FlexItem>&);
Size<bool> item_known_dimension_definiteness(const AlgoConstants&, const FlexItem&);
void determine_container_main_size(LayoutTree&, Size<AvailableSpace>, std::vector<FlexLine>&,
                                   AlgoConstants&);
void resolve_flexible_lengths(FlexLine&, const AlgoConstants&);
void determine_hypothetical_cross_size(LayoutTree&, FlexLine&, const AlgoConstants&,
                                       Size<std::optional<float>>, Size<AvailableSpace>);
void calculate_children_base_lines(LayoutTree&, Size<std::optional<float>>, Size<AvailableSpace>,
                                   std::vector<FlexLine>&, const AlgoConstants&);
void calculate_cross_size(std::vector<FlexLine>&, Size<std::optional<float>>, const AlgoConstants&);
void handle_align_content_stretch(std::vector<FlexLine>&, Size<std::optional<float>>,
                                  const AlgoConstants&);
void determine_used_cross_size(LayoutTree&, std::vector<FlexLine>&, const AlgoConstants&);
void distribute_remaining_free_space(std::vector<FlexLine>&, const AlgoConstants&);
void resolve_cross_axis_auto_margins(std::vector<FlexLine>&, const AlgoConstants&);
void align_flex_items_along_cross_axis(FlexLine&, float, const AlgoConstants&);
float determine_container_cross_size(const std::vector<FlexLine>&, Size<std::optional<float>>,
                                     AlgoConstants&);
void align_flex_lines_per_align_content(std::vector<FlexLine>&, const AlgoConstants&, float);
Rect<float> final_layout_pass(LayoutTree&, std::vector<FlexLine>&, const AlgoConstants&);
void collect_oof_candidates(LayoutTree&, NodeId, const AlgoConstants&, std::vector<FlexLine>&,
                            OofCandidates&);

inline float sum_axis_gaps(float gap, std::size_t count) {
    return count <= 1 ? 0.0f : gap * static_cast<float>(count - 1);
}
} // namespace taffy::flex_detail
