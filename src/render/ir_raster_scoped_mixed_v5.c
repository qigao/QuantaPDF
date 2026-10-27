#include "render/ir_raster_scoped_mixed_v5.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QP_V5_MAX_LAYERS 4u
#define QP_V5_MAX_PIXELS ((size_t)1048576u)
#define QP_V5_MAX_SOURCE_PIXELS ((size_t)4194304u)
#define QP_V5_MAX_DRAW_WORK ((size_t)4194304u)

static int v5_integer(float v,size_t max,size_t *out)
{
    size_t integral;
    if(!isfinite(v) || v<0.0f || (double)v>(double)max)return 0;
    integral=(size_t)v;
    if((float)integral!=v)return 0;
    *out=integral;
    return 1;
}

static void v5_over(unsigned char *dst,const unsigned char *src)
{
    const unsigned sa=src[3],da=dst[3];
    const uint64_t alpha=(uint64_t)sa*255u+(uint64_t)da*(255u-sa);
    size_t c;
    if(alpha==0u){memset(dst,0,4u);return;}
    for(c=0u;c<3u;++c){
        const uint64_t value=(uint64_t)src[c]*sa*255u+
            (uint64_t)dst[c]*da*(255u-sa);
        dst[c]=(unsigned char)((value+alpha/2u)/alpha);
    }
    dst[3]=(unsigned char)((alpha+127u)/255u);
}

/* Zero-operation is a legitimate opaque blank page, not a partially
 * published or failed Image/Path source. Admit only an exact empty
 * registry and integer bounded geometry; all other IR profiles retain
 * the existing strict V5 painted operation contract. */
static quantapdf_status v5_render_blank(
    const quantapdf_ir *ir,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    const quantapdf_composer_page_state *page=&ir->pages[0];
    const quantapdf_resource_registry *r=&ir->resources;
    size_t width,height,pixels,bytes;
    unsigned char *white;
    if(ir->page_count!=1u || ir->operations!=NULL ||
       ir->operation_count!=0u || ir->links!=NULL ||
       ir->link_count!=0u || ir->outlines!=NULL ||
       ir->outline_count!=0u || r->images!=NULL ||
       r->image_count!=0u || r->image_capacity!=0u ||
       r->fonts!=NULL || r->font_count!=0u ||
       r->font_capacity!=0u || r->graphics_states!=NULL ||
       r->graphics_state_count!=0u ||
       r->graphics_state_capacity!=0u || r->soft_masks!=NULL ||
       r->soft_mask_count!=0u || r->soft_mask_capacity!=0u ||
       r->clips!=NULL || r->clip_count!=0u ||
       r->clip_capacity!=0u || r->paints!=NULL ||
       r->paint_count!=0u || r->paint_capacity!=0u ||
       r->forms!=NULL || r->form_count!=0u ||
       r->form_capacity!=0u ||
       page->background_argb!=UINT32_C(0xffffffff) ||
       page->suppress_background!=0 ||
       !v5_integer(page->width_points,QP_V5_MAX_PIXELS,&width) ||
       !v5_integer(page->height_points,QP_V5_MAX_PIXELS,&height) ||
       width==0u || height==0u ||
       width>QP_V5_MAX_PIXELS/height)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    pixels=width*height;
    if(pixels>limits->max_pixels ||
       pixels>SIZE_MAX/4u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    bytes=pixels*4u;
    white=(unsigned char *)malloc(bytes);
    if(white==NULL)return QUANTAPDF_ERROR_NOMEM;
    memset(white,255,bytes);
    out_image->pixels=white;
    out_image->size=bytes;
    out_image->width=width;
    out_image->height=height;
    out_image->stride=width*4u;
    return QUANTAPDF_OK;
}

/* The source operation is immutable. Project a single borrowed resource
 * and normalize only stack-local ID copies to the single-layer profiles. */
quantapdf_status quantapdf_ir_raster_scoped_mixed_v5(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    quantapdf_ir projected={0};
    quantapdf_composer_page_state transparent={0};
    quantapdf_composer_operation operation={0};
    quantapdf_ir_raster_image layers[QP_V5_MAX_LAYERS]={{0}};
    quantapdf_ir_raster_v1_limits geometry_limits;
    quantapdf_ir_raster_image_v1_limits image_limits;
    unsigned char *result=NULL;
    size_t width,height,pixels,bytes,i,j;
    size_t edge_layers=0u,source_work=0u,draw_work=0u;
    unsigned image_used=0u,clip_used=0u;
    size_t path_count=0u,image_count=0u;
    quantapdf_status status=QUANTAPDF_ERROR_UNSUPPORTED;
    const quantapdf_composer_page_state *page;

    if(out_image==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if(ir==NULL || limits==NULL || ir->pages==NULL ||
       (ir->operations==NULL && ir->operation_count!=0u) ||
       ir->page_count!=1u || page_index!=0u ||
       limits->max_pixels==0u || limits->max_operations==0u ||
       limits->max_edges==0u || limits->max_sample_tests==0u ||
       limits->max_source_pixels==0u || limits->max_drawn_pixels==0u)
        return QUANTAPDF_ERROR_ARGUMENT;

    if(ir->operation_count==0u)
        return v5_render_blank(ir,limits,out_image);

    if(ir->operation_count>QP_V5_MAX_LAYERS ||
       ir->operation_count>limits->max_operations ||
       (ir->resources.image_count!=0u &&
        (ir->operation_count<2u ||
         ir->resources.image_count>QP_V5_MAX_LAYERS ||
         ir->resources.images==NULL)) ||
       ir->resources.graphics_state_count>1u ||
       ir->resources.clip_count!=ir->resources.graphics_state_count ||
       (ir->resources.graphics_state_count!=0u &&
        (ir->resources.graphics_states==NULL ||
         ir->resources.clips==NULL)) ||
       ir->resources.font_count!=0u || ir->resources.form_count!=0u ||
       ir->resources.soft_mask_count!=0u ||
       ir->resources.paint_count!=0u ||
       ir->link_count!=0u || ir->outline_count!=0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    /* Exact one/two PATHs with zero Images are a separately admitted
     * native-only profile, not an unvalidated partial mixed Image page.
     * Refuse ALL resource pointers, capacities and counts so no hidden
     * Image/Clip/GS/Font can be discarded by the fast path. */
    if(ir->resources.image_count==0u) {
        const quantapdf_resource_registry *r=&ir->resources;
        if(ir->operation_count==0u || ir->operation_count>2u ||
           r->images!=NULL || r->image_capacity!=0u ||
           r->graphics_states!=NULL || r->graphics_state_count!=0u ||
           r->graphics_state_capacity!=0u ||
           r->clips!=NULL || r->clip_count!=0u ||
           r->clip_capacity!=0u ||
           r->fonts!=NULL || r->font_count!=0u ||
           r->font_capacity!=0u ||
           r->forms!=NULL || r->form_count!=0u ||
           r->form_capacity!=0u ||
           r->soft_masks!=NULL || r->soft_mask_count!=0u ||
           r->soft_mask_capacity!=0u ||
           r->paints!=NULL || r->paint_count!=0u ||
           r->paint_capacity!=0u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    page=&ir->pages[0];
    if(!isfinite(page->width_points) || !isfinite(page->height_points) ||
       page->width_points<=0.0f || page->height_points<=0.0f ||
       page->width_points>(float)QP_V5_MAX_PIXELS ||
       page->height_points>(float)QP_V5_MAX_PIXELS ||
       (page->suppress_background!=0 && page->suppress_background!=1) ||
       (!page->suppress_background &&
        (page->background_argb>>24u)!=255u))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    width=(size_t)page->width_points;
    height=(size_t)page->height_points;
    if(width==0u || height==0u ||
       (float)width!=page->width_points ||
       (float)height!=page->height_points ||
       width>QP_V5_MAX_PIXELS/height)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    pixels=width*height;
    if(pixels>limits->max_pixels)return QUANTAPDF_ERROR_UNSUPPORTED;
    bytes=pixels*4u;

    if(ir->resources.graphics_state_count==1u) {
        const quantapdf_composer_graphics_state *gs=
            &ir->resources.graphics_states[0];
        const quantapdf_composer_clip_state *clip=&ir->resources.clips[0];
        if(gs->fill_alpha!=1.0f || gs->stroke_alpha!=1.0f ||
           gs->blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL ||
           gs->clip_id!=1u || gs->soft_mask_id!=0u ||
           clip->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
           clip->commands==NULL || clip->command_count==0u ||
           clip->command_count>9u ||
           clip->member_count!=0u || clip->members!=NULL ||
           (clip->fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO &&
            clip->fill_rule!=QUANTAPDF_COMPOSER_FILL_EVEN_ODD))
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    /* Charge unique registered RAW inputs once. Repeated Do may reuse the
     * same owned resource, but its destination work is charged each time. */
    for(i=0u;i<ir->resources.image_count;++i) {
        const quantapdf_composer_image_state *source=
            &ir->resources.images[i];
        size_t count,bytes_needed;
        if(source->format!=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW ||
           source->data==NULL || source->width==0u || source->height==0u ||
           (source->components!=1 && source->components!=3) ||
           (source->has_alpha!=0 && source->has_alpha!=1) ||
           (size_t)source->width>QP_V5_MAX_SOURCE_PIXELS/
               (size_t)source->height)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        count=(size_t)source->width*(size_t)source->height;
        if(count>limits->max_source_pixels ||
           count>QP_V5_MAX_SOURCE_PIXELS-source_work ||
           source_work>limits->max_source_pixels-count)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        bytes_needed=count*(size_t)source->components;
        if(source->size!=bytes_needed ||
           (source->has_alpha ?
             (source->alpha_data==NULL || source->alpha_size!=count) :
             (source->alpha_data!=NULL || source->alpha_size!=0u)))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        source_work+=count;
    }

    /* Reject *every* unknown or late operation and unused resource BEFORE
     * allocating output. Frozen Clip membership is per operation, never
     * implicitly inherited from the preceding layer. */
    for(i=0u;i<ir->operation_count;++i) {
        const quantapdf_composer_operation *op=&ir->operations[i];
        const size_t state=(size_t)op->graphics_state_id;
        if(op->page_index!=0u ||
           state>ir->resources.graphics_state_count)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if(state!=0u)clip_used=1u;
        if(op->kind==QUANTAPDF_COMPOSER_OPERATION_PATH) {
            const quantapdf_composer_path_operation *p=&op->value.path;
            const quantapdf_composer_path_options *opt=&p->options;
            if(opt->struct_size<QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE ||
               opt->fill!=1 || opt->stroke!=0 ||
               (opt->fill_argb>>24u)!=255u ||
               p->commands==NULL || p->command_count==0u ||
               p->command_count>10240u ||
               p->dash_count!=0u || p->dash_lengths!=NULL ||
               (state!=0u &&
                opt->struct_size<QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE) ||
               (opt->struct_size>=QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE &&
                (size_t)opt->graphics_state_id!=state) ||
               (opt->struct_size>=QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_MIN_SIZE &&
                (opt->fill_paint_id!=0u || opt->stroke_paint_id!=0u)))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            ++path_count;
            ++edge_layers;
        } else if(op->kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
            const quantapdf_composer_image_options *opt=&op->value.image.options;
            size_t x0,x1,y0,y1,id,area;
            if(opt->struct_size<QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_MIN_SIZE ||
               opt->fit!=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH ||
               (state!=0u &&
                opt->struct_size<QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_MIN_SIZE) ||
               (opt->struct_size>=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_MIN_SIZE &&
                (size_t)opt->graphics_state_id!=state))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            id=(size_t)op->value.image.image_id;
            if(id==0u || id>ir->resources.image_count ||
               !v5_integer(op->bounds.x0,width,&x0) ||
               !v5_integer(op->bounds.y0,height,&y0) ||
               !v5_integer(op->bounds.x1,width,&x1) ||
               !v5_integer(op->bounds.y1,height,&y1) ||
               x1<=x0 || y1<=y0)
                return QUANTAPDF_ERROR_UNSUPPORTED;
            area=(x1-x0)*(y1-y0);
            if(area>limits->max_drawn_pixels ||
               area>QP_V5_MAX_DRAW_WORK-draw_work ||
               draw_work>limits->max_drawn_pixels-area)
                return QUANTAPDF_ERROR_UNSUPPORTED;
            draw_work+=area;
            image_used|=1u<<(unsigned)(id-1u);
            ++image_count;
            if(state!=0u)++edge_layers; /* clip mask needs 4x4 work */
        } else {
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }
    if(path_count==0u ||
       (ir->resources.image_count!=0u && image_count==0u) ||
       (ir->resources.image_count==0u && image_count!=0u) ||
       image_used!=((1u<<(unsigned)ir->resources.image_count)-1u) ||
       clip_used!=ir->resources.clip_count ||
       edge_layers==0u ||
       limits->max_edges/edge_layers==0u ||
       limits->max_sample_tests/edge_layers==0u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    geometry_limits.max_pixels=limits->max_pixels;
    geometry_limits.max_operations=1u;
    geometry_limits.max_edges=limits->max_edges/edge_layers;
    geometry_limits.max_sample_tests=limits->max_sample_tests/edge_layers;
    image_limits.max_pixels=limits->max_pixels;
    image_limits.max_operations=1u;
    image_limits.max_source_pixels=limits->max_source_pixels;
    image_limits.max_drawn_pixels=limits->max_drawn_pixels;
    transparent=*page;
    transparent.suppress_background=1;
    transparent.background_argb=0u;
    projected.pages=&transparent;
    projected.page_count=1u;
    projected.operations=&operation;
    projected.operation_count=1u;

    for(i=0u;i<ir->operation_count;++i) {
        const quantapdf_composer_operation *op=&ir->operations[i];
        const size_t state=(size_t)op->graphics_state_id;
        operation=*op;
        memset(&projected.resources,0,sizeof(projected.resources));
        if(op->kind==QUANTAPDF_COMPOSER_OPERATION_PATH) {
            operation.graphics_state_id=0u;
            operation.value.path.options.graphics_state_id=0u;
            status=state==0u ?
                quantapdf_ir_raster_render_v1(&projected,0u,
                    &geometry_limits,&layers[i]) :
                quantapdf_ir_raster_render_v1_clipped(&projected,0u,
                    &geometry_limits,&ir->resources.clips[0],&layers[i]);
        } else {
            const size_t id=(size_t)op->value.image.image_id;
            projected.resources.images=&ir->resources.images[id-1u];
            projected.resources.image_count=1u;
            operation.value.image.image_id=1u;
            if(state!=0u) {
                projected.resources.graphics_states=
                    ir->resources.graphics_states;
                projected.resources.graphics_state_count=1u;
                projected.resources.clips=ir->resources.clips;
                projected.resources.clip_count=1u;
                status=quantapdf_ir_raster_clipped_raw_image_v1(
                    &projected,0u,&image_limits,
                    &geometry_limits,&layers[i]);
            } else {
                status=quantapdf_ir_raster_raw_images_v1(
                    &projected,0u,&image_limits,&layers[i]);
            }
        }
        if(status!=QUANTAPDF_OK)goto cleanup;
        if(layers[i].pixels==NULL ||
           layers[i].width!=width || layers[i].height!=height ||
           layers[i].stride!=width*4u || layers[i].size!=bytes) {
            status=QUANTAPDF_ERROR_UNSUPPORTED;
            goto cleanup;
        }
    }

    result=(unsigned char *)malloc(bytes);
    if(result==NULL){status=QUANTAPDF_ERROR_NOMEM;goto cleanup;}
    for(j=0u;j<pixels;++j) {
        unsigned char *p=result+j*4u;
        const uint32_t bg=page->background_argb;
        p[0]=page->suppress_background?0u:
            (unsigned char)((bg>>16u)&255u);
        p[1]=page->suppress_background?0u:
            (unsigned char)((bg>>8u)&255u);
        p[2]=page->suppress_background?0u:
            (unsigned char)(bg&255u);
        p[3]=page->suppress_background?0u:255u;
        for(i=0u;i<ir->operation_count;++i)
            v5_over(p,layers[i].pixels+j*4u);
    }
    out_image->pixels=result;
    out_image->size=bytes;
    out_image->width=width;
    out_image->height=height;
    out_image->stride=width*4u;
    result=NULL;
    status=QUANTAPDF_OK;
cleanup:
    free(result);
    for(i=0u;i<QP_V5_MAX_LAYERS;++i)
        quantapdf_ir_raster_image_dispose(&layers[i]);
    return status;
}
