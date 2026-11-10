#pragma once
#include <cstddef>
#include <cstdint>

// Status 0 = success, 1 = invalid input/native error, 2 = caught Rust panic.
// All pointers must reference aligned live storage of the stated capacity.
// Buffers are caller-owned; strings are byte spans, not NUL-terminated.
// A caught panic invalidates every involved object: reuse returns 1, but drop
// still succeeds once. Other handles remain usable. Builder finish consumes its
// live handle even on capacity error (on panic it leaves a destroyable tombstone).
// Style handles are thread-local; use/drop them on their creating thread.
extern "C" {
int32_t ps_misc_error(uint8_t*, size_t, size_t*);
// One-shot thread-local fault: 0 clear, 1 extracted pointer, 2 from_edge anchor,
// 3 omit constructor children, 4 builder push panic, 5 style property panic,
// 6 style equality panic (invalidates both operands). Supplemental regressions only.
int32_t ps_misc_inject_fault(uint32_t);
// Payload kind native i32/owned vector of i32/byte = 0/1/2.
// The transport is i64; inputs outside the native scalar range are rejected.
int32_t ps_misc_builder_new(size_t, uint32_t payload_kind, uint64_t*);
int32_t ps_misc_builder_push(uint64_t, uint32_t back, const int64_t*, size_t);
int32_t ps_misc_builder_finish(uint64_t, int64_t*, size_t, size_t*, size_t, size_t*, size_t*);
int32_t ps_misc_builder_drop(uint64_t);
int32_t ps_misc_balance(const float*, size_t, float, float, size_t, size_t*, size_t, size_t*);
// Edge codes start/end/center = 0/1/2. Safety default/safe/unsafe = 0/1/2.
int32_t ps_misc_static_offset(float, float, uint32_t, uint32_t, uint32_t, float, float, float, float*);
// Original AxisStaticPosition::from_edge(anchor, edge), then resolve offset.
int32_t ps_misc_static_from_edge(float anchor, uint32_t edge, float width, float left, float right, float*);
// Parent -1 denotes a root; display none/flex = 0/1. Output per node: w,h,x,y.
int32_t ps_misc_hidden(const int32_t*, const uint32_t*, const float*, const float*, size_t, size_t, float*, size_t);
// Same operation with optional final output of n native tree child counts.
int32_t ps_misc_hidden_with_counts(const int32_t*, const uint32_t*, const float*, const float*, size_t, size_t, float*, size_t, size_t*);
// Directions row/row-reverse/column/column-reverse = 0/1/2/3.
// Result bits row/column/reverse = 1/2/4.
int32_t ps_misc_flex_direction(uint32_t, uint32_t*);
// Content keywords normal/start/end/flex-start/flex-end/center/stretch/
// space-between/space-evenly/space-around = 0..9.
int32_t ps_misc_reverse_content(uint32_t, uint32_t*);
int32_t ps_misc_style_new(uint32_t factory, uint64_t*); // Default=0, DEFAULT=1
int32_t ps_misc_style_property(uint64_t, uint32_t property, float value);
int32_t ps_misc_style_equal(uint64_t, uint64_t, uint32_t*);
int32_t ps_misc_style_drop(uint64_t);
int32_t ps_misc_type_size(uint32_t type, size_t*);
// Kind: length,auto,fr,min-content,max-content,fit-px,fit-keyword,stretch,
// content,percent,fit-percent,calc,fit-calc = 0..12; raw nonpointer tag = 13.
int32_t ps_misc_compact_bits(size_t*, size_t);
int32_t ps_misc_compact_tag(uint32_t, size_t*);
// Flags: length/percentage,sizing-keyword,uses-percentage,plain-calc,
// fit-calc,has-calc,fit-content,intrinsic,max-or-fit,max-content-alike,auto
// = bits 0..10. C++ owns the nonnull, 8-byte-aligned opaque calc pointer;
// native never dereferences/frees it. Last output is native calc_value() (or null
// for non-calc), compared to the original pointer by C++, never a Rust boolean.
int32_t ps_misc_compact(uint32_t, float, size_t raw_tag, const void*, size_t*, uint32_t*, const void**);
// content=0 items,1 content; operation=0 serialize,1 deserialize. Items keyword
// codes normal/start/end/flex-start/flex-end/center/baseline/stretch/self-start/
// self-end = 0..9. Returns native serialized JSON and decoded keyword/safety.
// Requires root crate feature `serde` -> taffy/serde plus serde_json dependency.
int32_t ps_misc_has_serde(uint32_t*);
int32_t ps_misc_alignment_json(uint32_t, uint32_t, uint32_t, uint32_t,
    const uint8_t*, size_t, uint8_t*, size_t, size_t*, uint32_t*, uint32_t*);
}
