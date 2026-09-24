#include "internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <set>
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

struct staged_path {
    std::vector<quantapdf_composer_path_command> commands;
    quantapdf_composer_path_options options{};
    std::vector<float> dash_lengths;
    float dash_phase = 0.0f;
    float fill_alpha = 1.0f;
    float stroke_alpha = 1.0f;
    std::string fill_ref;
    std::string stroke_ref;
    std::string clip_ref;
    matrix resource_transform;
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
    staged.fill_ref = style.fill_ref;
    staged.stroke_ref = style.stroke_ref;
    staged.clip_ref = style.clip_ref;
    staged.resource_transform = transform;
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

    double stroke_scale = 1.0;
    if (style.stroke) {
        stroke_scale = conformal_scale(transform);
        double const width = style.stroke_width * stroke_scale;
        if (!finite(width) || width < 0.0 ||
            width > std::numeric_limits<float>::max())
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
        staged.options.stroke_width = static_cast<float>(width);
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
            double const scaled = item * stroke_scale;
            if (!finite(scaled) || scaled < 0.0 ||
                scaled > std::numeric_limits<float>::max())
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            pattern_length += scaled;
            if (!finite(pattern_length))
                fail(QUANTAPDF_ERROR_UNSUPPORTED);
            staged.dash_lengths.push_back(static_cast<float>(scaled));
        }
        if (pattern_length <= 0.0)
            staged.dash_lengths.clear();
        else {
            double phase = style.dash_offset * stroke_scale;
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

struct gradient_definition {
    bool radial = false;
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
    matrix transform;
    std::vector<quantapdf_composer_gradient_stop> stops;
};

struct clip_definition {
    std::vector<quantapdf_composer_path_command> commands;
    quantapdf_composer_fill_rule fill_rule =
        QUANTAPDF_COMPOSER_FILL_NONZERO;
};

struct definition_table {
    std::map<std::string, gradient_definition> gradients;
    std::map<std::string, clip_definition> clips;
    std::set<std::string> ids;
};

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

void validate_gradient_attributes(
    element const& item,
    bool radial)
{
    for (auto const& attr: item.attributes) {
        bool allowed =
            attr.name == "id" ||
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
    auto const* units = find_attribute(item, "gradientUnits");
    if (units == nullptr || trim(*units) != "userSpaceOnUse")
        fail(QUANTAPDF_ERROR_UNSUPPORTED);
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
    if (auto const* transform = find_attribute(item, "gradientTransform"))
        result.transform = parse_transform(*transform);

    if (!radial) {
        result.x1 = required_number(item, "x1");
        result.y1 = required_number(item, "y1");
        result.x2 = required_number(item, "x2");
        result.y2 = required_number(item, "y2");
        if (result.x1 == result.x2 && result.y1 == result.y2)
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
    } else {
        result.cx = required_number(item, "cx");
        result.cy = required_number(item, "cy");
        result.radius = required_number(item, "r");
        if (result.radius <= 0.0)
            fail(QUANTAPDF_ERROR_FORMAT);
        result.fx = optional_number(item, "fx", result.cx);
        result.fy = optional_number(item, "fy", result.cy);
        result.fr = optional_number(item, "fr", 0.0);
        if (result.fr < 0.0)
            fail(QUANTAPDF_ERROR_FORMAT);
        if (result.fr == result.radius &&
            result.fx == result.cx && result.fy == result.cy)
            fail(QUANTAPDF_ERROR_UNSUPPORTED);
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

                preserve_aspect const preserve =
                    parse_preserve_aspect(
                        find_attribute(item, "preserveAspectRatio"));
                clip_to_bounds_ = preserve.slice;
                matrix const viewport = viewport_matrix(
                    bounds_, values, preserve);

                context root;
                root.name = tag;
                root.style = derive_style(paint_style{}, item);
                if (root.style.opacity != 1.0)
                    fail(QUANTAPDF_ERROR_UNSUPPORTED);
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
                if (group.style.opacity != 1.0)
                    fail(QUANTAPDF_ERROR_UNSUPPORTED);
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

    bool clip_to_bounds() const
    {
        return clip_to_bounds_;
    }

  private:
    xml_scanner scanner_;
    quantapdf_rect bounds_;
    size_t max_paths_;
    bool clip_to_bounds_ = false;
    std::vector<context> stack_;
    std::vector<staged_path> paths_;
};

quantapdf_status publish_paths(
    quantapdf_composer* composer,
    size_t page_index,
    std::vector<staged_path> const& paths,
    quantapdf_rect const& bounds,
    bool clip_to_bounds)
{
    if (paths.empty())
        return QUANTAPDF_OK;

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

    std::vector<quantapdf_composer_graphics_state_id>
        state_ids(paths.size(), 0u);
    std::vector<quantapdf_composer_operation> staged(paths.size());

    size_t const state_snapshot = composer->graphics_state_count;
    size_t const clip_snapshot = composer->clip_count;
    size_t const resource_snapshot = composer->resource_bytes;

    auto rollback_resources = [&]() {
        for (size_t i = clip_snapshot; i < composer->clip_count; ++i)
            std::free(composer->clips[i].commands);
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

    if (composer->resource_bytes > composer->max_resource_bytes ||
        total_bytes >
            composer->max_resource_bytes - composer->resource_bytes) {
        rollback_resources();
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }

    for (size_t i = 0u; i < paths.size(); ++i) {
        if (paths[i].fill_alpha == 1.0f &&
            paths[i].stroke_alpha == 1.0f &&
            viewport_clip_id == 0u)
            continue;
        quantapdf_composer_graphics_state_options state{};
        state.struct_size =
            viewport_clip_id == 0u
            ? QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V1_SIZE
            : QUANTAPDF_COMPOSER_GRAPHICS_STATE_OPTIONS_V2_SIZE;
        state.fill_alpha = paths[i].fill_alpha;
        state.stroke_alpha = paths[i].stroke_alpha;
        state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
        state.clip_id = viewport_clip_id;
        quantapdf_status const status =
            quantapdf_composer_add_graphics_state(
                composer, &state, &state_ids[i]);
        if (status != QUANTAPDF_OK) {
            rollback_resources();
            return status;
        }
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
        return publish_paths(
            composer,
            page_index,
            paths,
            *bounds,
            parser.clip_to_bounds());
    } catch (svg_error const& error) {
        return error.status;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}
