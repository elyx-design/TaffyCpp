#ifndef TAFFY_CPP_CACHE_TEST_EXTENSIONS_H
#define TAFFY_CPP_CACHE_TEST_EXTENSIONS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Test-only observation of actual production Cache::cache_size, not a duplicate
 * frontend constant. Scalar has no ownership.
 * The shared cache_abi.h state/snapshot/mode observations are backed directly by
 * taffy::detail::CacheAccess. Snapshots own native measurement keys and sizes;
 * equality excludes final-layout entries, mode, cursor and recent-use metadata.
 * ts_cache_set_recently_used is explicit test access to the native recent bits
 * (uint32_t truncates to uint16_t, matching the reference ABI). No shadow state
 * or test header is used by the shipping cache implementation. */
uint64_t ts_cpp_cache_capacity(void);
#ifdef __cplusplus
}
#endif
#endif
