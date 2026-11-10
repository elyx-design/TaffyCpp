#pragma once

/// @file
/// Start here: the standalone C++23 layout API. Link the CMake target taffy::taffy.
/// Taffy computes geometry; it does not create windows, draw, load fonts or parse HTML.
/// Layout units are application-defined (normally CSS pixels); percentages are fractions.
///
/// @code{.cpp}
/// #include <taffy/taffy.hpp>
/// #include <array>
///
/// int main() {
///     taffy::Tree tree;
///     taffy::Style item;
///     item.size = {taffy::Dimension::length(80), taffy::Dimension::length(30)};
///     auto child = tree.new_leaf(item).value();
///
///     taffy::Style row;
///     row.display = taffy::Display::Flex;
///     row.size = {taffy::Dimension::length(200), taffy::Dimension::length(40)};
///     std::array children{child};
///     auto root = tree.new_with_children(row, children).value();
///     tree.compute_layout(root, taffy::Size<taffy::AvailableSpace>::max_content()).value();
///     const auto& box = tree.layout(child); // Parent-relative border-box position/size.
///     return box.size.width == 80 && box.size.height == 30 ? 0 : 1;
/// }
/// @endcode
///
/// Use Tree::set_intrinsic_measure() for text/images and application-owned NodeContext
/// for measurement data. After changes, recompute the root; changing captured callback
/// data in place also requires mark_dirty(). Read tree.hpp for ownership/error rules.
/// Advanced custom-storage/algorithm entry points are in compute.hpp;
/// most applications need only Tree, Style, geometry types and the resulting Layout.
/// Headers under detail/ support templates/tiny inline operations, not separate public APIs.

#include <taffy/cache.hpp>
#include <taffy/compute.hpp>
#include <taffy/geometry.hpp>
#include <taffy/layout.hpp>
#include <taffy/numeric.hpp>
#include <taffy/parse.hpp>
#include <taffy/style.hpp>
#include <taffy/tree.hpp>
