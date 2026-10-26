#include "render/ir_raster_image_clip_v2.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QP_V2_MAX_OUTPUT ((size_t)4194304u)
#define QP_V2_MAX_SOURCE ((size_t)4194304u)
#define QP_V2_MAX_DRAWN ((size_t)16777216u)

static int qp_integer(float value,size_t bound,size_t *out)
{
    size_t integer;
    if(!isfinite(value) || value<0.0f || (double)value>(double)bound)
        return 0;
    integer=(size_t)value;
    if((float)integer!=value) return 0;
    *out=integer;
    return 1;
}

static void qp_over(unsigned char *dst,const unsigned char *src)
{
    const unsigned sa=src[3],da=dst[3];
    const uint64_t out_a=(uint64_t)sa*255u+
        (uint64_t)da*(255u-sa);
    size_t c;
    if(out_a==0u) {
        memset(dst,0,4u);
        return;
    }
    for(c=0u;c<3u;++c) {
        uint64_t numerator=(uint64_t)src[c]*sa*255u+
            (uint64_t)dst[c]*da*(255u-sa);
        dst[c]=(unsigned char)((numerator+out_a/2u)/out_a);
    }
    dst[3]=(unsigned char)((out_a+127u)/255u);
}

quantapdf_status quantapdf_ir_raster_two_clipped_raw_images_v2(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *image_limits,
    const quantapdf_ir_raster_v1_limits *clip_limits,
    quantapdf_ir_raster_image *out_image)
{
    const quantapdf_composer_page_state *page;
    quantapdf_ir_raster_image layers[2]={{0},{0}};
    quantapdf_ir projected={0};
    quantapdf_composer_page_state transparent={0};
    quantapdf_composer_operation operation={0};
    quantapdf_ir_raster_image_v1_limits image_per_layer;
    quantapdf_ir_raster_v1_limits clip_per_layer;
    unsigned char *result=NULL;
    size_t width,height,count,bytes,source_total=0u,draw_total=0u;
    size_t i;
    quantapdf_status status=QUANTAPDF_ERROR_UNSUPPORTED;

    if(out_image==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if(ir==NULL || image_limits==NULL || clip_limits==NULL ||
       ir->pages==NULL || ir->operations==NULL ||
       ir->page_count!=1u || page_index!=0u ||
       image_limits->max_pixels==0u || image_limits->max_operations==0u ||
       image_limits->max_source_pixels==0u ||
       image_limits->max_drawn_pixels==0u ||
       clip_limits->max_pixels==0u || clip_limits->max_operations==0u ||
       clip_limits->max_edges==0u || clip_limits->max_sample_tests==0u)
        return QUANTAPDF_ERROR_ARGUMENT;

    if(ir->operation_count!=2u || image_limits->max_operations<2u ||
       clip_limits->max_operations<2u ||
       ir->resources.image_count!=2u || ir->resources.images==NULL ||
       ir->resources.graphics_state_count!=1u ||
       ir->resources.graphics_states==NULL ||
       ir->resources.clip_count!=1u || ir->resources.clips==NULL ||
       ir->resources.font_count!=0u || ir->resources.form_count!=0u ||
       ir->resources.soft_mask_count!=0u ||
       ir->resources.paint_count!=0u || ir->link_count!=0u ||
       ir->outline_count!=0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    page=&ir->pages[0];
    if(!isfinite(page->width_points) || !isfinite(page->height_points) ||
       page->width_points<=0.0f || page->height_points<=0.0f ||
       (page->suppress_background!=0 && page->suppress_background!=1) ||
       (!page->suppress_background &&
        (page->background_argb>>24u)!=255u))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    width=(size_t)page->width_points;
    height=(size_t)page->height_points;
    if(height==0u || width==0u ||
       (float)width!=page->width_points ||
       (float)height!=page->height_points ||
       width>QP_V2_MAX_OUTPUT/height)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    count=width*height;
    if(count>image_limits->max_pixels ||
       count>clip_limits->max_pixels)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    bytes=count*4u;

    /* No output allocation precedes the full two-resource/two-operation
     * preflight. Subsequent V1 calls validate exact buffer/Clip contents. */
    for(i=0u;i<2u;++i) {
        const quantapdf_composer_operation *op=&ir->operations[i];
        const quantapdf_composer_image_state *image=&ir->resources.images[i];
        size_t x0,y0,x1,y1,pixels,area;
        if(op->kind!=QUANTAPDF_COMPOSER_OPERATION_IMAGE ||
           op->page_index!=0u ||
           op->graphics_state_id!=1u ||
           op->value.image.image_id!=(quantapdf_composer_image_id)(i+1u) ||
           op->value.image.options.struct_size<
               QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_MIN_SIZE ||
           op->value.image.options.graphics_state_id!=1u ||
           op->value.image.options.fit!=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH ||
           image->width==0u || image->height==0u ||
           (size_t)image->width>QP_V2_MAX_SOURCE/(size_t)image->height ||
           !qp_integer(op->bounds.x0,width,&x0) ||
           !qp_integer(op->bounds.y0,height,&y0) ||
           !qp_integer(op->bounds.x1,width,&x1) ||
           !qp_integer(op->bounds.y1,height,&y1) ||
           x1<=x0 || y1<=y0)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        pixels=(size_t)image->width*(size_t)image->height;
        area=(x1-x0)*(y1-y0);
        if(pixels>image_limits->max_source_pixels ||
           source_total>image_limits->max_source_pixels-pixels ||
           source_total>QP_V2_MAX_SOURCE-pixels ||
           area>image_limits->max_drawn_pixels ||
           draw_total>image_limits->max_drawn_pixels-area ||
           draw_total>QP_V2_MAX_DRAWN-area)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        source_total+=pixels;
        draw_total+=area;
    }

    /* Divide global edge+sample work conservatively among layers. No
     * two full-budget V1 paints may silently double the caller's limit. */
    if(clip_limits->max_edges<2u ||
       clip_limits->max_sample_tests<2u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    clip_per_layer=*clip_limits;
    clip_per_layer.max_operations=1u;
    clip_per_layer.max_edges/=2u;
    clip_per_layer.max_sample_tests/=2u;
    image_per_layer=*image_limits;
    image_per_layer.max_operations=1u;

    transparent=*page;
    transparent.suppress_background=1;
    transparent.background_argb=0u;
    projected.pages=&transparent;
    projected.page_count=1u;
    projected.operations=&operation;
    projected.operation_count=1u;
    projected.resources.graphics_states=ir->resources.graphics_states;
    projected.resources.graphics_state_count=1u;
    projected.resources.clips=ir->resources.clips;
    projected.resources.clip_count=1u;
    projected.resources.image_count=1u;

    for(i=0u;i<2u;++i) {
        operation=ir->operations[i];
        operation.value.image.image_id=1u;
        projected.resources.images=&ir->resources.images[i];
        status=quantapdf_ir_raster_clipped_raw_image_v1(
            &projected,0u,&image_per_layer,&clip_per_layer,&layers[i]);
        if(status!=QUANTAPDF_OK)goto cleanup;
        if(layers[i].pixels==NULL || layers[i].width!=width ||
           layers[i].height!=height || layers[i].size!=bytes ||
           layers[i].stride!=width*4u) {
            status=QUANTAPDF_ERROR_UNSUPPORTED;
            goto cleanup;
        }
    }

    result=(unsigned char *)malloc(bytes);
    if(result==NULL) {
        status=QUANTAPDF_ERROR_NOMEM;
        goto cleanup;
    }
    for(i=0u;i<count;++i) {
        const uint32_t argb=page->background_argb;
        unsigned char *p=result+i*4u;
        p[0]=page->suppress_background?0u:(unsigned char)((argb>>16u)&255u);
        p[1]=page->suppress_background?0u:(unsigned char)((argb>>8u)&255u);
        p[2]=page->suppress_background?0u:(unsigned char)(argb&255u);
        p[3]=page->suppress_background?0u:255u;
        qp_over(p,layers[0].pixels+i*4u);
        qp_over(p,layers[1].pixels+i*4u);
    }
    out_image->pixels=result;
    out_image->width=width;
    out_image->height=height;
    out_image->stride=width*4u;
    out_image->size=bytes;
    result=NULL;
    status=QUANTAPDF_OK;
cleanup:
    free(result);
    quantapdf_ir_raster_image_dispose(&layers[0]);
    quantapdf_ir_raster_image_dispose(&layers[1]);
    return status;
}
