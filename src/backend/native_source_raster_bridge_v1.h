#ifndef QUANTAPDF_NATIVE_SOURCE_RASTER_BRIDGE_V1_H
#define QUANTAPDF_NATIVE_SOURCE_RASTER_BRIDGE_V1_H

#include "backend/qpdf_native_image_ir.h"
#include "render/ir_raster_scoped_mixed_v5.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PRIVATE, explicit Source profile selection across one product-owned
 * pipeline. Never persist this enum, export it, or silently select an
 * earlier profile for an unknown value. */
typedef enum quantapdf_private_native_source_profile {
    QUANTAPDF_PRIVATE_NATIVE_SOURCE_V8 = 0,
    QUANTAPDF_PRIVATE_NATIVE_SOURCE_V9 = 1,
    QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10 = 2
} quantapdf_private_native_source_profile;

/* Private only. No public C11 ABI, SDK header or provider fallback.
 * Source limits and C11 raster limits remain separately bounded. */
typedef struct quantapdf_native_source_raster_bridge_limits_v1 {
    quantapdf_qpdf_source_raw_image_limits source;
    quantapdf_ir_raster_scoped_mixed_v5_limits raster;
} quantapdf_native_source_raster_bridge_limits_v1;

/* Original PDF page index belongs to the borrowed backend Page lease,
 * NOT to the single-page C11 IR slot. Historical V8/V9 require index
 * zero/72 DPI; public V10 alone resolves a bounded selected original
 * PageTree leaf, at 72/96/144 DPI. Region/alpha/thumbnail controls
 * remain strictly rejected; caller display turns are post-raster. */
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
 * Historical V8 Source admission is still private and 72-DPI-only;
 * public page/option/thumbnail callbacks use the V10 bridge below,
 * with one pure C11 Raster and no reference renderer or fallback. */
quantapdf_status quantapdf_native_source_raster_bridge_v1(
    const unsigned char *original_pdf,
    size_t original_size,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image);

/* Private explicit Source V9 entry. V8 above preserves its exact legacy
 * rejection of every explicit Image /Decode. Neither entry changes the
 * public renderer or the C11 raster implementation/caps. */
quantapdf_status quantapdf_native_source_raster_bridge_v9(
    const unsigned char *original_pdf,
    size_t original_size,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image);

/* Public Source V10: strict 8-bit TIFF/PNG Predictor /DecodeParms,
 * exact 72-DPI Native or qualified direct 96/144-DPI device-space
 * projection. The complete original decimal TeXample uses the physical
 * 72/96/144-DPI shared-geometry paths under unchanged 512-edge/8M caller
 * limits. Higher DPI classifies original cubics at the target sample grid,
 * scales the actual width by DPI/72 and intersects certified row spans. Same C11 raster, distinct output/source
 * budgets and no provider or earlier-route retry. */
quantapdf_status quantapdf_native_source_raster_bridge_v10(
    const unsigned char *original_pdf,
    size_t original_size,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif
#endif
