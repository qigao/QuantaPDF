#ifndef QUANTAPDF_NATIVE_SOURCE_RASTER_BRIDGE_V1_H
#define QUANTAPDF_NATIVE_SOURCE_RASTER_BRIDGE_V1_H

#include "backend/qpdf_native_image_ir.h"
#include "render/ir_raster_scoped_mixed_v5.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private only. No public C11 ABI, SDK header or provider fallback.
 * Source limits and C11 raster limits remain separately bounded. */
typedef struct quantapdf_native_source_raster_bridge_limits_v1 {
    quantapdf_qpdf_source_raw_image_limits source;
    quantapdf_ir_raster_scoped_mixed_v5_limits raster;
} quantapdf_native_source_raster_bridge_limits_v1;

/* Fail-closed device gate: only one original integer-pixel 72-DPI page,
 * no rotation, region Clip, transparent page or thumbnail semantics.
 * All other settings are refused, not approximated by the first slice. */
typedef struct quantapdf_native_source_raster_device_v1 {
    size_t page_index;
    float dpi;
    float rotation_degrees;
    int clip_enabled;
    int alpha;
    int thumbnail;
} quantapdf_native_source_raster_device_v1;

/* Original bytes -> complete strict Source V8 admission -> uniquely owned
 * detached Composer IR -> provider-free C11 mixed Raster RGBA.
 * QPDF owner destruction occurs inside source admission BEFORE raster.
 * Published pixels are owned by the caller, who must dispose them with
 * quantapdf_ir_raster_image_dispose(). All failure paths reset output.
 *
 * This private function does NOT reroute public quantapdf_render_page(),
 * render options, or thumbnail calls; production remains PDFium-backed. */
quantapdf_status quantapdf_native_source_raster_bridge_v1(
    const unsigned char *original_pdf,
    size_t original_size,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif
#endif
