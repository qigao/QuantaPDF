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
    quantapdf_private_native_source_profile source_profile)
{
    quantapdf_status status;
    quantapdf_ir_raster_image native = {0};

    if (out_bitmap == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_bitmap, 0, sizeof(*out_bitmap));
    if (limits == NULL || device == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    switch (source_profile) {
    case QUANTAPDF_PRIVATE_NATIVE_SOURCE_V8:
        status = quantapdf_builtin_native_qualify_page_v1(
            builtin_page, limits, device, &native);
        break;
    case QUANTAPDF_PRIVATE_NATIVE_SOURCE_V9:
        status = quantapdf_builtin_native_qualify_page_v9(
            builtin_page, limits, device, &native);
        break;
    case QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10:
        status = quantapdf_builtin_native_qualify_page_v10(
            builtin_page, limits, device, &native);
        break;
    default:
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
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
        builtin_page, limits, device, out_bitmap,
        QUANTAPDF_PRIVATE_NATIVE_SOURCE_V8);
}

quantapdf_status quantapdf_builtin_native_qualify_bitmap_v9(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_backend_bitmap *out_bitmap)
{
    return builtin_native_bitmap_impl(
        builtin_page, limits, device, out_bitmap,
        QUANTAPDF_PRIVATE_NATIVE_SOURCE_V9);
}
quantapdf_status quantapdf_builtin_native_qualify_bitmap_v10(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_backend_bitmap *out_bitmap)
{
    return builtin_native_bitmap_impl(
        builtin_page, limits, device, out_bitmap,
        QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10);
}
