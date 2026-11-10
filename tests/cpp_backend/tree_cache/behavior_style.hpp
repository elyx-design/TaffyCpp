#pragma once
// Test-only native serde transport. No production JSON dependency or native linkage.
#include "../../support/behavior_suite_oracle.h"
#include "taffy/parse.hpp"
#include <bit>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>

namespace behavior_cpp {
namespace style_detail {
[[noreturn]] inline void invalid(std::string message = "invalid native style JSON") {
    throw std::invalid_argument(message);
}
struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    std::string text;
    std::vector<Json> array;
    std::map<std::string, Json> object;
    static Json str(std::string s) {
        Json v;
        v.kind = String;
        v.text = std::move(s);
        return v;
    }
    static Json num(std::string s) {
        Json v;
        v.kind = Number;
        v.text = std::move(s);
        return v;
    }
    static Json obj() {
        Json v;
        v.kind = Object;
        return v;
    }
    static Json arr() {
        Json v;
        v.kind = Array;
        return v;
    }
};
// Validate UTF-8, including escaped surrogate pairs; serde_json rejects malformed UTF-8.
inline void utf8(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        unsigned c = (unsigned char)s[i++];
        if (c < 128)
            continue;
        unsigned n = c >= 0xc2 && c <= 0xdf   ? 1
                     : c >= 0xe0 && c <= 0xef ? 2
                     : c >= 0xf0 && c <= 0xf4 ? 3
                                              : 99;
        if (n == 99 || i + n > s.size())
            invalid("invalid UTF-8");
        unsigned cp = c & ((1u << (6 - n)) - 1);
        for (unsigned k = 0; k < n; k++) {
            unsigned d = (unsigned char)s[i++];
            if ((d & 192) != 128)
                invalid("invalid UTF-8");
            cp = (cp << 6) | (d & 63);
        }
        if ((n == 1 && cp < 128) || (n == 2 && cp < 2048) || (n == 3 && cp < 65536) ||
            cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            invalid("invalid UTF-8");
    }
}
struct Parser {
    std::string_view s;
    size_t p = 0;
    bool unique = true;
    void ws() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t'))
            ++p;
    }
    char get() {
        if (p == s.size())
            invalid("unexpected JSON end");
        return s[p++];
    }
    unsigned hex() {
        unsigned r = 0;
        for (int i = 0; i < 4; i++) {
            char c = get();
            unsigned v = c >= '0' && c <= '9'   ? c - '0'
                         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                : 99;
            if (v == 99)
                invalid("invalid JSON escape");
            r = r * 16 + v;
        }
        return r;
    }
    static void append(std::string& o, unsigned c) {
        if (c < 128)
            o += char(c);
        else if (c < 2048) {
            o += char(192 | (c >> 6));
            o += char(128 | (c & 63));
        } else if (c < 65536) {
            o += char(224 | (c >> 12));
            o += char(128 | ((c >> 6) & 63));
            o += char(128 | (c & 63));
        } else {
            o += char(240 | (c >> 18));
            o += char(128 | ((c >> 12) & 63));
            o += char(128 | ((c >> 6) & 63));
            o += char(128 | (c & 63));
        }
    }
    std::string string() {
        if (get() != '"')
            invalid();
        std::string o;
        for (;;) {
            char c = get();
            if (c == '"')
                return o;
            if ((unsigned char)c < 32)
                invalid();
            if (c != '\\') {
                o += c;
                continue;
            }
            c = get();
            switch (c) {
            case '"':
            case '\\':
            case '/':
                o += c;
                break;
            case 'b':
                o += '\b';
                break;
            case 'f':
                o += '\f';
                break;
            case 'n':
                o += '\n';
                break;
            case 'r':
                o += '\r';
                break;
            case 't':
                o += '\t';
                break;
            case 'u': {
                unsigned cp = hex();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (get() != '\\' || get() != 'u')
                        invalid();
                    unsigned lo = hex();
                    if (lo < 0xdc00 || lo > 0xdfff)
                        invalid();
                    cp = 65536 + ((cp - 0xd800) << 10) + lo - 0xdc00;
                } else if (cp >= 0xdc00 && cp <= 0xdfff)
                    invalid();
                append(o, cp);
                break;
            }
            default:
                invalid();
            }
        }
    }
    Json value(unsigned depth = 0) {
        if (depth >= 128)
            invalid("JSON recursion limit");
        ws();
        if (p == s.size())
            invalid();
        char c = s[p];
        Json v;
        if (c == '"')
            return Json::str(string());
        if (c == '{' || c == '[') {
            ++p;
            v = c == '{' ? Json::obj() : Json::arr();
            ws();
            char close = c == '{' ? '}' : ']';
            if (p < s.size() && s[p] == close) {
                ++p;
                return v;
            }
            for (;;) {
                ws();
                std::string key;
                if (c == '{') {
                    key = string();
                    ws();
                    if (get() != ':')
                        invalid();
                }
                auto x = value(depth + 1);
                if (c == '{') {
                    if (unique && v.object.contains(key))
                        invalid("duplicate JSON field: " + key);
                    v.object[std::move(key)] = std::move(x);
                } else
                    v.array.push_back(std::move(x));
                ws();
                char d = get();
                if (d == close)
                    return v;
                if (d != ',')
                    invalid();
            }
        }
        for (auto word :
             {std::string_view("null"), std::string_view("true"), std::string_view("false")})
            if (s.substr(p, word.size()) == word) {
                p += word.size();
                if (word != "null") {
                    v.kind = Json::Bool;
                    v.text = word;
                }
                return v;
            }
        size_t start = p;
        if (c == '-')
            ++p;
        if (p == s.size())
            invalid();
        if (s[p] == '0')
            ++p;
        else {
            if (s[p] < '1' || s[p] > '9')
                invalid();
            while (p < s.size() && s[p] >= '0' && s[p] <= '9')
                ++p;
        }
        if (p < s.size() && s[p] == '.') {
            ++p;
            size_t a = p;
            while (p < s.size() && s[p] >= '0' && s[p] <= '9')
                ++p;
            if (a == p)
                invalid();
        }
        if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
            ++p;
            if (p < s.size() && (s[p] == '+' || s[p] == '-'))
                ++p;
            size_t a = p;
            while (p < s.size() && s[p] >= '0' && s[p] <= '9')
                ++p;
            if (a == p)
                invalid();
        }
        auto t = std::string(s.substr(start, p - start));
        char* end;
        double d = std::strtod(t.c_str(), &end);
        if (!std::isfinite(d))
            invalid("JSON number out of range");
        return Json::num(t);
    }
    Json parse() {
        utf8(s);
        auto v = value();
        ws();
        if (p != s.size())
            invalid("trailing JSON input");
        return v;
    }
};
inline std::string dump(const Json& v) {
    if (v.kind == Json::Null)
        return "null";
    if (v.kind == Json::Bool || v.kind == Json::Number)
        return v.text;
    if (v.kind == Json::String) {
        std::string o = "\"";
        const char* h = "0123456789abcdef";
        for (unsigned char c : v.text) {
            if (c == '"' || c == '\\') {
                o += '\\';
                o += char(c);
            } else if (c < 32) {
                o += "\\u00";
                o += h[c >> 4];
                o += h[c & 15];
            } else
                o += char(c);
        }
        return o + '"';
    }
    std::string o = v.kind == Json::Array ? "[" : "{";
    bool first = true;
    if (v.kind == Json::Array)
        for (auto& x : v.array) {
            if (!first)
                o += ',';
            first = false;
            o += dump(x);
        }
    else
        for (auto& [k, x] : v.object) {
            if (!first)
                o += ',';
            first = false;
            o += dump(Json::str(k)) + ':' + dump(x);
        }
    return o + (v.kind == Json::Array ? "]" : "}");
}
inline void keys(const Json& v, std::initializer_list<std::string_view> names) {
    if (v.kind != Json::Object || v.object.size() != names.size())
        invalid("missing/unknown native fields");
    for (auto n : names)
        if (!v.object.contains(std::string(n)))
            invalid("missing native field: " + std::string(n));
}
inline const Json& at(const Json& v, std::string_view key) {
    if (v.kind != Json::Object)
        invalid();
    auto it = v.object.find(std::string(key));
    if (it == v.object.end())
        invalid();
    return it->second;
}
template <class T> Json encode(const T&);
template <class T> T decode(const Json&);
template <class T> struct Codec;
template <class T> Json encode(const T& x) {
    return Codec<T>::put(x);
}
template <class T> T decode(const Json& x) {
    return Codec<T>::get(x);
}
template <> struct Codec<std::string> {
    static Json put(const std::string& s) {
        return Json::str(s);
    }
    static std::string get(const Json& v) {
        if (v.kind != Json::String)
            invalid();
        return v.text;
    }
};
template <> struct Codec<bool> {
    static Json put(bool b) {
        Json j;
        j.kind = Json::Bool;
        j.text = b ? "true" : "false";
        return j;
    }
    static bool get(const Json& j) {
        if (j.kind != Json::Bool)
            invalid();
        return j.text == "true";
    }
};
template <class T>
    requires std::is_integral_v<T> && (!std::is_same_v<T, bool>)
struct Codec<T> {
    static Json put(T x) {
        return Json::num(std::to_string(x));
    }
    static T get(const Json& v) {
        if (v.kind != Json::Number)
            invalid();
        T x;
        auto [e, ec] = std::from_chars(v.text.data(), v.text.data() + v.text.size(), x);
        if (ec != std::errc{} || e != v.text.data() + v.text.size())
            invalid("native integer out of range");
        return x;
    }
};
template <> struct Codec<float> {
    static Json put(float x) {
        if (!std::isfinite(x))
            invalid("non-finite native float cannot be represented by JSON");
        char b[64];
        auto [p, e] = std::to_chars(b, b + 64, x, std::chars_format::general);
        if (e != std::errc{})
            invalid();
        return Json::num(std::string(b, p));
    }
    static float get(const Json& v) {
        if (v.kind != Json::Number)
            invalid();
        double d = std::strtod(v.text.c_str(), nullptr);
        return static_cast<float>(d);
    }
};
template <class T> struct Codec<std::optional<T>> {
    static Json put(const std::optional<T>& x) {
        return x ? encode(*x) : Json{};
    }
    static std::optional<T> get(const Json& j) {
        return j.kind == Json::Null ? std::nullopt : std::optional<T>(decode<T>(j));
    }
};
template <class T> struct Codec<std::vector<T>> {
    static Json put(const std::vector<T>& a) {
        auto j = Json::arr();
        for (auto& x : a)
            j.array.push_back(encode(x));
        return j;
    }
    static std::vector<T> get(const Json& j) {
        if (j.kind != Json::Array)
            invalid();
        std::vector<T> a;
        for (auto& x : j.array)
            a.push_back(decode<T>(x));
        return a;
    }
};
#define BC_ENUM(Type, ...)                                                                         \
    template <> struct Codec<taffy::Type> {                                                        \
        static constexpr std::string_view names[] = {__VA_ARGS__};                                 \
        static Json put(taffy::Type x) {                                                           \
            auto n = static_cast<size_t>(x);                                                       \
            if (n >= std::size(names))                                                             \
                invalid();                                                                         \
            return Json::str(std::string(names[n]));                                               \
        }                                                                                          \
        static taffy::Type get(const Json& j) {                                                    \
            auto s = decode<std::string>(j);                                                       \
            for (size_t n = 0; n < std::size(names); n++)                                          \
                if (s == names[n])                                                                 \
                    return static_cast<taffy::Type>(n);                                            \
            invalid("unknown native enum: " + s);                                                  \
        }                                                                                          \
    };
BC_ENUM(Display, "Flex", "Grid", "Block", "FlowRoot", "None")
BC_ENUM(Position, "Static", "Relative", "Absolute", "Fixed", "Sticky")
BC_ENUM(BoxSizing, "BorderBox", "ContentBox")
BC_ENUM(Direction, "Ltr", "Rtl")
BC_ENUM(Overflow, "Visible", "Clip", "Hidden", "Scroll")
BC_ENUM(Float, "None", "Left", "Right")
BC_ENUM(Clear, "None", "Left", "Right", "Both")
BC_ENUM(TextAlign, "Auto", "LegacyLeft", "LegacyRight", "LegacyCenter")
BC_ENUM(FlexDirection, "Row", "Column", "RowReverse", "ColumnReverse")
BC_ENUM(FlexWrap, "NoWrap", "Wrap", "WrapReverse", "Balance", "BalanceReverse")
BC_ENUM(GridAutoFlow, "Row", "Column", "RowDense", "ColumnDense")
BC_ENUM(AlignItemsKeyword, "Normal", "Start", "End", "FlexStart", "FlexEnd", "SelfStart", "SelfEnd",
        "Center", "Baseline", "Stretch")
BC_ENUM(AlignContentKeyword, "Normal", "Start", "End", "FlexStart", "FlexEnd", "Center", "Stretch",
        "SpaceBetween", "SpaceEvenly", "SpaceAround")
#undef BC_ENUM
// Rust alignments serialize as canonical tag strings, not keyword/safety objects.
template <class T> struct AlignmentCodec {
    static Json put(T a) {
        std::string n = decode<std::string>(encode(a.keyword));
        if (n != "Normal" && n != "Baseline" && n != "Stretch" && !n.starts_with("Space")) {
            if (a.safety == taffy::AlignmentSafety::Safe)
                n = "Safe" + n;
            if (a.safety == taffy::AlignmentSafety::Unsafe)
                n = "Unsafe" + n;
        }
        return Json::str(n);
    }
    static T get(const Json& j) {
        auto n = decode<std::string>(j);
        auto safety = taffy::AlignmentSafety::Default;
        if (n.starts_with("Safe")) {
            safety = taffy::AlignmentSafety::Safe;
            n.erase(0, 4);
        } else if (n.starts_with("Unsafe")) {
            safety = taffy::AlignmentSafety::Unsafe;
            n.erase(0, 6);
        }
        if (safety != taffy::AlignmentSafety::Default &&
            (n == "Normal" || n == "Baseline" || n == "Stretch" || n.starts_with("Space")))
            invalid();
        T a;
        a.keyword = decode<decltype(a.keyword)>(Json::str(n));
        a.safety = safety;
        return a;
    }
};
template <> struct Codec<taffy::AlignItems> : AlignmentCodec<taffy::AlignItems> {};
template <> struct Codec<taffy::AlignContent> : AlignmentCodec<taffy::AlignContent> {};
// Contain is a one-byte bitset in both implementations. Retain even reserved
// u8 bits accepted by native derived serde, without changing the production API.
template <> struct Codec<taffy::Contain> {
    static_assert(sizeof(taffy::Contain) == 1 && std::is_trivially_copyable_v<taffy::Contain>);
    static Json put(taffy::Contain c) {
        return encode(std::bit_cast<std::uint8_t>(c));
    }
    static taffy::Contain get(const Json& j) {
        return std::bit_cast<taffy::Contain>(decode<std::uint8_t>(j));
    }
};
// Native serialized CompactLength rotates its pointer representation 32 bits:
// upper 32 bits = tag, lower 32 bits = exact f32 payload.
template <taffy::detail::LengthDomain D> struct Codec<taffy::BasicLength<D>> {
    using T = taffy::BasicLength<D>;
    static Json put(T x) {
        using K = taffy::LengthKind;
        unsigned tag = 0;
        std::uint32_t bits = 0;
        switch (x.kind()) {
        case K::Length:
            tag = 1;
            break;
        case K::Percent:
            tag = 5;
            break;
        case K::Auto:
            tag = 3;
            break;
        case K::MinContent:
            tag = 11;
            break;
        case K::MaxContent:
            tag = 27;
            break;
        case K::FitContent:
            tag = 59;
            break;
        case K::FitContentPx:
            tag = 43;
            break;
        case K::FitContentPercent:
            tag = 15;
            break;
        case K::Stretch:
            tag = 75;
            break;
        case K::Content:
            tag = 35;
            break;
        case K::Fr:
            tag = 19;
            break;
        default:
            invalid("calc cannot be transported by native serde");
        }
        if (tag == 1 || tag == 5 || tag == 43 || tag == 15 || tag == 19)
            bits = std::bit_cast<std::uint32_t>(x.value());
        return encode((std::uint64_t(tag) << 32) | bits);
    }
    static T get(const Json& j) {
        auto n = decode<std::uint64_t>(j);
        auto tag = n >> 32;
        // C++ keyword lengths have no payload storage. Reject rather than
        // silently canonicalizing a raw native serde bit pattern.
        if (tag != 1 && tag != 5 && tag != 43 && tag != 15 && tag != 19 &&
            std::uint32_t(n) != 0)
            invalid("unsupported noncanonical packed keyword payload");
        float f = std::bit_cast<float>(std::uint32_t(n));
        switch (tag) {
        case 1:
            return T::length(f);
        case 5:
            return T::percent(f);
        case 3:
            if constexpr (requires { T::auto_(); })
                return T::auto_();
            break;
        case 11:
            if constexpr (requires { T::min_content(); })
                return T::min_content();
            break;
        case 27:
            if constexpr (requires { T::max_content(); })
                return T::max_content();
            break;
        case 59:
            if constexpr (requires { T::fit_content(); })
                return T::fit_content();
            break;
        case 43:
            if constexpr (requires { T::fit_content_px(f); })
                return T::fit_content_px(f);
            break;
        case 15:
            if constexpr (requires { T::fit_content_percent(f); })
                return T::fit_content_percent(f);
            break;
        case 75:
            if constexpr (requires { T::stretch(); })
                return T::stretch();
            break;
        case 35:
            if constexpr (requires { T::content(); })
                return T::content();
            break;
        case 19:
            if constexpr (requires { T::fr(f); })
                return T::fr(f);
            break;
        }
        invalid("invalid packed length for domain");
    }
};
#define BC_PAIR(Type, A, B)                                                                        \
    template <class T> struct Codec<taffy::Type<T>> {                                              \
        static Json put(const taffy::Type<T>& x) {                                                 \
            auto j = Json::obj();                                                                  \
            j.object[#A] = encode(x.A);                                                            \
            j.object[#B] = encode(x.B);                                                            \
            return j;                                                                              \
        }                                                                                          \
        static taffy::Type<T> get(const Json& j) {                                                 \
            keys(j, {#A, #B});                                                                     \
            return {decode<T>(at(j, #A)), decode<T>(at(j, #B))};                                   \
        }                                                                                          \
    };
BC_PAIR(Size, width, height)
BC_PAIR(Point, x, y)
BC_PAIR(Line, start, end)
#undef BC_PAIR
template <class T> struct Codec<taffy::Rect<T>> {
    static Json put(const taffy::Rect<T>& x) {
        auto j = Json::obj();
        j.object["left"] = encode(x.left);
        j.object["right"] = encode(x.right);
        j.object["top"] = encode(x.top);
        j.object["bottom"] = encode(x.bottom);
        return j;
    }
    static taffy::Rect<T> get(const Json& j) {
        keys(j, {"left", "right", "top", "bottom"});
        return {decode<T>(at(j, "left")), decode<T>(at(j, "right")), decode<T>(at(j, "top")),
                decode<T>(at(j, "bottom"))};
    }
};
template <> struct Codec<taffy::TrackSizingFunction> {
    static Json put(const taffy::TrackSizingFunction& x) {
        auto j = Json::obj();
        j.object["min"] = encode(x.min);
        j.object["max"] = encode(x.max);
        return j;
    }
    static taffy::TrackSizingFunction get(const Json& j) {
        keys(j, {"min", "max"});
        return {decode<taffy::MinTrackSizingFunction>(at(j, "min")),
                decode<taffy::MaxTrackSizingFunction>(at(j, "max"))};
    }
};
template <> struct Codec<taffy::RepetitionCount> {
    static Json put(taffy::RepetitionCount x) {
        using K = taffy::RepetitionCount::Kind;
        if (x.kind() == K::AutoFill)
            return Json::str("AutoFill");
        if (x.kind() == K::AutoFit)
            return Json::str("AutoFit");
        auto j = Json::obj();
        j.object["Count"] = encode(x.value());
        return j;
    }
    static taffy::RepetitionCount get(const Json& j) {
        if (j.kind == Json::String) {
            if (j.text == "AutoFill")
                return taffy::RepetitionCount::auto_fill();
            if (j.text == "AutoFit")
                return taffy::RepetitionCount::auto_fit();
            invalid();
        }
        keys(j, {"Count"});
        return taffy::RepetitionCount::count(decode<std::uint16_t>(at(j, "Count")));
    }
};
template <> struct Codec<taffy::GridTemplateRepetition> {
    static Json put(const taffy::GridTemplateRepetition& x) {
        auto j = Json::obj();
        j.object["count"] = encode(x.count);
        j.object["tracks"] = encode(x.tracks);
        j.object["line_names"] = encode(x.line_names);
        return j;
    }
    static taffy::GridTemplateRepetition get(const Json& j) {
        keys(j, {"count", "tracks", "line_names"});
        return {decode<taffy::RepetitionCount>(at(j, "count")),
                decode<std::vector<taffy::TrackSizingFunction>>(at(j, "tracks")),
                decode<taffy::TemplateLineNames>(at(j, "line_names"))};
    }
};
template <> struct Codec<taffy::GridTemplateComponent> {
    static Json put(const taffy::GridTemplateComponent& x) {
        auto j = Json::obj();
        if (auto p = std::get_if<taffy::TrackSizingFunction>(&x.value))
            j.object["Single"] = encode(*p);
        else
            j.object["Repeat"] = encode(std::get<taffy::GridTemplateRepetition>(x.value));
        return j;
    }
    static taffy::GridTemplateComponent get(const Json& j) {
        if (j.kind != Json::Object || j.object.size() != 1)
            invalid();
        if (j.object.contains("Single"))
            return decode<taffy::TrackSizingFunction>(at(j, "Single"));
        keys(j, {"Repeat"});
        return decode<taffy::GridTemplateRepetition>(at(j, "Repeat"));
    }
};
template <> struct Codec<taffy::GridPlacement> {
    static Json put(const taffy::GridPlacement& x) {
        using K = taffy::GridPlacement::Kind;
        auto j = Json::obj();
        switch (x.kind()) {
        case K::Auto:
            return Json::str("Auto");
        case K::Line:
            j.object["Line"] = encode(x.line_index());
            break;
        case K::Span:
            j.object["Span"] = encode(x.span_count());
            break;
        case K::NamedLine:
        case K::NamedSpan: {
            auto a = Json::arr();
            a.array.push_back(encode(x.name()));
            a.array.push_back(x.kind() == K::NamedLine ? encode(x.line_index())
                                                       : encode(x.span_count()));
            j.object[x.kind() == K::NamedLine ? "NamedLine" : "NamedSpan"] = a;
            break;
        }
        }
        return j;
    }
    static taffy::GridPlacement get(const Json& j) {
        using P = taffy::GridPlacement;
        if (j.kind == Json::String) {
            if (j.text == "Auto")
                return {};
            invalid();
        }
        if (j.kind != Json::Object || j.object.size() != 1)
            invalid();
        auto& [n, v] = *j.object.begin();
        if (n == "Line")
            return P::line(decode<std::int16_t>(v));
        if (n == "Span")
            return P::span(decode<std::uint16_t>(v));
        if ((n != "NamedLine" && n != "NamedSpan") || v.kind != Json::Array || v.array.size() != 2)
            invalid();
        auto name = decode<std::string>(v.array[0]);
        return n == "NamedLine" ? P::named_line(name, decode<std::int16_t>(v.array[1]))
                                : P::named_span(name, decode<std::uint16_t>(v.array[1]));
    }
};
template <> struct Codec<taffy::GridTemplateArea> {
    static Json put(const taffy::GridTemplateArea& x) {
        auto j = Json::obj();
        j.object["name"] = encode(x.name);
        j.object["row_start"] = encode(x.row_start);
        j.object["row_end"] = encode(x.row_end);
        j.object["column_start"] = encode(x.column_start);
        j.object["column_end"] = encode(x.column_end);
        return j;
    }
    static taffy::GridTemplateArea get(const Json& j) {
        keys(j, {"name", "row_start", "row_end", "column_start", "column_end"});
        return {decode<std::string>(at(j, "name")), decode<std::uint16_t>(at(j, "row_start")),
                decode<std::uint16_t>(at(j, "row_end")),
                decode<std::uint16_t>(at(j, "column_start")),
                decode<std::uint16_t>(at(j, "column_end"))};
    }
};
template <> struct Codec<taffy::GridTemplateAreas> {
    static Json put(const taffy::GridTemplateAreas& x) {
        auto j = Json::obj();
        j.object["areas"] = encode(x.areas);
        j.object["row_count"] = encode(x.row_count);
        j.object["column_count"] = encode(x.column_count);
        return j;
    }
    static taffy::GridTemplateAreas get(const Json& j) {
        keys(j, {"areas", "row_count", "column_count"});
        return {decode<std::vector<taffy::GridTemplateArea>>(at(j, "areas")),
                decode<std::uint16_t>(at(j, "row_count")),
                decode<std::uint16_t>(at(j, "column_count"))};
    }
};
// One field list drives both directions; every native field (including dummy) required.
#define BC_STYLE_FIELDS(X)                                                                         \
    X(display, display)                                                                            \
    X(item_is_table, item_is_table) X(item_is_replaced, item_is_replaced) X(                       \
        item_is_compressible_replaced, item_is_compressible_replaced) X(box_sizing, box_sizing)    \
        X(direction, direction) X(overflow, overflow) X(scrollbar_width, scrollbar_width)          \
            X(contain, contain) X(float, float_) X(clear, clear) X(position, position) X(          \
                inset, inset) X(size, size) X(min_size, min_size) X(max_size, max_size)            \
                X(aspect_ratio, aspect_ratio) X(margin, margin) X(padding, padding) X(             \
                    border, border) X(align_items, align_items) X(align_self, align_self)          \
                    X(justify_items, justify_items) X(justify_self, justify_self) X(               \
                        align_content, align_content) X(justify_content, justify_content)          \
                        X(gap, gap) X(text_align, text_align) X(flex_direction, flex_direction) X( \
                            flex_wrap, flex_wrap) X(flex_line_count, flex_line_count)              \
                            X(flex_basis, flex_basis) X(flex_grow, flex_grow)                      \
                                X(flex_shrink,                                                     \
                                  flex_shrink) X(grid_template_rows, grid_template_rows)           \
                                    X(grid_template_columns, grid_template_columns)                \
                                        X(grid_auto_rows, grid_auto_rows)                          \
                                            X(grid_auto_columns, grid_auto_columns)                \
                                                X(grid_auto_flow, grid_auto_flow)                  \
                                                    X(grid_template_areas, grid_template_areas)    \
                                                        X(grid_template_column_names,              \
                                                          grid_template_column_names)              \
                                                            X(grid_template_row_names,             \
                                                              grid_template_row_names)             \
                                                                X(grid_row, grid_row)              \
                                                                    X(grid_column, grid_column)
template <> struct Codec<taffy::Style> {
    static Json put(const taffy::Style& x) {
        auto j = Json::obj();
        j.object["dummy"] = Json{};
#define BC_PUT(N, F) j.object[#N] = encode(x.F);
        BC_STYLE_FIELDS(BC_PUT)
#undef BC_PUT
        return j;
    }
    static taffy::Style get(const Json& j) {
        taffy::Style x;
        auto expected = put(x);
        if (j.kind != Json::Object || j.object.size() != expected.object.size())
            invalid("complete Style object required");
        for (auto& [n, v] : expected.object)
            if (!j.object.contains(n))
                invalid("missing native Style field: " + n);
        if (at(j, "dummy").kind != Json::Null)
            invalid();
#define BC_GET(N, F) x.F = decode<decltype(x.F)>(at(j, #N));
        BC_STYLE_FIELDS(BC_GET)
#undef BC_GET
        // Like the native transport, serialization fails for non-finite ordinary floats.
        (void)put(x);
        return x;
    }
};
#undef BC_STYLE_FIELDS
} // namespace style_detail

inline std::string style_json(const taffy::Style& style) {
    return style_detail::dump(style_detail::encode(style));
}
inline taffy::Style parse_style_json(std::string_view text) {
    return style_detail::decode<taffy::Style>(style_detail::Parser{text}.parse());
}
inline void set_native_field(taffy::Style& style, std::string_view name, std::string_view text) {
    auto j = style_detail::encode(style);
    auto it = j.object.find(std::string(name));
    if (it == j.object.end())
        style_detail::invalid("unknown native field: " + std::string(name));
    it->second = style_detail::Parser{text}.parse();
    auto next = style_detail::decode<taffy::Style>(j);
    style = std::move(next);
}
// Deliberately Value-only: arbitrary JSON and duplicate keys accepted, no Style schema.
inline style_detail::Json parse_json_value(std::string_view text) {
    return style_detail::Parser{text, 0, false}.parse();
}
inline taffy::Style parse_properties(const BehaviorProperty* properties, size_t count) {
    if (count && !properties)
        style_detail::invalid("null properties");
    taffy::Style style;
    std::set<std::string> seen;
    static const std::set<std::string_view> allowed = {"display",
                                                       "direction",
                                                       "item-is-table",
                                                       "item-is-replaced",
                                                       "item-is-compressible-replaced",
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
                                                       "left",
                                                       "right",
                                                       "top",
                                                       "bottom",
                                                       "margin-left",
                                                       "margin-right",
                                                       "margin-top",
                                                       "margin-bottom",
                                                       "padding-left",
                                                       "padding-right",
                                                       "padding-top",
                                                       "padding-bottom",
                                                       "border-left",
                                                       "border-right",
                                                       "border-top",
                                                       "border-bottom",
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
                                                       "grid-template-rows",
                                                       "grid-template-columns",
                                                       "grid-auto-rows",
                                                       "grid-auto-columns",
                                                       "grid-template-areas",
                                                       "grid-row-start",
                                                       "grid-row-end",
                                                       "grid-column-start",
                                                       "grid-column-end"};
    auto text = [](BehaviorText t) {
        if (t.len && !t.data)
            style_detail::invalid("null text");
        std::string_view s(t.data ? t.data : "", t.len);
        style_detail::utf8(s);
        return s;
    };
    for (size_t i = 0; i < count; i++) {
        auto name = text(properties[i].name), value = text(properties[i].value);
        if (!seen.insert(std::string(name)).second)
            style_detail::invalid("duplicate property: " + std::string(name));
        if (!allowed.contains(name))
            style_detail::invalid("unknown property: " + std::string(name));
        if (name == "grid-template-areas") {
            style.grid_template_areas =
                style_detail::decode<taffy::GridTemplateAreas>(style_detail::Parser{value}.parse());
            continue;
        }
        // Native bool/u16/f32 properties use scalar FromStr, not CSS tokens.
        if (name == "item-is-table" || name == "item-is-replaced" ||
            name == "item-is-compressible-replaced") {
            if (value != "true" && value != "false")
                style_detail::invalid("invalid native bool");
        }
        if (name == "flex-line-count") {
            auto raw = value;
            if (!raw.empty() && raw.front() == '+')
                raw.remove_prefix(1);
            style.flex_line_count =
                style_detail::decode<std::uint16_t>(style_detail::Json::num(std::string(raw)));
            continue;
        }
        if (name == "aspect-ratio" || name == "flex-grow" || name == "flex-shrink" ||
            name == "scrollbar-width") {
            if (value.empty() || value.find_first_of(" \t\r\n\f/") != std::string_view::npos)
                style_detail::invalid("invalid native scalar");
            if (name == "aspect-ratio") {
                taffy::Style tmp;
                auto r = taffy::parse_style_property(tmp, "flex-grow", value);
                if (!r)
                    style_detail::invalid(r.error().message);
                style.aspect_ratio = tmp.flex_grow;
                continue;
            }
        }
        auto result = taffy::parse_style_property(style, name, value);
        if (!result)
            style_detail::invalid("invalid property " + std::string(name) + ": " +
                                  result.error().message);
    }
    return style;
}
} // namespace behavior_cpp
