#include "render/ir_raster_image_clip_v1.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void qp_point(quantapdf_composer_path_command *p,
                     quantapdf_composer_path_command_kind kind,
                     float x,float y)
{
    memset(p,0,sizeof(*p));
    p->kind=kind;
    p->point1.x=x;
    p->point1.y=y;
}

static void qp_over(unsigned char *dst, const unsigned char *src,
                    unsigned alpha)
{
    const unsigned da=dst[3];
    const uint64_t out_a=(uint64_t)alpha*255u+
        (uint64_t)da*(255u-alpha);
    size_t c;
    if(out_a==0u) {
        memset(dst,0,4u);
        return;
    }
    for(c=0u;c<3u;++c) {
        const uint64_t n=(uint64_t)src[c]*alpha*255u+
            (uint64_t)dst[c]*da*(255u-alpha);
        dst[c]=(unsigned char)((n+out_a/2u)/out_a);
    }
    dst[3]=(unsigned char)((out_a+127u)/255u);
}

static quantapdf_status qp_clipped_raw_image(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *image_limits,
    const quantapdf_ir_raster_v1_limits *clip_limits,
    quantapdf_ir_raster_image *out_image,quantapdf_native_work_ledger_v1 *ledger)
{
    const quantapdf_composer_operation *image_op;
    const quantapdf_composer_graphics_state *gs;
    const quantapdf_composer_clip_state *clip;
    quantapdf_ir projected={0};
    quantapdf_composer_page_state isolated_page={0};
    quantapdf_composer_operation projected_image={0},masked_path={0};
    quantapdf_composer_path_command rect[5]={{0}};
    quantapdf_ir_raster_image raw={0},mask={0};
    unsigned char *pixels=NULL;
    quantapdf_status status=QUANTAPDF_ERROR_UNSUPPORTED;
    size_t width,height,pixel_count,bytes=0u,i;
    if(out_image==NULL) return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if(ir==NULL || image_limits==NULL || clip_limits==NULL ||
       ir->pages==NULL || ir->operations==NULL ||
       ir->page_count!=1u || page_index!=0u ||
       image_limits->max_pixels==0u || clip_limits->max_pixels==0u ||
       image_limits->max_operations==0u ||
       clip_limits->max_operations==0u ||
       clip_limits->max_edges==0u ||
       clip_limits->max_sample_tests==0u ||
       image_limits->max_source_pixels==0u ||
       image_limits->max_drawn_pixels==0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if(ir->operation_count!=1u ||
       ir->resources.image_count!=1u || ir->resources.images==NULL ||
       ir->resources.graphics_state_count!=1u ||
       ir->resources.graphics_states==NULL ||
       ir->resources.clip_count!=1u || ir->resources.clips==NULL ||
       ir->resources.font_count!=0u || ir->resources.paint_count!=0u ||
       ir->resources.form_count!=0u ||
       ir->resources.soft_mask_count!=0u ||
       ir->link_count!=0u || ir->outline_count!=0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    image_op=&ir->operations[0];
    gs=&ir->resources.graphics_states[0];
    clip=&ir->resources.clips[0];
    if(image_op->page_index!=0u ||
       image_op->kind!=QUANTAPDF_COMPOSER_OPERATION_IMAGE ||
       image_op->graphics_state_id!=1u ||
       image_op->value.image.image_id!=1u ||
       image_op->value.image.options.struct_size<
           QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_MIN_SIZE ||
       image_op->value.image.options.graphics_state_id!=1u ||
       image_op->value.image.options.fit!=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH ||
       gs->fill_alpha!=1.0f || gs->stroke_alpha!=1.0f ||
       gs->blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL ||
       gs->clip_id!=1u || gs->soft_mask_id!=0u ||
       clip->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
       clip->members!=NULL || clip->member_count!=0u ||
       clip->commands==NULL || clip->command_count==0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Raw Image and mask do not borrow graphics state ownership. They
     * receive borrowed, stack-local projections of the same input, each
     * with precisely its own admitted resource set. */
    isolated_page=ir->pages[0];
    isolated_page.suppress_background=1;
    isolated_page.background_argb=0u;
    projected.pages=&isolated_page;
    projected.page_count=1u;
    projected.operations=&projected_image;
    projected.operation_count=1u;
    projected.resources.images=ir->resources.images;
    projected.resources.image_count=1u;
    projected_image=*image_op;
    projected_image.graphics_state_id=0u;
    projected_image.value.image.options.graphics_state_id=0u;
    status=quantapdf_ir_raster_raw_images_v1_borrowed(
        &projected,0u,image_limits,ledger,&raw);
    if(status!=QUANTAPDF_OK) goto cleanup;
    width=raw.width;
    height=raw.height;
    pixel_count=width*height;
    bytes=raw.size;

    /* A white opaque rectangle exactly coincident with the Image bounds
     * produces per-sample alpha coverage for the original Clip leaf.
     * Pixel-center Image interpolation is fixed by the earlier RAW layer;
     * only the 4x4 geometric membership is masked here. */
    qp_point(&rect[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,
             image_op->bounds.x0,image_op->bounds.y0);
    qp_point(&rect[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,
             image_op->bounds.x1,image_op->bounds.y0);
    qp_point(&rect[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,
             image_op->bounds.x1,image_op->bounds.y1);
    qp_point(&rect[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,
             image_op->bounds.x0,image_op->bounds.y1);
    qp_point(&rect[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0.0f,0.0f);
    memset(&masked_path,0,sizeof(masked_path));
    masked_path.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    masked_path.value.path.commands=rect;
    masked_path.value.path.command_count=5u;
    masked_path.value.path.options.struct_size=
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    masked_path.value.path.options.fill=1;
    masked_path.value.path.options.fill_argb=UINT32_C(0xffffffff);
    masked_path.value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    projected.operations=&masked_path;
    memset(&projected.resources,0,sizeof(projected.resources));
    if(ledger!=NULL) {
        const quantapdf_composer_clip_state *one[1]={clip};
        status=quantapdf_ir_raster_render_v1_accounted(
            &projected,0u,clip_limits,one,1u,QP_NATIVE_WORK_FULL,ledger,&mask);
    } else {
        status=quantapdf_ir_raster_render_v1_clipped(
            &projected,0u,clip_limits,clip,&mask);
    }
    if(status!=QUANTAPDF_OK) goto cleanup;
    if(mask.width!=width || mask.height!=height ||
       raw.size!=mask.size || raw.stride!=width*4u ||
       mask.stride!=width*4u || bytes!=pixel_count*4u ||
       mask.pixels==NULL || raw.pixels==NULL) {
        status=QUANTAPDF_ERROR_UNSUPPORTED;
        goto cleanup;
    }

    /* Allocate and publish only after BOTH independent painters succeed.
     * This blends the effective source alpha multiplied by 4x4 Clip
     * membership, never multiplies two already averaged RGB masks. */
    pixels=(unsigned char *)quantapdf_native_raster_malloc_v1(ledger,bytes,&status);
    if(pixels==NULL)goto cleanup;
    for(i=0u;i<pixel_count;++i) {
        const uint32_t bg=ir->pages[0].background_argb;
        unsigned char *dst=pixels+4u*i;
        const unsigned char *src=raw.pixels+4u*i;
        const unsigned char *msk=mask.pixels+4u*i;
        const unsigned alpha=((unsigned)src[3]*(unsigned)msk[3]+127u)/255u;
        dst[0]=ir->pages[0].suppress_background ? 0u :
            (unsigned char)((bg>>16u)&255u);
        dst[1]=ir->pages[0].suppress_background ? 0u :
            (unsigned char)((bg>>8u)&255u);
        dst[2]=ir->pages[0].suppress_background ? 0u :
            (unsigned char)(bg&255u);
        dst[3]=ir->pages[0].suppress_background ? 0u : 255u;
        qp_over(dst,src,alpha);
    }
    out_image->pixels=pixels;
    out_image->size=bytes;
    out_image->width=width;
    out_image->height=height;
    out_image->stride=width*4u;
    pixels=NULL;
    status=QUANTAPDF_OK;
cleanup:
    quantapdf_native_raster_free_v1(ledger,pixels,bytes);
    quantapdf_native_raster_image_dispose_v1(ledger,&mask);
    quantapdf_native_raster_image_dispose_v1(ledger,&raw);
    return status;
}

quantapdf_status quantapdf_ir_raster_clipped_raw_image_v1(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *image_limits,
    const quantapdf_ir_raster_v1_limits *clip_limits,
    quantapdf_ir_raster_image *out)
{
    return qp_clipped_raw_image(ir,page_index,image_limits,clip_limits,out,NULL);
}

quantapdf_status quantapdf_ir_raster_clipped_raw_image_v1_accounted(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_image_v1_limits *image_limits,
    const quantapdf_ir_raster_v1_limits *clip_limits,
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_ir_raster_image *out)
{
    if(ledger==NULL) {
        if(out!=NULL)memset(out,0,sizeof(*out));
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    return qp_clipped_raw_image(ir,page_index,image_limits,clip_limits,out,ledger);
}
