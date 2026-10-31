#include "backend/native_prepared_page_v1.h"
#include <string.h>

/* No PDFium/qpdf handles reach this bridge. The private source reader
 * validates the COMPLETE original page and returns detached owned IR;
 * only then is the pure-C11 raster allowed to run. No provider fallback. */
static quantapdf_status native_source_bridge_impl(
    const unsigned char *original_pdf,
    size_t original_size,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image,
    quantapdf_private_native_source_profile source_profile)
{
    quantapdf_status status;
    quantapdf_ir ir = {0};
    quantapdf_native_prepared_page_v1 prepared;

    if (out_image == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image, 0, sizeof(*out_image));

    if (original_pdf == NULL || original_size == 0u || limits == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    status=quantapdf_native_source_device_validate_v1(device,source_profile);
    if(status!=QUANTAPDF_OK)return status;

    /* Each version preserves an independent immutable Source contract.
     * No fallback and no duplicate qpdf/C11 raster implementation. */
    switch (source_profile) {
    case QUANTAPDF_PRIVATE_NATIVE_SOURCE_V8:
        status = quantapdf_qpdf_source_scoped_mixed_v8_to_ir(
            original_pdf, original_size, &limits->source, &ir);
        break;
    case QUANTAPDF_PRIVATE_NATIVE_SOURCE_V9:
        status = quantapdf_qpdf_source_scoped_mixed_v9_image_decode_to_ir(
            original_pdf, original_size, &limits->source, &ir);
        break;
    case QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10:
        status = quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
            original_pdf, original_size, &limits->source,
            device->page_index, &ir);
        break;
    default:
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if (status != QUANTAPDF_OK) {
        /* Source refuses the entire page, without partial IR publication.
         * Dispose defensively even if source failed before materializing. */
        quantapdf_ir_dispose(&ir);
        return status;
    }

    status=quantapdf_native_prepare_page_v1(&ir,&limits->raster,device,source_profile,&prepared);
    if(status==QUANTAPDF_OK)
        status=quantapdf_native_execute_page_v1(&prepared,NULL,out_image);
    quantapdf_ir_dispose(&ir);
    return status;
}

quantapdf_status quantapdf_native_source_raster_bridge_v1(
    const unsigned char *original_pdf, size_t original_size,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image)
{
    return native_source_bridge_impl(
        original_pdf, original_size, limits, device, out_image,
        QUANTAPDF_PRIVATE_NATIVE_SOURCE_V8);
}

quantapdf_status quantapdf_native_source_raster_bridge_v9(
    const unsigned char *original_pdf, size_t original_size,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image)
{
    return native_source_bridge_impl(
        original_pdf, original_size, limits, device, out_image,
        QUANTAPDF_PRIVATE_NATIVE_SOURCE_V9);
}
quantapdf_status quantapdf_native_source_raster_bridge_v10(
    const unsigned char *original_pdf, size_t original_size,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_ir_raster_image *out_image)
{
    return native_source_bridge_impl(
        original_pdf, original_size, limits, device, out_image,
        QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10);
}
