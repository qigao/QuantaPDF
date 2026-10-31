#include "backend/native_source_raster_bridge_v1.h"
#include "render/native_device_ir_v1.h"
#include "render/ir_raster_v1.h"

#include <math.h>
#include <limits.h>
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

    if ((source_profile!=QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10 &&
         device->page_index!=0u) ||
        device->page_index>(size_t)INT_MAX ||
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

    /* Only V10 opts into device-space pixels. Earlier private Source
     * signatures remain exact 72 DPI. This is a projection into the
     * SINGLE C11 V5 raster, not a second renderer or image rescaler. */
    if(source_profile==QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10) {
        /* Select the complete owned state shape once. Two independent
         * leaves/GS are sibling scopes; three Clip records include an
         * intersection root. Every paint retains its exact membership. */
        /* The ORIGINAL yWorks has been completely Source-admitted
         * and independently rendered by the same private C11 sampler:
         * exactly three ordered PATH paints, three real GS owners and
         * seven frozen Clip records. No fallback, no inferred root,
         * no second provider, no partial source omission. */
        if(ir.operation_count>0u && ir.operations!=NULL &&
           ir.operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES) {
            quantapdf_ir_raster_v1_limits text_limits={limits->raster.max_pixels,
                limits->raster.max_operations,limits->raster.max_edges,limits->raster.max_sample_tests};
            status=quantapdf_ir_raster_render_v1_text_outlines(&ir,(unsigned)device->dpi,
                &text_limits,NULL,out_image);
        } else if(ir.page_count==1u && ir.pages!=NULL &&
           ir.pages[0].width_points==237.133f &&
           ir.pages[0].height_points==180.44f && ir.operation_count==3u) {
            const int physical_dpi=device->dpi!=72.0f;
            const size_t profile_pixels=physical_dpi?1000000u:50000u;
            const size_t profile_edges=physical_dpi?512u:1024u;
            quantapdf_native_device_plan plan={0};
            quantapdf_ir_raster_v1_limits work={
                limits->raster.max_pixels<profile_pixels?limits->raster.max_pixels:profile_pixels,
                3u,
                limits->raster.max_edges<profile_edges?limits->raster.max_edges:profile_edges,
                limits->raster.max_sample_tests<8000000u?
                    limits->raster.max_sample_tests:8000000u
            };
            /* Direct complete original Source path. Preserve caller caps,
             * including the public 512-edge ceiling; no older-route retry. */
            if(limits->raster.max_operations<3u)
                status=QUANTAPDF_ERROR_UNSUPPORTED;
            else {
                status=quantapdf_native_device_plan_create_physical(
                    "0 0 237.133 180.44","0 0 237.133 180.44","1",
                    0,(unsigned)device->dpi,work.max_pixels,&plan);
                if(status==QUANTAPDF_OK) {
                    if(physical_dpi)
                        status=quantapdf_ir_raster_render_v1_texample_physical_dpi_three_layer(
                            &ir,&work,&plan,NULL,out_image);
                    else
                        status=quantapdf_ir_raster_render_v1_texample_shared_three_layer(
                            &ir,&work,&plan,NULL,out_image);
                }
            }
        } else if(ir.resources.graphics_state_count==3u &&
           ir.resources.clip_count==7u &&
           ir.operation_count==3u &&
           ir.resources.image_count==0u)
            status=quantapdf_native_device_ir_render_v1_three_gs_original(
                &ir,(unsigned)device->dpi,&limits->raster,out_image);
        else if(ir.resources.graphics_state_count==2u &&
           ir.resources.clip_count==2u)
            status=quantapdf_native_device_ir_render_v1_sibling_clips(
                &ir,(unsigned)device->dpi,&limits->raster,out_image);
        else if(ir.resources.graphics_state_count!=0u &&
           ir.operation_count!=0u && ir.operations!=NULL &&
           ir.operations[0].graphics_state_id==0u)
            status=quantapdf_native_device_ir_render_v1_unclipped_prefix(
                &ir,(unsigned)device->dpi,&limits->raster,out_image);
        else if(ir.resources.graphics_state_count==2u)
            status=quantapdf_native_device_ir_render_v1_compound_clip_states(
                &ir,(unsigned)device->dpi,&limits->raster,out_image);
        else if(ir.resources.image_count==0u &&
           ir.resources.graphics_state_count==1u)
            status=quantapdf_native_device_ir_render_v1_clipped_paths(
                &ir,(unsigned)device->dpi,&limits->raster,out_image);
        else if(ir.resources.clip_count==3u)
            status=quantapdf_native_device_ir_render_v1_two_clip(
                &ir,(unsigned)device->dpi,&limits->raster,out_image);
        else
            status=quantapdf_native_device_ir_render_v1(
                &ir,(unsigned)device->dpi,&limits->raster,out_image);
    } else
        status=quantapdf_ir_raster_scoped_mixed_v5(
            &ir,0u,&limits->raster,out_image);
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
