#ifndef QUANTAPDF_PRIVATE_NATIVE_FACADE_BITMAP_V1_H
#define QUANTAPDF_PRIVATE_NATIVE_FACADE_BITMAP_V1_H

#include "backend/native_source_raster_bridge_v1.h"
#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PRIVATE facade lifecycle qualification, not a public ABI/export.
 * The page is a borrowed, live public C facade page. This publishes an
 * ordinary quantapdf_bitmap with its own retained Context reference
 * and separately malloc-owned RGB data. Existing bitmap_dimensions,
 * bitmap_data and drop_bitmap consume it without extra special cases.
 *
 * This does NOT replace quantapdf_render_page(), render_page_with_options()
 * or render_thumbnail(): those continue using PDFium. Unsupported source
 * and device modes refuse atomically; never retry through legacy render. */
quantapdf_status quantapdf_render_page_native_qualified_v1(
    quantapdf_page *page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_bitmap **out_bitmap);

/* Private Source V9 through an ACTUAL facade Page and Context. Source
 * V8 and public page/options/thumbnail dispatch remain unchanged; this
 * is never installed or exported in the public C ABI. */
quantapdf_status quantapdf_render_page_native_qualified_v9(
    quantapdf_page *page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_bitmap **out_bitmap);

/* Opt-in private Source V10 through an ordinary Context-retained
 * facade bitmap. Never exported as a public SDK API or renderer mode. */
quantapdf_status quantapdf_render_page_native_qualified_v10(
    quantapdf_page *page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_bitmap **out_bitmap);

#ifdef __cplusplus
}
#endif
#endif
