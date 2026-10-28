#include "backend/builtin_native_page_v1.h"
#include "backend/builtin_source_view_v1.h"

#include <string.h>

/* Owned by the product's private Native OBJECT target. The backend
 * supplies ONLY a read-only borrowed Source view; everything following
 * Source admission and QPDF decoding resolves in the product target.
 * This preserves one-way product -> backend archive linkage, with no
 * reversed reference from backend to private C11 raster symbols. */
static quantapdf_status builtin_native_page_impl(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image,
    quantapdf_private_native_source_profile source_profile)
{
    quantapdf_builtin_source_view_v1 view = {0};
    quantapdf_status status;

    if (out_image == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if (limits == NULL || device == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_builtin_page_source_view_v1(builtin_page,&view);
    if (status != QUANTAPDF_OK)
        return status;

    /* The first Native Source V8 profile is page-zero only. Do not
     * accidentally render the wrong page when the caller requests
     * another otherwise-valid built-in provider page. */
    if (view.page_index != device->page_index)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Authentication belongs to document open. These bounded Source
     * profiles refuse password-bearing render views rather than
     * misrepresent a decrypted original or silently omit content. */
    if (view.password != NULL && view.password[0] != '\0')
        return QUANTAPDF_ERROR_UNSUPPORTED;

    switch (source_profile) {
    case QUANTAPDF_PRIVATE_NATIVE_SOURCE_V8:
        return quantapdf_native_source_raster_bridge_v1(
            view.data, view.size, limits, device, out_image);
    case QUANTAPDF_PRIVATE_NATIVE_SOURCE_V9:
        return quantapdf_native_source_raster_bridge_v9(
            view.data, view.size, limits, device, out_image);
    case QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10:
        return quantapdf_native_source_raster_bridge_v10(
            view.data, view.size, limits, device, out_image);
    default:
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
}

quantapdf_status quantapdf_builtin_native_qualify_page_v1(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image)
{
    return builtin_native_page_impl(
        builtin_page, limits, device, out_image,
        QUANTAPDF_PRIVATE_NATIVE_SOURCE_V8);
}

quantapdf_status quantapdf_builtin_native_qualify_page_v9(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image)
{
    return builtin_native_page_impl(
        builtin_page, limits, device, out_image,
        QUANTAPDF_PRIVATE_NATIVE_SOURCE_V9);
}
quantapdf_status quantapdf_builtin_native_qualify_page_v10(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image)
{
    return builtin_native_page_impl(
        builtin_page, limits, device, out_image,
        QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10);
}
