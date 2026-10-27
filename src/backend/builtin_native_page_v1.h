#ifndef QUANTAPDF_BUILTIN_NATIVE_PAGE_V1_H
#define QUANTAPDF_BUILTIN_NATIVE_PAGE_V1_H

#include "backend/native_source_raster_bridge_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private built-in-provider page qualification, not a public SDK API.
 * page is the opaque handle returned by
 * quantapdf_builtin_backend_ops()->load_page(); it borrows the document.
 *
 * This function NEVER changes built-in render_page dispatch or loads a
 * PDFium page. It refuses nonzero page, unsupported device modes, or
 * password-bearing source contexts instead of silently retrying with
 * legacy PDFium. A successful RGBA result belongs to the caller and is
 * disposed with quantapdf_ir_raster_image_dispose(). Failure resets it. */
quantapdf_status quantapdf_builtin_native_qualify_page_v1(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image);

/* Same private borrowed Page admission, but Source V9 is explicitly
 * selected. A failure publishes no pixels and never calls PDFium. */
quantapdf_status quantapdf_builtin_native_qualify_page_v9(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image);

/* Private V10: same borrowed provider Page and rejection/ownership
 * checks, only the Source admission enables qualified Flate predictors. */
quantapdf_status quantapdf_builtin_native_qualify_page_v10(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif
#endif
