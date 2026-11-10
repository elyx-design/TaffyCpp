#include "taffy/parse.hpp"

#include <charconv>
#include <cmath>
#include <map>
#include <system_error>
#include <type_traits>

namespace taffy {
namespace {

[[noreturn]] void fail(std::size_t offset, std::string message,
                       ParseErrorCode code = ParseErrorCode::InvalidSyntax) {
    throw ParseError{code, offset, std::move(message)};
}

bool whitespace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool newline(unsigned char c) {
    return c == '\n' || c == '\r' || c == '\f';
}

bool digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

bool name_start(unsigned char c) {
    return c == 0 || c >= 128 || c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool name_char(unsigned char c) {
    return name_start(c) || digit(c) || c == '-';
}

std::string lower(std::string_view text) {
    std::string result(text);
    for (char& c : result)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return result;
}

void utf8(std::string& out, std::uint32_t c) {
    if (!c || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
        c = 0xfffd;
    if (c < 0x80)
        out += static_cast<char>(c);
    else if (c < 0x800) {
        out += static_cast<char>(0xc0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 63));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xe0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 63));
        out += static_cast<char>(0x80 | (c & 63));
    } else {
        out += static_cast<char>(0xf0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 63));
        out += static_cast<char>(0x80 | ((c >> 6) & 63));
        out += static_cast<char>(0x80 | (c & 63));
    }
}

void validate_utf8(std::string_view source) {
    for (std::size_t i = 0; i < source.size();) {
        const auto first = static_cast<unsigned char>(source[i]);
        if (first < 0x80) {
            ++i;
            continue;
        }
        const unsigned count = first >= 0xc2 && first <= 0xdf   ? 2
                               : first >= 0xe0 && first <= 0xef ? 3
                               : first >= 0xf0 && first <= 0xf4 ? 4
                                                                : 0;
        if (!count || source.size() - i < count)
            fail(i, "invalid UTF-8 in CSS input");
        std::uint32_t codepoint = first & (0x7f >> count);
        for (unsigned j = 1; j < count; ++j) {
            const auto c = static_cast<unsigned char>(source[i + j]);
            if ((c & 0xc0) != 0x80)
                fail(i + j, "invalid UTF-8 continuation byte");
            codepoint = (codepoint << 6) | (c & 63);
        }
        if ((count == 3 && codepoint < 0x800) || (count == 4 && codepoint < 0x10000) ||
            codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
            fail(i, "invalid UTF-8 code point");
        i += count;
    }
}

int hex(unsigned char c) {
    if (digit(c))
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
enum class Kind {
    End,
    Ident,
    String,
    Number,
    Percentage,
    Dimension,
    Function,
    Block,
    Delim
};

struct Token {
    Kind kind{Kind::End};
    std::size_t start{}, end{}, body_start{}, body_end{};
    std::string text;
    float value{};
    double double_value{};
    std::optional<std::int32_t> integer;
    char delim{};
};

// A component-value scanner. Block matching is iterative, so adversarial nesting
// does not recurse on the C++ stack. Only the supported value grammars recurse.
// Source positions always refer to the original input, including comments/escapes.
class Scanner {
    std::string_view source_;
    std::size_t pos_{}, end_{};

    unsigned char at(std::size_t p) const {
        return p < end_ ? static_cast<unsigned char>(source_[p]) : 0;
    }

    bool escape_start(std::size_t p) const {
        return p < end_ && at(p) == '\\' && (p + 1 == end_ || !newline(at(p + 1)));
    }

    bool ident_start(std::size_t p) const {
        if (p >= end_)
            return false;
        auto c = at(p);
        if (name_start(c) || escape_start(p))
            return true;
        return c == '-' && p + 1 < end_ &&
               (name_start(at(p + 1)) || at(p + 1) == '-' || escape_start(p + 1));
    }

    bool number_start(std::size_t p) const {
        if (p >= end_)
            return false;
        if (at(p) == '+' || at(p) == '-')
            ++p;
        return p < end_ && (digit(at(p)) || (at(p) == '.' && p + 1 < end_ && digit(at(p + 1))));
    }

    void consume_newline() {
        const auto c = at(pos_++);
        if (c == '\r' && pos_ < end_ && at(pos_) == '\n')
            ++pos_;
    }

    void escape(std::string& out) {
        ++pos_; // reverse solidus
        if (pos_ == end_) {
            utf8(out, 0xfffd);
            return;
        }
        if (hex(at(pos_)) >= 0) {
            std::uint32_t c = 0;
            for (int n = 0; n < 6 && pos_ < end_ && hex(at(pos_)) >= 0; ++n)
                c = c * 16 + static_cast<unsigned>(hex(at(pos_++)));
            if (pos_ < end_ && whitespace(at(pos_))) {
                if (newline(at(pos_)))
                    consume_newline();
                else
                    ++pos_;
            }
            utf8(out, c);
        } else if (!at(pos_)) {
            ++pos_;
            utf8(out, 0xfffd);
        } else
            out += source_[pos_++];
    }

    std::string name() {
        std::string result;
        while (pos_ < end_) {
            if (escape_start(pos_))
                escape(result);
            else if (name_char(at(pos_))) {
                if (!at(pos_)) {
                    ++pos_;
                    utf8(result, 0xfffd);
                } else
                    result += source_[pos_++];
            } else
                break;
        }
        return result;
    }

    Token lex() {
        skip();
        Token t;
        t.start = pos_;
        if (pos_ == end_) {
            t.end = pos_;
            return t;
        }
        auto c = at(pos_);
        if (number_start(pos_)) {
            bool negative = false;
            if (c == '+' || c == '-') {
                negative = c == '-';
                ++pos_;
            }
            double integral = 0, fractional = 0;
            while (pos_ < end_ && digit(at(pos_)))
                integral = integral * 10 + (at(pos_++) - '0');
            bool is_integer = true;
            if (pos_ + 1 < end_ && at(pos_) == '.' && digit(at(pos_ + 1))) {
                is_integer = false;
                ++pos_;
                double factor = .1;
                while (pos_ < end_ && digit(at(pos_))) {
                    fractional += (at(pos_++) - '0') * factor;
                    factor *= .1;
                }
            }
            double v = (negative ? -1.0 : 1.0) * (integral + fractional);
            if (pos_ + 1 < end_ && (at(pos_) == 'e' || at(pos_) == 'E') &&
                (digit(at(pos_ + 1)) ||
                 (pos_ + 2 < end_ && (at(pos_ + 1) == '+' || at(pos_ + 1) == '-') &&
                  digit(at(pos_ + 2))))) {
                is_integer = false;
                ++pos_;
                bool neg_exp = false;
                if (at(pos_) == '+' || at(pos_) == '-') {
                    neg_exp = at(pos_) == '-';
                    ++pos_;
                }
                double exponent = 0;
                while (pos_ < end_ && digit(at(pos_)))
                    exponent = exponent * 10 + (at(pos_++) - '0');
                v *= std::pow(10.0, neg_exp ? -exponent : exponent);
            }
            if (is_integer)
                t.integer = saturating_cast<std::int32_t>(v);
            t.double_value = v;
            t.value = static_cast<float>(v);
            if (pos_ < end_ && at(pos_) == '%') {
                ++pos_;
                t.kind = Kind::Percentage;
                t.value = static_cast<float>(v / 100.0);
                t.double_value = v / 100.0;
            } else if (ident_start(pos_)) {
                t.kind = Kind::Dimension;
                t.text = name();
            } else
                t.kind = Kind::Number;
        } else if (ident_start(pos_)) {
            t.text = name();
            t.kind = Kind::Ident;
            if (pos_ < end_ && at(pos_) == '(') {
                ++pos_;
                t.kind = Kind::Function;
                t.delim = '(';
            }
        } else if (c == '\'' || c == '"') {
            t.kind = Kind::String;
            ++pos_;
            while (pos_ < end_) {
                if (at(pos_) == c) {
                    ++pos_;
                    break;
                }
                if (newline(at(pos_)))
                    fail(pos_, "unescaped newline in CSS string");
                if (at(pos_) == '\\') {
                    if (pos_ + 1 == end_) {
                        ++pos_;
                        break;
                    }
                    if (newline(at(pos_ + 1))) {
                        ++pos_;
                        consume_newline();
                    } else
                        escape(t.text);
                } else if (!at(pos_)) {
                    ++pos_;
                    utf8(t.text, 0xfffd);
                } else
                    t.text += source_[pos_++];
            }
            // CSS EOF implicitly closes strings and blocks, as cssparser does.
        } else {
            ++pos_;
            t.delim = static_cast<char>(c);
            t.kind = Kind::Delim;
            if (c == '(' || c == '[' || c == '{')
                t.kind = Kind::Block;
        }
        t.end = pos_;
        return t;
    }

public:
    explicit Scanner(std::string_view source) : source_(source), end_(source.size()) {
        validate_utf8(source);
    }

    Scanner(std::string_view source, std::size_t start, std::size_t end)
        : source_(source), pos_(start), end_(end) {}

    void skip() {
        for (;;) {
            while (pos_ < end_ && whitespace(at(pos_)))
                ++pos_;
            if (pos_ + 1 >= end_ || at(pos_) != '/' || at(pos_ + 1) != '*')
                return;
            auto close = source_.find("*/", pos_ + 2);
            pos_ = close == std::string_view::npos || close + 2 > end_ ? end_ : close + 2;
        }
    }

    std::size_t offset() {
        skip();
        return pos_;
    }

    bool empty() {
        skip();
        return pos_ == end_;
    }

    Token next() {
        auto t = lex();
        if (t.kind == Kind::Block || t.kind == Kind::Function) {
            t.body_start = pos_;
            std::vector<char> closes;
            closes.push_back(t.delim == '(' ? ')' : t.delim == '[' ? ']' : '}');
            while (!closes.empty()) {
                auto inner = lex();
                if (inner.kind == Kind::End) {
                    t.body_end = end_;
                    break;
                }
                if (inner.kind == Kind::Block || inner.kind == Kind::Function)
                    closes.push_back(inner.delim == '(' ? ')' : inner.delim == '[' ? ']' : '}');
                else if (inner.kind == Kind::Delim && inner.delim == closes.back()) {
                    closes.pop_back();
                    if (closes.empty())
                        t.body_end = inner.start;
                }
            }
            t.end = pos_;
        }
        return t;
    }

    Token peek() {
        auto copy = *this;
        return copy.next();
    }

    Scanner body(const Token& t) const {
        return Scanner(source_, t.body_start, t.body_end);
    }

    std::string_view raw(const Token& t) const {
        return source_.substr(t.start, t.end - t.start);
    }

    void exhausted() {
        if (!empty())
            fail(offset(), "unexpected trailing CSS token");
    }

    void expect(char c) {
        auto t = next();
        if (t.kind != Kind::Delim || t.delim != c)
            fail(t.start, std::string("expected '") + c + "'");
    }

    std::string identifier() {
        auto t = next();
        if (t.kind != Kind::Ident)
            fail(t.start, "expected a CSS identifier");
        return t.text;
    }
};

template <class T, class F>
ParseResult<T> result(F&& fn) {
    try {
        return fn();
    } catch (const ParseError& e) {
        return std::unexpected(e);
    }
}
template <class T>
T parse(Scanner& s);

template <class T>
T entire(Scanner s) {
    auto value = parse<T>(s);
    s.exhausted();
    return value;
}

template <class T>
ParseResult<T> public_parse(std::string_view input) {
    return result<T>([&] {
        return entire<T>(Scanner(input));
    });
}

template <class T>
T keyword(Scanner& s, std::initializer_list<std::pair<std::string_view, T>> values) {
    auto t = s.next();
    if (t.kind != Kind::Ident)
        fail(t.start, "expected a CSS keyword");
    auto word = lower(t.text);
    for (const auto& [name, value] : values)
        if (word == name)
            return value;
    fail(t.start, "unrecognized CSS keyword: " + t.text, ParseErrorCode::InvalidValue);
}

template <class T>
T length(Scanner& s) {
    auto t = s.next();
    if (t.kind == Kind::Percentage)
        return T::percent(t.value);
    if (t.kind == Kind::Dimension && t.text == "px")
        return T::length(t.value);
    if constexpr (requires { T::fr(0); })
        if (t.kind == Kind::Dimension && t.text == "fr" && !std::signbit(t.value))
            return T::fr(t.value);
    if (t.kind == Kind::Ident) {
        if constexpr (requires { T::auto_(); })
            if (t.text == "auto")
                return T::auto_();
        if constexpr (requires { T::min_content(); }) {
            if (t.text == "min-content")
                return T::min_content();
            if (t.text == "max-content")
                return T::max_content();
        }
        if constexpr (requires { T::stretch(); }) {
            if (t.text == "stretch")
                return T::stretch();
            if (t.text == "content")
                return T::content();
            if (t.text == "fit-content")
                return T::fit_content();
        }
    }
    if constexpr (requires { T::fit_content(LengthPercentage{}); }) {
        if (t.kind == Kind::Function && t.text == "fit-content")
            return T::fit_content(entire<LengthPercentage>(s.body(t)));
    }
    fail(t.start, "expected a length in px, percentage, or valid sizing keyword",
         ParseErrorCode::InvalidValue);
}

template <class T>
T alignment(Scanner& s) {
    auto t = s.peek();
    if (t.kind != Kind::Ident)
        fail(t.start, "expected alignment keyword");
    const auto word = lower(t.text);
    AlignmentSafety safety = AlignmentSafety::Default;
    if (word == "safe" || word == "unsafe") {
        s.next();
        safety = word == "safe" ? AlignmentSafety::Safe : AlignmentSafety::Unsafe;
    }
    T value;
    if constexpr (std::same_as<T, AlignItems>) {
        value = keyword<T>(s, {{"normal", T::normal()},
                               {"start", T::start()},
                               {"end", T::end()},
                               {"flex-start", T::flex_start()},
                               {"flex-end", T::flex_end()},
                               {"self-start", T::self_start()},
                               {"self-end", T::self_end()},
                               {"center", T::center()},
                               {"baseline", T::baseline()},
                               {"stretch", T::stretch()}});
        if (safety != AlignmentSafety::Default && (value.keyword == AlignItemsKeyword::Normal ||
                                                   value.keyword == AlignItemsKeyword::Baseline ||
                                                   value.keyword == AlignItemsKeyword::Stretch))
            fail(t.start, "safety modifier requires positional alignment",
                 ParseErrorCode::InvalidValue);
    } else {
        value = keyword<T>(s, {{"normal", T::normal()},
                               {"start", T::start()},
                               {"end", T::end()},
                               {"flex-start", T::flex_start()},
                               {"flex-end", T::flex_end()},
                               {"center", T::center()},
                               {"stretch", T::stretch()},
                               {"space-between", T::space_between()},
                               {"space-around", T::space_around()},
                               {"space-evenly", T::space_evenly()}});
        if (safety != AlignmentSafety::Default &&
            (value.keyword == AlignContentKeyword::Normal ||
             value.keyword == AlignContentKeyword::Stretch ||
             value.keyword == AlignContentKeyword::SpaceBetween ||
             value.keyword == AlignContentKeyword::SpaceAround ||
             value.keyword == AlignContentKeyword::SpaceEvenly))
            fail(t.start, "safety modifier requires positional alignment",
                 ParseErrorCode::InvalidValue);
    }
    return value.with_safety(safety);
}

template <>
LengthPercentage parse(Scanner& s) {
    return length<LengthPercentage>(s);
}

template <>
LengthPercentageAuto parse(Scanner& s) {
    return length<LengthPercentageAuto>(s);
}

template <>
Dimension parse(Scanner& s) {
    return length<Dimension>(s);
}

template <>
MinTrackSizingFunction parse(Scanner& s) {
    return length<MinTrackSizingFunction>(s);
}

template <>
MaxTrackSizingFunction parse(Scanner& s) {
    return length<MaxTrackSizingFunction>(s);
}

template <>
AlignItems parse(Scanner& s) {
    return alignment<AlignItems>(s);
}

template <>
AlignContent parse(Scanner& s) {
    return alignment<AlignContent>(s);
}

template <>
Display parse(Scanner& s) {
    return keyword<Display>(s, {{"flex", Display::Flex},
                                {"grid", Display::Grid},
                                {"block", Display::Block},
                                {"flow-root", Display::FlowRoot},
                                {"none", Display::None}});
}

template <>
Position parse(Scanner& s) {
    return keyword<Position>(s, {{"static", Position::Static},
                                 {"relative", Position::Relative},
                                 {"absolute", Position::Absolute},
                                 {"fixed", Position::Fixed},
                                 {"sticky", Position::Sticky}});
}

template <>
BoxSizing parse(Scanner& s) {
    return keyword<BoxSizing>(
        s, {{"border-box", BoxSizing::BorderBox}, {"content-box", BoxSizing::ContentBox}});
}

template <>
Direction parse(Scanner& s) {
    return keyword<Direction>(s, {{"ltr", Direction::Ltr}, {"rtl", Direction::Rtl}});
}

template <>
Overflow parse(Scanner& s) {
    return keyword<Overflow>(s, {{"visible", Overflow::Visible},
                                 {"clip", Overflow::Clip},
                                 {"hidden", Overflow::Hidden},
                                 {"scroll", Overflow::Scroll}});
}

template <>
TextAlign parse(Scanner& s) {
    return keyword<TextAlign>(s, {{"auto", TextAlign::Auto},
                                  {"-webkit-left", TextAlign::LegacyLeft},
                                  {"-webkit-right", TextAlign::LegacyRight},
                                  {"-webkit-center", TextAlign::LegacyCenter}});
}

template <>
Float parse(Scanner& s) {
    return keyword<Float>(s,
                          {{"none", Float::None}, {"left", Float::Left}, {"right", Float::Right}});
}

template <>
Clear parse(Scanner& s) {
    return keyword<Clear>(s, {{"none", Clear::None},
                              {"left", Clear::Left},
                              {"right", Clear::Right},
                              {"both", Clear::Both}});
}

template <>
FlexDirection parse(Scanner& s) {
    return keyword<FlexDirection>(s, {{"row", FlexDirection::Row},
                                      {"column", FlexDirection::Column},
                                      {"row-reverse", FlexDirection::RowReverse},
                                      {"column-reverse", FlexDirection::ColumnReverse}});
}

template <>
FlexWrap parse(Scanner& s) {
    bool balance = false;
    std::optional<bool> reverse;
    bool first = true;
    do {
        const auto offset = s.offset();
        auto word = lower(s.identifier());
        if (first && word == "nowrap")
            return FlexWrap::NoWrap;
        first = false;
        if ((word == "wrap" || word == "wrap-reverse") && !reverse)
            reverse = word == "wrap-reverse";
        else if (word == "balance" && !balance)
            balance = true;
        else
            fail(offset, "invalid or duplicate flex-wrap keyword", ParseErrorCode::InvalidValue);
    } while (!s.empty());
    return balance ? (reverse.value_or(false) ? FlexWrap::BalanceReverse : FlexWrap::Balance)
                   : (reverse.value_or(false) ? FlexWrap::WrapReverse : FlexWrap::Wrap);
}

template <>
GridAutoFlow parse(Scanner& s) {
    std::optional<bool> column;
    bool dense = false;
    for (unsigned i = 0; i < 2 && !s.empty(); ++i) {
        const auto t = s.next();
        if (t.kind != Kind::Ident)
            fail(t.start, "expected grid-auto-flow keyword");
        if (t.text == "row" || t.text == "column")
            column = t.text == "column";
        else if (t.text == "dense")
            dense = true;
        else
            fail(t.start, "unrecognized grid-auto-flow keyword", ParseErrorCode::InvalidValue);
    }
    if (!column && !dense)
        fail(s.offset(), "expected grid-auto-flow keyword");
    return column.value_or(false) ? (dense ? GridAutoFlow::ColumnDense : GridAutoFlow::Column)
                                  : (dense ? GridAutoFlow::RowDense : GridAutoFlow::Row);
}

template <>
Contain parse(Scanner& s) {
    unsigned seen = 0;
    Contain value;
    do {
        const auto t = s.next();
        if (t.kind != Kind::Ident)
            fail(t.start, "expected containment keyword");
        const auto word = lower(t.text);
        if ((word == "none" || word == "content") && !seen && s.empty())
            return word == "none" ? Contain::none() : Contain::content();
        unsigned bit = word == "layout" ? 1 : word == "paint" ? 2 : word == "style" ? 4 : 0;
        if (!bit || (seen & bit))
            fail(t.start, "invalid or duplicate containment keyword", ParseErrorCode::InvalidValue);
        seen |= bit;
        if (bit == 1)
            value |= Contain::layout();
        if (bit == 2)
            value |= Contain::paint();
    } while (!s.empty());
    return value;
}

template <>
AvailableSpace parse(Scanner& s) {
    const auto t = s.next();
    if ((t.kind == Kind::Number || t.kind == Kind::Dimension) && t.value >= 0)
        return AvailableSpace::definite(t.value);
    if (t.kind == Kind::Ident && t.text == "min-content")
        return AvailableSpace::min_content();
    if (t.kind == Kind::Ident && t.text == "max-content")
        return AvailableSpace::max_content();
    fail(t.start, "expected nonnegative available space or intrinsic keyword",
         ParseErrorCode::InvalidValue);
}

template <>
TrackSizingFunction parse(Scanner& s) {
    auto t = s.peek();
    if (t.kind == Kind::Function && lower(t.text) == "minmax") {
        s.next();
        auto body = s.body(t);
        auto min = parse<MinTrackSizingFunction>(body);
        body.expect(',');
        auto max = parse<MaxTrackSizingFunction>(body);
        body.exhausted();
        return {min, max};
    }
    return TrackSizingFunction(parse<MaxTrackSizingFunction>(s));
}

template <>
RepetitionCount parse(Scanner& s) {
    const auto t = s.next();
    if (t.kind == Kind::Number && t.integer && *t.integer > 0)
        return RepetitionCount::count(static_cast<std::uint16_t>(std::min(*t.integer, 65535)));
    if (t.kind == Kind::Ident && t.text == "auto-fit")
        return RepetitionCount::auto_fit();
    if (t.kind == Kind::Ident && t.text == "auto-fill")
        return RepetitionCount::auto_fill();
    fail(t.start, "expected positive integer repetition count, auto-fit, or auto-fill",
         ParseErrorCode::InvalidValue);
}

template <>
GridPlacement parse(Scanner& s) {
    bool span = false;
    std::optional<std::int32_t> index;
    std::optional<std::string> name;
    const auto start = s.offset();
    while (!s.empty()) {
        const auto t = s.peek();
        if (t.kind == Kind::Delim && t.delim == '/')
            break;
        s.next();
        if (t.kind == Kind::Ident) {
            if (t.text == "auto") {
                if (span || index || name)
                    fail(t.start, "auto must stand alone");
                // Delimiters are boundaries only in placement shorthands.
                if (!s.empty() && !(s.peek().kind == Kind::Delim && s.peek().delim == '/'))
                    fail(s.offset(), "auto must stand alone");
                return GridPlacement::auto_();
            }
            if (t.text == "span") {
                if (span)
                    fail(t.start, "duplicate span");
                span = true;
            } else {
                if (name)
                    fail(t.start, "duplicate grid identifier");
                name = t.text;
            }
        } else if (t.kind == Kind::Number && t.integer && *t.integer != 0) {
            if (index)
                fail(t.start, "duplicate grid index");
            index = *t.integer;
        } else
            fail(t.start, "expected nonzero integer or grid identifier");
    }
    if (!span && !index && !name)
        fail(start, "expected grid placement");
    if (span) {
        auto count = static_cast<std::uint16_t>(std::clamp(index.value_or(0), 0, 65535));
        return name ? GridPlacement::named_span(std::move(*name), count)
                    : GridPlacement::span(count);
    }
    auto line = static_cast<std::int16_t>(std::clamp(index.value_or(0), -32768, 32767));
    return name ? GridPlacement::named_line(std::move(*name), line) : GridPlacement::line(line);
}

std::vector<std::string> line_names(Scanner& s) {
    auto t = s.next();
    if (t.kind != Kind::Block || t.delim != '[')
        fail(t.start, "expected bracketed grid line names");
    auto body = s.body(t);
    std::vector<std::string> names;
    while (!body.empty())
        names.push_back(body.identifier());
    return names;
}

template <class Track>
GridTemplateTracks<Track> tracks(Scanner& s) {
    GridTemplateTracks<Track> value;
    auto names = [&] {
        auto t = s.peek();
        return t.kind == Kind::Block && t.delim == '[' ? line_names(s) : std::vector<std::string>{};
    };
    value.line_names.push_back(names());
    while (!s.empty()) {
        value.tracks.push_back(parse<Track>(s));
        value.line_names.push_back(names());
    }
    if (value.tracks.empty())
        fail(s.offset(), "expected at least one grid track");
    return value;
}

template <>
GridTemplateTracks<TrackSizingFunction> parse(Scanner& s) {
    return tracks<TrackSizingFunction>(s);
}

template <>
GridTemplateComponent parse(Scanner& s) {
    const auto t = s.peek();
    if (t.kind == Kind::Function && lower(t.text) == "repeat") {
        s.next();
        auto body = s.body(t);
        auto count = parse<RepetitionCount>(body);
        body.expect(',');
        auto list = tracks<TrackSizingFunction>(body);
        return GridTemplateRepetition{count, std::move(list.tracks), std::move(list.line_names)};
    }
    return GridTemplateComponent(parse<TrackSizingFunction>(s));
}

template <>
GridTemplateTracks<> parse(Scanner& s) {
    return tracks<GridTemplateComponent>(s);
}

template <>
GridTemplateRepetition parse(Scanner& s) {
    auto start = s.offset();
    auto c = parse<GridTemplateComponent>(s);
    if (auto p = std::get_if<GridTemplateRepetition>(&c.value))
        return std::move(*p);
    fail(start, "expected repeat()");
}

template <>
GridAutoTracks parse(Scanner& s) {
    GridAutoTracks value;
    do {
        value.push_back(parse<TrackSizingFunction>(s));
    } while (!s.empty());
    return value;
}

template <>
TemplateLineNames parse(Scanner& s) {
    TemplateLineNames value;
    do {
        value.push_back(line_names(s));
    } while (!s.empty());
    return value;
}

template <>
GridLine parse(Scanner& s) {
    auto t = s.next();
    if (t.kind != Kind::Number || !t.integer || !*t.integer)
        fail(t.start, "expected nonzero integer grid line");
    return GridLine(static_cast<std::int16_t>(std::clamp(*t.integer, -32768, 32767)));
}

template <>
Line<GridPlacement> parse(Scanner& s) {
    auto start = parse<GridPlacement>(s);
    GridPlacement end = start.kind() == GridPlacement::Kind::NamedLine && start.line_index() == 0
                            ? start
                            : GridPlacement::auto_();
    if (!s.empty()) {
        s.expect('/');
        end = parse<GridPlacement>(s);
    }
    return {std::move(start), std::move(end)};
}

template <>
GridTemplateAreas parse(Scanner& s) {
    auto first = s.peek();
    if (first.kind == Kind::Ident && lower(first.text) == "none") {
        s.next();
        return {};
    }
    GridTemplateAreas value;
    std::vector<std::vector<std::string>> cells;
    std::map<std::string, std::size_t> indices;
    do {
        const auto t = s.next();
        if (t.kind != Kind::String)
            fail(t.start, "expected quoted grid area row");
        std::vector<std::string> row;
        std::size_t p = 0;
        while (p < t.text.size()) {
            auto c = static_cast<unsigned char>(t.text[p]);
            if (whitespace(c)) {
                ++p;
                continue;
            }
            const auto start = p;
            if (c == '.') {
                while (p < t.text.size() && t.text[p] == '.')
                    ++p;
                row.emplace_back();
            } else if (name_char(c)) {
                while (p < t.text.size() && name_char(static_cast<unsigned char>(t.text[p])))
                    ++p;
                row.emplace_back(t.text.substr(start, p - start));
            } else
                fail(t.start, "invalid grid area cell", ParseErrorCode::InvalidValue);
        }
        if (row.empty() || row.size() >= 65535 || cells.size() >= 65534)
            fail(t.start, "grid area dimensions are empty or unrepresentable",
                 ParseErrorCode::InvalidValue);
        if (cells.empty())
            value.column_count = static_cast<std::uint16_t>(row.size());
        if (row.size() != value.column_count)
            fail(t.start, "grid area rows have different widths", ParseErrorCode::InvalidValue);
        for (std::size_t col = 0; col < row.size(); ++col) {
            if (row[col].empty())
                continue;
            auto [it, inserted] = indices.try_emplace(row[col], value.areas.size());
            if (inserted)
                value.areas.push_back({row[col], static_cast<std::uint16_t>(cells.size() + 1),
                                       static_cast<std::uint16_t>(cells.size() + 2),
                                       static_cast<std::uint16_t>(col + 1),
                                       static_cast<std::uint16_t>(col + 2)});
            else {
                auto& area = value.areas[it->second];
                area.row_end = static_cast<std::uint16_t>(cells.size() + 2);
                area.column_start =
                    std::min(area.column_start, static_cast<std::uint16_t>(col + 1));
                area.column_end = std::max(area.column_end, static_cast<std::uint16_t>(col + 2));
            }
        }
        cells.push_back(std::move(row));
    } while (!s.empty());
    value.row_count = static_cast<std::uint16_t>(cells.size());
    for (const auto& area : value.areas)
        for (std::size_t row = area.row_start - 1; row < static_cast<std::size_t>(area.row_end - 1);
             ++row)
            for (std::size_t col = area.column_start - 1;
                 col < static_cast<std::size_t>(area.column_end - 1); ++col)
                if (cells[row][col] != area.name)
                    fail(first.start, "named grid area is not rectangular",
                         ParseErrorCode::InvalidValue);
    return value;
}

template <>
float parse(Scanner& s) {
    auto t = s.next();
    if (t.kind != Kind::Number)
        fail(t.start, "expected CSS number");
    return t.value;
}

template <>
double parse(Scanner& s) {
    auto t = s.next();
    if (t.kind != Kind::Number)
        fail(t.start, "expected CSS number");
    return t.double_value;
}

template <>
bool parse(Scanner& s) {
    return keyword<bool>(s, {{"true", true}, {"false", false}});
}

template <>
std::string parse(Scanner& s) {
    auto t = s.next();
    if (t.kind != Kind::Ident && t.kind != Kind::String)
        fail(t.start, "expected identifier or string");
    return t.text;
}

template <std::integral T>
T integral(Scanner& s) {
    auto t = s.next();
    if (t.kind != Kind::Number || !t.integer)
        fail(t.start, "expected integer");
    // Unlike the upstream grid domains, native integer parsing is range checked.
    auto raw = s.raw(t);
    if (!raw.empty() && raw.front() == '+')
        raw.remove_prefix(1);
    T value{};
    auto [end, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), value);
    if (ec != std::errc{} || end != raw.data() + raw.size())
        fail(t.start, "integer out of range", ParseErrorCode::InvalidValue);
    return value;
}

#define TAFFY_INTEGRAL(Type)                                                                       \
    template <>                                                                                    \
    Type parse(Scanner& s) {                                                                       \
        return integral<Type>(s);                                                                  \
    }
TAFFY_INTEGRAL(std::int16_t)
TAFFY_INTEGRAL(std::uint16_t)
TAFFY_INTEGRAL(std::int32_t)
TAFFY_INTEGRAL(std::uint32_t)
TAFFY_INTEGRAL(std::int64_t)
TAFFY_INTEGRAL(std::uint64_t)
#undef TAFFY_INTEGRAL

} // namespace

namespace detail {
ParseResult<std::string_view> css_integer_token(std::string_view input) {
    return result<std::string_view>([&] {
        Scanner s(input);
        auto t = s.next();
        s.exhausted();
        if (t.kind != Kind::Number || !t.integer)
            fail(t.start, "expected integer");
        return s.raw(t);
    });
}

bool css_auto(std::string_view input) {
    auto parsed = result<bool>([&] {
        Scanner s(input);
        auto t = s.next();
        s.exhausted();
        return t.kind == Kind::Ident && lower(t.text) == "auto";
    });
    return parsed && *parsed;
}

ParseResult<std::vector<std::string_view>> css_function_arguments(std::string_view input,
                                                                  std::string_view name) {
    return result<std::vector<std::string_view>>([&] {
        Scanner s(input);
        auto t = s.next();
        s.exhausted();
        if (t.kind != Kind::Function || lower(t.text) != name)
            fail(t.start, "expected function: " + std::string(name));
        auto body = s.body(t);
        std::vector<std::string_view> args;
        auto start = body.offset();
        auto end = start;
        while (!body.empty()) {
            auto token = body.next();
            if (token.kind == Kind::Delim && token.delim == ',') {
                args.push_back(input.substr(start, end - start));
                start = body.offset();
                end = start;
            } else
                end = token.end;
        }
        args.push_back(input.substr(start, end - start));
        return args;
    });
}

ParseResult<std::vector<std::string_view>> css_components(std::string_view input) {
    return result<std::vector<std::string_view>>([&] {
        Scanner s(input);
        std::vector<std::string_view> parts;
        while (!s.empty()) {
            auto t = s.next();
            parts.push_back(s.raw(t));
        }
        return parts;
    });
}
} // namespace detail

#define TAFFY_PUBLIC(Type)                                                                         \
    template <>                                                                                    \
    ParseResult<Type> parse_css<Type>(std::string_view input) {                                    \
        return public_parse<Type>(input);                                                          \
    }
TAFFY_PUBLIC(float)
TAFFY_PUBLIC(double)
TAFFY_PUBLIC(bool)
TAFFY_PUBLIC(std::int16_t)
TAFFY_PUBLIC(std::uint16_t)
TAFFY_PUBLIC(std::int32_t)
TAFFY_PUBLIC(std::uint32_t)
TAFFY_PUBLIC(std::int64_t)
TAFFY_PUBLIC(std::uint64_t)
TAFFY_PUBLIC(std::string)
TAFFY_PUBLIC(LengthPercentage)
TAFFY_PUBLIC(LengthPercentageAuto)
TAFFY_PUBLIC(Dimension)
TAFFY_PUBLIC(MinTrackSizingFunction)
TAFFY_PUBLIC(MaxTrackSizingFunction)
TAFFY_PUBLIC(AvailableSpace)
TAFFY_PUBLIC(Display)
TAFFY_PUBLIC(Position)
TAFFY_PUBLIC(BoxSizing)
TAFFY_PUBLIC(Direction)
TAFFY_PUBLIC(Overflow)
TAFFY_PUBLIC(Contain)
TAFFY_PUBLIC(AlignItems)
TAFFY_PUBLIC(AlignContent)
TAFFY_PUBLIC(TextAlign)
TAFFY_PUBLIC(Float)
TAFFY_PUBLIC(Clear)
TAFFY_PUBLIC(FlexDirection)
TAFFY_PUBLIC(FlexWrap)
TAFFY_PUBLIC(GridAutoFlow)
TAFFY_PUBLIC(TrackSizingFunction)
TAFFY_PUBLIC(RepetitionCount)
TAFFY_PUBLIC(GridPlacement)
TAFFY_PUBLIC(GridLine)
TAFFY_PUBLIC(GridTemplateRepetition)
TAFFY_PUBLIC(GridTemplateComponent)
TAFFY_PUBLIC(GridTemplateTracks<>)
TAFFY_PUBLIC(GridTemplateTracks<TrackSizingFunction>)
TAFFY_PUBLIC(GridAutoTracks)
TAFFY_PUBLIC(TemplateLineNames)
TAFFY_PUBLIC(GridTemplateAreas)
TAFFY_PUBLIC(Line<GridPlacement>)
#undef TAFFY_PUBLIC

#define TAFFY_NAMED_CSS_PARSER(Name, Type)                                                         \
    ParseResult<Type> parse_##Name(std::string_view input) {                                       \
        return parse_css<Type>(input);                                                             \
    }
TAFFY_NAMED_CSS_PARSER(number, float)
TAFFY_NAMED_CSS_PARSER(boolean, bool)
TAFFY_NAMED_CSS_PARSER(dimension, Dimension)
TAFFY_NAMED_CSS_PARSER(length_percentage, LengthPercentage)
TAFFY_NAMED_CSS_PARSER(length_percentage_auto, LengthPercentageAuto)
TAFFY_NAMED_CSS_PARSER(available_space, AvailableSpace)
TAFFY_NAMED_CSS_PARSER(display, Display)
TAFFY_NAMED_CSS_PARSER(position, Position)
TAFFY_NAMED_CSS_PARSER(box_sizing, BoxSizing)
TAFFY_NAMED_CSS_PARSER(direction, Direction)
TAFFY_NAMED_CSS_PARSER(overflow, Overflow)
TAFFY_NAMED_CSS_PARSER(contain, Contain)
TAFFY_NAMED_CSS_PARSER(align_items, AlignItems)
TAFFY_NAMED_CSS_PARSER(align_self, AlignSelf)
TAFFY_NAMED_CSS_PARSER(justify_items, JustifyItems)
TAFFY_NAMED_CSS_PARSER(justify_self, JustifySelf)
TAFFY_NAMED_CSS_PARSER(align_content, AlignContent)
TAFFY_NAMED_CSS_PARSER(justify_content, JustifyContent)
TAFFY_NAMED_CSS_PARSER(text_align, TextAlign)
TAFFY_NAMED_CSS_PARSER(float, Float)
TAFFY_NAMED_CSS_PARSER(clear, Clear)
TAFFY_NAMED_CSS_PARSER(flex_direction, FlexDirection)
TAFFY_NAMED_CSS_PARSER(flex_wrap, FlexWrap)
TAFFY_NAMED_CSS_PARSER(grid_auto_flow, GridAutoFlow)
TAFFY_NAMED_CSS_PARSER(min_track_sizing_function, MinTrackSizingFunction)
TAFFY_NAMED_CSS_PARSER(max_track_sizing_function, MaxTrackSizingFunction)
TAFFY_NAMED_CSS_PARSER(track_sizing_function, TrackSizingFunction)
TAFFY_NAMED_CSS_PARSER(repetition_count, RepetitionCount)
TAFFY_NAMED_CSS_PARSER(grid_placement, GridPlacement)
TAFFY_NAMED_CSS_PARSER(grid_line, GridLine)
TAFFY_NAMED_CSS_PARSER(grid_template_repetition, GridTemplateRepetition)
TAFFY_NAMED_CSS_PARSER(grid_template_component, GridTemplateComponent)
TAFFY_NAMED_CSS_PARSER(grid_template_tracks, GridTemplateTracks<>)
TAFFY_NAMED_CSS_PARSER(grid_auto_tracks, GridAutoTracks)
TAFFY_NAMED_CSS_PARSER(grid_template_areas, GridTemplateAreas)
#undef TAFFY_NAMED_CSS_PARSER

namespace {
// XML's native build_style uses Rust's scalar FromStr for these fields, not
// FromCss. Preserve its nonfinite spellings instead of turning them into zero.
float native_number(std::string_view value) {
    // Strip only surrounding CSS trivia. Keep the exact source between the
    // first and last tokens: neither comments nor whitespace may join interior
    // tokens, and Rust FromStr accepts spellings (e.g. 1.) spanning CSS tokens.
    Scanner s(value);
    const auto start = s.offset();
    auto end = start;
    while (!s.empty())
        end = s.next().end;
    auto raw = value.substr(start, end - start);
    const auto word = lower(raw);
    if (word == "nan" || word == "+nan" || word == "-nan")
        return std::copysign(std::numeric_limits<float>::quiet_NaN(),
                             word.front() == '-' ? -1.0f : 1.0f);
    if (word == "inf" || word == "infinity" || word == "+inf" || word == "+infinity")
        return std::numeric_limits<float>::infinity();
    if (word == "-inf" || word == "-infinity")
        return -std::numeric_limits<float>::infinity();
    // Scalar FromStr rounds directly to f32, never via the CSS f64 intermediate.
    if (!raw.empty() && raw.front() == '+') {
        raw.remove_prefix(1);
        if (!raw.empty() && (raw.front() == '-' || raw.front() == '+'))
            fail(start, "expected native scalar number");
    }
    auto numeric = raw;
    if (!numeric.empty() && numeric.front() == '-')
        numeric.remove_prefix(1);
    if (!numeric.empty() &&
        (digit(static_cast<unsigned char>(numeric.front())) || numeric.front() == '.')) {
        float scalar{};
        auto [end, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), scalar);
        if (end == raw.data() + raw.size()) {
            if (ec == std::errc{})
                return scalar;
            if (ec == std::errc::result_out_of_range) {
                // from_chars reports overflow/underflow whereas Rust FromStr
                // returns signed infinity/zero. Determine which without a locale
                // dependency or an intermediate that could manufacture NaN.
                const auto exp_at = numeric.find_first_of("eE");
                const auto mantissa = numeric.substr(0, exp_at);
                std::int64_t digits = 0, before_dot = 0, first_nonzero = -1;
                bool dot = false;
                for (char c : mantissa) {
                    if (c == '.') {
                        dot = true;
                        continue;
                    }
                    if (first_nonzero < 0 && c != '0')
                        first_nonzero = digits;
                    ++digits;
                    if (!dot)
                        ++before_dot;
                }
                std::int64_t exponent = 0;
                if (exp_at != std::string_view::npos) {
                    auto e = numeric.substr(exp_at + 1);
                    bool negative = false;
                    if (!e.empty() && (e.front() == '+' || e.front() == '-')) {
                        negative = e.front() == '-';
                        e.remove_prefix(1);
                    }
                    const auto cap =
                        static_cast<std::int64_t>(std::min<std::size_t>(
                            numeric.size(), static_cast<std::size_t>(
                                                std::numeric_limits<std::int64_t>::max() / 100))) +
                        1000;
                    for (char c : e)
                        exponent = std::min(cap, exponent * 10 + c - '0');
                    if (negative)
                        exponent = -exponent;
                }
                exponent += before_dot - first_nonzero - 1;
                const float magnitude = first_nonzero < 0 || exponent < 0
                                            ? 0.0f
                                            : std::numeric_limits<float>::infinity();
                return std::copysign(magnitude, raw.front() == '-' ? -1.0f : 1.0f);
            }
        }
    }
    auto parsed = parse_css<float>(value);
    if (!parsed)
        throw parsed.error();
    fail(start, "expected native scalar number");
}

template <class T>
void assign(T& target, std::string_view value) {
    auto parsed = parse_css<T>(value);
    if (!parsed)
        throw parsed.error();
    // Parsing may allocate or fail; committing the completed value must not.
    static_assert(std::is_nothrow_move_assignable_v<T>);
    target = std::move(*parsed);
}

struct PropertyNameError {
    ParseError value;
};

// Stage each property (including every shorthand field) before committing it.
// This preserves transactional updates without cloning unrelated style data.
void property(Style& s, std::string_view name, std::string_view value) {
    Scanner key(name);
    auto t = key.next();
    key.exhausted();
    if (t.kind != Kind::Ident)
        fail(t.start, "expected property name");
    const auto p = lower(t.text);
#define FIELD(Name, Field)                                                                         \
    do {                                                                                           \
        if (p == Name) {                                                                           \
            assign(s.Field, value);                                                                \
            return;                                                                                \
        }                                                                                          \
    } while (false)
    FIELD("display", display);
    FIELD("direction", direction);
    FIELD("box-sizing", box_sizing);
    FIELD("item-is-table", item_is_table);
    FIELD("item-is-replaced", item_is_replaced);
    FIELD("item-is-compressible-replaced", item_is_compressible_replaced);
    FIELD("overflow-x", overflow.x);
    FIELD("overflow-y", overflow.y);
    FIELD("contain", contain);
    FIELD("float", float_);
    FIELD("clear", clear);
    FIELD("position", position);
    FIELD("width", size.width);
    FIELD("height", size.height);
    FIELD("min-width", min_size.width);
    FIELD("min-height", min_size.height);
    FIELD("max-width", max_size.width);
    FIELD("max-height", max_size.height);
    FIELD("left", inset.left);
    FIELD("right", inset.right);
    FIELD("top", inset.top);
    FIELD("bottom", inset.bottom);
    FIELD("inset", inset);
    FIELD("margin", margin);
    FIELD("padding", padding);
    FIELD("border", border);
    FIELD("border-width", border);
    FIELD("margin-left", margin.left);
    FIELD("margin-right", margin.right);
    FIELD("margin-top", margin.top);
    FIELD("margin-bottom", margin.bottom);
    FIELD("padding-left", padding.left);
    FIELD("padding-right", padding.right);
    FIELD("padding-top", padding.top);
    FIELD("padding-bottom", padding.bottom);
    FIELD("border-left", border.left);
    FIELD("border-right", border.right);
    FIELD("border-top", border.top);
    FIELD("border-bottom", border.bottom);
    FIELD("border-left-width", border.left);
    FIELD("border-right-width", border.right);
    FIELD("border-top-width", border.top);
    FIELD("border-bottom-width", border.bottom);
    FIELD("column-gap", gap.width);
    FIELD("row-gap", gap.height);
    FIELD("align-items", align_items);
    FIELD("align-self", align_self);
    FIELD("justify-items", justify_items);
    FIELD("justify-self", justify_self);
    FIELD("align-content", align_content);
    FIELD("justify-content", justify_content);
    FIELD("text-align", text_align);
    FIELD("flex-direction", flex_direction);
    FIELD("flex-wrap", flex_wrap);
    FIELD("flex-line-count", flex_line_count);
    FIELD("flex-basis", flex_basis);
    FIELD("grid-auto-flow", grid_auto_flow);
    FIELD("grid-auto-rows", grid_auto_rows);
    FIELD("grid-auto-columns", grid_auto_columns);
    FIELD("grid-template-row-names", grid_template_row_names);
    FIELD("grid-template-column-names", grid_template_column_names);
    FIELD("grid-row-start", grid_row.start);
    FIELD("grid-row-end", grid_row.end);
    FIELD("grid-column-start", grid_column.start);
    FIELD("grid-column-end", grid_column.end);
    FIELD("grid-row", grid_row);
    FIELD("grid-column", grid_column);
#undef FIELD
    if (p == "scrollbar-width") {
        s.scrollbar_width = native_number(value);
        return;
    }
    if (p == "flex-grow") {
        s.flex_grow = native_number(value);
        return;
    }
    if (p == "flex-shrink") {
        s.flex_shrink = native_number(value);
        return;
    }
    if (p == "grid-template-rows" || p == "grid-template-columns") {
        Scanner input(value);
        auto token = input.peek();
        GridTemplateTracks<> list;
        if (token.kind == Kind::Ident && lower(token.text) == "none") {
            input.next();
            input.exhausted();
        } else
            list = entire<GridTemplateTracks<>>(input);
        if (p == "grid-template-rows") {
            s.grid_template_rows = std::move(list.tracks);
            s.grid_template_row_names = std::move(list.line_names);
        } else {
            s.grid_template_columns = std::move(list.tracks);
            s.grid_template_column_names = std::move(list.line_names);
        }
        return;
    }
    if (p == "grid-template-areas") {
        Scanner input(value);
        auto token = input.peek();
        auto areas = entire<GridTemplateAreas>(input);
        if (token.kind == Kind::Ident && lower(token.text) == "none")
            s.grid_template_areas.reset();
        else
            s.grid_template_areas = std::move(areas);
        return;
    }
    if (p == "overflow") {
        assign(s.overflow, value);
        return;
    }
    if (p == "gap") {
        auto gap = parse_css<Size<LengthPercentage>>(value);
        if (!gap)
            throw gap.error();
        s.gap = {gap->height, gap->width};
        return; // CSS row, column order
    }
    if (p == "aspect-ratio") {
        Scanner input(value);
        auto token = input.peek();
        if (token.kind == Kind::Ident && lower(token.text) == "auto") {
            input.next();
            input.exhausted();
            s.aspect_ratio.reset();
            return;
        }
        // The scalar spelling is also the native XML conversion, including
        // negative and nonfinite stored values. A CSS slash ratio is an extension.
        auto components = detail::css_components(value);
        bool slash = false;
        if (components)
            for (auto c : *components)
                if (c == "/")
                    slash = true;
        if (!slash) {
            s.aspect_ratio = native_number(value);
            return;
        }
        float numerator = parse<float>(input), denominator = 1;
        if (!input.empty()) {
            input.expect('/');
            denominator = parse<float>(input);
        }
        input.exhausted();
        if (!(numerator >= 0) || !(denominator > 0))
            fail(token.start, "invalid aspect ratio", ParseErrorCode::InvalidValue);
        const float ratio = numerator / denominator;
        if (!std::isfinite(ratio))
            fail(token.start, "aspect ratio cannot be represented", ParseErrorCode::InvalidValue);
        s.aspect_ratio = ratio;
        return;
    }
    if (p == "flex-flow") {
        Scanner input(value);
        bool direction = false, wrap = false;
        auto parsed_direction = FlexDirection::Row;
        auto parsed_wrap = FlexWrap::NoWrap;
        while (!input.empty()) {
            const auto token = input.peek();
            const auto word = lower(token.text);
            if (token.kind == Kind::Ident && (word == "row" || word == "row-reverse" ||
                                              word == "column" || word == "column-reverse")) {
                if (direction)
                    fail(token.start, "duplicate flex direction");
                parsed_direction = parse<FlexDirection>(input);
                direction = true;
            } else {
                if (wrap)
                    fail(token.start, "duplicate flex wrapping");
                // Wrap may have a two-keyword balance modifier, but not consume direction.
                auto end = token.start;
                while (!input.empty()) {
                    const auto next = input.peek();
                    const auto w = lower(next.text);
                    if (next.kind != Kind::Ident ||
                        (w != "nowrap" && w != "wrap" && w != "wrap-reverse" && w != "balance"))
                        break;
                    end = input.next().end;
                }
                if (end == token.start)
                    fail(token.start, "expected flex-flow value");
                parsed_wrap = entire<FlexWrap>(Scanner(value, token.start, end));
                wrap = true;
            }
        }
        if (!direction && !wrap)
            fail(0, "expected flex-flow value");
        s.flex_direction = parsed_direction;
        s.flex_wrap = parsed_wrap;
        return;
    }
    if (p == "flex") {
        Scanner input(value);
        const auto token = input.peek();
        if (token.kind == Kind::Ident && lower(token.text) == "none") {
            input.next();
            input.exhausted();
            s.flex_grow = 0;
            s.flex_shrink = 0;
            s.flex_basis = Dimension::auto_();
            return;
        }
        if (token.kind == Kind::Ident && lower(token.text) == "initial") {
            input.next();
            input.exhausted();
            s.flex_grow = 0;
            s.flex_shrink = 1;
            s.flex_basis = Dimension::auto_();
            return;
        }
        auto factor = [&] {
            const auto offset = input.offset();
            const auto n = parse<float>(input);
            if (!(n >= 0))
                fail(offset, "negative flex factor", ParseErrorCode::InvalidValue);
            return n;
        };
        auto flex_basis = [&] {
            const auto next = input.peek();
            // The shorthand disambiguates a third unitless zero as its basis;
            // the upstream standalone Dimension grammar still requires a unit.
            if (next.kind == Kind::Number && next.value == 0) {
                input.next();
                return Dimension::length(next.value);
            }
            if (next.kind == Kind::Ident && lower(next.text) == "auto") {
                input.next();
                return Dimension::auto_();
            }
            return parse<Dimension>(input);
        };
        float grow = 1, shrink = 1;
        Dimension basis = Dimension::percent(0);
        if (token.kind == Kind::Number) {
            grow = factor();
            if (!input.empty() && input.peek().kind == Kind::Number)
                shrink = factor();
            if (!input.empty())
                basis = flex_basis();
        } else {
            basis = flex_basis();
            if (!input.empty())
                grow = factor();
            if (!input.empty())
                shrink = factor();
        }
        input.exhausted();
        s.flex_grow = grow;
        s.flex_shrink = shrink;
        s.flex_basis = basis;
        return;
    }
    if (p == "place-items" || p == "place-content" || p == "place-self") {
        // Alignment values are one or two identifiers, not whitespace-split scalars.
        Scanner input(value);
        auto item = [&] {
            return parse<AlignItems>(input);
        };
        if (p == "place-content") {
            auto a = parse<AlignContent>(input);
            auto b = input.empty() ? a : parse<AlignContent>(input);
            input.exhausted();
            s.align_content = a;
            s.justify_content = b;
        } else if (p == "place-items") {
            auto a = item();
            auto b = input.empty() ? a : item();
            input.exhausted();
            s.align_items = a;
            s.justify_items = b;
        } else {
            auto self = [&]() -> std::optional<AlignItems> {
                auto next = input.peek();
                if (next.kind == Kind::Ident && lower(next.text) == "auto") {
                    input.next();
                    return {};
                }
                return item();
            };
            auto a = self();
            auto b = input.empty() ? a : self();
            input.exhausted();
            s.align_self = a;
            s.justify_self = b;
        }
        return;
    }
    if (p == "grid-area") {
        Scanner input(value);
        std::vector<GridPlacement> v;
        v.push_back(parse<GridPlacement>(input));
        while (!input.empty()) {
            input.expect('/');
            v.push_back(parse<GridPlacement>(input));
            if (v.size() > 4)
                fail(input.offset(), "too many grid-area placements");
        }
        auto fallback = [](const GridPlacement& x) {
            return x.kind() == GridPlacement::Kind::NamedLine && x.line_index() == 0
                       ? x
                       : GridPlacement::auto_();
        };
        Line<GridPlacement> row{v[0], v.size() > 2 ? v[2] : fallback(v[0])};
        Line<GridPlacement> column{v.size() > 1 ? v[1] : fallback(v[0]),
                                   v.size() > 3 ? v[3] : fallback(v.size() > 1 ? v[1] : v[0])};
        static_assert(std::is_nothrow_move_assignable_v<Line<GridPlacement>>);
        s.grid_row = std::move(row);
        s.grid_column = std::move(column);
        return;
    }
    throw PropertyNameError{
        ParseError{ParseErrorCode::InvalidValue, 0, "unknown style property: " + p}};
}
} // namespace

ParseResult<void> parse_style_property(Style& style, std::string_view name,
                                       std::string_view value) {
    try {
        property(style, name, value);
        return {};
    } catch (const PropertyNameError& e) {
        return std::unexpected(e.value);
    } catch (const ParseError& e) {
        return std::unexpected(e);
    }
}

ParseResult<Style> parse_style(std::string_view input) {
    return result<Style>([&] {
        Style style;
        Scanner scanner(input);
        struct ImportantDeclaration {
            std::string_view name, value;
            std::size_t name_offset, value_offset;
        };
        std::vector<ImportantDeclaration> important;
        auto apply = [&](std::string_view name, std::string_view value, std::size_t name_offset,
                         std::size_t value_offset) {
            try {
                property(style, name, value);
            } catch (const PropertyNameError& error) {
                auto e = error.value;
                e.offset += name_offset;
                throw e;
            } catch (const ParseError& error) {
                auto e = error;
                e.offset += value_offset;
                throw e;
            }
        };
        while (!scanner.empty()) {
            const auto name = scanner.next();
            if (name.kind == Kind::Delim && name.delim == ';')
                continue;
            if (name.kind != Kind::Ident)
                fail(name.start, "expected declaration property name");
            scanner.expect(':');
            const auto start = scanner.offset();
            std::size_t end = start;
            std::vector<Token> tokens;
            while (!scanner.empty()) {
                auto token = scanner.peek();
                if (token.kind == Kind::Delim && token.delim == ';')
                    break;
                tokens.push_back(scanner.next());
                end = tokens.back().end;
            }
            const bool priority = tokens.size() >= 2 && tokens.back().kind == Kind::Ident &&
                                  lower(tokens.back().text) == "important" &&
                                  tokens[tokens.size() - 2].kind == Kind::Delim &&
                                  tokens[tokens.size() - 2].delim == '!';
            if (priority)
                end = tokens.size() > 2 ? tokens[tokens.size() - 3].end : start;
            auto value = input.substr(start, end - start);
            apply(scanner.raw(name), value, name.start, start);
            if (priority)
                important.push_back({scanner.raw(name), value, name.start, start});
            if (!scanner.empty())
                scanner.expect(';');
        }
        // Replaying important declarations in source order implements priority for
        // overlapping shorthands/longhands as well as ordinary duplicate properties.
        for (const auto& declaration : important)
            apply(declaration.name, declaration.value, declaration.name_offset,
                  declaration.value_offset);
        return style;
    });
}

template <>
ParseResult<Style> parse_css<Style>(std::string_view input) {
    return parse_style(input);
}

} // namespace taffy
