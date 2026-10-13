#include "render/ir_device_polyline_stroke_v0.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    QP_DEVICE_STROKE_MAX_SEGMENTS = 24,
    QP_DEVICE_STROKE_SUBPIXELS = 4
};

typedef struct qp_stroke_point {
    double x, y;
} qp_stroke_point;

typedef struct qp_stroke_segment {
    qp_stroke_point a, b;
    double dx, dy, len_squared;
} qp_stroke_segment;

typedef struct qp_stroke_join {
    qp_stroke_point vertex, outer_a, outer_b, miter;
    int has_miter;
} qp_stroke_join;

static double qp_cross(double ax, double ay, double bx, double by)
{
    return ax * by - ay * bx;
}

static int qp_stroke_point_valid(
    quantapdf_point point,
    const quantapdf_native_device_grid_geometry *geometry)
{
    return isfinite(point.x) && isfinite(point.y) &&
           point.x >= 0.0f && point.y >= 0.0f &&
           (double)point.x <= geometry->page_width_points &&
           (double)point.y <= geometry->page_height_points;
}

static int qp_stroke_transform_identity_or_zero(
    const quantapdf_affine_transform *t)
{
    int zero = t->a == 0.0f && t->b == 0.0f && t->c == 0.0f &&
               t->d == 0.0f && t->e == 0.0f && t->f == 0.0f;
    int identity = t->a == 1.0f && t->b == 0.0f && t->c == 0.0f &&
                   t->d == 1.0f && t->e == 0.0f && t->f == 0.0f;
    return zero || identity;
}

static int qp_stroke_no_resources(const quantapdf_ir *ir)
{
    return ir->resources.graphics_state_count == 0u &&
           ir->resources.clip_count == 0u &&
           ir->resources.image_count == 0u &&
           ir->resources.font_count == 0u &&
           ir->resources.soft_mask_count == 0u &&
           ir->resources.paint_count == 0u &&
           ir->resources.form_count == 0u &&
           ir->link_count == 0u && ir->outline_count == 0u;
}

static int qp_stroke_admit_path(
    const quantapdf_composer_operation *op,
    const quantapdf_native_device_grid_geometry *geometry,
    qp_stroke_segment segments[QP_DEVICE_STROKE_MAX_SEGMENTS],
    size_t *out_count, double *out_radius, double *out_miter_limit,
    uint32_t *out_color)
{
    const quantapdf_composer_path_operation *path = &op->value.path;
    const quantapdf_composer_path_options *opt = &path->options;
    qp_stroke_point current;
    size_t count = 0u;

    if (op->kind != QUANTAPDF_COMPOSER_OPERATION_PATH ||
        op->page_index != 0u || op->graphics_state_id != 0u ||
        opt->struct_size < QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_MIN_SIZE ||
        opt->stroke != 1 || opt->fill != 0 ||
        opt->graphics_state_id != 0u ||
        opt->fill_paint_id != 0u || opt->stroke_paint_id != 0u ||
        (opt->stroke_argb >> 24u) != 255u ||
        opt->line_cap != QUANTAPDF_COMPOSER_LINE_CAP_BUTT ||
        opt->line_join != QUANTAPDF_COMPOSER_LINE_JOIN_MITER ||
        !isfinite(opt->stroke_width) ||
        opt->stroke_width <= 0.0f || opt->stroke_width > 16.0f ||
        !isfinite(opt->miter_limit) ||
        opt->miter_limit < 1.0f || opt->miter_limit > 10.0f ||
        path->dash_count != 0u || path->dash_lengths != NULL ||
        !qp_stroke_transform_identity_or_zero(&opt->transform) ||
        path->commands == NULL || path->command_count < 2u ||
        path->command_count > QP_DEVICE_STROKE_MAX_SEGMENTS + 1u ||
        path->commands[0].kind != QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
        !qp_stroke_point_valid(path->commands[0].point1, geometry))
        return 0;

    current.x = (double)path->commands[0].point1.x;
    current.y = (double)path->commands[0].point1.y;
    for (size_t i = 1u; i < path->command_count; ++i) {
        const quantapdf_composer_path_command *cmd = &path->commands[i];
        qp_stroke_segment segment;
        if (cmd->kind != QUANTAPDF_COMPOSER_PATH_LINE_TO ||
            !qp_stroke_point_valid(cmd->point1, geometry))
            return 0;
        segment.a = current;
        segment.b.x = (double)cmd->point1.x;
        segment.b.y = (double)cmd->point1.y;
        segment.dx = segment.b.x - segment.a.x;
        segment.dy = segment.b.y - segment.a.y;
        segment.len_squared = segment.dx * segment.dx +
                              segment.dy * segment.dy;
        if (!isfinite(segment.len_squared) ||
            segment.len_squared <= 0.0)
            return 0; /* no hidden dot/cap semantics */
        segments[count++] = segment;
        current = segment.b;
    }
    *out_count = count;
    *out_radius = (double)opt->stroke_width * 0.5;
    *out_miter_limit = (double)opt->miter_limit;
    *out_color = opt->stroke_argb;
    return count != 0u;
}

static int qp_in_triangle(qp_stroke_point p,
                          qp_stroke_point a,
                          qp_stroke_point b,
                          qp_stroke_point c)
{
    double ab = qp_cross(b.x-a.x,b.y-a.y,p.x-a.x,p.y-a.y);
    double bc = qp_cross(c.x-b.x,c.y-b.y,p.x-b.x,p.y-b.y);
    double ca = qp_cross(a.x-c.x,a.y-c.y,p.x-c.x,p.y-c.y);
    return (ab >= 0.0 && bc >= 0.0 && ca >= 0.0) ||
           (ab <= 0.0 && bc <= 0.0 && ca <= 0.0);
}

static int qp_stroke_join_build(
    const qp_stroke_segment *prev, const qp_stroke_segment *next,
    double radius, double miter_limit, qp_stroke_join *join)
{
    double p_len = sqrt(prev->len_squared);
    double n_len = sqrt(next->len_squared);
    double ux = prev->dx / p_len, uy = prev->dy / p_len;
    double vx = next->dx / n_len, vy = next->dy / n_len;
    double turn = qp_cross(ux,uy,vx,vy);
    double dot = ux*vx + uy*vy, side, dx,dy,t,dist;
    if (prev->b.x != next->a.x || prev->b.y != next->a.y)
        return 0;
    join->vertex = prev->b;
    /* Straight and exactly reversed segments are represented by the union
     * of their butt rectangles; do not generate a singular miter. */
    if (fabs(turn) <= 1e-12) {
        (void)dot;
        return 1;
    }
    side = turn > 0.0 ? -1.0 : 1.0;
    join->outer_a.x = join->vertex.x - uy * side * radius;
    join->outer_a.y = join->vertex.y + ux * side * radius;
    join->outer_b.x = join->vertex.x - vy * side * radius;
    join->outer_b.y = join->vertex.y + vx * side * radius;

    dx = join->outer_b.x - join->outer_a.x;
    dy = join->outer_b.y - join->outer_a.y;
    t = qp_cross(dx,dy,vx,vy) / turn;
    join->miter.x = join->outer_a.x + t * ux;
    join->miter.y = join->outer_a.y + t * uy;
    dist = hypot(join->miter.x - join->vertex.x,
                 join->miter.y - join->vertex.y);
    if (!isfinite(t) || !isfinite(dist))
        return 0;
    join->has_miter = dist <= radius * miter_limit;
    return 2; /* 2 == a valid noncollinear join */
}

static int qp_in_segment_rect(
    const qp_stroke_segment *s, qp_stroke_point p, double radius)
{
    double qx=p.x-s->a.x,qy=p.y-s->a.y;
    double projection=qx*s->dx+qy*s->dy;
    double cross=qp_cross(qx,qy,s->dx,s->dy);
    return projection >= 0.0 && projection <= s->len_squared &&
           cross * cross <= radius * radius * s->len_squared;
}

static int qp_stroke_hit(
    const qp_stroke_segment *segments, size_t count,
    const qp_stroke_join *joins, const int *join_kinds,
    qp_stroke_point p, double radius)
{
    for (size_t i=0u;i<count;++i)
        if(qp_in_segment_rect(&segments[i],p,radius))
            return 1; /* One source stroke; overlaps are a union, not alpha sums. */
    for (size_t i=0u;i+1u<count;++i) {
        const qp_stroke_join *j=&joins[i];
        if(join_kinds[i]!=2) continue;
        if(qp_in_triangle(p,j->vertex,j->outer_a,j->outer_b))
            return 1; /* bevel wedge, also part of a miter */
        if(j->has_miter &&
           qp_in_triangle(p,j->outer_a,j->miter,j->outer_b))
            return 1;
    }
    return 0;
}

quantapdf_status quantapdf_ir_raster_device_polyline_stroke_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    quantapdf_native_device_grid grid;
    quantapdf_native_device_grid_status gs;
    qp_stroke_segment segments[QP_DEVICE_STROKE_MAX_SEGMENTS];
    qp_stroke_join joins[QP_DEVICE_STROKE_MAX_SEGMENTS] = {{0}};
    int join_kinds[QP_DEVICE_STROKE_MAX_SEGMENTS] = {0};
    double radius,miter_limit;
    uint32_t color;
    size_t count=0u;
    unsigned char *rgba=NULL;
    quantapdf_status status=QUANTAPDF_ERROR_UNSUPPORTED;

    if(out_image==NULL) return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if(ir==NULL || geometry==NULL || grid_limits==NULL ||
       raster_limits==NULL || ir->pages==NULL || ir->operations==NULL ||
       page_index!=0u || ir->page_count!=1u || ir->operation_count!=1u ||
       raster_limits->max_operations<1u || !qp_stroke_no_resources(ir))
        return QUANTAPDF_ERROR_ARGUMENT;
    if(ir->pages[0].suppress_background!=1)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    gs=quantapdf_native_device_grid_create_v0(geometry,grid_limits,&grid);
    if(gs==QUANTAPDF_NATIVE_DEVICE_GRID_ARGUMENT)
        return QUANTAPDF_ERROR_ARGUMENT;
    if(gs!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
       quantapdf_native_device_grid_check_ir_v0(
           &grid,ir->pages[0].width_points,
           ir->pages[0].height_points)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
       grid.pixel_count>raster_limits->max_pixels ||
       !qp_stroke_admit_path(&ir->operations[0],geometry,segments,
           &count,&radius,&miter_limit,&color))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Charge segments + conservative one join test per source edge, avoiding
     * both overflow and an unbounded per-sample join traversal. */
    if(count>grid_limits->max_edges/2u ||
       count>raster_limits->max_edges/2u ||
       quantapdf_native_device_grid_admit_edges_v0(
           &grid,grid_limits,count*2u)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
       count*2u>raster_limits->max_sample_tests/grid.sample_slots)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    for(size_t i=1u;i<count;++i) {
        join_kinds[i-1u]=qp_stroke_join_build(&segments[i-1u],&segments[i],
                                               radius,miter_limit,&joins[i-1u]);
        if(join_kinds[i-1u]==0) return QUANTAPDF_ERROR_UNSUPPORTED;
    }

    rgba=(unsigned char *)malloc(grid.rgba_bytes);
    if(rgba==NULL) return QUANTAPDF_ERROR_NOMEM;
    memset(rgba,0,grid.rgba_bytes);
    for(size_t y=0u;y<grid.height;++y) {
        for(size_t x=0u;x<grid.width;++x) {
            unsigned covered=0u;
            for(unsigned sy=0u;sy<QP_DEVICE_STROKE_SUBPIXELS;++sy) {
                for(unsigned sx=0u;sx<QP_DEVICE_STROKE_SUBPIXELS;++sx) {
                    double source_x=0.0,source_y=0.0;
                    qp_stroke_point p;
                    if(quantapdf_native_device_grid_sample_v0(
                           &grid,x,y,sx,sy,&source_x,&source_y)!=
                       QUANTAPDF_NATIVE_DEVICE_GRID_OK) {
                        status=QUANTAPDF_ERROR_UNSUPPORTED;
                        goto cleanup;
                    }
                    p.x=source_x;p.y=source_y;
                    covered+=(unsigned)qp_stroke_hit(segments,count,joins,
                                            join_kinds,p,radius);
                }
            }
            if(covered!=0u) {
                size_t offset=(y*grid.width+x)*4u;
                rgba[offset]=(unsigned char)((color>>16u)&255u);
                rgba[offset+1u]=(unsigned char)((color>>8u)&255u);
                rgba[offset+2u]=(unsigned char)(color&255u);
                rgba[offset+3u]=(unsigned char)((covered*255u+8u)/16u);
            }
        }
    }
    out_image->pixels=rgba;
    out_image->width=grid.width;
    out_image->height=grid.height;
    out_image->stride=grid.stride;
    out_image->size=grid.rgba_bytes;
    return QUANTAPDF_OK;

cleanup:
    free(rgba);
    return status;
}
