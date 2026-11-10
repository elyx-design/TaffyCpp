#pragma once
#include <cstdint>
// Operation ABI. Handles are logical IDs, never client-dereferenced pointers.
// All non-null pointers must reference valid, aligned storage for the duration of
// the call. Callbacks/context must remain live until ct_destroy. Callbacks MUST
// NOT throw/unwind or reenter any ct_* function (registry lock is held).
// Only a container root with direct leaf children is supported, not nested trees.
extern "C" {
struct CtValue { std::uint32_t tag; float value; std::uint64_t calc; }; // auto=0,length=1,percent=2,fit-calc=3
struct CtAvailable { std::uint32_t tag; float value; }; // definite=0,min=1,max=2
struct CtSize { float width, height; };
struct CtStyle { CtValue width, height, min_width, min_height; };
struct CtMeasureInput { std::uint32_t known_width, known_height; float width, height; CtAvailable available_width, available_height; };
struct CtCounts { std::uint64_t calc, measure, cache_get, cache_hit, cache_store, cache_clear, hoist_clear, hoist_add; };
using CtResolve = float (*)(void*, float px, float pct, float basis);
using CtMeasure = CtSize (*)(void*, std::uint64_t node, CtMeasureInput);
// status: 0 success, 1 invalid argument/handle or poisoned tree, 2 caught Rust panic.
// Output unchanged on error. A tree-operation panic permanently disables that tree:
// only ct_destroy remains allowed. Independent trees remain usable.
int ct_create(CtResolve, CtMeasure, void*, std::uint64_t*);
int ct_destroy(std::uint64_t);
int ct_calc(std::uint64_t, float px, float pct, std::uint64_t*);
int ct_node(std::uint64_t, std::uint32_t kind, CtStyle, std::uint64_t*); // block=0,grid=1,leaf=2
int ct_children(std::uint64_t, std::uint64_t root, const std::uint64_t*, std::uint32_t);
int ct_style(std::uint64_t, std::uint64_t node, CtStyle);
int ct_track(std::uint64_t, std::uint64_t node, std::uint32_t axis, CtValue); // rows=0,columns=1; replaces the single fit-content track; limit length/percent/fit-calc
int ct_compute(std::uint64_t, std::uint64_t root, CtAvailable, CtAvailable);
int ct_layout(std::uint64_t, std::uint64_t node, CtSize*);
int ct_counts(std::uint64_t, CtCounts*);
int ct_panic_probe();
// Test-only: mutate a node's layout under the registry lock, then panic.
int ct_tree_panic_probe(std::uint64_t, std::uint64_t node);
}
