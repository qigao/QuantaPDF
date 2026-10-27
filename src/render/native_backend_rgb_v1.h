#ifndef QUANTAPDF_NATIVE_BACKEND_RGB_V1_H
#define QUANTAPDF_NATIVE_BACKEND_RGB_V1_H

#include "backend/document_backend.h"
#include "render/ir_raster.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private, provider-free C11 output-format gate. It borrows a complete,
 * opaque, tightly packed Native RGBA8 source and publishes independently
 * malloc-owned RGB8 samples compatible with quantapdf_backend_bitmap.
 * The caller frees out_bitmap->data with free(). No qpdf/PDFium ABI or
 * runtime dependency; no public renderer or provider dispatch change. */
quantapdf_status quantapdf_native_rgba_to_backend_rgb_v1(
    const quantapdf_ir_raster_image *rgba,
    size_t max_pixels,
    quantapdf_backend_bitmap *out_bitmap);

#ifdef __cplusplus
}
#endif
#endif
