#ifndef QUANTAPDF_BACKEND_TTF_FONT_H
#define QUANTAPDF_BACKEND_TTF_FONT_H

#include <stddef.h>

#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace quantapdf::detail {

enum class sfnt_outline_kind {
    true_type,
    cff
};

struct ttf_font_face {
    unsigned char const* data = nullptr;
    sfnt_outline_kind outline_kind = sfnt_outline_kind::true_type;
    size_t size = 0u;
    uint16_t units_per_em = 0u;
    uint16_t num_glyphs = 0u;
    uint16_t num_h_metrics = 0u;
    size_t hmtx_offset = 0u;
    size_t hmtx_size = 0u;
    size_t cmap_offset = 0u;
    size_t cmap_size = 0u;
    uint16_t cmap_format = 0u;
    int16_t bbox[4] = {};
    double ascent = 0.0;
    double descent = 0.0;
    double cap_height = 0.0;
    double italic_angle = 0.0;
    double stem_v = 80.0;
    int flags = 32;
    std::string postscript_name;

    static quantapdf_status parse(
        unsigned char const* data,
        size_t size,
        ttf_font_face* out);

    uint16_t glyph_for(uint32_t codepoint) const;
    int width_for(uint16_t glyph) const;
};

quantapdf_status decode_utf8_codepoints(
    char const* text,
    std::vector<uint32_t>* out);

quantapdf_status subset_true_type_font(
    ttf_font_face const& face,
    std::map<uint16_t, uint32_t> const& used_glyphs,
    std::vector<unsigned char>* out_font);


} // namespace quantapdf::detail
#endif

#ifdef __cplusplus
extern "C" {
#endif

quantapdf_status quantapdf_ttf_validate(
    const unsigned char *data,
    size_t size);

quantapdf_status quantapdf_ttf_validate_text(
    const unsigned char *data,
    size_t size,
    const char *text_utf8);

quantapdf_status quantapdf_ttf_glyph_count(
    const unsigned char *data,
    size_t size,
    uint32_t *out_glyph_count);

#ifdef __cplusplus
}
#endif

#endif
