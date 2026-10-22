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

quantapdf_status quantapdf_jpeg_validate(
    const unsigned char *data,
    size_t size,
    size_t max_working_bytes,
    uint32_t *out_width,
    uint32_t *out_height,
    int *out_components);

/* Detached malloc-owned Gray/RGB samples. PDF DCT ColorTransform is -1
 * (JPEG metadata), 0 (no transform), or 1 (YCbCr -> RGB). */
quantapdf_status quantapdf_jpeg_decode(
    const unsigned char *data, size_t size, size_t max_working_bytes,
    int color_transform, unsigned char **out_pixels,
    uint32_t *out_width, uint32_t *out_height, int *out_components);

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
