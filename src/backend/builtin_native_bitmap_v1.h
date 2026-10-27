#ifndef QUANTAPDF_BUILTIN_NATIVE_BITMAP_V1_H
#define QUANTAPDF_BUILTIN_NATIVE_BITMAP_V1_H

#include "backend/builtin_native_page_v1.h"
#include "backend/document_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A PRIVATE built-in page qualification producing the exact malloc-owned
 * RGB8 backend bitmap contract. It does NOT replace
 * quantapdf_builtin_backend_ops()->render_page (still PDFium-backed).
 * No transparent/rotated/thumbnail/device-clipped fallback is permitted. */
quantapdf_status quantapdf_builtin_native_qualify_bitmap_v1(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_backend_bitmap *out_bitmap);

/* Private V9 source selection through the same malloc-owned RGB8
 * conversion; the V8 public/private bridge stays strict. */
quantapdf_status quantapdf_builtin_native_qualify_bitmap_v9(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_backend_bitmap *out_bitmap);

/* Private V10 through the same RGBA→uniquely-owned RGB bitmap transfer,
 * not the PDFium-backed public provider render callback. */
quantapdf_status quantapdf_builtin_native_qualify_bitmap_v10(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_backend_bitmap *out_bitmap);

#ifdef __cplusplus
}
#endif
#endif
