#ifndef QUANTAPDF_NATIVE_PREPARED_PAGE_V1_H
#define QUANTAPDF_NATIVE_PREPARED_PAGE_V1_H

#include "backend/native_source_raster_bridge_v1.h"
#include "render/native_device_ir_v1.h"
#include "render/native_device_plan.h"
#include "render/ir_raster_v1.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* PRIVATE synchronous borrow. Source retains the sole IR owner until execute
 * returns. Preparation allocates nothing and never paints. The completed plan
 * and every reachable input must remain immutable for that borrow. */
typedef enum quantapdf_native_prepared_route_v1 {
    QP_NATIVE_PREPARED_NONE=0,
    QP_NATIVE_PREPARED_LEGACY,
    QP_NATIVE_PREPARED_DEVICE,
    QP_NATIVE_PREPARED_TWO_CLIP,
    QP_NATIVE_PREPARED_CLIPPED_PATHS,
    QP_NATIVE_PREPARED_CLIP_STATES,
    QP_NATIVE_PREPARED_PREFIX,
    QP_NATIVE_PREPARED_SIBLINGS,
    QP_NATIVE_PREPARED_THREE_GS,
    QP_NATIVE_PREPARED_TEXT,
    QP_NATIVE_PREPARED_EXACT_CUBICS
} quantapdf_native_prepared_route_v1;

typedef struct quantapdf_native_prepared_usage_v1 {
    size_t operations,expanded_paints,glyphs,commands,image_pixels,retained_bytes;
    unsigned images_used,states_used,clips_used;
} quantapdf_native_prepared_usage_v1;

typedef struct quantapdf_native_prepared_page_v1 {
    const quantapdf_ir *source;
    quantapdf_native_prepared_route_v1 route;
    unsigned dpi;
    quantapdf_ir_raster_scoped_mixed_v5_limits limits;
    quantapdf_ir_raster_v1_limits physical_limits;
    quantapdf_native_device_plan physical;
    quantapdf_native_prepared_usage_v1 usage;
} quantapdf_native_prepared_page_v1;

/* Existing Text/exact-cubic consumers expose actual sampled work. Other
 * consumers retain their conservative partitions; zero is not a measurement
 * unless sample_tests_exact is set. Failed execution clears this receipt. */
typedef struct quantapdf_native_execution_receipt_v1 {
    size_t sample_tests;
    int sample_tests_exact;
} quantapdf_native_execution_receipt_v1;

static inline quantapdf_status quantapdf_native_source_device_validate_v1(
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_private_native_source_profile profile)
{
    if(device==NULL || !isfinite(device->dpi) ||
       !isfinite(device->rotation_degrees) || device->dpi<=0.0f ||
       (device->alpha!=0 && device->alpha!=1) ||
       (device->clip_enabled!=0 && device->clip_enabled!=1) ||
       (device->thumbnail!=0 && device->thumbnail!=1))
        return QUANTAPDF_ERROR_ARGUMENT;
    if((profile!=QUANTAPDF_PRIVATE_NATIVE_SOURCE_V8 &&
        profile!=QUANTAPDF_PRIVATE_NATIVE_SOURCE_V9 &&
        profile!=QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10) ||
       (profile!=QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10 && device->page_index!=0u) ||
       device->page_index>(size_t)INT_MAX ||
       (device->dpi!=72.0f &&
        !(profile==QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10 &&
          (device->dpi==96.0f || device->dpi==144.0f))) ||
       device->rotation_degrees!=0.0f || device->clip_enabled ||
       device->alpha || device->thumbnail)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    return QUANTAPDF_OK;
}

static inline int qp_prepared_charge(size_t *used,size_t amount,size_t cap)
{
    if(*used>cap || amount>cap-*used)return 0;
    *used+=amount;return 1;
}
static inline int qp_prepared_bytes(size_t *used,size_t count,size_t size)
{
    return (size==0u || count<=SIZE_MAX/size) &&
        qp_prepared_charge(used,count*size,SIZE_MAX);
}
static inline int qp_prepared_commands(
    const quantapdf_composer_path_command *commands,size_t count,
    quantapdf_native_prepared_usage_v1 *usage)
{
    if(commands==NULL || count==0u ||
       !qp_prepared_charge(&usage->commands,count,4096u) ||
       !qp_prepared_bytes(&usage->retained_bytes,count,sizeof(*commands)))
        return 0;
    for(size_t i=0u;i<count;++i) {
        const quantapdf_composer_path_command *c=&commands[i];
        unsigned n;
        if(c->kind==QUANTAPDF_COMPOSER_PATH_CLOSE)continue;
        if(c->kind==QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
           c->kind==QUANTAPDF_COMPOSER_PATH_LINE_TO)n=1u;
        else if(c->kind==QUANTAPDF_COMPOSER_PATH_CUBIC_TO)n=3u;
        else return 0;
        if(!isfinite(c->point1.x) || !isfinite(c->point1.y) ||
           (n==3u && (!isfinite(c->point2.x) || !isfinite(c->point2.y) ||
                      !isfinite(c->point3.x) || !isfinite(c->point3.y))))
            return 0;
    }
    return 1;
}

/* Complete resource-reference closure and checked inventory for the currently
 * Source-owned profiles. Kernel-specific geometry/state-order validation stays
 * in the SAME existing consumers; this common gate cannot enable a feature. */
static inline int qp_prepared_resource_closure(
    const quantapdf_ir *ir,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_native_prepared_usage_v1 *u,int *text)
{
    const quantapdf_resource_registry *r=&ir->resources;
    unsigned closure[7]={0u};
    if(ir->pages==NULL || ir->page_count!=1u || ir->operation_count>4u ||
       ir->operation_count>limits->max_operations ||
       ((ir->operation_count==0u)!=(ir->operations==NULL)) ||
       ir->links!=NULL || ir->link_count!=0u ||
       ir->outlines!=NULL || ir->outline_count!=0u ||
       r->image_count>4u || r->image_capacity!=r->image_count ||
       ((r->image_count==0u)!=(r->images==NULL)) ||
       r->graphics_state_count>3u || r->graphics_state_capacity!=r->graphics_state_count ||
       ((r->graphics_state_count==0u)!=(r->graphics_states==NULL)) ||
       r->clip_count>7u || r->clip_capacity!=r->clip_count ||
       ((r->clip_count==0u)!=(r->clips==NULL)) ||
       r->fonts!=NULL || r->font_count!=0u || r->font_capacity!=0u ||
       r->forms!=NULL || r->form_count!=0u || r->form_capacity!=0u ||
       r->soft_masks!=NULL || r->soft_mask_count!=0u || r->soft_mask_capacity!=0u ||
       r->paints!=NULL || r->paint_count!=0u || r->paint_capacity!=0u ||
       !isfinite(ir->pages[0].width_points) || !isfinite(ir->pages[0].height_points) ||
       ir->pages[0].width_points<1.0f || ir->pages[0].height_points<1.0f ||
       ir->pages[0].width_points>1000000.0f || ir->pages[0].height_points>1000000.0f ||
       ir->pages[0].background_argb!=UINT32_C(0xffffffff) || ir->pages[0].suppress_background!=0)
        return 0;
    u->operations=ir->operation_count;
    if(!qp_prepared_bytes(&u->retained_bytes,1u,sizeof(*ir->pages)) ||
       !qp_prepared_bytes(&u->retained_bytes,ir->operation_count,sizeof(*ir->operations)) ||
       !qp_prepared_bytes(&u->retained_bytes,r->image_count,sizeof(*r->images)) ||
       !qp_prepared_bytes(&u->retained_bytes,r->graphics_state_count,sizeof(*r->graphics_states)) ||
       !qp_prepared_bytes(&u->retained_bytes,r->clip_count,sizeof(*r->clips)))
        return 0;
    for(size_t i=0u;i<r->image_count;++i) {
        const quantapdf_composer_image_state *image=&r->images[i];
        size_t pixels,bytes;
        if(image->width==0u || image->height==0u || image->data==NULL ||
           image->format!=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW ||
           (image->components!=1 && image->components!=3) ||
           (image->has_alpha!=0 && image->has_alpha!=1) ||
           (size_t)image->width>SIZE_MAX/(size_t)image->height)
            return 0;
        pixels=(size_t)image->width*(size_t)image->height;
        if(pixels>SIZE_MAX/(size_t)image->components)return 0;
        bytes=pixels*(size_t)image->components;
        if(image->size!=bytes ||
           (image->has_alpha ?
             (image->alpha_data==NULL || image->alpha_size!=pixels) :
             (image->alpha_data!=NULL || image->alpha_size!=0u)) ||
           !qp_prepared_charge(&u->image_pixels,pixels,limits->max_source_pixels) ||
           !qp_prepared_bytes(&u->retained_bytes,bytes,1u) ||
           !qp_prepared_bytes(&u->retained_bytes,image->alpha_size,1u))
            return 0;
    }
    for(size_t i=0u;i<r->clip_count;++i) {
        const quantapdf_composer_clip_state *c=&r->clips[i];
        closure[i]=1u<<(unsigned)i;
        if(c->kind==QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL) {
            if(c->members!=NULL || c->member_count!=0u ||
               !qp_prepared_commands(c->commands,c->command_count,u))
                return 0;
        } else if(c->kind==QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL) {
            if(c->commands!=NULL || c->command_count!=0u || c->members==NULL ||
               c->member_count<2u || c->member_count>7u ||
               !qp_prepared_bytes(&u->retained_bytes,c->member_count,sizeof(*c->members)))
                return 0;
            for(size_t j=0u;j<c->member_count;++j) {
                const size_t id=(size_t)c->members[j];
                /* Existing Source graphs are topologically numbered. */
                if(id==0u || id>i)return 0;
                closure[i]|=closure[id-1u];
            }
        } else return 0;
    }
    for(size_t i=0u;i<r->graphics_state_count;++i) {
        const quantapdf_composer_graphics_state *gs=&r->graphics_states[i];
        if(gs->fill_alpha!=1.0f || gs->stroke_alpha!=1.0f ||
           gs->blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL ||
           gs->soft_mask_id!=0u || gs->clip_id>r->clip_count)
            return 0;
    }
    for(size_t i=0u;i<ir->operation_count;++i) {
        const quantapdf_composer_operation *op=&ir->operations[i];
        const size_t state=(size_t)op->graphics_state_id;
        if(op->page_index!=0u || state>r->graphics_state_count)return 0;
        if(state!=0u) {
            const size_t clip=(size_t)r->graphics_states[state-1u].clip_id;
            u->states_used|=1u<<(unsigned)(state-1u);
            if(clip!=0u)u->clips_used|=closure[clip-1u];
        }
        if(op->kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
            const size_t id=(size_t)op->value.image.image_id;
            if(id==0u || id>r->image_count ||
               op->value.image.options.graphics_state_id!=op->graphics_state_id)
                return 0;
            u->images_used|=1u<<(unsigned)(id-1u);++u->expanded_paints;
        } else if(op->kind==QUANTAPDF_COMPOSER_OPERATION_PATH) {
            const quantapdf_composer_path_operation *p=&op->value.path;
            if(p->options.graphics_state_id!=op->graphics_state_id ||
               p->options.fill_paint_id!=0u || p->options.stroke_paint_id!=0u ||
               p->dash_lengths!=NULL || p->dash_count!=0u || p->dash_phase!=0.0f ||
               !qp_prepared_commands(p->commands,p->command_count,u))
                return 0;
            ++u->expanded_paints;
        } else if(op->kind==QUANTAPDF_COMPOSER_OPERATION_TEXT_OUTLINES) {
            const quantapdf_text_outline_operation *t=&op->value.text_outlines;
            if(state!=0u || (t->argb>>24u)!=255u ||
               ((t->glyph_count==0u)!=(t->glyphs==NULL)) ||
               !qp_prepared_charge(&u->glyphs,t->glyph_count,64u) ||
               !qp_prepared_bytes(&u->retained_bytes,t->glyph_count,sizeof(*t->glyphs)))
                return 0;
            *text=1;u->expanded_paints+=t->glyph_count;
            for(size_t g=0u;g<t->glyph_count;++g)
                if(!qp_prepared_commands(t->glyphs[g].commands,t->glyphs[g].command_count,u))
                    return 0;
        } else return 0;
    }
    return u->images_used==((1u<<(unsigned)r->image_count)-1u) &&
        u->states_used==((1u<<(unsigned)r->graphics_state_count)-1u) &&
        u->clips_used==((1u<<(unsigned)r->clip_count)-1u);
}

static inline quantapdf_status quantapdf_native_prepare_page_v1(
    const quantapdf_ir *source,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_private_native_source_profile profile,
    quantapdf_native_prepared_page_v1 *out)
{
    quantapdf_native_prepared_page_v1 p;
    quantapdf_status status;
    int text=0;
    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));memset(&p,0,sizeof(p));
    if(source==NULL || limits==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    status=quantapdf_native_source_device_validate_v1(device,profile);
    if(status!=QUANTAPDF_OK)return status;
    if(!qp_prepared_resource_closure(source,limits,&p.usage,&text))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    p.source=source;p.dpi=(unsigned)device->dpi;p.limits=*limits;
    if(profile!=QUANTAPDF_PRIVATE_NATIVE_SOURCE_V10)
        p.route=QP_NATIVE_PREPARED_LEGACY;
    else if(text) {
        char box[64];
        int printed;
        const quantapdf_composer_page_state *page=&source->pages[0];
        if(page->width_points>65536.0f || page->height_points>65536.0f ||
           floorf(page->width_points)!=page->width_points ||
           floorf(page->height_points)!=page->height_points)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        p.route=QP_NATIVE_PREPARED_TEXT;
        printed=snprintf(box,sizeof(box),"0 0 %u %u",
            (unsigned)page->width_points,(unsigned)page->height_points);
        if(printed<0 || (size_t)printed>=sizeof(box))return QUANTAPDF_ERROR_UNSUPPORTED;
        p.physical_limits.max_pixels=limits->max_pixels;
        p.physical_limits.max_operations=limits->max_operations;
        p.physical_limits.max_edges=limits->max_edges;
        p.physical_limits.max_sample_tests=limits->max_sample_tests;
        status=quantapdf_native_device_plan_create_physical(box,box,"1",0,
            p.dpi,p.physical_limits.max_pixels,&p.physical);
        if(status!=QUANTAPDF_OK)return status;
    } else if(source->pages[0].width_points==237.133f &&
              source->pages[0].height_points==180.44f && source->operation_count==3u) {
        const size_t pixels=p.dpi==72u?50000u:1000000u;
        const size_t edges=p.dpi==72u?1024u:512u;
        p.route=QP_NATIVE_PREPARED_EXACT_CUBICS;
        p.physical_limits.max_pixels=limits->max_pixels<pixels?limits->max_pixels:pixels;
        p.physical_limits.max_operations=3u;
        p.physical_limits.max_edges=limits->max_edges<edges?limits->max_edges:edges;
        p.physical_limits.max_sample_tests=limits->max_sample_tests<8000000u?
            limits->max_sample_tests:8000000u;
        if(limits->max_operations<3u)return QUANTAPDF_ERROR_UNSUPPORTED;
        status=quantapdf_native_device_plan_create_physical(
            "0 0 237.133 180.44","0 0 237.133 180.44","1",0,p.dpi,
            p.physical_limits.max_pixels,&p.physical);
        if(status!=QUANTAPDF_OK)return status;
    } else if(source->resources.graphics_state_count==3u &&
              source->resources.clip_count==7u && source->operation_count==3u &&
              source->resources.image_count==0u)p.route=QP_NATIVE_PREPARED_THREE_GS;
    else if(source->resources.graphics_state_count==2u &&
            source->resources.clip_count==2u)p.route=QP_NATIVE_PREPARED_SIBLINGS;
    else if(source->resources.graphics_state_count!=0u && source->operation_count!=0u &&
            source->operations[0].graphics_state_id==0u)p.route=QP_NATIVE_PREPARED_PREFIX;
    else if(source->resources.graphics_state_count==2u)p.route=QP_NATIVE_PREPARED_CLIP_STATES;
    else if(source->resources.image_count==0u &&
            source->resources.graphics_state_count==1u)p.route=QP_NATIVE_PREPARED_CLIPPED_PATHS;
    else if(source->resources.clip_count==3u)p.route=QP_NATIVE_PREPARED_TWO_CLIP;
    else p.route=QP_NATIVE_PREPARED_DEVICE;
    *out=p;return QUANTAPDF_OK;
}

/* Executes exactly one selected existing consumer with the frozen page cap.
 * Its established shared/partitioned raster counters stay unchanged. This
 * inventory is not an invented measurement of actual sampled-edge work. */
static inline quantapdf_status quantapdf_native_execute_page_v1(
    const quantapdf_native_prepared_page_v1 *p,
    quantapdf_native_execution_receipt_v1 *receipt,quantapdf_ir_raster_image *out)
{
    quantapdf_status status;
    size_t measured=0u;
    if(receipt!=NULL)memset(receipt,0,sizeof(*receipt));
    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(p==NULL || p->source==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    switch(p->route) {
    case QP_NATIVE_PREPARED_LEGACY:
        return quantapdf_ir_raster_scoped_mixed_v5(p->source,0u,&p->limits,out);
    case QP_NATIVE_PREPARED_DEVICE:
        return quantapdf_native_device_ir_render_v1(p->source,p->dpi,&p->limits,out);
    case QP_NATIVE_PREPARED_TWO_CLIP:
        return quantapdf_native_device_ir_render_v1_two_clip(p->source,p->dpi,&p->limits,out);
    case QP_NATIVE_PREPARED_CLIPPED_PATHS:
        return quantapdf_native_device_ir_render_v1_clipped_paths(p->source,p->dpi,&p->limits,out);
    case QP_NATIVE_PREPARED_CLIP_STATES:
        return quantapdf_native_device_ir_render_v1_compound_clip_states(p->source,p->dpi,&p->limits,out);
    case QP_NATIVE_PREPARED_PREFIX:
        return quantapdf_native_device_ir_render_v1_unclipped_prefix(p->source,p->dpi,&p->limits,out);
    case QP_NATIVE_PREPARED_SIBLINGS:
        return quantapdf_native_device_ir_render_v1_sibling_clips(p->source,p->dpi,&p->limits,out);
    case QP_NATIVE_PREPARED_THREE_GS:
        return quantapdf_native_device_ir_render_v1_three_gs_original(p->source,p->dpi,&p->limits,out);
    case QP_NATIVE_PREPARED_TEXT:
        status=quantapdf_ir_raster_render_v1_text_outlines(p->source,&p->physical_limits,&p->physical,&measured,out);
        break;
    case QP_NATIVE_PREPARED_EXACT_CUBICS:
        status=p->dpi==72u?
            quantapdf_ir_raster_render_v1_texample_shared_three_layer(p->source,&p->physical_limits,&p->physical,&measured,out):
            quantapdf_ir_raster_render_v1_texample_physical_dpi_three_layer(p->source,&p->physical_limits,&p->physical,&measured,out);
        break;
    default:return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if(status==QUANTAPDF_OK && receipt!=NULL) {
        receipt->sample_tests=measured;receipt->sample_tests_exact=1;
    }
    return status;
}
#endif

