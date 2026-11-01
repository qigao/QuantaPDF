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
    quantapdf_private_native_source_profile source_profile,quantapdf_context_lease_internal *lease)
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

    /* The borrowed backend Page lease and requested source index
     * MUST match, including nonzero public V10 pages. Earlier V8/V9
     * have a separate explicit page-zero gate in the Source bridge. */
    if (view.page_index != device->page_index)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Authentication belongs to document open. These bounded Source
     * profiles refuse password-bearing render views rather than
     * misrepresent a decrypted original or silently omit content. */
    if (view.password != NULL && view.password[0] != '\0')
        return QUANTAPDF_ERROR_UNSUPPORTED;

    if(lease!=NULL)
        return quantapdf_native_source_raster_bridge_context_v1(view.data,view.size,
            limits,device,source_profile,lease,out_image);

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
        QUANTAPDF_PRIVATE_NATIVE_SOURCE_V8,NULL);
}

quantapdf_status quantapdf_builtin_native_qualify_page_v9(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image)
{
    return builtin_native_page_impl(
        builtin_page, limits, device, out_image,
        QUANTAPDF_PRIVATE_NATIVE_SOURCE_V9,NULL);
}
quantapdf_status quantapdf_builtin_native_qualify_page_v10(
    void *builtin_page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image)
{
    return builtin_native_page_impl(
        builtin_page, limits, device, out_image,
        QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10,NULL);
}

quantapdf_status quantapdf_builtin_native_qualify_page_context_v1(
    void *builtin_page,const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_private_native_source_profile profile,quantapdf_context_lease_internal *lease,
    quantapdf_ir_raster_image *out)
{
    if(!quantapdf_context_lease_valid_internal(lease)) {
        if(out!=NULL)memset(out,0,sizeof(*out));
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    return builtin_native_page_impl(builtin_page,limits,device,out,profile,lease);
}
