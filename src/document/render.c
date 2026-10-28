#include "core/bitmap_internal.h"
#include "document/document_internal.h"
#include "backend/builtin_native_bitmap_v1.h"
#include "render/native_device_plan.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Public 72/96/144 DPI quarter turns permute the COMPLETE native RGB
 * output at the same already-qualified target DPI, never a 72-DPI
 * upscaler or second source renderer. This private 72-unit pixel-space
 * C11 device plan validates the image's discrete width/height and
 * one-million-pixel ceiling before swapping rows/columns. The actual
 * source raster has already been generated at the requested DPI.
 * Source PDF /Rotate and non-orthogonal angles remain unsupported. */
static quantapdf_status quantapdf_native_rotate_rgb_device(
    quantapdf_backend_bitmap *bitmap, int rotation)
{
    quantapdf_native_device_plan plan={0};
    char canonical_box[64];
    size_t sw,sh,pixels,src_stride,dst_stride,bytes;
    unsigned char *turned;
    quantapdf_status status;
    int printed;
    if(bitmap==NULL || bitmap->data==NULL ||
       bitmap->width<=0 || bitmap->height<=0 ||
       bitmap->components!=3 ||
       (rotation!=90 && rotation!=180 && rotation!=270))
        return QUANTAPDF_ERROR_BACKEND;
    sw=(size_t)bitmap->width;
    sh=(size_t)bitmap->height;
    if(sw>(size_t)INT_MAX/3u ||
       sh>QUANTAPDF_NATIVE_DEVICE_MAX_PIXELS ||
       sw>QUANTAPDF_NATIVE_DEVICE_MAX_PIXELS/sh)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    pixels=sw*sh;
    src_stride=sw*3u;
    bytes=pixels*3u;
    if(bitmap->stride!=(int)src_stride || bitmap->size!=bytes)
        return QUANTAPDF_ERROR_BACKEND;
    printed=snprintf(canonical_box,sizeof(canonical_box),
                     "0 0 %d %d",bitmap->width,bitmap->height);
    if(printed<0 || (size_t)printed>=sizeof(canonical_box))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    status=quantapdf_native_device_plan_create(
        canonical_box,canonical_box,"1",rotation,72u,
        QUANTAPDF_NATIVE_DEVICE_MAX_PIXELS,&plan);
    if(status!=QUANTAPDF_OK)return status;
    if(plan.width!=(rotation==180?sw:sh) ||
       plan.height!=(rotation==180?sh:sw) ||
       plan.width>(size_t)INT_MAX/3u ||
       plan.height>(size_t)INT_MAX ||
       plan.width*plan.height!=pixels ||
       plan.stride!=plan.width*4u ||
       plan.size!=pixels*4u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    dst_stride=plan.width*3u;
    turned=(unsigned char *)malloc(bytes);
    if(turned==NULL)return QUANTAPDF_ERROR_NOMEM;
    for(size_t sy=0u;sy<sh;++sy)
        for(size_t sx=0u;sx<sw;++sx) {
            size_t dx,dy;
            if(rotation==90) {
                dx=sh-1u-sy;dy=sx;
            } else if(rotation==180) {
                dx=sw-1u-sx;dy=sh-1u-sy;
            } else {
                dx=sy;dy=sw-1u-sx;
            }
            memcpy(turned+dy*dst_stride+dx*3u,
                   bitmap->data+sy*src_stride+sx*3u,3u);
        }
    free(bitmap->data);
    bitmap->data=turned;
    bitmap->width=(int)plan.width;
    bitmap->height=(int)plan.height;
    bitmap->stride=(int)dst_stride;
    bitmap->size=bytes;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_render_page_transformed(
    quantapdf_page *page,
    float dpi,
    float rotation_degrees,
    const quantapdf_rect *clip,
    int alpha,
    quantapdf_bitmap **out_bitmap)
{
    quantapdf_bitmap *bitmap;
    quantapdf_backend_bitmap rendered = {0};
    quantapdf_status status;
    double user_unit;
    const quantapdf_native_source_raster_bridge_limits_v1 limits = {
        /* Strict complete-page Source V10 admission. The numeric caps
         * are below the existing independent decoder and Raster maxima. */
        {2u*1024u*1024u,65536u,4u,4u,1000000u,1000000u,
         4000000u,8u},
        {1000000u,4u,512u,8000000u,1000000u,4000000u}
    };
    quantapdf_native_source_raster_device_v1 device = {0};

    if (out_bitmap == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_bitmap = NULL;

    if (page == NULL || page->document == NULL || page->backend == NULL ||
        page->context == NULL || page->document->context != page->context ||
        !isfinite(dpi) || dpi <= 0.0f ||
        !isfinite(rotation_degrees) || (alpha != 0 && alpha != 1))
        return QUANTAPDF_ERROR_ARGUMENT;

    /* Complete caller-selected original PageTree leaf -> Source V10
     * -> owned one-page local IR -> one C11 Raster at actual DPI.
     * The local IR slot zero never changes which original page is read.
     * No 72-DPI upscale, alternative provider or fallback. */
    if (page->page_index < 0 ||
        (dpi != 72.0f && dpi != 96.0f && dpi != 144.0f) ||
        (rotation_degrees != 0.0f &&
         rotation_degrees != 90.0f &&
         rotation_degrees != 180.0f &&
         rotation_degrees != 270.0f) ||
        clip != NULL || alpha != 0)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    device.page_index = (size_t)page->page_index;
    device.dpi = dpi;
    /* The source producer always draws a complete unrotated page;
     * its already validated pixels are then oriented by the plan. */
    device.rotation_degrees = 0.0f;
    device.clip_enabled = clip != NULL;
    device.alpha = alpha;

    status = quantapdf_context_retain_internal(page->context);
    if (status != QUANTAPDF_OK)
        return status;
    bitmap = (quantapdf_bitmap *)quantapdf_context_allocate_internal(
        page->context, sizeof(*bitmap));
    if (bitmap == NULL) {
        quantapdf_drop_context(page->context);
        return QUANTAPDF_ERROR_NOMEM;
    }
    bitmap->context = page->context;

    status = quantapdf_document_page_user_unit(
        page->document, page->page_index, &user_unit);
    if (status == QUANTAPDF_OK) {
        if (!isfinite(user_unit) || user_unit != 1.0)
            status = QUANTAPDF_ERROR_UNSUPPORTED;
        else {
            status = quantapdf_builtin_native_qualify_bitmap_v10(
                page->backend, &limits, &device, &rendered);
            if (status == QUANTAPDF_OK && rotation_degrees != 0.0f)
                status = quantapdf_native_rotate_rgb_device(
                    &rendered, (int)rotation_degrees);
        }
    }
    if (status != QUANTAPDF_OK) {
        free(rendered.data);
        free(bitmap);
        quantapdf_drop_context(page->context);
        return status;
    }

    bitmap->data = rendered.data;
    bitmap->size = rendered.size;
    bitmap->width = rendered.width;
    bitmap->height = rendered.height;
    bitmap->stride = rendered.stride;
    bitmap->components = rendered.components;

    *out_bitmap = bitmap;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_render_page(
    quantapdf_page *page,
    quantapdf_bitmap **out_bitmap)
{
    return quantapdf_render_page_transformed(
        page,
        72.0f,
        0.0f,
        NULL,
        0,
        out_bitmap);
}

quantapdf_status quantapdf_render_page_with_options(
    quantapdf_page *page,
    const quantapdf_render_options *options,
    quantapdf_bitmap **out_bitmap)
{
    size_t minimum_size;
    size_t rotation_size;
    size_t clip_enabled_size;
    size_t clip_size;
    size_t alpha_size;
    float rotation_degrees = 0.0f;
    int clip_enabled = 0;
    int alpha = 0;
    quantapdf_rect clip = { 0 };
    const quantapdf_rect *clip_ptr = NULL;

    if (out_bitmap == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_bitmap = NULL;

    if (page == NULL || options == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    minimum_size = offsetof(quantapdf_render_options, dpi) + sizeof(options->dpi);
    if (options->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    rotation_size = offsetof(quantapdf_render_options, rotation_degrees) +
        sizeof(options->rotation_degrees);
    if (options->struct_size >= rotation_size)
        rotation_degrees = options->rotation_degrees;

    clip_enabled_size = offsetof(quantapdf_render_options, clip_enabled) +
        sizeof(options->clip_enabled);
    if (options->struct_size >= clip_enabled_size)
        clip_enabled = options->clip_enabled != 0;

    if (clip_enabled) {
        clip_size = offsetof(quantapdf_render_options, clip) + sizeof(options->clip);
        if (options->struct_size < clip_size)
            return QUANTAPDF_ERROR_ARGUMENT;

        clip = options->clip;
        if (!isfinite(clip.x0) || !isfinite(clip.y0) ||
            !isfinite(clip.x1) || !isfinite(clip.y1) ||
            clip.x1 <= clip.x0 || clip.y1 <= clip.y0)
            return QUANTAPDF_ERROR_ARGUMENT;
        clip_ptr = &clip;
    }

    alpha_size = offsetof(quantapdf_render_options, alpha) + sizeof(options->alpha);
    if (options->struct_size >= alpha_size) {
        alpha = options->alpha;
        if (alpha != 0 && alpha != 1)
            return QUANTAPDF_ERROR_ARGUMENT;
    }

    return quantapdf_render_page_transformed(
        page,
        options->dpi,
        rotation_degrees,
        clip_ptr,
        alpha,
        out_bitmap);
}

/* Deterministic RGB area resampling for an ALREADY fully admitted native
 * page. Bound work by the original Source V10 max one-million pixels.
 * Integer numerator intersections avoid floating accumulation, precision
 * drift and a second imaging provider. The Bitmap keeps its single Context
 * reference; on failure the caller drops the original complete bitmap. */
static quantapdf_status quantapdf_native_thumbnail_reduce(
    quantapdf_bitmap *bitmap, size_t max_width, size_t max_height)
{
    size_t sw, sh, source_stride, tw, th, target_stride, target_size;
    uint64_t denominator;
    unsigned char *dst;

    if(bitmap==NULL || bitmap->data==NULL ||
       bitmap->width<=0 || bitmap->height<=0 ||
       bitmap->stride<=0 || bitmap->components!=3 ||
       max_width==0u || max_height==0u)
        return QUANTAPDF_ERROR_BACKEND;
    sw=(size_t)bitmap->width;
    sh=(size_t)bitmap->height;
    if(sw>SIZE_MAX/3u || sh>SIZE_MAX/sw ||
       sw*sh>1000000u || bitmap->stride!=(int)(sw*3u))
        return QUANTAPDF_ERROR_BACKEND;
    source_stride=sw*3u;
    if(sh>SIZE_MAX/source_stride ||
       bitmap->size!=sh*source_stride)
        return QUANTAPDF_ERROR_BACKEND;

    /* Existing fit-without-upscaling behavior, but the input image is
     * ALWAYS Native Source V10; we do not ask any backend to rescale. */
    if(sw<=max_width && sh<=max_height)
        return QUANTAPDF_OK;
    if((uint64_t)max_width*(uint64_t)sh <=
       (uint64_t)max_height*(uint64_t)sw) {
        tw=max_width;
        th=(size_t)((uint64_t)sh*(uint64_t)tw/(uint64_t)sw);
    } else {
        th=max_height;
        tw=(size_t)((uint64_t)sw*(uint64_t)th/(uint64_t)sh);
    }
    if(tw==0u)tw=1u;
    if(th==0u)th=1u;
    if(tw>max_width || th>max_height ||
       tw>sw || th>sh || tw>SIZE_MAX/3u ||
       th>SIZE_MAX/(tw*3u))
        return QUANTAPDF_ERROR_BACKEND;
    target_stride=tw*3u;
    target_size=target_stride*th;
    dst=(unsigned char *)malloc(target_size);
    if(dst==NULL)return QUANTAPDF_ERROR_NOMEM;
    denominator=(uint64_t)sw*(uint64_t)sh;

    for(size_t y=0u;y<th;++y) {
        const uint64_t y0=(uint64_t)y*(uint64_t)sh;
        const uint64_t y1=(uint64_t)(y+1u)*(uint64_t)sh;
        const size_t sy_start=(size_t)(y0/(uint64_t)th);
        const size_t sy_end=(size_t)((y1+(uint64_t)th-1u)/(uint64_t)th);
        for(size_t x=0u;x<tw;++x) {
            const uint64_t x0=(uint64_t)x*(uint64_t)sw;
            const uint64_t x1=(uint64_t)(x+1u)*(uint64_t)sw;
            const size_t sx_start=(size_t)(x0/(uint64_t)tw);
            const size_t sx_end=(size_t)((x1+(uint64_t)tw-1u)/(uint64_t)tw);
            uint64_t sums[3]={0u,0u,0u};
            for(size_t sy=sy_start;sy<sy_end;++sy) {
                const uint64_t ay=(uint64_t)sy*(uint64_t)th;
                const uint64_t by=(uint64_t)(sy+1u)*(uint64_t)th;
                const uint64_t low=y0>ay?y0:ay;
                const uint64_t high=y1<by?y1:by;
                const uint64_t wy=high-low;
                for(size_t sx=sx_start;sx<sx_end;++sx) {
                    const uint64_t ax=(uint64_t)sx*(uint64_t)tw;
                    const uint64_t bx=(uint64_t)(sx+1u)*(uint64_t)tw;
                    const uint64_t left=x0>ax?x0:ax;
                    const uint64_t right=x1<bx?x1:bx;
                    const uint64_t weight=(right-left)*wy;
                    const unsigned char *const pixel=
                        bitmap->data+sy*source_stride+sx*3u;
                    for(size_t channel=0u;channel<3u;++channel)
                        sums[channel]+=weight*(uint64_t)pixel[channel];
                }
            }
            for(size_t channel=0u;channel<3u;++channel)
                dst[y*target_stride+x*3u+channel]=
                    (unsigned char)((sums[channel]+denominator/2u)/
                                    denominator);
        }
    }
    free(bitmap->data);
    bitmap->data=dst;
    bitmap->size=target_size;
    bitmap->width=(int)tw;
    bitmap->height=(int)th;
    bitmap->stride=(int)target_stride;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_render_thumbnail(
    quantapdf_page *page,
    int max_width,
    int max_height,
    quantapdf_bitmap **out_bitmap)
{
    quantapdf_bitmap *complete=NULL;
    quantapdf_status status;
    if(out_bitmap==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    *out_bitmap=NULL;
    if(page==NULL || max_width<=0 || max_height<=0)
        return QUANTAPDF_ERROR_ARGUMENT;

    /* Only the full native page is allowed to paint source content.
     * Missing glyph/form/color/clip features still refuse atomically,
     * rather than silently omitting them from a smaller output. */
    status=quantapdf_render_page(page,&complete);
    if(status!=QUANTAPDF_OK)return status;
    status=quantapdf_native_thumbnail_reduce(
        complete,(size_t)max_width,(size_t)max_height);
    if(status!=QUANTAPDF_OK){
        quantapdf_drop_bitmap(complete);
        return status;
    }
    *out_bitmap=complete;
    return QUANTAPDF_OK;
}
