#pragma once
#include "../support/xml_oracle.h"
#include <cstddef>
#include <string>
#include <unordered_map>

namespace xml_test {
struct TextContext {
    std::string text;
    bool vertical = false;
    std::size_t count = 0;
};
using Contexts = std::unordered_map<uint64_t, TextContext>;
uint32_t measure(void* user, uint64_t node, OracleKnown known, OracleSizing available,
                 OracleSize* out) noexcept;
} // namespace xml_test
