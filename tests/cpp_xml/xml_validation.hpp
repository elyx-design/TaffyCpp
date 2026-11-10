#pragma once

#include <pugixml.hpp>

#include <climits>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace cpp_xml {

enum class Validation { Ok, InvalidUtf8, Malformed };

namespace validation_detail {

// Validate the entire byte span before pugixml can interpret an encoding
// declaration, stop at a NUL, or report a different XML error first. XML's
// character restrictions (including NUL) are separate from UTF-8 validity.
inline bool valid_utf8(std::string_view bytes) {
    std::size_t i = 0;
    while (i < bytes.size()) {
        const auto first = static_cast<unsigned char>(bytes[i++]);
        if (first < 0x80)
            continue;
        unsigned remaining;
        unsigned char low = 0x80, high = 0xbf;
        if (first >= 0xc2 && first <= 0xdf) {
            remaining = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            remaining = 2;
            if (first == 0xe0)
                low = 0xa0; // No overlong sequences.
            if (first == 0xed)
                high = 0x9f; // No UTF-16 surrogates.
        } else if (first >= 0xf0 && first <= 0xf4) {
            remaining = 3;
            if (first == 0xf0)
                low = 0x90;
            if (first == 0xf4)
                high = 0x8f; // At most U+10FFFF.
        } else {
            return false;
        }
        if (bytes.size() - i < remaining)
            return false;
        const auto second = static_cast<unsigned char>(bytes[i++]);
        if (second < low || second > high)
            return false;
        while (--remaining) {
            const auto next = static_cast<unsigned char>(bytes[i++]);
            if (next < 0x80 || next > 0xbf)
                return false;
        }
    }
    return true;
}

inline bool xml_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Called only after valid_utf8(): decode without rechecking byte sequences.
inline unsigned codepoint(std::string_view bytes, std::size_t& i) {
    const auto first = static_cast<unsigned char>(bytes[i++]);
    if (first < 0x80)
        return first;
    unsigned count = first < 0xe0 ? 1 : first < 0xf0 ? 2 : 3;
    unsigned value = first & (0x7f >> (count + 1));
    while (count--)
        value = (value << 6) | (static_cast<unsigned char>(bytes[i++]) & 0x3f);
    return value;
}

inline bool xml_char(unsigned c) {
    return c == 9 || c == 10 || c == 13 || (c >= 0x20 && c <= 0xd7ff) ||
           (c >= 0xe000 && c <= 0xfffd) || (c >= 0x10000 && c <= 0x10ffff);
}

inline bool name_start(unsigned c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
           (c >= 0xc0 && c <= 0xd6) || (c >= 0xd8 && c <= 0xf6) || (c >= 0xf8 && c <= 0x2ff) ||
           (c >= 0x370 && c <= 0x37d) || (c >= 0x37f && c <= 0x1fff) ||
           (c >= 0x200c && c <= 0x200d) || (c >= 0x2070 && c <= 0x218f) ||
           (c >= 0x2c00 && c <= 0x2fef) || (c >= 0x3001 && c <= 0xd7ff) ||
           (c >= 0xf900 && c <= 0xfdcf) || (c >= 0xfdf0 && c <= 0xfffd) ||
           (c >= 0x10000 && c <= 0xeffff);
}

inline bool name_char(unsigned c) {
    return name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == 0xb7 ||
           (c >= 0x300 && c <= 0x36f) || (c >= 0x203f && c <= 0x2040);
}

// roxmltree 0.21.2 tokenizer::parse_declaration checks ordered name
// *prefixes*, not exact attribute names, and ignores all three values.
// Validate that original grammar independently BEFORE hiding anything from
// pugixml. In particular, '&' is literal here, but '<' and non-XML characters
// are forbidden. Only the leading declaration (after an optional BOM) is
// eligible; comments, CDATA, PIs and misplaced declarations are never rewritten.
// Scratch changes only names/values; original quotes, Eq and attribute spacing
// survive, and no declaration value can select an encoding or XML version.
inline bool normalize_declaration(std::string_view bytes, std::string& scratch) {
    std::size_t i = bytes.substr(0, 3) == "\xef\xbb\xbf" ? 3 : 0;
    if (bytes.substr(i, 5) != "<?xml" || i + 5 >= bytes.size() || !xml_space(bytes[i + 5]))
        return true;
    // Preserve the historical ASCII-space compatibility path exactly. Other
    // XML whitespace formerly reached libxml's ordinary declaration grammar;
    // check those lexical values before pugixml can decode or truncate them.
    // We deliberately do not reproduce libxml's installed encoding-name
    // registry here: syntactically valid labels are ignored with forced UTF-8,
    // including on this uncommon non-space path where it rejected unknowns.
    const bool strict = bytes[i + 5] != ' ';
    i += 5;
    std::size_t copied = 0;
    const auto spaces = [&]() {
        while (i < bytes.size() && xml_space(bytes[i]))
            ++i;
    };
    const auto separator = [&]() {
        if (i < bytes.size() && xml_space(bytes[i])) {
            spaces();
            return true;
        }
        return bytes.substr(i, 2) == "?>";
    };
    const auto attribute = [&](std::string_view name, std::string_view value) {
        if (bytes.substr(i, name.size()) != name)
            return false;
        const auto start = i;
        // Match consume_qname, including its acceptance of an empty prefix.
        bool first = true, colon = false;
        while (i < bytes.size()) {
            auto next = i;
            const unsigned c = codepoint(bytes, next);
            if (c == ':') {
                if (colon)
                    return false;
                colon = true;
                first = true;
            } else {
                if (!name_char(c))
                    break;
                if (first && !name_start(c))
                    return false;
                first = false;
            }
            i = next;
        }
        if (first || (strict && bytes.substr(start, i - start) != name))
            return false;
        scratch.append(bytes.substr(copied, start - copied));
        scratch.append(name);
        copied = i;
        spaces();
        if (i == bytes.size() || bytes[i++] != '=')
            return false;
        spaces();
        if (i == bytes.size() || (bytes[i] != '\'' && bytes[i] != '"'))
            return false;
        const char quote = bytes[i++];
        scratch.append(bytes.substr(copied, i - copied));
        scratch.append(value);
        const auto value_start = i;
        while (i < bytes.size() && bytes[i] != quote) {
            const unsigned c = codepoint(bytes, i);
            if (c == '<' || !xml_char(c))
                return false;
        }
        if (i == bytes.size())
            return false;
        if (strict) {
            const auto original = bytes.substr(value_start, i - value_start);
            if (name == "version") {
                if (original.size() < 3 || original.substr(0, 2) != "1.")
                    return false;
                for (char c : original.substr(2))
                    if (c < '0' || c > '9')
                        return false;
            } else if (name == "standalone") {
                if (original != "yes" && original != "no")
                    return false;
            } else {
                const auto letter = [](char c) {
                    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
                };
                if (original.empty() || !letter(original.front()))
                    return false;
                for (char c : original)
                    if (!letter(c) && !(c >= '0' && c <= '9') && c != '.' && c != '_' && c != '-')
                        return false;
            }
        }
        copied = i++; // Retain the closing quote.
        return true;
    };
    spaces();
    if (!attribute("version", "1.0") || !separator())
        return false;
    if (bytes.substr(i, 8) == "encoding") {
        if (!attribute("encoding", "UTF-8") || !separator())
            return false;
    }
    if (bytes.substr(i, 10) == "standalone") {
        if (!attribute("standalone", "yes"))
            return false;
    }
    spaces();
    if (bytes.substr(i, 2) != "?>")
        return false;
    scratch.append(bytes.substr(copied));
    return true;
}

// Fixture well-formedness checks, not another XML parser: pugixml owns
// tokenization, tag matching and tree construction. Guard only information its
// permissive parser would otherwise discard or accept without validation.
inline bool original_guards(std::string_view bytes, std::string& error) {
    for (std::size_t i = 0; i < bytes.size();) {
        if (!xml_char(codepoint(bytes, i))) {
            error = "invalid XML character";
            return false;
        }
    }
    bool markup = false;
    char quote = 0;
    for (std::size_t i = 0; i < bytes.size();) {
        const auto tail = bytes.substr(i);
        if (!quote && tail.substr(0, 2) == "<!") {
            std::string_view end;
            std::size_t content;
            if (tail.substr(0, 4) == "<!--") {
                end = "--";
                content = i + 4;
            } else if (tail.substr(0, 9) == "<![CDATA[") {
                end = "]]>";
                content = i + 9;
            } else {
                // Reject before pugixml sees a DTD (including any subset).
                error = "DTD or unsupported XML declaration is forbidden";
                return false;
            }
            const auto close = bytes.find(end, content);
            if (close == std::string_view::npos ||
                (end == "--" && (close + 2 == bytes.size() || bytes[close + 2] != '>'))) {
                error = "malformed XML comment or CDATA";
                return false;
            }
            i = close + 3;
            continue;
        }
        if (!quote && tail.substr(0, 2) == "<?") {
            const auto close = bytes.find("?>", i + 2);
            if (close == std::string_view::npos) {
                error = "unterminated XML processing instruction";
                return false;
            }
            // The reserved XML target is only legal in a leading declaration.
            const auto target_end = bytes.find_first_of(" \t\r\n?", i + 2);
            const auto target = bytes.substr(i + 2, target_end - (i + 2));
            if (target.size() == 3 && (target[0] == 'x' || target[0] == 'X') &&
                (target[1] == 'm' || target[1] == 'M') &&
                (target[2] == 'l' || target[2] == 'L')) {
                const auto start = bytes.substr(0, 3) == "\xef\xbb\xbf" ? 3u : 0u;
                if (i != start || target != "xml" || target_end >= bytes.size() ||
                    !xml_space(bytes[target_end])) {
                    error = "misplaced or malformed XML declaration";
                    return false;
                }
            }
            i = close + 2;
            continue;
        }
        const char c = bytes[i];
        if (c == '<') {
            if (quote) {
                error = "literal '<' in XML attribute";
                return false;
            }
            markup = true;
        } else if (markup && (c == '\'' || c == '"')) {
            if (!quote)
                quote = c;
            else if (quote == c)
                quote = 0;
        } else if (!quote && c == '>') {
            markup = false;
        }
        if (!markup && tail.substr(0, 3) == "]]>") {
            error = "']]>' outside XML CDATA";
            return false;
        }
        if (c == '&') {
            const auto close = bytes.find(';', i + 1);
            if (close == std::string_view::npos) {
                error = "unterminated XML entity reference";
                return false;
            }
            auto entity = bytes.substr(i + 1, close - i - 1);
            bool valid = entity == "amp" || entity == "lt" || entity == "gt" ||
                         entity == "apos" || entity == "quot";
            if (!entity.empty() && entity.front() == '#') {
                entity.remove_prefix(1);
                unsigned base = 10, value = 0;
                if (!entity.empty() && entity.front() == 'x') {
                    base = 16;
                    entity.remove_prefix(1);
                }
                valid = !entity.empty();
                for (const char digit : entity) {
                    const unsigned n = digit >= '0' && digit <= '9' ? digit - '0' :
                                       digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 :
                                       digit >= 'A' && digit <= 'F' ? digit - 'A' + 10 : 16;
                    if (n >= base || value > (0x10ffffu - n) / base) {
                        valid = false;
                        break;
                    }
                    value = value * base + n;
                }
                valid = valid && xml_char(value);
            }
            if (!valid) {
                error = "invalid XML entity or character reference";
                return false;
            }
            i = close + 1;
        } else {
            ++i;
        }
    }
    return true;
}

inline bool ncname(std::string_view name) {
    if (name.empty())
        return false;
    std::size_t i = 0;
    if (!name_start(codepoint(name, i)))
        return false;
    while (i < name.size())
        if (!name_char(codepoint(name, i)))
            return false;
    return true;
}

inline bool qname(std::string_view name, std::string_view& prefix, std::string_view& local) {
    const auto colon = name.find(':');
    prefix = colon == std::string_view::npos ? std::string_view{} : name.substr(0, colon);
    local = colon == std::string_view::npos ? name : name.substr(colon + 1);
    return (colon == std::string_view::npos || ncname(prefix)) && ncname(local);
}

inline constexpr std::string_view xml_uri = "http://www.w3.org/XML/1998/namespace";
inline constexpr std::string_view xmlns_uri = "http://www.w3.org/2000/xmlns/";

inline std::string_view namespace_uri(pugi::xml_node node, std::string_view prefix) {
    if (prefix == "xml")
        return xml_uri;
    const std::string key = prefix.empty() ? "xmlns" : "xmlns:" + std::string(prefix);
    for (; node; node = node.parent())
        if (const auto attr = node.attribute(key.c_str()))
            return attr.value();
    return {};
}

inline bool element_guards(pugi::xml_node node) {
    std::string_view prefix, local;
    if (!qname(node.name(), prefix, local) || prefix == "xmlns" ||
        (!prefix.empty() && namespace_uri(node, prefix).empty()))
        return false;
    std::unordered_set<std::string_view> names;
    std::unordered_set<std::string> expanded;
    for (const auto attr : node.attributes()) {
        const std::string_view name = attr.name(), value = attr.value();
        if (!names.insert(name).second || !qname(name, prefix, local))
            return false;
        if (name == "xmlns" || prefix == "xmlns") {
            const auto binding = name == "xmlns" ? std::string_view{} : local;
            if (binding == "xmlns" || value == xmlns_uri ||
                ((binding == "xml") != (value == xml_uri)) ||
                (!binding.empty() && value.empty()))
                return false;
            continue;
        }
        const auto uri = prefix.empty() ? std::string_view{} : namespace_uri(node, prefix);
        if (!prefix.empty() && uri.empty())
            return false;
        // NCNames cannot contain NUL, so the expanded-name key is unambiguous.
        std::string key(uri);
        key += '\0';
        key += local;
        if (!expanded.insert(std::move(key)).second)
            return false;
    }
    return true;
}

inline bool dom_guards(const pugi::xml_document& document, std::string_view bytes) {
    unsigned roots = 0;
    for (auto node : document.children()) {
        if (node.type() == pugi::node_element)
            ++roots;
        else if (node.type() == pugi::node_pcdata) {
            // parse_fragment retains text outside the root instead of silently
            // dropping it. Only literal XML whitespace is allowed there.
            const auto offset = node.offset_debug();
            if (offset < 0 || static_cast<std::size_t>(offset) >= bytes.size())
                return false;
            for (auto i = static_cast<std::size_t>(offset); i < bytes.size() && bytes[i] != '<'; ++i)
                if (!xml_space(bytes[i]))
                    return false;
        } else if (node.type() != pugi::node_comment && node.type() != pugi::node_pi &&
                   node.type() != pugi::node_declaration) {
            return false;
        }
    }
    if (roots != 1)
        return false;
    // Iterative traversal: the existing 4096-deep fixture must not consume
    // the C++ call stack. Pugixml parses iteratively and frees document pages
    // without recursively walking the tree.
    for (auto node = document.first_child(); node;) {
        if (node.type() == pugi::node_element && !element_guards(node))
            return false;
        if (node.type() == pugi::node_pi) {
            // PI targets are XML Names, not namespace-qualified names.
            const std::string_view name = node.name();
            std::size_t i = 0;
            if (name.empty())
                return false;
            auto c = codepoint(name, i);
            if (c != ':' && !name_start(c))
                return false;
            while (i < name.size()) {
                c = codepoint(name, i);
                if (c != ':' && !name_char(c))
                    return false;
            }
        }
        if (node.first_child()) {
            node = node.first_child();
        } else {
            while (node && !node.next_sibling())
                node = node.parent();
            if (node)
                node = node.next_sibling();
        }
    }
    return true;
}

} // namespace validation_detail

// Validate original bytes before the fixture loader runs. C++17; pugixml is
// the sole XML parser. It never resolves external resources, and the byte
// guard rejects all DTDs before parsing. No global parser state is changed.
inline Validation validate(std::string_view bytes, std::string& error) {
    error.clear();
    if (!validation_detail::valid_utf8(bytes)) {
        error = "invalid utf-8 in XML input";
        return Validation::InvalidUtf8;
    }
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(INT_MAX)) {
        error = bytes.empty() ? "empty XML document" : "XML input exceeds validation byte limit";
        return Validation::Malformed;
    }
    // Do not let a parser truncate a byte span at a literal NUL.
    if (bytes.find('\0') != std::string_view::npos) {
        error = "NUL character in XML input";
        return Validation::Malformed;
    }
    std::string scratch;
    if (!validation_detail::normalize_declaration(bytes, scratch)) {
        error = "missing whitespace or malformed XML declaration";
        return Validation::Malformed;
    }
    if (!scratch.empty())
        bytes = scratch;
    if (bytes.size() > static_cast<std::size_t>(INT_MAX)) {
        error = "normalized XML input exceeds validation byte limit";
        return Validation::Malformed;
    }
    if (!validation_detail::original_guards(bytes, error))
        return Validation::Malformed;

    pugi::xml_document document;
    // Explicit UTF-8 prevents declarations from selecting a different byte
    // interpretation. Retain every node kind and outside-root text for guards.
    constexpr unsigned options = pugi::parse_full | pugi::parse_ws_pcdata | pugi::parse_fragment;
    const auto result = document.load_buffer(bytes.data(), bytes.size(), options, pugi::encoding_utf8);
    if (!result) {
        error = result.description();
        return Validation::Malformed;
    }
    if (!validation_detail::dom_guards(document, bytes)) {
        error = "malformed XML document (root, name, attribute or namespace)";
        return Validation::Malformed;
    }
    return Validation::Ok;
}

} // namespace cpp_xml
