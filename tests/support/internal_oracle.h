#ifndef TAFFY_TEST_INTERNAL_ORACLE_H
#define TAFFY_TEST_INTERNAL_ORACLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Native test-only ABI. All coordinate/span fields deliberately retain the
 * upstream i16/u16 domains. No Rust-layout types or bools cross this boundary.
 * Callers supply valid, aligned, writable output pointers (not overlapping
 * input); null pointers return INVALID_ARGUMENT. Output is zero on failure.
 * No allocation ownership crosses the ABI; no unwind crosses the ABI.
 */
enum InternalOracleStatus {
    INTERNAL_ORACLE_OK = 0,
    INTERNAL_ORACLE_INVALID_ARGUMENT = 1,
    INTERNAL_ORACLE_PANIC = 2
};
typedef struct InternalOracleAdvanceInput {
    int16_t position;
} InternalOracleAdvanceInput;
typedef struct InternalOracleAdvanceOutput {
    int16_t position;
} InternalOracleAdvanceOutput;
typedef struct InternalOracleSpanInput {
    int16_t position;
    uint16_t span;
} InternalOracleSpanInput;
typedef struct InternalOracleSpanOutput {
    int16_t start;
    int16_t end;
} InternalOracleSpanOutput;

int32_t internal_oracle_advance_position(const InternalOracleAdvanceInput* input,
                                         InternalOracleAdvanceOutput* output);
int32_t internal_oracle_resolve_indefinite_grid_span(const InternalOracleSpanInput* input,
                                                     InternalOracleSpanOutput* output);

#ifdef __cplusplus
}
static_assert(sizeof(InternalOracleAdvanceInput) == 2);
static_assert(sizeof(InternalOracleAdvanceOutput) == 2);
static_assert(sizeof(InternalOracleSpanInput) == 4);
static_assert(sizeof(InternalOracleSpanOutput) == 4);
#endif
#endif
