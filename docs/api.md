# Native C++ API guide

Use `#include <taffy/taffy.hpp>` and namespace `taffy`. This is a native C++23
layout library: it computes boxes, not rendering, text shaping, DOM, scrolling,
or a browser cascade. No Rust runtime or test adapter is needed by consumers.
The public headers are the API authority; the examples below use that API directly.

## Finding your way around the headers

- [`taffy.hpp`](../include/taffy/taffy.hpp): umbrella include and a complete getting-started example.
- [`tree.hpp`](../include/taffy/tree.hpp): node creation, child operations, callbacks, ownership and errors.
- [`style.hpp`](../include/taffy/style.hpp): style fields and layout keywords.
- [`values.hpp`](../include/taffy/values.hpp): typed lengths, percentages and available space.
- [`grid.hpp`](../include/taffy/grid.hpp): grid tracks, repetition, named areas and placement inputs.
- [`context.hpp`](../include/taffy/context.hpp): application-owned node data.
- [`error.hpp`](../include/taffy/error.hpp): tree errors and exceptions.
- [`layout.hpp`](../include/taffy/layout.hpp): output boxes, coordinate frames and optional grid metadata.
- [`geometry.hpp`](../include/taffy/geometry.hpp): sizes, points, edge rectangles and axis selectors.
- [`parse.hpp`](../include/taffy/parse.hpp): optional CSS-value/declaration parsing helpers.

Start with `Tree`. Custom-storage integrations can implement the public
`tree_adapter.hpp` interface and use `compute.hpp` entry points. Grid placement,
float fitting, line balancing and other algorithm machinery live privately in
`src/`; applications do not need to include or understand them.
This is a **compiled library**, not a header-only library: ordinary implementations
live in `src/`. Required template definitions and tiny hot-path inline arithmetic
are installed under `include/taffy/detail/` and included automatically; they are
not separate public APIs.
The private cache observation seam lives under `src/`, not the installed interface.

## Install and consume

The installed package is **`taffy_cpp`**, and its imported target is
**`taffy::taffy`** (not `taffy_cpp::taffy_cpp`). The target propagates the include
path and C++23 requirement:

```cmake
find_package(taffy_cpp CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE taffy::taffy)
```

A compiler **and standard library** with C++23 `std::expected` support are required.
See [building](building.md) for project options. [The examples](../examples/README.md)
are a separate installed-package consumer, not added to the root build.

## First layout

```cpp
#include <taffy/taffy.hpp>
#include <array>

int main() {
    taffy::Tree tree;
    taffy::Style item;
    item.size = {taffy::length(80), taffy::length(24)};
    auto leaf = tree.new_leaf(item).value();
    auto root = tree.new_with_children({}, std::array{leaf}).value();
    tree.compute_layout(root, {taffy::AvailableSpace::definite(320),
                               taffy::AvailableSpace::max_content()}).value();
    auto box = tree.layout(leaf); // copy of the computed result
    return box.size.width == 80 ? 0 : 1;
}
```

Here `.value()` is appropriate for a tiny fail-fast example, not a promise that
all failures use `std::expected`. See [errors](#errors-and-invalid-input).
An available width is a constraint/percentage basis, **not** an instruction to
set every root's width to that number. Set `Style::size` when a fixed box is wanted.
Empty nodes take the leaf path regardless of Flex/Grid/Block display.

## Values, units, and size keywords

All layout coordinates are `float` values in an application-chosen unit (usually
pixels). Percentages are **fractions**: `Dimension::percent(0.5f)` means 50%.
There is no implicit DPI, font unit, viewport unit, or device-pixel conversion.

| Type | Construction and intended fields |
| --- | --- |
| `LengthPercentage` | `length`, `percent`, `calc`, `zero`; padding, border, gap |
| `LengthPercentageAuto` | The above plus `auto_`; margin, inset, min/max size |
| `Dimension` | The above plus intrinsic keywords; preferred size and flex basis |
| `MinTrackSizingFunction` | Length/percentage/calc/auto/min-content/max-content |
| `MaxTrackSizingFunction` | Minimum-track values plus `fr` and bounded fit-content |
| `AvailableSpace` | `definite(n)`, `min_content()`, `max_content()`; a query constraint, not a CSS length |

`Dimension` provides `min_content()`, `max_content()`, `fit_content()` (bare
keyword), `fit_content_px(limit)`, `fit_content_percent(fraction)`,
`fit_content_calc(Calc{handle})`, `stretch()`, and `content()`. `content()` is
principally the flex-basis content keyword. Intrinsic keywords ask the layout
algorithm to measure content; they are not numeric zero or percentages.
`fit_content(LengthPercentage::length(100))` constructs a bounded value.
An indefinite percentage basis cannot resolve a percentage/calc to a definite
length; relevant sizing paths treat it as auto. Do not replace min/max-content
constraints with an arbitrary very large float.

Factories are constrained by domain: `LengthPercentage::auto_()` and
`Dimension::fr()` do not compile. `Style::min_size` and `max_size` are
`Size<LengthPercentageAuto>`, **not** `Size<Dimension>`: this API cannot express
intrinsic min/max-size keywords there. Accepted conversion between length domains
can also normalize unsupported track keywords to auto; construct track domains
explicitly when the distinction matters.

`kind()` identifies a length; `value()` throws `std::logic_error` for keywords or
calc handles, and `calc_value()` throws for a non-calc value. A default
`LengthPercentage` is length zero; the other length domains default to auto.
Default `AvailableSpace` is max-content. Free helpers `length()`, `percent()`,
`auto_()` default to `Dimension`; specify the output type for other fields,
e.g. `percent<LengthPercentage>(0.1f)`.

Aggregate ordering matters:

- `Size<T>{width, height}`, `Point<T>{x, y}`, `Line<T>{start, end}`.
- **`Rect<T>{left, right, top, bottom}`**, not CSS shorthand order.
- `rect_all(value)` and `size_all(value)` avoid repetition.
- `gap.width` is column gap; `gap.height` is row gap.

## Style defaults and practical CSS semantics

`Style{}` is intentionally not a browser element's default stylesheet:

| Fields | Defaults |
| --- | --- |
| display / box sizing / direction | `Flex` / `BorderBox` / `Ltr` |
| position / inset | `Static` / all auto |
| size, min_size, max_size | both auto |
| margin, padding, border, gap | zero on all edges/axes |
| overflow, scrollbar_width | visible on both axes, zero gutter width |
| aspect_ratio | absent |
| align_items, justify_items, align_content, justify_content | `Normal`, default safety |
| align_self, justify_self | absent (inherit container alignment behavior) |
| flex_direction, flex_wrap, flex_line_count | row, no-wrap, 1 |
| flex_basis, flex_grow, flex_shrink | auto, 0, 1 |
| grid templates and auto-track lists | empty; default implicit sizing is auto |
| grid_auto_flow / grid_row / grid_column | row / auto / auto |
| contain / float_ / clear / text_align | none / none / none / auto |
| table/replaced/compressible-replaced flags | false |

`Normal` alignment is algorithm-dependent, not a universal start position; grid
content normal resolves to stretch. Set `AlignItems::center()` or
`JustifyContent::space_between()` explicitly when desired. Safe/unsafe alignment
is represented by `AlignmentSafety`, not a separate layout mode.

Other important semantics:

- Border-box size includes padding and border; content-box adds them to the
  requested size. Margin is outside the border box. A box cannot shrink below
  its padding/border floor.
- Percentage margin/padding/border edges use the containing inline width basis,
  including top/bottom edges. Preferred height percentages use a height basis.
- Flex defaults shrink but do not grow. The automatic minimum can prevent a
  flex item from shrinking; set the relevant `min_size` to length zero when
  that behavior is wanted. `flex_basis` is not simply an alias for width.
- Grid `TrackSizingFunction::fr(1)` is `minmax(auto, 1fr)`;
  `TrackSizingFunction::flex(1)` is `minmax(0, 1fr)`. This matters for intrinsic
  minimums. `GridTemplateComponent` holds either a track or a repetition.
  `minmax(minimum, maximum)` constructs a track; placement uses
  `GridPlacement::line(n)`, `span(n)`, named variants, or auto. Grid lines are
  CSS line coordinates, not zero-based child indices.
- `Display::None` recursively hides layout. It does not remove nodes/contexts.
  Block and FlowRoot choose block layout, with FlowRoot separating its block
  context. Relative positioning offsets the laid-out box; absolute/fixed are
  out of normal flow. Containing-block and hoisting logic is part of layout.
- Overflow is geometry, not clipping/rendering. `Scroll` reserves the configured
  `scrollbar_width` on the opposite axis; this library does not create scrollbars.
  Sticky has a public enum but there is no scroll-position update API: do not
  expect this library alone to implement interactive sticky scrolling.
- CSS-like layout support does not imply a complete browser engine or all CSS
  sizing-keyword contexts. In particular, the bare leaf algorithm resolves
  definite dimensions; contextual intrinsic keyword resolution is supplied by
  container/positioning algorithms, not a general expression evaluator.

## Reading layout

`tree.layout(node)` borrows the final rounded layout by default.
`disable_rounding()` selects unrounded results; `enable_rounding()` restores
rounding for subsequent computations. Run layout again after changing this
setting before expecting newly populated final results. Root rounding is
performed after layout and uses accumulated coordinates to avoid independent
edge-rounding drift.

`Layout` contains `order`, `location`, border-box `size`, padding/border/margin,
`scrollbar_size`, and `scrollable_overflow_rect`. Child location is relative to
its layout coordinate parent; out-of-flow descendants may be hoisted, so do not
blindly treat logical ancestry as geometric ancestry. Helpers include
`content_box_size()`, `content_box_x/y()`, and `scroll_width/height()`.
`unrounded_layout(node)` is also available. No geometry is promised current
until a successful layout after mutation.

For grid, `detailed_layout_info(node)` is a variant: test with
`std::get_if<DetailedGridInfo>` before accessing track positions, implicit/explicit
counts, names, areas, and item placement. Non-grid info can be `std::monostate`.
`Layout` itself does not expose baselines: those travel in `LayoutOutput` between
algorithms and callbacks.

## Measurement: intrinsic size versus full output

### Intrinsic measurement

```cpp
tree.set_intrinsic_measure(
    [](const taffy::LayoutInput& input, taffy::NodeId node,
       taffy::NodeContext* context, const taffy::Style& style) -> taffy::Size<float> {
        // Measure application content here; node/context/style identify it.
        (void)node; (void)context; (void)style;
        return {input.known_dimensions.width.value_or(80),
                input.known_dimensions.height.value_or(16)};
    });
```

`IntrinsicMeasure` returns raw **content size**. `compute_leaf` applies the CSS
box model, min/max constraints, aspect ratio and overflow around it. No callback
means zero intrinsic content size, not an error. The callback may run repeatedly
with different constraints, or not run on a cache hit/definite-size shortcut.
Never depend on a fixed invocation count or use it as a render notification.

Inspect `available_space` to distinguish min-content, max-content, and definite
queries. Known dimensions are optional constraints, not necessarily the final
CSS size. The leaf algorithm adjusts available space for insets and, during a
perform-layout measurement, resets the measurement input's known dimensions;
implement measurement using the actual input, not assumptions about the parent
call. The sample's fixed text metrics are illustrative, not text shaping.

### Full leaf output

`Tree::LeafLayout` is:

```cpp
std::function<taffy::LayoutOutput(taffy::LayoutTree&, taffy::NodeId,
                                 const taffy::LayoutInput&)>
```

Install one persistently with `set_leaf_layout(callback)`, or synchronously with
`compute_layout_with_measure(root, space, callback)`. Despite its name, the latter
accepts **full leaf layout**, not an intrinsic-size callback. It replaces the
leaf algorithm, so returning only a size does **not** automatically apply CSS.
Call `compute_leaf(tree, node, input)` inside it to retain intrinsic/CSS behavior,
then augment the output. Do not call `compute_child_layout` for that same leaf
inside its callback: that reenters the callback.

`LayoutOutput` includes size, scrollable overflow, first/last optional baselines,
collapsible top/bottom margins, collapse-through flag, OOF candidates and optional
OOF positioning area. Use `from_outer_size`, `from_sizes`, or
`from_sizes_and_baselines` when useful. Honour run/sizing modes and requested
axes, and supply metadata required by the parent's algorithm. The full callback
takes precedence over intrinsic measurement; it can deliberately call the leaf
algorithm, which then uses intrinsic measurement.

The temporary callback is restored even on exceptions. It **does not invalidate
existing caches on entry or exit**. Explicitly `mark_dirty` before changing its
meaning or switching back to another callback; otherwise cached output may be
reused. This also applies to changed captured state. Persistent callback setters
and `set_calc_resolver` invalidate all nodes automatically.

Callbacks run synchronously and may borrow captured state for the call. Persistent
callbacks must not outlive their captures. The built-in tree rejects structural,
style, context and callback mutation, `mark_dirty`, and reentrant root layout while
layout runs (`TreePanic`). Reading borrowed styles/contexts is supported; do not
use low-level cache/layout setters as an escape hatch to mutate algorithm state.

## Context ownership and RAII

Nodes can uniquely own arbitrary application data:

```cpp
struct Text { std::string contents; };
auto node = tree.new_leaf_with_context({}, taffy::make_context<Text>(Text{"hello"})).value();
auto* text = taffy::context_cast<Text>(tree.node_context(node));
if (text) {
    text->contents = "updated";
    tree.mark_dirty(node).value();
}
```

`NodeContext` has a virtual destructor. Subclass it directly, or use
`make_context<T>(constructor_args...)`, which wraps a value in `ValueContext<T>`
and supports move-only types. `context_cast<T>` returns null on absent/mismatched
wrapped type; use `dynamic_cast` for your own direct subclass instead.

Replacement via `set_node_context`, node removal, `clear()`, or tree destruction
immediately destroys owned context. Context and style references are borrowed;
do not retain them across replacement/removal/destruction. The tree is moveable,
not copyable. External resource owners can put RAII handles in their context.
`disjoint_node_contexts(span)` returns an optional vector of pointers; duplicates,
invalid IDs, or absent contexts yield no value. It is not a locking mechanism.
Context payload changes through a pointer are not automatically observed: mark
the node dirty. Replacing its context through the setter does this automatically.

## Mutation, identity, dirty state, and cache

`Tree(capacity = 16)` owns a generational node arena. `capacity()` is allocated
arena slot capacity, not a node's child capacity; `total_node_count()` is live
nodes. `NodeId{0}` is invalid. IDs are tree-local handles, not persistent document
IDs; removal/clear invalidates them. `contains(id)` tests validity. Do not mix IDs
from different trees even if their numeric values happen to match.

Creation: `new_leaf`, `new_leaf_with_context`, `new_with_children`.
Topology: `add_child`, `insert_child_at_index`, `set_children`, `remove_child`,
`remove_child_at_index`, `remove_children_range` (half-open range),
`replace_child_at_index`, `parent`, `children`, `child_at_index`.
`remove(node)` removes **only that node**, detaches it from its parent and detaches
its children; it does not recursively delete a subtree. `clear()` deletes all nodes.

Maintain an acyclic, unique-child, single-parent tree. Do not assume all attachment
APIs detach a child from an old parent: `add_child`, insertion, replacement, and
`new_with_children` can leave an old parent's child list intact; `set_children`
has explicit reparenting behavior. Safest explicit move: remove from old parent,
then add to new parent. Avoid duplicates and replacing a child by itself; those
edge cases are not a normalized DOM mutation interface.

`set_style` copies/replaces a style and dirties the node and ancestors. To edit,
copy `tree.style(node)`, modify the copy, then `set_style`. Child-list changes and
context replacement also invalidate affected ancestors. `mark_dirty(node)` clears
that node and walks upward, stopping at an already-empty cache. This relies on
valid single-parent topology and on not manually breaking the dirty invariant.
`dirty(node)` means **cache empty**, not a separate application change flag.
Descendants are not all recursively invalidated by marking their parent dirty.

Caches are per node, with a final-layout entry and up to nine measurement entries.
Both parent percentage bases participate in measurement matching. Numeric cache
eligibility follows pinned Rust, including negative, signed-zero and nonfinite
request dimensions.
Keys account for layout constraints/modes; they do not hash your context or
captured application state. Use `mark_dirty` on the actual changed leaf. Changing
a persistent intrinsic/full callback or calc resolver invalidates all caches.
Do not manually clear just one `cache(node)` when ancestors might retain output.
The library does not provide synchronization; externally serialize access to a
mutable tree and its borrowed data.

## Errors and invalid input

There are **both expected errors and exceptions**. Handle both at an application
boundary:

- Many methods return `std::expected<T, TreeError>`; child-index bounds failures
  use `TreeErrorKind::ChildIndexOutOfBounds`, with node/index/count and `message()`.
- Invalid/stale node access commonly throws `NodeAccessError` (`std::out_of_range`)
  even from methods whose signature returns `expected`, including `compute_layout`.
  Do not assume `InvalidInputNode`/`InvalidParentNode`/`InvalidChildNode` enum values
  mean those methods always return an error instead of throwing.
- Cycle insertion, removing a non-child, invalid removal ranges, and mutation
  during layout or node-context destruction throw `TreePanic` (`std::logic_error`).
  Context destructors must not throw or mutate their owning tree. Moving a tree
  during layout/context destruction terminates because moves are `noexcept`.
- Unhandled tree calc values resolve to zero, as in pinned Rust; explicit direct
  length helpers still require their resolver. Bad length inspection throws
  `std::logic_error`; structural validation can throw `std::invalid_argument`;
  allocation and user callback exceptions propagate.
- `Style::validate()` checks structural invariants (notably grid names/areas and
  repetitions). Do not assume creation or `set_style` validates every numeric or
  CSS constraint. The library is not a sanitizing untrusted-input boundary.

Calling `.value()` on an unexpected result throws `std::bad_expected_access`.
Check the result and report `result.error().message()` if recovery is needed.
No general transaction/rollback guarantee is provided for arbitrary layout or
callback exceptions; fix the cause, invalidate affected data, and recompute.

## Calculation resolver

`Calc{std::uintptr_t_handle}` is an **opaque application-owned identifier**, not a
parsed/owned expression or pointer-tagging ABI. Register a resolver before layout:

```cpp
tree.set_calc_resolver([](taffy::Calc expression, float basis) {
    if (expression.handle != 7) throw std::invalid_argument("unknown calc handle");
    return basis * 0.5f + 12.0f;
});
auto style = tree.style(node);
style.size.width = taffy::Dimension::calc(taffy::Calc{7});
tree.set_style(node, style).value();
```

The resolver receives the relevant percentage basis and returns a resolved length.
The application owns expression storage and its lifetime. All calc handles are
conservatively basis-dependent; an absent basis can defer resolution as with a
percentage. Bounded fit-content can carry a calc limit too. Replacing the resolver
invalidates all caches; changing captured expression data requires invalidation.
A custom `LayoutTree` may override `resolve_calc`. Without an override or an
installed resolver, both tree interfaces return zero, matching pinned Rust's
`LayoutPartialTree` and `TaffyTree`. Clearing a resolver restores this default.
Direct length helper calls with an empty `CalcResolver` can still throw
`std::bad_function_call`: those helpers require the caller's resolution function.

## Custom trees and lower-level algorithms

Implement `LayoutTree` to lay out externally owned storage without copying it
into `Tree`. Required operations cover child IDs, styles, recursive child layout,
raw measurement, unrounded/final layout storage, and hoisted-child management.
Use one `Cache` per node, or override **all** `cache_get`, `cache_store`, and
`cache_clear` hooks. Override `cache_contains` too for a non-copying presence
query; its compatibility default delegates to `cache_get`. The default `cache()`
throws when no native cache is supplied.
Algorithm-specific style views default to `style(node)` and may be overridden.

`child_ids(node)` returns an owned snapshot. Override the virtual `child_count(node)`
and `child_id(node, index)` for allocation-free indexed traversal. Their defaults
use `child_ids` for compatibility with existing custom trees; `Tree` overrides both
with O(1) access to its stored vector. Flex's repeated scans take one snapshot
per pass, avoiding quadratic copies for compatibility adapters. Invalid indices
throw `std::out_of_range`.
The new virtual methods change the binary interface: rebuild the library and all
consumers/custom-tree implementations together.

A normal adapter's `compute_child_layout` calls the **four-argument**
`compute_cached_layout(tree, node, input, compute_function)`. Its compute function
uses `compute_dispatch` (uncached Flex/Grid/Block/leaf selection) and performs
`compute_oof_layout` in perform-layout mode. The three-argument cached overload
calls your virtual `compute_child_layout`: using it as that method's implementation
causes recursion. Individual `compute_leaf`, `compute_flex`, `compute_grid`,
`compute_block`, `compute_hidden_layout` functions are also public.

Call `compute_root_layout(adapter, root, space)` and optionally
`round_layout(adapter, root)`. The adapter owns invalidation, ID validation,
context lifetimes, mutation/reentrancy policy, and topology correctness. Style
references must stay valid until mutation; algorithms may retain borrowed views
during callbacks. A full block adapter should forward the synchronous borrowed
`BlockContext*` in `compute_block_child_layout` when sharing float/block context;
the default implementation falls back to ordinary child layout. Implement grid
metadata hooks if callers need detailed grid info (defaults discard it), and real
hoisted-child storage for multi-node out-of-flow layout/rounding.

[custom_tree.cpp](../examples/custom_tree.cpp) is intentionally a minimal one-node
adapter, not a production multi-node topology implementation. Deferred out-of-flow
positioning is implemented by `compute_oof.cpp` and root integration; the public
`compute.hpp` documents that boundary. An exposed enum alone is not a promise of
full CSS conformance.

## CSS value parsing

`parse_css<T>(text)` returns `std::expected<T, ParseError>` with code, byte offset,
and message. It parses a complete supported value, not arbitrary stylesheets.
`parse_css<Style>` parses a declaration list using the supported property grammar;
there is no selector matching, inheritance engine, DOM, or full cascade.
Geometry parsing uses **CSS shorthand order**, unlike C++ `Rect` aggregate order.
Keyword-enum parsing is ASCII-insensitive; length units/keywords follow the pinned
grammar's case rules. Parse failure is not silent normalization.

`calc()`/`var()` strings do not create `Calc` handles: use the application resolver
API for calculations. Not every public metadata type has a CSS grammar; unsupported
types return `ParseErrorCode::Unsupported`. Check parser results before passing
values into layout. The parser is a convenience layer, not a browser CSS engine.

## Runnable examples

- [flex.cpp](../examples/flex.cpp): growth, gap, explicit root dimensions.
- [grid.cpp](../examples/grid.cpp): fixed + fractional tracks, detailed grid info.
- [measurement.cpp](../examples/measurement.cpp): RAII context, intrinsic content,
  padding, and a full-output callback that adds a baseline.
- [mutation.cpp](../examples/mutation.cpp): cache reuse, captured-state invalidation,
  style replacement, expected index errors, stale-ID exceptions.
- [custom_tree.cpp](../examples/custom_tree.cpp): independent storage and dispatch.

See [examples/README.md](../examples/README.md) for build and run commands.
These examples demonstrate library usage, not browser conformance; see
[known issues](known-issues.md) for the remaining compatibility boundaries.
