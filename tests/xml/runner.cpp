#include "measure.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <clocale>
#include <pugixml.hpp>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#ifndef XML_BACKEND_LABEL
#define XML_BACKEND_LABEL "native-rust-xml-oracle-with-cpp-intrinsic"
#endif

namespace {
using namespace std::literals;
namespace fs = std::filesystem;
[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}
XmlOracleString span(std::string_view s) {
    return {s.data(), s.size()};
}
XmlOracleString optional_span(pugi::xml_attribute a) {
    return a ? span(a.value()) : XmlOracleString{nullptr, 0};
}
std::string quote(std::string_view input) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : input) {
        switch (c) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (c < 32)
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c)
                    << std::dec;
            else
                out << c;
        }
    }
    return out.str() + '"';
}
// Rust str::{trim,split_whitespace} use Unicode White_Space, not locale whitespace.
std::size_t whitespace_length(std::string_view t) {
    if (t.empty())
        return 0;
    const auto c = static_cast<unsigned char>(t[0]);
    if ((c >= 9 && c <= 13) || c == 32)
        return 1;
    for (auto w :
         {"\xC2\x85"sv, "\xC2\xA0"sv, "\xE1\x9A\x80"sv, "\xE2\x80\x80"sv, "\xE2\x80\x81"sv,
          "\xE2\x80\x82"sv, "\xE2\x80\x83"sv, "\xE2\x80\x84"sv, "\xE2\x80\x85"sv, "\xE2\x80\x86"sv,
          "\xE2\x80\x87"sv, "\xE2\x80\x88"sv, "\xE2\x80\x89"sv, "\xE2\x80\x8A"sv, "\xE2\x80\xA8"sv,
          "\xE2\x80\xA9"sv, "\xE2\x80\xAF"sv, "\xE2\x81\x9F"sv, "\xE3\x80\x80"sv})
        if (t.starts_with(w))
            return w.size();
    return 0;
}
std::string_view trim_start_rust(std::string_view s) {
    while (auto n = whitespace_length(s))
        s.remove_prefix(n);
    return s;
}
std::string trim_rust(std::string_view s) {
    std::size_t first = 0, last_nonspace = 0;
    while (first < s.size()) {
        auto n = whitespace_length(s.substr(first));
        if (!n)
            break;
        first += n;
    }
    for (std::size_t i = first; i < s.size();) {
        auto n = whitespace_length(s.substr(i));
        if (n)
            i += n;
        else {
            ++i;
            last_nonspace = i;
        }
    }
    return last_nonspace > first ? std::string(s.substr(first, last_nonspace - first)) : "";
}
void attributes(pugi::xml_node node, const std::set<std::string_view>& allowed,
                std::string_view path) {
    std::set<std::string_view> seen;
    for (auto a : node.attributes()) {
        if (!seen.insert(a.name()).second)
            fail(std::string(path) + ": duplicate attribute " + a.name());
        if (!allowed.contains(a.name()))
            fail(std::string(path) + ": unknown attribute " + a.name());
    }
}
std::vector<pugi::xml_node> elements(pugi::xml_node node, std::string_view path,
                                     bool allow_text = false) {
    std::vector<pugi::xml_node> out;
    for (auto child : node.children()) {
        if (child.type() == pugi::node_element)
            out.push_back(child);
        else if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            if (!allow_text && !trim_rust(child.value()).empty())
                fail(std::string(path) + ": unexpected text");
        } else if (child.type() != pugi::node_comment)
            fail(std::string(path) + ": unexpected XML node type");
    }
    return out;
}
float number(std::string_view text, std::string_view label) {
    // Rust's finite decimal f32 grammar: one sign, decimal digits/dot, optional
    // exponent. Do not let strtof accept whitespace, hex floats, or a second sign.
    auto invalid = [&] { fail(std::string(label) + ": invalid/nonfinite number " + quote(text)); };
    std::size_t i = 0, digits = 0;
    if (i < text.size() && (text[i] == '+' || text[i] == '-'))
        ++i;
    auto consume_digits = [&] {
        std::size_t count = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            ++i;
            ++count;
        }
        return count;
    };
    digits += consume_digits();
    if (i < text.size() && text[i] == '.') {
        ++i;
        digits += consume_digits();
    }
    if (!digits)
        invalid();
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-'))
            ++i;
        if (!consume_digits())
            invalid();
    }
    if (i != text.size())
        invalid();
    // C-locale strtof rounds directly to f32 and, unlike from_chars, returns
    // signed zero/subnormals on underflow. ERANGE is not itself a failure:
    // Rust accepts underflow too; overflow is rejected by the finite check.
    // main owns this standalone, synchronous test process's numeric locale.
    // No per-number locale mutation or platform-specific locale API is needed.
    const std::string copy(text);
    char* end = nullptr;
    const float value = std::strtof(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value))
        invalid();
    return value;
}
float required_number(pugi::xml_node n, const char* key, std::string_view path) {
    auto a = n.attribute(key);
    if (!a)
        fail(std::string(path) + ": missing expected " + key);
    return number(a.value(), std::string(path) + "/" + key);
}
void close(float expected, float actual, std::string_view path) {
    if (!std::isfinite(expected) || !std::isfinite(actual) ||
        !(std::abs(expected - actual) < 0.1f)) {
        std::ostringstream out;
        out << path << ": expected=" << std::setprecision(9) << expected << " actual=" << actual
            << " (requires finite abs difference < 0.1)";
        fail(out.str());
    }
}
using Token = std::variant<std::vector<std::string>, float>;
std::vector<Token> tracks(std::string_view input) {
    const auto trimmed = trim_rust(input);
    if (trimmed == "none")
        return {};
    std::string_view rest = trimmed;
    std::vector<Token> out;
    while (!rest.empty()) {
        rest = trim_start_rust(rest);
        if (rest.empty())
            break;
        if (rest.starts_with('[')) {
            const auto end = rest.find(']');
            if (end == rest.npos)
                fail("unterminated resolved track names: " + quote(input));
            auto names = rest.substr(1, end - 1);
            std::vector<std::string> group;
            while (!(names = trim_start_rust(names)).empty()) {
                std::size_t length = 0;
                while (length < names.size() && !whitespace_length(names.substr(length)))
                    ++length;
                group.emplace_back(names.substr(0, length));
                names.remove_prefix(length);
            }
            out.emplace_back(std::move(group));
            rest.remove_prefix(end + 1);
        } else {
            const auto end = rest.find(' '); // upstream separates numeric tokens at ASCII space
            auto size = rest.substr(0, end);
            if (!size.ends_with("px"))
                fail("invalid resolved track size: " + quote(size));
            size.remove_suffix(2);
            out.emplace_back(number(size, "resolved track size"));
            rest.remove_prefix(end == rest.npos ? rest.size() : end);
        }
    }
    return out;
}
void compare_tracks(std::string_view expected, std::string_view actual, std::string_view path) {
    const auto e = tracks(expected), a = tracks(actual);
    if (e.size() != a.size())
        fail(std::string(path) + ": track token count differs expected=" + quote(expected) +
             " actual=" + quote(actual));
    for (std::size_t i = 0; i < e.size(); ++i) {
        if (e[i].index() != a[i].index())
            fail(std::string(path) + ": track token type differs");
        if (auto v = std::get_if<float>(&e[i]))
            close(*v, std::get<float>(a[i]),
                  std::string(path) + "/token[" + std::to_string(i) + "]");
        else if (std::get<0>(e[i]) != std::get<0>(a[i]))
            fail(std::string(path) + ": track line names differ expected=" + quote(expected) +
                 " actual=" + quote(actual));
    }
}
// Exact keys consumed by upstream tests/xml.rs::build_style. writing-mode is
// parsed separately for leaf measurement; no other ignored input keys exist.
const std::set<std::string_view> style_keys = {"display",
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
                                               "grid-template-rows",
                                               "grid-template-columns",
                                               "grid-auto-rows",
                                               "grid-auto-columns",
                                               "grid-row-start",
                                               "grid-row-end",
                                               "grid-column-start",
                                               "grid-column-end",
                                               "writing-mode"};
struct Expected {
    uint64_t handle;
    pugi::xml_node node;
    std::string path;
    std::vector<Expected> children;
};
struct Fixture {
    XmlOracle* tree = nullptr;
    xml_test::Contexts contexts;
    std::string injection;
    bool injected = false;
    bool debug_cache_checked = false;
    explicit Fixture(std::string inject) : injection(std::move(inject)) {
        check(xml_oracle_create(&tree), "create");
    }
    ~Fixture() {
        if (tree)
            xml_oracle_free(tree);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    void check(uint32_t status, std::string_view operation) {
        if (status == ORACLE_OK)
            return;
        std::size_t needed = 0;
        xml_oracle_error(tree, nullptr, 0, &needed);
        std::vector<char> error(needed ? needed : 1);
        xml_oracle_error(tree, error.data(), error.size(), &needed);
        fail(std::string(operation) + ": native status=" + std::to_string(status) + " " +
             error.data());
    }
    Expected construct(pugi::xml_node input, pugi::xml_node expected, std::string path) {
        if (input.name() != "div"sv && input.name() != "text"sv)
            fail(path + ": invalid input tag " + input.name());
        if (expected.name() != "node"sv)
            fail(path + ": expected tag must be node, got " + expected.name());
        attributes(input, style_keys, path);
        attributes(expected,
                   {"x", "y", "width", "height", "scroll_width", "scroll_height", "resolved-rows",
                    "resolved-columns"},
                   path + "/expected");
        for (const char* key : {"x", "y", "width", "height"})
            (void)required_number(expected, key, path);
        if (bool(expected.attribute("scroll_width")) != bool(expected.attribute("scroll_height")))
            fail(path + ": incomplete expected scroll metadata");
        for (const char* key : {"scroll_width", "scroll_height"})
            if (expected.attribute(key))
                (void)required_number(expected, key, path);
        for (const char* key : {"resolved-rows", "resolved-columns"})
            if (expected.attribute(key))
                (void)tracks(expected.attribute(key).value());
        auto in_children = elements(input, path, true),
             ex_children = elements(expected, path + "/expected");
        if (in_children.size() != ex_children.size())
            fail(path + ": input/expected child counts differ: " +
                 std::to_string(in_children.size()) + " vs " + std::to_string(ex_children.size()));
        std::vector<XmlOracleStylePair> pairs;
        for (auto a : input.attributes())
            pairs.push_back({span(a.name()), span(a.value())});
        uint64_t handle = 0;
        check(xml_oracle_new_node(tree, pairs.data(), pairs.size(), &handle), path + "/new_node");
        Expected out{handle, expected, path, {}};
        // roxmltree coalesces adjacent text/CDATA into one text node, but
        // comments/elements interrupt it. Node::text uses only the first child.
        // parse_ws_pcdata preserves whitespace-only text.
        if (in_children.empty()) {
            bool vertical = false;
            if (auto mode = input.attribute("writing-mode")) {
                const std::string_view value = mode.value();
                if (value.find("vertical") != value.npos)
                    vertical = true;
                else if (value.find("horizontal") == value.npos)
                    fail(path + ": invalid writing-mode " + mode.value());
            }
            auto first = input.first_child();
            if (first && (first.type() == pugi::node_pcdata || first.type() == pugi::node_cdata)) {
                std::string text;
                for (auto part = first;
                     part && (part.type() == pugi::node_pcdata || part.type() == pugi::node_cdata);
                     part = part.next_sibling())
                    text += part.value();
                contexts.emplace(handle, xml_test::TextContext{trim_rust(text), vertical, 0});
            }
        } else {
            // writing-mode is deliberately ignored on nonleaves upstream.
            std::vector<uint64_t> handles;
            for (std::size_t i = 0; i < in_children.size(); ++i) {
                out.children.push_back(
                    construct(in_children[i], ex_children[i], path + "/" + std::to_string(i)));
                handles.push_back(out.children.back().handle);
            }
            check(xml_oracle_set_children(tree, handle, handles.data(), handles.size()),
                  path + "/set_children");
        }
        return out;
    }
    std::string resolved(uint64_t node, uint32_t axis) {
        std::size_t needed = 0;
        check(xml_oracle_tracks(tree, node, axis, nullptr, 0, &needed), "tracks length");
        if (!needed)
            fail("tracks: invalid native string length");
        std::vector<char> data(needed);
        check(xml_oracle_tracks(tree, node, axis, data.data(), data.size(), &needed), "tracks");
        if (needed != data.size() || data.back() != '\0')
            fail("tracks: inconsistent native string length");
        return data.data();
    }
    void compare_node(const Expected& e) {
        XmlOracleLayout actual{};
        check(xml_oracle_layout(tree, e.handle, &actual), e.path + "/layout");
        bool has_scroll = actual.has_scroll_size != 0;
        if (!injected) {
            if (injection == "geometry") {
                actual.layout.x += 1;
                injected = true;
            } else if (injection == "nan") {
                actual.layout.x = std::numeric_limits<float>::quiet_NaN();
                injected = true;
            } else if (injection == "missing-scroll" && e.node.attribute("scroll_width")) {
                has_scroll = false;
                injected = true;
            } else if (injection == "missing-grid" && (e.node.attribute("resolved-rows") ||
                                                       e.node.attribute("resolved-columns"))) {
                actual.has_resolved_tracks = 0;
                injected = true;
            }
        }
        close(required_number(e.node, "x", e.path), actual.layout.x, e.path + "/x");
        close(required_number(e.node, "y", e.path), actual.layout.y, e.path + "/y");
        close(required_number(e.node, "width", e.path), actual.layout.size.width,
              e.path + "/width");
        close(required_number(e.node, "height", e.path), actual.layout.size.height,
              e.path + "/height");
        // Match upstream tests/xml.rs: optional metadata is compared only when
        // both the expected and computed values are available.
        if (e.node.attribute("scroll_width") && has_scroll) {
            close(required_number(e.node, "scroll_width", e.path), actual.scroll_size.width,
                  e.path + "/scroll_width");
            close(required_number(e.node, "scroll_height", e.path), actual.scroll_size.height,
                  e.path + "/scroll_height");
        }
        for (const auto& [key, axis] : {std::pair{"resolved-rows", 0u}, {"resolved-columns", 1u}}) {
            if (auto expected = e.node.attribute(key); expected && actual.has_resolved_tracks) {
                compare_tracks(expected.value(), resolved(e.handle, axis), e.path + "/" + key);
            }
        }
    }
    void compare(const Expected& e, std::vector<std::string>& diagnostics) {
        try {
            compare_node(e);
        } catch (const std::exception& error) {
            diagnostics.emplace_back(error.what());
        }
        // Check descendant geometry regardless of optional metadata availability.
        for (const auto& child : e.children)
            compare(child, diagnostics);
    }
    void run(const fs::path& file, std::string_view id) {
        std::ifstream stream(file, std::ios::binary);
        if (!stream)
            fail("cannot read XML " + file.string());
        const std::string bytes((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
        if (stream.bad())
            fail("cannot read XML " + file.string());
        check(xml_oracle_validate_xml(tree, span(bytes)), "XML validation");
        pugi::xml_document doc;
        auto parsed =
            doc.load_buffer(bytes.data(), bytes.size(),
                            pugi::parse_default | pugi::parse_ws_pcdata | pugi::parse_comments,
                            pugi::encoding_utf8);
        if (!parsed)
            fail("XML parse: " + std::string(parsed.description()) +
                 " offset=" + std::to_string(parsed.offset));
        auto top = elements(doc, "document");
        if (top.size() != 1 || top[0].name() != "test"sv)
            fail("document must contain exactly one test element");
        auto root = top[0];
        attributes(root, {"name", "use-rounding"}, "test");
        if (!root.attribute("name") ||
            root.attribute("name").value() != fs::path(id).filename().string())
            fail("test name does not match fixture ID");
        uint32_t rounding = 1;
        if (auto a = root.attribute("use-rounding")) {
            if (a.value() == "true"sv)
                rounding = 1;
            else if (a.value() == "false"sv)
                rounding = 0;
            else
                fail("invalid use-rounding (requires true/false)");
        }
        auto wrappers = elements(root, "test");
        if (wrappers.size() != 3 || wrappers[0].name() != "viewport"sv ||
            wrappers[1].name() != "input"sv || wrappers[2].name() != "expectations"sv)
            fail("test requires exactly viewport,input,expectations in order");
        auto viewport = wrappers[0];
        attributes(viewport, {"width", "height"}, "viewport");
        if (!elements(viewport, "viewport").empty())
            fail("viewport must be empty");
        attributes(wrappers[1], {}, "input");
        attributes(wrappers[2], {}, "expectations");
        auto inputs = elements(wrappers[1], "input"),
             expectations = elements(wrappers[2], "expectations");
        if (inputs.size() != 1 || expectations.size() != 1)
            fail("input/expectations each require exactly one root node");
        auto expected = construct(inputs[0], expectations[0], "root");
        uint64_t cache_before = 0, cache_after = 0;
        uint32_t cache_available = 0, cache_available_after = 0;
        check(xml_oracle_cache_mode_change_evictions(tree, &cache_before, &cache_available),
              "cache counter before");
        check(xml_oracle_compute(tree, expected.handle, optional_span(viewport.attribute("width")),
                                 optional_span(viewport.attribute("height")), rounding, &contexts,
                                 xml_test::measure),
              "compute");
        check(xml_oracle_cache_mode_change_evictions(tree, &cache_after, &cache_available_after),
              "cache counter after");
        if (cache_available != cache_available_after)
            fail("cache counter availability changed");
        if (cache_available && cache_before != cache_after)
            fail("cache mode changed during first layout (upstream debug assertion)");
        debug_cache_checked = cache_available != 0;
        std::vector<std::string> diagnostics;
        compare(expected, diagnostics);
        if (!diagnostics.empty()) {
            std::string message;
            for (const auto& diagnostic : diagnostics) {
                if (!message.empty())
                    message += "; ";
                message += diagnostic;
            }
            fail(message);
        }
        if (!injection.empty() && !injected)
            fail("injection not applied: fixture lacks required expected metadata");
    }
};
struct Result {
    std::string id, diagnostic;
    bool injected, debug_cache_checked;
};
void report(const fs::path& path, const std::vector<Result>& results, std::string_view injection,
            std::size_t registered) {
    std::ofstream out(path);
    if (!out)
        fail("cannot create report " + path.string());
    const auto failures = std::count_if(results.begin(), results.end(),
                                        [](const auto& r) { return !r.diagnostic.empty(); });
    out << "{\n  \"backend\": \"" XML_BACKEND_LABEL "\",\n  \"registered\": "
        << registered << ",\n  \"executed\": " << results.size()
        << ",\n  \"passed\": " << results.size() - failures << ",\n  \"failed\": " << failures
        << ",\n  \"debug_cache_checks\": "
        << std::count_if(results.begin(), results.end(),
                         [](const auto& r) { return r.debug_cache_checked; })
        << ",\n  \"injection\": " << quote(injection) << ",\n  \"fixtures\": [\n";
    for (std::size_t i = 0; i < results.size(); ++i) {
        auto& r = results[i];
        out << "    {\"id\": " << quote(r.id)
            << ", \"status\": " << quote(r.diagnostic.empty() ? "pass" : "fail")
            << ", \"injected\": " << (r.injected ? "true" : "false")
            << ", \"debug_cache_checked\": " << (r.debug_cache_checked ? "true" : "false")
            << ", \"diagnostic\": " << quote(r.diagnostic) << "}"
            << (i + 1 == results.size() ? "\n" : ",\n");
    }
    out << "  ]\n}\n";
    out.close();
    if (!out)
        fail("cannot write report " + path.string());
}
void self_test() {
    std::size_t checked = 0;
    auto rejects = [&](const auto& operation) {
        bool rejected = false;
        try {
            operation();
        } catch (const std::exception&) {
            rejected = true;
        }
        if (!rejected)
            fail("self-test: expected rejection");
        ++checked;
    };
    close(0, 0.099f, "inside tolerance");
    ++checked;
    rejects([] { close(0, 0.1f, "boundary"); });
    rejects([] { close(0, -0.1f, "negative boundary"); });
    rejects([] { close(0, std::numeric_limits<float>::quiet_NaN(), "NaN"); });
    rejects([] { close(0, std::numeric_limits<float>::infinity(), "infinity"); });
    compare_tracks("[a b] 20px [c]", "[a b] 20.05px [c]", "tracks");
    ++checked;
    compare_tracks("none", "none", "empty tracks");
    ++checked;
    rejects([] { compare_tracks("[a b] 20px", "[b a] 20px", "name order"); });
    rejects([] { compare_tracks("[a] 20px", "[b] 20px", "name spelling"); });
    rejects([] { compare_tracks("[a] 20px", "20px [a]", "token order"); });
    rejects([] { compare_tracks("10px", "10px 10px", "token count"); });
    rejects([] { compare_tracks("none", "0px", "empty is not zero track"); });
    rejects([] { (void)tracks("nanpx"); });
    rejects([] { (void)tracks("infpx"); });
    rejects([] { (void)tracks("[unterminated"); });
    compare_tracks("[a\v\fb] 20px", "[a b] 20px", "Unicode whitespace including VT/FF");
    ++checked;
    compare_tracks("[a\xC2\xA0"
                   "b] 20px",
                   "[a b] 20px", "NBSP names");
    ++checked;
    for (auto s : {"+-0", "++0", "--0", "-+0", "0x0p0", " 0", "0 ", ".", "1e", "1e+", "1e50"})
        rejects([&] { (void)number(s, "numeric grammar"); });
    if (number("1e-50", "underflow") != 0 || !std::signbit(number("-1e-50", "negative underflow")))
        fail("self-test: signed underflow");
    ++checked;
    if (number("1e-45", "subnormal") != std::numeric_limits<float>::denorm_min())
        fail("self-test: subnormal");
    ++checked;
    if (number("+.0e+2", "plus") != 0 || number("1.", "dot") != 1 ||
        number(".5", "fraction") != 0.5f)
        fail("self-test: valid decimal grammar");
    ++checked;
    std::cout << "SELF TEST passed=" << checked << '\n';
}
std::vector<std::string> discover_fixtures(const fs::path& corpus) {
    std::vector<std::string> ids;
    for (const auto& entry : fs::recursive_directory_iterator(corpus))
        if (entry.is_regular_file() && entry.path().extension() == ".xml")
            ids.push_back(
                entry.path().lexically_relative(corpus).replace_extension().generic_string());
    std::sort(ids.begin(), ids.end());
    if (ids.size() != 6604)
        fail("runtime XML corpus must contain exactly 6604 fixtures; found " +
             std::to_string(ids.size()) + "; restore corpus");
    return ids;
}
} // namespace
int main(int argc, char** argv) {
    try {
        // This CLI owns LC_NUMERIC for its lifetime: measurement callbacks are
        // synchronous and single-threaded. This is not a public-library policy.
        if (!std::setlocale(LC_NUMERIC, "C"))
            fail("cannot establish C numeric locale");
        fs::path corpus = XML_DEFAULT_CORPUS, output;
        std::string selection, injection;
        for (int i = 1; i < argc; ++i) {
            std::string_view option = argv[i];
            if (option == "--self-test") {
                self_test();
                return 0;
            }
            if (option == "--help") {
                std::cout
                    << "taffy_xml_runner [--fixture group/name] [--report path] [--corpus path]\n"
                       "                 [--inject geometry|missing-scroll|missing-grid|nan]\n";
                return 0;
            }
            if (i + 1 >= argc)
                fail("missing argument for " + std::string(option));
            std::string value = argv[++i];
            if (option == "--fixture")
                selection = value;
            else if (option == "--report")
                output = value;
            else if (option == "--corpus")
                corpus = value;
            else if (option == "--inject")
                injection = value;
            else
                fail("unknown option " + std::string(option));
        }
        if (!injection.empty() && injection != "geometry" && injection != "missing-scroll" &&
            injection != "missing-grid" && injection != "nan")
            fail("unknown injection " + injection);
        if (!injection.empty() && selection.empty())
            fail("--inject requires --fixture to avoid corrupting every corpus result");
        const auto fixture_ids = discover_fixtures(corpus);
        std::vector<std::string> ids;
        for (const auto& id : fixture_ids)
            if (selection.empty() || id == selection)
                ids.emplace_back(id);
        if (ids.empty())
            fail("fixture not registered: " + selection);
        std::vector<Result> results;
        for (const auto& id : ids) {
            Fixture fixture(injection);
            std::string diagnostic;
            try {
                fixture.run(corpus / (id + ".xml"), id);
            } catch (const std::exception& error) {
                diagnostic = error.what();
            }
            results.push_back({id, diagnostic, fixture.injected, fixture.debug_cache_checked});
            if (!diagnostic.empty())
                std::cerr << "FAIL " << id << ": " << diagnostic << '\n';
            else if (!selection.empty())
                std::cout << "PASS " << id << '\n';
        }
        const auto failed = std::count_if(results.begin(), results.end(),
                                          [](const auto& r) { return !r.diagnostic.empty(); });
        if (!output.empty())
            report(output, results, injection, fixture_ids.size());
        std::cout << "XML registered=" << fixture_ids.size() << " executed=" << results.size()
                  << " passed=" << results.size() - failed << " failed=" << failed << '\n';
        return failed ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "HARNESS ERROR: " << error.what() << '\n';
        return 2;
    }
}
