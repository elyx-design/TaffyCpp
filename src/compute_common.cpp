#include "compute_common.hpp"
#include <taffy/numeric.hpp>
#include <taffy/tree.hpp>

namespace taffy {
std::optional<float> child_first_baseline(std::optional<float> first, float height,
                                          Overflow overflow) {
    return is_scroll_container(overflow)
               ? std::optional<float>{f32_max(f32_min(first.value_or(height), height), 0)}
               : first;
}

AlignItemsKeyword resolve_self_alignment_safety(AlignItems alignment, bool overflows) {
    return alignment.safety == AlignmentSafety::Safe && overflows ? AlignItemsKeyword::Start
                                                                  : alignment.keyword;
}

AlignContentKeyword apply_alignment_fallback(float free_space, std::size_t items,
                                             AlignContent alignment) {
    auto keyword = alignment.keyword == AlignContentKeyword::Normal ? AlignContentKeyword::Stretch
                                                                    : alignment.keyword;
    bool safe = alignment.safety == AlignmentSafety::Safe;
    if (items <= 1 || free_space <= 0) {
        switch (keyword) {
        case AlignContentKeyword::Stretch:
        case AlignContentKeyword::SpaceBetween:
            keyword = AlignContentKeyword::FlexStart;
            safe = true;
            break;
        case AlignContentKeyword::SpaceAround:
        case AlignContentKeyword::SpaceEvenly:
            keyword = AlignContentKeyword::Center;
            safe = true;
            break;
        default:
            break;
        }
    }
    return free_space <= 0 && safe ? AlignContentKeyword::Start : keyword;
}

float compute_alignment_offset(float free_space, std::size_t items, float gap,
                               AlignContentKeyword alignment, bool reversed, bool first) {
    if (first) {
        switch (alignment) {
        case AlignContentKeyword::Start:
        case AlignContentKeyword::Normal:
        case AlignContentKeyword::Stretch:
        case AlignContentKeyword::SpaceBetween:
            return 0;
        case AlignContentKeyword::FlexStart:
            return reversed ? free_space : 0;
        case AlignContentKeyword::End:
            return free_space;
        case AlignContentKeyword::FlexEnd:
            return reversed ? 0 : free_space;
        case AlignContentKeyword::Center:
            return free_space / 2;
        case AlignContentKeyword::SpaceAround:
            return free_space >= 0 ? (free_space / static_cast<float>(items)) / 2 : free_space / 2;
        case AlignContentKeyword::SpaceEvenly:
            return free_space >= 0 ? free_space / (static_cast<float>(items) + 1) : free_space / 2;
        }
    }
    free_space = f32_max(free_space, 0);
    switch (alignment) {
    case AlignContentKeyword::SpaceBetween:
        if (items == 0)
            throw TreePanic("space-between item count underflow");
        return gap + free_space / static_cast<float>(items - 1);
    case AlignContentKeyword::SpaceAround:
        return gap + free_space / static_cast<float>(items);
    case AlignContentKeyword::SpaceEvenly:
        return gap + free_space / (static_cast<float>(items) + 1);
    default:
        return gap;
    }
}

Rect<float> compute_scrollable_overflow_contribution(Point<float> location, Size<float> size,
                                                     Rect<float> rect, Point<Overflow> overflow,
                                                     Contain contain, bool parent_scrolls) {
    const bool scrolls = is_scroll_container(overflow.x) || is_scroll_container(overflow.y);
    const bool contained = contain.contains_scrollable_overflow();
    const Point<bool> propagates{!scrolls && !contained && overflow.x == Overflow::Visible,
                                 !scrolls && !contained && overflow.y == Overflow::Visible};
    const Size<float> end{propagates.x ? f32_max(size.width, rect.right) : size.width,
                          propagates.y ? f32_max(size.height, rect.bottom) : size.height};
    if (end.width <= 0 || end.height <= 0)
        return {};
    const Rect<float> contribution{
        location.x + (propagates.x ? f32_min(0, rect.left) : 0), location.x + end.width,
        location.y + (propagates.y ? f32_min(0, rect.top) : 0), location.y + end.height};
    return parent_scrolls && (contribution.right <= 0 || contribution.bottom <= 0) ? Rect<float>{}
                                                                                   : contribution;
}
} // namespace taffy
