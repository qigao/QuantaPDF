#include "render/ir_rect_fill_right_preview_v0.h"
#include "render/ir_raster.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct qp_rect_source {
    double left,top,right,bottom;
    size_t edges;
} qp_rect_source;

typedef struct qp_point_source {
    double x,y;
} qp_point_source;

static int qp_translation(const quantapdf_affine_transform *t,
                          double *tx,double *ty)
{
    int zero=t->a==0.0f && t->b==0.0f && t->c==0.0f &&
             t->d==0.0f && t->e==0.0f && t->f==0.0f;
    if(zero) {*tx=0.0;*ty=0.0;return 1;}
    if(t->a!=1.0f || t->b!=0.0f || t->c!=0.0f || t->d!=1.0f ||
       !isfinite(t->e) || !isfinite(t->f) ||
       fabs((double)t->e)>1000000.0 || fabs((double)t->f)>1000000.0)
        return 0;
    *tx=(double)t->e;*ty=(double)t->f;
    return 1;
}
static int qp_map(quantapdf_point p,double tx,double ty,
                  const quantapdf_native_device_grid_geometry *geo,
                  qp_point_source *out)
{
    const double x=(double)p.x+tx,y=(double)p.y+ty;
    if(!isfinite(x) || !isfinite(y) ||
       x<0.0 || y<0.0 ||
       x>geo->page_width_points || y>geo->page_height_points)
        return 0;
    out->x=x;out->y=y;
    return 1;
}
static int qp_equals(qp_point_source a,qp_point_source b)
{
    return a.x==b.x && a.y==b.y;
}

/* Admit only one positively wound axis-aligned rectangle: move, right,
 * down, left, up. Accept CLOSE instead of the fourth line, or the two
 * redundant top-edge retraces emitted by some original PDF producers.
 * These extra source edges do NOT imply extra alpha passes.
 * A disjoint/diagonal subpath, self-intersection or extra point rejects. */
static int qp_rectangle(const quantapdf_composer_path_command *commands,
                        size_t count,const quantapdf_affine_transform *tr,
                        const quantapdf_native_device_grid_geometry *geo,
                        qp_rect_source *out)
{
    qp_point_source p[7];
    double tx,ty;
    if(commands==NULL || out==NULL ||
       (count!=5u && count!=7u && count!=8u) ||
       !qp_translation(tr,&tx,&ty) ||
       commands[0].kind!=QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
       commands[1].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
       commands[2].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
       commands[3].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
       !qp_map(commands[0].point1,tx,ty,geo,&p[0]) ||
       !qp_map(commands[1].point1,tx,ty,geo,&p[1]) ||
       !qp_map(commands[2].point1,tx,ty,geo,&p[2]) ||
       !qp_map(commands[3].point1,tx,ty,geo,&p[3]))
        return 0;

    if(!(p[1].x>p[0].x && p[1].y==p[0].y &&
         p[2].x==p[1].x && p[2].y>p[1].y &&
         p[3].x==p[0].x && p[3].y==p[2].y))
        return 0;
    if(count==5u && commands[4].kind==QUANTAPDF_COMPOSER_PATH_CLOSE) {
        out->edges=4u;
    } else {
        if(commands[4].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
           !qp_map(commands[4].point1,tx,ty,geo,&p[4]) ||
           !qp_equals(p[4],p[0]))
            return 0;
        if(count==5u) {
            out->edges=4u;
        } else {
            if(commands[5].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
               commands[6].kind!=QUANTAPDF_COMPOSER_PATH_LINE_TO ||
               !qp_map(commands[5].point1,tx,ty,geo,&p[5]) ||
               !qp_map(commands[6].point1,tx,ty,geo,&p[6]) ||
               !qp_equals(p[5],p[1]) || !qp_equals(p[6],p[0]) ||
               (count==8u &&
                commands[7].kind!=QUANTAPDF_COMPOSER_PATH_CLOSE))
                return 0;
            out->edges=6u;
        }
    }
    out->left=p[0].x;out->top=p[0].y;
    out->right=p[1].x;out->bottom=p[2].y;
    return 1;
}

quantapdf_status quantapdf_ir_raster_rect_fill_right_preview_v0(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    quantapdf_native_device_grid grid;
    quantapdf_native_device_grid_status grid_status;
    qp_rect_source fill,clip;
    const quantapdf_composer_operation *op;
    const quantapdf_composer_graphics_state *gs;
    const quantapdf_composer_clip_state *w;
    const quantapdf_composer_path_options *opt;
    size_t work_edges,column;
    double effective_left,effective_top,effective_right,effective_bottom;
    double x0,x1,overlap;
    unsigned alpha;
    quantapdf_status status;
    if(out_image==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if(ir==NULL || geometry==NULL || grid_limits==NULL ||
       raster_limits==NULL || ir->pages==NULL || ir->operations==NULL ||
       ir->page_count!=1u || page_index!=0u ||
       ir->operation_count!=1u ||
       ir->resources.graphics_state_count!=1u ||
       ir->resources.clip_count!=1u ||
       ir->resources.graphics_states==NULL ||
       ir->resources.clips==NULL)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    op=&ir->operations[0];
    gs=&ir->resources.graphics_states[0];
    w=&ir->resources.clips[0];
    if(op->kind!=QUANTAPDF_COMPOSER_OPERATION_PATH ||
       op->page_index!=0u || op->graphics_state_id!=1u ||
       gs->clip_id!=1u ||
       gs->blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL ||
       gs->fill_alpha!=1.0f || gs->stroke_alpha!=1.0f ||
       gs->soft_mask_id!=0u ||
       w->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
       w->members!=NULL || w->member_count!=0u ||
       w->fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    opt=&op->value.path.options;
    if(opt->struct_size<QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_MIN_SIZE ||
       opt->fill!=1 || opt->stroke!=0 ||
       opt->fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO ||
       opt->graphics_state_id!=1u ||
       (opt->fill_argb>>24u)!=255u ||
       !qp_rectangle(op->value.path.commands,
                     op->value.path.command_count,
                     &opt->transform,geometry,&fill) ||
       !qp_rectangle(w->commands,w->command_count,
                     &w->transform,geometry,&clip))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    /* Genuine overlap: left and upper boundaries from W Clip, right and
     * lower boundaries from Fill. If not, this particular A/B has no
     * authority to interpret the other geometry or alpha ownership. */
    if(!(fill.left<clip.left && clip.left<fill.right &&
         fill.right<clip.right &&
         fill.top<clip.top && clip.top<fill.bottom &&
         fill.bottom<clip.bottom))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    effective_left=clip.left;
    effective_top=clip.top;
    effective_right=fill.right;
    effective_bottom=fill.bottom;
    grid_status=quantapdf_native_device_grid_create_v0(
        geometry,grid_limits,&grid);
    if(grid_status==QUANTAPDF_NATIVE_DEVICE_GRID_ARGUMENT)
        return QUANTAPDF_ERROR_ARGUMENT;
    if(grid_status!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
       quantapdf_native_device_grid_check_ir_v0(
           &grid,ir->pages[0].width_points,
           ir->pages[0].height_points)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    /* The native V0 already accounts for BOTH source rectangles' sampled
     * edges. This variant charges one more analytical boundary-edge pass
     * before ANY output allocation; never silently borrow old quota. */
    work_edges=fill.edges+clip.edges+1u;
    if(work_edges>raster_limits->max_edges ||
       work_edges>grid_limits->max_edges ||
       quantapdf_native_device_grid_admit_edges_v0(
           &grid,grid_limits,work_edges)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
       work_edges>raster_limits->max_sample_tests/grid.sample_slots)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    status=quantapdf_ir_raster_scoped_device_grid_v0(
        ir,page_index,geometry,grid_limits,raster_limits,out_image);
    if(status!=QUANTAPDF_OK)return status;
    if(out_image->pixels==NULL || out_image->size!=grid.rgba_bytes ||
       out_image->stride!=grid.stride || out_image->width!=grid.width ||
       out_image->height!=grid.height) {
        quantapdf_ir_raster_image_dispose(out_image);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    column=(size_t)floor(effective_right/grid.points_per_device_x);
    if(column>=grid.width)return QUANTAPDF_OK;
    x0=(double)column*grid.points_per_device_x;
    x1=((double)column+1.0)*grid.points_per_device_x;
    if(!(x0<effective_right && effective_right<x1))
        return QUANTAPDF_OK;
    overlap=fmin(x1,effective_right)-fmax(x0,effective_left);
    if(!(overlap>0.0 && overlap<x1-x0))
        return QUANTAPDF_OK;
    alpha=(unsigned)floor(255.0*overlap/(x1-x0)+0.5);
    if(alpha==0u || alpha>=255u)
        return QUANTAPDF_OK;
    for(size_t y=0u;y<grid.height;++y) {
        const double y0=(double)y*grid.points_per_device_y;
        const double y1=((double)y+1.0)*grid.points_per_device_y;
        unsigned char *pixel;
        if(!(y0>effective_top && y1<effective_bottom))
            continue; /* no top/bottom Clip/Fill corner coupling */
        pixel=out_image->pixels+y*out_image->stride+4u*column;
        if(pixel[3]==0u || pixel[3]==255u)
            continue; /* not an actual partially covered source pixel */
        /* The only admitted operation is one solid fill with one Clip.
         * If this borrowed baseline layer disagrees, reject rather than
         * invent a second color/compositing ownership policy. */
        if(pixel[0]!=(unsigned char)((opt->fill_argb>>16u)&255u) ||
           pixel[1]!=(unsigned char)((opt->fill_argb>>8u)&255u) ||
           pixel[2]!=(unsigned char)(opt->fill_argb&255u)) {
            quantapdf_ir_raster_image_dispose(out_image);
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        pixel[3]=(unsigned char)alpha;
    }
    return QUANTAPDF_OK;
}
