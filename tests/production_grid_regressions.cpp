#include "../src/grid.hpp"
#include <catch2/catch_test_macros.hpp>
#include <taffy/compute.hpp>
#include <taffy/tree.hpp>

using namespace taffy;

TEST_CASE("production grid reverse disjoint intervals preserve occupancy and cursors",
          "[production][grid][occupancy]") {
    // Preallocate the axes to isolate interval insertion from implicit-track growth.
    // Thousands of separated runs in reverse order previously rebuilt the entire
    // vector on every insertion. Exercise both orientations and their metadata.
    constexpr std::int32_t count = max_grid_tracks / 2;
    for (const auto axis : {AbsoluteAxis::Horizontal, AbsoluteAxis::Vertical}) {
        const auto primary = grid::TrackCounts::from_raw(0, count * 2, 0);
        const auto secondary = grid::TrackCounts::from_raw(0, 1, 0);
        grid::CellOccupancy occupancy(axis == AbsoluteAxis::Horizontal ? primary : secondary,
                                      axis == AbsoluteAxis::Horizontal ? secondary : primary);
        for (auto i = count; i-- > 0;)
            occupancy.mark_area_as(axis, {2 * i, 2 * i + 1}, {0, 1},
                                   grid::CellOccupancyState::AutoPlaced);
        CHECK(occupancy.auto_placement_cursor(axis, 0) == 2 * count - 1);
        CHECK(occupancy.track_counts(axis) == primary);
        CHECK(occupancy.occupied_track_jump(axis, {0, count * 2}) == 2 * count - 1);
        for (std::int32_t i = 0; i < count; ++i) {
            REQUIRE(occupancy.line_area_collision_jump(axis, {2 * i, 2 * i + 1}, {0, 1}) ==
                    2 * i + 1);
            REQUIRE(occupancy.line_area_is_unoccupied(axis, {2 * i + 1, 2 * i + 2}, {0, 1}));
            const auto row = axis == AbsoluteAxis::Horizontal ? 0 : 2 * i;
            const auto column = axis == AbsoluteAxis::Horizontal ? 2 * i : 0;
            REQUIRE(occupancy.state_at(row, column) == grid::CellOccupancyState::AutoPlaced);
        }
        // Overwrite/split existing runs, bridge gaps, then merge an entire track.
        occupancy.mark_area_as(axis, {3, 21}, {0, 1}, grid::CellOccupancyState::DefinitelyPlaced);
        REQUIRE(occupancy.line_area_collision_jump(axis, {4, 5}, {0, 1}) == 21);
        occupancy.mark_area_as(axis, {0, count * 2}, {0, 1},
                               grid::CellOccupancyState::DefinitelyPlaced);
        CHECK(occupancy.line_area_collision_jump(axis, {1, 2}, {0, 1}) == count * 2);
        CHECK(occupancy.auto_placement_cursor(axis, 0) == 2 * count - 1);
    }
}

TEST_CASE("production grid indexed overwrites match a dense occupancy oracle",
          "[production][grid][occupancy]") {
    constexpr std::int32_t count = 96;
    grid::CellOccupancy occupancy(grid::TrackCounts::from_raw(0, count, 0),
                                  grid::TrackCounts::from_raw(0, 1, 0));
    std::vector<grid::CellOccupancyState> expected(count, grid::CellOccupancyState::Unoccupied);
    std::vector<bool> painted(count);
    const auto paint = [&](std::int32_t start, std::int32_t end, grid::CellOccupancyState state) {
        occupancy.mark_area_as(AbsoluteAxis::Horizontal, {start, end}, {0, 1}, state);
        for (auto i = start; i < end; ++i) {
            expected[i] = state;
            painted[i] = true;
        }
        for (std::int32_t i = 0; i < count; ++i) {
            REQUIRE(occupancy.state_at(0, i) == expected[i]);
            // Painting Unoccupied records provenance too; it does not erase a run.
            REQUIRE(occupancy.line_area_is_unoccupied(AbsoluteAxis::Horizontal, {i, i + 1},
                                                      {0, 1}) == !painted[i]);
        }
    };
    for (auto i = count; i > 0; i -= 2)
        paint(i - 1, i, grid::CellOccupancyState::AutoPlaced);
    std::uint32_t random = 12345;
    for (int i = 0; i < 300; ++i) {
        random = random * 1664525u + 1013904223u;
        const auto start = static_cast<std::int32_t>(random % count);
        random = random * 1664525u + 1013904223u;
        const auto end = start + 1 + static_cast<std::int32_t>(random % (count - start));
        paint(start, end, static_cast<grid::CellOccupancyState>(i % 3));
    }
}

TEST_CASE("production grid retains large child identities and last subtree OOF candidates",
          "[production][grid][source-order][oof]") {
    Tree tree;
    Style child_style;
    child_style.size = Size<Dimension>::from_lengths(1, 1);
    child_style.grid_row =
        child_style.grid_column = {GridPlacement::line(1), GridPlacement::line(2)};
    constexpr std::size_t count = 65537;
    std::vector<NodeId> children;
    children.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        children.push_back(tree.new_leaf(child_style).value());
    const auto descendant = tree.new_leaf().value();
    tree.set_leaf_layout([&](LayoutTree&, NodeId node, const LayoutInput&) {
        auto output = LayoutOutput::from_outer_size({1, 1});
        if (node == children.back()) {
            OofCandidate candidate;
            candidate.node = descendant;
            candidate.position = Position::Fixed;
            output.oof_candidates.push(candidate);
        }
        return output;
    });
    Style style;
    style.display = Display::Grid;
    style.size = Size<Dimension>::from_lengths(10, 10);
    const auto root = tree.new_with_children(style, children).value();
    grid::NamedLineResolver resolver(style);
    grid::CellOccupancy occupancy;
    const auto placed = grid::place_grid_items(tree, root, occupancy, resolver);
    REQUIRE(placed.size() == count);
    CHECK(placed[65535].source_order == 65535);
    CHECK(placed[65536].source_order == 65536);
    const auto item =
        grid::GridItem::new_with_style_and_order(children.back(), child_style, {}, {}, count - 1);
    CHECK(item.source_order == count - 1);
    const auto output = compute_grid(tree, root, {});
    REQUIRE(output.oof_candidates.size() == 1);
    CHECK(output.oof_candidates.inner.front().node == descendant);
    CHECK(tree.unrounded_layout(children.back()).order == count - 1);
}
