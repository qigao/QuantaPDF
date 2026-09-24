#include "composer_text_layout.h"

#include "base14_metrics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quantapdf::detail {
namespace {

std::optional<unsigned char> winansi_byte(std::uint32_t codepoint)
{
    struct mapping {
        std::uint32_t unicode;
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
    if (codepoint <= 0x7fu || (codepoint >= 0xa0u && codepoint <= 0xffu))
        return static_cast<unsigned char>(codepoint);
    for (auto const& item: special) {
        if (item.unicode == codepoint)
            return item.byte;
    }
    return std::nullopt;
}

quantapdf_status to_winansi(char const* utf8, std::string* out)
{
    if (utf8 == nullptr || out == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    try {
        out->clear();
        auto cursor = reinterpret_cast<unsigned char const*>(utf8);
        while (*cursor != 0u) {
            std::uint32_t codepoint;
            size_t count;
            if (*cursor < 0x80u) {
                codepoint = *cursor;
                count = 1u;
            } else if (*cursor >= 0xc2u && *cursor <= 0xdfu) {
                codepoint = static_cast<std::uint32_t>(*cursor & 0x1fu);
                count = 2u;
            } else if (*cursor >= 0xe0u && *cursor <= 0xefu) {
                codepoint = static_cast<std::uint32_t>(*cursor & 0x0fu);
                count = 3u;
            } else {
                return QUANTAPDF_ERROR_FORMAT;
            }
            for (size_t i = 1u; i < count; ++i) {
                if (cursor[i] == 0u || (cursor[i] & 0xc0u) != 0x80u)
                    return QUANTAPDF_ERROR_FORMAT;
                codepoint = (codepoint << 6u) |
                    static_cast<std::uint32_t>(cursor[i] & 0x3fu);
            }
            if ((count == 2u && codepoint < 0x80u) ||
                (count == 3u && codepoint < 0x800u) ||
                (codepoint >= 0xd800u && codepoint <= 0xdfffu))
                return QUANTAPDF_ERROR_FORMAT;
            auto const mapped = winansi_byte(codepoint);
            if (!mapped.has_value())
                return QUANTAPDF_ERROR_FORMAT;
            out->push_back(static_cast<char>(*mapped));
            cursor += count;
        }
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    }
}

double base14_line_width(
    std::string const& text,
    quantapdf_composer_font font,
    double font_size)
{
    double units = 0.0;
    for (unsigned char glyph: text)
        units += base14_glyph_width(glyph, font);
    return units * font_size / 1000.0;
}

double embedded_line_width(
    std::vector<embedded_glyph_item> const& glyphs,
    double font_size)
{
    double units = 0.0;
    for (auto const& glyph: glyphs)
        units += glyph.width;
    return units * font_size / 1000.0;
}

template <typename Line>
quantapdf_status summarize_lines(
    std::vector<Line> const& lines,
    double font_size,
    double line_height_multiplier,
    quantapdf_composer_text_measurement* out)
{
    if (out == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    double width = 0.0;
    for (auto const& line: lines)
        width = std::max(width, line.width_points);

    double height = font_size;
    if (lines.size() > 1u) {
        double const step = font_size * line_height_multiplier;
        height += static_cast<double>(lines.size() - 1u) * step;
    }

    double const max_float = std::numeric_limits<float>::max();
    if (!std::isfinite(width) || !std::isfinite(height) ||
        width < 0.0 || height < 0.0 ||
        width > max_float || height > max_float)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    out->width = static_cast<float>(width);
    out->height = static_cast<float>(height);
    out->line_count = lines.size();
    return QUANTAPDF_OK;
}

} // namespace

quantapdf_status layout_base14_text(
    char const* text_utf8,
    quantapdf_composer_text_options const& options,
    double max_width,
    std::vector<base14_text_line>* out_lines)
{
    if (text_utf8 == nullptr || out_lines == nullptr ||
        !std::isfinite(max_width) || max_width <= 0.0)
        return QUANTAPDF_ERROR_ARGUMENT;

    std::string text;
    quantapdf_status const conversion = to_winansi(text_utf8, &text);
    if (conversion != QUANTAPDF_OK)
        return conversion;

    try {
        out_lines->clear();
        std::string line;
        std::size_t last_space = std::string::npos;

        auto publish = [&]() {
            while (!line.empty() && line.back() == ' ')
                line.pop_back();
            base14_text_line published;
            published.text = line;
            published.width_points =
                base14_line_width(line, options.font, options.font_size);
            out_lines->push_back(std::move(published));
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
                base14_line_width(line, options.font, options.font_size) >
                    max_width) {
                if (last_space != std::string::npos) {
                    std::string remainder = line.substr(last_space + 1u);
                    line.resize(last_space);
                    publish();
                    line = std::move(remainder);
                } else {
                    char const overflow = line.back();
                    line.pop_back();
                    publish();
                    line.push_back(overflow);
                }
            }
        }

        if (!line.empty() || text.empty() || text.back() == '\n')
            publish();
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        out_lines->clear();
        return QUANTAPDF_ERROR_NOMEM;
    }
}

quantapdf_status layout_embedded_text(
    char const* text_utf8,
    quantapdf_composer_embedded_text_options const& options,
    ttf_font_face const& face,
    double max_width,
    std::vector<embedded_text_line>* out_lines)
{
    if (text_utf8 == nullptr || out_lines == nullptr ||
        !std::isfinite(max_width) || max_width <= 0.0)
        return QUANTAPDF_ERROR_ARGUMENT;

    std::vector<std::uint32_t> codepoints;
    quantapdf_status const decoded =
        decode_utf8_codepoints(text_utf8, &codepoints);
    if (decoded != QUANTAPDF_OK)
        return decoded;

    try {
        out_lines->clear();
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
            out_lines->push_back(std::move(published));
            line.clear();
            last_space = std::string::npos;
        };

        for (std::uint32_t cp: codepoints) {
            if (cp == '\r')
                continue;
            if (cp == '\n') {
                publish();
                continue;
            }
            if (cp == '\t')
                cp = 0x20u;

            std::uint16_t const glyph = face.glyph_for(cp);
            if (glyph == 0u) {
                out_lines->clear();
                return QUANTAPDF_ERROR_FORMAT;
            }
            line.push_back({cp, glyph, face.width_for(glyph)});
            if (cp == 0x20u)
                last_space = line.size() - 1u;

            if (options.wrap && line.size() > 1u &&
                embedded_line_width(line, options.font_size) > max_width) {
                if (last_space != std::string::npos) {
                    std::vector<embedded_glyph_item> remainder(
                        line.begin() +
                            static_cast<std::ptrdiff_t>(last_space + 1u),
                        line.end());
                    line.resize(last_space);
                    publish();
                    line = std::move(remainder);
                    recompute_space();
                } else {
                    embedded_glyph_item const overflow = line.back();
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
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        out_lines->clear();
        return QUANTAPDF_ERROR_NOMEM;
    }
}

} // namespace quantapdf::detail

extern "C" quantapdf_status quantapdf_measure_base14_text_internal(
    const char* text_utf8,
    float max_width,
    const quantapdf_composer_text_options* options,
    quantapdf_composer_text_measurement* out_measurement)
{
    if (options == nullptr || out_measurement == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    std::vector<quantapdf::detail::base14_text_line> lines;
    quantapdf_status const status = quantapdf::detail::layout_base14_text(
        text_utf8, *options, max_width, &lines);
    if (status != QUANTAPDF_OK)
        return status;
    return quantapdf::detail::summarize_lines(
        lines,
        options->font_size,
        options->line_height_multiplier,
        out_measurement);
}

extern "C" quantapdf_status quantapdf_measure_embedded_text_internal(
    const unsigned char* font_data,
    size_t font_size,
    const char* text_utf8,
    float max_width,
    const quantapdf_composer_embedded_text_options* options,
    quantapdf_composer_text_measurement* out_measurement)
{
    if (font_data == nullptr || font_size == 0u || options == nullptr ||
        out_measurement == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    quantapdf::detail::ttf_font_face face;
    quantapdf_status const parsed =
        quantapdf::detail::ttf_font_face::parse(font_data, font_size, &face);
    if (parsed != QUANTAPDF_OK)
        return parsed;

    std::vector<quantapdf::detail::embedded_text_line> lines;
    quantapdf_status const status = quantapdf::detail::layout_embedded_text(
        text_utf8, *options, face, max_width, &lines);
    if (status != QUANTAPDF_OK)
        return status;
    return quantapdf::detail::summarize_lines(
        lines,
        options->font_size,
        options->line_height_multiplier,
        out_measurement);
}
