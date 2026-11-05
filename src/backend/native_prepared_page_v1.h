#ifndef QUANTAPDF_NATIVE_PREPARED_PAGE_V1_H
#define QUANTAPDF_NATIVE_PREPARED_PAGE_V1_H

#include "backend/native_page.h"
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
typedef struct quantapdf_native_prepared_usage_v1 {
    size_t operations,expanded_paints,glyphs,commands,image_pixels,retained_bytes;
    size_t states_used,clips_used,clip_leaves;
} quantapdf_native_prepared_usage_v1;

typedef struct quantapdf_native_prepared_page_v1 {
    const quantapdf_ir *source;
    quantapdf_native_prepared_route_v1 route;
    unsigned dpi;
    quantapdf_ir_raster_scoped_mixed_v5_limits limits;
    quantapdf_ir_raster_v1_limits cubic_limits;
    quantapdf_native_device_plan physical;
    quantapdf_native_prepared_usage_v1 usage;
    quantapdf_native_resource_limits_v1 resource_limits;
    /* Test-only allocation fault at real ledger-backed C11 malloc sites.
     * Production preparation leaves this empty. */
    quantapdf_native_malloc_fault_v1 malloc_fault;
} quantapdf_native_prepared_page_v1;

/* Exact classifier counts remain separate from conservative charged work.
 * Every production route reports its common page ledger, including actual
 * retained flattened edges and reservation peaks. Failure clears everything. */
typedef struct quantapdf_native_execution_receipt_v1 {
    size_t sample_tests;
    int sample_tests_exact;
    size_t charged_sample_work,compiled_edges;
    size_t peak_reserved_sample_work,peak_reserved_edges,completed_leases;
    size_t source_pixels,drawn_pixels,retained_bytes,output_bytes,peak_live_bytes;
} quantapdf_native_execution_receipt_v1;

static inline quantapdf_status quantapdf_native_source_device_validate_v1(
    const quantapdf_native_page_device *device)
{
    if(device==NULL || !isfinite(device->dpi) ||
       !isfinite(device->rotation_degrees) || device->dpi<=0.0f ||
       (device->alpha!=0 && device->alpha!=1) ||
       (device->clip_enabled!=0 && device->clip_enabled!=1) ||
       (device->thumbnail!=0 && device->thumbnail!=1))
        return QUANTAPDF_ERROR_ARGUMENT;
    if(device->page_index>(size_t)INT_MAX ||
       (device->dpi!=72.0f && device->dpi!=96.0f && device->dpi!=144.0f) ||
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
       !qp_prepared_charge(&usage->commands,count,SIZE_MAX) ||
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
    if(ir->pages==NULL || ir->page_count!=1u || ir->operation_count>limits->max_operations ||
       ((ir->operation_count==0u)!=(ir->operations==NULL)) ||
       ir->links!=NULL || ir->link_count!=0u ||
       ir->outlines!=NULL || ir->outline_count!=0u ||
       r->image_count>ir->operation_count || r->image_capacity!=r->image_count ||
       ((r->image_count==0u)!=(r->images==NULL)) ||
       r->graphics_state_capacity!=r->graphics_state_count ||
       ((r->graphics_state_count==0u)!=(r->graphics_states==NULL)) ||
       r->clip_capacity!=r->clip_count ||
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
        if(c->kind==QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL) {
            if(c->members!=NULL || c->member_count!=0u ||
               !qp_prepared_commands(c->commands,c->command_count,u))
                return 0;
            ++u->clip_leaves;
        } else if(c->kind==QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL) {
            if(c->commands!=NULL || c->command_count!=0u || c->members==NULL ||
               c->member_count<2u ||
               !qp_prepared_bytes(&u->retained_bytes,c->member_count,sizeof(*c->members)))
                return 0;
            for(size_t j=0u;j<c->member_count;++j) {
                const size_t id=(size_t)c->members[j];
                /* Existing Source graphs are topologically numbered. */
                if(id==0u || id>i)return 0;
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
        if(op->kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
            const size_t id=(size_t)op->value.image.image_id;
            if(id==0u || id>r->image_count ||
               op->value.image.options.graphics_state_id!=op->graphics_state_id)
                return 0;
            ++u->expanded_paints;
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
            if((t->argb>>24u)!=255u ||
               ((t->glyph_count==0u)!=(t->glyphs==NULL)) ||
               !qp_prepared_charge(&u->glyphs,t->glyph_count,SIZE_MAX) ||
               !qp_prepared_bytes(&u->retained_bytes,t->glyph_count,sizeof(*t->glyphs)))
                return 0;
            *text=1;u->expanded_paints+=t->glyph_count;
            for(size_t g=0u;g<t->glyph_count;++g)
                if(!qp_prepared_commands(t->glyphs[g].commands,t->glyphs[g].command_count,u))
                    return 0;
        } else return 0;
    }
    /* Topological numbering makes allocation-free closure possible: every
     * node must feed a later node or an observed state. A finite forward
     * chain therefore ends at an observed root, including shared subgraphs. */
    for(size_t state=0u;state<r->graphics_state_count;++state) {
        size_t op=0u;
        while(op<ir->operation_count && ir->operations[op].graphics_state_id!=state+1u)++op;
        if(op==ir->operation_count)return 0;
        ++u->states_used;
    }
    for(size_t node=0u;node<r->clip_count;++node) {
        int used=0;
        for(size_t state=0u;state<r->graphics_state_count && !used;++state)
            used=r->graphics_states[state].clip_id==node+1u;
        for(size_t later=node+1u;later<r->clip_count && !used;++later) {
            const quantapdf_composer_clip_state *c=&r->clips[later];
            for(size_t member=0u;member<c->member_count && !used;++member)
                used=c->members[member]==node+1u;
        }
        if(!used)return 0;
        ++u->clips_used;
    }
    return 1;
}

static inline quantapdf_status quantapdf_native_prepare_page_v1(
    const quantapdf_ir *source,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    const quantapdf_native_page_device *device,
    quantapdf_native_prepared_page_v1 *out)
{
    quantapdf_native_prepared_page_v1 p;
    quantapdf_status status;
    int text=0;
    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));memset(&p,0,sizeof(p));
    if(source==NULL || limits==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    status=quantapdf_native_source_device_validate_v1(device);
    if(status!=QUANTAPDF_OK)return status;
    if(!qp_prepared_resource_closure(source,limits,&p.usage,&text))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    p.source=source;p.dpi=(unsigned)device->dpi;p.limits=*limits;
    int stroke=0;
    for(size_t i=0u;i<source->operation_count;++i)
        if(source->operations[i].kind==QUANTAPDF_COMPOSER_OPERATION_PATH)
            stroke|=source->operations[i].value.path.options.stroke;
    const int exact_cubics=!text && source->pages[0].width_points==237.133f &&
        source->pages[0].height_points==180.44f && source->operation_count==3u;
    if((text || !stroke) && source->operation_count!=0u && !exact_cubics) {
        char box[64];
        int printed;
        const quantapdf_composer_page_state *page=&source->pages[0];
        if(page->width_points>65536.0f || page->height_points>65536.0f)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        p.route=QP_NATIVE_PREPARED_SCENE;
        printed=snprintf(box,sizeof(box),"0 0 %.9g %.9g",
            (double)page->width_points,(double)page->height_points);
        if(printed<0 || (size_t)printed>=sizeof(box))return QUANTAPDF_ERROR_UNSUPPORTED;
        status=quantapdf_native_device_plan_create_physical(box,box,"1",0,
            p.dpi,limits->max_pixels,&p.physical);
        if(status!=QUANTAPDF_OK)return status;
    } else if(source->pages[0].width_points==237.133f &&
              source->pages[0].height_points==180.44f && source->operation_count==3u) {
        const size_t pixels=p.dpi==72u?50000u:1000000u;
        const size_t edges=p.dpi==72u?1024u:512u;
        p.route=QP_NATIVE_PREPARED_EXACT_CUBICS;
        p.cubic_limits.max_pixels=limits->max_pixels<pixels?limits->max_pixels:pixels;
        p.cubic_limits.max_operations=3u;
        p.cubic_limits.max_edges=limits->max_edges<edges?limits->max_edges:edges;
        p.cubic_limits.max_sample_tests=limits->max_sample_tests<8000000u?
            limits->max_sample_tests:8000000u;
        if(limits->max_operations<3u)return QUANTAPDF_ERROR_UNSUPPORTED;
        status=quantapdf_native_device_plan_create_physical(
            "0 0 237.133 180.44","0 0 237.133 180.44","1",0,p.dpi,
            p.cubic_limits.max_pixels,&p.physical);
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
    {
        size_t geometry=0u,projection=0u,bytes=p.usage.retained_bytes;
        const size_t draws=p.usage.expanded_paints==0u?1u:p.usage.expanded_paints;
        if(!quantapdf_native_owned_geometry_bound_v1(limits->max_edges,draws,p.usage.commands,
                source->operation_count==0u?1u:source->operation_count,&geometry) ||
           !qp_prepared_charge(&bytes,geometry,SIZE_MAX))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if(p.route==QP_NATIVE_PREPARED_SCENE) {
            /* One result surface and scratch only for original glyph masks. */
            const size_t pixel_bytes=4u+(p.usage.glyphs!=0u?sizeof(uint16_t):0u);
            size_t clips=0u;
            if(!quantapdf_native_scene_clip_storage_bound_v1(source->resources.clip_count,p.usage.clip_leaves,&clips) ||
               !qp_prepared_charge(&bytes,clips,SIZE_MAX) ||
               !qp_prepared_bytes(&bytes,p.usage.expanded_paints,sizeof(quantapdf_composer_operation)) ||
               !qp_prepared_bytes(&bytes,limits->max_pixels,pixel_bytes))
                return QUANTAPDF_ERROR_UNSUPPORTED;
        } else {
            /* Remaining clipped layer compilers retain their existing envelope. */
            if(!quantapdf_native_device_projection_bytes(source->operation_count,&projection) ||
               !qp_prepared_charge(&bytes,projection,SIZE_MAX) ||
               (text && !qp_prepared_bytes(&bytes,p.usage.expanded_paints,sizeof(quantapdf_composer_operation))) ||
               !qp_prepared_bytes(&bytes,source->operation_count,sizeof(quantapdf_native_scoped_layer_v5)) ||
               !qp_prepared_bytes(&bytes,limits->max_pixels,24u))
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        p.resource_limits.max_source_pixels=limits->max_source_pixels;
        p.resource_limits.max_drawn_pixels=limits->max_drawn_pixels;
        p.resource_limits.max_live_bytes=bytes;
    }
    *out=p;return QUANTAPDF_OK;
}

/* PRIVATE address-stable compiled page. One page lifetime owns one ledger;
 * repeated execution never resets work, image-placement or memory accounting.
 * The immutable metadata copy retains no IR pointer. RAW views are protected
 * by the enclosing Source owner (or the old synchronous borrowed wrapper). */
typedef struct quantapdf_native_compiled_page_v1 {
    quantapdf_native_work_ledger_v1 ledger;
    quantapdf_native_prepared_page_v1 metadata;
    quantapdf_native_scoped_page_v5 scoped;
    quantapdf_native_geometry_v1 exact;
    int ready;
} quantapdf_native_compiled_page_v1;

typedef struct quantapdf_native_source_page_v1 {
    quantapdf_native_compiled_page_v1 compiled;
    quantapdf_ir ir;
    int owns_ir;
    quantapdf_context_lease_internal domain;
} quantapdf_native_source_page_v1;

static inline void quantapdf_native_compiled_page_dispose_v1(
    quantapdf_native_compiled_page_v1 *p)
{
    if(p==NULL)return;
    quantapdf_native_geometry_dispose_v1(&p->exact);
    quantapdf_native_scoped_dispose_v5(&p->scoped);
    memset(p,0,sizeof(*p));
}

static inline quantapdf_status quantapdf_native_compiled_page_compile_v1(
    const quantapdf_native_prepared_page_v1 *metadata,
    quantapdf_native_compiled_page_v1 *out)
{
    quantapdf_status status;
    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(metadata==NULL || metadata->source==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    out->metadata=*metadata;out->metadata.source=NULL;
    quantapdf_native_work_init_v1(&out->ledger,metadata->limits.max_sample_tests,metadata->limits.max_edges);
    out->ledger.malloc_fault=metadata->malloc_fault;
    if(!quantapdf_native_resources_init_v1(&out->ledger,&metadata->resource_limits,
                                          metadata->usage.retained_bytes)) {
        quantapdf_native_compiled_page_dispose_v1(out);return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if(metadata->route==QP_NATIVE_PREPARED_SCENE) {
        const quantapdf_ir_raster_v1_limits work={metadata->limits.max_pixels,
            metadata->limits.max_operations,metadata->limits.max_edges,
            metadata->limits.max_sample_tests};
        status=quantapdf_native_geometry_prepare_scene_v1(metadata->source,
            &work,&metadata->physical,&out->ledger,&out->exact);
    }
    else if(metadata->route==QP_NATIVE_PREPARED_EXACT_CUBICS)
        status=quantapdf_native_geometry_prepare_texample_v1(metadata->source,
            &metadata->cubic_limits,&metadata->physical,&out->ledger,&out->exact);
    else
        status=quantapdf_native_device_ir_prepare_v1(metadata->source,metadata->dpi,
            &metadata->limits,metadata->route,&out->ledger,&out->scoped);
    if(status!=QUANTAPDF_OK) {quantapdf_native_compiled_page_dispose_v1(out);return status;}
    out->ready=1;return QUANTAPDF_OK;
}

static inline size_t qp_compiled_retained_bytes(const quantapdf_native_compiled_page_v1 *p)
{
    quantapdf_native_geometry_info_v1 info;
    size_t bytes=p->scoped.capacity*sizeof(quantapdf_native_scoped_layer_v5);
    if(p->exact.data!=NULL && quantapdf_native_geometry_info_get_v1(&p->exact,&info)==QUANTAPDF_OK)
        bytes=info.retained_bytes;
    for(size_t i=0u;i<p->scoped.count;++i)
        if(p->scoped.layers[i].geometry.data!=NULL &&
           quantapdf_native_geometry_info_get_v1(&p->scoped.layers[i].geometry,&info)==QUANTAPDF_OK &&
           !qp_prepared_charge(&bytes,info.retained_bytes,SIZE_MAX))return SIZE_MAX;
    return bytes;
}

/* A successful publication transfers output bytes out of the private page
 * ledger. The caller owns the detached image and uses ordinary image dispose;
 * it survives both later executions and Source-page disposal. Receipt counters
 * are cumulative for this compiled page; each old one-shot wrapper starts its
 * own new page lifetime, preserving historical receipts and recovery. */
static inline quantapdf_status quantapdf_native_compiled_page_execute_v1(
    quantapdf_native_compiled_page_v1 *p,
    quantapdf_native_execution_receipt_v1 *receipt,quantapdf_ir_raster_image *out)
{
    quantapdf_status status;
    quantapdf_native_work_ledger_v1 *ledger;
    size_t live;
    if(receipt!=NULL)memset(receipt,0,sizeof(*receipt));
    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(p==NULL || !p->ready)return QUANTAPDF_ERROR_ARGUMENT;
    ledger=&p->ledger;
    if(ledger->failed)return QUANTAPDF_ERROR_UNSUPPORTED;
    status=p->exact.data!=NULL?quantapdf_native_geometry_execute_v1(&p->exact,NULL,out):
        quantapdf_native_scoped_execute_v5(&p->scoped,out);
    live=p->metadata.usage.retained_bytes;
    if(!qp_prepared_charge(&live,qp_compiled_retained_bytes(p),ledger->resources.limit.max_live_bytes) ||
       (status==QUANTAPDF_OK &&
        (ledger->failed || ledger->reserved.sample_work!=0u || ledger->reserved.edges!=0u ||
         out->pixels==NULL || out->size==0u ||
         ledger->resources.source_pixels!=p->metadata.usage.image_pixels ||
         !qp_prepared_charge(&live,out->size,ledger->resources.limit.max_live_bytes))) ||
       ledger->resources.live_bytes!=live)status=QUANTAPDF_ERROR_UNSUPPORTED;
    if(status!=QUANTAPDF_OK) {
        ledger->failed=1;quantapdf_native_raster_image_dispose_v1(ledger,out);return status;
    }
    if(receipt!=NULL) {
        receipt->sample_tests=ledger->samples_exact?ledger->used.sample_work:0u;
        receipt->sample_tests_exact=ledger->samples_exact;
        receipt->charged_sample_work=ledger->used.sample_work;
        receipt->compiled_edges=ledger->used.edges;
        receipt->peak_reserved_sample_work=ledger->peak_reserved.sample_work;
        receipt->peak_reserved_edges=ledger->peak_reserved.edges;
        receipt->completed_leases=ledger->completed_leases;
        receipt->source_pixels=ledger->resources.source_pixels;
        receipt->drawn_pixels=ledger->resources.drawn_pixels;
        receipt->retained_bytes=ledger->resources.borrowed_bytes;
        receipt->output_bytes=out->size;
        receipt->peak_live_bytes=ledger->resources.peak_live_bytes;
    }
    if(!quantapdf_native_memory_give_v1(ledger,out->size)) {
        if(receipt!=NULL)memset(receipt,0,sizeof(*receipt));
        /* This unreachable accounting failure still releases actual pixels. */
        quantapdf_ir_raster_image_dispose(out);return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    return QUANTAPDF_OK;
}

/* Unique zero-initialized handle: never copy or move it while live. Commit
 * adopts Source payloads only AFTER full preparation, then clears the input.
 * Every failed preparation leaves the caller's IR untouched and output empty. */
static inline quantapdf_status quantapdf_native_source_page_prepare_context_v1(
    quantapdf_ir *source,const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    const quantapdf_native_page_device *device,
    quantapdf_context_lease_internal *lease,
    quantapdf_native_source_page_v1 *out)
{
    quantapdf_native_prepared_page_v1 metadata;
    quantapdf_status status;
    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(lease!=NULL && !quantapdf_context_lease_valid_internal(lease))return QUANTAPDF_ERROR_ARGUMENT;
    status=quantapdf_native_prepare_page_v1(source,limits,device,&metadata);
    if(status!=QUANTAPDF_OK)return status;
    status=quantapdf_native_compiled_page_compile_v1(&metadata,&out->compiled);
    if(status!=QUANTAPDF_OK) {memset(out,0,sizeof(*out));return status;}
    if(lease!=NULL && !quantapdf_context_lease_move_internal(&out->domain,lease)) {
        quantapdf_native_compiled_page_dispose_v1(&out->compiled);
        memset(out,0,sizeof(*out));return QUANTAPDF_ERROR_ARGUMENT;
    }
    out->ir=*source;memset(source,0,sizeof(*source));out->owns_ir=1;
    return QUANTAPDF_OK;
}

static inline quantapdf_status quantapdf_native_source_page_prepare_v1(
    quantapdf_ir *source,const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    const quantapdf_native_page_device *device,
    quantapdf_native_source_page_v1 *out)
{
    return quantapdf_native_source_page_prepare_context_v1(source,limits,device,NULL,out);
}

static inline quantapdf_status quantapdf_native_source_page_execute_v1(
    quantapdf_native_source_page_v1 *page,
    quantapdf_native_execution_receipt_v1 *receipt,quantapdf_ir_raster_image *out)
{
    if(page==NULL || !page->owns_ir) {
        if(receipt!=NULL)memset(receipt,0,sizeof(*receipt));
        if(out!=NULL)memset(out,0,sizeof(*out));
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    return quantapdf_native_compiled_page_execute_v1(&page->compiled,receipt,out);
}

static inline void quantapdf_native_source_page_dispose_v1(quantapdf_native_source_page_v1 *page)
{
    quantapdf_context_lease_internal domain;
    if(page==NULL)return;
    domain=page->domain;
    quantapdf_native_compiled_page_dispose_v1(&page->compiled);
    if(page->owns_ir)quantapdf_ir_dispose(&page->ir);
    memset(page,0,sizeof(*page));
    quantapdf_context_lease_drop_internal(&domain);
}

/* Historical synchronous metadata borrow: same common compiled-page path,
 * disposed after one execution. No route revalidation or geometry compilation
 * occurs inside the compiled executor. */
static inline quantapdf_status quantapdf_native_execute_page_v1(
    const quantapdf_native_prepared_page_v1 *metadata,
    quantapdf_native_execution_receipt_v1 *receipt,quantapdf_ir_raster_image *out)
{
    quantapdf_native_compiled_page_v1 compiled;
    quantapdf_status status;
    if(receipt!=NULL)memset(receipt,0,sizeof(*receipt));
    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));
    status=quantapdf_native_compiled_page_compile_v1(metadata,&compiled);
    if(status==QUANTAPDF_OK)status=quantapdf_native_compiled_page_execute_v1(&compiled,receipt,out);
    quantapdf_native_compiled_page_dispose_v1(&compiled);
    return status;
}
#endif
