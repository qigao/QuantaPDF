#include "render/native_backend_rgb_v1.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Product-side private RGB layout adapter. Both the incoming RGBA buffer
 * and returned RGB buffer are detached; the converter never retains a
 * source pointer or calls QPDF/PDFium or the legacy renderer. */
quantapdf_status quantapdf_native_rgba_to_backend_rgb_v1(
    const quantapdf_ir_raster_image *rgba,
    size_t max_pixels,
    quantapdf_backend_bitmap *out_bitmap)
{
    size_t pixels, source_stride, target_stride, source_bytes, target_bytes;
    size_t i;
    unsigned char *target;

    if (out_bitmap == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_bitmap, 0, sizeof(*out_bitmap));
    if (rgba == NULL || max_pixels == 0u)
        return QUANTAPDF_ERROR_ARGUMENT;

    if (rgba->pixels == NULL || rgba->width == 0u || rgba->height == 0u ||
        rgba->width > (size_t)INT_MAX / 3u ||
        rgba->height > (size_t)INT_MAX ||
        rgba->width > SIZE_MAX / rgba->height)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    pixels = rgba->width * rgba->height;
    if (pixels > max_pixels || pixels > (size_t)1048576u ||
        rgba->width > SIZE_MAX / 4u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    source_stride = rgba->width * 4u;
    target_stride = rgba->width * 3u;
    if (source_stride > SIZE_MAX / rgba->height ||
        target_stride > SIZE_MAX / rgba->height)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    source_bytes = source_stride * rgba->height;
    target_bytes = target_stride * rgba->height;
    if (rgba->stride != source_stride || rgba->size != source_bytes ||
        target_bytes != pixels * 3u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Strictly opaque first slice, without alpha flattening or silently
     * accepting semitransparent output. Validate the WHOLE source before
     * allocating/publishing a single output byte. */
    for (i = 0u; i < pixels; ++i)
        if (rgba->pixels[i * 4u + 3u] != 255u)
            return QUANTAPDF_ERROR_UNSUPPORTED;

    target = (unsigned char *)malloc(target_bytes);
    if (target == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    for (i = 0u; i < pixels; ++i) {
        target[3u * i] = rgba->pixels[4u * i];
        target[3u * i + 1u] = rgba->pixels[4u * i + 1u];
        target[3u * i + 2u] = rgba->pixels[4u * i + 2u];
    }
    out_bitmap->data = target;
    out_bitmap->size = target_bytes;
    out_bitmap->width = (int)rgba->width;
    out_bitmap->height = (int)rgba->height;
    out_bitmap->stride = (int)target_stride;
    out_bitmap->components = 3;
    return QUANTAPDF_OK;
}
