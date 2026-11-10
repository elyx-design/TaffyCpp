#include "../block.hpp"

#include <algorithm>
#include <stdexcept>

namespace taffy {
namespace {
constexpr float infinity = std::numeric_limits<float>::infinity();

constexpr std::size_t side(FloatDirection direction) {
    return static_cast<std::size_t>(direction);
}
} // namespace

namespace block_detail {
bool float_fits_horizontally(float width, FloatDirection direction, float bfc_width,
                             std::array<float, 2> float_insets, std::array<float, 2> cb_insets) {
    const auto lead = side(direction);
    const auto trail = 1 - lead;
    const float x_inset = f32_max(float_insets[lead], cb_insets[lead]);
    const bool fits_opposite_floats =
        float_insets[trail] == 0.0f ||
        x_inset + width <= bfc_width - float_insets[trail] + FIT_TOLERANCE;
    const bool fits_containing_block =
        float_insets[lead] == 0.0f ||
        x_inset + width <= bfc_width - cb_insets[trail] + FIT_TOLERANCE;
    return fits_opposite_floats && fits_containing_block;
}

bool Segment::fits_float_width(Size<float> box, FloatDirection direction, float bfc_width,
                               std::array<float, 2> cb_insets) const {
    return float_fits_horizontally(box.width, direction, bfc_width, insets, cb_insets);
}

FloatFitter::FloatFitter(float width, float height, std::array<float, 2> insets)
    : bfc_width(width), slot_height(static_cast<double>(height)), cb_insets(insets) {}

void FloatFitter::union_insets(std::array<float, 2> insets) {
    float_insets[0] = f32_max(float_insets[0], insets[0]);
    float_insets[1] = f32_max(float_insets[1], insets[1]);
}

float FloatFitter::placed_inset(FloatDirection direction) const {
    return f32_max(float_insets[side(direction)], cb_insets[side(direction)]);
}

bool FloatFitter::fits_horizontally(float width, FloatDirection direction) const {
    return float_fits_horizontally(width, direction, bfc_width, float_insets, cb_insets);
}

void FloatFitter::add_height(float height) {
    slot_height += static_cast<double>(height);
}

bool FloatFitter::fits_vertically(float height) const {
    return slot_height >= static_cast<double>(height);
}
} // namespace block_detail

bool FloatContext::has_floats() const {
    return has_floats_;
}

bool FloatContext::has_active_floats(float min_y) const {
    return has_floats_ && !segments_.empty() && segments_.back().y.end > min_y;
}

void FloatContext::set_width(float available_width) {
    available_width_ = available_width;
}

std::span<const PlacedFloatedBox> FloatContext::left_floats() const {
    return left_floats_;
}

std::span<const PlacedFloatedBox> FloatContext::right_floats() const {
    return right_floats_;
}

void FloatContext::subdivide_segment(std::size_t idx, float divide_at_y) {
    auto& old = segments_.at(idx);
    if (!old.y.contains(divide_at_y) || old.y.start == divide_at_y)
        throw std::logic_error("float segment subdivision must lie strictly within the segment");
    const block_detail::Segment next{{divide_at_y, old.y.end}, old.insets, old.has_float};
    old.y.end = divide_at_y;
    segments_.insert(segments_.begin() + static_cast<std::ptrdiff_t>(idx + 1), next);
    // Splitting a band shifts stored half-open placement ranges as well.
    for (auto& placement : last_placed_floats_) {
        if (placement.start > idx)
            ++placement.start;
        if (placement.end > idx)
            ++placement.end;
    }
}

void FloatContext::update_last_placed_float(FloatDirection direction,
                                            block_detail::Range<std::size_t> placement) {
    auto& last = last_placed_floats_[side(direction)];
    last.start = std::max(last.start, placement.start);
    last.end = std::max(last.end, placement.end);
}

Point<float> FloatContext::place_floated_box(Size<float> box, float min_y,
                                             std::array<float, 2> cb_insets,
                                             FloatDirection direction, Clear clear) {
    has_floats_ = true;
    const auto placed = place_floated_box_inner(box, min_y, cb_insets, direction, clear);
    auto& bottom = clear_bottoms_[side(direction)];
    const float new_bottom = placed.y + placed.height;
    bottom = bottom ? f32_max(*bottom, new_bottom) : new_bottom;
    float_ceiling_ = float_ceiling_ ? f32_max(*float_ceiling_, placed.y) : placed.y;
    if (direction == FloatDirection::Left) {
        left_floats_.push_back(placed);
        return {placed.x_inset, placed.y};
    }
    right_floats_.push_back(placed);
    return {available_width_ - placed.x_inset - box.width, placed.y};
}

PlacedFloatedBox FloatContext::place_floated_box_inner(Size<float> box, float min_y,
                                                       std::array<float, 2> cb_insets,
                                                       FloatDirection direction, Clear clear) {
    const auto slot = side(direction);
    min_y = f32_max(f32_max(min_y, float_ceiling_.value_or(-infinity)),
                    cleared_threshold(clear).value_or(-infinity));
    const auto float_start = std::max(last_placed_floats_[0].start, last_placed_floats_[1].start);
    // Placement ends already name the first band after the cleared floats.
    std::size_t hwm = float_start;
    switch (clear) {
    case Clear::Left:
        hwm = std::max(float_start, last_placed_floats_[0].end);
        break;
    case Clear::Right:
        hwm = std::max(float_start, last_placed_floats_[1].end);
        break;
    case Clear::Both:
        hwm = std::max(last_placed_floats_[0].end, last_placed_floats_[1].end);
        break;
    case Clear::None:
        break;
    }
    std::size_t start_idx = segments_.size();
    for (auto idx = hwm; idx < segments_.size(); ++idx) {
        if (segments_[idx].y.end > min_y) {
            start_idx = idx;
            break;
        }
    }
    float start_y = min_y;
    auto end_idx = start_idx;
    std::optional<std::size_t> start, end;
    float placed_inset = cb_insets[slot];
    // Pin a candidate start band and accumulate both edge unions and f64 height.
    // end_idx intentionally survives failed candidates, as in the pinned algorithm.
    for (;;) {
        if (start_idx >= segments_.size())
            break;
        const auto& start_segment = segments_[start_idx];
        if (!start_segment.fits_float_width(box, direction, available_width_, cb_insets)) {
            ++start_idx;
            end_idx = std::max(end_idx, start_idx);
            continue;
        }
        start_y = f32_max(start_y, start_segment.y.start);
        block_detail::FloatFitter fitter(available_width_, start_segment.y.end - start_y,
                                         cb_insets);
        fitter.union_insets(start_segment.insets);
        bool retry = false;
        for (;;) {
            if (end_idx >= segments_.size()) {
                start = start_idx;
                placed_inset = fitter.placed_inset(direction);
                break;
            }
            const auto& end_segment = segments_[end_idx];
            fitter.union_insets(end_segment.insets);
            if (!fitter.fits_horizontally(box.width, direction)) {
                ++start_idx;
                end_idx = std::max(end_idx, start_idx);
                retry = true;
                break;
            }
            if (end_idx != start_idx)
                fitter.add_height(end_segment.y.end - end_segment.y.start);
            if (!fitter.fits_vertically(box.height)) {
                ++end_idx;
                continue;
            }
            start = start_idx;
            end = end_idx;
            placed_inset = fitter.placed_inset(direction);
            break;
        }
        if (retry)
            continue;
        break;
    }
    if (box.height == 0.0f)
        return {box.width, box.height, placed_inset, start_y};
    if (!start) {
        const float last_y_end = segments_.empty() ? -infinity : segments_.back().y.end;
        if (start_y > last_y_end)
            segments_.push_back({{last_y_end, start_y}, {0.0f, 0.0f}, {false, false}});
        start_y = f32_max(last_y_end, start_y);
        auto insets = cb_insets;
        insets[slot] += box.width;
        std::array<bool, 2> has_float{};
        has_float[slot] = true;
        segments_.push_back({{start_y, start_y + box.height}, insets, has_float});
        const auto new_start = segments_.size() - 1;
        update_last_placed_float(direction, {new_start, new_start + 1});
        return {box.width, box.height, cb_insets[slot], start_y};
    }
    start_idx = *start;
    if (start_y != segments_.at(start_idx).y.start) {
        subdivide_segment(start_idx, start_y);
        ++start_idx;
        if (end)
            ++*end;
    }
    if (!end) {
        const float last_y_end = segments_.back().y.end;
        const float end_y = start_y + box.height;
        if (end_y > last_y_end)
            segments_.push_back({{last_y_end, end_y}, cb_insets, {false, false}});
        end_idx = segments_.size() - 1;
    } else {
        end_idx = *end;
        const float end_y = start_y + box.height;
        while (end_idx > start_idx && end_y <= segments_.at(end_idx).y.start)
            --end_idx;
        if (segments_.at(end_idx).y.start < end_y && end_y < segments_[end_idx].y.end)
            subdivide_segment(end_idx, end_y);
    }
    const float inset_plus_width = placed_inset + box.width;
    for (auto idx = start_idx; idx <= end_idx; ++idx) {
        segments_.at(idx).insets[slot] = inset_plus_width;
        segments_[idx].has_float[slot] = true;
    }
    update_last_placed_float(direction, {start_idx, end_idx + 1});
    return {box.width, box.height, placed_inset, start_y};
}

std::optional<std::size_t> FloatContext::cleared_segment(Clear clear) const {
    const auto left = last_placed_floats_[0].end;
    const auto right = last_placed_floats_[1].end;
    if (clear == Clear::Left && left > 0)
        return left;
    if (clear == Clear::Right && right > 0)
        return right;
    if (clear == Clear::Both && (left > 0 || right > 0))
        return std::max(left, right);
    return std::nullopt;
}

std::optional<float> FloatContext::cleared_threshold(Clear clear) const {
    switch (clear) {
    case Clear::Left:
        return clear_bottoms_[0];
    case Clear::Right:
        return clear_bottoms_[1];
    case Clear::Both:
        if (clear_bottoms_[0] && clear_bottoms_[1])
            return f32_max(*clear_bottoms_[0], *clear_bottoms_[1]);
        return clear_bottoms_[0] ? clear_bottoms_[0] : clear_bottoms_[1];
    case Clear::None:
        return std::nullopt;
    }
    return std::nullopt;
}

ContentSlot FloatContext::find_content_slot(float min_y, std::array<float, 2> cb_insets,
                                            Clear clear, std::optional<std::size_t> after) const {
    // Zero-height floats have no bands, but their bottoms still impose clearance.
    min_y = f32_max(min_y, cleared_threshold(clear).value_or(-infinity));
    if (!has_active_floats(min_y))
        return {std::nullopt, cb_insets[0], min_y, available_width_ - cb_insets[0] - cb_insets[1],
                infinity};
    const auto cleared = cleared_segment(clear);
    // Unlike an inclusive `after` token, a cleared endpoint is already exclusive.
    const auto hwm = std::max(after ? *after + 1 : 0, cleared.value_or(0));
    for (auto idx = hwm; idx < segments_.size(); ++idx) {
        const auto& segment = segments_[idx];
        if (!(segment.y.end > min_y))
            continue;
        const float left = f32_max(segment.insets[0], cb_insets[0]);
        const float right = f32_max(segment.insets[1], cb_insets[1]);
        return {idx, left, f32_max(segment.y.start, min_y), available_width_ - left - right,
                infinity};
    }
    // Exhausting `after` must move below the bands, not expose full width beside them.
    min_y = f32_max(segments_.empty() ? min_y : segments_.back().y.end, min_y);
    return {std::nullopt, cb_insets[0], min_y, available_width_ - cb_insets[0] - cb_insets[1],
            infinity};
}

BfcSlot FloatContext::find_bfc_slot(float min_y, std::array<float, 2> cb_insets,
                                    std::array<float, 2> margins, Direction direction, Clear clear,
                                    std::optional<std::size_t> after) const {
    min_y = f32_max(min_y, cleared_threshold(clear).value_or(-infinity));
    const std::array margin_insets{cb_insets[0] + margins[0], cb_insets[1] + margins[1]};
    const float no_float_width = available_width_ - margin_insets[0] - margin_insets[1];
    BfcSlot no_float_slot{std::nullopt, margin_insets[0], min_y, no_float_width, no_float_width};
    if (!has_active_floats(min_y))
        return no_float_slot;
    const auto cleared = cleared_segment(clear);
    // Unlike an inclusive `after` token, a cleared endpoint is already exclusive.
    const auto hwm = std::max(after ? *after + 1 : 0, cleared.value_or(0));
    for (auto idx = hwm; idx < segments_.size(); ++idx) {
        const auto& segment = segments_[idx];
        if (!(segment.y.end > min_y))
            continue;
        const std::size_t lead = direction == Direction::Ltr ? 0 : 1;
        const auto trail = 1 - lead;
        std::array<float, 2> fit{}, stretch{};
        fit[lead] = segment.has_float[lead] ? f32_max(segment.insets[lead], margin_insets[lead])
                                            : margin_insets[lead];
        stretch[lead] = fit[lead];
        fit[trail] = segment.has_float[trail] ? f32_max(segment.insets[trail], cb_insets[trail])
                                              : f32_min(margin_insets[trail], cb_insets[trail]);
        stretch[trail] = segment.has_float[trail]
                             ? f32_max(segment.insets[trail], margin_insets[trail])
                             : margin_insets[trail];
        return {idx, fit[0], f32_max(segment.y.start, min_y), available_width_ - fit[0] - fit[1],
                available_width_ - stretch[0] - stretch[1]};
    }
    no_float_slot.y = f32_max(segments_.empty() ? min_y : segments_.back().y.end, min_y);
    return no_float_slot;
}

FloatIntrinsicWidthCalculator::FloatIntrinsicWidthCalculator(AvailableSpace width)
    : available_width_(width) {}

void FloatIntrinsicWidthCalculator::add_float(float width, FloatDirection direction, Clear clear) {
    if (available_width_.is_min_content())
        contribution_ = f32_max(contribution_, width);
    else {
        if (clear == Clear::Left || clear == Clear::Both)
            side_sums_[0] = 0.0f;
        if (clear == Clear::Right || clear == Clear::Both)
            side_sums_[1] = 0.0f;
        side_sums_[side(direction)] += width;
        contribution_ = f32_max(contribution_, side_sums_[0] + side_sums_[1]);
    }
    widest_ = f32_max(widest_, width);
}

float FloatIntrinsicWidthCalculator::result() const {
    return available_width_.is_definite()
               ? f32_max(f32_min(contribution_, available_width_.value()), widest_)
               : contribution_;
}

BlockContext BlockFormattingContext::root_block_context() {
    return BlockContext(*this);
}

BlockContext BlockContext::sub_context(float additional_y_offset, std::array<float, 2> insets) {
    BlockContext child(*bfc_);
    child.y_offset_ = y_offset_ + additional_y_offset;
    child.insets_ = {insets_[0] + insets[0], insets_[1] + insets[1]};
    child.content_box_insets_ = child.insets_;
    child.is_root_ = false;
    child.adjoining_floats_ = adjoining_floats_;
    return child;
}

bool BlockContext::is_bfc_root() const {
    return is_root_;
}

void BlockContext::set_width(float width) {
    bfc_->float_context_.set_width(width);
}

void BlockContext::apply_content_box_inset(std::array<float, 2> insets) {
    content_box_insets_ = {insets_[0] + insets[0], insets_[1] + insets[1]};
}

bool BlockContext::has_floats() const {
    return bfc_->float_context_.has_floats();
}

bool BlockContext::has_active_floats(float min_y) const {
    return bfc_->float_context_.has_active_floats(min_y + y_offset_);
}

Point<float> BlockContext::place_floated_box(Size<float> box, float min_y, FloatDirection direction,
                                             Clear clear, bool adjoins_unresolved_strut) {
    if (adjoins_unresolved_strut)
        adjoining_floats_[side(direction)] = true;
    auto pos = bfc_->float_context_.place_floated_box(box, min_y + y_offset_, content_box_insets_,
                                                      direction, clear);
    pos.y -= y_offset_;
    pos.x -= insets_[0];
    float_content_contribution_ = f32_max(float_content_contribution_, pos.y + box.height);
    return pos;
}

ContentSlot BlockContext::find_content_slot(float min_y, Clear clear,
                                            std::optional<std::size_t> after) const {
    auto slot = bfc_->float_context_.find_content_slot(min_y + y_offset_, content_box_insets_,
                                                       clear, after);
    slot.y -= y_offset_;
    slot.x -= insets_[0];
    return slot;
}

BfcSlot BlockContext::find_bfc_slot(float min_y, std::array<float, 2> margins, Direction direction,
                                    Clear clear, std::optional<std::size_t> after) const {
    auto slot = bfc_->float_context_.find_bfc_slot(min_y + y_offset_, content_box_insets_, margins,
                                                   direction, clear, after);
    slot.y -= y_offset_;
    slot.x -= insets_[0];
    return slot;
}

std::optional<float> BlockContext::cleared_threshold(Clear clear) const {
    const auto threshold = bfc_->float_context_.cleared_threshold(clear);
    return threshold ? std::optional<float>(*threshold - y_offset_) : std::nullopt;
}

bool BlockContext::has_adjoining_float(Clear clear) const {
    switch (clear) {
    case Clear::Left:
        return adjoining_floats_[0];
    case Clear::Right:
        return adjoining_floats_[1];
    case Clear::Both:
        return adjoining_floats_[0] || adjoining_floats_[1];
    case Clear::None:
        return false;
    }
    return false;
}

void BlockContext::merge_adjoining_floats(std::array<bool, 2> flags) {
    adjoining_floats_[0] |= flags[0];
    adjoining_floats_[1] |= flags[1];
}

void BlockContext::commit_strut() {
    if (!top_adjoining_floats_)
        top_adjoining_floats_ = adjoining_floats_;
    adjoining_floats_ = {false, false};
}

std::array<bool, 2> BlockContext::top_adjoining_floats() const {
    return top_adjoining_floats_.value_or(adjoining_floats_);
}

void BlockContext::add_child_floated_content_height_contribution(float contribution) {
    float_content_contribution_ = f32_max(float_content_contribution_, contribution);
}

float BlockContext::floated_content_height_contribution() const {
    return float_content_contribution_;
}
} // namespace taffy
