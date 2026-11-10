#pragma once

// Included by parse.hpp after the public parser declarations. Only generic
// definitions live here; concrete grammars and scanning remain in parse.cpp.
namespace taffy::detail {

template <class T>
struct CssValueParser {
    static ParseResult<T> parse(std::string_view input) {
        if constexpr (std::is_integral_v<T> && !std::same_as<T, bool> &&
                      requires(const char* p, T& n) { std::from_chars(p, p, n); }) {
            auto token = css_integer_token(input);
            if (!token)
                return std::unexpected(token.error());
            auto text = *token;
            if (!text.empty() && text.front() == '+')
                text.remove_prefix(1);
            T value{};
            auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (ec != std::errc{} || end != text.data() + text.size())
                return std::unexpected(
                    ParseError{ParseErrorCode::InvalidValue,
                               static_cast<std::size_t>(token->data() - input.data()),
                               "integer out of range"});
            return value;
        } else {
            return std::unexpected(
                ParseError{ParseErrorCode::Unsupported, 0, "this type has no CSS value grammar"});
        }
    }
};

template <class T>
ParseResult<T> css_component(std::string_view whole, std::string_view part) {
    auto result = parse_css<T>(part);
    if (!result)
        result.error().offset += static_cast<std::size_t>(part.data() - whole.data());
    return result;
}

template <class T>
struct CssValueParser<std::optional<T>> {
    static ParseResult<std::optional<T>> parse(std::string_view input) {
        // Auto is the absence spelling for optional self-alignment/aspect ratio.
        if (css_auto(input))
            return std::optional<T>{};
        auto value = parse_css<T>(input);
        if (!value)
            return std::unexpected(value.error());
        return std::optional<T>{std::move(*value)};
    }
};

template <class T>
struct CssValueParser<std::vector<T>> {
    static ParseResult<std::vector<T>> parse(std::string_view input) {
        auto parts = css_components(input);
        if (!parts)
            return std::unexpected(parts.error());
        if (parts->empty())
            return std::unexpected(
                ParseError{ParseErrorCode::InvalidSyntax, input.size(), "expected a value"});
        std::vector<T> result;
        for (auto part : *parts) {
            auto value = css_component<T>(input, part);
            if (!value)
                return std::unexpected(value.error());
            result.push_back(std::move(*value));
        }
        return result;
    }
};

template <class T, unsigned N>
ParseResult<std::vector<T>> css_tuple(std::string_view input) {
    auto values = CssValueParser<std::vector<T>>::parse(input);
    if (!values)
        return std::unexpected(values.error());
    if (values->size() > N)
        return std::unexpected(
            ParseError{ParseErrorCode::InvalidSyntax, 0, "too many component values"});
    return values;
}

template <class T>
struct CssValueParser<Size<T>> {
    static ParseResult<Size<T>> parse(std::string_view input) {
        auto v = css_tuple<T, 2>(input);
        if (!v)
            return std::unexpected(v.error());
        return Size<T>{(*v)[0], v->size() == 1 ? (*v)[0] : (*v)[1]};
    }
};

template <class T>
struct CssValueParser<Point<T>> {
    static ParseResult<Point<T>> parse(std::string_view input) {
        auto v = CssValueParser<Size<T>>::parse(input);
        if (!v)
            return std::unexpected(v.error());
        return Point<T>{v->width, v->height};
    }
};

template <class T>
struct CssValueParser<InBothAbsAxis<T>> {
    static ParseResult<InBothAbsAxis<T>> parse(std::string_view input) {
        auto v = CssValueParser<Size<T>>::parse(input);
        if (!v)
            return std::unexpected(v.error());
        return InBothAbsAxis<T>{v->width, v->height};
    }
};

template <class T>
struct CssValueParser<Line<T>> {
    static ParseResult<Line<T>> parse(std::string_view input) {
        auto v = CssValueParser<Size<T>>::parse(input);
        if (!v)
            return std::unexpected(v.error());
        return Line<T>{v->width, v->height};
    }
};

template <class Min, class Max>
struct CssValueParser<MinMax<Min, Max>> {
    static ParseResult<MinMax<Min, Max>> parse(std::string_view input) {
        if constexpr (std::same_as<Min, MinTrackSizingFunction> &&
                      std::same_as<Max, MaxTrackSizingFunction>) {
            auto v = parse_css<TrackSizingFunction>(input);
            if (!v)
                return std::unexpected(v.error());
            return MinMax<Min, Max>{v->min, v->max};
        } else {
            auto args = css_function_arguments(input, "minmax");
            if (!args)
                return std::unexpected(args.error());
            if (args->size() != 2)
                return std::unexpected(
                    ParseError{ParseErrorCode::InvalidSyntax, 0, "minmax requires two arguments"});
            auto min = css_component<Min>(input, (*args)[0]);
            if (!min)
                return std::unexpected(min.error());
            auto max = css_component<Max>(input, (*args)[1]);
            if (!max)
                return std::unexpected(max.error());
            return MinMax<Min, Max>{std::move(*min), std::move(*max)};
        }
    }
};

template <class T>
struct CssValueParser<Rect<T>> {
    static ParseResult<Rect<T>> parse(std::string_view input) {
        auto v = css_tuple<T, 4>(input);
        if (!v)
            return std::unexpected(v.error());
        const auto& top = (*v)[0];
        const auto& right = v->size() > 1 ? (*v)[1] : top;
        const auto& bottom = v->size() > 2 ? (*v)[2] : top;
        const auto& left = v->size() > 3 ? (*v)[3] : right;
        return Rect<T>{left, right, top, bottom};
    }
};

} // namespace taffy::detail
