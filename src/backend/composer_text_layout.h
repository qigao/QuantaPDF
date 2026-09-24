#ifndef QUANTAPDF_BACKEND_COMPOSER_TEXT_LAYOUT_H
#define QUANTAPDF_BACKEND_COMPOSER_TEXT_LAYOUT_H

#include <stddef.h>

#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
#include "ttf_font.h"

#include <cstdint>
#include <string>
#include <vector>

namespace quantapdf::detail {

struct base14_text_line {
    std::string text;
    double width_points = 0.0;
};

struct embedded_glyph_item {
    std::uint32_t codepoint = 0u;
    std::uint16_t glyph = 0u;
    int width = 0;
};

struct embedded_text_line {
    std::vector<embedded_glyph_item> glyphs;
    double width_points = 0.0;
};

quantapdf_status layout_base14_text(
    char const* text_utf8,
    quantapdf_composer_text_options const& options,
    double max_width,
    std::vector<base14_text_line>* out_lines);

quantapdf_status layout_embedded_text(
    char const* text_utf8,
    quantapdf_composer_embedded_text_options const& options,
    ttf_font_face const& face,
    double max_width,
    std::vector<embedded_text_line>* out_lines);

} // namespace quantapdf::detail

extern "C" {
#endif

quantapdf_status quantapdf_measure_base14_text_internal(
    const char *text_utf8,
    float max_width,
    const quantapdf_composer_text_options *options,
    quantapdf_composer_text_measurement *out_measurement);

quantapdf_status quantapdf_measure_embedded_text_internal(
    const unsigned char *font_data,
    size_t font_size,
    const char *text_utf8,
    float max_width,
    const quantapdf_composer_embedded_text_options *options,
    quantapdf_composer_text_measurement *out_measurement);

#ifdef __cplusplus
}
#endif

#endif
