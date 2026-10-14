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

static quantapdf_status qp_rect_fill_boundary_impl(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image,int left_preview)
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
    work_edges=fill.edges+clip.edges+(left_preview?2u:1u);
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
    if(column>=grid.width) {
        if(left_preview) {
            quantapdf_ir_raster_image_dispose(out_image);
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        return QUANTAPDF_OK;
    }
    x0=(double)column*grid.points_per_device_x;
    x1=((double)column+1.0)*grid.points_per_device_x;
    if(!(x0<effective_right && effective_right<x1)) {
        if(left_preview) {
            quantapdf_ir_raster_image_dispose(out_image);
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        return QUANTAPDF_OK;
    }
    overlap=fmin(x1,effective_right)-fmax(x0,effective_left);
    if(!(overlap>0.0 && overlap<x1-x0)) {
        if(left_preview) {
            quantapdf_ir_raster_image_dispose(out_image);
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        return QUANTAPDF_OK;
    }
    alpha=(unsigned)floor(255.0*overlap/(x1-x0)+0.5);
    if(alpha==0u || alpha>=255u) {
        if(left_preview) {
            quantapdf_ir_raster_image_dispose(out_image);
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        return QUANTAPDF_OK;
    }
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
    if(left_preview) {
        /* A second, explicit-only source-Clip LEFT boundary coverage
         * hypothesis. Its source X comes from the actual admitted W
         * rectangle, not jsPDF IDs or hardcoded device columns.
         * The earlier Fill-right pass above is left untouched. */
        const size_t right_column=column;
        size_t left_column,changed=0u;
        if(effective_left<=0.0 ||
           effective_left>=grid.source_width_points) {
            quantapdf_ir_raster_image_dispose(out_image);
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        left_column=(size_t)floor(
            effective_left/grid.points_per_device_x);
        if(left_column>=right_column || left_column>=grid.width) {
            quantapdf_ir_raster_image_dispose(out_image);
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        x0=(double)left_column*grid.points_per_device_x;
        x1=((double)left_column+1.0)*grid.points_per_device_x;
        if(!(x0<effective_left && effective_left<x1)) {
            quantapdf_ir_raster_image_dispose(out_image);
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        overlap=fmin(x1,effective_right)-fmax(x0,effective_left);
        if(!(overlap>0.0 && overlap<x1-x0)) {
            quantapdf_ir_raster_image_dispose(out_image);
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        alpha=(unsigned)floor(255.0*overlap/(x1-x0)+0.5);
        if(alpha==0u || alpha>=255u) {
            quantapdf_ir_raster_image_dispose(out_image);
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        for(size_t y=0u;y<grid.height;++y) {
            const double row0=(double)y*grid.points_per_device_y;
            const double row1=((double)y+1.0)*grid.points_per_device_y;
            unsigned char *pixel;
            if(!(row0>effective_top && row1<effective_bottom))
                continue; /* no multi-edge/corner phase inference */
            pixel=out_image->pixels+y*out_image->stride+left_column*4u;
            if(pixel[3]==0u ||
               pixel[0]!=(unsigned char)((opt->fill_argb>>16u)&255u) ||
               pixel[1]!=(unsigned char)((opt->fill_argb>>8u)&255u) ||
               pixel[2]!=(unsigned char)(opt->fill_argb&255u)) {
                quantapdf_ir_raster_image_dispose(out_image);
                return QUANTAPDF_ERROR_UNSUPPORTED;
            }
            if(pixel[3]!=(unsigned char)alpha) {
                pixel[3]=(unsigned char)alpha;
                ++changed;
            }
        }
        if(changed==0u) {
            quantapdf_ir_raster_image_dispose(out_image);
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }
    return QUANTAPDF_OK;
}

/* Existing CI-qualified right-Fill trial remains a separate, byte-stable
 * opt-in. No caller automatically selects the new Clip-left hypothesis. */
quantapdf_status quantapdf_ir_raster_rect_fill_right_preview_v0(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_rect_fill_boundary_impl(ir,page_index,geometry,grid_limits,
                                      raster_limits,out_image,0);
}

/* Seventh private A/B: keep the accepted Fill-right preview and add
 * one strictly admitted, source-derived W Clip LEFT interior border. */
quantapdf_status quantapdf_ir_raster_rect_fill_left_clip_preview_v0(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_rect_fill_boundary_impl(ir,page_index,geometry,grid_limits,
                                      raster_limits,out_image,1);
}


/* Eighth, EXPLICIT PRIVATE rectangle Fill/W Clip BOTTOM fringe hypothesis.
 * The first 7 isolated C11 modes are NEVER called implicitly by the
 * default SDK: this mode composes only with previous mode7's exact
 * separately admitted clipped Fill result.
 *
 * A weak PDFium edge on the first device row after a fractional source
 * bottom is modeled as the true source rectangle/pixel Y intersection
 * only when the old 4x4 sampler produced a completely clear pixel and
 * both X sides lie more than one full device pixel away from the red
 * Fill/W Clip corners. No global sampling-grid translation or oracle. */
quantapdf_status quantapdf_ir_raster_rect_fill_bottom_preview_v0(
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
    size_t work_edges,row,changed=0u;
    double y0,y1,overlap,coverage,outer_left,outer_right;
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
    opt=&op->value.path.options;
    if(op->kind!=QUANTAPDF_COMPOSER_OPERATION_PATH ||
       op->page_index!=0u || op->graphics_state_id!=1u ||
       gs->clip_id!=1u ||
       gs->blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL ||
       gs->fill_alpha!=1.0f || gs->stroke_alpha!=1.0f ||
       gs->soft_mask_id!=0u ||
       opt->struct_size<QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_MIN_SIZE ||
       opt->fill!=1 || opt->stroke!=0 ||
       opt->fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO ||
       opt->graphics_state_id!=1u ||
       (opt->fill_argb>>24u)!=255u ||
       w->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
       w->members!=NULL || w->member_count!=0u ||
       w->fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO ||
       !qp_rectangle(op->value.path.commands,
                     op->value.path.command_count,
                     &opt->transform,geometry,&fill) ||
       !qp_rectangle(w->commands,w->command_count,
                     &w->transform,geometry,&clip))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* The actual source overlap must be bounded on all FOUR sides,
     * with the clip left/top and Fill right/bottom each independently
     * owning the effective edge, or this hypothesis refuses. */
    if(!(fill.left<clip.left && clip.left<fill.right &&
         fill.right<clip.right &&
         fill.top<clip.top && clip.top<fill.bottom &&
         fill.bottom<clip.bottom))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    outer_left=clip.left;
    outer_right=fill.right;
    grid_status=quantapdf_native_device_grid_create_v0(
        geometry,grid_limits,&grid);
    if(grid_status==QUANTAPDF_NATIVE_DEVICE_GRID_ARGUMENT)
        return QUANTAPDF_ERROR_ARGUMENT;
    if(grid_status!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
       quantapdf_native_device_grid_check_ir_v0(
           &grid,ir->pages[0].width_points,
           ir->pages[0].height_points)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    /* Existing mode7 already charges +2 analytical passes; this new
     * candidate charges an additional whole source-edge pass BEFORE
     * any new output can be published, with no borrowing/retry. */
    work_edges=fill.edges+clip.edges+3u;
    if(work_edges>grid_limits->max_edges ||
       work_edges>raster_limits->max_edges ||
       quantapdf_native_device_grid_admit_edges_v0(
           &grid,grid_limits,work_edges)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
       work_edges>raster_limits->max_sample_tests/grid.sample_slots)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* ONLY one fractional source-bottom row, no adjacent pixels and no
     * Clip-bottom/corner coupling. If the boundary is exactly on a
     * device row (or outside the grid), explicitly refuse this mode. */
    row=(size_t)floor(fill.bottom/grid.points_per_device_y);
    if(row>=grid.height)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    y0=(double)row*grid.points_per_device_y;
    y1=((double)row+1.0)*grid.points_per_device_y;
    if(!(y0<fill.bottom && fill.bottom<y1 &&
         y0>clip.top && y1<clip.bottom))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    overlap=fmin(y1,fill.bottom)-fmax(y0,clip.top);
    if(!(overlap>0.0 && overlap<y1-y0))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    coverage=overlap/(y1-y0);
    if(!isfinite(coverage) || !(coverage>0.0 && coverage<1.0))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    alpha=(unsigned)floor(255.0*coverage+0.5);
    if(alpha==0u || alpha>=255u)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Same independently owned RGBA as mode7, no second color or source
     * lifetime. Strict failure leaves a transactional zero output. */
    status=quantapdf_ir_raster_rect_fill_left_clip_preview_v0(
        ir,page_index,geometry,grid_limits,raster_limits,out_image);
    if(status!=QUANTAPDF_OK)return status;
    if(out_image->pixels==NULL || out_image->size!=grid.rgba_bytes ||
       out_image->stride!=grid.stride ||
       out_image->width!=grid.width ||
       out_image->height!=grid.height) {
        quantapdf_ir_raster_image_dispose(out_image);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }

    /* Reserve a full device pixel on BOTH X sides: skip left W Clip
     * and right red Fill intersections/corners, including x=150. */
    for(size_t x=0u;x<grid.width;++x) {
        const double x0=(double)x*grid.points_per_device_x;
        const double x1=((double)x+1.0)*grid.points_per_device_x;
        unsigned char *pixel;
        if(!(x0>outer_left+grid.points_per_device_x &&
             x1<outer_right-grid.points_per_device_x))
            continue;
        pixel=out_image->pixels+row*out_image->stride+x*4u;
        /* Only add a faint fringe where the older 4x4 clipped-Fill
         * sampler supplied no color or alpha; never overwrite paint. */
        if(pixel[0]!=0u || pixel[1]!=0u || pixel[2]!=0u ||
           pixel[3]!=0u) {
            quantapdf_ir_raster_image_dispose(out_image);
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        pixel[0]=(unsigned char)((opt->fill_argb>>16u)&255u);
        pixel[1]=(unsigned char)((opt->fill_argb>>8u)&255u);
        pixel[2]=(unsigned char)(opt->fill_argb&255u);
        pixel[3]=(unsigned char)alpha;
        ++changed;
    }
    if(changed==0u) {
        quantapdf_ir_raster_image_dispose(out_image);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    return QUANTAPDF_OK;
}


/* NINTH independent PRIVATE clipped-Fill candidate. A true normal/opaque
 * source Fill rectangle and one nonzero W rectangle define a source
 * intersection; only the device cell containing Fill RIGHT and W Clip TOP
 * may be updated. This is a testable Q8 integer-coverage hypothesis, not
 * an assertion that PDFium always uses this quantization. */
static int qp_rect_q8_device_edge(double source_coord,double step,
                                   int64_t *result)
{
    float device_coord,scaled;
    if(result==NULL || !isfinite(source_coord) ||
       !isfinite(step) || !(step>0.0))
        return 0;
    device_coord=(float)(source_coord/step);
    if(!isfinite(device_coord) ||
       fabsf(device_coord)>8388607.0f)
        return 0;
    scaled=device_coord*256.0f;
    *result=(int64_t)scaled; /* C11 truncation toward zero */
    return 1;
}

quantapdf_status
quantapdf_ir_raster_rect_fill_upper_right_q8_preview_v0(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    quantapdf_native_device_grid grid;
    quantapdf_native_device_grid_status gs_status;
    quantapdf_status status;
    qp_rect_source fill,clip;
    const quantapdf_composer_operation *op;
    const quantapdf_composer_graphics_state *state;
    const quantapdf_composer_clip_state *mask;
    const quantapdf_composer_path_options *options;
    size_t count,col,row,offset;
    int64_t fill_right_q,clip_left_q,clip_top_q,fill_bottom_q;
    int64_t cell_x,cell_y,x0,x1,y0,y1,area_x,area_y;
    unsigned alpha;
    unsigned char *pixel;
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
    state=&ir->resources.graphics_states[0];
    mask=&ir->resources.clips[0];
    options=&op->value.path.options;
    if(op->kind!=QUANTAPDF_COMPOSER_OPERATION_PATH ||
       op->page_index!=0u || op->graphics_state_id!=1u ||
       state->clip_id!=1u ||
       state->blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL ||
       state->fill_alpha!=1.0f || state->stroke_alpha!=1.0f ||
       state->soft_mask_id!=0u ||
       options->struct_size<QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_MIN_SIZE ||
       options->fill!=1 || options->stroke!=0 ||
       options->fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO ||
       options->graphics_state_id!=1u ||
       (options->fill_argb>>24u)!=255u ||
       mask->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
       mask->members!=NULL || mask->member_count!=0u ||
       mask->fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO ||
       !qp_rectangle(op->value.path.commands,
                     op->value.path.command_count,
                     &options->transform,geometry,&fill) ||
       !qp_rectangle(mask->commands,mask->command_count,
                     &mask->transform,geometry,&clip))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if(!(fill.left<clip.left && clip.left<fill.right &&
         fill.right<clip.right && fill.top<clip.top &&
         clip.top<fill.bottom && fill.bottom<clip.bottom))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    gs_status=quantapdf_native_device_grid_create_v0(
        geometry,grid_limits,&grid);
    if(gs_status==QUANTAPDF_NATIVE_DEVICE_GRID_ARGUMENT)
        return QUANTAPDF_ERROR_ARGUMENT;
    if(gs_status!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
       quantapdf_native_device_grid_check_ir_v0(
           &grid,ir->pages[0].width_points,
           ir->pages[0].height_points)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    /* Charge an additional whole rectangle-edge pass BEFORE invoking
     * the previous bounded mode8 or allocating any output. */
    count=fill.edges+clip.edges+4u;
    if(count>grid_limits->max_edges ||
       count>raster_limits->max_edges ||
       quantapdf_native_device_grid_admit_edges_v0(
           &grid,grid_limits,count)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
       count>raster_limits->max_sample_tests/grid.sample_slots)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    col=(size_t)floor(fill.right/grid.points_per_device_x);
    row=(size_t)floor(clip.top/grid.points_per_device_y);
    if(col>=grid.width || row>=grid.height ||
       (uint64_t)col>(uint64_t)(INT64_MAX/256-1) ||
       (uint64_t)row>(uint64_t)(INT64_MAX/256-1))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    cell_x=(int64_t)col*256;
    cell_y=(int64_t)row*256;
    if(!qp_rect_q8_device_edge(fill.right,grid.points_per_device_x,
                               &fill_right_q) ||
       !qp_rect_q8_device_edge(clip.left,grid.points_per_device_x,
                               &clip_left_q) ||
       !qp_rect_q8_device_edge(clip.top,grid.points_per_device_y,
                               &clip_top_q) ||
       !qp_rect_q8_device_edge(fill.bottom,grid.points_per_device_y,
                               &fill_bottom_q))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    /* A single genuine fractional Fill-right AND W-top corner; neither
     * the Clip-left nor the Fill-bottom may touch this pixel. */
    if(!(cell_x<fill_right_q && fill_right_q<cell_x+256 &&
         cell_y<clip_top_q && clip_top_q<cell_y+256 &&
         clip_left_q<cell_x && fill_bottom_q>cell_y+256))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    x0=clip_left_q>cell_x?clip_left_q:cell_x;
    x1=fill_right_q<cell_x+256?fill_right_q:cell_x+256;
    y0=clip_top_q>cell_y?clip_top_q:cell_y;
    y1=fill_bottom_q<cell_y+256?fill_bottom_q:cell_y+256;
    if(!(x1>x0 && y1>y0))return QUANTAPDF_ERROR_UNSUPPORTED;
    area_x=x1-x0;area_y=y1-y0;
    alpha=(unsigned)((area_x*area_y+128)/256);
    if(alpha==0u || alpha>=255u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    status=quantapdf_ir_raster_rect_fill_bottom_preview_v0(
        ir,page_index,geometry,grid_limits,raster_limits,out_image);
    if(status!=QUANTAPDF_OK)return status;
    if(out_image->pixels==NULL || out_image->size!=grid.rgba_bytes ||
       out_image->stride!=grid.stride || out_image->width!=grid.width ||
       out_image->height!=grid.height) {
        quantapdf_ir_raster_image_dispose(out_image);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    offset=row*out_image->stride+col*4u;
    pixel=out_image->pixels+offset;
    if(pixel[3]==0u || pixel[3]==255u ||
       pixel[0]!=(unsigned char)((options->fill_argb>>16u)&255u) ||
       pixel[1]!=(unsigned char)((options->fill_argb>>8u)&255u) ||
       pixel[2]!=(unsigned char)(options->fill_argb&255u) ||
       pixel[3]==(unsigned char)alpha) {
        quantapdf_ir_raster_image_dispose(out_image);
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    pixel[3]=(unsigned char)alpha;
    return QUANTAPDF_OK;
}
