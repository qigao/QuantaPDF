#ifndef QUANTAPDF_BACKEND_QPDF_COMPOSER_H
#define QUANTAPDF_BACKEND_QPDF_COMPOSER_H

#include <stddef.h>

#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct quantapdf_ir quantapdf_ir;

quantapdf_status quantapdf_qpdf_compose(
    const quantapdf_ir *ir,
    unsigned char **out_data,
    size_t *out_size);

quantapdf_status quantapdf_png_decode(
    const unsigned char *data,
    size_t size,
    size_t max_decoded_bytes,
    unsigned char **out_rgb,
    size_t *out_rgb_size,
    unsigned char **out_alpha,
    size_t *out_alpha_size,
    uint32_t *out_width,
    uint32_t *out_height);

#define QUANTAPDF_JPEG_MAX_SCAN_BLOCKS ((size_t)16u * 1024u * 1024u)

typedef struct quantapdf_jpeg_limits {
    size_t max_working_bytes; /* Encoded bytes, output samples and coefficient arrays. */
    size_t max_scan_blocks;   /* Total MCU data units visited by a multi-scan decode. */
} quantapdf_jpeg_limits;

typedef struct quantapdf_jpeg_layout {
    uint32_t width, height;
    int components;
} quantapdf_jpeg_layout;

typedef struct quantapdf_jpeg_info {
    quantapdf_jpeg_layout layout;
    int color_transform;
} quantapdf_jpeg_info;

/* The resolved transform describes the encoded samples, for PDF embedding. */
quantapdf_status quantapdf_jpeg_validate(
    const unsigned char *data, size_t size, quantapdf_jpeg_limits limits,
    quantapdf_jpeg_info *out_info);

/* Decode a PDF image into a private width*height*components byte buffer.
 * The JPEG header must match layout before scanline decoding starts.
 * ColorTransform is -1 (PDF default), 0 (none), or 1 (YCbCr to RGB).
 * An Adobe APP14 marker takes precedence. Discard the buffer on failure. */
quantapdf_status quantapdf_jpeg_decode(
    const unsigned char *data, size_t size, quantapdf_jpeg_limits limits,
    int color_transform, quantapdf_jpeg_layout layout, unsigned char *pixels);

#if defined(QUANTAPDF_TESTING)
#if defined(__GNUC__)
__attribute__((visibility("hidden")))
#endif
void quantapdf_jpeg_force_oom_for_testing(int enabled);
#endif

#ifdef __cplusplus
}
#endif

#endif
