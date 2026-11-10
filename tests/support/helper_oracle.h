#ifndef TAFFY_HELPER_ORACLE_H
#define TAFFY_HELPER_ORACLE_H
#include "oracle_abi.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* 0=no context, 1=zero, 2=fixed, 3=aspect, 4=Ahem text. */
    uint32_t kind;
    /* Fixed: width,height. Aspect: width,height/width ratio. */
    float width, height_ratio_or_height;
    /* UTF-8 bytes, not NUL terminated. NULL allowed only with length 0.
       Validated for every kind; embedded NUL bytes are preserved. */
    const uint8_t* text;
    size_t text_length;
    /* 0=horizontal, 1=vertical (validated for every kind). */
    uint32_t vertical;
    OracleLayoutInput input;
} HelperOracleRequest;

typedef struct {
    OracleSize size;
    /* Upstream TestNodeContext.count, reset for each call; 0 without context. */
    size_t count;
} HelperOracleResult;

/* Calls upstream test_measure_function with Style::default() ONLY.
   Returns ORACLE_OK, ORACLE_UNSUPPORTED (unknown tags/invalid booleans),
   ORACLE_INVALID (NULL/misaligned/overlapping records, bad text length/UTF-8),
   or ORACLE_PANIC (caught Rust unwind). Result is unchanged on failure.
   Non-NULL pointers must reference live allocations of the indicated size;
   output must be writable and must not overlap request or text storage.
   Arbitrary dangling/inaccessible pointers cannot be detected. No pointers
   are retained. No adapter context, callback, or tree is involved. */
uint32_t helper_oracle_measure(const HelperOracleRequest*, HelperOracleResult*);

#ifdef __cplusplus
}
#endif
#endif
