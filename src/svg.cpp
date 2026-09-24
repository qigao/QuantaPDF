#include "internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
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

double conformal_scale(matrix const& transform)
{
    double const sx = std::hypot(transform.a, transform.b);
    double const sy = std::hypot(transform.c, transform.d);
    double const dot =
        transform.a * transform.c + transform.b * transform.d;
    double const magnitude = std::max({1.0, sx, sy});
    double const epsilon = 1e-8 * magnitude;
    if (sx <= 0.0 || sy <= 0.0 ||
        std::fabs(sx - sy) > epsilon ||
        std::fabs(dot) > 1e-8 * sx * sy)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    return (sx + sy) / 2.0;
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
    double fill_opacity = 1.0;
    double stroke_opacity = 1.0;
    double stroke_width = 1.0;
    std::vector<double> dash_array;
    double dash_offset = 0.0;
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

double unit_interval(std::string const& value)
{
    std::string text = trim(value);
    number_scanner scanner(text);
    double result = scanner.number();
    if (!scanner.done())
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    if (result < 0.0)
        return 0.0;
    if (result > 1.0)
        return 1.0;
    return result;
}

std::vector<double> dash_array(std::string const& value)
{
    std::string const text = lower_ascii(trim(value));
    if (text == "none")
        return {};

    auto values = number_list(text);
    if (values.empty())
        fail(QUANTAPDF_ERROR_FORMAT);
    for (double item: values) {
        if (!finite(item) || item < 0.0)
            fail(QUANTAPDF_ERROR_FORMAT);
    }
    if ((values.size() & 1u) != 0u) {
        if (values.size() > QUANTAPDF_COMPOSER_MAX_DASH_COUNT / 2u)
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        size_t const original = values.size();
        values.reserve(original * 2u);
        for (size_t i = 0u; i < original; ++i)
            values.push_back(values[i]);
    }
    if (values.size() > QUANTAPDF_COMPOSER_MAX_DASH_COUNT)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);

    bool have_positive = false;
    for (double item: values) {
        if (item > 0.0) {
            have_positive = true;
            break;
        }
    }
    return have_positive ? values : std::vector<double>{};
}

void apply_style_property(
    paint_style* style,
    std::string name,
    std::string const& value)
{
    name = lower_ascii(trim(name));
    if (name == "fill") {
        style->fill_argb = parse_color(value, &style->fill);
    } else if (name == "stroke") {
        style->stroke_argb = parse_color(value, &style->stroke);
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
        style->fill_opacity = unit_interval(value);
    } else if (name == "stroke-opacity") {
        style->stroke_opacity = unit_interval(value);
    } else if (name == "stroke-dasharray") {
        style->dash_array = dash_array(value);
    } else if (name == "stroke-dashoffset") {
        style->dash_offset = scalar(value);
        if (!finite(style->dash_offset))
            fail(QUANTAPDF_ERROR_FORMAT);
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
        name == "stroke-dasharray" || name == "stroke-dashoffset";
}

bool tag_attribute_allowed(
    std::string const& tag,
    std::string const& name,
    bool root)
{
    if (common_attribute(name))
        return true;
    if (root) {
        if (name == "viewBox" || name == "preserveAspectRatio" ||
            name == "width" || name == "height" ||
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
    for (auto const& attr: item.attributes) {
        if (attr.name == "fill" || attr.name == "stroke" ||
            attr.name == "fill-rule" || attr.name == "stroke-width" ||
            attr.name == "stroke-linecap" ||
            attr.name == "stroke-linejoin" ||
            attr.name == "stroke-miterlimit" ||
            attr.name == "fill-opacity" ||
            attr.name == "stroke-opacity" ||
            attr.name == "stroke-dasharray" ||
            attr.name == "stroke-dashoffset")
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

struct staged_path {
    std::vector<quantapdf_composer_path_command> commands;
    quantapdf_composer_path_options options{};
};

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

void stage_path(
    std::vector<staged_path>* paths,
    std::vector<quantapdf_composer_path_command> commands,
    paint_style const& style,
    matrix const& transform,
    size_t max_paths)
{
    if (!style.fill && !style.stroke)
        return;
    if (commands.empty())
        fail(QUANTAPDF_ERROR_FORMAT);
    if (paths->size() >= max_paths)
        fail(QUANTAPDF_ERROR_UNSUPPORTED);

    transform_commands(&commands, transform);

    staged_path staged;
    staged.commands = std::move(commands);
    staged.options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_SIZE;
    staged.options.fill = style.fill ? 1 : 0;
    staged.options.stroke = style.stroke ? 1 : 0;
    staged.options.fill_argb = style.fill_argb;
    staged.options.stroke_argb = style.stroke_argb;
    staged.options.fill_rule = style.fill_rule;
    staged.options.line_cap = style.line_cap;
    staged.options.line_join = style.line_join;
    if (!finite(style.miter_limit) ||
        style.miter_limit < 1.0 ||
        style.miter_limit > std::numeric_limits<float>::max())
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
    staged.options.miter_limit = static_cast<float>(style.miter_limit);

    if (style.stroke) {
        double const width =
            style.stroke_width * conformal_scale(transform);
        if (!finite(width) || width < 0.0 ||
            width > std::numeric_limits<float>::max())
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        staged.options.stroke_width = static_cast<float>(width);
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
            if (command == 'A' || command == 'a')
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
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

struct context {
    std::string name;
    paint_style style;
    matrix transform;
    bool leaf = false;
};

class svg_parser {
  public:
    svg_parser(
        unsigned char const* data,
        size_t size,
        quantapdf_rect const& bounds,
        size_t max_paths):
        scanner_(data, size),
        bounds_(bounds),
        max_paths_(max_paths)
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

                double const sx =
                    (bounds_.x1 - bounds_.x0) / values[2];
                double const sy =
                    (bounds_.y1 - bounds_.y0) / values[3];
                matrix viewport = multiply(
                    translate_matrix(bounds_.x0, bounds_.y0),
                    multiply(
                        scale_matrix(sx, sy),
                        translate_matrix(-values[0], -values[1])));

                context root;
                root.name = tag;
                root.style = derive_style(paint_style{}, item);
                root.transform = derive_transform(viewport, item);
                if (!item.self_closing)
                    stack_.push_back(std::move(root));
                continue;
            }

            context const& parent = stack_.back();
            if (tag == "svg")
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            if (tag == "g") {
                validate_attributes(item, tag, false);
                context group;
                group.name = tag;
                group.style = derive_style(parent.style, item);
                group.transform =
                    derive_transform(parent.transform, item);
                if (!item.self_closing)
                    stack_.push_back(std::move(group));
                continue;
            }

            if (tag != "path" && tag != "rect" && tag != "line" &&
                tag != "polyline" && tag != "polygon" &&
                tag != "circle" && tag != "ellipse")
                fail(QUANTAPDF_ERROR_UNSUPPORTED);

            validate_attributes(item, tag, false);
            paint_style const style =
                derive_style(parent.style, item);
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

  private:
    xml_scanner scanner_;
    quantapdf_rect bounds_;
    size_t max_paths_;
    std::vector<context> stack_;
    std::vector<staged_path> paths_;
};

quantapdf_status publish_paths(
    quantapdf_composer* composer,
    size_t page_index,
    std::vector<staged_path> const& paths)
{
    if (paths.empty())
        return QUANTAPDF_OK;

    size_t total_bytes = 0u;
    for (auto const& path: paths) {
        if (path.commands.size() >
            SIZE_MAX / sizeof(quantapdf_composer_path_command))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        size_t const bytes =
            path.commands.size() *
            sizeof(quantapdf_composer_path_command);
        if (bytes > SIZE_MAX - total_bytes)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        total_bytes += bytes;
    }
    if (composer->resource_bytes > composer->max_resource_bytes ||
        total_bytes >
            composer->max_resource_bytes - composer->resource_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    quantapdf_status const reserve =
        quantapdf_composer_reserve_operations_internal(
            composer, paths.size());
    if (reserve != QUANTAPDF_OK)
        return reserve;

    std::vector<quantapdf_composer_operation> staged(paths.size());
    size_t allocated = 0u;
    for (size_t i = 0u; i < paths.size(); ++i) {
        size_t const bytes =
            paths[i].commands.size() *
            sizeof(quantapdf_composer_path_command);
        auto* copy =
            static_cast<quantapdf_composer_path_command*>(
                std::malloc(bytes));
        if (copy == nullptr) {
            for (size_t j = 0u; j < allocated; ++j)
                std::free(staged[j].value.path.commands);
            return QUANTAPDF_ERROR_NOMEM;
        }
        std::memcpy(copy, paths[i].commands.data(), bytes);
        quantapdf_composer_operation operation{};
        operation.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
        operation.page_index = page_index;
        operation.value.path.commands = copy;
        operation.value.path.command_count =
            paths[i].commands.size();
        operation.value.path.options = paths[i].options;
        staged[i] = operation;
        ++allocated;
    }

    for (auto const& operation: staged)
        composer->operations[composer->operation_count++] = operation;
    composer->resource_bytes += total_bytes;
    return QUANTAPDF_OK;
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
        svg_parser parser(svg_data, svg_size, *bounds, available);
        auto paths = parser.parse();
        return publish_paths(composer, page_index, paths);
    } catch (svg_error const& error) {
        return error.status;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}
