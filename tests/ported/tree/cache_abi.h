#ifndef TREE_SUITE_CACHE_ABI_H
#define TREE_SUITE_CACHE_ABI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Test-only ABI. All pointers must be live, aligned, non-overlapping and valid
 * for the declared object/buffer sizes. Handles are owned, thread-confined and
 * must be freed once. No Rust/C++ layouts or enum discriminants cross the ABI.
 * Status: 0 OK, 1 invalid argument, 2 caught Rust panic, 3 insufficient candidate
 * capacity, 4 unavailable in this upstream build. Boolean integers are 0/1.
 * Methods are primitive operations; scenarios and assertions live in Catch3.
 */
enum { TS_CACHE_OK=0, TS_CACHE_INVALID=1, TS_CACHE_PANIC=2,
       TS_CACHE_CAPACITY=3, TS_CACHE_UNAVAILABLE=4 };
enum { TS_CACHE_LAYOUT=0, TS_CACHE_MEASURE=1, TS_CACHE_HIDDEN=2 };
enum { TS_CACHE_CONTENT_SIZE=0, TS_CACHE_INHERENT_SIZE=1 };
enum { TS_CACHE_HORIZONTAL=0, TS_CACHE_VERTICAL=1, TS_CACHE_BOTH=2 };
enum { TS_CACHE_DEFINITE=0, TS_CACHE_MIN_CONTENT=1, TS_CACHE_MAX_CONTENT=2 };
enum { TS_CACHE_START=0, TS_CACHE_CENTER=1, TS_CACHE_END=2 };
enum { TS_CACHE_DEFAULT_SAFETY=0, TS_CACHE_SAFE=1, TS_CACHE_UNSAFE=2 };
enum { TS_CACHE_RELATIVE=0, TS_CACHE_ABSOLUTE=1, TS_CACHE_FIXED=2,
       TS_CACHE_STATIC=3, TS_CACHE_STICKY=4 };
typedef struct TsCache TsCache;
typedef struct TsCacheSnapshot TsCacheSnapshot;
typedef struct { float value; uint32_t present; } TsCacheOptional;
typedef struct { float value; uint32_t kind; } TsCacheAvailable;
typedef struct {
    uint32_t run_mode, sizing_mode, axis;
    TsCacheOptional known_dimensions[2]; /* width, height */
    uint32_t known_dimensions_are_definite[2];
    TsCacheOptional parent_size[2];
    TsCacheAvailable available_space[2];
    uint32_t vertical_margins_are_collapsible[2]; /* start, end */
} TsCacheInput;
typedef struct {
    float area_start, area_end;
    uint32_t keyword, safety, fallback;
} TsCacheStaticPosition;
typedef struct {
    uint64_t node;
    uint32_t order, position;
    TsCacheStaticPosition static_position[2]; /* x, y */
} TsCacheCandidate;
typedef struct {
    float size[2];
    float scrollable_overflow_rect[4]; /* left, right, top, bottom */
    TsCacheOptional baselines[2]; /* first, last */
    float top_margin[2], bottom_margin[2]; /* positive, negative */
    uint32_t margins_can_collapse_through;
    TsCacheCandidate *oof_candidates;
    uint64_t oof_count, oof_capacity;
    uint32_t oof_positioning_area_present;
    float oof_positioning_size[2], oof_positioning_offset[2];
} TsCacheOutput;
typedef struct {
    uint32_t measure_count, has_final, recently_used, next_measure_entry, is_empty;
} TsCacheState;
int32_t ts_cache_new(TsCache **out);
int32_t ts_cache_free(TsCache *cache);
int32_t ts_cache_store(TsCache *cache, const TsCacheInput *input, const TsCacheOutput *output);
/* get writes found=0/1; on miss output is untouched. On capacity failure,
 * found=1 and oof_count reports required capacity; the lookup already occurred. */
int32_t ts_cache_get(TsCache *cache, const TsCacheInput *input, TsCacheOutput *output, uint32_t *found);
int32_t ts_cache_clear(TsCache *cache, uint32_t *cleared);
int32_t ts_cache_state(const TsCache *cache, TsCacheState *out);
int32_t ts_cache_set_recently_used(TsCache *cache, uint32_t bits);
/* Owned snapshot compares actual native measurement entries (keys + content),
 * not a lossy fingerprint; cursor/recently-used bits are deliberately excluded. */
int32_t ts_cache_snapshot(const TsCache *cache, TsCacheSnapshot **out);
int32_t ts_cache_snapshot_free(TsCacheSnapshot *snapshot);
int32_t ts_cache_measure_entries_equal(const TsCache *cache, const TsCacheSnapshot *snapshot, uint32_t *equal);
int32_t ts_cache_mode_bits(const TsCacheInput *input, uint32_t *bits);
/* Returns UNAVAILABLE unless upstream has both debug_assertions and std. */
int32_t ts_cache_mode_change_evictions(uint64_t *out);
/* Native constructors exposed as primitives, including full HIDDEN input. */
int32_t ts_cache_hidden_input(TsCacheInput *out);
int32_t ts_cache_output_from_outer_size(float width, float height, TsCacheOutput *out);
int32_t ts_cache_margin_from_margin(float margin, float out_positive_negative[2]);
#ifdef __cplusplus
}
#endif
#endif
