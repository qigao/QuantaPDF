#include "render/native_device_ir_v1.h"
#include "render/native_device_plan.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define QP_DPI_MAX_OPS 4u
#define QP_DPI_MAX_PATH_COMMANDS 9u

static int qp_device_float(float source, double scale, float *out)
{
    const double value=(double)source*scale;
    if(!isfinite(source) || !isfinite(value) ||
       value< -1000000.0 || value>1000000.0)
        return 0;
    *out=(float)value;
    return isfinite(*out);
}

static int qp_device_point(quantapdf_point *point,double scale)
{
    float x,y;
    if(!qp_device_float(point->x,scale,&x) ||
       !qp_device_float(point->y,scale,&y))
        return 0;
    point->x=x;point->y=y;
    return 1;
}

static int qp_device_commands(
    quantapdf_composer_path_command *dst,
    const quantapdf_composer_path_command *src,
    size_t count,double scale,int allow_cubic)
{
    if(src==NULL || count==0u || count>QP_DPI_MAX_PATH_COMMANDS)
        return 0;
    for(size_t i=0u;i<count;++i) {
        dst[i]=src[i];
        switch(dst[i].kind) {
        case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
        case QUANTAPDF_COMPOSER_PATH_LINE_TO:
            if(!qp_device_point(&dst[i].point1,scale))return 0;
            break;
        case QUANTAPDF_COMPOSER_PATH_CUBIC_TO:
            /* Source-admitted Fill and Clip commands project all three
             * points with the same bounded finite device policy. */
            if(!allow_cubic ||
               !qp_device_point(&dst[i].point1,scale) ||
               !qp_device_point(&dst[i].point2,scale) ||
               !qp_device_point(&dst[i].point3,scale))
                return 0;
            break;
        case QUANTAPDF_COMPOSER_PATH_CLOSE:
            break;
        default:
            /* All unqualified PATH operators still fail closed. */
            return 0;
        }
    }
    return 1;
}

static int qp_device_closed_contour(
    const quantapdf_composer_path_command *commands,size_t count)
{
    if(commands==NULL || count<4u || count>QP_DPI_MAX_PATH_COMMANDS ||
       commands[0].kind!=QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
       commands[count-1u].kind!=QUANTAPDF_COMPOSER_PATH_CLOSE)
        return 0;
    for(size_t i=1u;i+1u<count;++i)
        if(commands[i].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO &&
           commands[i].kind!=QUANTAPDF_COMPOSER_PATH_CUBIC_TO)
            return 0;
    return 1;
}

/* Compound Clip opt-in only. Every contour keeps the original minimum
 * MOVE + two LINE/CUBIC commands + CLOSE, within one nine-command leaf.
 * Painted PATHs and the older two-GS entry retain the helper above. */
static int qp_device_closed_clip_contours(
    const quantapdf_composer_path_command *commands,size_t count)
{
    size_t contours=0u,segments=0u;
    int active=0;
    if(commands==NULL || count<4u || count>QP_DPI_MAX_PATH_COMMANDS)
        return 0;
    for(size_t i=0u;i<count;++i) {
        switch(commands[i].kind) {
        case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
            if(active || contours==2u)return 0;
            active=1;segments=0u;
            break;
        case QUANTAPDF_COMPOSER_PATH_LINE_TO:
        case QUANTAPDF_COMPOSER_PATH_CUBIC_TO:
            if(!active)return 0;
            ++segments;
            break;
        case QUANTAPDF_COMPOSER_PATH_CLOSE:
            if(!active || segments<2u)return 0;
            active=0;++contours;
            break;
        default:return 0;
        }
    }
    return !active && contours!=0u;
}

static int qp_image_bounds(
    quantapdf_rect *rect,double scale,size_t width,size_t height)
{
    float x0,x1,y0,y1;
    if(!qp_device_float(rect->x0,scale,&x0) ||
       !qp_device_float(rect->x1,scale,&x1) ||
       !qp_device_float(rect->y0,scale,&y0) ||
       !qp_device_float(rect->y1,scale,&y1) ||
       x1<=x0 || y1<=y0 ||
       width==0u || height==0u)
        return 0;
    /* Full authored IMAGE placement may cross the actual CropBox
     * viewport. Source V10 bounded it; C11 V5 device sampler clips
     * it at the SAME 4x4 geometric grid, without changing colors. */
    /* Fractional 96-DPI Image footprints are retained exactly and
     * charged by their enclosing device-pixel box in the V5 renderer.
     * Original integer Source geometry and lifetime are unchanged. */
    rect->x0=x0;rect->y0=y0;rect->x1=x1;rect->y1=y1;
    return 1;
}

static quantapdf_status qp_native_device_ir_render(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image,int two_clip_optin,
    int clipped_paths_optin,int clip_states_optin,int compound_clips_optin)
{
    quantapdf_native_device_plan plan={0};
    quantapdf_ir projected={0};
    quantapdf_composer_page_state page={0};
    quantapdf_composer_operation operations[QP_DPI_MAX_OPS]={{0}};
    quantapdf_composer_path_command paths[QP_DPI_MAX_OPS]
        [QP_DPI_MAX_PATH_COMMANDS]={{{0}}};
    quantapdf_composer_clip_state clips[3]={{0}};
    quantapdf_composer_path_command
        clip_commands[2][QP_DPI_MAX_PATH_COMMANDS]={{{0}}};
    quantapdf_composer_clip_id clip_members[2]={0u,0u};
    char box[80];
    size_t source_width,source_height;
    int printed;
    double scale;
    quantapdf_status status;

    if(out_image==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if(source==NULL || source->pages==NULL || limits==NULL ||
       limits->max_pixels==0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if((dpi!=72u && dpi!=96u && dpi!=144u) || source->page_count!=1u ||
       source->operation_count>QP_DPI_MAX_OPS ||
       (clip_states_optin && source->operation_count<2u) ||
       (source->operation_count!=0u && source->operations==NULL) ||
       (two_clip_optin ? source->resources.clip_count!=3u :
                        source->resources.clip_count>1u) ||
       !isfinite(source->pages[0].width_points) ||
       !isfinite(source->pages[0].height_points) ||
       source->pages[0].width_points<1.0f ||
       source->pages[0].height_points<1.0f ||
       source->pages[0].width_points>1000000.0f ||
       source->pages[0].height_points>1000000.0f)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if(two_clip_optin || clipped_paths_optin) {
        const quantapdf_resource_registry *r=&source->resources;
        const quantapdf_composer_clip_state *root;
        /* Establish the bounded storage shape before indexing any
         * Clip record or member. The V5 consumer separately validates
         * every preserved resource and per-operation GS relationship. */
        if(r->clips==NULL || r->clip_count!=(two_clip_optin?3u:1u) ||
           r->clip_capacity!=r->clip_count ||
           r->graphics_states==NULL ||
           r->graphics_state_count!=(clip_states_optin?2u:1u) ||
           r->graphics_state_capacity!=r->graphics_state_count)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if(two_clip_optin) {
            root=&r->clips[2];
            if(root->kind!=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL ||
               root->commands!=NULL || root->command_count!=0u ||
               root->members==NULL || root->member_count!=2u ||
               !((root->members[0]==1u && root->members[1]==2u) ||
                 (root->members[0]==2u && root->members[1]==1u)))
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }
    /* QPDF admitted original signed quarter-point spans; recover
     * exact quarters from the binary-representable projected local
     * Page extent and pass decimal quarters to the existing C11
     * quotient/remainder device geometry planner. */
    {
        /* Original quarter-point box multiplied by original
         * quarter-point /UserUnit has up to a SIXTEENTH-point physical
         * span. These values are exact in binary32 under the source
         * and physical pixel caps; no binary64->text recovery. */
        const double qw=(double)source->pages[0].width_points*16.0;
        const double qh=(double)source->pages[0].height_points*16.0;
        if(!isfinite(qw) || !isfinite(qh) || qw<=0.0 || qh<=0.0 ||
           qw>16000000.0 || qh>16000000.0 ||
           floor(qw)!=qw || floor(qh)!=qh)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        source_width=(size_t)qw;
        source_height=(size_t)qh;
    }
    printed=snprintf(box,sizeof(box),
        "0 0 %zu.%04u %zu.%04u",
        source_width/16u,(unsigned)(source_width%16u)*625u,
        source_height/16u,(unsigned)(source_height%16u)*625u);
    if(printed<0 || (size_t)printed>=sizeof(box))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    status=quantapdf_native_device_plan_create(
        box,box,"1",0,dpi,limits->max_pixels,&plan);
    if(status!=QUANTAPDF_OK)return status;
    if(plan.width==0u || plan.height==0u ||
       plan.width>SIZE_MAX/plan.height ||
       plan.width*plan.height>limits->max_pixels)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* A shallow projected IR borrows image resources and every untouched
     * registry pointer. Stack-owned command projections cannot be freed
     * as an IR: the original Source owner alone disposes all payloads. */
    projected=*source;
    page=source->pages[0];
    page.width_points=(float)plan.width;
    page.height_points=(float)plan.height;
    projected.pages=&page;
    projected.operations=source->operation_count!=0u?operations:NULL;
    scale=(double)dpi/72.0;
    for(size_t i=0u;i<source->operation_count;++i) {
        const quantapdf_composer_operation *original=&source->operations[i];
        operations[i]=*original;
        if(clipped_paths_optin &&
           original->kind!=QUANTAPDF_COMPOSER_OPERATION_PATH)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if(original->kind==QUANTAPDF_COMPOSER_OPERATION_PATH) {
            const quantapdf_composer_path_operation *path=&original->value.path;
            const quantapdf_affine_transform *t=&path->options.transform;
            const int zero=(t->a==0.0f && t->b==0.0f &&
                t->c==0.0f && t->d==0.0f &&
                t->e==0.0f && t->f==0.0f);
            if(!zero || (clipped_paths_optin && path->command_count<4u) ||
               (clip_states_optin &&
                !qp_device_closed_contour(path->commands,path->command_count)) ||
               !qp_device_commands(paths[i],path->commands,
                                             path->command_count,scale,1))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            operations[i].value.path.commands=paths[i];
        } else if(original->kind==QUANTAPDF_COMPOSER_OPERATION_IMAGE) {
            if(!qp_image_bounds(&operations[i].bounds,scale,
                                plan.width,plan.height))
                return QUANTAPDF_ERROR_UNSUPPORTED;
        } else {
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }
    if(source->resources.clip_count!=0u) {
        const size_t leaf_count=two_clip_optin?2u:1u;
        if(source->resources.clips==NULL)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        for(size_t i=0u;i<leaf_count;++i) {
            const quantapdf_composer_clip_state *old=&source->resources.clips[i];
            /* Source has already frozen definition-time CTM in the
             * commands. Do not scale an unfrozen transform a second
             * time or allow it to escape device-coordinate bounds. */
            if(old->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
               old->transform.a!=1.0f || old->transform.b!=0.0f ||
               old->transform.c!=0.0f || old->transform.d!=1.0f ||
               old->transform.e!=0.0f || old->transform.f!=0.0f ||
               (clipped_paths_optin && old->command_count<4u) ||
               (clip_states_optin &&
                !(compound_clips_optin ?
                    qp_device_closed_clip_contours(old->commands,old->command_count) :
                    qp_device_closed_contour(old->commands,old->command_count))) ||
               !qp_device_commands(clip_commands[i],old->commands,
                                   old->command_count,scale,1))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            clips[i]=*old;
            clips[i].commands=clip_commands[i];
        }
        if(two_clip_optin) {
            clips[2]=source->resources.clips[2];
            clip_members[0]=clips[2].members[0];
            clip_members[1]=clips[2].members[1];
            clips[2].members=clip_members;
        }
        projected.resources.clips=clips;
    }
    /* All public V10 true-DPI pages use the canonical device opt-in
     * for original IMAGE/Crop viewport intersection. Existing 72/144
     * integer placements still use the identical V5 RAW layer; only
     * off-viewport or fractional bounds require the C11 4x4 mask.
     * No 72-DPI bitmap upscale or second pixel producer. */
    if(compound_clips_optin)
        return quantapdf_ir_raster_scoped_mixed_v5_compound_clip_states_device_viewport(
            &projected,0u,limits,
            (double)source_width*(double)dpi/1152.0,
            (double)source_height*(double)dpi/1152.0,
            out_image);
    if(clip_states_optin)
        return quantapdf_ir_raster_scoped_mixed_v5_clip_states_device_viewport(
            &projected,0u,limits,
            (double)source_width*(double)dpi/1152.0,
            (double)source_height*(double)dpi/1152.0,
            out_image);
    if(clipped_paths_optin)
        return quantapdf_ir_raster_scoped_mixed_v5_clipped_paths_device_viewport(
            &projected,0u,limits,
            (double)source_width*(double)dpi/1152.0,
            (double)source_height*(double)dpi/1152.0,
            out_image);
    if(two_clip_optin)
        return quantapdf_ir_raster_scoped_mixed_v5_two_clip_device_viewport(
            &projected,0u,limits,
            (double)source_width*(double)dpi/1152.0,
            (double)source_height*(double)dpi/1152.0,
            out_image);
    return quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
        &projected,0u,limits,
        (double)source_width*(double)dpi/1152.0,
        (double)source_height*(double)dpi/1152.0,
        out_image);
}

quantapdf_status quantapdf_native_device_ir_render_v1(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_native_device_ir_render(source,dpi,limits,out_image,0,0,0,0);
}

quantapdf_status quantapdf_native_device_ir_render_v1_two_clip(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_native_device_ir_render(source,dpi,limits,out_image,1,0,0,0);
}

quantapdf_status quantapdf_native_device_ir_render_v1_clipped_paths(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    const int two=source!=NULL && source->resources.clip_count==3u;
    return qp_native_device_ir_render(source,dpi,limits,out_image,two,1,0,0);
}

quantapdf_status quantapdf_native_device_ir_render_v1_clip_states(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_native_device_ir_render(source,dpi,limits,out_image,1,0,1,0);
}

quantapdf_status quantapdf_native_device_ir_render_v1_compound_clip_states(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_native_device_ir_render(source,dpi,limits,out_image,1,0,1,1);
}
