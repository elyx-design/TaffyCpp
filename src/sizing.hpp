#pragma once

/// @file
/// Private sizing-keyword resolution helpers.
#include <taffy/layout.hpp>

namespace taffy {

/** @brief Resolved sizing keyword: either an intrinsic layout request or an exact size.
 * Measurement means intrinsic layout of the node, not necessarily invocation of
 * the user's leaf-measure callback. All definite values use layout units.
 */
struct SizingKeywordResolution {
    enum class Kind {
        Measure,
        Exact
    }; ///< Discriminates the meaningful payload field.
    Kind kind = Kind::Measure;        ///< Resolution mode.
    AvailableSpace available_space{}; ///< Meaningful for Measure; definite or intrinsic constraint.
    float exact = 0; ///< Meaningful for Exact; no NaN or sign validation is performed.
};
class LayoutTree;
/** @brief Resolve intrinsic/fit-content/stretch sizing into measurement or an exact size.
 * Missing stretch/percentage bases leave dependent keywords unresolved (nullopt),
 * as do non-keyword dimensions. Present NaN bases remain present. calc_resolver is
 * borrowed for the call and invoked only for a resolvable fit-content calc;
 * its exceptions (including an empty std::function) propagate.
 */
std::optional<SizingKeywordResolution>
resolve_sizing_keyword(const Dimension& style, std::optional<float> stretch_size,
                       std::optional<float> percent_resolution_basis,
                       const std::function<float(Calc, float)>& calc_resolver);
/** @brief Fill unknown absolute-positioned border-box dimensions from sizing keywords.
 * Borrows tree/node; may perform cached intrinsic layout and mutate tree caches.
 * area_size supplies physical percentage bases. Missing insets/margins count as
 * zero when deriving nonnegative stretch space. Existing known dimensions are
 * retained; both measure keywords can be measured together. No ownership escapes.
 * Exceptions from tree access, calculation, measurement or allocation propagate.
 */
void resolve_absolute_sizing_keywords(LayoutTree& tree, NodeId node,
                                      Size<std::optional<float>>& known_dimensions,
                                      Size<Dimension> size_style, Size<float> area_size,
                                      Rect<std::optional<float>> inset,
                                      Rect<std::optional<float>> margin, SizingMode sizing_mode);

} // namespace taffy
