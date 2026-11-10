#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/* All buffers belong to the caller. No Rust layout or allocation crosses ABI.
 * Status: 0 success, 1 invalid input, 2 caught Rust panic, 3 insufficient output.
 * Sizing tags: auto=0 length=1 percent=2 fr=3 calc=4 min-content=5 max-content=6.
 * Output additionally supports fit-content length=7 percent=8 calc=9.
 * A calc handle must be non-null and 8-byte aligned; it is never dereferenced.
 * A null resolver uses the source helpers' constant 42.42 resolver.
 * Nonempty buffers must point to aligned readable/writable storage of stated
 * lengths and must not overlap. Outputs are valid only on success, except that
 * written reports required capacity when status is 3. Rust panics never unwind
 * through C; foreign callbacks must not throw or unwind through Rust.
 * Component modes: single=0 repeat-count=1 auto-fit=2 auto-fill=3.
 * axis: horizontal=0 vertical=1; strategy: max-fit=0 min-overflow=1.
 */
typedef struct { uint32_t tag; float value; const void *calc; } PsGridSizing;
typedef struct { PsGridSizing min, max; } PsGridTrackDef;
typedef struct { uint32_t mode, count; const PsGridTrackDef *tracks; size_t len; } PsGridComponent;
typedef struct {
    const PsGridComponent *columns; size_t columns_len;
    const PsGridComponent *rows; size_t rows_len;
    const PsGridTrackDef *auto_columns; size_t auto_columns_len;
    const PsGridTrackDef *auto_rows; size_t auto_rows_len;
    float width, height, min_width, min_height; /* NaN = auto */
    float gap_width, gap_height;
    float padding_left, padding_right, padding_top, padding_bottom;
} PsGridStyle;
typedef struct { uint16_t negative_implicit, explicit_count, positive_implicit; } PsGridCounts;
typedef struct { uint16_t repetitions, tracks; } PsGridExplicit;
typedef struct { uint32_t kind, collapsed; PsGridSizing min, max; } PsGridTrack; /* kind track=0 gutter=1 */
typedef struct { int32_t kind, value; } PsGridPlacement; /* auto=0 CSS line=1 span=2 */
typedef struct { uint64_t node; PsGridPlacement column_start, column_end, row_start, row_end; } PsGridChild;
typedef struct { uint64_t node; int16_t column_start, column_end, row_start, row_end; } PsGridPlaced;
typedef struct { int16_t min, max; uint16_t span; } PsGridBounds;
typedef float (*PsGridCalcResolver)(const void *, float);
int32_t ps_grid_explicit(const PsGridStyle *, uint32_t axis, uint32_t has_size, float size, uint32_t strategy, PsGridCalcResolver, PsGridExplicit *);
int32_t ps_grid_tracks(const PsGridStyle *, PsGridCounts, uint32_t axis, uint16_t repetitions, const uint8_t *has_items, size_t has_items_len, PsGridTrack *, size_t capacity, size_t *written);
int32_t ps_grid_child_bounds(PsGridPlacement start, PsGridPlacement end, uint16_t explicit_count, PsGridBounds *);
int32_t ps_grid_estimate(uint16_t columns, uint16_t rows, const PsGridChild *, size_t len, PsGridCounts *column_counts, PsGridCounts *row_counts);
/* Flow: Row=0 Column=1 RowDense=2 ColumnDense=3. Setup resolves named lines,
 * estimates occupancy dimensions, creates real GridItems, then calls place_grid_items. */
int32_t ps_grid_place(uint16_t columns, uint16_t rows, const PsGridChild *, size_t len, uint32_t flow, PsGridPlaced *, size_t capacity, size_t *written, PsGridCounts *column_counts, PsGridCounts *row_counts);
#ifdef __cplusplus
}
#endif
