#ifndef TAFFY_TEST_XML_ORACLE_H
#define TAFFY_TEST_XML_ORACLE_H
#include "oracle_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Native persistent XML tree. No adapter dependency. Serialized calls only;
   valid/aligned pointers required. No callback may throw or reenter this tree.
   Status values are ORACLE_* from oracle_abi.h. Strings are UTF-8 byte spans;
   null+0 means absent only where documented. Handles belong to one tree. */
typedef struct XmlOracle XmlOracle;
typedef struct {
    const char* data;
    size_t len;
} XmlOracleString;
typedef struct {
    XmlOracleString name, value;
} XmlOracleStylePair;
typedef struct {
    OracleLayout layout;
    OracleSize scroll_size;
    uint32_t has_scroll_size;
    uint32_t has_resolved_tracks;
} XmlOracleLayout;
/* Intrinsic callback receives exactly native compute_leaf_layout's known
   dimensions and available space, using OracleKnown/OracleSizing tags.
   The C++ runner MUST call its existing C++ measurement helper's intrinsic
   primitive here (expose/reuse that primitive, not test_measure_function's
   adapter leaf wrapper). Native Rust owns the FULL parsed Style and performs
   compute_leaf_layout, so no style is flattened or lost across the ABI.
   user is borrowed until free; callback receives node handle to look up the
   C++ TestNodeContext. Return nonzero on failure; never throw across C ABI.
   Callbacks are invoked even for absent context/both-known dimensions: keep
   existing C++ intrinsic short circuits and count semantics unchanged. */
typedef uint32_t (*XmlOracleIntrinsic)(void* user, uint64_t node, OracleKnown known,
                                       OracleSizing available, OracleSize* out);
uint32_t xml_oracle_create(XmlOracle** out);
uint32_t xml_oracle_free(XmlOracle* tree);
/* Error string copied, NUL-terminated when capacity>0. required includes NUL.
   Error state remains valid until the next tree operation (except error()). */
uint32_t xml_oracle_error(XmlOracle* tree, char* out, size_t capacity, size_t* required);
/* Validate the ORIGINAL document byte span using upstream's pinned roxmltree
   Document::parse defaults, before any foreign XML parsing/normalization.
   No tree mutation. null+0 is absent (INVALID); non-null empty is malformed
   XML (ERROR). Invalid UTF-8 => INVALID; malformed XML => ERROR; details via
   xml_oracle_error(). Success does not validate fixture-specific semantics. */
uint32_t xml_oracle_validate_xml(XmlOracle* tree, XmlOracleString xml);
/* Real upstream thread-local cache-mode eviction counter, not per-tree state.
   Both outputs required. Debug: available=1 and count is the native snapshot.
   Release: available=0,count=0 (unavailable, NOT a validated zero).
   Snapshot immediately before/after first compute on a fresh tree, on the same
   thread, to reproduce upstream tests/xml.rs's debug-only equality assertion.
   Existing nonempty caches may legitimately change modes on later computes. */
uint32_t xml_oracle_cache_mode_change_evictions(XmlOracle* tree, uint64_t* out_count,
                                                uint32_t* out_available);
/* build_style exactly mirrors upstream tests/xml.rs. Unknown keys fail loudly;
   writing-mode is explicitly permitted but handled only in C++ measurement.
   Invalid native FromStr values fail; invalid aspect-ratio maps to None.
   Duplicate keys use the first occurrence. Nodes start with no children. */
uint32_t xml_oracle_new_node(XmlOracle* tree, const XmlOracleStylePair* pairs, size_t count,
                             uint64_t* out_node);
uint32_t xml_oracle_set_children(XmlOracle* tree, uint64_t node, const uint64_t* children,
                                 size_t count);
/* null+0 available width/height => MaxContent; rounding must be 0 or 1.
   Native AvailableSpace::from_str handles non-absent viewport attributes.
   Callback/user are borrowed only for this compute, not retained afterward. */
uint32_t xml_oracle_compute(XmlOracle* tree, uint64_t root, XmlOracleString available_width,
                            XmlOracleString available_height, uint32_t rounding, void* user,
                            XmlOracleIntrinsic callback);
uint32_t xml_oracle_layout(XmlOracle* tree, uint64_t node, XmlOracleLayout* out);
/* axis: rows=0,columns=1. Returns full native grid_template_* string, or
   empty string for non-grid. required includes NUL. Query length using
   out=NULL,capacity=0. Strings include named lines. */
uint32_t xml_oracle_tracks(XmlOracle* tree, uint64_t node, uint32_t axis, char* out,
                           size_t capacity, size_t* required);
#ifdef __cplusplus
}
#endif
#endif
