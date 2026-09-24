#include "qpdf_composer.h"

#include "../internal.h"
#include "composer_text_layout.h"
#include "ttf_font.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

uint32_t read_be32(unsigned char const* data)
{
    return (static_cast<uint32_t>(data[0]) << 24u) |
        (static_cast<uint32_t>(data[1]) << 16u) |
        (static_cast<uint32_t>(data[2]) << 8u) |
        static_cast<uint32_t>(data[3]);
}

bool checked_multiply(size_t left, size_t right, size_t& result)
{
    if (left != 0u && right > std::numeric_limits<size_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

bool checked_add(size_t left, size_t right, size_t& result)
{
    if (right > std::numeric_limits<size_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

bool png_chunk_type_valid(unsigned char const* type)
{
    for (size_t i = 0u; i < 4u; ++i) {
        if (!((type[i] >= 'A' && type[i] <= 'Z') ||
              (type[i] >= 'a' && type[i] <= 'z')))
            return false;
    }
    return type[2] >= 'A' && type[2] <= 'Z';
}

unsigned char paeth_predictor(
    unsigned char left,
    unsigned char up,
    unsigned char upper_left)
{
    int const prediction = static_cast<int>(left) + static_cast<int>(up) -
        static_cast<int>(upper_left);
    int const left_distance = std::abs(prediction - static_cast<int>(left));
    int const up_distance = std::abs(prediction - static_cast<int>(up));
    int const diagonal_distance =
        std::abs(prediction - static_cast<int>(upper_left));
    if (left_distance <= up_distance && left_distance <= diagonal_distance)
        return left;
    return up_distance <= diagonal_distance ? up : upper_left;
}

int decimal_precision(double value)
{
    double const magnitude = std::abs(value);
    if (magnitude == 0.0 || magnitude >= 0.00005)
        return 4;
    return std::min(
        64, std::max(4, static_cast<int>(std::ceil(-std::log10(magnitude))) + 4));
}

std::string number(double value)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(decimal_precision(value)) << value;
    std::string result = stream.str();
    while (result.size() > 1u && result.back() == '0')
        result.pop_back();
    if (!result.empty() && result.back() == '.')
        result.pop_back();
    return result;
}

double canonical_zero(double value)
{
    return value == 0.0 ? 0.0 : value;
}

char const* blend_mode_name(
    quantapdf_composer_blend_mode blend_mode)
{
    switch (blend_mode) {
    case QUANTAPDF_COMPOSER_BLEND_NORMAL:
        return "/Normal";
    case QUANTAPDF_COMPOSER_BLEND_MULTIPLY:
        return "/Multiply";
    case QUANTAPDF_COMPOSER_BLEND_SCREEN:
        return "/Screen";
    case QUANTAPDF_COMPOSER_BLEND_OVERLAY:
        return "/Overlay";
    case QUANTAPDF_COMPOSER_BLEND_DARKEN:
        return "/Darken";
    case QUANTAPDF_COMPOSER_BLEND_LIGHTEN:
        return "/Lighten";
    }
    throw std::logic_error("invalid Composer blend mode");
}

QPDFObjectHandle make_graphics_state(
    QPDF& pdf,
    quantapdf_composer_graphics_state const& state)
{
    auto dictionary = QPDFObjectHandle::newDictionary();
    dictionary.replaceKey(
        "/Type", QPDFObjectHandle::newName("/ExtGState"));
    dictionary.replaceKey(
        "/ca",
        QPDFObjectHandle::newReal(
            state.fill_alpha, decimal_precision(state.fill_alpha)));
    dictionary.replaceKey(
        "/CA",
        QPDFObjectHandle::newReal(
            state.stroke_alpha, decimal_precision(state.stroke_alpha)));
    dictionary.replaceKey(
        "/BM",
        QPDFObjectHandle::newName(blend_mode_name(state.blend_mode)));
    return pdf.makeIndirectObject(dictionary);
}

QPDFObjectHandle rgb_array(uint32_t argb)
{
    auto result = QPDFObjectHandle::newArray();
    result.appendItem(QPDFObjectHandle::newReal(
        ((argb >> 16u) & 0xffu) / 255.0, 6));
    result.appendItem(QPDFObjectHandle::newReal(
        ((argb >> 8u) & 0xffu) / 255.0, 6));
    result.appendItem(QPDFObjectHandle::newReal(
        (argb & 0xffu) / 255.0, 6));
    return result;
}

QPDFObjectHandle make_gradient_segment_function(
    QPDF& pdf,
    uint32_t start_argb,
    uint32_t end_argb)
{
    auto function = QPDFObjectHandle::newDictionary();
    auto domain = QPDFObjectHandle::newArray();
    domain.appendItem(QPDFObjectHandle::newInteger(0));
    domain.appendItem(QPDFObjectHandle::newInteger(1));
    function.replaceKey("/FunctionType", QPDFObjectHandle::newInteger(2));
    function.replaceKey("/Domain", domain);
    function.replaceKey("/C0", rgb_array(start_argb));
    function.replaceKey("/C1", rgb_array(end_argb));
    function.replaceKey("/N", QPDFObjectHandle::newInteger(1));
    return pdf.makeIndirectObject(function);
}

QPDFObjectHandle make_gradient_function(
    QPDF& pdf,
    quantapdf_composer_paint_state const& paint)
{
    if (paint.stop_count == 2u) {
        return make_gradient_segment_function(
            pdf, paint.stops[0].argb, paint.stops[1].argb);
    }

    auto function = QPDFObjectHandle::newDictionary();
    auto domain = QPDFObjectHandle::newArray();
    auto functions = QPDFObjectHandle::newArray();
    auto bounds = QPDFObjectHandle::newArray();
    auto encode = QPDFObjectHandle::newArray();

    domain.appendItem(QPDFObjectHandle::newInteger(0));
    domain.appendItem(QPDFObjectHandle::newInteger(1));
    for (size_t i = 0u; i + 1u < paint.stop_count; ++i) {
        functions.appendItem(make_gradient_segment_function(
            pdf, paint.stops[i].argb, paint.stops[i + 1u].argb));
        encode.appendItem(QPDFObjectHandle::newInteger(0));
        encode.appendItem(QPDFObjectHandle::newInteger(1));
        if (i + 1u < paint.stop_count - 1u) {
            bounds.appendItem(QPDFObjectHandle::newReal(
                paint.stops[i + 1u].offset,
                decimal_precision(paint.stops[i + 1u].offset)));
        }
    }

    function.replaceKey("/FunctionType", QPDFObjectHandle::newInteger(3));
    function.replaceKey("/Domain", domain);
    function.replaceKey("/Functions", functions);
    function.replaceKey("/Bounds", bounds);
    function.replaceKey("/Encode", encode);
    return pdf.makeIndirectObject(function);
}

QPDFObjectHandle make_gradient_shading(
    QPDF& pdf,
    quantapdf_composer_paint_state const& paint)
{
    auto shading = QPDFObjectHandle::newDictionary();
    auto coords = QPDFObjectHandle::newArray();
    auto extend = QPDFObjectHandle::newArray();

    shading.replaceKey(
        "/ShadingType",
        QPDFObjectHandle::newInteger(
            paint.kind == QUANTAPDF_COMPOSER_PAINT_LINEAR_GRADIENT_INTERNAL
                ? 2
                : 3));
    shading.replaceKey(
        "/ColorSpace", QPDFObjectHandle::newName("/DeviceRGB"));

    coords.appendItem(QPDFObjectHandle::newReal(
        paint.start.x, decimal_precision(paint.start.x)));
    coords.appendItem(QPDFObjectHandle::newReal(
        paint.start.y, decimal_precision(paint.start.y)));
    if (paint.kind ==
        QUANTAPDF_COMPOSER_PAINT_RADIAL_GRADIENT_INTERNAL) {
        coords.appendItem(QPDFObjectHandle::newReal(
            paint.start_radius, decimal_precision(paint.start_radius)));
    }
    coords.appendItem(QPDFObjectHandle::newReal(
        paint.end.x, decimal_precision(paint.end.x)));
    coords.appendItem(QPDFObjectHandle::newReal(
        paint.end.y, decimal_precision(paint.end.y)));
    if (paint.kind ==
        QUANTAPDF_COMPOSER_PAINT_RADIAL_GRADIENT_INTERNAL) {
        coords.appendItem(QPDFObjectHandle::newReal(
            paint.end_radius, decimal_precision(paint.end_radius)));
    }
    shading.replaceKey("/Coords", coords);
    shading.replaceKey("/Function", make_gradient_function(pdf, paint));
    extend.appendItem(QPDFObjectHandle::newBool(true));
    extend.appendItem(QPDFObjectHandle::newBool(true));
    shading.replaceKey("/Extend", extend);
    return pdf.makeIndirectObject(shading);
}

QPDFObjectHandle make_gradient_pattern(
    QPDF& pdf,
    QPDFObjectHandle shading,
    quantapdf_composer_paint_state const& paint,
    double page_height)
{
    auto pattern = QPDFObjectHandle::newDictionary();
    auto matrix = QPDFObjectHandle::newArray();

    pattern.replaceKey("/Type", QPDFObjectHandle::newName("/Pattern"));
    pattern.replaceKey("/PatternType", QPDFObjectHandle::newInteger(2));
    pattern.replaceKey("/Shading", shading);

    matrix.appendItem(QPDFObjectHandle::newReal(
        canonical_zero(paint.transform.a),
        decimal_precision(paint.transform.a)));
    matrix.appendItem(QPDFObjectHandle::newReal(
        canonical_zero(-static_cast<double>(paint.transform.b)),
        decimal_precision(paint.transform.b)));
    matrix.appendItem(QPDFObjectHandle::newReal(
        canonical_zero(paint.transform.c),
        decimal_precision(paint.transform.c)));
    matrix.appendItem(QPDFObjectHandle::newReal(
        canonical_zero(-static_cast<double>(paint.transform.d)),
        decimal_precision(paint.transform.d)));
    matrix.appendItem(QPDFObjectHandle::newReal(
        canonical_zero(paint.transform.e),
        decimal_precision(paint.transform.e)));
    double const pdf_f =
        page_height - static_cast<double>(paint.transform.f);
    matrix.appendItem(QPDFObjectHandle::newReal(
        canonical_zero(pdf_f), decimal_precision(pdf_f)));
    pattern.replaceKey("/Matrix", matrix);
    return pdf.makeIndirectObject(pattern);
}

QPDFObjectHandle make_tiling_pattern(
    QPDF& pdf,
    QPDFObjectHandle tile_form,
    quantapdf_composer_paint_state const& paint,
    double page_height)
{
    auto pattern = pdf.newStream("q /Tile Do Q\n");
    auto dictionary = pattern.getDict();
    auto bbox = QPDFObjectHandle::newArray();
    auto resources = QPDFObjectHandle::newDictionary();
    auto xobjects = QPDFObjectHandle::newDictionary();
    auto matrix = QPDFObjectHandle::newArray();

    dictionary.replaceKey("/Type", QPDFObjectHandle::newName("/Pattern"));
    dictionary.replaceKey("/PatternType", QPDFObjectHandle::newInteger(1));
    dictionary.replaceKey("/PaintType", QPDFObjectHandle::newInteger(1));
    dictionary.replaceKey("/TilingType", QPDFObjectHandle::newInteger(1));

    bbox.appendItem(QPDFObjectHandle::newInteger(0));
    bbox.appendItem(QPDFObjectHandle::newInteger(0));
    bbox.appendItem(QPDFObjectHandle::newReal(
        paint.tile_width, decimal_precision(paint.tile_width)));
    bbox.appendItem(QPDFObjectHandle::newReal(
        paint.tile_height, decimal_precision(paint.tile_height)));
    dictionary.replaceKey("/BBox", bbox);
    dictionary.replaceKey(
        "/XStep",
        QPDFObjectHandle::newReal(
            paint.x_step, decimal_precision(paint.x_step)));
    dictionary.replaceKey(
        "/YStep",
        QPDFObjectHandle::newReal(
            -static_cast<double>(paint.y_step),
            decimal_precision(paint.y_step)));

    xobjects.replaceKey("/Tile", tile_form);
    resources.replaceKey("/XObject", xobjects);
    dictionary.replaceKey("/Resources", resources);

    double const a = canonical_zero(paint.transform.a);
    double const b =
        canonical_zero(-static_cast<double>(paint.transform.b));
    double const c_value =
        canonical_zero(-static_cast<double>(paint.transform.c));
    double const d = canonical_zero(paint.transform.d);
    double const e =
        static_cast<double>(paint.transform.c) * paint.tile_height +
        static_cast<double>(paint.transform.e);
    double const f =
        page_height -
        static_cast<double>(paint.transform.d) * paint.tile_height -
        static_cast<double>(paint.transform.f);
    matrix.appendItem(QPDFObjectHandle::newReal(
        a, decimal_precision(a)));
    matrix.appendItem(QPDFObjectHandle::newReal(
        b, decimal_precision(b)));
    matrix.appendItem(QPDFObjectHandle::newReal(
        c_value, decimal_precision(c_value)));
    matrix.appendItem(QPDFObjectHandle::newReal(
        d, decimal_precision(d)));
    matrix.appendItem(QPDFObjectHandle::newReal(
        canonical_zero(e), decimal_precision(e)));
    matrix.appendItem(QPDFObjectHandle::newReal(
        canonical_zero(f), decimal_precision(f)));
    dictionary.replaceKey("/Matrix", matrix);
    return pattern;
}

void append_text_matrix(
    std::string& content,
    quantapdf_composer_page_state const& page,
    quantapdf_affine_transform const& transform,
    double local_x,
    double local_y)
{
    double const display_x =
        static_cast<double>(transform.a) * local_x +
        static_cast<double>(transform.c) * local_y +
        static_cast<double>(transform.e);
    double const display_y =
        static_cast<double>(transform.b) * local_x +
        static_cast<double>(transform.d) * local_y +
        static_cast<double>(transform.f);
    double const pdf_a = canonical_zero(transform.a);
    double const pdf_b = canonical_zero(-static_cast<double>(transform.b));
    double const pdf_c = canonical_zero(-static_cast<double>(transform.c));
    double const pdf_d = canonical_zero(transform.d);
    double const pdf_x = canonical_zero(display_x);
    double const pdf_y = canonical_zero(page.height_points - display_y);

    content += number(pdf_a) + " " + number(pdf_b) + " " +
        number(pdf_c) + " " + number(pdf_d) + " " +
        number(pdf_x) + " " + number(pdf_y) + " Tm ";
}

char const* base_font_name(quantapdf_composer_font font)
{
    static char const* const names[] = {
        "/Helvetica", "/Helvetica-Bold", "/Helvetica-Oblique",
        "/Helvetica-BoldOblique", "/Times-Roman", "/Times-Bold",
        "/Times-Italic", "/Times-BoldItalic", "/Courier",
        "/Courier-Bold", "/Courier-Oblique", "/Courier-BoldOblique"};
    return names[static_cast<int>(font)];
}

std::string pdf_string(std::string const& value)
{
    std::string result = "(";
    char buffer[5];
    for (unsigned char byte: value) {
        if (byte == '(' || byte == ')' || byte == '\\') {
            result.push_back('\\');
            result.push_back(static_cast<char>(byte));
        } else if (byte < 0x20 || byte >= 0x7f) {
            std::snprintf(buffer, sizeof(buffer), "\\%03o", byte);
            result += buffer;
        } else {
            result.push_back(static_cast<char>(byte));
        }
    }
    result.push_back(')');
    return result;
}

QPDFObjectHandle make_font(quantapdf_composer_font font)
{
    auto dictionary = QPDFObjectHandle::newDictionary();
    dictionary.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    dictionary.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
    dictionary.replaceKey(
        "/BaseFont", QPDFObjectHandle::newName(base_font_name(font)));
    dictionary.replaceKey(
        "/Encoding", QPDFObjectHandle::newName("/WinAnsiEncoding"));
    return dictionary;
}

void append_text_content(
    std::string& content,
    quantapdf_composer_page_state const& page,
    quantapdf_composer_operation const& operation)
{
    auto const& options = operation.value.text.options;
    auto const& transform = operation.value.text.transform;
    std::vector<quantapdf::detail::base14_text_line> lines;
    quantapdf_status const layout_status =
        quantapdf::detail::layout_base14_text(
            operation.value.text.text_utf8,
            options,
            operation.bounds.x1 - operation.bounds.x0,
            &lines);
    if (layout_status == QUANTAPDF_ERROR_NOMEM)
        throw std::bad_alloc();
    if (layout_status == QUANTAPDF_ERROR_FORMAT)
        throw std::invalid_argument("validated Base-14 text layout failed");
    if (layout_status != QUANTAPDF_OK)
        throw std::logic_error("Base-14 text layout failed");

    double const line_height =
        options.font_size * options.line_height_multiplier;
    double baseline_y = operation.bounds.y0 + options.font_size;
    double const red = ((options.argb >> 16u) & 0xffu) / 255.0;
    double const green = ((options.argb >> 8u) & 0xffu) / 255.0;
    double const blue = (options.argb & 0xffu) / 255.0;
    for (auto const& line: lines) {
        double x = operation.bounds.x0;
        if (options.alignment == QUANTAPDF_COMPOSER_TEXT_ALIGN_CENTER)
            x += (operation.bounds.x1 - operation.bounds.x0 -
                  line.width_points) /
                2.0;
        else if (options.alignment == QUANTAPDF_COMPOSER_TEXT_ALIGN_RIGHT)
            x = operation.bounds.x1 - line.width_points;
        if (baseline_y > operation.bounds.y1)
            break;
        content += "BT /F" + std::to_string(static_cast<int>(options.font)) +
            " " + number(options.font_size) + " Tf " + number(red) + " " +
            number(green) + " " + number(blue) + " rg ";
        append_text_matrix(content, page, transform, x, baseline_y);
        content += pdf_string(line.text) + " Tj ET\n";
        baseline_y += line_height;
    }
}


struct embedded_font_usage {
    bool referenced = false;
    std::map<uint16_t, uint32_t> glyph_to_unicode;
};

std::string glyph_hex(
    std::vector<quantapdf::detail::embedded_glyph_item> const& glyphs)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << '<' << std::uppercase << std::hex << std::setfill('0');
    for (auto const& glyph: glyphs)
        out << std::setw(4) << static_cast<unsigned int>(glyph.glyph);
    out << '>';
    return out.str();
}

void append_embedded_text_content(
    std::string& content,
    quantapdf_composer_page_state const& page,
    quantapdf_composer_operation const& operation,
    std::vector<quantapdf::detail::ttf_font_face> const& faces)
{
    auto const& options = operation.value.embedded_text.options;
    auto const& transform = operation.value.embedded_text.transform;
    auto const& face = faces[options.font_id - 1u];
    std::vector<quantapdf::detail::embedded_text_line> lines;
    quantapdf_status const layout_status =
        quantapdf::detail::layout_embedded_text(
            operation.value.embedded_text.text_utf8,
            options,
            face,
            operation.bounds.x1 - operation.bounds.x0,
            &lines);
    if (layout_status == QUANTAPDF_ERROR_NOMEM)
        throw std::bad_alloc();
    if (layout_status == QUANTAPDF_ERROR_FORMAT)
        throw std::invalid_argument("validated embedded text layout failed");
    if (layout_status != QUANTAPDF_OK)
        throw std::logic_error("embedded text layout failed");

    double const line_height =
        options.font_size * options.line_height_multiplier;
    double baseline_y = operation.bounds.y0 + options.font_size;
    double const red = ((options.argb >> 16u) & 0xffu) / 255.0;
    double const green = ((options.argb >> 8u) & 0xffu) / 255.0;
    double const blue = (options.argb & 0xffu) / 255.0;

    for (auto const& line: lines) {
        double x = operation.bounds.x0;
        if (options.alignment == QUANTAPDF_COMPOSER_TEXT_ALIGN_CENTER)
            x += (operation.bounds.x1 - operation.bounds.x0 -
                  line.width_points) /
                2.0;
        else if (options.alignment == QUANTAPDF_COMPOSER_TEXT_ALIGN_RIGHT)
            x = operation.bounds.x1 - line.width_points;
        if (baseline_y > operation.bounds.y1)
            break;
        content +=
            "BT /EF" + std::to_string(options.font_id) + " " +
            number(options.font_size) + " Tf " +
            number(red) + " " + number(green) + " " + number(blue) + " rg ";
        append_text_matrix(content, page, transform, x, baseline_y);
        content += glyph_hex(line.glyphs) + " Tj ET\n";
        baseline_y += line_height;
    }
}

std::string unicode_hex(uint32_t codepoint)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::uppercase << std::hex << std::setfill('0');
    if (codepoint <= 0xffffu) {
        out << std::setw(4) << codepoint;
    } else {
        uint32_t const value = codepoint - 0x10000u;
        uint16_t const high =
            static_cast<uint16_t>(0xd800u + (value >> 10u));
        uint16_t const low =
            static_cast<uint16_t>(0xdc00u + (value & 0x3ffu));
        out << std::setw(4) << high << std::setw(4) << low;
    }
    return out.str();
}

std::string make_to_unicode_cmap(
    size_t font_index,
    embedded_font_usage const& usage)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "/CIDInit /ProcSet findresource begin\n"
        << "12 dict begin\n"
        << "begincmap\n"
        << "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) "
           "/Supplement 0 >> def\n"
        << "/CMapName /QuantaPDF-EF" << (font_index + 1u)
        << "-UCS def\n"
        << "/CMapType 2 def\n"
        << "1 begincodespacerange\n"
        << "<0000> <FFFF>\n"
        << "endcodespacerange\n";

    auto it = usage.glyph_to_unicode.begin();
    while (it != usage.glyph_to_unicode.end()) {
        size_t count = 0u;
        auto block_end = it;
        while (block_end != usage.glyph_to_unicode.end() && count < 100u) {
            ++block_end;
            ++count;
        }
        out << count << " beginbfchar\n";
        for (; it != block_end; ++it) {
            out << '<' << std::uppercase << std::hex << std::setfill('0')
                << std::setw(4) << static_cast<unsigned int>(it->first)
                << "> <" << unicode_hex(it->second) << ">\n";
        }
        out << "endbfchar\n";
    }
    out << "endcmap\n"
        << "CMapName currentdict /CMap defineresource pop\n"
        << "end\n"
        << "end\n";
    return out.str();
}

void collect_embedded_font_usage(
    quantapdf_composer const* composer,
    std::vector<quantapdf::detail::ttf_font_face> const& faces,
    std::vector<embedded_font_usage>* usages)
{
    for (size_t i = 0u; i < composer->operation_count; ++i) {
        auto const& operation = composer->operations[i];
        if (operation.kind != QUANTAPDF_COMPOSER_OPERATION_EMBEDDED_TEXT)
            continue;
        auto const& options = operation.value.embedded_text.options;
        size_t const index = options.font_id - 1u;
        auto& usage = (*usages)[index];
        usage.referenced = true;
        std::vector<uint32_t> codepoints;
        if (quantapdf::detail::decode_utf8_codepoints(
                operation.value.embedded_text.text_utf8, &codepoints) !=
            QUANTAPDF_OK)
            throw std::invalid_argument("invalid embedded text utf8");
        for (uint32_t cp: codepoints) {
            if (cp == '\n' || cp == '\r')
                continue;
            if (cp == '\t')
                cp = 0x20u;
            uint16_t const glyph = faces[index].glyph_for(cp);
            if (glyph == 0u)
                throw std::invalid_argument("embedded font missing glyph");
            usage.glyph_to_unicode.emplace(glyph, cp);
        }
    }
}

QPDFObjectHandle make_embedded_font(
    QPDF& pdf,
    quantapdf_composer_font_state const& state,
    quantapdf::detail::ttf_font_face const& face,
    embedded_font_usage const& usage,
    size_t font_index)
{
    bool const true_type =
        face.outline_kind == quantapdf::detail::sfnt_outline_kind::true_type;
    std::vector<unsigned char> subset;
    unsigned char const* embedded_data = state.data;
    size_t embedded_size = state.size;
    if (true_type) {
        quantapdf_status const subset_status =
            quantapdf::detail::subset_true_type_font(
                face, usage.glyph_to_unicode, &subset);
        if (subset_status == QUANTAPDF_OK &&
            !subset.empty() && subset.size() < state.size) {
            embedded_data = subset.data();
            embedded_size = subset.size();
        }
    }

    auto font_file = pdf.newStream(std::string(
        reinterpret_cast<char const*>(embedded_data), embedded_size));
    if (true_type) {
        font_file.getDict().replaceKey(
            "/Length1",
            QPDFObjectHandle::newInteger(
                static_cast<long long>(embedded_size)));
    } else {
        font_file.getDict().replaceKey(
            "/Subtype", QPDFObjectHandle::newName("/OpenType"));
    }

    auto descriptor = QPDFObjectHandle::newDictionary();
    descriptor.replaceKey("/Type", QPDFObjectHandle::newName("/FontDescriptor"));
    descriptor.replaceKey(
        "/FontName", QPDFObjectHandle::newName("/" + face.postscript_name));
    descriptor.replaceKey("/Flags", QPDFObjectHandle::newInteger(face.flags));

    auto bbox = QPDFObjectHandle::newArray();
    for (int i = 0; i < 4; ++i) {
        double const value =
            static_cast<double>(face.bbox[i]) * 1000.0 / face.units_per_em;
        bbox.appendItem(
            QPDFObjectHandle::newReal(value, decimal_precision(value)));
    }
    descriptor.replaceKey("/FontBBox", bbox);
    descriptor.replaceKey(
        "/ItalicAngle",
        QPDFObjectHandle::newReal(
            face.italic_angle, decimal_precision(face.italic_angle)));
    descriptor.replaceKey(
        "/Ascent",
        QPDFObjectHandle::newReal(
            face.ascent, decimal_precision(face.ascent)));
    descriptor.replaceKey(
        "/Descent",
        QPDFObjectHandle::newReal(
            face.descent, decimal_precision(face.descent)));
    descriptor.replaceKey(
        "/CapHeight",
        QPDFObjectHandle::newReal(
            face.cap_height, decimal_precision(face.cap_height)));
    descriptor.replaceKey(
        "/StemV",
        QPDFObjectHandle::newReal(
            face.stem_v, decimal_precision(face.stem_v)));
    descriptor.replaceKey(
        true_type ? "/FontFile2" : "/FontFile3", font_file);
    auto descriptor_ref = pdf.makeIndirectObject(descriptor);

    auto system_info = QPDFObjectHandle::newDictionary();
    system_info.replaceKey("/Registry", QPDFObjectHandle::newString("Adobe"));
    system_info.replaceKey("/Ordering", QPDFObjectHandle::newString("Identity"));
    system_info.replaceKey("/Supplement", QPDFObjectHandle::newInteger(0));

    auto widths = QPDFObjectHandle::newArray();
    for (auto const& entry: usage.glyph_to_unicode) {
        auto one_width = QPDFObjectHandle::newArray();
        one_width.appendItem(
            QPDFObjectHandle::newInteger(face.width_for(entry.first)));
        widths.appendItem(QPDFObjectHandle::newInteger(entry.first));
        widths.appendItem(one_width);
    }

    auto descendant = QPDFObjectHandle::newDictionary();
    descendant.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    descendant.replaceKey(
        "/Subtype",
        QPDFObjectHandle::newName(
            true_type ? "/CIDFontType2" : "/CIDFontType0"));
    descendant.replaceKey(
        "/BaseFont", QPDFObjectHandle::newName("/" + face.postscript_name));
    descendant.replaceKey("/CIDSystemInfo", system_info);
    int const default_width = std::max(1, face.width_for(0u));
    descendant.replaceKey(
        "/DW", QPDFObjectHandle::newInteger(default_width));
    if (widths.getArrayNItems() != 0)
        descendant.replaceKey("/W", widths);
    descendant.replaceKey("/FontDescriptor", descriptor_ref);
    if (true_type) {
        descendant.replaceKey(
            "/CIDToGIDMap", QPDFObjectHandle::newName("/Identity"));
    }
    auto descendant_ref = pdf.makeIndirectObject(descendant);

    auto descendants = QPDFObjectHandle::newArray();
    descendants.appendItem(descendant_ref);

    auto to_unicode = pdf.newStream(
        make_to_unicode_cmap(font_index, usage));

    auto type0 = QPDFObjectHandle::newDictionary();
    type0.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    type0.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type0"));
    type0.replaceKey(
        "/BaseFont", QPDFObjectHandle::newName("/" + face.postscript_name));
    type0.replaceKey("/Encoding", QPDFObjectHandle::newName("/Identity-H"));
    type0.replaceKey("/DescendantFonts", descendants);
    type0.replaceKey("/ToUnicode", to_unicode);
    return pdf.makeIndirectObject(type0);
}


struct glyph_run_font_entry {
    uint16_t cid = 0u;
    uint16_t glyph = 0u;
    std::string unicode_utf8;
};

struct glyph_run_font_usage {
    bool referenced = false;
    std::map<std::pair<uint16_t, std::string>, uint16_t> key_to_cid;
    std::map<uint16_t, std::string> cff_gid_unicode;
    std::vector<glyph_run_font_entry> entries;
    std::map<uint16_t, uint32_t> subset_glyphs;
};

uint32_t first_utf8_scalar(std::string const& value, uint16_t glyph)
{
    if (value.empty())
        return 0xf0000u + glyph;

    auto const* data =
        reinterpret_cast<unsigned char const*>(value.data());
    size_t const size = value.size();
    unsigned char const first = data[0];
    uint32_t codepoint;
    size_t count;
    if (first < 0x80u) {
        codepoint = first;
        count = 1u;
    } else if (first >= 0xc2u && first <= 0xdfu) {
        codepoint = first & 0x1fu;
        count = 2u;
    } else if (first >= 0xe0u && first <= 0xefu) {
        codepoint = first & 0x0fu;
        count = 3u;
    } else if (first >= 0xf0u && first <= 0xf4u) {
        codepoint = first & 0x07u;
        count = 4u;
    } else {
        throw std::invalid_argument("invalid glyph-run unicode");
    }
    if (count > size)
        throw std::invalid_argument("truncated glyph-run unicode");
    for (size_t i = 1u; i < count; ++i) {
        if ((data[i] & 0xc0u) != 0x80u)
            throw std::invalid_argument("invalid glyph-run unicode");
        codepoint =
            (codepoint << 6u) | static_cast<uint32_t>(data[i] & 0x3fu);
    }
    return codepoint;
}

std::string utf8_sequence_hex(std::string const& value)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::uppercase << std::hex << std::setfill('0');

    auto const* data =
        reinterpret_cast<unsigned char const*>(value.data());
    size_t offset = 0u;
    while (offset < value.size()) {
        unsigned char const first = data[offset];
        uint32_t codepoint;
        size_t count;
        if (first < 0x80u) {
            codepoint = first;
            count = 1u;
        } else if (first >= 0xc2u && first <= 0xdfu) {
            codepoint = first & 0x1fu;
            count = 2u;
        } else if (first >= 0xe0u && first <= 0xefu) {
            codepoint = first & 0x0fu;
            count = 3u;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            codepoint = first & 0x07u;
            count = 4u;
        } else {
            throw std::invalid_argument("invalid glyph-run unicode");
        }
        if (count > value.size() - offset)
            throw std::invalid_argument("truncated glyph-run unicode");
        for (size_t i = 1u; i < count; ++i) {
            unsigned char const byte = data[offset + i];
            if ((byte & 0xc0u) != 0x80u)
                throw std::invalid_argument("invalid glyph-run unicode");
            codepoint =
                (codepoint << 6u) |
                static_cast<uint32_t>(byte & 0x3fu);
        }
        if (codepoint <= 0xffffu) {
            out << std::setw(4) << codepoint;
        } else {
            uint32_t const adjusted = codepoint - 0x10000u;
            uint16_t const high =
                static_cast<uint16_t>(0xd800u + (adjusted >> 10u));
            uint16_t const low =
                static_cast<uint16_t>(0xdc00u + (adjusted & 0x3ffu));
            out << std::setw(4) << high << std::setw(4) << low;
        }
        offset += count;
    }
    return out.str();
}

void collect_glyph_run_font_usage(
    quantapdf_composer const* composer,
    std::vector<quantapdf::detail::ttf_font_face> const& faces,
    std::vector<glyph_run_font_usage>* usages)
{
    for (size_t op_index = 0u; op_index < composer->operation_count;
         ++op_index) {
        auto const& operation = composer->operations[op_index];
        if (operation.kind != QUANTAPDF_COMPOSER_OPERATION_GLYPH_RUN)
            continue;

        auto const& run = operation.value.glyph_run;
        size_t const font_index = run.options.font_id - 1u;
        if (font_index >= usages->size())
            throw std::logic_error("glyph-run font id out of range");
        auto& usage = (*usages)[font_index];
        usage.referenced = true;

        for (size_t i = 0u; i < run.glyph_count; ++i) {
            auto const& glyph = run.glyphs[i];
            if (glyph.glyph_id >= faces[font_index].num_glyphs)
                throw std::invalid_argument("glyph-run glyph id out of range");
            std::string cluster;
            if (glyph.unicode_length != 0u) {
                cluster.assign(
                    run.unicode_utf8 + glyph.unicode_offset,
                    glyph.unicode_length);
            }
            auto key = std::make_pair(
                static_cast<uint16_t>(glyph.glyph_id),
                cluster);
            auto found = usage.key_to_cid.find(key);
            if (found == usage.key_to_cid.end()) {
                uint16_t cid = 0u;
                bool const true_type =
                    faces[font_index].outline_kind ==
                    quantapdf::detail::sfnt_outline_kind::true_type;
                if (true_type) {
                    if (usage.entries.size() >= 65535u)
                        throw std::length_error("too many glyph-run CIDs");
                    cid = static_cast<uint16_t>(
                        usage.entries.size() + 1u);
                } else {
                    uint16_t const gid =
                        static_cast<uint16_t>(glyph.glyph_id);
                    auto const existing =
                        usage.cff_gid_unicode.find(gid);
                    if (existing != usage.cff_gid_unicode.end() &&
                        existing->second != cluster)
                        throw std::length_error(
                            "CFF glyph cannot map to multiple clusters");
                    usage.cff_gid_unicode.emplace(gid, cluster);
                    cid = gid;
                }
                usage.key_to_cid.emplace(key, cid);
                usage.entries.push_back({
                    cid,
                    static_cast<uint16_t>(glyph.glyph_id),
                    cluster});
            }
            usage.subset_glyphs.emplace(
                static_cast<uint16_t>(glyph.glyph_id),
                first_utf8_scalar(
                    cluster,
                    static_cast<uint16_t>(glyph.glyph_id)));
        }
    }
}

std::string make_glyph_run_to_unicode_cmap(
    size_t font_index,
    glyph_run_font_usage const& usage)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "/CIDInit /ProcSet findresource begin\n"
        << "12 dict begin\n"
        << "begincmap\n"
        << "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) "
           "/Supplement 0 >> def\n"
        << "/CMapName /QuantaPDF-GR" << (font_index + 1u)
        << "-UCS def\n"
        << "/CMapType 2 def\n"
        << "1 begincodespacerange\n"
        << "<0000> <FFFF>\n"
        << "endcodespacerange\n";

    size_t position = 0u;
    while (position < usage.entries.size()) {
        size_t count = 0u;
        size_t cursor = position;
        while (cursor < usage.entries.size() && count < 100u) {
            if (!usage.entries[cursor].unicode_utf8.empty())
                ++count;
            ++cursor;
        }
        if (count == 0u) {
            position = cursor;
            continue;
        }
        out << count << " beginbfchar\n";
        for (; position < cursor; ++position) {
            auto const& entry = usage.entries[position];
            if (entry.unicode_utf8.empty())
                continue;
            out << '<' << std::uppercase << std::hex << std::setfill('0')
                << std::setw(4) << static_cast<unsigned int>(entry.cid)
                << "> <" << utf8_sequence_hex(entry.unicode_utf8) << ">\n";
        }
        out << "endbfchar\n";
    }

    out << "endcmap\n"
        << "CMapName currentdict /CMap defineresource pop\n"
        << "end\n"
        << "end\n";
    return out.str();
}

QPDFObjectHandle make_glyph_run_font(
    QPDF& pdf,
    quantapdf_composer_font_state const& state,
    quantapdf::detail::ttf_font_face const& face,
    glyph_run_font_usage const& usage,
    size_t font_index)
{
    bool const true_type =
        face.outline_kind == quantapdf::detail::sfnt_outline_kind::true_type;
    std::vector<unsigned char> subset;
    unsigned char const* embedded_data = state.data;
    size_t embedded_size = state.size;
    if (true_type) {
        quantapdf_status const subset_status =
            quantapdf::detail::subset_true_type_font(
                face, usage.subset_glyphs, &subset);
        if (subset_status == QUANTAPDF_OK &&
            !subset.empty() && subset.size() < state.size) {
            embedded_data = subset.data();
            embedded_size = subset.size();
        }
    }

    auto font_file = pdf.newStream(std::string(
        reinterpret_cast<char const*>(embedded_data), embedded_size));
    if (true_type) {
        font_file.getDict().replaceKey(
            "/Length1",
            QPDFObjectHandle::newInteger(
                static_cast<long long>(embedded_size)));
    } else {
        font_file.getDict().replaceKey(
            "/Subtype", QPDFObjectHandle::newName("/OpenType"));
    }

    auto descriptor = QPDFObjectHandle::newDictionary();
    descriptor.replaceKey("/Type", QPDFObjectHandle::newName("/FontDescriptor"));
    descriptor.replaceKey(
        "/FontName", QPDFObjectHandle::newName("/" + face.postscript_name));
    descriptor.replaceKey("/Flags", QPDFObjectHandle::newInteger(face.flags));

    auto bbox = QPDFObjectHandle::newArray();
    for (int i = 0; i < 4; ++i) {
        double const value =
            static_cast<double>(face.bbox[i]) * 1000.0 / face.units_per_em;
        bbox.appendItem(
            QPDFObjectHandle::newReal(value, decimal_precision(value)));
    }
    descriptor.replaceKey("/FontBBox", bbox);
    descriptor.replaceKey(
        "/ItalicAngle",
        QPDFObjectHandle::newReal(
            face.italic_angle, decimal_precision(face.italic_angle)));
    descriptor.replaceKey(
        "/Ascent",
        QPDFObjectHandle::newReal(
            face.ascent, decimal_precision(face.ascent)));
    descriptor.replaceKey(
        "/Descent",
        QPDFObjectHandle::newReal(
            face.descent, decimal_precision(face.descent)));
    descriptor.replaceKey(
        "/CapHeight",
        QPDFObjectHandle::newReal(
            face.cap_height, decimal_precision(face.cap_height)));
    descriptor.replaceKey(
        "/StemV",
        QPDFObjectHandle::newReal(
            face.stem_v, decimal_precision(face.stem_v)));
    descriptor.replaceKey(
        true_type ? "/FontFile2" : "/FontFile3", font_file);
    auto descriptor_ref = pdf.makeIndirectObject(descriptor);

    auto system_info = QPDFObjectHandle::newDictionary();
    system_info.replaceKey("/Registry", QPDFObjectHandle::newString("Adobe"));
    system_info.replaceKey("/Ordering", QPDFObjectHandle::newString("Identity"));
    system_info.replaceKey("/Supplement", QPDFObjectHandle::newInteger(0));

    auto widths = QPDFObjectHandle::newArray();
    for (auto const& entry: usage.entries) {
        auto one_width = QPDFObjectHandle::newArray();
        one_width.appendItem(
            QPDFObjectHandle::newInteger(face.width_for(entry.glyph)));
        widths.appendItem(QPDFObjectHandle::newInteger(entry.cid));
        widths.appendItem(one_width);
    }

    std::optional<QPDFObjectHandle> cid_to_gid;
    if (true_type) {
        std::string cid_to_gid_bytes(
            (usage.entries.size() + 1u) * 2u, '\0');
        for (auto const& entry: usage.entries) {
            size_t const at = static_cast<size_t>(entry.cid) * 2u;
            cid_to_gid_bytes[at] =
                static_cast<char>(entry.glyph >> 8u);
            cid_to_gid_bytes[at + 1u] =
                static_cast<char>(entry.glyph & 0xffu);
        }
        cid_to_gid = pdf.newStream(cid_to_gid_bytes);
    }

    auto descendant = QPDFObjectHandle::newDictionary();
    descendant.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    descendant.replaceKey(
        "/Subtype",
        QPDFObjectHandle::newName(
            true_type ? "/CIDFontType2" : "/CIDFontType0"));
    descendant.replaceKey(
        "/BaseFont", QPDFObjectHandle::newName("/" + face.postscript_name));
    descendant.replaceKey("/CIDSystemInfo", system_info);
    descendant.replaceKey(
        "/DW", QPDFObjectHandle::newInteger(std::max(1, face.width_for(0u))));
    if (widths.getArrayNItems() != 0)
        descendant.replaceKey("/W", widths);
    descendant.replaceKey("/FontDescriptor", descriptor_ref);
    if (cid_to_gid.has_value())
        descendant.replaceKey("/CIDToGIDMap", *cid_to_gid);
    auto descendant_ref = pdf.makeIndirectObject(descendant);

    auto descendants = QPDFObjectHandle::newArray();
    descendants.appendItem(descendant_ref);

    auto to_unicode = pdf.newStream(
        make_glyph_run_to_unicode_cmap(font_index, usage));

    auto type0 = QPDFObjectHandle::newDictionary();
    type0.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    type0.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type0"));
    type0.replaceKey(
        "/BaseFont", QPDFObjectHandle::newName("/" + face.postscript_name));
    type0.replaceKey("/Encoding", QPDFObjectHandle::newName("/Identity-H"));
    type0.replaceKey("/DescendantFonts", descendants);
    type0.replaceKey("/ToUnicode", to_unicode);
    return pdf.makeIndirectObject(type0);
}

std::string glyph_run_cid_hex(uint16_t cid)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << '<' << std::uppercase << std::hex << std::setfill('0')
        << std::setw(4) << static_cast<unsigned int>(cid) << '>';
    return out.str();
}

void append_glyph_run_content(
    std::string& content,
    quantapdf_composer_page_state const& page,
    quantapdf_composer_operation const& operation,
    glyph_run_font_usage const& usage)
{
    auto const& run = operation.value.glyph_run;
    auto const& options = run.options;
    auto const& transform = run.transform;
    double const scale = options.font_size / 1000.0;
    double pen_x = run.origin.x;
    double pen_y = run.origin.y;
    double const red = ((options.argb >> 16u) & 0xffu) / 255.0;
    double const green = ((options.argb >> 8u) & 0xffu) / 255.0;
    double const blue = (options.argb & 0xffu) / 255.0;

    content +=
        "BT /GR" + std::to_string(options.font_id) + " " +
        number(options.font_size) + " Tf " +
        number(red) + " " + number(green) + " " + number(blue) + " rg ";

    for (size_t i = 0u; i < run.glyph_count; ++i) {
        auto const& glyph = run.glyphs[i];
        std::string cluster;
        if (glyph.unicode_length != 0u) {
            cluster.assign(
                run.unicode_utf8 + glyph.unicode_offset,
                glyph.unicode_length);
        }
        auto const found = usage.key_to_cid.find(std::make_pair(
            static_cast<uint16_t>(glyph.glyph_id), cluster));
        if (found == usage.key_to_cid.end())
            throw std::logic_error("glyph-run CID assignment missing");

        double const x =
            pen_x + static_cast<double>(glyph.x_offset) * scale;
        double const y =
            pen_y + static_cast<double>(glyph.y_offset) * scale;
        append_text_matrix(content, page, transform, x, y);
        content += glyph_run_cid_hex(found->second) + " Tj ";
        pen_x += static_cast<double>(glyph.x_advance) * scale;
        pen_y += static_cast<double>(glyph.y_advance) * scale;
    }
    content += "ET\n";
}

void append_image_content(
    std::string& content,
    quantapdf_composer const* composer,
    quantapdf_composer_page_state const& page,
    quantapdf_composer_operation const& operation)
{
    auto const& image =
        composer->images[operation.value.image.image_id - 1u];
    double const box_width = operation.bounds.x1 - operation.bounds.x0;
    double const box_height = operation.bounds.y1 - operation.bounds.y0;
    double draw_width = box_width;
    double draw_height = box_height;
    double draw_x = operation.bounds.x0;
    double draw_top = operation.bounds.y0;
    bool const clip = operation.value.image.options.fit ==
        QUANTAPDF_COMPOSER_IMAGE_FIT_COVER;
    if (operation.value.image.options.fit !=
        QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH) {
        double const scale_x = box_width / image.width;
        double const scale_y = box_height / image.height;
        double const scale = operation.value.image.options.fit ==
                QUANTAPDF_COMPOSER_IMAGE_FIT_CONTAIN
            ? std::min(scale_x, scale_y)
            : std::max(scale_x, scale_y);
        draw_width = image.width * scale;
        draw_height = image.height * scale;
        draw_x += (box_width - draw_width) / 2.0;
        draw_top += (box_height - draw_height) / 2.0;
    }
    content += "q ";
    if (clip) {
        content += number(operation.bounds.x0) + " " +
            number(page.height_points - operation.bounds.y1) + " " +
            number(box_width) + " " + number(box_height) + " re W n ";
    }
    content += number(draw_width) + " 0 0 " + number(draw_height) + " " +
        number(draw_x) + " " +
        number(page.height_points - draw_top - draw_height) + " cm /Im" +
        std::to_string(operation.value.image.image_id) + " Do Q\n";
}


void append_path_content(
    std::string& content,
    quantapdf_composer_page_state const& page,
    quantapdf_composer_operation const& operation)
{
    auto const& path = operation.value.path;
    auto const& options = path.options;
    auto append_color = [&](uint32_t argb, char const* op) {
        double const red = ((argb >> 16u) & 0xffu) / 255.0;
        double const green = ((argb >> 8u) & 0xffu) / 255.0;
        double const blue = (argb & 0xffu) / 255.0;
        content += number(red) + " " + number(green) + " " +
            number(blue) + " " + op + " ";
    };
    auto append_point = [&](quantapdf_point const& point) {
        content += number(point.x) + " " +
            number(page.height_points - point.y);
    };

    content += "q ";
    auto const& transform = options.transform;
    bool const identity_transform =
        options.struct_size < QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_MIN_SIZE ||
        (transform.a == 1.0f && transform.b == 0.0f &&
         transform.c == 0.0f && transform.d == 1.0f &&
         transform.e == 0.0f && transform.f == 0.0f);
    if (!identity_transform) {
        double const a = canonical_zero(transform.a);
        double const b =
            canonical_zero(-static_cast<double>(transform.b));
        double const c_value =
            canonical_zero(-static_cast<double>(transform.c));
        double const d = canonical_zero(transform.d);
        double const e =
            static_cast<double>(transform.c) * page.height_points +
            static_cast<double>(transform.e);
        double const f =
            page.height_points *
                (1.0 - static_cast<double>(transform.d)) -
            static_cast<double>(transform.f);
        content += number(a) + " " + number(b) + " " +
            number(c_value) + " " + number(d) + " " +
            number(canonical_zero(e)) + " " +
            number(canonical_zero(f)) + " cm ";
    }
    if (options.stroke) {
        if (options.stroke_paint_id != 0u) {
            content += "/Pattern CS /P" +
                std::to_string(options.stroke_paint_id) + " SCN ";
        } else {
            append_color(options.stroke_argb, "RG");
        }
        content += number(options.stroke_width) + " w " +
            std::to_string(static_cast<int>(options.line_cap)) + " J " +
            std::to_string(static_cast<int>(options.line_join)) + " j " +
            number(options.miter_limit) + " M ";
        if (path.dash_count != 0u) {
            content += "[";
            for (size_t i = 0u; i < path.dash_count; ++i) {
                if (i != 0u)
                    content += " ";
                content += number(path.dash_lengths[i]);
            }
            content += "] " + number(path.dash_phase) + " d ";
        }
    }
    if (options.fill) {
        if (options.fill_paint_id != 0u) {
            content += "/Pattern cs /P" +
                std::to_string(options.fill_paint_id) + " scn ";
        } else {
            append_color(options.fill_argb, "rg");
        }
    }

    for (size_t i = 0u; i < path.command_count; ++i) {
        auto const& command = path.commands[i];
        switch (command.kind) {
        case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
            append_point(command.point1);
            content += " m ";
            break;
        case QUANTAPDF_COMPOSER_PATH_LINE_TO:
            append_point(command.point1);
            content += " l ";
            break;
        case QUANTAPDF_COMPOSER_PATH_CUBIC_TO:
            append_point(command.point1);
            content += " ";
            append_point(command.point2);
            content += " ";
            append_point(command.point3);
            content += " c ";
            break;
        case QUANTAPDF_COMPOSER_PATH_CLOSE:
            content += "h ";
            break;
        }
    }

    if (options.stroke && options.fill)
        content += options.fill_rule == QUANTAPDF_COMPOSER_FILL_EVEN_ODD
            ? "B*"
            : "B";
    else if (options.fill)
        content += options.fill_rule == QUANTAPDF_COMPOSER_FILL_EVEN_ODD
            ? "f*"
            : "f";
    else
        content += "S";
    content += " Q\n";
}

void append_form_content(
    std::string& content,
    quantapdf_composer const* composer,
    quantapdf_composer_page_state const& page,
    quantapdf_composer_operation const& operation)
{
    auto const form_id = operation.value.form.form_id;
    if (form_id == 0u || form_id > composer->form_count)
        throw std::logic_error("form resource missing");
    auto const& form = composer->forms[form_id - 1u];
    auto const& transform = operation.value.form.transform;
    double const a = canonical_zero(transform.a);
    double const b = canonical_zero(-static_cast<double>(transform.b));
    double const c_value = canonical_zero(-static_cast<double>(transform.c));
    double const d = canonical_zero(transform.d);
    double const e =
        static_cast<double>(transform.c) * form.height_points +
        static_cast<double>(transform.e);
    double const f =
        page.height_points -
        static_cast<double>(transform.d) * form.height_points -
        static_cast<double>(transform.f);

    content += "q " +
        number(a) + " " + number(b) + " " +
        number(c_value) + " " + number(d) + " " +
        number(canonical_zero(e)) + " " +
        number(canonical_zero(f)) + " cm /Fm" +
        std::to_string(form_id) + " Do Q\n";
}


QPDFObjectHandle navigation_destination(
    std::vector<QPDFObjectHandle> const& pages,
    quantapdf_composer const* composer,
    size_t page_index,
    quantapdf_point target)
{
    auto destination = QPDFObjectHandle::newArray();
    double const pdf_y =
        composer->pages[page_index].height_points - target.y;
    destination.appendItem(pages[page_index]);
    destination.appendItem(QPDFObjectHandle::newName("/XYZ"));
    destination.appendItem(QPDFObjectHandle::newReal(
        target.x, decimal_precision(target.x)));
    destination.appendItem(QPDFObjectHandle::newReal(
        pdf_y, decimal_precision(pdf_y)));
    destination.appendItem(QPDFObjectHandle::newNull());
    return destination;
}

QPDFObjectHandle navigation_rectangle(
    quantapdf_composer_page_state const& page,
    quantapdf_rect const& rect)
{
    auto result = QPDFObjectHandle::newArray();
    double const bottom = page.height_points - rect.y1;
    double const top = page.height_points - rect.y0;
    result.appendItem(QPDFObjectHandle::newReal(
        rect.x0, decimal_precision(rect.x0)));
    result.appendItem(QPDFObjectHandle::newReal(
        bottom, decimal_precision(bottom)));
    result.appendItem(QPDFObjectHandle::newReal(
        rect.x1, decimal_precision(rect.x1)));
    result.appendItem(QPDFObjectHandle::newReal(
        top, decimal_precision(top)));
    return result;
}

void apply_composer_links(
    QPDF& pdf,
    quantapdf_composer const* composer,
    std::vector<QPDFObjectHandle>& pages)
{
    for (size_t i = 0u; i < composer->link_count; ++i) {
        auto const& link = composer->links[i];
        auto annotation = QPDFObjectHandle::newDictionary();
        auto border = QPDFObjectHandle::newArray();
        border.appendItem(QPDFObjectHandle::newInteger(0));
        border.appendItem(QPDFObjectHandle::newInteger(0));
        border.appendItem(QPDFObjectHandle::newInteger(0));

        annotation.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        annotation.replaceKey("/Subtype", QPDFObjectHandle::newName("/Link"));
        annotation.replaceKey(
            "/Rect",
            navigation_rectangle(
                composer->pages[link.page_index], link.hotspot));
        annotation.replaceKey("/Border", border);

        if (link.kind == QUANTAPDF_COMPOSER_LINK_URI_INTERNAL) {
            auto action = QPDFObjectHandle::newDictionary();
            action.replaceKey("/S", QPDFObjectHandle::newName("/URI"));
            action.replaceKey(
                "/URI", QPDFObjectHandle::newUnicodeString(link.uri_utf8));
            annotation.replaceKey("/A", action);
        } else {
            annotation.replaceKey(
                "/Dest",
                navigation_destination(
                    pages, composer, link.target_page_index, link.target));
        }

        auto annots = pages[link.page_index].getKey("/Annots");
        if (annots.isNull()) {
            annots = QPDFObjectHandle::newArray();
            pages[link.page_index].replaceKey("/Annots", annots);
        }
        if (!annots.isArray())
            throw std::logic_error("composer page Annots is not an array");
        annots.appendItem(pdf.makeIndirectObject(annotation));
    }
}

void link_outline_children(
    QPDFObjectHandle parent,
    std::vector<size_t> const& children,
    std::vector<QPDFObjectHandle> const& nodes)
{
    if (children.empty())
        return;
    parent.replaceKey("/First", nodes[children.front()]);
    parent.replaceKey("/Last", nodes[children.back()]);
    for (size_t position = 0u; position < children.size(); ++position) {
        auto node = nodes[children[position]];
        if (position != 0u)
            node.replaceKey("/Prev", nodes[children[position - 1u]]);
        if (position + 1u < children.size())
            node.replaceKey("/Next", nodes[children[position + 1u]]);
    }
}

void apply_composer_outlines(
    QPDF& pdf,
    quantapdf_composer const* composer,
    std::vector<QPDFObjectHandle> const& pages)
{
    if (composer->outline_count == 0u)
        return;

    auto outline_root =
        pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
    outline_root.replaceKey("/Type", QPDFObjectHandle::newName("/Outlines"));

    std::vector<QPDFObjectHandle> nodes;
    nodes.reserve(composer->outline_count);
    for (size_t i = 0u; i < composer->outline_count; ++i) {
        auto const& source = composer->outlines[i];
        auto node = QPDFObjectHandle::newDictionary();
        node.replaceKey("/Type", QPDFObjectHandle::newName("/Outline"));
        node.replaceKey(
            "/Title", QPDFObjectHandle::newUnicodeString(source.title_utf8));
        node.replaceKey(
            "/Dest",
            navigation_destination(
                pages, composer, source.target_page_index, source.target));
        nodes.push_back(pdf.makeIndirectObject(node));
    }

    std::vector<std::vector<size_t>> children(composer->outline_count + 1u);
    for (size_t i = 0u; i < composer->outline_count; ++i) {
        auto const parent_id = composer->outlines[i].parent_id;
        children[static_cast<size_t>(parent_id)].push_back(i);
        nodes[i].replaceKey(
            "/Parent",
            parent_id == 0u
                ? outline_root
                : nodes[static_cast<size_t>(parent_id) - 1u]);
    }

    link_outline_children(outline_root, children[0], nodes);
    for (size_t i = 0u; i < composer->outline_count; ++i)
        link_outline_children(nodes[i], children[i + 1u], nodes);

    std::vector<size_t> descendants(composer->outline_count, 0u);
    for (size_t i = composer->outline_count; i-- > 0u;) {
        auto const parent_id = composer->outlines[i].parent_id;
        if (parent_id != 0u) {
            size_t& parent_descendants =
                descendants[static_cast<size_t>(parent_id) - 1u];
            if (parent_descendants >
                std::numeric_limits<size_t>::max() - descendants[i] - 1u)
                throw std::overflow_error("outline descendant count overflow");
            parent_descendants += descendants[i] + 1u;
        }
    }
    for (size_t i = 0u; i < composer->outline_count; ++i) {
        if (descendants[i] == 0u)
            continue;
        auto const value = static_cast<long long>(descendants[i]);
        nodes[i].replaceKey(
            "/Count",
            QPDFObjectHandle::newInteger(
                composer->outlines[i].is_open ? value : -value));
    }
    outline_root.replaceKey(
        "/Count",
        QPDFObjectHandle::newInteger(
            static_cast<long long>(composer->outline_count)));
    pdf.getRoot().replaceKey("/Outlines", outline_root);
}

void apply_composer_navigation(
    QPDF& pdf,
    quantapdf_composer const* composer)
{
    auto pages = pdf.getAllPages();
    if (pages.size() != composer->page_count)
        throw std::logic_error("composer page count mismatch");
    apply_composer_links(pdf, composer, pages);
    apply_composer_outlines(pdf, composer, pages);
}

void append_path_clip_content(
    std::string& content,
    quantapdf_composer_page_state const& page,
    quantapdf_composer_clip_state const& clip)
{
    if (clip.kind != QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL)
        throw std::logic_error("path clip expected");

    auto append_point = [&](quantapdf_point const& point) {
        double const x =
            static_cast<double>(clip.transform.a) * point.x +
            static_cast<double>(clip.transform.c) * point.y +
            static_cast<double>(clip.transform.e);
        double const y =
            static_cast<double>(clip.transform.b) * point.x +
            static_cast<double>(clip.transform.d) * point.y +
            static_cast<double>(clip.transform.f);
        content += number(canonical_zero(x)) + " " +
            number(canonical_zero(page.height_points - y));
    };

    for (size_t i = 0u; i < clip.command_count; ++i) {
        auto const& command = clip.commands[i];
        switch (command.kind) {
        case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
            append_point(command.point1);
            content += " m ";
            break;
        case QUANTAPDF_COMPOSER_PATH_LINE_TO:
            append_point(command.point1);
            content += " l ";
            break;
        case QUANTAPDF_COMPOSER_PATH_CUBIC_TO:
            append_point(command.point1);
            content += " ";
            append_point(command.point2);
            content += " ";
            append_point(command.point3);
            content += " c ";
            break;
        case QUANTAPDF_COMPOSER_PATH_CLOSE:
            content += "h ";
            break;
        }
    }
    content += clip.fill_rule == QUANTAPDF_COMPOSER_FILL_EVEN_ODD
        ? "W* n\n"
        : "W n\n";
}

void append_clip_content(
    std::string& content,
    quantapdf_composer const* composer,
    quantapdf_composer_page_state const& page,
    quantapdf_composer_clip_state const& clip)
{
    if (clip.kind == QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL) {
        append_path_clip_content(content, page, clip);
        return;
    }
    if (clip.kind != QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL)
        throw std::logic_error("unknown clip resource kind");

    for (size_t i = 0u; i < clip.member_count; ++i) {
        auto const id = clip.members[i];
        if (id == 0u || id > composer->clip_count)
            throw std::logic_error("compound clip member missing");
        auto const& member = composer->clips[id - 1u];
        if (member.kind != QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL)
            throw std::logic_error("compound clip member is not a leaf");
        append_path_clip_content(content, page, member);
    }
}

std::string page_content(
    quantapdf_composer const* composer,
    std::size_t page_index,
    std::vector<quantapdf::detail::ttf_font_face> const& embedded_faces,
    std::vector<glyph_run_font_usage> const& glyph_run_usages)
{
    auto const& page = composer->pages[page_index];
    std::string content;
    double const red = ((page.background_argb >> 16u) & 0xffu) / 255.0;
    double const green = ((page.background_argb >> 8u) & 0xffu) / 255.0;
    double const blue = (page.background_argb & 0xffu) / 255.0;
    if (!page.suppress_background) {
        content += "q " + number(red) + " " + number(green) + " " +
            number(blue) + " rg 0 0 " + number(page.width_points) + " " +
            number(page.height_points) + " re f Q\n";
    }

    for (std::size_t i = 0; i < composer->operation_count; ++i) {
        auto const& operation = composer->operations[i];
        if (operation.page_index != page_index)
            continue;

        bool const scoped_state = operation.graphics_state_id != 0u;
        if (scoped_state) {
            if (static_cast<size_t>(operation.graphics_state_id) >
                composer->graphics_state_count)
                throw std::logic_error("graphics state resource missing");
            auto const& state =
                composer->graphics_states[operation.graphics_state_id - 1u];
            if (state.clip_id != 0u) {
                if (state.clip_id > composer->clip_count)
                    throw std::logic_error("clip resource missing");
                content += "q\n";
                append_clip_content(
                    content,
                    composer,
                    page,
                    composer->clips[state.clip_id - 1u]);
                content += "/GS" +
                    std::to_string(operation.graphics_state_id) + " gs\n";
            } else {
                content += "q /GS" +
                    std::to_string(operation.graphics_state_id) + " gs\n";
            }
        }

        if (operation.kind == QUANTAPDF_COMPOSER_OPERATION_TEXT)
            append_text_content(content, page, operation);
        else if (operation.kind == QUANTAPDF_COMPOSER_OPERATION_IMAGE)
            append_image_content(content, composer, page, operation);
        else if (operation.kind == QUANTAPDF_COMPOSER_OPERATION_PATH)
            append_path_content(content, page, operation);
        else if (operation.kind ==
                 QUANTAPDF_COMPOSER_OPERATION_EMBEDDED_TEXT)
            append_embedded_text_content(
                content, page, operation, embedded_faces);
        else if (operation.kind ==
                 QUANTAPDF_COMPOSER_OPERATION_GLYPH_RUN) {
            size_t const font_index =
                operation.value.glyph_run.options.font_id - 1u;
            if (font_index >= glyph_run_usages.size())
                throw std::logic_error("glyph-run usage missing");
            append_glyph_run_content(
                content,
                page,
                operation,
                glyph_run_usages[font_index]);
        } else if (operation.kind == QUANTAPDF_COMPOSER_OPERATION_FORM) {
            append_form_content(content, composer, page, operation);
        }

        if (scoped_state)
            content += "Q\n";
    }
    return content;
}

} // namespace

extern "C" quantapdf_status quantapdf_png_decode(
    unsigned char const* data,
    size_t size,
    size_t max_decoded_bytes,
    unsigned char** out_rgb,
    size_t* out_rgb_size,
    unsigned char** out_alpha,
    size_t* out_alpha_size,
    uint32_t* out_width,
    uint32_t* out_height)
{
    static unsigned char const signature[] = {
        0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au};
    uint32_t width = 0u;
    uint32_t height = 0u;
    unsigned int color_type = 0u;
    bool have_header = false;
    bool have_data = false;
    bool have_end = false;
    bool data_ended = false;
    bool have_palette = false;
    size_t components = 0u;
    size_t row_size = 0u;
    size_t inflated_size = 0u;
    size_t sample_size = 0u;
    size_t pixels = 0u;
    size_t rgb_size = 0u;
    size_t alpha_size = 0u;
    std::vector<unsigned char> compressed;

    if (out_rgb == nullptr || out_rgb_size == nullptr ||
        out_alpha == nullptr || out_alpha_size == nullptr ||
        out_width == nullptr || out_height == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_rgb = nullptr;
    *out_rgb_size = 0u;
    *out_alpha = nullptr;
    *out_alpha_size = 0u;
    *out_width = 0u;
    *out_height = 0u;
    if (data == nullptr || size < sizeof(signature) ||
        std::memcmp(data, signature, sizeof(signature)) != 0)
        return QUANTAPDF_ERROR_FORMAT;

    try {
        size_t offset = sizeof(signature);
        while (!have_end && offset <= size && size - offset >= 12u) {
            uint32_t const chunk_size = read_be32(data + offset);
            offset += 4u;
            if (static_cast<size_t>(chunk_size) > size - offset - 8u)
                return QUANTAPDF_ERROR_FORMAT;
            unsigned char const* type = data + offset;
            unsigned char const* payload = type + 4u;
            uint32_t const expected_crc = read_be32(payload + chunk_size);
            if (!png_chunk_type_valid(type))
                return QUANTAPDF_ERROR_FORMAT;
            if (chunk_size > std::numeric_limits<uInt>::max() - 4u)
                return QUANTAPDF_ERROR_UNSUPPORTED;
            uLong crc = crc32(0L, Z_NULL, 0);
            crc = crc32(crc, type, static_cast<uInt>(chunk_size + 4u));
            if (static_cast<uint32_t>(crc) != expected_crc)
                return QUANTAPDF_ERROR_FORMAT;

            if (std::memcmp(type, "IHDR", 4u) == 0) {
                size_t row_with_filter = 0u;
                size_t working_size = size;
                if (have_header || have_data || offset != 12u ||
                    chunk_size != 13u)
                    return QUANTAPDF_ERROR_FORMAT;
                width = read_be32(payload);
                height = read_be32(payload + 4u);
                color_type = payload[9u];
                if (width == 0u || height == 0u || payload[8u] != 8u ||
                    (color_type != 2u && color_type != 6u) ||
                    payload[10u] != 0u || payload[11u] != 0u ||
                    payload[12u] != 0u)
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                components = color_type == 6u ? 4u : 3u;
                if (!checked_multiply(width, components, row_size) ||
                    !checked_add(row_size, 1u, row_with_filter) ||
                    !checked_multiply(
                        row_with_filter, height, inflated_size) ||
                    !checked_multiply(row_size, height, sample_size) ||
                    !checked_multiply(width, height, pixels) ||
                    !checked_multiply(pixels, 3u, rgb_size))
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                alpha_size = color_type == 6u ? pixels : 0u;
                if (!checked_add(working_size, inflated_size, working_size) ||
                    !checked_add(working_size, sample_size, working_size) ||
                    !checked_add(working_size, rgb_size, working_size) ||
                    !checked_add(working_size, alpha_size, working_size) ||
                    working_size > max_decoded_bytes)
                    return QUANTAPDF_ERROR_UNSUPPORTED;
                compressed.reserve(size);
                have_header = true;
            } else if (std::memcmp(type, "IDAT", 4u) == 0) {
                if (!have_header || have_end || data_ended ||
                    static_cast<size_t>(chunk_size) >
                        std::numeric_limits<size_t>::max() - compressed.size())
                    return QUANTAPDF_ERROR_FORMAT;
                compressed.insert(
                    compressed.end(), payload, payload + chunk_size);
                have_data = true;
            } else if (std::memcmp(type, "IEND", 4u) == 0) {
                if (!have_header || !have_data || chunk_size != 0u)
                    return QUANTAPDF_ERROR_FORMAT;
                have_end = true;
            } else if (std::memcmp(type, "PLTE", 4u) == 0) {
                if (!have_header || have_data || have_palette ||
                    chunk_size == 0u || chunk_size > 768u ||
                    chunk_size % 3u != 0u)
                    return QUANTAPDF_ERROR_FORMAT;
                have_palette = true;
            } else if ((type[0] & 0x20u) == 0u &&
                       std::memcmp(type, "PLTE", 4u) != 0) {
                return QUANTAPDF_ERROR_UNSUPPORTED;
            }
            if (have_data && std::memcmp(type, "IDAT", 4u) != 0)
                data_ended = true;
            offset += 4u + static_cast<size_t>(chunk_size) + 4u;
        }
        if (!have_end || offset != size || compressed.empty())
            return QUANTAPDF_ERROR_FORMAT;

        if (inflated_size > std::numeric_limits<uLongf>::max() ||
            compressed.size() > std::numeric_limits<uLong>::max())
            return QUANTAPDF_ERROR_UNSUPPORTED;
        std::vector<unsigned char> inflated(inflated_size);
        uLongf actual_size = static_cast<uLongf>(inflated_size);
        if (uncompress(
                inflated.data(), &actual_size, compressed.data(),
                static_cast<uLong>(compressed.size())) != Z_OK ||
            actual_size != inflated_size)
            return QUANTAPDF_ERROR_FORMAT;

        std::vector<unsigned char> samples(sample_size);
        for (size_t row = 0u; row < height; ++row) {
            unsigned int const filter = inflated[row * (row_size + 1u)];
            if (filter > 4u)
                return QUANTAPDF_ERROR_FORMAT;
            unsigned char const* source =
                inflated.data() + row * (row_size + 1u) + 1u;
            unsigned char* target = samples.data() + row * row_size;
            unsigned char const* previous =
                row == 0u ? nullptr : target - row_size;
            for (size_t column = 0u; column < row_size; ++column) {
                unsigned char const left =
                    column < components ? 0u : target[column - components];
                unsigned char const up = previous == nullptr
                    ? 0u
                    : previous[column];
                unsigned char const upper_left =
                    previous == nullptr || column < components
                    ? 0u
                    : previous[column - components];
                unsigned int predictor = 0u;
                if (filter == 1u)
                    predictor = left;
                else if (filter == 2u)
                    predictor = up;
                else if (filter == 3u)
                    predictor = (static_cast<unsigned int>(left) + up) / 2u;
                else if (filter == 4u)
                    predictor = paeth_predictor(left, up, upper_left);
                target[column] = static_cast<unsigned char>(
                    source[column] + predictor);
            }
        }

        auto* rgb = static_cast<unsigned char*>(std::malloc(rgb_size));
        auto* alpha = alpha_size == 0u
            ? nullptr
            : static_cast<unsigned char*>(std::malloc(alpha_size));
        if (rgb == nullptr || (alpha_size != 0u && alpha == nullptr)) {
            std::free(alpha);
            std::free(rgb);
            return QUANTAPDF_ERROR_NOMEM;
        }
        if (color_type == 2u) {
            std::memcpy(rgb, samples.data(), rgb_size);
        } else {
            for (size_t pixel = 0u; pixel < pixels; ++pixel) {
                rgb[pixel * 3u] = samples[pixel * 4u];
                rgb[pixel * 3u + 1u] = samples[pixel * 4u + 1u];
                rgb[pixel * 3u + 2u] = samples[pixel * 4u + 2u];
                alpha[pixel] = samples[pixel * 4u + 3u];
            }
        }
        *out_rgb = rgb;
        *out_rgb_size = rgb_size;
        *out_alpha = alpha;
        *out_alpha_size = alpha_size;
        *out_width = width;
        *out_height = height;
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_compose(
    quantapdf_composer const* composer,
    unsigned char** out_data,
    size_t* out_size)
{
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0u;
    if (composer == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        QPDF pdf;
        pdf.emptyPDF();
        std::vector<QPDFObjectHandle> image_objects;
        image_objects.reserve(composer->image_count);
        for (std::size_t i = 0; i < composer->image_count; ++i) {
            auto const& image = composer->images[i];
            auto stream = pdf.newStream(std::string(
                reinterpret_cast<char const*>(image.data), image.size));
            auto dictionary = stream.getDict();
            dictionary.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
            dictionary.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
            dictionary.replaceKey(
                "/Width", QPDFObjectHandle::newInteger(image.width));
            dictionary.replaceKey(
                "/Height", QPDFObjectHandle::newInteger(image.height));
            dictionary.replaceKey(
                "/BitsPerComponent", QPDFObjectHandle::newInteger(8));
            dictionary.replaceKey(
                "/ColorSpace",
                QPDFObjectHandle::newName(
                    image.components == 1 ? "/DeviceGray" : "/DeviceRGB"));
            if (image.format == QUANTAPDF_COMPOSER_IMAGE_FORMAT_JPEG) {
                dictionary.replaceKey(
                    "/Filter", QPDFObjectHandle::newName("/DCTDecode"));
            } else if (image.has_alpha) {
                auto alpha_stream = pdf.newStream(std::string(
                    reinterpret_cast<char const*>(image.alpha_data),
                    image.alpha_size));
                auto alpha_dictionary = alpha_stream.getDict();
                alpha_dictionary.replaceKey(
                    "/Type", QPDFObjectHandle::newName("/XObject"));
                alpha_dictionary.replaceKey(
                    "/Subtype", QPDFObjectHandle::newName("/Image"));
                alpha_dictionary.replaceKey(
                    "/Width", QPDFObjectHandle::newInteger(image.width));
                alpha_dictionary.replaceKey(
                    "/Height", QPDFObjectHandle::newInteger(image.height));
                alpha_dictionary.replaceKey(
                    "/BitsPerComponent", QPDFObjectHandle::newInteger(8));
                alpha_dictionary.replaceKey(
                    "/ColorSpace", QPDFObjectHandle::newName("/DeviceGray"));
                dictionary.replaceKey("/SMask", alpha_stream);
            }
            image_objects.push_back(stream);
        }

        std::vector<std::optional<QPDFObjectHandle>>
            form_objects(composer->form_count);
        std::vector<std::unique_ptr<QPDF>>
            form_source_pdfs(composer->form_count);
        auto ensure_form_object = [&](quantapdf_composer_form_id id)
            -> QPDFObjectHandle {
            if (id == 0u || id > composer->form_count)
                throw std::logic_error("form resource missing");
            auto& slot = form_objects[id - 1u];
            if (!slot.has_value()) {
                auto const& form = composer->forms[id - 1u];
                auto source = std::make_unique<QPDF>();
                std::string const description =
                    "quantapdf-form-" + std::to_string(id);
                source->processMemoryFile(
                    description.c_str(),
                    reinterpret_cast<char const*>(form.pdf_data),
                    form.pdf_size);
                auto foreign_pages =
                    QPDFPageDocumentHelper::get(*source).getAllPages();
                if (foreign_pages.size() != 1u)
                    throw std::logic_error(
                        "form snapshot page count mismatch");
                QPDFObjectHandle foreign_form =
                    foreign_pages[0].getFormXObjectForPage();
                slot = pdf.copyForeignObject(foreign_form);
                if ((form.flags &
                     QUANTAPDF_COMPOSER_FORM_FLAG_TRANSPARENCY_GROUP) != 0u) {
                    auto group = QPDFObjectHandle::newDictionary();
                    group.replaceKey(
                        "/S", QPDFObjectHandle::newName("/Transparency"));
                    group.replaceKey(
                        "/CS", QPDFObjectHandle::newName("/DeviceRGB"));
                    group.replaceKey(
                        "/I",
                        QPDFObjectHandle::newBool(
                            (form.flags &
                             QUANTAPDF_COMPOSER_FORM_FLAG_ISOLATED) != 0u));
                    group.replaceKey(
                        "/K",
                        QPDFObjectHandle::newBool(
                            (form.flags &
                             QUANTAPDF_COMPOSER_FORM_FLAG_KNOCKOUT) != 0u));
                    slot->getDict().replaceKey("/Group", group);
                }
                /*
                 * getFormXObjectForPage may lazily read source page content
                 * while the destination writer serializes the copied object.
                 * Keep the source QPDF alive until this compose call returns.
                 */
                form_source_pdfs[id - 1u] = std::move(source);
            }
            return *slot;
        };

        std::vector<quantapdf::detail::ttf_font_face> embedded_faces;
        embedded_faces.reserve(composer->font_count);
        for (size_t i = 0u; i < composer->font_count; ++i) {
            quantapdf::detail::ttf_font_face face;
            if (quantapdf::detail::ttf_font_face::parse(
                    composer->fonts[i].data,
                    composer->fonts[i].size,
                    &face) != QUANTAPDF_OK)
                throw std::invalid_argument("registered font no longer parses");
            embedded_faces.push_back(std::move(face));
        }
        std::vector<embedded_font_usage> embedded_usage(composer->font_count);
        collect_embedded_font_usage(
            composer, embedded_faces, &embedded_usage);
        std::vector<std::optional<QPDFObjectHandle>> embedded_font_objects(
            composer->font_count);
        for (size_t i = 0u; i < composer->font_count; ++i) {
            if (embedded_usage[i].referenced) {
                embedded_font_objects[i] = make_embedded_font(
                    pdf,
                    composer->fonts[i],
                    embedded_faces[i],
                    embedded_usage[i],
                    i);
            }
        }

        std::vector<glyph_run_font_usage> glyph_run_usage(
            composer->font_count);
        collect_glyph_run_font_usage(
            composer, embedded_faces, &glyph_run_usage);
        std::vector<std::optional<QPDFObjectHandle>> glyph_run_font_objects(
            composer->font_count);
        bool requires_pdf_16 = false;
        for (size_t i = 0u; i < composer->font_count; ++i) {
            if (glyph_run_usage[i].referenced) {
                glyph_run_font_objects[i] = make_glyph_run_font(
                    pdf,
                    composer->fonts[i],
                    embedded_faces[i],
                    glyph_run_usage[i],
                    i);
            }
            if ((embedded_usage[i].referenced ||
                 glyph_run_usage[i].referenced) &&
                embedded_faces[i].outline_kind !=
                    quantapdf::detail::sfnt_outline_kind::true_type)
                requires_pdf_16 = true;
        }

        for (size_t i = 0u; i < composer->operation_count; ++i) {
            auto const& operation = composer->operations[i];
            if (operation.kind == QUANTAPDF_COMPOSER_OPERATION_FORM) {
                auto const id = operation.value.form.form_id;
                if (id == 0u || id > composer->form_count)
                    throw std::logic_error("form resource missing");
                if (composer->forms[id - 1u].requires_pdf_16)
                    requires_pdf_16 = true;
            } else if (
                operation.kind == QUANTAPDF_COMPOSER_OPERATION_PATH) {
                auto check_paint = [&](quantapdf_composer_paint_id id) {
                    if (id == 0u)
                        return;
                    if (id > composer->paint_count)
                        throw std::logic_error("paint resource missing");
                    auto const& paint = composer->paints[id - 1u];
                    if (paint.kind ==
                            QUANTAPDF_COMPOSER_PAINT_TILING_PATTERN_INTERNAL &&
                        paint.requires_pdf_16)
                        requires_pdf_16 = true;
                };
                check_paint(operation.value.path.options.fill_paint_id);
                check_paint(operation.value.path.options.stroke_paint_id);
            }
        }

        std::vector<std::optional<QPDFObjectHandle>>
            graphics_state_objects(composer->graphics_state_count);

        std::vector<std::optional<QPDFObjectHandle>>
            paint_shading_objects(composer->paint_count);
        std::vector<std::optional<QPDFObjectHandle>>
            paint_tile_form_objects(composer->paint_count);
        std::vector<std::unique_ptr<QPDF>>
            paint_source_pdfs(composer->paint_count);
        auto ensure_tile_form = [&](quantapdf_composer_paint_id id)
            -> QPDFObjectHandle {
            if (id == 0u || id > composer->paint_count)
                throw std::logic_error("tiling paint resource missing");
            auto const& paint = composer->paints[id - 1u];
            if (paint.kind != QUANTAPDF_COMPOSER_PAINT_TILING_PATTERN_INTERNAL)
                throw std::logic_error("paint is not a tiling pattern");
            auto& slot = paint_tile_form_objects[id - 1u];
            if (!slot.has_value()) {
                auto source = std::make_unique<QPDF>();
                std::string const description =
                    "quantapdf-pattern-" + std::to_string(id);
                source->processMemoryFile(
                    description.c_str(),
                    reinterpret_cast<char const*>(paint.pdf_data),
                    paint.pdf_size);
                auto foreign_pages =
                    QPDFPageDocumentHelper::get(*source).getAllPages();
                if (foreign_pages.size() != 1u)
                    throw std::logic_error(
                        "tiling snapshot page count mismatch");
                QPDFObjectHandle foreign_form =
                    foreign_pages[0].getFormXObjectForPage();
                slot = pdf.copyForeignObject(foreign_form);
                paint_source_pdfs[id - 1u] = std::move(source);
            }
            return *slot;
        };

        for (std::size_t page_index = 0; page_index < composer->page_count;
             ++page_index) {
            auto page = QPDFObjectHandle::newDictionary();
            auto media_box = QPDFObjectHandle::newArray();
            auto resources = QPDFObjectHandle::newDictionary();
            auto fonts = QPDFObjectHandle::newDictionary();
            auto xobjects = QPDFObjectHandle::newDictionary();
            auto ext_gstates = QPDFObjectHandle::newDictionary();
            auto patterns = QPDFObjectHandle::newDictionary();
            std::vector<std::optional<QPDFObjectHandle>>
                page_patterns(composer->paint_count);
            bool used_fonts[12] = {};
            bool used_ext_gstate = false;
            bool used_pattern = false;

            for (std::size_t i = 0; i < composer->operation_count; ++i) {
                auto const& operation = composer->operations[i];
                if (operation.page_index == page_index &&
                    operation.kind == QUANTAPDF_COMPOSER_OPERATION_TEXT)
                    used_fonts[static_cast<int>(operation.value.text.options.font)] = true;
            }
            for (int font = 0; font < 12; ++font) {
                if (used_fonts[font])
                    fonts.replaceKey(
                        "/F" + std::to_string(font),
                        make_font(static_cast<quantapdf_composer_font>(font)));
            }
            for (std::size_t i = 0; i < composer->operation_count; ++i) {
                auto const& operation = composer->operations[i];
                if (operation.page_index != page_index ||
                    operation.kind !=
                        QUANTAPDF_COMPOSER_OPERATION_EMBEDDED_TEXT)
                    continue;
                auto const id = operation.value.embedded_text.options.font_id;
                if (id == 0u ||
                    static_cast<size_t>(id) > embedded_font_objects.size() ||
                    !embedded_font_objects[id - 1u].has_value())
                    throw std::logic_error("embedded font resource missing");
                fonts.replaceKey(
                    "/EF" + std::to_string(id),
                    *embedded_font_objects[id - 1u]);
            }
            for (std::size_t i = 0; i < composer->operation_count; ++i) {
                auto const& operation = composer->operations[i];
                if (operation.page_index != page_index ||
                    operation.kind != QUANTAPDF_COMPOSER_OPERATION_GLYPH_RUN)
                    continue;
                auto const id = operation.value.glyph_run.options.font_id;
                if (id == 0u ||
                    static_cast<size_t>(id) > glyph_run_font_objects.size() ||
                    !glyph_run_font_objects[id - 1u].has_value())
                    throw std::logic_error("glyph-run font resource missing");
                fonts.replaceKey(
                    "/GR" + std::to_string(id),
                    *glyph_run_font_objects[id - 1u]);
            }
            resources.replaceKey("/Font", fonts);
            for (std::size_t i = 0; i < composer->operation_count; ++i) {
                auto const& operation = composer->operations[i];
                if (operation.page_index != page_index)
                    continue;
                if (operation.kind == QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
                    auto id = operation.value.image.image_id;
                    xobjects.replaceKey(
                        "/Im" + std::to_string(id), image_objects[id - 1u]);
                } else if (
                    operation.kind == QUANTAPDF_COMPOSER_OPERATION_FORM) {
                    auto id = operation.value.form.form_id;
                    xobjects.replaceKey(
                        "/Fm" + std::to_string(id),
                        ensure_form_object(id));
                }
            }
            resources.replaceKey("/XObject", xobjects);
            for (std::size_t i = 0; i < composer->operation_count; ++i) {
                auto const& operation = composer->operations[i];
                if (operation.page_index != page_index ||
                    operation.graphics_state_id == 0u)
                    continue;
                auto const id = operation.graphics_state_id;
                if (static_cast<size_t>(id) >
                    graphics_state_objects.size())
                    throw std::logic_error("graphics state resource missing");
                auto& object = graphics_state_objects[id - 1u];
                if (!object.has_value()) {
                    object = make_graphics_state(
                        pdf, composer->graphics_states[id - 1u]);
                }
                ext_gstates.replaceKey(
                    "/GS" + std::to_string(id), *object);
                used_ext_gstate = true;
            }
            if (used_ext_gstate)
                resources.replaceKey("/ExtGState", ext_gstates);

            for (std::size_t i = 0; i < composer->operation_count; ++i) {
                auto const& operation = composer->operations[i];
                if (operation.page_index != page_index ||
                    operation.kind != QUANTAPDF_COMPOSER_OPERATION_PATH)
                    continue;
                auto add_paint = [&](quantapdf_composer_paint_id id) {
                    if (id == 0u)
                        return;
                    if (id > composer->paint_count)
                        throw std::logic_error("paint resource missing");
                    auto& page_pattern = page_patterns[id - 1u];
                    if (!page_pattern.has_value()) {
                        auto const& paint = composer->paints[id - 1u];
                        if (paint.kind ==
                            QUANTAPDF_COMPOSER_PAINT_TILING_PATTERN_INTERNAL) {
                            page_pattern = make_tiling_pattern(
                                pdf,
                                ensure_tile_form(id),
                                paint,
                                composer->pages[page_index].height_points);
                        } else {
                            auto& shading = paint_shading_objects[id - 1u];
                            if (!shading.has_value()) {
                                shading = make_gradient_shading(pdf, paint);
                            }
                            page_pattern = make_gradient_pattern(
                                pdf,
                                *shading,
                                paint,
                                composer->pages[page_index].height_points);
                        }
                    }
                    patterns.replaceKey(
                        "/P" + std::to_string(id), *page_pattern);
                    used_pattern = true;
                };
                add_paint(operation.value.path.options.fill_paint_id);
                add_paint(operation.value.path.options.stroke_paint_id);
            }
            if (used_pattern)
                resources.replaceKey("/Pattern", patterns);
            media_box.appendItem(QPDFObjectHandle::newInteger(0));
            media_box.appendItem(QPDFObjectHandle::newInteger(0));
            media_box.appendItem(QPDFObjectHandle::newReal(
                composer->pages[page_index].width_points,
                decimal_precision(composer->pages[page_index].width_points)));
            media_box.appendItem(QPDFObjectHandle::newReal(
                composer->pages[page_index].height_points,
                decimal_precision(composer->pages[page_index].height_points)));
            page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
            page.replaceKey("/MediaBox", media_box);
            page.replaceKey("/Resources", resources);
            page.replaceKey(
                "/Contents",
                pdf.newStream(page_content(
                    composer,
                    page_index,
                    embedded_faces,
                    glyph_run_usage)));
            pdf.addPage(pdf.makeIndirectObject(page), false);
        }
        apply_composer_navigation(pdf, composer);

        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.setStaticID(true);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setMinimumPDFVersion(
            requires_pdf_16 ? "1.6" : "1.4");
        writer.write();
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (buffer->getSize() == 0u)
            return QUANTAPDF_ERROR_BACKEND;
        auto* copied = static_cast<unsigned char*>(std::malloc(buffer->getSize()));
        if (copied == nullptr)
            return QUANTAPDF_ERROR_NOMEM;
        std::memcpy(copied, buffer->getBuffer(), buffer->getSize());
        *out_data = copied;
        *out_size = buffer->getSize();
        return QUANTAPDF_OK;
    } catch (std::invalid_argument const&) {
        return QUANTAPDF_ERROR_FORMAT;
    } catch (std::length_error const&) {
        return QUANTAPDF_ERROR_UNSUPPORTED;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}
