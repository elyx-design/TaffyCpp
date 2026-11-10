// Test-only C ABI adapter. Layout, style parsing and metadata are production C++.
#include "../cpp_xml/xml_validation.hpp"
#include "../support/xml_oracle.h"
#include <algorithm>
#include <cstring>
#include <taffy/taffy.hpp>
#include <unordered_map>
#include <unordered_set>

using namespace taffy;
struct XmlOracle {
    Tree tree;
    std::vector<NodeId> nodes;
    std::unordered_map<NodeId, uint64_t> handles;
    std::string error;
    bool poisoned = false;
};
namespace {
// The callback outcome must not depend on diagnostic string allocation.
struct CallbackFailure {};
struct Failure {
    uint32_t code;
    std::string message;
};
[[noreturn]] void fail(uint32_t code, std::string message) {
    throw Failure{code, std::move(message)};
}
void require(bool value, const char* message) {
    if (!value)
        fail(ORACLE_INVALID, message);
}
// Validate strings independently of XML (style/viewport strings are not documents).
bool utf8(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i++]);
        if (c < 0x80)
            continue;
        unsigned n;
        uint32_t v, minimum;
        if (c >= 0xc2 && c <= 0xdf) {
            n = 1;
            v = c & 31;
            minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            n = 2;
            v = c & 15;
            minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            n = 3;
            v = c & 7;
            minimum = 0x10000;
        } else
            return false;
        if (n > s.size() - i)
            return false;
        while (n--) {
            auto d = static_cast<unsigned char>(s[i++]);
            if ((d & 0xc0) != 0x80)
                return false;
            v = (v << 6) | (d & 63);
        }
        if (v < minimum || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff))
            return false;
    }
    return true;
}
std::optional<std::string_view> text(XmlOracleString s) {
    require(s.data || s.len == 0, "null string with nonzero length");
    if (!s.data)
        return {};
    std::string_view v(s.data, s.len);
    require(utf8(v), "invalid UTF-8");
    return v;
}
NodeId node(XmlOracle& c, uint64_t h) {
    if (!h || h > c.nodes.size())
        fail(ORACLE_STALE, "unknown node handle");
    return c.nodes[h - 1];
}
template <class F> uint32_t call(XmlOracle* c, F&& f) noexcept {
    if (!c)
        return ORACLE_INVALID;
    if (c->poisoned)
        return ORACLE_POISONED;
    try {
        c->error.clear();
        f(*c);
        return ORACLE_OK;
    } catch (const CallbackFailure&) {
        c->poisoned = true;
        try {
            c->error = "C++ intrinsic callback failed; tree poisoned";
        } catch (...) {
        }
        return ORACLE_CALLBACK_FAILURE;
    } catch (const Failure& e) {
        try {
            c->error = e.message;
        } catch (...) {
        }
        return e.code;
    } catch (const std::exception& e) {
        c->poisoned = true;
        try {
            c->error = e.what();
        } catch (...) {
        }
        return ORACLE_PANIC;
    } catch (...) {
        c->poisoned = true;
        return ORACLE_PANIC;
    }
}
void copy(std::string_view s, char* out, size_t cap, size_t* required) {
    require(required && (!cap || out), "invalid string output");
    *required = s.size() + 1;
    if (cap) {
        auto n = std::min(s.size(), cap - 1);
        if (n)
            std::memcpy(out, s.data(), n);
        out[n] = 0;
    }
}
template <class T> T parsed(std::string_view s) {
    auto r = parse_css<T>(s);
    if (!r)
        fail(ORACLE_ERROR, r.error().message);
    return std::move(*r);
}
template <class T> void checked(const std::expected<T, TreeError>& r) {
    if (!r)
        fail(ORACLE_ERROR, "production Tree operation failed");
}
constexpr std::string_view keys[] = {"grid-template-rows",
                                     "grid-template-columns",
                                     "display",
                                     "direction",
                                     "box-sizing",
                                     "overflow-x",
                                     "overflow-y",
                                     "scrollbar-width",
                                     "contain",
                                     "float",
                                     "clear",
                                     "position",
                                     "width",
                                     "height",
                                     "min-width",
                                     "min-height",
                                     "max-width",
                                     "max-height",
                                     "top",
                                     "left",
                                     "bottom",
                                     "right",
                                     "margin-top",
                                     "margin-left",
                                     "margin-bottom",
                                     "margin-right",
                                     "padding-top",
                                     "padding-left",
                                     "padding-bottom",
                                     "padding-right",
                                     "border-top",
                                     "border-left",
                                     "border-bottom",
                                     "border-right",
                                     "column-gap",
                                     "row-gap",
                                     "aspect-ratio",
                                     "align-items",
                                     "align-self",
                                     "justify-items",
                                     "justify-self",
                                     "align-content",
                                     "justify-content",
                                     "text-align",
                                     "flex-direction",
                                     "flex-wrap",
                                     "flex-line-count",
                                     "flex-grow",
                                     "flex-shrink",
                                     "flex-basis",
                                     "grid-auto-flow",
                                     "grid-auto-rows",
                                     "grid-auto-columns",
                                     "grid-row-start",
                                     "grid-row-end",
                                     "grid-column-start",
                                     "grid-column-end"};
std::string_view trim_unicode(std::string_view s) {
    constexpr std::string_view spaces[] = {"\t",           "\n",           "\v",
                                           "\f",           "\r",           " ",
                                           "\xc2\x85",     "\xc2\xa0",     "\xe1\x9a\x80",
                                           "\xe2\x80\x80", "\xe2\x80\x81", "\xe2\x80\x82",
                                           "\xe2\x80\x83", "\xe2\x80\x84", "\xe2\x80\x85",
                                           "\xe2\x80\x86", "\xe2\x80\x87", "\xe2\x80\x88",
                                           "\xe2\x80\x89", "\xe2\x80\x8a", "\xe2\x80\xa8",
                                           "\xe2\x80\xa9", "\xe2\x80\xaf", "\xe2\x81\x9f",
                                           "\xe3\x80\x80"};
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto w : spaces) {
            if (s.starts_with(w)) {
                s.remove_prefix(w.size());
                changed = true;
            }
            if (s.ends_with(w)) {
                s.remove_suffix(w.size());
                changed = true;
            }
        }
    }
    return s;
}
OracleOptional known(std::optional<float> v) {
    return {uint32_t(v.has_value()), v.value_or(0)};
}
OracleScalar available(AvailableSpace v) {
    if (v.kind() == AvailableSpace::Kind::Definite)
        return {1, v.value()};
    return {v.kind() == AvailableSpace::Kind::MinContent ? 3u : 4u, 0};
}
OracleSize size(Size<float> s) {
    return {s.width, s.height};
}
OracleRect rect(Rect<float> r) {
    return {r.left, r.right, r.top, r.bottom};
}
} // namespace
extern "C" {
uint32_t xml_oracle_create(XmlOracle** out) {
    if (!out)
        return ORACLE_INVALID;
    *out = nullptr;
    try {
        *out = new XmlOracle;
        return ORACLE_OK;
    } catch (...) {
        return ORACLE_PANIC;
    }
}
uint32_t xml_oracle_free(XmlOracle* c) {
    if (!c)
        return ORACLE_INVALID;
    try {
        delete c;
        return ORACLE_OK;
    } catch (...) {
        return ORACLE_PANIC;
    }
}
uint32_t xml_oracle_error(XmlOracle* c, char* out, size_t cap, size_t* required) {
    if (!c)
        return ORACLE_INVALID;
    try {
        copy(c->error, out, cap, required);
        return ORACLE_OK;
    } catch (...) {
        return ORACLE_INVALID;
    }
}
uint32_t xml_oracle_validate_xml(XmlOracle* c, XmlOracleString xml) {
    return call(c, [&](auto&) {
        auto v = text(xml);
        require(v.has_value(), "absent XML document");
        std::string error;
        auto result = cpp_xml::validate(*v, error);
        if (result != cpp_xml::Validation::Ok)
            fail(result == cpp_xml::Validation::InvalidUtf8 ? ORACLE_INVALID : ORACLE_ERROR, error);
    });
}
uint32_t xml_oracle_cache_mode_change_evictions(XmlOracle* c, uint64_t* count, uint32_t* avail) {
    return call(c, [&](auto&) {
        require(count && avail, "null cache counter output");
#ifndef NDEBUG
        *count = cache_mode_change_evictions();
        *avail = 1;
#else
        *count=0;*avail=0;
#endif
    });
}
uint32_t xml_oracle_new_node(XmlOracle* c, const XmlOracleStylePair* pairs, size_t count,
                             uint64_t* out) {
    return call(c, [&](auto& t) {
        require(out && (!count || pairs), "invalid node arguments");
        Style style;
        std::unordered_map<std::string_view, std::string_view> attrs;
        for (size_t i = 0; i < count; ++i) {
            auto name = text(pairs[i].name), value = text(pairs[i].value);
            require(name.has_value() && value.has_value(), "absent style name/value");
            require(*name == "writing-mode" ||
                        std::find(std::begin(keys), std::end(keys), *name) != std::end(keys),
                    "unknown style property");
            attrs.try_emplace(*name, *value);
        }
        // Validate every ABI span/key before parsing, then use native build_style order.
        for (auto name : keys) {
            auto at = attrs.find(name);
            if (at == attrs.end())
                continue;
            auto value = at->second;
            if (name == "grid-template-rows" || name == "grid-template-columns") {
                auto tracks = parsed<GridTemplateTracks<>>(value);
                if (name == "grid-template-rows") {
                    style.grid_template_rows = std::move(tracks.tracks);
                    style.grid_template_row_names = std::move(tracks.line_names);
                } else {
                    style.grid_template_columns = std::move(tracks.tracks);
                    style.grid_template_column_names = std::move(tracks.line_names);
                }
                continue;
            }
            if (name == "align-self" || name == "justify-self") {
                auto trimmed = trim_unicode(value);
                bool auto_value = trimmed.size() == 4;
                constexpr std::string_view literal = "auto";
                for (size_t j = 0; auto_value && j < 4; ++j)
                    auto_value = (trimmed[j] == literal[j] || trimmed[j] == literal[j] - 32);
                std::optional<AlignSelf> alignment;
                if (!auto_value)
                    alignment = parsed<AlignSelf>(value);
                if (name == "align-self")
                    style.align_self = alignment;
                else
                    style.justify_self = alignment;
                continue;
            }
            if (name == "scrollbar-width" || name == "flex-grow" || name == "flex-shrink" ||
                name == "aspect-ratio" || name == "flex-line-count") {
                // Rust scalar FromStr does not consume CSS trivia or escapes.
                bool lexical = !value.empty();
                for (unsigned char ch : value)
                    lexical = lexical &&
                              ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
                               (ch >= 'A' && ch <= 'Z') || ch == '+' || ch == '-' || ch == '.');
                if (!lexical) {
                    if (name == "aspect-ratio") {
                        style.aspect_ratio.reset();
                        continue;
                    }
                    fail(ORACLE_ERROR, std::string(name) + ": invalid native scalar");
                }
            }
            auto r = parse_style_property(style, name, value);
            if (name == "aspect-ratio") {
                if (!r)
                    style.aspect_ratio.reset();
            } else if (!r)
                fail(ORACLE_ERROR, std::string(name) + ": " + r.error().message);
        }
        auto n = t.tree.new_leaf(std::move(style));
        checked(n);
        t.nodes.push_back(*n);
        auto h = t.nodes.size();
        t.handles.emplace(*n, h);
        *out = h;
    });
}
uint32_t xml_oracle_set_children(XmlOracle* c, uint64_t h, const uint64_t* children, size_t count) {
    return call(c, [&](auto& t) {
        require(!count || children, "null children");
        auto parent = node(t, h);
        std::vector<NodeId> ids;
        std::unordered_set<NodeId> seen;
        for (size_t i = 0; i < count; ++i) {
            auto id = node(t, children[i]);
            require(seen.insert(id).second, "duplicate child");
            for (auto p = std::optional(parent); p; p = t.tree.parent(*p))
                require(*p != id, "cycle in tree");
            ids.push_back(id);
        }
        checked(t.tree.set_children(parent, ids));
    });
}
uint32_t xml_oracle_compute(XmlOracle* c, uint64_t h, XmlOracleString width, XmlOracleString height,
                            uint32_t rounding, void* user, XmlOracleIntrinsic callback) {
    return call(c, [&](auto& t) {
        require(rounding <= 1, "invalid rounding boolean");
        require(callback, "missing intrinsic callback");
        auto root = node(t, h);
        auto w = text(width), v = text(height);
        Size<AvailableSpace> space{w ? parsed<AvailableSpace>(*w) : AvailableSpace::max_content(),
                                   v ? parsed<AvailableSpace>(*v) : AvailableSpace::max_content()};
        if (rounding)
            t.tree.enable_rounding();
        else
            t.tree.disable_rounding();
        checked(t.tree.compute_layout_with_measure(
            root, space, [&](LayoutTree& tree, NodeId n, const LayoutInput& input) {
                return compute_leaf_layout(
                    input, tree.style(n), {}, [&](const LayoutInput& intrinsic) {
                        OracleSize out{};
                        const auto handle = t.handles.at(n);
                        uint32_t status;
                        try {
                            status = callback(user, handle,
                                               {known(intrinsic.known_dimensions.width),
                                                known(intrinsic.known_dimensions.height)},
                                               {available(intrinsic.available_space.width),
                                                available(intrinsic.available_space.height)},
                                               &out);
                        } catch (...) {
                            t.poisoned = true;
                            throw CallbackFailure{};
                        }
                        if (status != ORACLE_OK) {
                            t.poisoned = true;
                            throw CallbackFailure{};
                        }
                        return Size<float>{out.width, out.height};
                    });
            }));
    });
}
uint32_t xml_oracle_layout(XmlOracle* c, uint64_t h, XmlOracleLayout* out) {
    return call(c, [&](auto& t) {
        require(out, "null layout output");
        auto n = node(t, h);
        const auto& l = t.tree.layout(n);
        *out = {{l.order, l.location.x, l.location.y, size(l.size), size(l.content_box_size()),
                 size(l.scrollbar_size), rect(l.scrollable_overflow_rect), rect(l.border),
                 rect(l.padding), rect(l.margin), l.scroll_width(), l.scroll_height()},
                {l.scroll_width(), l.scroll_height()},
                1,
                uint32_t(std::holds_alternative<DetailedGridInfo>(t.tree.detailed_layout_info(n)))};
    });
}
uint32_t xml_oracle_tracks(XmlOracle* c, uint64_t h, uint32_t axis, char* out, size_t cap,
                           size_t* required) {
    return call(c, [&](auto& t) {
        require(axis <= 1, "invalid track axis");
        auto n = node(t, h);
        std::string tracks;
        if (auto info = std::get_if<DetailedGridInfo>(&t.tree.detailed_layout_info(n)))
            tracks = axis ? info->grid_template_columns(1) : info->grid_template_rows(1);
        copy(tracks, out, cap, required);
    });
}
}
