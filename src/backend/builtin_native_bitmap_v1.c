#include "backend/builtin_native_bitmap_v1.h"
#include "render/native_backend_rgb_v1.h"

#include <string.h>

/* The provider's private Page lease is borrowed, not retained. This
 * entry is deliberately separate from builtin_ops.render_page: the
 * product/public callbacks continue to render through PDFium. */
static quantapdf_status builtin_native_bitmap_impl(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_backend_bitmap *out_bitmap,
    int decode_v9)
{
    quantapdf_status status;
    quantapdf_ir_raster_image native = {0};

    if (out_bitmap == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_bitmap, 0, sizeof(*out_bitmap));
    if (limits == NULL || device == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = decode_v9 ?
        quantapdf_builtin_native_qualify_page_v9(
            builtin_page, limits, device, &native) :
        quantapdf_builtin_native_qualify_page_v1(
            builtin_page, limits, device, &native);
    if (status == QUANTAPDF_OK)
        status = quantapdf_native_rgba_to_backend_rgb_v1(
            &native, limits->raster.max_pixels, out_bitmap);
    quantapdf_ir_raster_image_dispose(&native);
    return status;
}

quantapdf_status quantapdf_builtin_native_qualify_bitmap_v1(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_backend_bitmap *out_bitmap)
{
    return builtin_native_bitmap_impl(
        builtin_page, limits, device, out_bitmap, 0);
}

quantapdf_status quantapdf_builtin_native_qualify_bitmap_v9(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_backend_bitmap *out_bitmap)
{
    return builtin_native_bitmap_impl(
        builtin_page, limits, device, out_bitmap, 1);
}
