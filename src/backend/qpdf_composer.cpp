#include "qpdf_composer.h"

#include "../internal.h"
#include "base14_metrics.h"
#include "ttf_font.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
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

char const* base_font_name(quantapdf_composer_font font)
{
    static char const* const names[] = {
        "/Helvetica", "/Helvetica-Bold", "/Helvetica-Oblique",
        "/Helvetica-BoldOblique", "/Times-Roman", "/Times-Bold",
        "/Times-Italic", "/Times-BoldItalic", "/Courier",
        "/Courier-Bold", "/Courier-Oblique", "/Courier-BoldOblique"};
    return names[static_cast<int>(font)];
}

std::optional<unsigned char> winansi_byte(unsigned int codepoint)
{
    struct mapping {
        unsigned int unicode;
        unsigned char byte;
    };
    static mapping const special[] = {
        {0x20ac, 0x80}, {0x201a, 0x82}, {0x0192, 0x83}, {0x201e, 0x84},
        {0x2026, 0x85}, {0x2020, 0x86}, {0x2021, 0x87}, {0x02c6, 0x88},
        {0x2030, 0x89}, {0x0160, 0x8a}, {0x2039, 0x8b}, {0x0152, 0x8c},
        {0x017d, 0x8e}, {0x2018, 0x91}, {0x2019, 0x92}, {0x201c, 0x93},
        {0x201d, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97},
        {0x02dc, 0x98}, {0x2122, 0x99}, {0x0161, 0x9a}, {0x203a, 0x9b},
        {0x0153, 0x9c}, {0x017e, 0x9e}, {0x0178, 0x9f}};
    if (codepoint <= 0x7f || (codepoint >= 0xa0 && codepoint <= 0xff))
        return static_cast<unsigned char>(codepoint);
    for (auto const& item: special) {
        if (item.unicode == codepoint)
            return item.byte;
    }
    return std::nullopt;
}

std::string to_winansi(char const* utf8)
{
    std::string result;
    auto cursor = reinterpret_cast<unsigned char const*>(utf8);
    while (*cursor != 0) {
        unsigned int codepoint;
        unsigned int count;
        if (*cursor < 0x80) {
            codepoint = *cursor;
            count = 1;
        } else if (*cursor < 0xe0) {
            codepoint = *cursor & 0x1f;
            count = 2;
        } else {
            codepoint = *cursor & 0x0f;
            count = 3;
        }
        for (unsigned int i = 1; i < count; ++i)
            codepoint = (codepoint << 6) | (cursor[i] & 0x3f);
        auto const mapped = winansi_byte(codepoint);
        if (!mapped.has_value())
            throw std::invalid_argument("text is not representable in WinAnsi");
        result.push_back(static_cast<char>(*mapped));
        cursor += count;
    }
    return result;
}

double glyph_width(unsigned char glyph, quantapdf_composer_font font)
{
    return quantapdf::detail::base14_glyph_width(glyph, font);
}

double text_width(
    std::string const& text,
    quantapdf_composer_font font,
    double font_size)
{
    double units = 0.0;
    for (unsigned char glyph: text)
        units += glyph_width(glyph, font);
    return units * font_size / 1000.0;
}

std::vector<std::string> layout_lines(
    std::string const& text,
    quantapdf_composer_text_options const& options,
    double width)
{
    std::vector<std::string> lines;
    std::string line;
    std::size_t last_space = std::string::npos;

    auto publish = [&]() {
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
        lines.push_back(line);
        line.clear();
        last_space = std::string::npos;
    };
    for (char value: text) {
        if (value == '\r')
            continue;
        if (value == '\n') {
            publish();
            continue;
        }
        line.push_back(value == '\t' ? ' ' : value);
        if (value == ' ' || value == '\t')
            last_space = line.size() - 1u;
        if (options.wrap && line.size() > 1u &&
            text_width(line, options.font, options.font_size) > width) {
            if (last_space != std::string::npos) {
                std::string remainder = line.substr(last_space + 1u);
                line.resize(last_space);
                publish();
                line = remainder;
            } else {
                char overflow = line.back();
                line.pop_back();
                publish();
                line.push_back(overflow);
            }
        }
    }
    if (!line.empty() || text.empty() || text.back() == '\n')
        publish();
    return lines;
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
    auto text = to_winansi(operation.value.text.text_utf8);
    auto lines = layout_lines(
        text, options, operation.bounds.x1 - operation.bounds.x0);
    double const line_height =
        options.font_size * options.line_height_multiplier;
    double y = page.height_points - operation.bounds.y0 - options.font_size;
    double const red = ((options.argb >> 16u) & 0xffu) / 255.0;
    double const green = ((options.argb >> 8u) & 0xffu) / 255.0;
    double const blue = (options.argb & 0xffu) / 255.0;
    for (auto const& line: lines) {
        double const width = text_width(line, options.font, options.font_size);
        double x = operation.bounds.x0;
        if (options.alignment == QUANTAPDF_COMPOSER_TEXT_ALIGN_CENTER)
            x += (operation.bounds.x1 - operation.bounds.x0 - width) / 2.0;
        else if (options.alignment == QUANTAPDF_COMPOSER_TEXT_ALIGN_RIGHT)
            x = operation.bounds.x1 - width;
        if (y < page.height_points - operation.bounds.y1)
            break;
        content += "BT /F" + std::to_string(static_cast<int>(options.font)) +
            " " + number(options.font_size) + " Tf " + number(red) + " " +
            number(green) + " " + number(blue) + " rg 1 0 0 1 " + number(x) +
            " " + number(y) + " Tm " + pdf_string(line) + " Tj ET\n";
        y -= line_height;
    }
}


struct embedded_glyph_item {
    uint32_t codepoint = 0u;
    uint16_t glyph = 0u;
    int width = 0;
};

struct embedded_text_line {
    std::vector<embedded_glyph_item> glyphs;
    double width_points = 0.0;
};

struct embedded_font_usage {
    bool referenced = false;
    std::map<uint16_t, uint32_t> glyph_to_unicode;
};

double embedded_line_width(
    std::vector<embedded_glyph_item> const& glyphs,
    double font_size)
{
    double units = 0.0;
    for (auto const& glyph: glyphs)
        units += glyph.width;
    return units * font_size / 1000.0;
}

std::vector<embedded_text_line> layout_embedded_lines(
    char const* text,
    quantapdf_composer_embedded_text_options const& options,
    quantapdf::detail::ttf_font_face const& face,
    double max_width)
{
    std::vector<uint32_t> codepoints;
    if (quantapdf::detail::decode_utf8_codepoints(text, &codepoints) !=
        QUANTAPDF_OK)
        throw std::invalid_argument("invalid embedded text utf8");

    std::vector<embedded_text_line> lines;
    std::vector<embedded_glyph_item> line;
    size_t last_space = std::string::npos;

    auto recompute_space = [&]() {
        last_space = std::string::npos;
        for (size_t i = 0u; i < line.size(); ++i) {
            if (line[i].codepoint == 0x20u)
                last_space = i;
        }
    };
    auto publish = [&]() {
        while (!line.empty() && line.back().codepoint == 0x20u)
            line.pop_back();
        embedded_text_line published;
        published.glyphs = line;
        published.width_points =
            embedded_line_width(published.glyphs, options.font_size);
        lines.push_back(std::move(published));
        line.clear();
        last_space = std::string::npos;
    };

    for (uint32_t cp: codepoints) {
        if (cp == '\r')
            continue;
        if (cp == '\n') {
            publish();
            continue;
        }
        if (cp == '\t')
            cp = 0x20u;
        uint16_t const glyph = face.glyph_for(cp);
        if (glyph == 0u)
            throw std::invalid_argument("embedded font missing glyph");
        line.push_back({cp, glyph, face.width_for(glyph)});
        if (cp == 0x20u)
            last_space = line.size() - 1u;

        if (options.wrap && line.size() > 1u &&
            embedded_line_width(line, options.font_size) > max_width) {
            if (last_space != std::string::npos) {
                std::vector<embedded_glyph_item> remainder(
                    line.begin() + static_cast<std::ptrdiff_t>(last_space + 1u),
                    line.end());
                line.resize(last_space);
                publish();
                line = std::move(remainder);
                recompute_space();
            } else {
                auto overflow = line.back();
                line.pop_back();
                publish();
                line.push_back(overflow);
                recompute_space();
            }
        }
    }
    if (!line.empty() || codepoints.empty() ||
        (!codepoints.empty() && codepoints.back() == '\n'))
        publish();
    return lines;
}

std::string glyph_hex(std::vector<embedded_glyph_item> const& glyphs)
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
    auto const& face = faces[options.font_id - 1u];
    auto lines = layout_embedded_lines(
        operation.value.embedded_text.text_utf8,
        options,
        face,
        operation.bounds.x1 - operation.bounds.x0);
    double const line_height =
        options.font_size * options.line_height_multiplier;
    double y = page.height_points - operation.bounds.y0 - options.font_size;
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
        if (y < page.height_points - operation.bounds.y1)
            break;
        content +=
            "BT /EF" + std::to_string(options.font_id) + " " +
            number(options.font_size) + " Tf " +
            number(red) + " " + number(green) + " " + number(blue) +
            " rg 1 0 0 1 " + number(x) + " " + number(y) +
            " Tm " + glyph_hex(line.glyphs) + " Tj ET\n";
        y -= line_height;
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
        double const display_y =
            pen_y + static_cast<double>(glyph.y_offset) * scale;
        double const pdf_y = page.height_points - display_y;
        content +=
            "1 0 0 1 " + number(x) + " " + number(pdf_y) +
            " Tm " + glyph_run_cid_hex(found->second) + " Tj ";
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
    if (options.stroke) {
        append_color(options.stroke_argb, "RG");
        content += number(options.stroke_width) + " w " +
            std::to_string(static_cast<int>(options.line_cap)) + " J " +
            std::to_string(static_cast<int>(options.line_join)) + " j " +
            number(options.miter_limit) + " M ";
    }
    if (options.fill)
        append_color(options.fill_argb, "rg");

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
    content += "q " + number(red) + " " + number(green) + " " +
        number(blue) + " rg 0 0 " + number(page.width_points) + " " +
        number(page.height_points) + " re f Q\n";

    for (std::size_t i = 0; i < composer->operation_count; ++i) {
        auto const& operation = composer->operations[i];
        if (operation.page_index != page_index)
            continue;
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
        }
    }
    return content;
}


void store_text_measurement(
    double width,
    double height,
    size_t line_count,
    quantapdf_composer_text_measurement* out_measurement)
{
    if (!std::isfinite(width) || !std::isfinite(height) ||
        width < 0.0 || height < 0.0 ||
        width > std::numeric_limits<float>::max() ||
        height > std::numeric_limits<float>::max())
        throw std::overflow_error("text measurement overflow");
    out_measurement->width_points = static_cast<float>(width);
    out_measurement->height_points = static_cast<float>(height);
    out_measurement->line_count = line_count;
}

} // namespace

extern "C" quantapdf_status quantapdf_qpdf_measure_base14_text(
    char const* text_utf8,
    float max_width_points,
    quantapdf_composer_text_options const* options,
    quantapdf_composer_text_measurement* out_measurement)
{
    if (text_utf8 == nullptr || options == nullptr ||
        out_measurement == nullptr ||
        out_measurement->struct_size <
            QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_MIN_SIZE)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        auto const text = to_winansi(text_utf8);
        auto const lines =
            layout_lines(text, *options, max_width_points);
        double max_width = 0.0;
        for (auto const& line: lines) {
            max_width = std::max(
                max_width,
                text_width(line, options->font, options->font_size));
        }
        double const height = lines.empty()
            ? 0.0
            : options->font_size +
                static_cast<double>(lines.size() - 1u) *
                    options->font_size *
                    options->line_height_multiplier;
        store_text_measurement(
            max_width, height, lines.size(), out_measurement);
        return QUANTAPDF_OK;
    } catch (std::invalid_argument const&) {
        return QUANTAPDF_ERROR_FORMAT;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

extern "C" quantapdf_status quantapdf_qpdf_measure_embedded_text(
    unsigned char const* font_data,
    size_t font_size,
    char const* text_utf8,
    float max_width_points,
    quantapdf_composer_embedded_text_options const* options,
    quantapdf_composer_text_measurement* out_measurement)
{
    if (font_data == nullptr || font_size == 0u ||
        text_utf8 == nullptr || options == nullptr ||
        out_measurement == nullptr ||
        out_measurement->struct_size <
            QUANTAPDF_COMPOSER_TEXT_MEASUREMENT_V1_MIN_SIZE)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        quantapdf::detail::ttf_font_face face;
        quantapdf_status const parse_status =
            quantapdf::detail::ttf_font_face::parse(
                font_data, font_size, &face);
        if (parse_status != QUANTAPDF_OK)
            return parse_status;

        auto const lines = layout_embedded_lines(
            text_utf8, *options, face, max_width_points);
        double max_width = 0.0;
        for (auto const& line: lines)
            max_width = std::max(max_width, line.width_points);
        double const height = lines.empty()
            ? 0.0
            : options->font_size +
                static_cast<double>(lines.size() - 1u) *
                    options->font_size *
                    options->line_height_multiplier;
        store_text_measurement(
            max_width, height, lines.size(), out_measurement);
        return QUANTAPDF_OK;
    } catch (std::invalid_argument const&) {
        return QUANTAPDF_ERROR_FORMAT;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

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

        for (std::size_t page_index = 0; page_index < composer->page_count;
             ++page_index) {
            auto page = QPDFObjectHandle::newDictionary();
            auto media_box = QPDFObjectHandle::newArray();
            auto resources = QPDFObjectHandle::newDictionary();
            auto fonts = QPDFObjectHandle::newDictionary();
            auto xobjects = QPDFObjectHandle::newDictionary();
            bool used_fonts[12] = {};

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
                if (operation.page_index == page_index &&
                    operation.kind == QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
                    auto id = operation.value.image.image_id;
                    xobjects.replaceKey(
                        "/Im" + std::to_string(id), image_objects[id - 1u]);
                }
            }
            resources.replaceKey("/XObject", xobjects);
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
