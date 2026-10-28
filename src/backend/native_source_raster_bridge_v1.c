#include "backend/native_source_raster_bridge_v1.h"
#include "render/native_device_ir_v1.h"

#include <math.h>
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

    if (out_image == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image, 0, sizeof(*out_image));

    if (original_pdf == NULL || original_size == 0u ||
        limits == NULL || device == NULL ||
        !isfinite(device->dpi) || !isfinite(device->rotation_degrees) ||
        device->dpi <= 0.0f ||
        (device->alpha != 0 && device->alpha != 1) ||
        (device->clip_enabled != 0 && device->clip_enabled != 1) ||
        (device->thumbnail != 0 && device->thumbnail != 1))
        return QUANTAPDF_ERROR_ARGUMENT;

    if (device->page_index != 0u ||
        (device->dpi != 72.0f &&
         !(source_profile==QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10 &&
           (device->dpi==96.0f || device->dpi==144.0f))) ||
        device->rotation_degrees != 0.0f || device->clip_enabled ||
        device->alpha || device->thumbnail)
        return QUANTAPDF_ERROR_UNSUPPORTED;

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
        status = quantapdf_qpdf_source_scoped_mixed_v10_flate_predictor_to_ir(
            original_pdf, original_size, &limits->source, &ir);
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

    /* Only V10 opts into device-space pixels. Earlier private Source
     * signatures remain exact 72 DPI. This is a projection into the
     * SINGLE C11 V5 raster, not a second renderer or image rescaler. */
    if(device->dpi==72.0f)
        status=quantapdf_ir_raster_scoped_mixed_v5(
            &ir,0u,&limits->raster,out_image);
    else
        status=quantapdf_native_device_ir_render_v1(
            &ir,(unsigned)device->dpi,&limits->raster,out_image);
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
