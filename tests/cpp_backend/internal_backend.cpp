// Test-only native implementation of the two legacy internal-oracle entry points.
#include "../support/internal_oracle.h"

#include "../../src/grid.hpp"

extern "C" int32_t internal_oracle_advance_position(
    const InternalOracleAdvanceInput* input, InternalOracleAdvanceOutput* output) {
    if (output) *output = {};
    if (!input || !output) return INTERNAL_ORACLE_INVALID_ARGUMENT;
    output->position = static_cast<int16_t>(taffy::grid::advance_position(input->position));
    return INTERNAL_ORACLE_OK;
}

extern "C" int32_t internal_oracle_resolve_indefinite_grid_span(
    const InternalOracleSpanInput* input, InternalOracleSpanOutput* output) {
    if (output) *output = {};
    if (!input || !output) return INTERNAL_ORACLE_INVALID_ARGUMENT;
    const auto span = taffy::grid::resolve_indefinite_grid_span(input->position, input->span);
    output->start = static_cast<int16_t>(span.start);
    output->end = static_cast<int16_t>(span.end);
    return INTERNAL_ORACLE_OK;
}
