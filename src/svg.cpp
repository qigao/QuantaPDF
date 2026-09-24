#include "internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr size_t k_svg_max_input_bytes =
    static_cast<size_t>(16u) * 1024u * 1024u;
constexpr size_t k_svg_max_depth = 64u;
constexpr double k_circle_kappa = 0.5522847498307936;
constexpr double k_pi = 3.14159265358979323846;

struct svg_error {
    quantapdf_status status;
};

[[noreturn]] void fail(quantapdf_status status)
{
    throw svg_error{status};
}

bool finite(double value)
{
    return std::isfinite(value);
}

bool ascii_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

std::string trim(std::string_view value)
{
    size_t first = 0u;
    size_t last = value.size();
    while (first < last && ascii_space(value[first]))
        ++first;
    while (last > first && ascii_space(value[last - 1u]))
        --last;
    return std::string(value.substr(first, last - first));
}

std::string lower_ascii(std::string value)
{
    for (char& ch: value) {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
    return value;
}

bool name_char(char ch)
{
    unsigned char const uch = static_cast<unsigned char>(ch);
    return std::isalnum(uch) != 0 || ch == '_' || ch == '-' ||
        ch == ':' || ch == '.';
}

std::string local_name(std::string const& name)
{
    size_t const colon = name.rfind(':');
    return colon == std::string::npos ? name : name.substr(colon + 1u);
}

struct matrix {
    double a = 1.0;
    double b = 0.0;
    double c = 0.0;
    double d = 1.0;
    double e = 0.0;
    double f = 0.0;
};

matrix multiply(matrix const& left, matrix const& right)
{
    matrix result;
    result.a = left.a * right.a + left.c * right.b;
    result.b = left.b * right.a + left.d * right.b;
    result.c = left.a * right.c + left.c * right.d;
    result.d = left.b * right.c + left.d * right.d;
    result.e = left.a * right.e + left.c * right.f + left.e;
    result.f = left.b * right.e + left.d * right.f + left.f;
    return result;
}

matrix translate_matrix(double x, double y)
{
    matrix result;
    result.e = x;
    result.f = y;
    return result;
}

matrix scale_matrix(double x, double y)
{
    matrix result;
    result.a = x;
    result.d = y;
    return result;
}

matrix rotate_matrix(double degrees)
{
    double const radians = degrees * k_pi / 180.0;
    double const cosine = std::cos(radians);
    double const sine = std::sin(radians);
    matrix result;
    result.a = cosine;
    result.b = sine;
    result.c = -sine;
    result.d = cosine;
    return result;
}

quantapdf_point apply(matrix const& transform, double x, double y)
{
    double const tx = transform.a * x + transform.c * y + transform.e;
    double const ty = transform.b * x + transform.d * y + transform.f;
    if (!finite(tx) || !finite(ty) ||
        tx < -static_cast<double>(std::numeric_limits<float>::max()) ||
        tx > static_cast<double>(std::numeric_limits<float>::max()) ||
        ty < -static_cast<double>(std::numeric_limits<float>::max()) ||
        ty > static_cast<double>(std::numeric_limits<float>::max()))
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    return {
        static_cast<float>(tx),
        static_cast<float>(ty)};
}

class number_scanner {
  public:
    explicit number_scanner(std::string const& text):
        text_(text)
    {
    }

    void skip_separators()
    {
        while (position_ < text_.size()) {
            char const ch = text_[position_];
            if (ascii_space(ch) || ch == ',')
                ++position_;
            else
                break;
        }
    }

    bool done()
    {
        skip_separators();
        return position_ == text_.size();
    }

    bool next_is_number()
    {
        skip_separators();
        if (position_ == text_.size())
            return false;
        char const ch = text_[position_];
        return ch == '+' || ch == '-' || ch == '.' ||
            (ch >= '0' && ch <= '9');
    }

    bool next_is_alpha()
    {
        skip_separators();
        return position_ < text_.size() &&
            std::isalpha(static_cast<unsigned char>(text_[position_])) != 0;
    }

    char take_alpha()
    {
        skip_separators();
        if (!next_is_alpha())
            fail(QUANTAPDF_ERROR_FORMAT);
        return text_[position_++];
    }

    double number()
    {
        skip_separators();
        if (position_ == text_.size())
            fail(QUANTAPDF_ERROR_FORMAT);
        char const* begin = text_.c_str() + position_;
        char* end = nullptr;
        double const value = std::strtod(begin, &end);
        if (end == begin || !finite(value))
            fail(QUANTAPDF_ERROR_FORMAT);
        size_t const consumed = static_cast<size_t>(end - begin);
        if (consumed == 0u || consumed > text_.size() - position_)
            fail(QUANTAPDF_ERROR_FORMAT);
        position_ += consumed;
        return value;
    }

  private:
    std::string const& text_;
    size_t position_ = 0u;
};

double scalar(std::string const& value)
{
    std::string text = trim(value);
    if (text.empty())
        fail(QUANTAPDF_ERROR_FORMAT);
    if (text.size() > 2u &&
        text.compare(text.size() - 2u, 2u, "px") == 0)
        text.resize(text.size() - 2u);
    number_scanner scanner(text);
    double const result = scanner.number();
    if (!scanner.done())
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    return result;
}

std::vector<double> number_list(std::string const& value)
{
    std::string text = trim(value);
    number_scanner scanner(text);
    std::vector<double> values;
    while (!scanner.done()) {
        if (!scanner.next_is_number())
            fail(QUANTAPDF_ERROR_FORMAT);
        values.push_back(scanner.number());
    }
    return values;
}

matrix parse_transform(std::string const& value)
{
    std::string text = trim(value);
    size_t position = 0u;
    matrix result;

    auto skip = [&]() {
        while (position < text.size() &&
               (ascii_space(text[position]) || text[position] == ','))
            ++position;
    };

    while (true) {
        skip();
        if (position == text.size())
            break;

        size_t const name_begin = position;
        while (position < text.size() &&
               std::isalpha(static_cast<unsigned char>(text[position])) != 0)
            ++position;
        if (position == name_begin)
            fail(QUANTAPDF_ERROR_FORMAT);
        std::string const name =
            text.substr(name_begin, position - name_begin);
        skip();
        if (position >= text.size() || text[position] != '(')
            fail(QUANTAPDF_ERROR_FORMAT);
        ++position;

        size_t const args_begin = position;
        int nesting = 1;
        while (position < text.size() && nesting != 0) {
            if (text[position] == '(')
                ++nesting;
            else if (text[position] == ')')
                --nesting;
            if (nesting != 0)
                ++position;
        }
        if (nesting != 0 || position > text.size())
            fail(QUANTAPDF_ERROR_FORMAT);
        std::string const args =
            text.substr(args_begin, position - args_begin);
        ++position;

        auto const values = number_list(args);
        matrix current;
        if (name == "matrix") {
            if (values.size() != 6u)
                fail(QUANTAPDF_ERROR_FORMAT);
            current = {
                values[0], values[1], values[2],
                values[3], values[4], values[5]};
        } else if (name == "translate") {
            if (values.size() != 1u && values.size() != 2u)
                fail(QUANTAPDF_ERROR_FORMAT);
            current = translate_matrix(
                values[0], values.size() == 2u ? values[1] : 0.0);
        } else if (name == "scale") {
            if (values.size() != 1u && values.size() != 2u)
                fail(QUANTAPDF_ERROR_FORMAT);
            current = scale_matrix(
                values[0],
                values.size() == 2u ? values[1] : values[0]);
        } else if (name == "rotate") {
            if (values.size() != 1u && values.size() != 3u)
                fail(QUANTAPDF_ERROR_FORMAT);
            current = rotate_matrix(values[0]);
            if (values.size() == 3u) {
                current = multiply(
                    translate_matrix(values[1], values[2]),
                    multiply(
                        current,
                        translate_matrix(-values[1], -values[2])));
            }
        } else if (name == "skewX") {
            if (values.size() != 1u)
                fail(QUANTAPDF_ERROR_FORMAT);
            double const tangent =
                std::tan(values[0] * k_pi / 180.0);
            if (!finite(tangent))
                fail(QUANTAPDF_ERROR_FORMAT);
            current.c = tangent;
        } else if (name == "skewY") {
            if (values.size() != 1u)
                fail(QUANTAPDF_ERROR_FORMAT);
            double const tangent =
                std::tan(values[0] * k_pi / 180.0);
            if (!finite(tangent))
                fail(QUANTAPDF_ERROR_FORMAT);
            current.b = tangent;
        } else {
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        }

        for (double component: {
                 current.a, current.b, current.c,
                 current.d, current.e, current.f}) {
            if (!finite(component))
                fail(QUANTAPDF_ERROR_FORMAT);
        }

        // SVG transform-list items are applied in the order written.
        // With column vectors, that is left-to-right matrix concatenation.
        result = multiply(result, current);
    }
    return result;
}

struct paint_style {
    bool fill = true;
    bool stroke = false;
    uint32_t fill_argb = UINT32_C(0xff000000);
    uint32_t stroke_argb = UINT32_C(0xff000000);
    double stroke_width = 1.0;
    double fill_opacity = 1.0;
    double stroke_opacity = 1.0;
    double opacity = 1.0;
    std::vector<double> dash_array;
    double dash_offset = 0.0;
    std::string fill_ref;
    std::string stroke_ref;
    std::string clip_ref;
    quantapdf_composer_fill_rule fill_rule =
        QUANTAPDF_COMPOSER_FILL_NONZERO;
    quantapdf_composer_line_cap line_cap =
        QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    quantapdf_composer_line_join line_join =
        QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    double miter_limit = 4.0;
};

uint32_t rgb(unsigned int red, unsigned int green, unsigned int blue)
{
    return UINT32_C(0xff000000) |
        (static_cast<uint32_t>(red) << 16u) |
        (static_cast<uint32_t>(green) << 8u) |
        static_cast<uint32_t>(blue);
}

uint32_t parse_color(std::string value, bool* enabled)
{
    value = lower_ascii(trim(value));
    if (value == "none") {
        *enabled = false;
        return UINT32_C(0xff000000);
    }
    *enabled = true;

    if (value == "black")
        return rgb(0u, 0u, 0u);
    if (value == "white")
        return rgb(255u, 255u, 255u);
    if (value == "red")
        return rgb(255u, 0u, 0u);
    if (value == "green")
        return rgb(0u, 128u, 0u);
    if (value == "blue")
        return rgb(0u, 0u, 255u);
    if (value == "yellow")
        return rgb(255u, 255u, 0u);
    if (value == "cyan" || value == "aqua")
        return rgb(0u, 255u, 255u);
    if (value == "magenta" || value == "fuchsia")
        return rgb(255u, 0u, 255u);
    if (value == "gray" || value == "grey")
        return rgb(128u, 128u, 128u);

    if (value.size() == 4u && value[0] == '#') {
        auto nibble = [](char ch) -> unsigned int {
            if (ch >= '0' && ch <= '9')
                return static_cast<unsigned int>(ch - '0');
            if (ch >= 'a' && ch <= 'f')
                return static_cast<unsigned int>(ch - 'a' + 10);
            fail(QUANTAPDF_ERROR_FORMAT);
        };
        unsigned int const r = nibble(value[1]);
        unsigned int const g = nibble(value[2]);
        unsigned int const b = nibble(value[3]);
        return rgb(r * 17u, g * 17u, b * 17u);
    }
    if (value.size() == 7u && value[0] == '#') {
        auto byte = [&](size_t at) -> unsigned int {
            auto nibble = [](char ch) -> unsigned int {
                if (ch >= '0' && ch <= '9')
                    return static_cast<unsigned int>(ch - '0');
                if (ch >= 'a' && ch <= 'f')
                    return static_cast<unsigned int>(ch - 'a' + 10);
                fail(QUANTAPDF_ERROR_FORMAT);
            };
            return nibble(value[at]) * 16u + nibble(value[at + 1u]);
        };
        return rgb(byte(1u), byte(3u), byte(5u));
    }
    fail(QUANTAPDF_ERROR_UNSUPPORTED);
}

bool local_fragment_url(
    std::string const& value,
    std::string* out_id)
{
    std::string const text = trim(value);
    if (text.rfind("url(", 0u) != 0u)
        return false;
    if (text.size() < 7u || text.back() != ')' || text[4] != '#')
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    std::string const id = text.substr(5u, text.size() - 6u);
    if (id.empty())
        fail(QUANTAPDF_ERROR_FORMAT);
    for (char ch: id) {
        if (!name_char(ch))
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }
    *out_id = id;
    return true;
}

double opacity_value(std::string const& value)
{
    std::string const text = trim(value);
    if (text.empty())
        fail(QUANTAPDF_ERROR_FORMAT);
    number_scanner scanner(text);
    double const parsed = scanner.number();
    if (!scanner.done())
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    if (parsed < 0.0)
        return 0.0;
    if (parsed > 1.0)
        return 1.0;
    return parsed;
}

std::vector<double> dash_array_value(std::string const& value)
{
    std::string const parsed = lower_ascii(trim(value));
    if (parsed == "none")
        return {};
    auto values = number_list(parsed);
    if (values.empty())
        fail(QUANTAPDF_ERROR_FORMAT);
    double sum = 0.0;
    for (double item: values) {
        if (!finite(item) || item < 0.0)
            fail(QUANTAPDF_ERROR_FORMAT);
        sum += item;
        if (!finite(sum))
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }
    if (sum == 0.0)
        return {};
    if ((values.size() & 1u) != 0u) {
        if (values.size() > QUANTAPDF_COMPOSER_MAX_DASH_COUNT / 2u)
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        auto duplicate = values;
        values.insert(values.end(), duplicate.begin(), duplicate.end());
    }
    if (values.size() > QUANTAPDF_COMPOSER_MAX_DASH_COUNT)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    return values;
}

void apply_style_property(
    paint_style* style,
    std::string name,
    std::string const& value)
{
    name = lower_ascii(trim(name));
    if (name == "fill") {
        std::string reference;
        if (local_fragment_url(value, &reference)) {
            style->fill = true;
            style->fill_ref = std::move(reference);
        } else {
            style->fill_ref.clear();
            style->fill_argb = parse_color(value, &style->fill);
        }
    } else if (name == "stroke") {
        std::string reference;
        if (local_fragment_url(value, &reference)) {
            style->stroke = true;
            style->stroke_ref = std::move(reference);
        } else {
            style->stroke_ref.clear();
            style->stroke_argb = parse_color(value, &style->stroke);
        }
    } else if (name == "fill-rule") {
        std::string const parsed = lower_ascii(trim(value));
        if (parsed == "nonzero")
            style->fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
        else if (parsed == "evenodd")
            style->fill_rule = QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
        else
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    } else if (name == "stroke-width") {
        style->stroke_width = scalar(value);
        if (style->stroke_width < 0.0)
            fail(QUANTAPDF_ERROR_FORMAT);
    } else if (name == "stroke-linecap") {
        std::string const parsed = lower_ascii(trim(value));
        if (parsed == "butt")
            style->line_cap = QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
        else if (parsed == "round")
            style->line_cap = QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
        else if (parsed == "square")
            style->line_cap = QUANTAPDF_COMPOSER_LINE_CAP_SQUARE;
        else
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    } else if (name == "stroke-linejoin") {
        std::string const parsed = lower_ascii(trim(value));
        if (parsed == "miter")
            style->line_join = QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
        else if (parsed == "round")
            style->line_join = QUANTAPDF_COMPOSER_LINE_JOIN_ROUND;
        else if (parsed == "bevel")
            style->line_join = QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL;
        else
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    } else if (name == "stroke-miterlimit") {
        style->miter_limit = scalar(value);
        if (style->miter_limit < 1.0)
            fail(QUANTAPDF_ERROR_FORMAT);
    } else if (name == "fill-opacity") {
        style->fill_opacity = opacity_value(value);
    } else if (name == "stroke-opacity") {
        style->stroke_opacity = opacity_value(value);
    } else if (name == "opacity") {
        style->opacity = opacity_value(value);
    } else if (name == "stroke-dasharray") {
        style->dash_array = dash_array_value(value);
    } else if (name == "stroke-dashoffset") {
        style->dash_offset = scalar(value);
    } else if (name == "clip-path") {
        std::string const parsed = lower_ascii(trim(value));
        if (parsed == "none") {
            style->clip_ref.clear();
        } else {
            std::string reference;
            if (!local_fragment_url(value, &reference))
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            style->clip_ref = std::move(reference);
        }
    } else {
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }
}

void apply_inline_style(paint_style* style, std::string const& text)
{
    size_t position = 0u;
    while (position < text.size()) {
        size_t const semi = text.find(';', position);
        size_t const end =
            semi == std::string::npos ? text.size() : semi;
        std::string const item =
            trim(std::string_view(text).substr(position, end - position));
        if (!item.empty()) {
            size_t const colon = item.find(':');
            if (colon == std::string::npos)
                fail(QUANTAPDF_ERROR_FORMAT);
            apply_style_property(
                style,
                item.substr(0u, colon),
                item.substr(colon + 1u));
        }
        if (semi == std::string::npos)
            break;
        position = semi + 1u;
    }
}

struct attribute {
    std::string name;
    std::string value;
};

struct element {
    std::string name;
    std::vector<attribute> attributes;
    bool closing = false;
    bool self_closing = false;
};

class xml_scanner {
  public:
    xml_scanner(unsigned char const* data, size_t size):
        input_(
            reinterpret_cast<char const*>(data),
            size)
    {
    }

    bool next(element* out)
    {
        while (true) {
            if (position_ == input_.size())
                return false;
            size_t const lt = input_.find('<', position_);
            if (lt == std::string_view::npos)
                fail(QUANTAPDF_ERROR_FORMAT);
            for (size_t i = position_; i < lt; ++i) {
                if (!ascii_space(input_[i]))
                    fail(QUANTAPDF_ERROR_UNSUPPORTED);
            }
            position_ = lt;

            if (input_.substr(position_, 4u) == "<!--") {
                size_t const end = input_.find("-->", position_ + 4u);
                if (end == std::string_view::npos)
                    fail(QUANTAPDF_ERROR_FORMAT);
                position_ = end + 3u;
                continue;
            }
            if (input_.substr(position_, 2u) == "<?") {
                size_t const end = input_.find("?>", position_ + 2u);
                if (end == std::string_view::npos)
                    fail(QUANTAPDF_ERROR_FORMAT);
                position_ = end + 2u;
                continue;
            }
            if (input_.substr(position_, 2u) == "<!")
                fail(QUANTAPDF_ERROR_UNSUPPORTED);

            ++position_;
            *out = {};
            if (position_ < input_.size() && input_[position_] == '/') {
                out->closing = true;
                ++position_;
            }

            skip_space();
            out->name = parse_name();
            if (out->name.empty())
                fail(QUANTAPDF_ERROR_FORMAT);

            if (out->closing) {
                skip_space();
                if (position_ >= input_.size() || input_[position_] != '>')
                    fail(QUANTAPDF_ERROR_FORMAT);
                ++position_;
                return true;
            }

            while (true) {
                skip_space();
                if (position_ >= input_.size())
                    fail(QUANTAPDF_ERROR_FORMAT);
                if (input_[position_] == '>') {
                    ++position_;
                    return true;
                }
                if (input_[position_] == '/') {
                    ++position_;
                    if (position_ >= input_.size() ||
                        input_[position_] != '>')
                        fail(QUANTAPDF_ERROR_FORMAT);
                    ++position_;
                    out->self_closing = true;
                    return true;
                }

                attribute attr;
                attr.name = parse_name();
                if (attr.name.empty())
                    fail(QUANTAPDF_ERROR_FORMAT);
                for (auto const& existing: out->attributes) {
                    if (existing.name == attr.name)
                        fail(QUANTAPDF_ERROR_FORMAT);
                }
                skip_space();
                if (position_ >= input_.size() ||
                    input_[position_] != '=')
                    fail(QUANTAPDF_ERROR_FORMAT);
                ++position_;
                skip_space();
                if (position_ >= input_.size() ||
                    (input_[position_] != '\'' &&
                     input_[position_] != '"'))
                    fail(QUANTAPDF_ERROR_FORMAT);
                char const quote = input_[position_++];
                size_t const begin = position_;
                while (position_ < input_.size() &&
                       input_[position_] != quote) {
                    unsigned char const byte =
                        static_cast<unsigned char>(input_[position_]);
                    if (byte == 0u || input_[position_] == '&')
                        fail(QUANTAPDF_ERROR_UNSUPPORTED);
                    ++position_;
                }
                if (position_ >= input_.size())
                    fail(QUANTAPDF_ERROR_FORMAT);
                attr.value = std::string(
                    input_.substr(begin, position_ - begin));
                ++position_;
                out->attributes.push_back(std::move(attr));
            }
        }
    }

    void finish()
    {
        for (size_t i = position_; i < input_.size(); ++i) {
            if (!ascii_space(input_[i]))
                fail(QUANTAPDF_ERROR_FORMAT);
        }
        position_ = input_.size();
    }

  private:
    void skip_space()
    {
        while (position_ < input_.size() &&
               ascii_space(input_[position_]))
            ++position_;
    }

    std::string parse_name()
    {
        size_t const begin = position_;
        while (position_ < input_.size() &&
               name_char(input_[position_]))
            ++position_;
        return std::string(input_.substr(begin, position_ - begin));
    }

    std::string_view input_;
    size_t position_ = 0u;
};

std::string const* find_attribute(
    element const& item,
    std::string const& name)
{
    for (auto const& attr: item.attributes) {
        if (attr.name == name)
            return &attr.value;
    }
    return nullptr;
}

bool common_attribute(std::string const& name)
{
    return name == "id" || name == "transform" || name == "style" ||
        name == "fill" || name == "stroke" || name == "fill-rule" ||
        name == "stroke-width" || name == "stroke-linecap" ||
        name == "stroke-linejoin" || name == "stroke-miterlimit" ||
        name == "fill-opacity" || name == "stroke-opacity" ||
        name == "opacity" || name == "stroke-dasharray" ||
        name == "stroke-dashoffset" || name == "clip-path";
}

bool tag_attribute_allowed(
    std::string const& tag,
    std::string const& name,
    bool root)
{
    if (common_attribute(name))
        return true;
    if (root) {
        if (name == "viewBox" || name == "width" || name == "height" ||
            name == "preserveAspectRatio" ||
            name == "version" || name == "xmlns" ||
            name.rfind("xmlns:", 0u) == 0u)
            return true;
    }
    if (tag == "path")
        return name == "d";
    if (tag == "rect")
        return name == "x" || name == "y" ||
            name == "width" || name == "height";
    if (tag == "line")
        return name == "x1" || name == "y1" ||
            name == "x2" || name == "y2";
    if (tag == "polyline" || tag == "polygon")
        return name == "points";
    if (tag == "circle")
        return name == "cx" || name == "cy" || name == "r";
    if (tag == "ellipse")
        return name == "cx" || name == "cy" ||
            name == "rx" || name == "ry";
    return false;
}

void validate_attributes(
    element const& item,
    std::string const& tag,
    bool root)
{
    for (auto const& attr: item.attributes) {
        if (!tag_attribute_allowed(tag, attr.name, root))
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }
}

paint_style derive_style(
    paint_style const& parent,
    element const& item)
{
    paint_style result = parent;
    // SVG opacity and clip-path are not inherited presentation properties.
    result.opacity = 1.0;
    result.clip_ref.clear();
    for (auto const& attr: item.attributes) {
        if (attr.name == "fill" || attr.name == "stroke" ||
            attr.name == "fill-rule" || attr.name == "stroke-width" ||
            attr.name == "stroke-linecap" ||
            attr.name == "stroke-linejoin" ||
            attr.name == "stroke-miterlimit" ||
            attr.name == "fill-opacity" ||
            attr.name == "stroke-opacity" ||
            attr.name == "opacity" ||
            attr.name == "stroke-dasharray" ||
            attr.name == "stroke-dashoffset" ||
            attr.name == "clip-path")
            apply_style_property(&result, attr.name, attr.value);
    }
    if (auto const* style = find_attribute(item, "style"))
        apply_inline_style(&result, *style);
    return result;
}

matrix derive_transform(matrix const& parent, element const& item)
{
    if (auto const* transform = find_attribute(item, "transform"))
        return multiply(parent, parse_transform(*transform));
    return parent;
}

double optional_number(
    element const& item,
    char const* name,
    double fallback)
{
    auto const* value = find_attribute(item, name);
    return value == nullptr ? fallback : scalar(*value);
}

double required_number(element const& item, char const* name)
{
    auto const* value = find_attribute(item, name);
    if (value == nullptr)
        fail(QUANTAPDF_ERROR_FORMAT);
    return scalar(*value);
}

struct preserve_aspect {
    bool none = false;
    double align_x = 0.5;
    double align_y = 0.5;
    bool slice = false;
};

std::vector<std::string> whitespace_tokens(std::string const& value)
{
    std::vector<std::string> result;
    size_t position = 0u;
    while (position < value.size()) {
        while (position < value.size() && ascii_space(value[position]))
            ++position;
        if (position == value.size())
            break;
        size_t const begin = position;
        while (position < value.size() && !ascii_space(value[position]))
            ++position;
        result.push_back(value.substr(begin, position - begin));
    }
    return result;
}

preserve_aspect parse_preserve_aspect(std::string const* value)
{
    preserve_aspect result;
    if (value == nullptr || trim(*value).empty())
        return result;

    auto const tokens = whitespace_tokens(trim(*value));
    if (tokens.empty() || tokens.size() > 2u)
        fail(QUANTAPDF_ERROR_FORMAT);
    if (tokens[0] == "defer")
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    if (tokens[0] == "none") {
        if (tokens.size() != 1u)
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        result.none = true;
        return result;
    }

    struct alignment {
        char const* name;
        double x;
        double y;
    };
    static alignment const alignments[] = {
        {"xMinYMin", 0.0, 0.0},
        {"xMidYMin", 0.5, 0.0},
        {"xMaxYMin", 1.0, 0.0},
        {"xMinYMid", 0.0, 0.5},
        {"xMidYMid", 0.5, 0.5},
        {"xMaxYMid", 1.0, 0.5},
        {"xMinYMax", 0.0, 1.0},
        {"xMidYMax", 0.5, 1.0},
        {"xMaxYMax", 1.0, 1.0}};
    bool found = false;
    for (auto const& item: alignments) {
        if (tokens[0] == item.name) {
            result.align_x = item.x;
            result.align_y = item.y;
            found = true;
            break;
        }
    }
    if (!found)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    if (tokens.size() == 2u) {
        if (tokens[1] == "meet")
            result.slice = false;
        else if (tokens[1] == "slice")
            result.slice = true;
        else
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }
    return result;
}

matrix viewport_matrix(
    quantapdf_rect const& bounds,
    std::vector<double> const& view_box,
    preserve_aspect const& preserve)
{
    double const viewport_width = bounds.x1 - bounds.x0;
    double const viewport_height = bounds.y1 - bounds.y0;
    double const sx = viewport_width / view_box[2];
    double const sy = viewport_height / view_box[3];
    if (!finite(sx) || !finite(sy) || sx <= 0.0 || sy <= 0.0)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);

    if (preserve.none) {
        return multiply(
            translate_matrix(bounds.x0, bounds.y0),
            multiply(
                scale_matrix(sx, sy),
                translate_matrix(-view_box[0], -view_box[1])));
    }

    double const scale =
        preserve.slice ? std::max(sx, sy) : std::min(sx, sy);
    double const used_width = view_box[2] * scale;
    double const used_height = view_box[3] * scale;
    double const tx =
        bounds.x0 + preserve.align_x * (viewport_width - used_width);
    double const ty =
        bounds.y0 + preserve.align_y * (viewport_height - used_height);
    return multiply(
        translate_matrix(tx, ty),
        multiply(
            scale_matrix(scale, scale),
            translate_matrix(-view_box[0], -view_box[1])));
}

struct geometry_bounds {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    bool valid = false;
};

struct clip_component {
    std::string ref;
    matrix transform;
    geometry_bounds local_bounds;
};

struct staged_path {
    size_t order = 0u;
    std::vector<quantapdf_composer_path_command> commands;
    quantapdf_composer_path_options options{};
    std::vector<float> dash_lengths;
    float dash_phase = 0.0f;
    float fill_alpha = 1.0f;
    float stroke_alpha = 1.0f;
    std::string fill_ref;
    std::string stroke_ref;
    matrix resource_transform;
    geometry_bounds local_bounds;
    std::vector<clip_component> clip_components;
};

struct staged_use {
    size_t order = 0u;
    std::string symbol_ref;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    matrix user_transform;
    std::vector<clip_component> clip_components;
};

struct staged_group {
    size_t order = 0u;
    std::string svg;
    float opacity = 1.0f;
    std::vector<clip_component> clip_components;
};

quantapdf_affine_transform resource_affine(matrix const& value);

void transform_commands(
    std::vector<quantapdf_composer_path_command>* commands,
    matrix const& transform)
{
    for (auto& command: *commands) {
        switch (command.kind) {
        case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
        case QUANTAPDF_COMPOSER_PATH_LINE_TO:
            command.point1 =
                apply(transform, command.point1.x, command.point1.y);
            break;
        case QUANTAPDF_COMPOSER_PATH_CUBIC_TO:
            command.point1 =
                apply(transform, command.point1.x, command.point1.y);
            command.point2 =
                apply(transform, command.point2.x, command.point2.y);
            command.point3 =
                apply(transform, command.point3.x, command.point3.y);
            break;
        case QUANTAPDF_COMPOSER_PATH_CLOSE:
            break;
        default:
            fail(QUANTAPDF_ERROR_BACKEND);
        }
    }
}

void include_bound_point(
    geometry_bounds* bounds,
    double x,
    double y)
{
    if (!finite(x) || !finite(y))
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    if (!bounds->valid) {
        bounds->x0 = bounds->x1 = x;
        bounds->y0 = bounds->y1 = y;
        bounds->valid = true;
        return;
    }
    bounds->x0 = std::min(bounds->x0, x);
    bounds->y0 = std::min(bounds->y0, y);
    bounds->x1 = std::max(bounds->x1, x);
    bounds->y1 = std::max(bounds->y1, y);
}

double cubic_coordinate(
    double p0,
    double p1,
    double p2,
    double p3,
    double t)
{
    double const mt = 1.0 - t;
    return mt * mt * mt * p0 +
        3.0 * mt * mt * t * p1 +
        3.0 * mt * t * t * p2 +
        t * t * t * p3;
}

void include_cubic_extrema(
    geometry_bounds* bounds,
    double x0,
    double y0,
    quantapdf_point const& p1,
    quantapdf_point const& p2,
    quantapdf_point const& p3)
{
    include_bound_point(bounds, x0, y0);
    include_bound_point(bounds, p3.x, p3.y);

    auto roots = [](double p0, double p1, double p2, double p3) {
        std::vector<double> result;
        double const a = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
        double const b = 2.0 * (p0 - 2.0 * p1 + p2);
        double const d = p1 - p0;
        if (a == 0.0) {
            if (b != 0.0) {
                double const t = -d / b;
                if (t > 0.0 && t < 1.0)
                    result.push_back(t);
            }
            return result;
        }
        double const discriminant = b * b - 4.0 * a * d;
        if (discriminant < 0.0)
            return result;
        double const root = std::sqrt(std::max(0.0, discriminant));
        double const t1 = (-b + root) / (2.0 * a);
        double const t2 = (-b - root) / (2.0 * a);
        if (t1 > 0.0 && t1 < 1.0)
            result.push_back(t1);
        if (t2 > 0.0 && t2 < 1.0 && t2 != t1)
            result.push_back(t2);
        return result;
    };

    auto const x_roots = roots(x0, p1.x, p2.x, p3.x);
    auto const y_roots = roots(y0, p1.y, p2.y, p3.y);
    for (double t: x_roots) {
        include_bound_point(
            bounds,
            cubic_coordinate(x0, p1.x, p2.x, p3.x, t),
            cubic_coordinate(y0, p1.y, p2.y, p3.y, t));
    }
    for (double t: y_roots) {
        include_bound_point(
            bounds,
            cubic_coordinate(x0, p1.x, p2.x, p3.x, t),
            cubic_coordinate(y0, p1.y, p2.y, p3.y, t));
    }
}

geometry_bounds command_bounds(
    std::vector<quantapdf_composer_path_command> const& commands)
{
    geometry_bounds bounds;
    double x = 0.0;
    double y = 0.0;
    double subpath_x = 0.0;
    double subpath_y = 0.0;
    bool have_current = false;
    bool have_subpath = false;
    bool subpath_has_draw = false;

    for (auto const& command: commands) {
        switch (command.kind) {
        case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
            x = command.point1.x;
            y = command.point1.y;
            subpath_x = x;
            subpath_y = y;
            have_current = true;
            have_subpath = true;
            subpath_has_draw = false;
            break;
        case QUANTAPDF_COMPOSER_PATH_LINE_TO:
            if (!have_current)
                fail(QUANTAPDF_ERROR_BACKEND);
            include_bound_point(&bounds, x, y);
            include_bound_point(
                &bounds, command.point1.x, command.point1.y);
            x = command.point1.x;
            y = command.point1.y;
            subpath_has_draw = true;
            break;
        case QUANTAPDF_COMPOSER_PATH_CUBIC_TO:
            if (!have_current)
                fail(QUANTAPDF_ERROR_BACKEND);
            include_cubic_extrema(
                &bounds,
                x,
                y,
                command.point1,
                command.point2,
                command.point3);
            x = command.point3.x;
            y = command.point3.y;
            subpath_has_draw = true;
            break;
        case QUANTAPDF_COMPOSER_PATH_CLOSE:
            if (!have_current || !have_subpath)
                fail(QUANTAPDF_ERROR_BACKEND);
            if (subpath_has_draw) {
                include_bound_point(&bounds, x, y);
                include_bound_point(&bounds, subpath_x, subpath_y);
            }
            x = subpath_x;
            y = subpath_y;
            break;
        default:
            fail(QUANTAPDF_ERROR_BACKEND);
        }
    }
    return bounds;
}

void stage_path(
    std::vector<staged_path>* paths,
    std::vector<quantapdf_composer_path_command> commands,
    paint_style const& style,
    matrix const& transform,
    size_t order,
    size_t max_paths)
{
    if (!style.fill && !style.stroke)
        return;
    if (commands.empty())
        fail(QUANTAPDF_ERROR_FORMAT);
    if (paths->size() >= max_paths)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);

    staged_path staged;
    staged.order = order;
    staged.commands = std::move(commands);
    staged.local_bounds = command_bounds(staged.commands);
    staged.fill_ref = style.fill ? style.fill_ref : std::string{};
    staged.stroke_ref = style.stroke ? style.stroke_ref : std::string{};
    staged.clip_ref = style.clip_ref;
    // Clip resources are established outside the PATH q/cm scope, so they
    // still need the full referencing-element transform.
    staged.resource_transform = transform;
    staged.options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    staged.options.fill = style.fill ? 1 : 0;
    staged.options.stroke = style.stroke ? 1 : 0;
    staged.options.fill_argb = style.fill_argb;
    staged.options.stroke_argb = style.stroke_argb;
    staged.options.fill_rule = style.fill_rule;
    staged.options.line_cap = style.line_cap;
    staged.options.line_join = style.line_join;
    staged.options.transform = resource_affine(transform);

    if (!finite(style.miter_limit) ||
        style.miter_limit < 1.0 ||
        style.miter_limit > std::numeric_limits<float>::max())
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    staged.options.miter_limit = static_cast<float>(style.miter_limit);

    if (style.stroke) {
        if (!finite(style.stroke_width) || style.stroke_width < 0.0 ||
            style.stroke_width > std::numeric_limits<float>::max())
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        staged.options.stroke_width =
            static_cast<float>(style.stroke_width);
    }

    double const fill_alpha =
        style.fill ? style.opacity * style.fill_opacity : 1.0;
    double const stroke_alpha =
        style.stroke ? style.opacity * style.stroke_opacity : 1.0;
    if (!finite(fill_alpha) || !finite(stroke_alpha) ||
        fill_alpha < 0.0 || fill_alpha > 1.0 ||
        stroke_alpha < 0.0 || stroke_alpha > 1.0)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    staged.fill_alpha = static_cast<float>(fill_alpha);
    staged.stroke_alpha = static_cast<float>(stroke_alpha);

    if (style.stroke && !style.dash_array.empty()) {
        double pattern_length = 0.0;
        staged.dash_lengths.reserve(style.dash_array.size());
        for (double item: style.dash_array) {
            if (!finite(item) || item < 0.0 ||
                item > std::numeric_limits<float>::max())
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            pattern_length += item;
            if (!finite(pattern_length))
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            staged.dash_lengths.push_back(static_cast<float>(item));
        }
        if (pattern_length <= 0.0) {
            staged.dash_lengths.clear();
        } else {
            double phase = style.dash_offset;
            if (!finite(phase))
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            phase = std::fmod(phase, pattern_length);
            if (phase < 0.0)
                phase += pattern_length;
            if (phase > std::numeric_limits<float>::max())
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            staged.dash_phase = static_cast<float>(phase);
        }
    }

    paths->push_back(std::move(staged));
}

quantapdf_point point(double x, double y)
{
    if (!finite(x) || !finite(y) ||
        x < -std::numeric_limits<float>::max() ||
        x > std::numeric_limits<float>::max() ||
        y < -std::numeric_limits<float>::max() ||
        y > std::numeric_limits<float>::max())
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    return {static_cast<float>(x), static_cast<float>(y)};
}

quantapdf_composer_path_command move_to(double x, double y)
{
    quantapdf_composer_path_command command{};
    command.kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    command.point1 = point(x, y);
    return command;
}

quantapdf_composer_path_command line_to(double x, double y)
{
    quantapdf_composer_path_command command{};
    command.kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    command.point1 = point(x, y);
    return command;
}

quantapdf_composer_path_command cubic_to(
    double x1,
    double y1,
    double x2,
    double y2,
    double x3,
    double y3)
{
    quantapdf_composer_path_command command{};
    command.kind = QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    command.point1 = point(x1, y1);
    command.point2 = point(x2, y2);
    command.point3 = point(x3, y3);
    return command;
}

quantapdf_composer_path_command close_path()
{
    quantapdf_composer_path_command command{};
    command.kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
    return command;
}

double vector_angle(
    double ux,
    double uy,
    double vx,
    double vy)
{
    return std::atan2(
        ux * vy - uy * vx,
        ux * vx + uy * vy);
}

void append_arc(
    std::vector<quantapdf_composer_path_command>* result,
    double x0,
    double y0,
    double rx,
    double ry,
    double rotation_degrees,
    bool large_arc,
    bool sweep,
    double x1,
    double y1)
{
    if (x0 == x1 && y0 == y1)
        return;
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    if (rx == 0.0 || ry == 0.0) {
        result->push_back(line_to(x1, y1));
        return;
    }

    double const phi = std::fmod(rotation_degrees, 360.0) * k_pi / 180.0;
    double const cos_phi = std::cos(phi);
    double const sin_phi = std::sin(phi);
    if (!finite(cos_phi) || !finite(sin_phi))
        fail(QUANTAPDF_ERROR_FORMAT);

    double const half_dx = (x0 - x1) / 2.0;
    double const half_dy = (y0 - y1) / 2.0;
    double const x1p = cos_phi * half_dx + sin_phi * half_dy;
    double const y1p = -sin_phi * half_dx + cos_phi * half_dy;

    double lambda =
        (x1p * x1p) / (rx * rx) +
        (y1p * y1p) / (ry * ry);
    if (!finite(lambda))
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    if (lambda > 1.0) {
        double const scale = std::sqrt(lambda);
        rx *= scale;
        ry *= scale;
    }

    double const rx2 = rx * rx;
    double const ry2 = ry * ry;
    double const x1p2 = x1p * x1p;
    double const y1p2 = y1p * y1p;
    double const denominator =
        rx2 * y1p2 + ry2 * x1p2;
    if (!finite(denominator) || denominator <= 0.0) {
        result->push_back(line_to(x1, y1));
        return;
    }
    double numerator =
        rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2;
    if (!finite(numerator))
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    numerator = std::max(0.0, numerator);
    double coefficient = std::sqrt(numerator / denominator);
    if (large_arc == sweep)
        coefficient = -coefficient;

    double const cxp = coefficient * (rx * y1p / ry);
    double const cyp = coefficient * (-ry * x1p / rx);
    double const cx =
        cos_phi * cxp - sin_phi * cyp + (x0 + x1) / 2.0;
    double const cy =
        sin_phi * cxp + cos_phi * cyp + (y0 + y1) / 2.0;

    double const ux = (x1p - cxp) / rx;
    double const uy = (y1p - cyp) / ry;
    double const vx = (-x1p - cxp) / rx;
    double const vy = (-y1p - cyp) / ry;
    double theta = vector_angle(1.0, 0.0, ux, uy);
    double delta = vector_angle(ux, uy, vx, vy);
    if (!sweep && delta > 0.0)
        delta -= 2.0 * k_pi;
    else if (sweep && delta < 0.0)
        delta += 2.0 * k_pi;

    int const segments = std::max(
        1,
        static_cast<int>(
            std::ceil(std::fabs(delta) / (k_pi / 2.0))));
    double const step = delta / static_cast<double>(segments);

    auto point_on_arc = [&](double angle) {
        quantapdf_point p;
        p.x = static_cast<float>(
            cx + cos_phi * rx * std::cos(angle) -
            sin_phi * ry * std::sin(angle));
        p.y = static_cast<float>(
            cy + sin_phi * rx * std::cos(angle) +
            cos_phi * ry * std::sin(angle));
        if (!finite(p.x) || !finite(p.y))
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        return p;
    };
    auto derivative = [&](double angle) {
        quantapdf_point p;
        p.x = static_cast<float>(
            -cos_phi * rx * std::sin(angle) -
            sin_phi * ry * std::cos(angle));
        p.y = static_cast<float>(
            -sin_phi * rx * std::sin(angle) +
            cos_phi * ry * std::cos(angle));
        if (!finite(p.x) || !finite(p.y))
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        return p;
    };

    for (int segment = 0; segment < segments; ++segment) {
        double const t1 = theta + step * static_cast<double>(segment);
        double const t2 = t1 + step;
        double const alpha =
            (4.0 / 3.0) * std::tan((t2 - t1) / 4.0);
        auto const p1 = point_on_arc(t1);
        auto p2 = point_on_arc(t2);
        auto const d1 = derivative(t1);
        auto const d2 = derivative(t2);
        if (segment + 1 == segments)
            p2 = point(x1, y1);
        result->push_back(cubic_to(
            static_cast<double>(p1.x) + alpha * d1.x,
            static_cast<double>(p1.y) + alpha * d1.y,
            static_cast<double>(p2.x) - alpha * d2.x,
            static_cast<double>(p2.y) - alpha * d2.y,
            p2.x,
            p2.y));
    }
}

std::vector<quantapdf_composer_path_command> parse_path_data(
    std::string const& data)
{
    number_scanner scanner(data);
    std::vector<quantapdf_composer_path_command> result;
    char command = 0;
    double x = 0.0;
    double y = 0.0;
    double start_x = 0.0;
    double start_y = 0.0;
    double cubic_x = 0.0;
    double cubic_y = 0.0;
    double quad_x = 0.0;
    double quad_y = 0.0;
    bool have_current = false;
    bool previous_cubic = false;
    bool previous_quad = false;

    auto absolute = [&](double value, double current, bool relative) {
        return relative ? current + value : value;
    };

    while (!scanner.done()) {
        if (scanner.next_is_alpha()) {
            command = scanner.take_alpha();
        } else if (command == 0) {
            fail(QUANTAPDF_ERROR_FORMAT);
        }

        bool const relative =
            command >= 'a' && command <= 'z';
        char const upper = static_cast<char>(
            std::toupper(static_cast<unsigned char>(command)));

        if (upper == 'Z') {
            if (!have_current)
                fail(QUANTAPDF_ERROR_FORMAT);
            result.push_back(close_path());
            x = start_x;
            y = start_y;
            previous_cubic = false;
            previous_quad = false;
            command = 0;
            continue;
        }

        size_t groups = 0u;
        while (scanner.next_is_number()) {
            if (upper == 'M' || upper == 'L' || upper == 'T') {
                double const vx = scanner.number();
                double const vy = scanner.number();
                double const nx = absolute(vx, x, relative);
                double const ny = absolute(vy, y, relative);

                if (upper == 'M' && groups == 0u) {
                    result.push_back(move_to(nx, ny));
                    start_x = nx;
                    start_y = ny;
                    have_current = true;
                    previous_quad = false;
                } else if (upper == 'T') {
                    if (!have_current)
                        fail(QUANTAPDF_ERROR_FORMAT);
                    double const qx =
                        previous_quad ? 2.0 * x - quad_x : x;
                    double const qy =
                        previous_quad ? 2.0 * y - quad_y : y;
                    double const c1x = x + (2.0 / 3.0) * (qx - x);
                    double const c1y = y + (2.0 / 3.0) * (qy - y);
                    double const c2x = nx + (2.0 / 3.0) * (qx - nx);
                    double const c2y = ny + (2.0 / 3.0) * (qy - ny);
                    result.push_back(cubic_to(
                        c1x, c1y, c2x, c2y, nx, ny));
                    quad_x = qx;
                    quad_y = qy;
                    previous_quad = true;
                    previous_cubic = false;
                } else {
                    if (!have_current)
                        fail(QUANTAPDF_ERROR_FORMAT);
                    result.push_back(line_to(nx, ny));
                    previous_quad = false;
                }
                x = nx;
                y = ny;
                previous_cubic = false;
            } else if (upper == 'H') {
                if (!have_current)
                    fail(QUANTAPDF_ERROR_FORMAT);
                double const nx =
                    absolute(scanner.number(), x, relative);
                result.push_back(line_to(nx, y));
                x = nx;
                previous_cubic = false;
                previous_quad = false;
            } else if (upper == 'V') {
                if (!have_current)
                    fail(QUANTAPDF_ERROR_FORMAT);
                double const ny =
                    absolute(scanner.number(), y, relative);
                result.push_back(line_to(x, ny));
                y = ny;
                previous_cubic = false;
                previous_quad = false;
            } else if (upper == 'C') {
                if (!have_current)
                    fail(QUANTAPDF_ERROR_FORMAT);
                double const x1 =
                    absolute(scanner.number(), x, relative);
                double const y1 =
                    absolute(scanner.number(), y, relative);
                double const x2 =
                    absolute(scanner.number(), x, relative);
                double const y2 =
                    absolute(scanner.number(), y, relative);
                double const nx =
                    absolute(scanner.number(), x, relative);
                double const ny =
                    absolute(scanner.number(), y, relative);
                result.push_back(cubic_to(
                    x1, y1, x2, y2, nx, ny));
                x = nx;
                y = ny;
                cubic_x = x2;
                cubic_y = y2;
                previous_cubic = true;
                previous_quad = false;
            } else if (upper == 'S') {
                if (!have_current)
                    fail(QUANTAPDF_ERROR_FORMAT);
                double const x1 =
                    previous_cubic ? 2.0 * x - cubic_x : x;
                double const y1 =
                    previous_cubic ? 2.0 * y - cubic_y : y;
                double const x2 =
                    absolute(scanner.number(), x, relative);
                double const y2 =
                    absolute(scanner.number(), y, relative);
                double const nx =
                    absolute(scanner.number(), x, relative);
                double const ny =
                    absolute(scanner.number(), y, relative);
                result.push_back(cubic_to(
                    x1, y1, x2, y2, nx, ny));
                x = nx;
                y = ny;
                cubic_x = x2;
                cubic_y = y2;
                previous_cubic = true;
                previous_quad = false;
            } else if (upper == 'Q') {
                if (!have_current)
                    fail(QUANTAPDF_ERROR_FORMAT);
                double const qx =
                    absolute(scanner.number(), x, relative);
                double const qy =
                    absolute(scanner.number(), y, relative);
                double const nx =
                    absolute(scanner.number(), x, relative);
                double const ny =
                    absolute(scanner.number(), y, relative);
                double const c1x = x + (2.0 / 3.0) * (qx - x);
                double const c1y = y + (2.0 / 3.0) * (qy - y);
                double const c2x = nx + (2.0 / 3.0) * (qx - nx);
                double const c2y = ny + (2.0 / 3.0) * (qy - ny);
                result.push_back(cubic_to(
                    c1x, c1y, c2x, c2y, nx, ny));
                x = nx;
                y = ny;
                quad_x = qx;
                quad_y = qy;
                previous_quad = true;
                previous_cubic = false;
            } else if (upper == 'A') {
                if (!have_current)
                    fail(QUANTAPDF_ERROR_FORMAT);
                double const rx = scanner.number();
                double const ry = scanner.number();
                double const rotation = scanner.number();
                double const large_value = scanner.number();
                double const sweep_value = scanner.number();
                if ((large_value != 0.0 && large_value != 1.0) ||
                    (sweep_value != 0.0 && sweep_value != 1.0))
                    fail(QUANTAPDF_ERROR_FORMAT);
                double const nx =
                    absolute(scanner.number(), x, relative);
                double const ny =
                    absolute(scanner.number(), y, relative);
                append_arc(
                    &result,
                    x,
                    y,
                    rx,
                    ry,
                    rotation,
                    large_value == 1.0,
                    sweep_value == 1.0,
                    nx,
                    ny);
                x = nx;
                y = ny;
                previous_quad = false;
                previous_cubic = false;
            } else {
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            }
            ++groups;
            if (scanner.next_is_alpha())
                break;
        }
        if (groups == 0u)
            fail(QUANTAPDF_ERROR_FORMAT);
        if (upper == 'M')
            command = relative ? 'l' : 'L';
        if (!scanner.done() &&
            !scanner.next_is_alpha() &&
            !scanner.next_is_number())
            fail(QUANTAPDF_ERROR_FORMAT);
    }

    if (result.empty())
        fail(QUANTAPDF_ERROR_FORMAT);
    return result;
}

std::vector<quantapdf_composer_path_command> rectangle(
    double x,
    double y,
    double width,
    double height)
{
    if (width <= 0.0 || height <= 0.0)
        fail(QUANTAPDF_ERROR_FORMAT);
    return {
        move_to(x, y),
        line_to(x + width, y),
        line_to(x + width, y + height),
        line_to(x, y + height),
        close_path()};
}

std::vector<quantapdf_composer_path_command> ellipse(
    double cx,
    double cy,
    double rx,
    double ry)
{
    if (rx <= 0.0 || ry <= 0.0)
        fail(QUANTAPDF_ERROR_FORMAT);
    double const kx = rx * k_circle_kappa;
    double const ky = ry * k_circle_kappa;
    return {
        move_to(cx + rx, cy),
        cubic_to(
            cx + rx, cy + ky,
            cx + kx, cy + ry,
            cx, cy + ry),
        cubic_to(
            cx - kx, cy + ry,
            cx - rx, cy + ky,
            cx - rx, cy),
        cubic_to(
            cx - rx, cy - ky,
            cx - kx, cy - ry,
            cx, cy - ry),
        cubic_to(
            cx + kx, cy - ry,
            cx + rx, cy - ky,
            cx + rx, cy),
        close_path()};
}

std::vector<quantapdf_composer_path_command> points_path(
    std::string const& value,
    bool close)
{
    auto const values = number_list(value);
    if ((values.size() & 1u) != 0u ||
        values.size() < (close ? 6u : 4u))
        fail(QUANTAPDF_ERROR_FORMAT);
    std::vector<quantapdf_composer_path_command> result;
    result.push_back(move_to(values[0], values[1]));
    for (size_t i = 2u; i < values.size(); i += 2u)
        result.push_back(line_to(values[i], values[i + 1u]));
    if (close)
        result.push_back(close_path());
    return result;
}

std::vector<quantapdf_composer_path_command> shape_commands(
    element const& item,
    std::string const& tag,
    bool* out_force_no_fill)
{
    *out_force_no_fill = false;
    if (tag == "path") {
        auto const* data = find_attribute(item, "d");
        if (data == nullptr)
            fail(QUANTAPDF_ERROR_FORMAT);
        return parse_path_data(*data);
    }
    if (tag == "rect") {
        return rectangle(
            optional_number(item, "x", 0.0),
            optional_number(item, "y", 0.0),
            required_number(item, "width"),
            required_number(item, "height"));
    }
    if (tag == "line") {
        *out_force_no_fill = true;
        return {
            move_to(
                optional_number(item, "x1", 0.0),
                optional_number(item, "y1", 0.0)),
            line_to(
                optional_number(item, "x2", 0.0),
                optional_number(item, "y2", 0.0))};
    }
    if (tag == "polyline" || tag == "polygon") {
        auto const* points = find_attribute(item, "points");
        if (points == nullptr)
            fail(QUANTAPDF_ERROR_FORMAT);
        if (tag == "polyline")
            *out_force_no_fill = true;
        return points_path(*points, tag == "polygon");
    }
    if (tag == "circle") {
        double const radius = required_number(item, "r");
        return ellipse(
            optional_number(item, "cx", 0.0),
            optional_number(item, "cy", 0.0),
            radius,
            radius);
    }
    if (tag == "ellipse") {
        return ellipse(
            optional_number(item, "cx", 0.0),
            optional_number(item, "cy", 0.0),
            required_number(item, "rx"),
            required_number(item, "ry"));
    }
    fail(QUANTAPDF_ERROR_UNSUPPORTED);
}

bool drawable_tag(std::string const& tag)
{
    return tag == "path" || tag == "rect" || tag == "line" ||
        tag == "polyline" || tag == "polygon" ||
        tag == "circle" || tag == "ellipse";
}

enum class resource_units {
    unspecified,
    user_space,
    object_bbox
};

struct gradient_definition {
    bool radial = false;
    resource_units units = resource_units::unspecified;
    bool transform_specified = false;
    matrix transform;
    std::string template_ref;
    std::string x1_text;
    std::string y1_text;
    std::string x2_text;
    std::string y2_text;
    std::string cx_text;
    std::string cy_text;
    std::string radius_text;
    std::string fx_text;
    std::string fy_text;
    std::string fr_text;
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    double radius = 0.0;
    double fx = 0.0;
    double fy = 0.0;
    double fr = 0.0;
    bool normalized = false;
    std::vector<quantapdf_composer_gradient_stop> stops;
};

struct clip_definition {
    std::vector<quantapdf_composer_path_command> commands;
    quantapdf_composer_fill_rule fill_rule =
        QUANTAPDF_COMPOSER_FILL_NONZERO;
    resource_units units = resource_units::user_space;
};

struct symbol_definition {
    double min_x = 0.0;
    double min_y = 0.0;
    double width = 0.0;
    double height = 0.0;
    preserve_aspect preserve;
    std::vector<element> tokens;
    std::set<std::string> dependencies;
};

struct pattern_definition {
    resource_units units = resource_units::unspecified;
    resource_units content_units = resource_units::unspecified;
    bool transform_specified = false;
    matrix transform;
    std::string template_ref;
    std::string x_text;
    std::string y_text;
    std::string width_text;
    std::string height_text;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    bool normalized = false;
    std::vector<element> tokens;
    std::set<std::string> pattern_dependencies;
};

struct definition_table {
    std::map<std::string, gradient_definition> gradients;
    std::map<std::string, clip_definition> clips;
    std::map<std::string, symbol_definition> symbols;
    std::map<std::string, pattern_definition> patterns;
    std::vector<element> defs_tokens;
    std::set<std::string> ids;
};

std::string local_fragment_href(std::string const& value)
{
    std::string const text = trim(value);
    if (text.size() < 2u || text[0] != '#')
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    std::string const id = text.substr(1u);
    if (id.empty())
        fail(QUANTAPDF_ERROR_FORMAT);
    for (char ch: id) {
        if (!name_char(ch))
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }
    return id;
}

void validate_use_attributes(element const& item)
{
    for (auto const& attr: item.attributes) {
        if (attr.name != "id" && attr.name != "href" &&
            attr.name != "x" && attr.name != "y" &&
            attr.name != "width" && attr.name != "height" &&
            attr.name != "transform")
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }
    auto const* href = find_attribute(item, "href");
    if (href == nullptr)
        fail(QUANTAPDF_ERROR_FORMAT);
    (void)local_fragment_href(*href);
}

void validate_symbol_attributes(element const& item)
{
    for (auto const& attr: item.attributes) {
        if (attr.name != "id" && attr.name != "viewBox" &&
            attr.name != "preserveAspectRatio")
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }
}

resource_units parse_resource_units(std::string const& value);
std::string local_template_reference(element const& item);
std::string attribute_text(element const& item, char const* name);

void validate_pattern_attributes(element const& item)
{
    for (auto const& attr: item.attributes) {
        if (attr.name != "id" && attr.name != "href" &&
            attr.name != "xlink:href" &&
            attr.name != "patternUnits" &&
            attr.name != "patternContentUnits" &&
            attr.name != "patternTransform" &&
            attr.name != "x" && attr.name != "y" &&
            attr.name != "width" && attr.name != "height")
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }
}

pattern_definition start_pattern_definition(element const& item)
{
    validate_pattern_attributes(item);
    pattern_definition result;
    result.template_ref = local_template_reference(item);
    if (auto const* units = find_attribute(item, "patternUnits"))
        result.units = parse_resource_units(*units);
    if (auto const* units = find_attribute(item, "patternContentUnits"))
        result.content_units = parse_resource_units(*units);
    if (auto const* transform = find_attribute(item, "patternTransform")) {
        result.transform = parse_transform(*transform);
        result.transform_specified = true;
    }
    result.x_text = attribute_text(item, "x");
    result.y_text = attribute_text(item, "y");
    result.width_text = attribute_text(item, "width");
    result.height_text = attribute_text(item, "height");
    return result;
}

std::string local_paint_reference(std::string const& value)
{
    std::string const parsed = trim(value);
    if (parsed.size() < 6u ||
        lower_ascii(parsed.substr(0u, 5u)) != "url(#" ||
        parsed.back() != ')')
        return {};
    std::string const id =
        trim(parsed.substr(5u, parsed.size() - 6u));
    if (id.empty())
        fail(QUANTAPDF_ERROR_FORMAT);
    for (char ch: id) {
        if (!name_char(ch))
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }
    return id;
}

void collect_pattern_paint_dependencies(
    pattern_definition* pattern,
    element const& item)
{
    auto inspect = [&](std::string const& value) {
        std::string const id = local_paint_reference(value);
        if (!id.empty())
            pattern->pattern_dependencies.insert(id);
    };
    if (auto const* fill = find_attribute(item, "fill"))
        inspect(*fill);
    if (auto const* stroke = find_attribute(item, "stroke"))
        inspect(*stroke);
    if (auto const* style = find_attribute(item, "style")) {
        size_t position = 0u;
        while (position < style->size()) {
            size_t const semi = style->find(';', position);
            size_t const end =
                semi == std::string::npos ? style->size() : semi;
            std::string const entry = trim(
                std::string_view(*style).substr(position, end - position));
            if (!entry.empty()) {
                size_t const colon = entry.find(':');
                if (colon == std::string::npos)
                    fail(QUANTAPDF_ERROR_FORMAT);
                std::string const name =
                    lower_ascii(trim(entry.substr(0u, colon)));
                if (name == "fill" || name == "stroke")
                    inspect(trim(entry.substr(colon + 1u)));
            }
            if (semi == std::string::npos)
                break;
            position = semi + 1u;
        }
    }
}

std::string serialize_element(
    element const& item,
    bool strip_id)
{
    std::string result;
    if (item.closing)
        return "</" + item.name + ">";

    result += "<";
    result += item.name;
    for (auto const& attr: item.attributes) {
        if (strip_id && attr.name == "id")
            continue;
        if (attr.value.find('&') != std::string::npos ||
            attr.value.find('<') != std::string::npos)
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        char quote = '"';
        if (attr.value.find('"') != std::string::npos) {
            if (attr.value.find('\'') != std::string::npos)
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            quote = '\'';
        }
        result += " ";
        result += attr.name;
        result += "=";
        result.push_back(quote);
        result += attr.value;
        result.push_back(quote);
    }
    result += item.self_closing ? "/>" : ">";
    return result;
}

std::string serialize_tokens(
    std::vector<element> const& tokens,
    bool strip_id)
{
    std::string result;
    for (auto const& token: tokens)
        result += serialize_element(token, strip_id);
    return result;
}

std::string required_id(element const& item)
{
    auto const* id = find_attribute(item, "id");
    if (id == nullptr || id->empty())
        fail(QUANTAPDF_ERROR_FORMAT);
    for (char ch: *id) {
        if (!name_char(ch))
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }
    return *id;
}

void register_id(
    definition_table* definitions,
    element const& item)
{
    auto const* id = find_attribute(item, "id");
    if (id == nullptr)
        return;
    if (id->empty())
        fail(QUANTAPDF_ERROR_FORMAT);
    for (char ch: *id) {
        if (!name_char(ch))
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }
    if (!definitions->ids.insert(*id).second)
        fail(QUANTAPDF_ERROR_FORMAT);
}

double stop_offset_value(std::string const& value)
{
    std::string text = trim(value);
    bool percentage = false;
    if (!text.empty() && text.back() == '%') {
        percentage = true;
        text.pop_back();
    }
    if (text.empty())
        fail(QUANTAPDF_ERROR_FORMAT);
    number_scanner scanner(text);
    double result = scanner.number();
    if (!scanner.done())
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    if (percentage)
        result /= 100.0;
    if (!finite(result) || result < 0.0 || result > 1.0)
        fail(QUANTAPDF_ERROR_FORMAT);
    return result;
}

resource_units parse_resource_units(
    std::string const& value)
{
    std::string const parsed = trim(value);
    if (parsed == "userSpaceOnUse")
        return resource_units::user_space;
    if (parsed == "objectBoundingBox")
        return resource_units::object_bbox;
    fail(QUANTAPDF_ERROR_UNSUPPORTED);
}

std::string local_template_reference(element const& item)
{
    auto const* href = find_attribute(item, "href");
    auto const* xlink = find_attribute(item, "xlink:href");
    if (href != nullptr && xlink != nullptr)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    if (href != nullptr)
        return local_fragment_href(*href);
    if (xlink != nullptr)
        return local_fragment_href(*xlink);
    return {};
}

std::string attribute_text(
    element const& item,
    char const* name)
{
    auto const* value = find_attribute(item, name);
    return value == nullptr ? std::string{} : trim(*value);
}

void validate_gradient_attributes(
    element const& item,
    bool radial)
{
    for (auto const& attr: item.attributes) {
        bool allowed =
            attr.name == "id" ||
            attr.name == "href" ||
            attr.name == "xlink:href" ||
            attr.name == "gradientUnits" ||
            attr.name == "gradientTransform" ||
            attr.name == "spreadMethod";
        if (radial) {
            allowed = allowed || attr.name == "cx" || attr.name == "cy" ||
                attr.name == "r" || attr.name == "fx" ||
                attr.name == "fy" || attr.name == "fr";
        } else {
            allowed = allowed || attr.name == "x1" || attr.name == "y1" ||
                attr.name == "x2" || attr.name == "y2";
        }
        if (!allowed)
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }
    if (auto const* spread = find_attribute(item, "spreadMethod")) {
        if (trim(*spread) != "pad")
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }
}

gradient_definition start_gradient_definition(
    element const& item,
    bool radial)
{
    validate_gradient_attributes(item, radial);
    gradient_definition result;
    result.radial = radial;
    result.template_ref = local_template_reference(item);
    if (auto const* units = find_attribute(item, "gradientUnits"))
        result.units = parse_resource_units(*units);
    if (auto const* transform = find_attribute(item, "gradientTransform")) {
        result.transform = parse_transform(*transform);
        result.transform_specified = true;
    }

    if (!radial) {
        result.x1_text = attribute_text(item, "x1");
        result.y1_text = attribute_text(item, "y1");
        result.x2_text = attribute_text(item, "x2");
        result.y2_text = attribute_text(item, "y2");
    } else {
        result.cx_text = attribute_text(item, "cx");
        result.cy_text = attribute_text(item, "cy");
        result.radius_text = attribute_text(item, "r");
        result.fx_text = attribute_text(item, "fx");
        result.fy_text = attribute_text(item, "fy");
        result.fr_text = attribute_text(item, "fr");
    }
    return result;
}

void append_gradient_stop(
    gradient_definition* gradient,
    element const& item)
{
    for (auto const& attr: item.attributes) {
        if (attr.name != "id" && attr.name != "offset" &&
            attr.name != "stop-color" && attr.name != "stop-opacity" &&
            attr.name != "style")
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }

    auto const* offset_text = find_attribute(item, "offset");
    if (offset_text == nullptr)
        fail(QUANTAPDF_ERROR_FORMAT);
    double const offset = stop_offset_value(*offset_text);

    std::string color_text = "black";
    double stop_opacity = 1.0;
    if (auto const* color = find_attribute(item, "stop-color"))
        color_text = *color;
    if (auto const* opacity = find_attribute(item, "stop-opacity"))
        stop_opacity = opacity_value(*opacity);

    if (auto const* style = find_attribute(item, "style")) {
        size_t position = 0u;
        while (position < style->size()) {
            size_t const semi = style->find(';', position);
            size_t const end =
                semi == std::string::npos ? style->size() : semi;
            std::string const entry = trim(
                std::string_view(*style).substr(position, end - position));
            if (!entry.empty()) {
                size_t const colon = entry.find(':');
                if (colon == std::string::npos)
                    fail(QUANTAPDF_ERROR_FORMAT);
                std::string const name =
                    lower_ascii(trim(entry.substr(0u, colon)));
                std::string const value =
                    trim(entry.substr(colon + 1u));
                if (name == "stop-color")
                    color_text = value;
                else if (name == "stop-opacity")
                    stop_opacity = opacity_value(value);
                else
                    fail(QUANTAPDF_ERROR_UNSUPPORTED);
            }
            if (semi == std::string::npos)
                break;
            position = semi + 1u;
        }
    }

    if (stop_opacity != 1.0)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    bool enabled = false;
    uint32_t const color = parse_color(color_text, &enabled);
    if (!enabled)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    if (!gradient->stops.empty() &&
        offset <= gradient->stops.back().offset)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    if (gradient->stops.size() >=
        QUANTAPDF_COMPOSER_MAX_GRADIENT_STOPS)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    gradient->stops.push_back(
        {static_cast<float>(offset), color});
}

void finalize_gradient(gradient_definition* gradient)
{
    if (gradient->stops.empty())
        fail(QUANTAPDF_ERROR_FORMAT);
    if (gradient->stops.size() == 1u) {
        uint32_t const color = gradient->stops[0].argb;
        gradient->stops.clear();
        gradient->stops.push_back({0.0f, color});
        gradient->stops.push_back({1.0f, color});
        return;
    }
    bool const need_start = gradient->stops.front().offset != 0.0f;
    bool const need_end = gradient->stops.back().offset != 1.0f;
    size_t const final_count =
        gradient->stops.size() +
        (need_start ? 1u : 0u) +
        (need_end ? 1u : 0u);
    if (final_count > QUANTAPDF_COMPOSER_MAX_GRADIENT_STOPS)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    if (need_start) {
        gradient->stops.insert(
            gradient->stops.begin(),
            {0.0f, gradient->stops.front().argb});
    }
    if (need_end) {
        gradient->stops.push_back(
            {1.0f, gradient->stops.back().argb});
    }
}

double resource_length(
    std::string const& text,
    resource_units units,
    double default_value,
    bool required_user_space)
{
    if (text.empty()) {
        if (units == resource_units::user_space && required_user_space)
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        return default_value;
    }

    std::string value = trim(text);
    if (units == resource_units::object_bbox) {
        bool percentage = false;
        if (!value.empty() && value.back() == '%') {
            percentage = true;
            value.pop_back();
        }
        if (value.empty())
            fail(QUANTAPDF_ERROR_FORMAT);
        number_scanner scanner(value);
        double result = scanner.number();
        if (!scanner.done())
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        if (percentage)
            result /= 100.0;
        if (!finite(result))
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        return result;
    }

    if (!value.empty() && value.back() == '%')
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    return scalar(value);
}

void normalize_gradient_definition(
    definition_table* definitions,
    std::string const& id,
    std::set<std::string>* visiting)
{
    auto found = definitions->gradients.find(id);
    if (found == definitions->gradients.end())
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    if (found->second.normalized)
        return;
    if (!visiting->insert(id).second)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);

    gradient_definition local = found->second;
    gradient_definition merged;
    merged.radial = local.radial;

    if (!local.template_ref.empty()) {
        auto base = definitions->gradients.find(local.template_ref);
        if (base == definitions->gradients.end() ||
            base->second.radial != local.radial)
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        normalize_gradient_definition(
            definitions, local.template_ref, visiting);
        merged = definitions->gradients.at(local.template_ref);
        merged.radial = local.radial;
        merged.normalized = false;
        merged.template_ref.clear();
    }

    if (local.units != resource_units::unspecified)
        merged.units = local.units;
    if (merged.units == resource_units::unspecified)
        merged.units = resource_units::object_bbox;

    if (local.transform_specified) {
        merged.transform = local.transform;
        merged.transform_specified = true;
    }

    auto override_text = [](std::string& target, std::string const& source) {
        if (!source.empty())
            target = source;
    };
    override_text(merged.x1_text, local.x1_text);
    override_text(merged.y1_text, local.y1_text);
    override_text(merged.x2_text, local.x2_text);
    override_text(merged.y2_text, local.y2_text);
    override_text(merged.cx_text, local.cx_text);
    override_text(merged.cy_text, local.cy_text);
    override_text(merged.radius_text, local.radius_text);
    override_text(merged.fx_text, local.fx_text);
    override_text(merged.fy_text, local.fy_text);
    override_text(merged.fr_text, local.fr_text);

    if (!local.stops.empty())
        merged.stops = local.stops;
    if (merged.stops.empty())
        fail(QUANTAPDF_ERROR_FORMAT);

    if (!merged.radial) {
        merged.x1 = resource_length(
            merged.x1_text, merged.units, 0.0, true);
        merged.y1 = resource_length(
            merged.y1_text, merged.units, 0.0, true);
        merged.x2 = resource_length(
            merged.x2_text, merged.units, 1.0, true);
        merged.y2 = resource_length(
            merged.y2_text, merged.units, 0.0, true);
        if (merged.x1 == merged.x2 && merged.y1 == merged.y2)
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    } else {
        merged.cx = resource_length(
            merged.cx_text, merged.units, 0.5, true);
        merged.cy = resource_length(
            merged.cy_text, merged.units, 0.5, true);
        merged.radius = resource_length(
            merged.radius_text, merged.units, 0.5, true);
        merged.fx = resource_length(
            merged.fx_text, merged.units, merged.cx, false);
        merged.fy = resource_length(
            merged.fy_text, merged.units, merged.cy, false);
        merged.fr = resource_length(
            merged.fr_text, merged.units, 0.0, false);
        if (merged.radius <= 0.0 || merged.fr < 0.0)
            fail(QUANTAPDF_ERROR_FORMAT);
        if (merged.fr > merged.radius)
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        if (merged.fr == merged.radius &&
            merged.fx == merged.cx && merged.fy == merged.cy)
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }

    finalize_gradient(&merged);
    merged.template_ref.clear();
    merged.normalized = true;
    found->second = std::move(merged);
    visiting->erase(id);
}

void normalize_pattern_definition(
    definition_table* definitions,
    std::string const& id,
    std::set<std::string>* visiting)
{
    auto found = definitions->patterns.find(id);
    if (found == definitions->patterns.end())
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    if (found->second.normalized)
        return;
    if (!visiting->insert(id).second)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);

    pattern_definition local = found->second;
    pattern_definition merged;

    if (!local.template_ref.empty()) {
        auto base = definitions->patterns.find(local.template_ref);
        if (base == definitions->patterns.end())
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        normalize_pattern_definition(
            definitions, local.template_ref, visiting);
        merged = definitions->patterns.at(local.template_ref);
        merged.normalized = false;
        merged.template_ref.clear();
    }

    if (local.units != resource_units::unspecified)
        merged.units = local.units;
    if (merged.units == resource_units::unspecified)
        merged.units = resource_units::object_bbox;

    if (local.content_units != resource_units::unspecified)
        merged.content_units = local.content_units;
    if (merged.content_units == resource_units::unspecified)
        merged.content_units = resource_units::user_space;

    if (local.transform_specified) {
        merged.transform = local.transform;
        merged.transform_specified = true;
    }

    auto override_text = [](std::string& target, std::string const& source) {
        if (!source.empty())
            target = source;
    };
    override_text(merged.x_text, local.x_text);
    override_text(merged.y_text, local.y_text);
    override_text(merged.width_text, local.width_text);
    override_text(merged.height_text, local.height_text);

    if (!local.tokens.empty()) {
        merged.tokens = local.tokens;
        merged.pattern_dependencies = local.pattern_dependencies;
    }

    merged.x = resource_length(
        merged.x_text, merged.units, 0.0, false);
    merged.y = resource_length(
        merged.y_text, merged.units, 0.0, false);
    merged.width = resource_length(
        merged.width_text, merged.units, 0.0, false);
    merged.height = resource_length(
        merged.height_text, merged.units, 0.0, false);
    if (merged.width <= 0.0 || merged.height <= 0.0)
        fail(QUANTAPDF_ERROR_FORMAT);
    if (merged.tokens.empty())
        fail(QUANTAPDF_ERROR_FORMAT);

    merged.template_ref.clear();
    merged.normalized = true;
    found->second = std::move(merged);
    visiting->erase(id);
}

void normalize_resource_definitions(definition_table* definitions)
{
    std::set<std::string> visiting;
    std::vector<std::string> gradient_ids;
    std::vector<std::string> pattern_ids;
    gradient_ids.reserve(definitions->gradients.size());
    pattern_ids.reserve(definitions->patterns.size());

    for (auto const& entry: definitions->gradients)
        gradient_ids.push_back(entry.first);
    for (auto const& entry: definitions->patterns)
        pattern_ids.push_back(entry.first);

    for (auto const& id: gradient_ids)
        normalize_gradient_definition(definitions, id, &visiting);
    visiting.clear();
    for (auto const& id: pattern_ids)
        normalize_pattern_definition(definitions, id, &visiting);
}

quantapdf_composer_fill_rule clip_rule_value(
    std::string const* value,
    quantapdf_composer_fill_rule fallback)
{
    if (value == nullptr)
        return fallback;
    std::string const parsed = lower_ascii(trim(*value));
    if (parsed == "nonzero")
        return QUANTAPDF_COMPOSER_FILL_NONZERO;
    if (parsed == "evenodd")
        return QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    fail(QUANTAPDF_ERROR_UNSUPPORTED);
}

struct clip_context {
    matrix transform;
    quantapdf_composer_fill_rule fill_rule =
        QUANTAPDF_COMPOSER_FILL_NONZERO;
};

quantapdf_composer_fill_rule clip_rule_from_element(
    element const& item,
    quantapdf_composer_fill_rule fallback)
{
    quantapdf_composer_fill_rule result =
        clip_rule_value(find_attribute(item, "clip-rule"), fallback);
    if (auto const* style = find_attribute(item, "style")) {
        size_t position = 0u;
        while (position < style->size()) {
            size_t const semi = style->find(';', position);
            size_t const end =
                semi == std::string::npos ? style->size() : semi;
            std::string const entry = trim(
                std::string_view(*style).substr(position, end - position));
            if (!entry.empty()) {
                size_t const colon = entry.find(':');
                if (colon == std::string::npos)
                    fail(QUANTAPDF_ERROR_FORMAT);
                std::string const name =
                    lower_ascii(trim(entry.substr(0u, colon)));
                if (name != "clip-rule")
                    fail(QUANTAPDF_ERROR_UNSUPPORTED);
                std::string const value =
                    trim(entry.substr(colon + 1u));
                result = clip_rule_value(&value, result);
            }
            if (semi == std::string::npos)
                break;
            position = semi + 1u;
        }
    }
    return result;
}

bool clip_geometry_attribute_allowed(
    std::string const& tag,
    std::string const& name)
{
    if (name == "id" || name == "transform" ||
        name == "clip-rule" || name == "style")
        return true;
    if (tag == "path")
        return name == "d";
    if (tag == "rect")
        return name == "x" || name == "y" ||
            name == "width" || name == "height";
    if (tag == "line")
        return name == "x1" || name == "y1" ||
            name == "x2" || name == "y2";
    if (tag == "polyline" || tag == "polygon")
        return name == "points";
    if (tag == "circle")
        return name == "cx" || name == "cy" || name == "r";
    if (tag == "ellipse")
        return name == "cx" || name == "cy" ||
            name == "rx" || name == "ry";
    return false;
}

void validate_clip_geometry_attributes(
    element const& item,
    std::string const& tag)
{
    for (auto const& attr: item.attributes) {
        if (!clip_geometry_attribute_allowed(tag, attr.name))
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }
}

struct definition_frame {
    enum class kind {
        normal,
        defs,
        gradient,
        clip,
        clip_group,
        symbol,
        symbol_child,
        pattern,
        pattern_child,
        leaf
    };

    std::string name;
    kind type = kind::normal;
    std::string id;
    matrix transform;
    quantapdf_composer_fill_rule fill_rule =
        QUANTAPDF_COMPOSER_FILL_NONZERO;
};

bool stack_contains_defs(std::vector<definition_frame> const& stack)
{
    return std::any_of(
        stack.begin(),
        stack.end(),
        [](definition_frame const& frame) {
            return frame.type == definition_frame::kind::defs;
        });
}

std::string active_symbol_id(
    std::vector<definition_frame> const& stack)
{
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
        if (it->type == definition_frame::kind::symbol ||
            it->type == definition_frame::kind::symbol_child)
            return it->id;
    }
    return {};
}

std::string active_pattern_id(
    std::vector<definition_frame> const& stack)
{
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
        if (it->type == definition_frame::kind::pattern ||
            it->type == definition_frame::kind::pattern_child)
            return it->id;
    }
    return {};
}

void validate_symbol_dependencies(definition_table const& definitions)
{
    enum class visit_state {
        unseen,
        visiting,
        done
    };
    std::map<std::string, visit_state> states;

    std::function<void(std::string const&)> visit =
        [&](std::string const& id) {
            auto symbol = definitions.symbols.find(id);
            if (symbol == definitions.symbols.end())
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            auto& state = states[id];
            if (state == visit_state::done)
                return;
            if (state == visit_state::visiting)
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            state = visit_state::visiting;
            for (auto const& dependency: symbol->second.dependencies) {
                if (definitions.symbols.find(dependency) ==
                    definitions.symbols.end())
                    fail(QUANTAPDF_ERROR_UNSUPPORTED);
                visit(dependency);
            }
            state = visit_state::done;
        };

    for (auto const& item: definitions.symbols)
        visit(item.first);
}

void validate_pattern_dependencies(definition_table const& definitions)
{
    enum class visit_state {
        unseen,
        visiting,
        done
    };
    std::map<std::string, visit_state> states;

    std::function<void(std::string const&)> visit =
        [&](std::string const& id) {
            auto pattern = definitions.patterns.find(id);
            if (pattern == definitions.patterns.end())
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            auto& state = states[id];
            if (state == visit_state::done)
                return;
            if (state == visit_state::visiting)
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            state = visit_state::visiting;
            for (auto const& dependency:
                 pattern->second.pattern_dependencies) {
                auto nested = definitions.patterns.find(dependency);
                if (nested != definitions.patterns.end()) {
                    visit(dependency);
                    continue;
                }
                if (definitions.gradients.find(dependency) !=
                    definitions.gradients.end())
                    continue;
                // fill/stroke URLs may only reference a paint resource.
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            }
            state = visit_state::done;
        };

    for (auto const& item: definitions.patterns)
        visit(item.first);
}

definition_table parse_definitions(
    unsigned char const* data,
    size_t size)
{
    xml_scanner scanner(data, size);
    definition_table definitions;
    std::vector<definition_frame> stack;
    element item;
    bool root_seen = false;

    auto push_if_needed = [&](definition_frame frame, bool self_closing) {
        if (!self_closing)
            stack.push_back(std::move(frame));
    };

    while (scanner.next(&item)) {
        std::string const tag = local_name(item.name);
        if (item.closing) {
            if (stack.empty() || stack.back().name != tag)
                fail(QUANTAPDF_ERROR_FORMAT);

            bool const in_defs = stack_contains_defs(stack);
            std::string const symbol_id = active_symbol_id(stack);
            std::string const pattern_id = active_pattern_id(stack);
            if (in_defs &&
                stack.back().type != definition_frame::kind::defs)
                definitions.defs_tokens.push_back(item);
            if (!symbol_id.empty() &&
                stack.back().type != definition_frame::kind::symbol) {
                auto symbol = definitions.symbols.find(symbol_id);
                if (symbol == definitions.symbols.end())
                    fail(QUANTAPDF_ERROR_BACKEND);
                symbol->second.tokens.push_back(item);
            }
            if (!pattern_id.empty() &&
                stack.back().type != definition_frame::kind::pattern) {
                auto pattern = definitions.patterns.find(pattern_id);
                if (pattern == definitions.patterns.end())
                    fail(QUANTAPDF_ERROR_BACKEND);
                pattern->second.tokens.push_back(item);
            }

            definition_frame frame = std::move(stack.back());
            stack.pop_back();
            if (frame.type == definition_frame::kind::gradient) {
                auto found = definitions.gradients.find(frame.id);
                if (found == definitions.gradients.end())
                    fail(QUANTAPDF_ERROR_BACKEND);
            } else if (frame.type == definition_frame::kind::clip) {
                auto found = definitions.clips.find(frame.id);
                if (found == definitions.clips.end())
                    fail(QUANTAPDF_ERROR_BACKEND);
                if (found->second.commands.empty())
                    fail(QUANTAPDF_ERROR_FORMAT);
            }
            continue;
        }

        if (stack.size() >= k_svg_max_depth)
            fail(QUANTAPDF_ERROR_UNSUPPORTED);

        register_id(&definitions, item);

        if (stack.empty()) {
            if (root_seen || tag != "svg")
                fail(QUANTAPDF_ERROR_FORMAT);
            root_seen = true;
            definition_frame root;
            root.name = tag;
            root.type = definition_frame::kind::normal;
            push_if_needed(std::move(root), item.self_closing);
            continue;
        }

        bool const in_defs = stack_contains_defs(stack);
        std::string const symbol_id = active_symbol_id(stack);
        std::string const pattern_id = active_pattern_id(stack);
        if (in_defs)
            definitions.defs_tokens.push_back(item);
        if (!symbol_id.empty()) {
            auto symbol = definitions.symbols.find(symbol_id);
            if (symbol == definitions.symbols.end())
                fail(QUANTAPDF_ERROR_BACKEND);
            symbol->second.tokens.push_back(item);
        }
        if (!pattern_id.empty()) {
            auto pattern = definitions.patterns.find(pattern_id);
            if (pattern == definitions.patterns.end())
                fail(QUANTAPDF_ERROR_BACKEND);
            pattern->second.tokens.push_back(item);
        }

        definition_frame const& parent = stack.back();

        if (parent.type == definition_frame::kind::normal) {
            if (parent.name == "svg" && tag == "defs") {
                for (auto const& attr: item.attributes) {
                    if (attr.name != "id")
                        fail(QUANTAPDF_ERROR_UNSUPPORTED);
                }
                definition_frame frame;
                frame.name = tag;
                frame.type = definition_frame::kind::defs;
                push_if_needed(std::move(frame), item.self_closing);
            } else {
                definition_frame frame;
                frame.name = tag;
                frame.type = definition_frame::kind::normal;
                push_if_needed(std::move(frame), item.self_closing);
            }
            continue;
        }

        if (parent.type == definition_frame::kind::defs) {
            if (tag == "linearGradient" || tag == "radialGradient") {
                std::string const id = required_id(item);
                gradient_definition gradient =
                    start_gradient_definition(
                        item, tag == "radialGradient");
                auto inserted = definitions.gradients.emplace(
                    id, std::move(gradient));
                if (!inserted.second)
                    fail(QUANTAPDF_ERROR_FORMAT);
                definition_frame frame;
                frame.name = tag;
                frame.type = definition_frame::kind::gradient;
                frame.id = id;
                if (!item.self_closing)
                    stack.push_back(std::move(frame));
                continue;
            }

            if (tag == "clipPath") {
                for (auto const& attr: item.attributes) {
                    if (attr.name != "id" &&
                        attr.name != "clipPathUnits" &&
                        attr.name != "transform" &&
                        attr.name != "clip-rule" &&
                        attr.name != "style")
                        fail(QUANTAPDF_ERROR_UNSUPPORTED);
                }
                auto const* units =
                    find_attribute(item, "clipPathUnits");
                std::string const id = required_id(item);
                clip_definition clip;
                if (units != nullptr)
                    clip.units = parse_resource_units(*units);
                clip.fill_rule = clip_rule_from_element(
                    item, QUANTAPDF_COMPOSER_FILL_NONZERO);
                auto inserted =
                    definitions.clips.emplace(id, std::move(clip));
                if (!inserted.second)
                    fail(QUANTAPDF_ERROR_FORMAT);
                definition_frame frame;
                frame.name = tag;
                frame.type = definition_frame::kind::clip;
                frame.id = id;
                frame.fill_rule = inserted.first->second.fill_rule;
                if (auto const* transform =
                        find_attribute(item, "transform"))
                    frame.transform = parse_transform(*transform);
                if (item.self_closing)
                    fail(QUANTAPDF_ERROR_FORMAT);
                stack.push_back(std::move(frame));
                continue;
            }

            if (tag == "symbol") {
                validate_symbol_attributes(item);
                std::string const id = required_id(item);
                auto const* view_box_text = find_attribute(item, "viewBox");
                if (view_box_text == nullptr)
                    fail(QUANTAPDF_ERROR_UNSUPPORTED);
                auto const view_box = number_list(*view_box_text);
                if (view_box.size() != 4u ||
                    view_box[2] <= 0.0 || view_box[3] <= 0.0)
                    fail(QUANTAPDF_ERROR_FORMAT);

                symbol_definition symbol;
                symbol.min_x = view_box[0];
                symbol.min_y = view_box[1];
                symbol.width = view_box[2];
                symbol.height = view_box[3];
                symbol.preserve = parse_preserve_aspect(
                    find_attribute(item, "preserveAspectRatio"));
                auto inserted =
                    definitions.symbols.emplace(id, std::move(symbol));
                if (!inserted.second)
                    fail(QUANTAPDF_ERROR_FORMAT);

                definition_frame frame;
                frame.name = tag;
                frame.type = definition_frame::kind::symbol;
                frame.id = id;
                push_if_needed(std::move(frame), item.self_closing);
                continue;
            }

            if (tag == "pattern") {
                std::string const id = required_id(item);
                pattern_definition pattern =
                    start_pattern_definition(item);
                auto inserted =
                    definitions.patterns.emplace(id, std::move(pattern));
                if (!inserted.second)
                    fail(QUANTAPDF_ERROR_FORMAT);

                definition_frame frame;
                frame.name = tag;
                frame.type = definition_frame::kind::pattern;
                frame.id = id;
                push_if_needed(std::move(frame), item.self_closing);
                continue;
            }

            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        }

        if (parent.type == definition_frame::kind::pattern ||
            parent.type == definition_frame::kind::pattern_child) {
            std::string const current_pattern = active_pattern_id(stack);
            if (current_pattern.empty())
                fail(QUANTAPDF_ERROR_BACKEND);
            auto pattern = definitions.patterns.find(current_pattern);
            if (pattern == definitions.patterns.end())
                fail(QUANTAPDF_ERROR_BACKEND);

            if (tag == "g") {
                validate_attributes(item, tag, false);
            } else if (drawable_tag(tag)) {
                validate_attributes(item, tag, false);
            } else {
                // V3A deliberately excludes use/symbol recursion inside tiles.
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            }
            collect_pattern_paint_dependencies(
                &pattern->second, item);

            definition_frame frame;
            frame.name = tag;
            frame.type = definition_frame::kind::pattern_child;
            frame.id = current_pattern;
            push_if_needed(std::move(frame), item.self_closing);
            continue;
        }

        if (parent.type == definition_frame::kind::symbol ||
            parent.type == definition_frame::kind::symbol_child) {
            std::string const current_symbol = active_symbol_id(stack);
            if (current_symbol.empty())
                fail(QUANTAPDF_ERROR_BACKEND);
            auto symbol = definitions.symbols.find(current_symbol);
            if (symbol == definitions.symbols.end())
                fail(QUANTAPDF_ERROR_BACKEND);

            if (tag == "use") {
                validate_use_attributes(item);
                auto const* href = find_attribute(item, "href");
                if (href == nullptr)
                    fail(QUANTAPDF_ERROR_FORMAT);
                symbol->second.dependencies.insert(
                    local_fragment_href(*href));
            } else if (tag == "g") {
                validate_attributes(item, tag, false);
            } else if (drawable_tag(tag)) {
                validate_attributes(item, tag, false);
            } else {
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            }

            definition_frame frame;
            frame.name = tag;
            frame.type = definition_frame::kind::symbol_child;
            frame.id = current_symbol;
            push_if_needed(std::move(frame), item.self_closing);
            continue;
        }

        if (parent.type == definition_frame::kind::gradient) {
            if (tag != "stop")
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            auto found = definitions.gradients.find(parent.id);
            if (found == definitions.gradients.end())
                fail(QUANTAPDF_ERROR_BACKEND);
            append_gradient_stop(&found->second, item);
            definition_frame frame;
            frame.name = tag;
            frame.type = definition_frame::kind::leaf;
            push_if_needed(std::move(frame), item.self_closing);
            continue;
        }

        if (parent.type == definition_frame::kind::clip ||
            parent.type == definition_frame::kind::clip_group) {
            std::string clip_id = parent.id;
            if (parent.type == definition_frame::kind::clip_group) {
                for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                    if (it->type == definition_frame::kind::clip) {
                        clip_id = it->id;
                        break;
                    }
                }
            }
            auto found = definitions.clips.find(clip_id);
            if (found == definitions.clips.end())
                fail(QUANTAPDF_ERROR_BACKEND);

            if (tag == "g") {
                for (auto const& attr: item.attributes) {
                    if (attr.name != "id" &&
                        attr.name != "transform" &&
                        attr.name != "clip-rule" &&
                        attr.name != "style")
                        fail(QUANTAPDF_ERROR_UNSUPPORTED);
                }
                definition_frame frame;
                frame.name = tag;
                frame.type = definition_frame::kind::clip_group;
                frame.id = clip_id;
                frame.transform = parent.transform;
                if (auto const* transform =
                        find_attribute(item, "transform")) {
                    frame.transform = multiply(
                        frame.transform, parse_transform(*transform));
                }
                frame.fill_rule =
                    clip_rule_from_element(item, parent.fill_rule);
                push_if_needed(std::move(frame), item.self_closing);
                continue;
            }

            if (!drawable_tag(tag))
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            validate_clip_geometry_attributes(item, tag);
            bool force_no_fill = false;
            auto commands = shape_commands(
                item, tag, &force_no_fill);
            (void)force_no_fill;
            matrix transform = parent.transform;
            if (auto const* local_transform =
                    find_attribute(item, "transform")) {
                transform = multiply(
                    transform, parse_transform(*local_transform));
            }
            transform_commands(&commands, transform);
            quantapdf_composer_fill_rule const rule =
                clip_rule_from_element(item, parent.fill_rule);
            if (!found->second.commands.empty() &&
                found->second.fill_rule != rule)
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            found->second.fill_rule = rule;
            found->second.commands.insert(
                found->second.commands.end(),
                commands.begin(),
                commands.end());
            definition_frame frame;
            frame.name = tag;
            frame.type = definition_frame::kind::leaf;
            push_if_needed(std::move(frame), item.self_closing);
            continue;
        }

        if (parent.type == definition_frame::kind::leaf)
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    }

    scanner.finish();
    if (!root_seen || !stack.empty())
        fail(QUANTAPDF_ERROR_FORMAT);
    normalize_resource_definitions(&definitions);
    validate_symbol_dependencies(definitions);
    validate_pattern_dependencies(definitions);
    return definitions;
}

void validate_style_references(
    paint_style const& style,
    definition_table const& definitions)
{
    auto paint_exists = [&](std::string const& id) {
        return definitions.gradients.find(id) !=
                definitions.gradients.end() ||
            definitions.patterns.find(id) !=
                definitions.patterns.end();
    };
    if (!style.fill_ref.empty() && !paint_exists(style.fill_ref))
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    if (!style.stroke_ref.empty() && !paint_exists(style.stroke_ref))
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    if (!style.clip_ref.empty() &&
        definitions.clips.find(style.clip_ref) ==
            definitions.clips.end())
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
}

struct context {
    std::string name;
    paint_style style;
    matrix transform;
    std::vector<clip_component> active_clips;
    bool leaf = false;
    bool skip = false;
};

quantapdf_affine_transform resource_affine(matrix const& value)
{
    double const determinant =
        value.a * value.d - value.b * value.c;
    if (!finite(value.a) || !finite(value.b) ||
        !finite(value.c) || !finite(value.d) ||
        !finite(value.e) || !finite(value.f) ||
        !finite(determinant) || determinant == 0.0)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);

    auto component = [](double item) -> float {
        if (!finite(item) ||
            item < -static_cast<double>(std::numeric_limits<float>::max()) ||
            item > static_cast<double>(std::numeric_limits<float>::max()))
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        float result = static_cast<float>(item);
        return result == 0.0f ? 0.0f : result;
    };

    return {
        component(value.a),
        component(value.b),
        component(value.c),
        component(value.d),
        component(value.e),
        component(value.f)};
}

matrix object_bbox_matrix(geometry_bounds const& bounds)
{
    if (!bounds.valid)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    double const width = bounds.x1 - bounds.x0;
    double const height = bounds.y1 - bounds.y0;
    if (!finite(width) || !finite(height) ||
        width <= 0.0 || height <= 0.0)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    return multiply(
        translate_matrix(bounds.x0, bounds.y0),
        scale_matrix(width, height));
}

matrix resource_units_matrix(
    resource_units units,
    geometry_bounds const& bounds)
{
    if (units == resource_units::user_space)
        return matrix{};
    if (units == resource_units::object_bbox)
        return object_bbox_matrix(bounds);
    fail(QUANTAPDF_ERROR_BACKEND);
}


quantapdf_status register_gradient_paint(
    quantapdf_composer* composer,
    gradient_definition const& gradient,
    matrix const& user_transform,
    geometry_bounds const& local_bounds,
    quantapdf_composer_paint_id* out_paint_id)
{
    /*
     * PATH V4 keeps geometry local. Paint matrices therefore carry the full
     * referencing element transform. objectBoundingBox adds its local bbox
     * coordinate system before gradientTransform.
     */
    matrix const units =
        resource_units_matrix(gradient.units, local_bounds);
    matrix const composed =
        multiply(
            user_transform,
            multiply(units, gradient.transform));
    quantapdf_affine_transform const transform =
        resource_affine(composed);

    if (gradient.radial) {
        quantapdf_composer_radial_gradient_options options{};
        options.struct_size =
            QUANTAPDF_COMPOSER_RADIAL_GRADIENT_OPTIONS_V1_SIZE;
        options.start_center =
            point(gradient.fx, gradient.fy);
        options.start_radius = static_cast<float>(gradient.fr);
        options.end_center =
            point(gradient.cx, gradient.cy);
        options.end_radius = static_cast<float>(gradient.radius);
        options.transform = transform;
        options.stops = gradient.stops.data();
        options.stop_count = gradient.stops.size();
        return quantapdf_composer_add_radial_gradient(
            composer, &options, out_paint_id);
    }

    quantapdf_composer_linear_gradient_options options{};
    options.struct_size =
        QUANTAPDF_COMPOSER_LINEAR_GRADIENT_OPTIONS_V1_SIZE;
    options.start = point(gradient.x1, gradient.y1);
    options.end = point(gradient.x2, gradient.y2);
    options.transform = transform;
    options.stops = gradient.stops.data();
    options.stop_count = gradient.stops.size();
    return quantapdf_composer_add_linear_gradient(
        composer, &options, out_paint_id);
}

quantapdf_status register_clip_resource(
    quantapdf_composer* composer,
    clip_definition const& clip,
    matrix const& user_transform,
    geometry_bounds const& local_bounds,
    quantapdf_composer_clip_id* out_clip_id)
{
    quantapdf_composer_clip_options options{};
    options.struct_size = QUANTAPDF_COMPOSER_CLIP_OPTIONS_V1_SIZE;
    options.fill_rule = clip.fill_rule;
    matrix const units =
        resource_units_matrix(clip.units, local_bounds);
    options.transform = resource_affine(
        multiply(user_transform, units));
    return quantapdf_composer_add_clip_path(
        composer,
        clip.commands.data(),
        clip.commands.size(),
        &options,
        out_clip_id);
}

bool style_is_default_for_use(paint_style const& style)
{
    paint_style const defaults;
    return style.fill == defaults.fill &&
        style.stroke == defaults.stroke &&
        style.fill_argb == defaults.fill_argb &&
        style.stroke_argb == defaults.stroke_argb &&
        style.stroke_width == defaults.stroke_width &&
        style.fill_opacity == defaults.fill_opacity &&
        style.stroke_opacity == defaults.stroke_opacity &&
        style.opacity == defaults.opacity &&
        style.dash_array.empty() &&
        style.dash_offset == defaults.dash_offset &&
        style.fill_ref.empty() &&
        style.stroke_ref.empty() &&
        style.clip_ref.empty() &&
        style.fill_rule == defaults.fill_rule &&
        style.line_cap == defaults.line_cap &&
        style.line_join == defaults.line_join &&
        style.miter_limit == defaults.miter_limit;
}

staged_use parse_use(
    element const& item,
    paint_style const& parent_style,
    matrix const& parent_transform,
    definition_table const& definitions)
{
    if (!style_is_default_for_use(parent_style))
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    validate_use_attributes(item);
    auto const* href = find_attribute(item, "href");
    if (href == nullptr)
        fail(QUANTAPDF_ERROR_FORMAT);
    std::string const symbol_ref = local_fragment_href(*href);
    if (definitions.symbols.find(symbol_ref) == definitions.symbols.end())
        fail(QUANTAPDF_ERROR_UNSUPPORTED);

    staged_use result;
    result.symbol_ref = symbol_ref;
    result.x = optional_number(item, "x", 0.0);
    result.y = optional_number(item, "y", 0.0);
    result.width = required_number(item, "width");
    result.height = required_number(item, "height");
    if (result.width <= 0.0 || result.height <= 0.0)
        fail(QUANTAPDF_ERROR_FORMAT);
    result.user_transform =
        derive_transform(parent_transform, item);
    return result;
}

std::string svg_number(double value)
{
    if (!finite(value))
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(17) << value;
    return stream.str();
}

std::string build_symbol_svg(
    definition_table const& definitions,
    symbol_definition const& symbol)
{
    std::string result;
    result += "<svg viewBox=\"";
    result += svg_number(symbol.min_x);
    result += " ";
    result += svg_number(symbol.min_y);
    result += " ";
    result += svg_number(symbol.width);
    result += " ";
    result += svg_number(symbol.height);
    result += "\" preserveAspectRatio=\"none\"><defs>";
    result += serialize_tokens(definitions.defs_tokens, false);
    result += "</defs>";
    result += serialize_tokens(symbol.tokens, true);
    result += "</svg>";
    if (result.size() > k_svg_max_input_bytes)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    return result;
}

struct symbol_form_builder_context {
    std::string const* svg = nullptr;
    float width = 0.0f;
    float height = 0.0f;
};

quantapdf_status symbol_form_builder(
    quantapdf_composer* composer,
    size_t page_index,
    void* user_data)
{
    auto const* context =
        static_cast<symbol_form_builder_context const*>(user_data);
    if (context == nullptr || context->svg == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    quantapdf_composer_svg_options options{};
    options.struct_size = QUANTAPDF_COMPOSER_SVG_OPTIONS_V1_SIZE;
    quantapdf_rect bounds = {
        0.0f,
        0.0f,
        context->width,
        context->height};
    return quantapdf_composer_draw_svg(
        composer,
        page_index,
        reinterpret_cast<unsigned char const*>(context->svg->data()),
        context->svg->size(),
        &bounds,
        &options);
}

quantapdf_status materialize_symbol(
    quantapdf_composer* composer,
    definition_table const& definitions,
    std::string const& symbol_id,
    std::map<std::string, quantapdf_composer_form_id>* cache,
    quantapdf_composer_form_id* out_form_id)
{
    auto cached = cache->find(symbol_id);
    if (cached != cache->end()) {
        *out_form_id = cached->second;
        return QUANTAPDF_OK;
    }

    auto found = definitions.symbols.find(symbol_id);
    if (found == definitions.symbols.end())
        return QUANTAPDF_ERROR_UNSUPPORTED;
    auto const& symbol = found->second;

    std::string synthetic_svg = build_symbol_svg(definitions, symbol);
    symbol_form_builder_context context;
    context.svg = &synthetic_svg;
    context.width = static_cast<float>(symbol.width);
    context.height = static_cast<float>(symbol.height);
    if (!finite(context.width) || !finite(context.height) ||
        context.width <= 0.0f || context.height <= 0.0f)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    quantapdf_composer_form_options form_options{};
    form_options.struct_size = QUANTAPDF_COMPOSER_FORM_OPTIONS_V1_SIZE;
    form_options.width_points = context.width;
    form_options.height_points = context.height;

    quantapdf_composer_form_id form_id = 0u;
    quantapdf_status const status = quantapdf_composer_add_form(
        composer,
        &form_options,
        symbol_form_builder,
        &context,
        &form_id);
    if (status != QUANTAPDF_OK)
        return status;
    cache->emplace(symbol_id, form_id);
    *out_form_id = form_id;
    return QUANTAPDF_OK;
}

std::string build_pattern_svg(
    definition_table const& definitions,
    pattern_definition const& pattern,
    double view_x,
    double view_y,
    double view_width,
    double view_height)
{
    std::string result;
    result += "<svg viewBox=\"";
    result += svg_number(view_x);
    result += " ";
    result += svg_number(view_y);
    result += " ";
    result += svg_number(view_width);
    result += " ";
    result += svg_number(view_height);
    result += "\" preserveAspectRatio=\"none\"><defs>";
    result += serialize_tokens(definitions.defs_tokens, false);
    result += "</defs>";
    result += serialize_tokens(pattern.tokens, true);
    result += "</svg>";
    if (result.size() > k_svg_max_input_bytes)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    return result;
}

struct pattern_builder_context {
    std::string const* svg = nullptr;
    float width = 0.0f;
    float height = 0.0f;
};

quantapdf_status pattern_builder(
    quantapdf_composer* composer,
    size_t page_index,
    void* user_data)
{
    auto const* context =
        static_cast<pattern_builder_context const*>(user_data);
    if (context == nullptr || context->svg == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    quantapdf_composer_svg_options options{};
    options.struct_size = QUANTAPDF_COMPOSER_SVG_OPTIONS_V1_SIZE;
    quantapdf_rect bounds = {
        0.0f, 0.0f, context->width, context->height};
    return quantapdf_composer_draw_svg(
        composer,
        page_index,
        reinterpret_cast<unsigned char const*>(context->svg->data()),
        context->svg->size(),
        &bounds,
        &options);
}

quantapdf_status materialize_pattern(
    quantapdf_composer* composer,
    definition_table const& definitions,
    std::string const& pattern_id,
    matrix const& user_transform,
    geometry_bounds const& local_bounds,
    quantapdf_composer_paint_id* out_paint_id)
{
    auto found = definitions.patterns.find(pattern_id);
    if (found == definitions.patterns.end())
        return QUANTAPDF_ERROR_UNSUPPORTED;
    auto const& pattern = found->second;

    double view_x = pattern.x;
    double view_y = pattern.y;
    double view_width = pattern.width;
    double view_height = pattern.height;
    matrix units;

    try {
        units = resource_units_matrix(pattern.units, local_bounds);
        if (pattern.content_units != pattern.units) {
            if (!local_bounds.valid)
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            double const bbox_width =
                local_bounds.x1 - local_bounds.x0;
            double const bbox_height =
                local_bounds.y1 - local_bounds.y0;
            if (bbox_width <= 0.0 || bbox_height <= 0.0)
                fail(QUANTAPDF_ERROR_UNSUPPORTED);

            if (pattern.units == resource_units::object_bbox &&
                pattern.content_units == resource_units::user_space) {
                view_x =
                    local_bounds.x0 + pattern.x * bbox_width;
                view_y =
                    local_bounds.y0 + pattern.y * bbox_height;
                view_width = pattern.width * bbox_width;
                view_height = pattern.height * bbox_height;
            } else if (
                pattern.units == resource_units::user_space &&
                pattern.content_units == resource_units::object_bbox) {
                view_x =
                    (pattern.x - local_bounds.x0) / bbox_width;
                view_y =
                    (pattern.y - local_bounds.y0) / bbox_height;
                view_width = pattern.width / bbox_width;
                view_height = pattern.height / bbox_height;
            } else {
                fail(QUANTAPDF_ERROR_BACKEND);
            }
        }
    } catch (svg_error const& error) {
        return error.status;
    }

    std::string synthetic_svg;
    try {
        synthetic_svg = build_pattern_svg(
            definitions,
            pattern,
            view_x,
            view_y,
            view_width,
            view_height);
    } catch (svg_error const& error) {
        return error.status;
    }

    pattern_builder_context context;
    context.svg = &synthetic_svg;
    context.width = static_cast<float>(pattern.width);
    context.height = static_cast<float>(pattern.height);
    if (!finite(context.width) || !finite(context.height) ||
        context.width <= 0.0f || context.height <= 0.0f)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    matrix const placement =
        multiply(
            user_transform,
            multiply(
                units,
                multiply(
                    pattern.transform,
                    translate_matrix(pattern.x, pattern.y))));

    quantapdf_composer_tiling_pattern_options options{};
    options.struct_size =
        QUANTAPDF_COMPOSER_TILING_PATTERN_OPTIONS_V1_SIZE;
    options.width_points = context.width;
    options.height_points = context.height;
    options.x_step = context.width;
    options.y_step = context.height;
    try {
        options.transform = resource_affine(placement);
    } catch (svg_error const& error) {
        return error.status;
    }

    quantapdf_composer_paint_id paint_id = 0u;
    quantapdf_status const status =
        quantapdf_composer_add_tiling_pattern(
            composer,
            &options,
            pattern_builder,
            &context,
            &paint_id);
    if (status != QUANTAPDF_OK)
        return status;
    *out_paint_id = paint_id;
    return QUANTAPDF_OK;
}

std::string svg_color(uint32_t argb)
{
    char buffer[8];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "#%02x%02x%02x",
        static_cast<unsigned int>((argb >> 16u) & 0xffu),
        static_cast<unsigned int>((argb >> 8u) & 0xffu),
        static_cast<unsigned int>(argb & 0xffu));
    return buffer;
}

char const* svg_line_cap(quantapdf_composer_line_cap value)
{
    switch (value) {
    case QUANTAPDF_COMPOSER_LINE_CAP_BUTT:
        return "butt";
    case QUANTAPDF_COMPOSER_LINE_CAP_ROUND:
        return "round";
    case QUANTAPDF_COMPOSER_LINE_CAP_SQUARE:
        return "square";
    }
    fail(QUANTAPDF_ERROR_BACKEND);
}

char const* svg_line_join(quantapdf_composer_line_join value)
{
    switch (value) {
    case QUANTAPDF_COMPOSER_LINE_JOIN_MITER:
        return "miter";
    case QUANTAPDF_COMPOSER_LINE_JOIN_ROUND:
        return "round";
    case QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL:
        return "bevel";
    }
    fail(QUANTAPDF_ERROR_BACKEND);
}

std::string resolved_style_attributes(paint_style const& style)
{
    std::string result;
    result += " fill=\"";
    if (!style.fill)
        result += "none";
    else if (!style.fill_ref.empty())
        result += "url(#" + style.fill_ref + ")";
    else
        result += svg_color(style.fill_argb);
    result += "\" stroke=\"";
    if (!style.stroke)
        result += "none";
    else if (!style.stroke_ref.empty())
        result += "url(#" + style.stroke_ref + ")";
    else
        result += svg_color(style.stroke_argb);
    result += "\" fill-rule=\"";
    result += style.fill_rule == QUANTAPDF_COMPOSER_FILL_EVEN_ODD
        ? "evenodd"
        : "nonzero";
    result += "\" fill-opacity=\"";
    result += svg_number(style.fill_opacity);
    result += "\" stroke-opacity=\"";
    result += svg_number(style.stroke_opacity);
    result += "\" stroke-width=\"";
    result += svg_number(style.stroke_width);
    result += "\" stroke-linecap=\"";
    result += svg_line_cap(style.line_cap);
    result += "\" stroke-linejoin=\"";
    result += svg_line_join(style.line_join);
    result += "\" stroke-miterlimit=\"";
    result += svg_number(style.miter_limit);
    result += "\"";
    if (!style.dash_array.empty()) {
        result += " stroke-dasharray=\"";
        for (size_t i = 0u; i < style.dash_array.size(); ++i) {
            if (i != 0u)
                result += " ";
            result += svg_number(style.dash_array[i]);
        }
        result += "\" stroke-dashoffset=\"";
        result += svg_number(style.dash_offset);
        result += "\"";
    }
    return result;
}

std::string svg_matrix_attribute(matrix const& transform)
{
    std::string result = "matrix(";
    result += svg_number(transform.a);
    result += " ";
    result += svg_number(transform.b);
    result += " ";
    result += svg_number(transform.c);
    result += " ";
    result += svg_number(transform.d);
    result += " ";
    result += svg_number(transform.e);
    result += " ";
    result += svg_number(transform.f);
    result += ")";
    return result;
}

std::string build_opacity_group_svg(
    definition_table const& definitions,
    quantapdf_rect const& bounds,
    paint_style const& style,
    matrix const& transform,
    std::vector<element> const& tokens)
{
    std::string result;
    result += "<svg viewBox=\"";
    result += svg_number(bounds.x0);
    result += " ";
    result += svg_number(bounds.y0);
    result += " ";
    result += svg_number(bounds.x1 - bounds.x0);
    result += " ";
    result += svg_number(bounds.y1 - bounds.y0);
    result += "\" preserveAspectRatio=\"none\"><defs>";
    result += serialize_tokens(definitions.defs_tokens, false);
    result += "</defs><g";
    result += resolved_style_attributes(style);
    result += " transform=\"";
    result += svg_matrix_attribute(transform);
    result += "\">";
    result += serialize_tokens(tokens, false);
    result += "</g></svg>";
    if (result.size() > k_svg_max_input_bytes)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    return result;
}

class svg_parser {
  public:
    svg_parser(
        unsigned char const* data,
        size_t size,
        quantapdf_rect const& bounds,
        size_t max_paths,
        definition_table const& definitions):
        scanner_(data, size),
        bounds_(bounds),
        max_paths_(max_paths),
        definitions_(definitions)
    {
    }

    std::vector<staged_path> parse()
    {
        element item;
        bool root_seen = false;

        while (scanner_.next(&item)) {
            std::string const tag = local_name(item.name);
            if (item.closing) {
                if (stack_.empty() ||
                    stack_.back().name != tag)
                    fail(QUANTAPDF_ERROR_FORMAT);
                stack_.pop_back();
                continue;
            }

            if (stack_.size() >= k_svg_max_depth)
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            if (!stack_.empty() && stack_.back().leaf)
                fail(QUANTAPDF_ERROR_UNSUPPORTED);

            if (stack_.empty()) {
                if (root_seen || tag != "svg")
                    fail(QUANTAPDF_ERROR_FORMAT);
                root_seen = true;
                validate_attributes(item, tag, true);
                auto const* view_box = find_attribute(item, "viewBox");
                if (view_box == nullptr)
                    fail(QUANTAPDF_ERROR_UNSUPPORTED);
                auto const values = number_list(*view_box);
                if (values.size() != 4u ||
                    values[2] <= 0.0 || values[3] <= 0.0)
                    fail(QUANTAPDF_ERROR_FORMAT);

                preserve_aspect const preserve =
                    parse_preserve_aspect(
                        find_attribute(item, "preserveAspectRatio"));
                clip_to_bounds_ = preserve.slice;
                matrix const viewport = viewport_matrix(
                    bounds_, values, preserve);

                context root;
                root.name = tag;
                root.style = derive_style(paint_style{}, item);
                validate_style_references(root.style, definitions_);
                if (!root.style.clip_ref.empty())
                    fail(QUANTAPDF_ERROR_UNSUPPORTED);
                root.transform = derive_transform(viewport, item);
                if (root.style.opacity != 1.0) {
                    if (!item.self_closing) {
                        auto tokens = capture_children(tag, true);
                        stage_opacity_group(
                            root.style,
                            root.transform,
                            std::move(tokens));
                    }
                } else if (!item.self_closing) {
                    stack_.push_back(std::move(root));
                }
                continue;
            }

            context const& parent = stack_.back();

            if (parent.skip) {
                if (!item.self_closing) {
                    context skipped;
                    skipped.name = tag;
                    skipped.skip = true;
                    stack_.push_back(std::move(skipped));
                }
                continue;
            }

            if (tag == "svg")
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            if (tag == "defs") {
                if (parent.name != "svg")
                    fail(QUANTAPDF_ERROR_UNSUPPORTED);
                if (!item.self_closing) {
                    context skipped;
                    skipped.name = tag;
                    skipped.skip = true;
                    stack_.push_back(std::move(skipped));
                }
                continue;
            }
            if (tag == "use") {
                if (paths_.size() + uses_.size() + groups_.size() >=
                    max_paths_)
                    fail(QUANTAPDF_ERROR_UNSUPPORTED);
                staged_use use =
                    parse_use(
                        item,
                        parent.style,
                        parent.transform,
                        definitions_);
                use.order = next_order_++;
                uses_.push_back(std::move(use));
                if (!item.self_closing) {
                    context leaf;
                    leaf.name = tag;
                    leaf.leaf = true;
                    stack_.push_back(std::move(leaf));
                }
                continue;
            }
            if (tag == "g") {
                validate_attributes(item, tag, false);
                context group;
                group.name = tag;
                group.style = derive_style(parent.style, item);
                validate_style_references(group.style, definitions_);
                if (!group.style.clip_ref.empty())
                    fail(QUANTAPDF_ERROR_UNSUPPORTED);
                group.transform =
                    derive_transform(parent.transform, item);
                if (group.style.opacity != 1.0) {
                    if (!item.self_closing) {
                        auto tokens = capture_children(tag, false);
                        stage_opacity_group(
                            group.style,
                            group.transform,
                            std::move(tokens));
                    }
                } else if (!item.self_closing) {
                    stack_.push_back(std::move(group));
                }
                continue;
            }

            if (tag != "path" && tag != "rect" && tag != "line" &&
                tag != "polyline" && tag != "polygon" &&
                tag != "circle" && tag != "ellipse")
                fail(QUANTAPDF_ERROR_UNSUPPORTED);

            validate_attributes(item, tag, false);
            paint_style const style =
                derive_style(parent.style, item);
            validate_style_references(style, definitions_);
            matrix const transform =
                derive_transform(parent.transform, item);
            std::vector<quantapdf_composer_path_command> commands;

            if (tag == "path") {
                auto const* data = find_attribute(item, "d");
                if (data == nullptr)
                    fail(QUANTAPDF_ERROR_FORMAT);
                commands = parse_path_data(*data);
            } else if (tag == "rect") {
                commands = rectangle(
                    optional_number(item, "x", 0.0),
                    optional_number(item, "y", 0.0),
                    required_number(item, "width"),
                    required_number(item, "height"));
            } else if (tag == "line") {
                commands = {
                    move_to(
                        optional_number(item, "x1", 0.0),
                        optional_number(item, "y1", 0.0)),
                    line_to(
                        optional_number(item, "x2", 0.0),
                        optional_number(item, "y2", 0.0))};
                paint_style line_style = style;
                line_style.fill = false;
                stage_path(
                    &paths_,
                    std::move(commands),
                    line_style,
                    transform,
                    next_order_++,
                    max_paths_);
                if (!item.self_closing) {
                    context leaf{
                        tag, line_style, transform, true};
                    stack_.push_back(std::move(leaf));
                }
                continue;
            } else if (tag == "polyline" || tag == "polygon") {
                auto const* points = find_attribute(item, "points");
                if (points == nullptr)
                    fail(QUANTAPDF_ERROR_FORMAT);
                commands = points_path(
                    *points, tag == "polygon");
            } else if (tag == "circle") {
                commands = ellipse(
                    optional_number(item, "cx", 0.0),
                    optional_number(item, "cy", 0.0),
                    required_number(item, "r"),
                    required_number(item, "r"));
            } else if (tag == "ellipse") {
                commands = ellipse(
                    optional_number(item, "cx", 0.0),
                    optional_number(item, "cy", 0.0),
                    required_number(item, "rx"),
                    required_number(item, "ry"));
            }

            stage_path(
                &paths_,
                std::move(commands),
                style,
                transform,
                next_order_++,
                max_paths_);
            if (!item.self_closing) {
                context leaf{tag, style, transform, true};
                stack_.push_back(std::move(leaf));
            }
        }

        scanner_.finish();
        if (!root_seen || !stack_.empty())
            fail(QUANTAPDF_ERROR_FORMAT);
        return std::move(paths_);
    }

    bool clip_to_bounds() const
    {
        return clip_to_bounds_;
    }

    std::vector<staged_use> take_uses()
    {
        return std::move(uses_);
    }

    std::vector<staged_group> take_groups()
    {
        return std::move(groups_);
    }

  private:
    xml_scanner scanner_;
    quantapdf_rect bounds_;
    size_t max_paths_;
    bool clip_to_bounds_ = false;
    definition_table const& definitions_;
    size_t next_order_ = 0u;
    std::vector<context> stack_;
    std::vector<staged_path> paths_;
    std::vector<staged_use> uses_;
    std::vector<staged_group> groups_;

    std::vector<element> capture_children(
        std::string const& root_tag,
        bool skip_root_defs)
    {
        std::vector<std::string> open_tags{root_tag};
        std::vector<element> tokens;
        size_t skip_depth = 0u;
        element item;

        while (scanner_.next(&item)) {
            std::string const tag = local_name(item.name);
            if (item.closing) {
                if (open_tags.empty() || open_tags.back() != tag)
                    fail(QUANTAPDF_ERROR_FORMAT);
                bool const skipping = skip_depth != 0u;
                open_tags.pop_back();
                if (skip_depth != 0u)
                    --skip_depth;
                if (open_tags.empty())
                    return tokens;
                if (!skipping)
                    tokens.push_back(std::move(item));
                continue;
            }

            if (open_tags.size() >= k_svg_max_depth)
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            bool const start_defs_skip =
                skip_root_defs &&
                skip_depth == 0u &&
                open_tags.size() == 1u &&
                tag == "defs";
            bool const skipping =
                skip_depth != 0u || start_defs_skip;
            if (!skipping)
                tokens.push_back(item);
            if (!item.self_closing) {
                open_tags.push_back(tag);
                if (skipping)
                    ++skip_depth;
            }
        }
        fail(QUANTAPDF_ERROR_FORMAT);
    }

    void stage_opacity_group(
        paint_style style,
        matrix const& transform,
        std::vector<element> tokens)
    {
        if (paths_.size() + uses_.size() + groups_.size() >=
            max_paths_)
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        if (style.opacity < 0.0 || style.opacity > 1.0 ||
            !finite(style.opacity))
            fail(QUANTAPDF_ERROR_UNSUPPORTED);

        staged_group group;
        group.order = next_order_++;
        group.opacity = static_cast<float>(style.opacity);
        style.opacity = 1.0;
        group.svg = build_opacity_group_svg(
            definitions_,
            bounds_,
            style,
            transform,
            tokens);
        groups_.push_back(std::move(group));
    }
};

matrix use_viewport_matrix(
    staged_use const& use,
    symbol_definition const& symbol)
{
    double const sx = use.width / symbol.width;
    double const sy = use.height / symbol.height;
    if (!finite(sx) || !finite(sy) || sx <= 0.0 || sy <= 0.0)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);

    if (symbol.preserve.none) {
        return multiply(
            translate_matrix(use.x, use.y),
            scale_matrix(sx, sy));
    }

    double const scale =
        symbol.preserve.slice ? std::max(sx, sy) : std::min(sx, sy);
    double const used_width = symbol.width * scale;
    double const used_height = symbol.height * scale;
    double const tx =
        use.x + symbol.preserve.align_x * (use.width - used_width);
    double const ty =
        use.y + symbol.preserve.align_y * (use.height - used_height);
    return multiply(
        translate_matrix(tx, ty),
        scale_matrix(scale, scale));
}

quantapdf_status register_use_clip(
    quantapdf_composer* composer,
    staged_use const& use,
    quantapdf_composer_clip_id* out_clip_id)
{
    auto commands = rectangle(
        use.x, use.y, use.width, use.height);
    quantapdf_composer_clip_options options{};
    options.struct_size = QUANTAPDF_COMPOSER_CLIP_OPTIONS_V1_SIZE;
    options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    options.transform = resource_affine(use.user_transform);
    return quantapdf_composer_add_clip_path(
        composer,
        commands.data(),
        commands.size(),
        &options,
        out_clip_id);
}

quantapdf_status register_bounds_clip(
    quantapdf_composer* composer,
    quantapdf_rect const& bounds,
    quantapdf_composer_clip_id* out_clip_id)
{
    auto commands = rectangle(
        bounds.x0,
        bounds.y0,
        bounds.x1 - bounds.x0,
        bounds.y1 - bounds.y0);
    quantapdf_composer_clip_options options{};
    options.struct_size = QUANTAPDF_COMPOSER_CLIP_OPTIONS_V1_SIZE;
    options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    return quantapdf_composer_add_clip_path(
        composer,
        commands.data(),
        commands.size(),
        &options,
        out_clip_id);
}

quantapdf_status publish_uses(
    quantapdf_composer* composer,
    size_t page_index,
    std::vector<staged_use> const& uses,
    quantapdf_rect const& bounds,
    bool clip_to_bounds,
    definition_table const& definitions)
{
    if (uses.empty())
        return QUANTAPDF_OK;

    quantapdf_status const reserve =
        quantapdf_composer_reserve_operations_internal(
            composer, uses.size());
    if (reserve != QUANTAPDF_OK)
        return reserve;

    std::map<std::string, quantapdf_composer_form_id> form_cache;

    quantapdf_composer_clip_id viewport_clip_id = 0u;
    if (clip_to_bounds) {
        quantapdf_status const status =
            register_bounds_clip(
                composer, bounds, &viewport_clip_id);
        if (status != QUANTAPDF_OK)
            return status;
    }

    for (auto const& use: uses) {
        auto found = definitions.symbols.find(use.symbol_ref);
        if (found == definitions.symbols.end())
            return QUANTAPDF_ERROR_UNSUPPORTED;
        auto const& symbol = found->second;

        if (clip_to_bounds && symbol.preserve.slice)
            return QUANTAPDF_ERROR_UNSUPPORTED;

        quantapdf_composer_form_id form_id = 0u;
        quantapdf_status status = materialize_symbol(
            composer,
            definitions,
            use.symbol_ref,
            &form_cache,
            &form_id);
        if (status != QUANTAPDF_OK)
            return status;

        quantapdf_composer_clip_id effective_clip_id =
            viewport_clip_id;
        if (symbol.preserve.slice) {
            status = register_use_clip(
                composer, use, &effective_clip_id);
            if (status != QUANTAPDF_OK)
                return status;
        }

        quantapdf_composer_graphics_state_id state_id = 0u;
        if (effective_clip_id != 0u) {
            quantapdf_composer_graphics_state_options state{};
            state.struct_size =
                QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V2_SIZE;
            state.fill_alpha = 1.0f;
            state.stroke_alpha = 1.0f;
            state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
            state.clip_id = effective_clip_id;
            status = quantapdf_composer_add_graphics_state(
                composer, &state, &state_id);
            if (status != QUANTAPDF_OK)
                return status;
        }

        matrix const viewport =
            use_viewport_matrix(use, symbol);
        matrix const placement =
            multiply(use.user_transform, viewport);
        quantapdf_affine_transform const transform =
            resource_affine(placement);

        quantapdf_composer_form_draw_options draw_options{};
        draw_options.struct_size =
            QUANTAPDF_COMPOSER_FORM_DRAW_OPTIONS_V1_SIZE;
        draw_options.graphics_state_id = state_id;
        status = quantapdf_composer_draw_form(
            composer,
            page_index,
            form_id,
            &transform,
            &draw_options);
        if (status != QUANTAPDF_OK)
            return status;
    }

    return QUANTAPDF_OK;
}

quantapdf_status group_form_builder(
    quantapdf_composer* composer,
    size_t page_index,
    void* user_data)
{
    return symbol_form_builder(composer, page_index, user_data);
}

quantapdf_status publish_groups(
    quantapdf_composer* composer,
    size_t page_index,
    std::vector<staged_group> const& groups,
    quantapdf_rect const& bounds)
{
    if (groups.empty())
        return QUANTAPDF_OK;

    quantapdf_status const reserve =
        quantapdf_composer_reserve_operations_internal(
            composer, groups.size());
    if (reserve != QUANTAPDF_OK)
        return reserve;

    float const width = bounds.x1 - bounds.x0;
    float const height = bounds.y1 - bounds.y0;
    if (!std::isfinite(width) || !std::isfinite(height) ||
        width <= 0.0f || height <= 0.0f)
        return QUANTAPDF_ERROR_ARGUMENT;

    for (auto const& group: groups) {
        symbol_form_builder_context context;
        context.svg = &group.svg;
        context.width = width;
        context.height = height;

        quantapdf_composer_form_options form_options{};
        form_options.struct_size =
            QUANTAPDF_COMPOSER_FORM_OPTIONS_V2_SIZE;
        form_options.width_points = width;
        form_options.height_points = height;
        form_options.flags =
            QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP |
            QUANTAPDF_COMPOSER_FORM_FLAG_ISOLATED;

        quantapdf_composer_form_id form_id = 0u;
        quantapdf_status status = quantapdf_composer_add_form(
            composer,
            &form_options,
            group_form_builder,
            &context,
            &form_id);
        if (status != QUANTAPDF_OK)
            return status;

        quantapdf_composer_graphics_state_options state{};
        state.struct_size =
            QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V1_SIZE;
        state.fill_alpha = group.opacity;
        state.stroke_alpha = group.opacity;
        state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;

        quantapdf_composer_graphics_state_id state_id = 0u;
        status = quantapdf_composer_add_graphics_state(
            composer, &state, &state_id);
        if (status != QUANTAPDF_OK)
            return status;

        quantapdf_affine_transform placement = {
            1.0f, 0.0f, 0.0f, 1.0f, bounds.x0, bounds.y0};
        quantapdf_composer_form_draw_options draw_options{};
        draw_options.struct_size =
            QUANTAPDF_COMPOSER_FORM_DRAW_OPTIONS_V1_SIZE;
        draw_options.graphics_state_id = state_id;
        status = quantapdf_composer_draw_form(
            composer,
            page_index,
            form_id,
            &placement,
            &draw_options);
        if (status != QUANTAPDF_OK)
            return status;
    }
    return QUANTAPDF_OK;
}

struct svg_publish_snapshot {
    size_t operation_count = 0u;
    size_t paint_count = 0u;
    size_t clip_count = 0u;
    size_t graphics_state_count = 0u;
    size_t form_count = 0u;
    size_t resource_bytes = 0u;
};

svg_publish_snapshot capture_svg_snapshot(
    quantapdf_composer const* composer)
{
    return {
        composer->operation_count,
        composer->paint_count,
        composer->clip_count,
        composer->graphics_state_count,
        composer->form_count,
        composer->resource_bytes};
}

void rollback_svg_publish(
    quantapdf_composer* composer,
    svg_publish_snapshot const& snapshot)
{
    for (size_t i = snapshot.operation_count;
         i < composer->operation_count;
         ++i) {
        auto& operation = composer->operations[i];
        if (operation.kind == QUANTAPDF_COMPOSER_OPERATION_PATH) {
            std::free(operation.value.path.dash_lengths);
            std::free(operation.value.path.commands);
        }
    }
    for (size_t i = snapshot.paint_count;
         i < composer->paint_count;
         ++i) {
        std::free(composer->paints[i].stops);
        std::free(composer->paints[i].pdf_data);
    }
    for (size_t i = snapshot.clip_count;
         i < composer->clip_count;
         ++i)
        std::free(composer->clips[i].commands);
    for (size_t i = snapshot.form_count;
         i < composer->form_count;
         ++i)
        std::free(composer->forms[i].pdf_data);

    composer->operation_count = snapshot.operation_count;
    composer->paint_count = snapshot.paint_count;
    composer->clip_count = snapshot.clip_count;
    composer->graphics_state_count = snapshot.graphics_state_count;
    composer->form_count = snapshot.form_count;
    composer->resource_bytes = snapshot.resource_bytes;
}

quantapdf_status publish_paths(
    quantapdf_composer* composer,
    size_t page_index,
    std::vector<staged_path> const& paths,
    quantapdf_rect const& bounds,
    bool clip_to_bounds,
    definition_table const& definitions)
{
    if (paths.empty())
        return QUANTAPDF_OK;

    if (clip_to_bounds) {
        for (auto const& path: paths) {
            if (!path.clip_ref.empty())
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }

    size_t total_bytes = 0u;
    for (auto const& path: paths) {
        if (path.commands.size() >
            SIZE_MAX / sizeof(quantapdf_composer_path_command) ||
            path.dash_lengths.size() > SIZE_MAX / sizeof(float))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        size_t const command_bytes =
            path.commands.size() *
            sizeof(quantapdf_composer_path_command);
        size_t const dash_bytes =
            path.dash_lengths.size() * sizeof(float);
        if (command_bytes > SIZE_MAX - dash_bytes)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        size_t const bytes = command_bytes + dash_bytes;
        if (bytes > SIZE_MAX - total_bytes)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        total_bytes += bytes;
    }

    quantapdf_status const reserve =
        quantapdf_composer_reserve_operations_internal(
            composer, paths.size());
    if (reserve != QUANTAPDF_OK)
        return reserve;

    std::vector<quantapdf_composer_paint_id>
        fill_paint_ids(paths.size(), 0u);
    std::vector<quantapdf_composer_paint_id>
        stroke_paint_ids(paths.size(), 0u);
    std::vector<quantapdf_composer_clip_id>
        clip_ids(paths.size(), 0u);
    std::vector<quantapdf_composer_graphics_state_id>
        state_ids(paths.size(), 0u);
    std::vector<quantapdf_composer_operation> staged(paths.size());

    size_t const paint_snapshot = composer->paint_count;
    size_t const state_snapshot = composer->graphics_state_count;
    size_t const clip_snapshot = composer->clip_count;
    size_t const resource_snapshot = composer->resource_bytes;

    auto rollback_resources = [&]() {
        for (size_t i = paint_snapshot; i < composer->paint_count; ++i) {
            std::free(composer->paints[i].stops);
            std::free(composer->paints[i].pdf_data);
        }
        for (size_t i = clip_snapshot; i < composer->clip_count; ++i)
            std::free(composer->clips[i].commands);
        composer->paint_count = paint_snapshot;
        composer->clip_count = clip_snapshot;
        composer->graphics_state_count = state_snapshot;
        composer->resource_bytes = resource_snapshot;
    };

    quantapdf_composer_clip_id viewport_clip_id = 0u;
    if (clip_to_bounds) {
        auto clip_commands = rectangle(
            bounds.x0,
            bounds.y0,
            bounds.x1 - bounds.x0,
            bounds.y1 - bounds.y0);
        quantapdf_composer_clip_options clip_options{};
        clip_options.struct_size = QUANTAPDF_COMPOSER_CLIP_OPTIONS_V1_SIZE;
        clip_options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
        quantapdf_status const clip_status =
            quantapdf_composer_add_clip_path(
                composer,
                clip_commands.data(),
                clip_commands.size(),
                &clip_options,
                &viewport_clip_id);
        if (clip_status != QUANTAPDF_OK) {
            rollback_resources();
            return clip_status;
        }
    }

    for (size_t i = 0u; i < paths.size(); ++i) {
        auto resolve_paint = [&](std::string const& reference,
                                 quantapdf_composer_paint_id* out_id)
            -> quantapdf_status {
            if (reference.empty())
                return QUANTAPDF_OK;
            auto gradient = definitions.gradients.find(reference);
            if (gradient != definitions.gradients.end()) {
                return register_gradient_paint(
                    composer,
                    gradient->second,
                    paths[i].resource_transform,
                    paths[i].local_bounds,
                    out_id);
            }
            auto pattern = definitions.patterns.find(reference);
            if (pattern != definitions.patterns.end()) {
                return materialize_pattern(
                    composer,
                    definitions,
                    reference,
                    paths[i].resource_transform,
                    paths[i].local_bounds,
                    out_id);
            }
            return QUANTAPDF_ERROR_UNSUPPORTED;
        };

        quantapdf_status status =
            resolve_paint(paths[i].fill_ref, &fill_paint_ids[i]);
        if (status != QUANTAPDF_OK) {
            rollback_resources();
            return status;
        }
        status = resolve_paint(
            paths[i].stroke_ref, &stroke_paint_ids[i]);
        if (status != QUANTAPDF_OK) {
            rollback_resources();
            return status;
        }

        quantapdf_composer_clip_id effective_clip_id = viewport_clip_id;
        if (!paths[i].clip_ref.empty()) {
            auto found = definitions.clips.find(paths[i].clip_ref);
            if (found == definitions.clips.end()) {
                rollback_resources();
                return QUANTAPDF_ERROR_UNSUPPORTED;
            }
            quantapdf_status const clip_resource_status =
                register_clip_resource(
                    composer,
                    found->second,
                    paths[i].resource_transform,
                    paths[i].local_bounds,
                    &clip_ids[i]);
            if (clip_resource_status != QUANTAPDF_OK) {
                rollback_resources();
                return clip_resource_status;
            }
            effective_clip_id = clip_ids[i];
        }

        if (paths[i].fill_alpha != 1.0f ||
            paths[i].stroke_alpha != 1.0f ||
            effective_clip_id != 0u) {
            quantapdf_composer_graphics_state_options state{};
            state.struct_size =
                effective_clip_id == 0u
                ? QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V1_SIZE
                : QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V2_SIZE;
            state.fill_alpha = paths[i].fill_alpha;
            state.stroke_alpha = paths[i].stroke_alpha;
            state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
            state.clip_id = effective_clip_id;
            quantapdf_status const state_status =
                quantapdf_composer_add_graphics_state(
                    composer, &state, &state_ids[i]);
            if (state_status != QUANTAPDF_OK) {
                rollback_resources();
                return state_status;
            }
        }
    }

    if (composer->resource_bytes > composer->max_resource_bytes ||
        total_bytes >
            composer->max_resource_bytes - composer->resource_bytes) {
        rollback_resources();
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }

    size_t allocated = 0u;
    for (size_t i = 0u; i < paths.size(); ++i) {
        size_t const command_bytes =
            paths[i].commands.size() *
            sizeof(quantapdf_composer_path_command);
        auto* command_copy =
            static_cast<quantapdf_composer_path_command*>(
                std::malloc(command_bytes));
        if (command_copy == nullptr) {
            for (size_t j = 0u; j < allocated; ++j) {
                std::free(staged[j].value.path.dash_lengths);
                std::free(staged[j].value.path.commands);
            }
            rollback_resources();
            return QUANTAPDF_ERROR_NOMEM;
        }
        std::memcpy(
            command_copy, paths[i].commands.data(), command_bytes);

        float* dash_copy = nullptr;
        if (!paths[i].dash_lengths.empty()) {
            size_t const dash_bytes =
                paths[i].dash_lengths.size() * sizeof(float);
            dash_copy =
                static_cast<float*>(std::malloc(dash_bytes));
            if (dash_copy == nullptr) {
                std::free(command_copy);
                for (size_t j = 0u; j < allocated; ++j) {
                    std::free(staged[j].value.path.dash_lengths);
                    std::free(staged[j].value.path.commands);
                }
                rollback_resources();
                return QUANTAPDF_ERROR_NOMEM;
            }
            std::memcpy(
                dash_copy, paths[i].dash_lengths.data(), dash_bytes);
        }

        quantapdf_composer_operation operation{};
        operation.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
        operation.page_index = page_index;
        operation.graphics_state_id = state_ids[i];
        operation.value.path.commands = command_copy;
        operation.value.path.command_count =
            paths[i].commands.size();
        operation.value.path.options = paths[i].options;
        if (fill_paint_ids[i] != 0u || stroke_paint_ids[i] != 0u) {
            /*
             * Do not downgrade a V4 PATH to the V3 paint boundary.
             * V4 owns the affine CTM; paint IDs are an earlier tail.
             */
            if (operation.value.path.options.struct_size <
                QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_MIN_SIZE)
                operation.value.path.options.struct_size =
                    QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_SIZE;
            operation.value.path.options.fill_paint_id =
                fill_paint_ids[i];
            operation.value.path.options.stroke_paint_id =
                stroke_paint_ids[i];
        }
        operation.value.path.dash_lengths = dash_copy;
        operation.value.path.dash_count =
            paths[i].dash_lengths.size();
        operation.value.path.dash_phase =
            paths[i].dash_phase;
        staged[i] = operation;
        ++allocated;
    }

    for (auto const& operation: staged)
        composer->operations[composer->operation_count++] = operation;
    composer->resource_bytes += total_bytes;
    return QUANTAPDF_OK;
}

quantapdf_status publish_svg_document(
    quantapdf_composer* composer,
    size_t page_index,
    std::vector<staged_path> const& paths,
    std::vector<staged_use> const& uses,
    std::vector<staged_group> const& groups,
    quantapdf_rect const& bounds,
    bool clip_to_bounds,
    definition_table const& definitions)
{
    svg_publish_snapshot const snapshot =
        capture_svg_snapshot(composer);
    try {
        quantapdf_status status = publish_paths(
            composer,
            page_index,
            paths,
            bounds,
            clip_to_bounds,
            definitions);
        if (status != QUANTAPDF_OK) {
            rollback_svg_publish(composer, snapshot);
            return status;
        }

        status = publish_uses(
            composer,
            page_index,
            uses,
            bounds,
            clip_to_bounds,
            definitions);
        if (status != QUANTAPDF_OK) {
            rollback_svg_publish(composer, snapshot);
            return status;
        }

        status = publish_groups(
            composer,
            page_index,
            groups,
            bounds);
        if (status != QUANTAPDF_OK) {
            rollback_svg_publish(composer, snapshot);
            return status;
        }

        size_t const expected =
            paths.size() + uses.size() + groups.size();
        if (composer->operation_count - snapshot.operation_count !=
            expected) {
            rollback_svg_publish(composer, snapshot);
            return QUANTAPDF_ERROR_BACKEND;
        }

        std::vector<std::pair<size_t, quantapdf_composer_operation>>
            ordered;
        ordered.reserve(expected);
        size_t cursor = snapshot.operation_count;
        for (auto const& path: paths)
            ordered.emplace_back(path.order, composer->operations[cursor++]);
        for (auto const& use: uses)
            ordered.emplace_back(use.order, composer->operations[cursor++]);
        for (auto const& group: groups)
            ordered.emplace_back(group.order, composer->operations[cursor++]);
        std::sort(
            ordered.begin(),
            ordered.end(),
            [](auto const& left, auto const& right) {
                return left.first < right.first;
            });
        for (size_t i = 1u; i < ordered.size(); ++i) {
            if (ordered[i - 1u].first == ordered[i].first) {
                rollback_svg_publish(composer, snapshot);
                return QUANTAPDF_ERROR_BACKEND;
            }
        }
        for (size_t i = 0u; i < ordered.size(); ++i) {
            composer->operations[snapshot.operation_count + i] =
                ordered[i].second;
        }
        return QUANTAPDF_OK;
    } catch (...) {
        rollback_svg_publish(composer, snapshot);
        throw;
    }
}

} // namespace

extern "C" quantapdf_status quantapdf_composer_draw_svg(
    quantapdf_composer* composer,
    size_t page_index,
    unsigned char const* svg_data,
    size_t svg_size,
    quantapdf_rect const* bounds,
    quantapdf_composer_svg_options const* options)
{
    if (composer == nullptr || svg_data == nullptr || svg_size == 0u ||
        bounds == nullptr || options == nullptr ||
        options->struct_size < QUANTAPDF_COMPOSER_SVG_OPTIONS_V1_MIN_SIZE ||
        page_index >= composer->page_count ||
        !std::isfinite(bounds->x0) || !std::isfinite(bounds->y0) ||
        !std::isfinite(bounds->x1) || !std::isfinite(bounds->y1) ||
        bounds->x1 <= bounds->x0 || bounds->y1 <= bounds->y0)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (svg_size > k_svg_max_input_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (composer->operation_count > composer->max_operations)
        return QUANTAPDF_ERROR_STATE;

    for (size_t i = 0u; i < svg_size; ++i) {
        if (svg_data[i] == 0u)
            return QUANTAPDF_ERROR_FORMAT;
    }

    try {
        size_t const available =
            composer->max_operations - composer->operation_count;
        definition_table const definitions =
            parse_definitions(svg_data, svg_size);
        svg_parser parser(
            svg_data,
            svg_size,
            *bounds,
            available,
            definitions);
        auto paths = parser.parse();
        auto uses = parser.take_uses();
        auto groups = parser.take_groups();
        if (paths.size() > available ||
            uses.size() > available - paths.size() ||
            groups.size() >
                available - paths.size() - uses.size())
            return QUANTAPDF_ERROR_UNSUPPORTED;
        return publish_svg_document(
            composer,
            page_index,
            paths,
            uses,
            groups,
            *bounds,
            parser.clip_to_bounds(),
            definitions);
    } catch (svg_error const& error) {
        return error.status;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}
