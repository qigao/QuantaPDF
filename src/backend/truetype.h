#ifndef QUANTAPDF_BACKEND_TRUETYPE_H
#define QUANTAPDF_BACKEND_TRUETYPE_H

#include <stddef.h>
#include <stdint.h>

#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct quantapdf_truetype_info {
    uint16_t units_per_em;
    uint16_t num_glyphs;
    uint16_t num_hmetrics;
    int16_t x_min;
    int16_t y_min;
    int16_t x_max;
    int16_t y_max;
    int16_t ascender;
    int16_t descender;
    uint16_t cmap_format;
    size_t cmap_offset;
    size_t cmap_length;
    size_t hmtx_offset;
    size_t hmtx_length;
} quantapdf_truetype_info;

quantapdf_status quantapdf_truetype_inspect(
    const unsigned char *data,
    size_t size,
    quantapdf_truetype_info *out_info);

quantapdf_status quantapdf_truetype_glyph(
    const unsigned char *data,
    size_t size,
    const quantapdf_truetype_info *info,
    uint32_t codepoint,
    uint16_t *out_glyph);

quantapdf_status quantapdf_truetype_advance(
    const unsigned char *data,
    size_t size,
    const quantapdf_truetype_info *info,
    uint16_t glyph,
    uint16_t *out_advance);

#ifdef __cplusplus
}
#endif

#endif
