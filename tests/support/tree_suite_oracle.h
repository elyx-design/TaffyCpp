#ifndef TREE_SUITE_ORACLE_H
#define TREE_SUITE_ORACLE_H
#include "oracle_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Serialized, non-reentrant test-only native TaffyTree ABI. All borrowed spans
   must be valid for the call. No C++ exception may cross a callback boundary.
   Status codes/tags and scalar input/layout records are ORACLE_* from oracle_abi.h.
   PANIC and callback failure poison the tree; only free remains permitted.
   Logical handles never recycle, including across clear. 0 is no parent.
   Native Taffy Result errors return ERROR; bad/stale handles return STALE.
   Unknown properties / invalid CSS values return UNSUPPORTED, never default.
   Styles are full replacements starting from Style::default(). Properties use
   upstream CSS FromStr: width:10px, width:100%, grid-column-start:1,
   grid-template-columns:100px auto, minmax(0px, 1fr), etc. All Style fields
   are supported (boolean item-is-* included); duplicate keys are rejected.
   grid-template-{rows,columns} includes named lines. grid-template-areas uses
   CSS quoted rows. grid-template-{row,column}-names accepts JSON nested arrays.
   The three grid-template representations may not conflict. */
typedef struct TsTree TsTree;
typedef struct { const char* data; size_t len; } TsText;
typedef struct { TsText name, value; } TsProperty;
typedef OracleLayoutInput TsInput;
typedef OracleLayout TsLayout;
typedef struct {
    OracleLayoutOutput value;
    /* Optional owning native LayoutOutput, returned by ts_leaf. Overrides value;
       transferred to compute on callback return (including failure). Outside a
       callback release with ts_output_free. Never copy an owning token. */
    void* native_output;
} TsOutput;
/* style is a borrowed opaque native Style, valid only during this callback.
   To reproduce upstream callbacks using compute_leaf_layout, call ts_leaf here;
   do NOT run a frontend leaf algorithm. count is tracked per context invocation. */
typedef uint32_t (*TsMeasure)(void*, uint64_t, const TsInput*, const void* style, TsOutput*);
typedef OracleIntrinsic TsIntrinsic;
typedef OracleDrop TsDrop;
uint32_t ts_create(size_t capacity, TsTree**); /* SIZE_MAX selects TaffyTree::new */
uint32_t ts_free(TsTree*);
/* Frees (including poisoned trees), then reports total actual Rust context
   drops, including those during free. Requires nonnull tree/output. */
uint32_t ts_free_report(TsTree*, uint64_t* drops);
uint32_t ts_new_leaf(TsTree*, const TsProperty*, size_t, uint64_t*);
uint32_t ts_new_leaf_with_context(TsTree*, const TsProperty*, size_t, void*, TsMeasure, TsDrop, uint64_t*);
uint32_t ts_new_with_children(TsTree*, const TsProperty*, size_t, const uint64_t*, size_t, uint64_t*);
/* Context ownership transfers on successful new/set only. Drop is native Rust
   Drop, exactly once on replace/remove/clear/free. Null/null/null clears it. */
uint32_t ts_set_context(TsTree*, uint64_t, void*, TsMeasure, TsDrop);
uint32_t ts_get_context(TsTree*, uint64_t, uint32_t* present, void** user, uint64_t* calls);
uint32_t ts_reset_context_calls(TsTree*, uint64_t);
uint32_t ts_remove(TsTree*, uint64_t, uint64_t* removed);
uint32_t ts_clear(TsTree*);
uint32_t ts_count(TsTree*, size_t*);
/* Counts actual Rust context Drop calls, independent of the foreign drop hook. */
uint32_t ts_drop_count(TsTree*, uint64_t*);
/* Requires generated upstream TaffyTree::tree_suite_capacities(&self)
   -> (usize,usize,usize), returning nodes/children/parents.capacity(). */
uint32_t ts_capacities(TsTree*, size_t* nodes, size_t* children, size_t* parents);
uint32_t ts_set_style(TsTree*, uint64_t, const TsProperty*, size_t);
uint32_t ts_style_equals(TsTree*, uint64_t, const TsProperty*, size_t, uint32_t*);
/* Debug representation of native Style; NUL terminated, required includes NUL.
   Null/0 queries size. Error details include original native TaffyError Debug. */
uint32_t ts_style_debug(TsTree*, uint64_t, char*, size_t, size_t*);
uint32_t ts_style_display(TsTree*, uint64_t, uint32_t*); /* native display tag */
uint32_t ts_print_tree(TsTree*, uint64_t);
uint32_t ts_error(TsTree*, char*, size_t, size_t*); /* available after poison */
uint32_t ts_add_child(TsTree*, uint64_t parent, uint64_t child);
uint32_t ts_insert_child(TsTree*, uint64_t parent, size_t index, uint64_t child);
uint32_t ts_set_children(TsTree*, uint64_t parent, const uint64_t*, size_t);
uint32_t ts_remove_child(TsTree*, uint64_t parent, uint64_t child, uint64_t*);
uint32_t ts_remove_child_at(TsTree*, uint64_t parent, size_t index, uint64_t*);
uint32_t ts_replace_child(TsTree*, uint64_t parent, size_t index, uint64_t child, uint64_t* old_child);
/* Bounds: 0 unbounded, 1 included, 2 excluded. Invalid ranges execute native
   operation and return PANIC (poison), not a fabricated Result error. */
uint32_t ts_remove_children_range(TsTree*, uint64_t parent, uint32_t start_kind, size_t start, uint32_t end_kind, size_t end);
uint32_t ts_parent(TsTree*, uint64_t, uint64_t*);
uint32_t ts_child(TsTree*, uint64_t parent, size_t index, uint64_t*);
uint32_t ts_child_count(TsTree*, uint64_t parent, size_t*);
uint32_t ts_children(TsTree*, uint64_t, uint64_t*, size_t capacity, size_t* count);
uint32_t ts_dirty(TsTree*, uint64_t, uint32_t*);
uint32_t ts_mark_dirty(TsTree*, uint64_t);
uint32_t ts_rounding(TsTree*, uint32_t enabled);
uint32_t ts_compute(TsTree*, uint64_t, OracleSizing, uint32_t with_measure);
/* Full translated measure closure: per-context callback when present; otherwise
   this fallback handles uncontexted leaves too. Borrowed fallback user is not
   retained/dropped. Call fallback->ts_leaf with a no-context intrinsic helper. */
uint32_t ts_compute_with_measure(TsTree*, uint64_t, OracleSizing, void* fallback_user, TsMeasure fallback);
uint32_t ts_layout(TsTree*, uint64_t, uint32_t unrounded, TsLayout*);
uint32_t ts_leaf(const void* native_style, const TsInput*, void*, TsIntrinsic, TsOutput*);
uint32_t ts_output_free(TsOutput*);
uint32_t ts_test_panic(TsTree*);
#ifdef __cplusplus
}
#endif
#endif
