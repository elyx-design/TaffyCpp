#ifndef TAFFY_TEST_ORACLE_ABI_H
#define TAFFY_TEST_ORACLE_ABI_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Test-only ABI v1. All tags are explicit; unknown tags return UNSUPPORTED.
   Pointers must be valid/aligned, calls serialized, no reentry on a context. */
typedef struct OracleContext OracleContext;
enum {
    ORACLE_OK = 0,
    ORACLE_ERROR = 1,
    ORACLE_PANIC = 2,
    ORACLE_CALLBACK_FAILURE = 3,
    ORACLE_UNSUPPORTED = 4,
    ORACLE_STALE = 5,
    ORACLE_POISONED = 6,
    ORACLE_INVALID = 7
};
/* Dimension: auto=0,length=1,percent=2,min-content=3,max-content=4,
   fit-content=5,fit-content-px=6,stretch=7,content=8,fit-content-percent=9.
   calc=10 and fit-content-calc=11 explicitly unsupported (no resolver seam).
   LengthPercentage(Auto) fields accept only length/percent (and auto).
   Available space: definite=1,min-content=3,max-content=4. */
typedef struct {
    uint32_t tag;
    float value;
} OracleScalar;
typedef struct {
    uint32_t present;
    float value;
} OracleOptional;
typedef struct {
    float width, height;
} OracleSize;
typedef struct {
    float left, right, top, bottom;
} OracleRect;
typedef struct {
    OracleScalar width, height;
} OracleSizing;
typedef struct {
    OracleScalar left, right, top, bottom;
} OracleEdges;
typedef struct {
    OracleOptional width, height;
} OracleKnown;
/* Optional alignment tags: 0 omits the property (native default, and None
   for align-self/justify-self). Explicit NORMAL is distinct from omitted self.
   Safety modifiers and unknown tags are unsupported, never silently defaulted. */
enum {
    ORACLE_ALIGN_DEFAULT = 0,
    ORACLE_ALIGN_START = 1,
    ORACLE_ALIGN_END = 2,
    ORACLE_ALIGN_FLEX_START = 3,
    ORACLE_ALIGN_FLEX_END = 4,
    ORACLE_ALIGN_CENTER = 5,
    ORACLE_ALIGN_BASELINE = 6,
    ORACLE_ALIGN_STRETCH = 7,
    ORACLE_ALIGN_NORMAL = 8,
    ORACLE_ALIGN_SELF_START = 9,
    ORACLE_ALIGN_SELF_END = 10
};
enum {
    ORACLE_CONTENT_DEFAULT = 0,
    ORACLE_CONTENT_START = 1,
    ORACLE_CONTENT_END = 2,
    ORACLE_CONTENT_FLEX_START = 3,
    ORACLE_CONTENT_FLEX_END = 4,
    ORACLE_CONTENT_CENTER = 5,
    ORACLE_CONTENT_STRETCH = 6,
    ORACLE_CONTENT_SPACE_BETWEEN = 7,
    ORACLE_CONTENT_SPACE_EVENLY = 8,
    ORACLE_CONTENT_SPACE_AROUND = 9,
    ORACLE_CONTENT_NORMAL = 10
};
typedef struct {
    OracleSizing size, min_size, max_size;
    OracleEdges padding, border, margin, inset;
    OracleSizing gap;
    OracleOptional aspect_ratio;
    OracleScalar flex_basis;
    float flex_grow, flex_shrink, scrollbar_width;
    /* display flex=0,grid=1,block=2,none=3,flow-root=4;
       position relative=0,absolute=1,fixed=2,static=3,sticky=4;
       box-sizing border=0,content=1; overflow visible=0,hidden=1,scroll=2,clip=3;
       flex-direction row=0,column=1,row-reverse=2,column-reverse=3;
       wrap no-wrap=0,wrap=1,wrap-reverse=2,balance=3; direction ltr=0,rtl=1. */
    uint32_t display, position, box_sizing, overflow_x, overflow_y, flex_direction, flex_wrap,
        direction;
    /* Items/self use ORACLE_ALIGN_*; content uses ORACLE_CONTENT_*. */
    uint32_t align_items, align_self, justify_items, justify_self, align_content, justify_content;
} OracleStyle;
/* Explicit non-repeated tracks only; min/max accept length=1, percent=2.
   Arrays are borrowed for the call; the native Style owns its copied tracks. */
typedef struct {
    OracleScalar min, max;
} OracleGridTrack;
typedef struct {
    /* run-mode perform=0,size=1,hidden=2; sizing content=0,inherent=1;
       axis horizontal=0,vertical=1,both=2; booleans must be 0/1. */
    uint32_t run_mode, sizing_mode, axis;
    OracleKnown known_dimensions;
    uint32_t definite_width, definite_height;
    OracleKnown parent_size;
    OracleSizing available_space;
    uint32_t collapse_start, collapse_end;
} OracleLayoutInput;
typedef struct {
    OracleSize size;
    OracleRect scrollable_overflow;
    OracleOptional first_baseline, last_baseline;
    /* Each margin set retains positive and negative components, not just sum. */
    float top_positive, top_negative, bottom_positive, bottom_negative;
    uint32_t margins_can_collapse_through;
    /* v1 only accepts 0 here. Nonempty OOF metadata explicitly rejected. */
    uint32_t oof_candidates_count, oof_area_present;
} OracleLayoutOutput;
typedef struct {
    uint32_t order;
    float x, y;
    OracleSize size, content_size, scrollbar_size;
    OracleRect scrollable_overflow, border, padding, margin;
    /* Results of native Layout methods, not recomputed by C++. */
    float scroll_width, scroll_height;
} OracleLayout;
/* Return 0 on success, nonzero on failure. Never throw/unwind from callbacks.
   Layout/style/input pointers are borrowed only for callback duration. */
typedef uint32_t (*OracleMeasure)(void*, uint64_t, const OracleLayoutInput*, const OracleStyle*,
                                  OracleLayoutOutput*);
typedef uint32_t (*OracleIntrinsic)(void*, OracleKnown, OracleSizing, OracleSize*);
typedef void (*OracleDrop)(void*);
uint32_t oracle_create(OracleContext**);
uint32_t oracle_free(OracleContext*);
uint32_t oracle_default_style(OracleStyle*);
/* Ownership of user transfers ONLY on successful new/set-context. Drop runs once
   on successful replacement/removal/clear, or on free. Poisoned trees reject
   mutations; free still releases every retained callback context exactly once. */
uint32_t oracle_new_node(OracleContext*, const OracleStyle*, void*, OracleMeasure, OracleDrop,
                         uint64_t*);
uint32_t oracle_set_context(OracleContext*, uint64_t, void*, OracleMeasure, OracleDrop);
uint32_t oracle_remove(OracleContext*, uint64_t);
uint32_t oracle_clear(OracleContext*);
/* Full replacement of the narrow scalar style; clears separately set columns. */
uint32_t oracle_set_style(OracleContext*, uint64_t, const OracleStyle*);
uint32_t oracle_set_grid_template_columns(OracleContext*, uint64_t, const OracleGridTrack*, size_t);
/* Query count with null/0; otherwise copy to caller-owned capacity. */
uint32_t oracle_get_grid_template_columns(OracleContext*, uint64_t, OracleGridTrack*, size_t,
                                          size_t*);
uint32_t oracle_position_is_positioned(uint32_t, uint32_t*);
uint32_t oracle_position_is_out_of_flow(uint32_t, uint32_t*);
uint32_t oracle_get_style(OracleContext*, uint64_t, OracleStyle*);
uint32_t oracle_set_children(OracleContext*, uint64_t, const uint64_t*, size_t);
uint32_t oracle_parent(OracleContext*, uint64_t, uint64_t*); /* 0 means no parent */
uint32_t oracle_child(OracleContext*, uint64_t, size_t, uint64_t*);
uint32_t oracle_child_count(OracleContext*, uint64_t, size_t*);
uint32_t oracle_mark_dirty(OracleContext*, uint64_t);
uint32_t oracle_dirty(OracleContext*, uint64_t, uint32_t*);
uint32_t oracle_rounding(OracleContext*, uint32_t);
uint32_t oracle_compute(OracleContext*, uint64_t, OracleSizing);
uint32_t oracle_layout(OracleContext*, uint64_t, uint32_t unrounded, OracleLayout*);
/* Backend-native leaf sizing around a C++ intrinsic callback. Independent of
   tree context, safe to call inside OracleMeasure. */
uint32_t oracle_leaf(const OracleStyle*, const OracleLayoutInput*, void*, OracleIntrinsic,
                     OracleLayoutOutput*);
/* Deliberate bounded unwind proof; poisons the context. */
uint32_t oracle_test_panic(OracleContext*);
#ifdef __cplusplus
}
#endif
#endif
