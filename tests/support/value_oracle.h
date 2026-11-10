#pragma once
// Test-only ABI for Taffy v0.15.1, 0064bd54cbe38bf1d8bcd6ec6ab0704d45fb048a.
// All buffers remain caller-owned and must be valid, aligned, and correctly sized.
// No C++ callbacks or exceptions cross Rust; every export catches Rust unwinds.
// The operation-specific concrete records/declarations are in the included test
// fragments, adjacent to their semantic tag definitions (never Rust enum ABI).
#include <cstddef>
#include <cstdint>

namespace value_oracle {
enum class Status : std::int32_t { ok = 0, invalid_input = 1, domain_error = 2, panic = 3 };
// Constructor operations, never source-test/scenario identifiers.
enum class DimensionComposite : std::uint32_t { components = 0, auto_value = 1, lengths = 2, percents = 3 };
enum class OptionalSizeConstructor : std::uint32_t { components = 0, none = 1, new_value = 2 };
enum class FloatRectConstructor : std::uint32_t { new_value = 0, zero = 1 };
inline constexpr const char* upstream_commit = "0064bd54cbe38bf1d8bcd6ec6ab0704d45fb048a";
}
extern "C" std::int32_t vo_contain_parse(const std::uint8_t*, std::size_t, std::uint32_t*);
