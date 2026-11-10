#ifndef TAFFY_TEST_BEHAVIOR_SUITE_ORACLE_H
#define TAFFY_TEST_BEHAVIOR_SUITE_ORACLE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Persistent native Rust std oracle. No production implementation is called.
 * String output required lengths include trailing NUL; capacity=0 permits
 * NULL buffers. Node-array required lengths are element counts, no terminator.
 * Short buffers receive a prefix and report full required length, status OK.
 * Outputs are valid only on OK; no implicit zero-on-error promise.
 * All pointers must be valid/aligned for their lengths. No concurrent use or
 * callback reentry on a tree. Strings are UTF-8, length-delimited, not owned.
 * All functions except free/error return POISONED after a caught Rust panic or
 * callback failure. error remains available for diagnosis. IDs are never reused.
 */
typedef struct BehaviorOracle BehaviorOracle;
typedef struct BehaviorText { const char* data; size_t len; } BehaviorText;
typedef struct BehaviorProperty { BehaviorText name, value; } BehaviorProperty;
enum BehaviorStatus { BEHAVIOR_OK=0, BEHAVIOR_ERROR=1, BEHAVIOR_PANIC=2,
 BEHAVIOR_CALLBACK_ERROR=3, BEHAVIOR_STALE=5, BEHAVIOR_POISONED=6, BEHAVIOR_INVALID=7 };
typedef struct BehaviorOptional { uint32_t present; float value; } BehaviorOptional;
/* available-space tags: 1 definite(value), 3 min-content, 4 max-content */
typedef struct BehaviorScalar { uint32_t tag; float value; } BehaviorScalar;
typedef struct BehaviorSize { float width, height; } BehaviorSize;
typedef struct BehaviorKnown { BehaviorOptional width, height; } BehaviorKnown;
typedef struct BehaviorAvailable { BehaviorScalar width, height; } BehaviorAvailable;
typedef struct BehaviorRect { float left, right, top, bottom; } BehaviorRect;
/* content_size = actual Layout::content_box_size(); scroll_width/height are
 * actual native maximum scroll offsets, not content dimensions. */
typedef struct BehaviorLayout {
 uint32_t order; float x,y; BehaviorSize size,content_size,scrollbar_size;
 BehaviorRect scrollable_overflow,border,padding,margin;
 float scroll_width,scroll_height;
} BehaviorLayout;
/* style_json is the complete actual native Style supplied by Taffy to measure;
 * valid only during callback. Callback is the raw measure function (do NOT run
 * compute_leaf_layout again); native Taffy applies style constraints itself.
 * Return nonzero on failure. C++ exceptions MUST be caught before returning.
 * A NULL callback selects native zero intrinsic measurement (not a fallback).
 */
typedef uint32_t (*BehaviorMeasure)(void*,uint64_t,BehaviorKnown,BehaviorAvailable,BehaviorText,BehaviorSize*);
uint32_t behavior_oracle_create(BehaviorOracle**);
void behavior_oracle_free(BehaviorOracle*);
uint32_t behavior_oracle_error(BehaviorOracle*,char*,size_t,size_t*);
/* Properties use native FromStr syntax, same basis as XML oracle. Empty list
 * explicitly requests Style::default(). Unknown/duplicate properties fail.
 * Includes item-is-table/item-is-replaced/item-is-compressible-replaced bools
 * and grid-template-areas (native serde object {areas,row_count,column_count})
 * beyond XML properties.
 */
uint32_t behavior_oracle_new_node(BehaviorOracle*,const BehaviorProperty*,size_t,uint64_t*);
uint32_t behavior_oracle_set_style(BehaviorOracle*,uint64_t,const BehaviorProperty*,size_t);
/* Complete native serde Style JSON only: every serialized field required,
 * unknown/duplicate fields rejected, including nested fields. Non-finite
 * ordinary float fields fail JSON transport explicitly (never become null);
 * packed native length/track u64 JSON preserves non-finite float bits.
 * Obtain a full template via
 * style_json and modify it, or use properties plus set_native_field.
 */
uint32_t behavior_oracle_set_style_json(BehaviorOracle*,uint64_t,BehaviorText);
/* Clone the supplied complete native snapshot, change only one inset field,
 * then set_style. side 0=left,1=right,2=top,3=bottom; tag 0=auto,1=length,
 * 2=percent. Preserves all other fields, not a fixture-style reconstruction. */
uint32_t behavior_oracle_set_style_json_inset(BehaviorOracle*,uint64_t,BehaviorText snapshot,uint32_t side,uint32_t tag,float);
/* Typed native serde field replacement (e.g. grid_template_areas, named lines,
 * safety-bearing alignments, contain, replaced flags); unknown fields fail. */
uint32_t behavior_oracle_set_native_field(BehaviorOracle*,uint64_t,BehaviorText name,BehaviorText json_value);
uint32_t behavior_oracle_style_json(BehaviorOracle*,uint64_t,char*,size_t,size_t*);
uint32_t behavior_oracle_set_children(BehaviorOracle*,uint64_t,const uint64_t*,size_t);
uint32_t behavior_oracle_add_child(BehaviorOracle*,uint64_t,uint64_t);
uint32_t behavior_oracle_insert_child(BehaviorOracle*,uint64_t,size_t,uint64_t);
uint32_t behavior_oracle_remove_child(BehaviorOracle*,uint64_t,uint64_t);
uint32_t behavior_oracle_remove_node(BehaviorOracle*,uint64_t);
uint32_t behavior_oracle_parent(BehaviorOracle*,uint64_t,uint64_t*); /* 0 = no parent */
uint32_t behavior_oracle_children(BehaviorOracle*,uint64_t,uint64_t*,size_t,size_t*);
uint32_t behavior_oracle_mark_dirty(BehaviorOracle*,uint64_t);
uint32_t behavior_oracle_dirty(BehaviorOracle*,uint64_t,uint32_t*);
uint32_t behavior_oracle_hoisted_children(BehaviorOracle*,uint64_t,uint64_t*,size_t,size_t*);
/* Effective CoreStyle flags: is_replaced and is_compressible_replaced. */
uint32_t behavior_oracle_core_flags(BehaviorOracle*,uint64_t,uint32_t*,uint32_t*);
/* Typed non-finite-capable native setters for adversarial styles. dimension:
 * field 0=size,1=min_size,2=max_size,3=flex_basis(axis ignored for basis);
 * axis 0=width,1=height; tag 0=auto,1=length,2=percent,3=min-content,
 * 4=max-content,5=fit-content. min/max accept only tags 0,1,2.
 * grid_track_fr replaces one template Single track with auto/min + fr/max.
 * Its float is passed directly to native from_fr (NaN/Inf retained).
 */
uint32_t behavior_oracle_set_dimension(BehaviorOracle*,uint64_t,uint32_t field,uint32_t axis,uint32_t tag,float);
uint32_t behavior_oracle_set_grid_track_fr(BehaviorOracle*,uint64_t,uint32_t axis,size_t index,float);
uint32_t behavior_oracle_compute(BehaviorOracle*,uint64_t,BehaviorAvailable,uint32_t rounding,void*,BehaviorMeasure);
uint32_t behavior_oracle_layout(BehaviorOracle*,uint64_t,BehaviorLayout*);
uint32_t behavior_oracle_unrounded_layout(BehaviorOracle*,uint64_t,BehaviorLayout*);
/* JSON is null for DetailedLayoutInfo::None (not empty grid). Grid contains
 * rows/columns {negative_implicit_tracks,explicit_tracks,positive_implicit_tracks,
 * positions,collapsed_tracks,empty_axis_line,line_names,resolved_line_names}, items with native
 * row_start,row_end,column_start,column_end; all fields preserved.
 * empty_axis_line is null when absent; line_names is native stored explicit
 * names; resolved_line_names includes implicit empty groups. Additional native
 * derived strings/areas are available through tracks/item_grid_area/resolve_area.
 */
uint32_t behavior_oracle_detailed_json(BehaviorOracle*,uint64_t,char*,size_t,size_t*);
uint32_t behavior_oracle_tracks(BehaviorOracle*,uint64_t,uint32_t axis,char*,size_t,size_t*); /* 0 rows,1 cols; absent => "" */
uint32_t behavior_oracle_tracks_zoom(BehaviorOracle*,uint64_t,uint32_t axis,float zoom,char*,size_t,size_t*);
/* present=0 means no detailed grid / out of range, outputs zeroed. */
uint32_t behavior_oracle_item_grid_area(BehaviorOracle*,uint64_t,size_t index,uint32_t* present,BehaviorRect*);
/* Native CSS GridPlacement strings, row-start/end then column-start/end.
 * direction 0=LTR,1=RTL. Returns present=0 if detailed grid info absent. */
uint32_t behavior_oracle_resolve_area(BehaviorOracle*,uint64_t,BehaviorText,BehaviorText,BehaviorText,BehaviorText,uint32_t direction,BehaviorRect padding_box,uint32_t* present,BehaviorRect*);
/* Typed native detailed-grid queries: unlike optional helpers above, return
 * ERROR if node is not DetailedLayoutInfo::Grid; never fabricate empty grid. */
typedef struct BehaviorGridAxisInfo {
 uint32_t negative_implicit_tracks,explicit_tracks,positive_implicit_tracks,stored_names_empty;
 size_t position_count,iterator_count;
 BehaviorOptional empty_axis_line;
} BehaviorGridAxisInfo;
uint32_t behavior_oracle_grid_axis_info(BehaviorOracle*,uint64_t,uint32_t axis,BehaviorGridAxisInfo*);
uint32_t behavior_oracle_grid_position(BehaviorOracle*,uint64_t,uint32_t axis,size_t index,float* start,float* end);
/* mode 0=stored line_names.line(line),1=iter_line_names nth(line),
 * 2=actual names_for_line(line). Mode 1 rejects out-of-range iterator index. */
uint32_t behavior_oracle_grid_names_count(BehaviorOracle*,uint64_t,uint32_t axis,size_t line,uint32_t mode,size_t*);
uint32_t behavior_oracle_grid_name(BehaviorOracle*,uint64_t,uint32_t axis,size_t line,uint32_t mode,size_t index,char*,size_t,size_t*);
/* Uses the ACTUAL upstream tests/common test_measure_function with native
 * TestNodeContext and full Style; not C++ measurement or double constraints.
 * writing_mode 0=Horizontal,1=Vertical. Setting context marks native dirty. */
uint32_t behavior_oracle_set_ahem_context(BehaviorOracle*,uint64_t,BehaviorText,uint32_t writing_mode);
uint32_t behavior_oracle_set_aspect_context(BehaviorOracle*,uint64_t,float width,float height_ratio);
uint32_t behavior_oracle_context_count(BehaviorOracle*,uint64_t,size_t*);
uint32_t behavior_oracle_compute_test_measure(BehaviorOracle*,uint64_t,BehaviorAvailable,uint32_t rounding);
/* Exact operations of the two upstream serde source tests. The second is
 * intentionally Value parsing, NOT Style deserialization. */
uint32_t behavior_oracle_serde_default(BehaviorOracle*,char*,size_t,size_t*);
uint32_t behavior_oracle_serde_parse_value(BehaviorOracle*,BehaviorText);
/* Deterministic Rust panic injection. Returns PANIC and permanently poisons. */
uint32_t behavior_oracle_inject_panic(BehaviorOracle*);
#ifdef __cplusplus
}
/* Catch exceptions in C++, before they could cross an extern-C/Rust frame.
 * Pass behavior_measure_guard<YourCallable> and a pointer to YourCallable.
 * Callable signature matches BehaviorMeasure except for the user pointer. */
template<class Callable>
uint32_t behavior_measure_guard(void* user,uint64_t node,BehaviorKnown known,
 BehaviorAvailable available,BehaviorText style,BehaviorSize* out) noexcept {
 try { return (*static_cast<Callable*>(user))(node,known,available,style,out); }
 catch (...) { return BEHAVIOR_CALLBACK_ERROR; }
}
#endif
#endif
