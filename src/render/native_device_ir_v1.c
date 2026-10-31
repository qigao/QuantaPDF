#include "render/native_device_ir_v1.h"
#include "render/native_device_plan.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define QP_DPI_MAX_OPS 4u
/* Source/V5 independently admit only the exact canonical 10-command
 * pair of nested rectangles. Other profiles retain their old shapes
 * and hard nine-command source limits. */
#define QP_DPI_MAX_PATH_COMMANDS 10u

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
    int clipped_paths_optin,int clip_states_optin,int compound_clips_optin,
    int unclipped_prefix_optin,int sibling_clips_optin,
    int original_three_branch_optin,quantapdf_native_work_ledger_v1 *ledger)
{
    const int two_states=clip_states_optin || sibling_clips_optin;
    quantapdf_native_device_plan plan={0};
    quantapdf_ir projected={0};
    quantapdf_composer_page_state page={0};
    quantapdf_composer_operation operations[QP_DPI_MAX_OPS]={{0}};
    quantapdf_composer_path_command paths[QP_DPI_MAX_OPS]
        [QP_DPI_MAX_PATH_COMMANDS]={{{0}}};
    /* One fixed-stack device projection; legacy profiles still use
     * their historical 1..3 Clip nodes and smaller edge quotas. */
    quantapdf_composer_clip_state clips[7]={{0}};
    quantapdf_composer_path_command
        clip_commands[4][QP_DPI_MAX_PATH_COMMANDS]={{{0}}};
    quantapdf_composer_clip_id clip_members[3][2]={{0}};
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
    if((dpi!=72u && dpi!=96u && dpi!=144u) ||
       (original_three_branch_optin && dpi!=72u) ||
       source->page_count!=1u ||
       source->operation_count>QP_DPI_MAX_OPS ||
       (original_three_branch_optin && source->operation_count!=3u) ||
       (two_states && source->operation_count<2u) ||
       (source->operation_count!=0u && source->operations==NULL) ||
       (original_three_branch_optin ?
            source->resources.clip_count!=7u :
        sibling_clips_optin ? source->resources.clip_count!=2u :
        two_clip_optin ? source->resources.clip_count!=3u :
                         source->resources.clip_count>1u) ||
       !isfinite(source->pages[0].width_points) ||
       !isfinite(source->pages[0].height_points) ||
       source->pages[0].width_points<1.0f ||
       source->pages[0].height_points<1.0f ||
       source->pages[0].width_points>1000000.0f ||
       source->pages[0].height_points>1000000.0f)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if(unclipped_prefix_optin &&
       (source->operation_count<2u ||
        source->operations[0].graphics_state_id!=0u ||
        (clip_states_optin && !two_clip_optin)))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    /* The sole native V1 triangle Stroke models one DEVICE pixel. At
     * 96/144 DPI its original width-one source Stroke would require a
     * physically wider sampler, which is not qualified. Reject before
     * raster projection; Fill/Image use their established DPI paths. */
    if(dpi!=72u)
        for(size_t i=0u;i<source->operation_count;++i)
            if(source->operations[i].kind==QUANTAPDF_COMPOSER_OPERATION_PATH &&
               source->operations[i].value.path.options.stroke!=0)
                return QUANTAPDF_ERROR_UNSUPPORTED;
    if(original_three_branch_optin &&
       (two_clip_optin || clipped_paths_optin || clip_states_optin ||
        compound_clips_optin || unclipped_prefix_optin ||
        sibling_clips_optin))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if(two_clip_optin || clipped_paths_optin || unclipped_prefix_optin ||
       sibling_clips_optin || original_three_branch_optin) {
        const quantapdf_resource_registry *r=&source->resources;
        const quantapdf_composer_clip_state *root;
        /* Establish the bounded storage shape before indexing any
         * Clip record or member. The V5 consumer separately validates
         * every preserved resource and per-operation GS relationship. */
        if(r->clips==NULL ||
           r->clip_count!=(original_three_branch_optin?7u:
                sibling_clips_optin?2u:two_clip_optin?3u:1u) ||
           r->clip_capacity!=r->clip_count ||
           r->graphics_states==NULL ||
           r->graphics_state_count!=(original_three_branch_optin?3u:
                                       two_states?2u:1u) ||
           r->graphics_state_capacity!=r->graphics_state_count)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if(original_three_branch_optin) {
            if(r->images!=NULL || r->image_count!=0u ||
               r->image_capacity!=0u)
                return QUANTAPDF_ERROR_UNSUPPORTED;
            for(size_t i=0u;i<3u;++i) {
                const quantapdf_composer_clip_state *node=&r->clips[i+4u];
                const quantapdf_composer_graphics_state *gs=
                    &r->graphics_states[i];
                if(node->kind!=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL ||
                   node->commands!=NULL || node->command_count!=0u ||
                   node->members==NULL || node->member_count!=2u ||
                   node->members[0]!=1u || node->members[1]!=i+2u ||
                   gs->clip_id!=i+5u || gs->fill_alpha!=1.0f ||
                   gs->stroke_alpha!=1.0f ||
                   gs->blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL)
                    return QUANTAPDF_ERROR_UNSUPPORTED;
            }
        }
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
            if(!zero ||
               (original_three_branch_optin &&
                (i==0u ? path->command_count!=2u :
                         path->command_count!=5u)) ||
               (clipped_paths_optin && path->command_count<4u) ||
               ((two_states || unclipped_prefix_optin) &&
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
        const size_t leaf_count=original_three_branch_optin?4u:
            (two_clip_optin || sibling_clips_optin)?2u:1u;
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
               (original_three_branch_optin &&
                (old->command_count!=(i==1u?10u:i==0u?5u:6u) ||
                 !qp_device_closed_clip_contours(
                      old->commands,old->command_count))) ||
               ((two_states || unclipped_prefix_optin ||
                 (clipped_paths_optin && compound_clips_optin)) &&
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
            clip_members[0][0]=clips[2].members[0];
            clip_members[0][1]=clips[2].members[1];
            clips[2].members=clip_members[0];
        }
        if(original_three_branch_optin)
            for(size_t i=0u;i<3u;++i) {
                clips[i+4u]=source->resources.clips[i+4u];
                clip_members[i][0]=clips[i+4u].members[0];
                clip_members[i][1]=clips[i+4u].members[1];
                clips[i+4u].members=clip_members[i];
            }
        projected.resources.clips=clips;
    }
    /* Public V10 uses the canonical device path for original IMAGE/Crop
     * viewport intersection. Fractional/off-viewport bounds use the
     * shared C11 4x4 mask. Explicit owned multi-leaf profiles also use
     * that helper at integral bounds to select the applicable leaf or
     * leaves; older integral RAW routes remain unchanged.
     * No 72-DPI bitmap upscale or second pixel producer. */
    if(ledger!=NULL) {
        const quantapdf_native_scoped_profile_v5 profile=original_three_branch_optin?
            QP_SCOPED_THREE_GS:sibling_clips_optin?QP_SCOPED_SIBLINGS:
            unclipped_prefix_optin?QP_SCOPED_PREFIX:
            compound_clips_optin && clipped_paths_optin?QP_SCOPED_COMPOUND_PATHS:
            compound_clips_optin?QP_SCOPED_COMPOUND_STATES:
            clip_states_optin?QP_SCOPED_CLIP_STATES:
            clipped_paths_optin?QP_SCOPED_CLIPPED_PATHS:
            two_clip_optin?QP_SCOPED_TWO_CLIP:QP_SCOPED_DEVICE;
        return quantapdf_ir_raster_scoped_mixed_v5_accounted(
            &projected,0u,limits,(double)source_width*(double)dpi/1152.0,
            (double)source_height*(double)dpi/1152.0,profile,ledger,out_image);
    }
    if(original_three_branch_optin)
        return quantapdf_ir_raster_scoped_mixed_v5_three_gs_branch_stroke_visible_work_device_viewport(
            &projected,0u,limits,
            (double)source_width*(double)dpi/1152.0,
            (double)source_height*(double)dpi/1152.0,
            out_image);
    if(sibling_clips_optin)
        return quantapdf_ir_raster_scoped_mixed_v5_sibling_clips_device_viewport(
            &projected,0u,limits,
            (double)source_width*(double)dpi/1152.0,
            (double)source_height*(double)dpi/1152.0,
            out_image);
    if(unclipped_prefix_optin)
        return quantapdf_ir_raster_scoped_mixed_v5_unclipped_prefix_device_viewport(
            &projected,0u,limits,
            (double)source_width*(double)dpi/1152.0,
            (double)source_height*(double)dpi/1152.0,
            out_image);
    if(compound_clips_optin && clipped_paths_optin)
        return quantapdf_ir_raster_scoped_mixed_v5_compound_clipped_paths_device_viewport(
            &projected,0u,limits,
            (double)source_width*(double)dpi/1152.0,
            (double)source_height*(double)dpi/1152.0,
            out_image);
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
    return qp_native_device_ir_render(source,dpi,limits,out_image,0,0,0,0,0,0,0,NULL);
}

quantapdf_status quantapdf_native_device_ir_render_v1_two_clip(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_native_device_ir_render(source,dpi,limits,out_image,1,0,0,0,0,0,0,NULL);
}

static int qp_device_compound_clip(const quantapdf_ir *source)
{
    int compound=0;
    /* Public V10 alone may opt a SINGLE already owned Clip leaf with
     * two authored closed contours into the SAME C11 sampler. Other
     * one-Clip paths keep their exact old C11 profile. Two-leaf DAGs
     * retain the independent existing intersection contract. */
    if(source!=NULL &&
       source->resources.clip_count==1u &&
       source->resources.clips!=NULL) {
        const quantapdf_composer_clip_state *clip=
            &source->resources.clips[0];
        /* The exact canonical W* nested-rectangle Source leaf
         * is the sole ten-command compound exception. Its complete
         * geometry and original owner are revalidated in V5. */
        if(clip->commands!=NULL &&
           (clip->command_count<=9u ||
            (clip->command_count==10u &&
             clip->fill_rule==QUANTAPDF_COMPOSER_FILL_EVEN_ODD &&
             qp_device_closed_clip_contours(
                 clip->commands,clip->command_count))))
            for(size_t i=1u;i<clip->command_count;++i)
                if(clip->commands[i].kind==
                    QUANTAPDF_COMPOSER_PATH_MOVE_TO) {
                    compound=1;
                    break;
                }
    }
    return compound;
}

quantapdf_status quantapdf_native_device_ir_render_v1_clipped_paths(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    const int two=source!=NULL && source->resources.clip_count==3u;
    const int compound=qp_device_compound_clip(source);
    return qp_native_device_ir_render(
        source,dpi,limits,out_image,two,1,0,compound,0,0,0,NULL);
}

quantapdf_status quantapdf_native_device_ir_render_v1_clip_states(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_native_device_ir_render(source,dpi,limits,out_image,1,0,1,0,0,0,0,NULL);
}

quantapdf_status quantapdf_native_device_ir_render_v1_compound_clip_states(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_native_device_ir_render(source,dpi,limits,out_image,1,0,1,1,0,0,0,NULL);
}

/* Only this entry interprets an initial GS0 run as paint before W.
 * Exact graph and GS capacities are validated before any projection. */
quantapdf_status quantapdf_native_device_ir_render_v1_unclipped_prefix(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    const int two=source!=NULL && source->resources.clip_count==3u;
    const int states=source!=NULL &&
        source->resources.graphics_state_count==2u;
    return qp_native_device_ir_render(source,dpi,limits,out_image,two,0,states,1,1,0,0,NULL);
}

/* Sibling scopes own two independent leaves, never a fabricated root.
 * The earlier graph profiles retain their original storage contracts. */
quantapdf_status quantapdf_native_device_ir_render_v1_sibling_clips(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_native_device_ir_render(source,dpi,limits,out_image,0,0,0,1,0,1,0,NULL);
}

/* PRIVATE original-Source three-GS, seven-Clip entrypoint.
 * One fully admitted immutable graph, device-projected on the same
 * bounded stack, sampled only by the existing C11 V5->V1 raster.
 * The 1px source Stroke remains strictly 72 DPI. */
quantapdf_status quantapdf_native_device_ir_render_v1_three_gs_original(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_native_device_ir_render(source,dpi,limits,out_image,
                                      0,0,0,0,0,0,1,NULL);
}

quantapdf_status quantapdf_native_device_ir_render_v1_accounted(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_native_prepared_route_v1 route,quantapdf_native_work_ledger_v1 *ledger,
    quantapdf_ir_raster_image *out)
{
    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(source==NULL || limits==NULL || ledger==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    switch(route) {
    case QP_NATIVE_PREPARED_LEGACY:
        if(source->pages==NULL || source->page_count==0u)return QUANTAPDF_ERROR_ARGUMENT;
        return quantapdf_ir_raster_scoped_mixed_v5_accounted(source,0u,limits,
            source->pages[0].width_points,source->pages[0].height_points,
            QP_SCOPED_LEGACY,ledger,out);
    case QP_NATIVE_PREPARED_DEVICE:
        return qp_native_device_ir_render(source,dpi,limits,out,0,0,0,0,0,0,0,ledger);
    case QP_NATIVE_PREPARED_TWO_CLIP:
        return qp_native_device_ir_render(source,dpi,limits,out,1,0,0,0,0,0,0,ledger);
    case QP_NATIVE_PREPARED_CLIPPED_PATHS:
        return qp_native_device_ir_render(source,dpi,limits,out,
            source->resources.clip_count==3u,1,0,qp_device_compound_clip(source),0,0,0,ledger);
    case QP_NATIVE_PREPARED_CLIP_STATES:
        return qp_native_device_ir_render(source,dpi,limits,out,1,0,1,1,0,0,0,ledger);
    case QP_NATIVE_PREPARED_PREFIX:
        return qp_native_device_ir_render(source,dpi,limits,out,
            source->resources.clip_count==3u,0,source->resources.graphics_state_count==2u,
            1,1,0,0,ledger);
    case QP_NATIVE_PREPARED_SIBLINGS:
        return qp_native_device_ir_render(source,dpi,limits,out,0,0,0,1,0,1,0,ledger);
    case QP_NATIVE_PREPARED_THREE_GS:
        return qp_native_device_ir_render(source,dpi,limits,out,0,0,0,0,0,0,1,ledger);
    default:return QUANTAPDF_ERROR_UNSUPPORTED;
    }
}
