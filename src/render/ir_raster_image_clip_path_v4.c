#include "render/ir_raster_image_clip_path_v4.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QP_V4_MAX_PIXELS ((size_t)1048576u)
#define QP_V4_MAX_SOURCE_PIXELS ((size_t)4194304u)
#define QP_V4_MAX_DRAWN_PIXELS ((size_t)4194304u)

static int qp_integer(float x,size_t bound,size_t *out)
{
    size_t value;
    if (!isfinite(x) || x < 0.0f || (double)x > (double)bound)
        return 0;
    value=(size_t)x;
    if ((float)value != x)
        return 0;
    *out=value;
    return 1;
}

static void qp_over(unsigned char *dst,const unsigned char *src)
{
    const unsigned sa=src[3],da=dst[3];
    const uint64_t out_a=(uint64_t)sa*255u+
        (uint64_t)da*(255u-sa);
    size_t k;
    if(out_a==0u) {
        memset(dst,0,4u);
        return;
    }
    for(k=0u;k<3u;++k) {
        const uint64_t num=(uint64_t)src[k]*sa*255u+
            (uint64_t)dst[k]*da*(255u-sa);
        dst[k]=(unsigned char)((num+out_a/2u)/out_a);
    }
    dst[3]=(unsigned char)((out_a+127u)/255u);
}

quantapdf_status quantapdf_ir_raster_clipped_image_then_path_v4(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *image_limits,
    const quantapdf_ir_raster_v1_limits *path_clip_limits,
    quantapdf_ir_raster_image *out_image)
{
    const quantapdf_composer_page_state *page;
    const quantapdf_composer_operation *image_op,*path_op;
    const quantapdf_composer_image_state *source;
    quantapdf_ir projected={0};
    quantapdf_composer_page_state transparent={0};
    quantapdf_composer_operation operation={0};
    quantapdf_ir_raster_image layers[2]={{0},{0}};
    quantapdf_ir_raster_v1_limits half_work;
    quantapdf_ir_raster_image_v1_limits image_work;
    unsigned char *result=NULL;
    size_t width,height,pixels,bytes,source_pixels,x0,x1,y0,y1;
    size_t i;
    quantapdf_status status=QUANTAPDF_ERROR_UNSUPPORTED;

    if(out_image==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if(ir==NULL || image_limits==NULL || path_clip_limits==NULL ||
       ir->pages==NULL || ir->operations==NULL ||
       ir->page_count!=1u || page_index!=0u ||
       image_limits->max_pixels==0u ||
       image_limits->max_operations==0u ||
       image_limits->max_source_pixels==0u ||
       image_limits->max_drawn_pixels==0u ||
       path_clip_limits->max_pixels==0u ||
       path_clip_limits->max_operations==0u ||
       path_clip_limits->max_edges==0u ||
       path_clip_limits->max_sample_tests==0u)
        return QUANTAPDF_ERROR_ARGUMENT;

    if(ir->operation_count!=2u ||
       image_limits->max_operations<2u ||
       path_clip_limits->max_operations<2u ||
       ir->resources.image_count!=1u || ir->resources.images==NULL ||
       ir->resources.graphics_state_count!=1u ||
       ir->resources.graphics_states==NULL ||
       ir->resources.clip_count!=1u || ir->resources.clips==NULL ||
       ir->resources.font_count!=0u || ir->resources.form_count!=0u ||
       ir->resources.soft_mask_count!=0u ||
       ir->resources.paint_count!=0u ||
       ir->link_count!=0u || ir->outline_count!=0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    page=&ir->pages[0];
    if(!isfinite(page->width_points) || !isfinite(page->height_points) ||
       page->width_points<=0.0f || page->height_points<=0.0f ||
       page->width_points>(float)QP_V4_MAX_PIXELS ||
       page->height_points>(float)QP_V4_MAX_PIXELS ||
       (page->suppress_background!=0 && page->suppress_background!=1) ||
       (!page->suppress_background &&
        (page->background_argb>>24u)!=255u))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    width=(size_t)page->width_points;
    height=(size_t)page->height_points;
    if(width==0u || height==0u ||
       (float)width!=page->width_points ||
       (float)height!=page->height_points ||
       width>QP_V4_MAX_PIXELS/height)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    pixels=width*height;
    if(pixels>image_limits->max_pixels ||
       pixels>path_clip_limits->max_pixels)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    bytes=pixels*4u;

    /* All page/operation/resource association checks happen before
     * allocation. The first Image owns Clip GS #1, the second Path must
     * have NO GS or Clip after an original Q restored that state. */
    image_op=&ir->operations[0];
    path_op=&ir->operations[1];
    source=&ir->resources.images[0];
    if(image_op->kind!=QUANTAPDF_COMPOSER_OPERATION_IMAGE ||
       path_op->kind!=QUANTAPDF_COMPOSER_OPERATION_PATH ||
       image_op->page_index!=0u || path_op->page_index!=0u ||
       image_op->graphics_state_id!=1u ||
       path_op->graphics_state_id!=0u ||
       image_op->value.image.image_id!=1u ||
       image_op->value.image.options.struct_size<
           QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_MIN_SIZE ||
       image_op->value.image.options.graphics_state_id!=1u ||
       image_op->value.image.options.fit!=
           QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH ||
       path_op->value.path.options.struct_size<
           QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE ||
       path_op->value.path.options.fill!=1 ||
       path_op->value.path.options.stroke!=0 ||
       (path_op->value.path.options.fill_argb>>24u)!=255u ||
       (path_op->value.path.options.struct_size>=
            QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE &&
        path_op->value.path.options.graphics_state_id!=0u) ||
       (path_op->value.path.options.struct_size>=
            QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_MIN_SIZE &&
        (path_op->value.path.options.fill_paint_id!=0u ||
         path_op->value.path.options.stroke_paint_id!=0u)) ||
       path_op->value.path.dash_count!=0u ||
       source->width==0u || source->height==0u ||
       (size_t)source->width>QP_V4_MAX_SOURCE_PIXELS/
           (size_t)source->height ||
       !qp_integer(image_op->bounds.x0,width,&x0) ||
       !qp_integer(image_op->bounds.y0,height,&y0) ||
       !qp_integer(image_op->bounds.x1,width,&x1) ||
       !qp_integer(image_op->bounds.y1,height,&y1) ||
       x1<=x0 || y1<=y0)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    source_pixels=(size_t)source->width*(size_t)source->height;
    if(source_pixels>QP_V4_MAX_SOURCE_PIXELS ||
       source_pixels>image_limits->max_source_pixels ||
       (x1-x0)*(y1-y0)>QP_V4_MAX_DRAWN_PIXELS ||
       (x1-x0)*(y1-y0)>image_limits->max_drawn_pixels ||
       path_clip_limits->max_edges<2u ||
       path_clip_limits->max_sample_tests<2u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Clip mask and independent Path both use the V1 edge/subpixel budget.
     * Conservatively allocate half of the total work to each; there is
     * no invisible second full-budget evaluation. */
    half_work=*path_clip_limits;
    half_work.max_operations=1u;
    half_work.max_edges/=2u;
    half_work.max_sample_tests/=2u;
    image_work=*image_limits;
    image_work.max_operations=1u;

    transparent=*page;
    transparent.suppress_background=1;
    transparent.background_argb=0u;
    projected.pages=&transparent;
    projected.page_count=1u;
    projected.operations=&operation;
    projected.operation_count=1u;
    projected.resources.images=ir->resources.images;
    projected.resources.image_count=1u;
    projected.resources.graphics_states=ir->resources.graphics_states;
    projected.resources.graphics_state_count=1u;
    projected.resources.clips=ir->resources.clips;
    projected.resources.clip_count=1u;
    operation=*image_op;
    status=quantapdf_ir_raster_clipped_raw_image_v1(
        &projected,0u,&image_work,&half_work,&layers[0]);
    if(status!=QUANTAPDF_OK)goto cleanup;

    /* Q has removed the Clip. This is a fresh projection with no resource
     * aliases; the Path's original fill/transform/rule remain immutable. */
    memset(&projected.resources,0,sizeof(projected.resources));
    operation=*path_op;
    status=quantapdf_ir_raster_render_v1(
        &projected,0u,&half_work,&layers[1]);
    if(status!=QUANTAPDF_OK)goto cleanup;
    if(layers[0].pixels==NULL || layers[1].pixels==NULL ||
       layers[0].width!=width || layers[1].width!=width ||
       layers[0].height!=height || layers[1].height!=height ||
       layers[0].stride!=width*4u || layers[1].stride!=width*4u ||
       layers[0].size!=bytes || layers[1].size!=bytes) {
        status=QUANTAPDF_ERROR_UNSUPPORTED;
        goto cleanup;
    }

    result=(unsigned char *)malloc(bytes);
    if(result==NULL) {
        status=QUANTAPDF_ERROR_NOMEM;
        goto cleanup;
    }
    for(i=0u;i<pixels;++i) {
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
