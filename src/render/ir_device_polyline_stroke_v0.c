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

/* Source PDF Path lowering uses local (x,-y) coordinates with a
 * compensating translation to the displayed top-left page. Preserve that
 * translation exactly. Scaling, shear and rotation remain unqualified. */
static int qp_stroke_translation_only(
    const quantapdf_affine_transform *t, double *tx, double *ty)
{
    int zero = t->a == 0.0f && t->b == 0.0f && t->c == 0.0f &&
               t->d == 0.0f && t->e == 0.0f && t->f == 0.0f;
    if (zero) {
        *tx = 0.0;
        *ty = 0.0;
        return 1;
    }
    if (t->a != 1.0f || t->b != 0.0f || t->c != 0.0f ||
        t->d != 1.0f || !isfinite(t->e) || !isfinite(t->f) ||
        fabs((double)t->e) > 1000000.0 ||
        fabs((double)t->f) > 1000000.0)
        return 0;
    *tx = (double)t->e;
    *ty = (double)t->f;
    return 1;
}

static int qp_stroke_map_source_point(
    quantapdf_point point,
    double tx, double ty,
    const quantapdf_native_device_grid_geometry *geometry,
    qp_stroke_point *out)
{
    if (!isfinite(point.x) || !isfinite(point.y) ||
        fabs((double)point.x) > 1000000.0 ||
        fabs((double)point.y) > 1000000.0)
        return 0;
    out->x = (double)point.x + tx;
    out->y = (double)point.y + ty;
    return isfinite(out->x) && isfinite(out->y) &&
           out->x >= 0.0 && out->y >= 0.0 &&
           out->x <= geometry->page_width_points &&
           out->y <= geometry->page_height_points;
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
    double tx = 0.0, ty = 0.0;

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
        path->commands == NULL || path->command_count < 2u ||
        path->command_count > QP_DEVICE_STROKE_MAX_SEGMENTS + 1u ||
        path->commands[0].kind != QUANTAPDF_COMPOSER_PATH_MOVE_TO)
        return 0;
    if (!qp_stroke_translation_only(&opt->transform, &tx, &ty) ||
        !qp_stroke_map_source_point(path->commands[0].point1,
                                    tx, ty, geometry, &current))
        return 0;
    for (size_t i = 1u; i < path->command_count; ++i) {
        const quantapdf_composer_path_command *cmd = &path->commands[i];
        qp_stroke_segment segment;
        if (cmd->kind != QUANTAPDF_COMPOSER_PATH_LINE_TO)
            return 0;
        segment.a = current;
        if (!qp_stroke_map_source_point(cmd->point1, tx, ty,
                                        geometry, &segment.b))
            return 0;
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

/* **EXPERIMENTAL PRIVATE PREVIEW**: PDFium original-black source Stroke has
 * full opaque horizontal scan rows at 72/96dpi that the unmodified C11
 * area sampler splits into two partial rows. Test a narrowly restricted
 * alternative: horizontal 1pt source strips projected to <2 device rows
 * fully cover intersected rows ONLY when at least TWO overlapping source
 * segments cover the SAME interior device pixel. The original jsPDF top
 * line is traced three times; its bottom line only once. Vertical,
 * diagonal, joins, endpoints, wider strokes and >=2px rows use the
 * unchanged 4x4 sampler. This is a test-only hypothesis, NOT a PDF
 * specification rule or default native admission. */
/* Two mutually exclusive preview hypotheses, both restricted to repeated
 * same-Y source horizontal lines. The original private V0 and the first
 * repeated-interior candidate remain unchanged. Endpoint A/B uses pixel
 * strip *intersection* at the source endpoint; only an explicit second
 * preview opts in. This is not PDF cap/join semantics or a general fix. */
static int qp_horizontal_thin_preview_hit(
    const qp_stroke_segment *segments, size_t count,
    const quantapdf_native_device_grid *grid,
    size_t x, size_t y, double radius, int include_endpoint_pixels)
{
    const double point_y_per_pixel=grid->points_per_device_y;
    const double point_x_per_pixel=grid->points_per_device_x;
    const double source_x=((double)x+0.5)*point_x_per_pixel;
    const double pixel_left=(double)x*point_x_per_pixel;
    const double pixel_right=((double)x+1.0)*point_x_per_pixel;
    const double top=(double)y*point_y_per_pixel;
    const double bottom=((double)y+1.0)*point_y_per_pixel;
    size_t overlap_count=0u;
    double matching_source_y=0.0;
    double first_left=0.0, first_right=0.0;
    int matching_y_seen=0;
    if(radius!=0.5 || point_y_per_pixel<=0.5)
        return 0;
    for(size_t i=0u;i<count;++i) {
        const qp_stroke_segment *segment=&segments[i];
        double left,right;
        if(segment->dy!=0.0 || segment->dx==0.0)
            continue;
        left=fmin(segment->a.x,segment->b.x);
        right=fmax(segment->a.x,segment->b.x);
        if(include_endpoint_pixels) {
            /* A true overlapping source horizontal strip must occupy
             * some of the pixel; do not extend Butt Caps by a whole pixel. */
            if(pixel_right<=left || pixel_left>=right)
                continue;
        } else {
            /* Preserve #284's exact interior-only behavior. */
            if(source_x<=left+radius || source_x>=right-radius)
                continue;
        }
        if(bottom<=segment->a.y-radius ||
           top>=segment->a.y+radius)
            continue;
        /* Mode 1 remains the already qualified #284 repeated-interior
         * algorithm; do not silently alter its supported behavior. */
        if(!include_endpoint_pixels) {
            ++overlap_count;
            if(overlap_count>=2u) return 1;
            continue;
        }
        /* The NEW endpoint-inclusive trial must not count unrelated
         * parallel lines or non-overlapping spans inside one pixel. */
        if(!matching_y_seen) {
            matching_source_y=segment->a.y;
            first_left=left;
            first_right=right;
            matching_y_seen=1;
            ++overlap_count;
        } else if(segment->a.y==matching_source_y &&
                  fmax(first_left,left)<fmin(first_right,right)) {
            /* Two source segments must overlap geometrically, not just
             * occupy different tiny parts of the same device pixel. */
            ++overlap_count;
        }
        if(overlap_count>=2u)
            return 1;
    }
    return 0;
}

/* Third EXPERIMENTAL, opt-in corner trial. The repeated source horizontal
 * Stroke is not a generic filled rectangle: where a right-going segment
 * turns downward into a vertical segment, its outer upper-right pixel
 * can have only partial source coverage. Detect this from actual admitted
 * path topology, NEVER PDF source IDs, literal device pixel coordinates
 * or a PDFium call. The first two preview modes are unchanged. */
static size_t qp_repeated_right_down_corner_outer(
    const qp_stroke_segment *segments, size_t count,
    const quantapdf_native_device_grid *grid,
    size_t x, size_t y, double radius)
{
    const double x0=(double)x*grid->points_per_device_x;
    const double x1=((double)x+1.0)*grid->points_per_device_x;
    const double y0=(double)y*grid->points_per_device_y;
    const double yc=((double)y+0.5)*grid->points_per_device_y;
    const double y1=((double)y+1.0)*grid->points_per_device_y;
    if(radius!=0.5 || grid->points_per_device_y<=0.5)
        return 0u;
    for(size_t i=0u;i+1u<count;++i) {
        const qp_stroke_segment *h=&segments[i];
        const qp_stroke_segment *v=&segments[i+1u];
        size_t repeats=0u;
        if(h->dy!=0.0 || h->dx<=0.0 ||
           v->dx!=0.0 || v->dy<=0.0 ||
           h->b.x!=v->a.x || h->b.y!=v->a.y)
            continue;
        if(!(x0<h->b.x && x1>h->b.x) ||
           !(yc<h->b.y && y1>h->b.y-radius && y0<h->b.y))
            continue;
        for(size_t j=0u;j<count;++j) {
            const qp_stroke_segment *q=&segments[j];
            const double left=fmin(q->a.x,q->b.x);
            const double right=fmax(q->a.x,q->b.x);
            if(q->dy==0.0 && q->dx!=0.0 && q->a.y==h->a.y &&
               left<h->b.x && right>=h->b.x &&
               x0<right && x1>left)
                ++repeats;
        }
        if(repeats>=2u)return repeats;
    }
    return 0u;
}

/* Repeated transparent-to-opaque source-over is ONLY a bounded diagnostic
 * at the narrow orthogonal corner above. No floating pow() or threshold:
 * integer arithmetic stays deterministic on Windows/macOS/Linux. */
static unsigned qp_repeated_alpha(unsigned single_pass_alpha,size_t repeats)
{
    unsigned remains=255u;
    if(single_pass_alpha>=255u)return 255u;
    for(size_t i=0u;i<repeats;++i)
        remains=(remains*(255u-single_pass_alpha)+127u)/255u;
    return 255u-remains;
}



/* EIGHTH PRIVATE source-edge Q8 A/B. Quantize each source stroke strip
 * boundary in DEVICE coordinates to 1/256 pixel BEFORE evaluating the
 * covered interval. Float then truncation mirrors a falsifiable device-edge
 * hypothesis; this is not a claim about the pinned PDFium binary's backend.
 * Coordinate/range limits prevent undefined float-to-integer conversion. */
static int qp_q8_device_strip_alpha(
    double source_left, double source_right, double points_per_device,
    size_t device_index, unsigned *out_alpha)
{
    float left_device,right_device,left_scaled,right_scaled;
    int64_t left_q,right_q,cell0,cell1,hit0,hit1,amount;
    if(out_alpha==NULL || !isfinite(source_left) ||
       !isfinite(source_right) || !(source_right>source_left) ||
       !isfinite(points_per_device) || !(points_per_device>0.0) ||
       (uint64_t)device_index>(uint64_t)(INT64_MAX/256-1))
        return 0;
    left_device=(float)(source_left/points_per_device);
    right_device=(float)(source_right/points_per_device);
    if(!isfinite(left_device) || !isfinite(right_device) ||
       fabsf(left_device)>8388607.0f ||
       fabsf(right_device)>8388607.0f)
        return 0;
    left_scaled=left_device*256.0f;
    right_scaled=right_device*256.0f;
    /* Truncation toward zero is intentional; negative points are not
     * silently floored or wrapped. */
    left_q=(int64_t)left_scaled;
    right_q=(int64_t)right_scaled;
    if(right_q<left_q)
        return 0;
    cell0=(int64_t)device_index*256;
    cell1=cell0+256;
    hit0=left_q>cell0?left_q:cell0;
    hit1=right_q<cell1?right_q:cell1;
    amount=hit1>hit0?hit1-hit0:0;
    *out_alpha=(unsigned)(amount>=255?255:amount);
    return 1;
}


/* NINTH ISOLATED PRIVATE A/B: a 90-degree inner-miter BEVEL is not
 * identical to the Boolean union of its two stroke rectangles. On a
 * Q8 raster grid an additional half-square, bounded to the inner
 * corner, contributes signed coverage; the outer miter contributes
 * its full quadrant. This is source-topology/geometry only, never a
 * PDFium lookup, coordinate fixture, or public renderer admission.
 *
 * Scope: 1pt opaque butt/miter, orthogonal adjacent segments, exact
 * shared endpoint, sufficient source length and no THIRD expanded edge
 * touching this device pixel. Repeated/diagonal/ambiguous joins retain
 * the independently qualified eighth preview byte-for-byte. */
typedef struct qp_join_q8_rect {
    int64_t x0,y0,x1,y1;
} qp_join_q8_rect;

typedef struct qp_join_q8_point {
    double x,y;
} qp_join_q8_point;

static int qp_join_q8_coord(
    double source, double points_per_device, int64_t *out)
{
    float device;
    if(out==NULL || !isfinite(source) ||
       !isfinite(points_per_device) || !(points_per_device>0.0))
        return 0;
    device=(float)(source/points_per_device);
    if(!isfinite(device) || fabsf(device)>8388607.0f)
        return 0;
    *out=(int64_t)(device*256.0f); /* explicitly truncate toward zero */
    return 1;
}

static int qp_join_q8_rect_make(
    const quantapdf_native_device_grid *grid,
    double left,double top,double right,double bottom,
    qp_join_q8_rect *out)
{
    if(out==NULL || !(right>left && bottom>top))
        return 0;
    return qp_join_q8_coord(left,grid->points_per_device_x,&out->x0) &&
           qp_join_q8_coord(right,grid->points_per_device_x,&out->x1) &&
           qp_join_q8_coord(top,grid->points_per_device_y,&out->y0) &&
           qp_join_q8_coord(bottom,grid->points_per_device_y,&out->y1) &&
           out->x0<=out->x1 && out->y0<=out->y1;
}

static int64_t qp_join_q8_rect_cell_area(
    qp_join_q8_rect rect,int64_t cell_x,int64_t cell_y)
{
    const int64_t a=rect.x0>cell_x?rect.x0:cell_x;
    const int64_t b=rect.x1<cell_x+256?rect.x1:cell_x+256;
    const int64_t c=rect.y0>cell_y?rect.y0:cell_y;
    const int64_t d=rect.y1<cell_y+256?rect.y1:cell_y+256;
    return b>a && d>c?(b-a)*(d-c):0;
}

static int qp_join_q8_inside(qp_join_q8_point p,unsigned plane)
{
    if(plane==0u)return p.x>=0.0;
    if(plane==1u)return p.x<=256.0;
    if(plane==2u)return p.y>=0.0;
    return p.y<=256.0;
}

static qp_join_q8_point qp_join_q8_intersection(
    qp_join_q8_point a,qp_join_q8_point b,unsigned plane)
{
    qp_join_q8_point p;
    const double bound=(plane==1u || plane==3u)?256.0:0.0;
    const double av=plane<2u?a.x:a.y;
    const double bv=plane<2u?b.x:b.y;
    const double t=(bound-av)/(bv-av); /* called only across plane */
    p.x=a.x+(b.x-a.x)*t;
    p.y=a.y+(b.y-a.y)*t;
    if(plane<2u)p.x=bound;else p.y=bound;
    return p;
}

/* Triangle clipped to exactly one Q8 device cell. Relative coordinates
 * avoid large floating point products; at most seven polygon vertices. */
static double qp_join_q8_triangle_cell_area(
    const qp_join_q8_rect *inside, int inner_x,int inner_y,
    int64_t joint_x,int64_t joint_y,
    int64_t cell_x,int64_t cell_y)
{
    qp_join_q8_point src[12],dst[12];
    int64_t cx=inner_x>0?inside->x1:inside->x0;
    int64_t cy=inner_y>0?inside->y1:inside->y0;
    size_t n=3u;
    src[0]=(qp_join_q8_point){(double)(cx-cell_x),(double)(cy-cell_y)};
    src[1]=(qp_join_q8_point){(double)(joint_x-cell_x),(double)(cy-cell_y)};
    src[2]=(qp_join_q8_point){(double)(cx-cell_x),(double)(joint_y-cell_y)};
    for(unsigned plane=0u;plane<4u;++plane) {
        size_t m=0u;
        qp_join_q8_point prev=src[n-1u];
        int prev_inside=qp_join_q8_inside(prev,plane);
        for(size_t i=0u;i<n;++i) {
            const qp_join_q8_point cur=src[i];
            const int cur_inside=qp_join_q8_inside(cur,plane);
            if(cur_inside!=prev_inside) {
                if(m>=12u)return 0.0;
                dst[m++]=qp_join_q8_intersection(prev,cur,plane);
            }
            if(cur_inside) {
                if(m>=12u)return 0.0;
                dst[m++]=cur;
            }
            prev=cur;prev_inside=cur_inside;
        }
        if(m<3u)return 0.0;
        for(size_t i=0u;i<m;++i)src[i]=dst[i];
        n=m;
    }
    double double_area=0.0;
    for(size_t i=0u;i<n;++i) {
        const size_t next=(i+1u)%n;
        double_area+=src[i].x*src[next].y-src[next].x*src[i].y;
    }
    return fabs(double_area)*0.5;
}

static int qp_q8_isolated_inner_bevel_join_alpha(
    const qp_stroke_segment *segments,size_t count,
    const quantapdf_native_device_grid *grid,
    size_t x,size_t y,double radius,double miter_limit,
    unsigned *out_alpha)
{
    const double px0=(double)x*grid->points_per_device_x;
    const double px1=((double)x+1.0)*grid->points_per_device_x;
    const double py0=(double)y*grid->points_per_device_y;
    const double py1=((double)y+1.0)*grid->points_per_device_y;
    size_t hit=SIZE_MAX;
    int inner_x=0,inner_y=0;
    if(radius!=0.5 || miter_limit<1.414214 ||
       out_alpha==NULL ||
       (uint64_t)x>(uint64_t)(INT64_MAX/256-1) ||
       (uint64_t)y>(uint64_t)(INT64_MAX/256-1))
        return 0;
    for(size_t i=0u;i<count;++i)
        if(segments[i].dx!=0.0 && segments[i].dy!=0.0)
            return 0;
    for(size_t i=0u;i+1u<count;++i) {
        const qp_stroke_segment *a=&segments[i];
        const qp_stroke_segment *b=&segments[i+1u];
        int sx=0,sy=0;
        const int hv=(a->dx!=0.0 && a->dy==0.0 &&
                      b->dx==0.0 && b->dy!=0.0);
        const int vh=(a->dx==0.0 && a->dy!=0.0 &&
                      b->dx!=0.0 && b->dy==0.0);
        if((!hv && !vh) ||
           a->b.x!=b->a.x || a->b.y!=b->a.y ||
           sqrt(a->len_squared)<=radius+2.0*grid->points_per_device_x+
                                          2.0*grid->points_per_device_y ||
           sqrt(b->len_squared)<=radius+2.0*grid->points_per_device_x+
                                          2.0*grid->points_per_device_y)
            continue;
        if(hv) {
            sx=a->dx<0.0?1:-1;
            sy=b->dy<0.0?-1:1;
        } else {
            sx=b->dx<0.0?-1:1;
            sy=a->dy<0.0?1:-1;
        }
        const double jx=a->b.x,jy=a->b.y;
        if(px1<=jx-radius || px0>=jx+radius ||
           py1<=jy-radius || py0>=jy+radius)
            continue;
        /* A third source edge touching the entire cell makes this
         * local analytic replacement ambiguous; retain mode8 instead. */
        int isolated=1;
        for(size_t k=0u;k<count;++k) {
            if(k==i || k==i+1u)continue;
            const qp_stroke_segment *other=&segments[k];
            const double lx=fmin(other->a.x,other->b.x)-radius;
            const double rx=fmax(other->a.x,other->b.x)+radius;
            const double ty=fmin(other->a.y,other->b.y)-radius;
            const double by=fmax(other->a.y,other->b.y)+radius;
            if(px0<rx && px1>lx && py0<by && py1>ty) {
                isolated=0;break;
            }
        }
        if(!isolated)continue;
        if(hit!=SIZE_MAX)return 0;
        hit=i;inner_x=sx;inner_y=sy;
    }
    if(hit==SIZE_MAX)return 0;
    const qp_stroke_segment *a=&segments[hit];
    const qp_stroke_segment *b=&segments[hit+1u];
    const qp_stroke_segment *h=a->dx!=0.0?a:b;
    const qp_stroke_segment *v=a->dy!=0.0?a:b;
    const double jx=a->b.x,jy=a->b.y;
    qp_join_q8_rect rh,rv,outer,inner;
    const double outer_x=jx-(double)inner_x*radius;
    const double outer_y=jy-(double)inner_y*radius;
    if(!qp_join_q8_rect_make(grid,
           fmin(h->a.x,h->b.x),jy-radius,
           fmax(h->a.x,h->b.x),jy+radius,&rh) ||
       !qp_join_q8_rect_make(grid,
           jx-radius,fmin(v->a.y,v->b.y),
           jx+radius,fmax(v->a.y,v->b.y),&rv) ||
       !qp_join_q8_rect_make(grid,
           fmin(jx,outer_x),fmin(jy,outer_y),
           fmax(jx,outer_x),fmax(jy,outer_y),&outer) ||
       !qp_join_q8_rect_make(grid,
           fmin(jx,jx+(double)inner_x*radius),
           fmin(jy,jy+(double)inner_y*radius),
           fmax(jx,jx+(double)inner_x*radius),
           fmax(jy,jy+(double)inner_y*radius),&inner))
        return 0;
    int64_t jqx=0,jqy=0;
    if(!qp_join_q8_coord(jx,grid->points_per_device_x,&jqx) ||
       !qp_join_q8_coord(jy,grid->points_per_device_y,&jqy))
        return 0;
    const int64_t cell_x=(int64_t)x*256;
    const int64_t cell_y=(int64_t)y*256;
    qp_join_q8_rect shared={
        rh.x0>rv.x0?rh.x0:rv.x0,
        rh.y0>rv.y0?rh.y0:rv.y0,
        rh.x1<rv.x1?rh.x1:rv.x1,
        rh.y1<rv.y1?rh.y1:rv.y1
    };
    const int64_t h_area=qp_join_q8_rect_cell_area(rh,cell_x,cell_y);
    const int64_t v_area=qp_join_q8_rect_cell_area(rv,cell_x,cell_y);
    const int64_t both=qp_join_q8_rect_cell_area(shared,cell_x,cell_y);
    const int64_t extra=qp_join_q8_rect_cell_area(outer,cell_x,cell_y);
    const double bevel=qp_join_q8_triangle_cell_area(
        &inner,inner_x,inner_y,jqx,jqy,cell_x,cell_y);
    const double total=(double)(h_area+v_area-both+extra)+bevel;
    if(!isfinite(total) || total<0.0)return 0;
    *out_alpha=(unsigned)(total>=65280.0?255.0:floor(total/256.0));
    return 1;
}

/* Fourth EXPLICIT PRIVATE A/B: for an *isolated*, single-traversal,
 * axis-aligned 1pt vertical Stroke interior, estimate the source strip's
 * exact horizontal intersection with this device pixel. No fixed pixel
 * coordinate, PDFium input, half-pixel offset or legacy replacement.
 * Guard both segment ends by one full device row beyond the source radius;
 * nearby joins, caps, diagonals and overlapping source segments retain
 * the old sampled union. This is not a generic PDF AA specification. */
static int qp_single_vertical_analytic_alpha(
    const qp_stroke_segment *segments, size_t count,
    const quantapdf_native_device_grid *grid,
    size_t x, size_t y, double radius, unsigned *out_alpha,
    int q8_policy)
{
    const double x0=(double)x*grid->points_per_device_x;
    const double x1=((double)x+1.0)*grid->points_per_device_x;
    const double y0=(double)y*grid->points_per_device_y;
    const double y1=((double)y+1.0)*grid->points_per_device_y;
    const double endpoint_guard=radius+grid->points_per_device_y;
    size_t hit=SIZE_MAX;
    double coverage=0.0;

    if(radius!=0.5 || out_alpha==NULL ||
       !(x1>x0 && y1>y0) || !isfinite(endpoint_guard))
        return 0;
    /* Orthogonal-only source topology is deliberate: an acute diagonal
     * miter may project several radii beyond its neighbor's rectangle.
     * Refuse this analytic shortcut for the entire source Stroke if ANY
     * diagonal appears, rather than silently sampling across such a join. */
    for(size_t i=0u;i<count;++i)
        if(segments[i].dx!=0.0 && segments[i].dy!=0.0)
            return 0;
    for(size_t i=0u;i<count;++i) {
        const qp_stroke_segment *v=&segments[i];
        const double bottom=fmin(v->a.y,v->b.y);
        const double top=fmax(v->a.y,v->b.y);
        const double left=v->a.x-radius;
        const double right=v->a.x+radius;
        double common;
        if(v->dx!=0.0 || v->dy==0.0 ||
           !(y0>bottom+endpoint_guard &&
             y1<top-endpoint_guard) ||
           !(x0<right && x1>left))
            continue;
        common=fmin(x1,right)-fmax(x0,left);
        if(!(common>0.0))
            continue;
        if(hit!=SIZE_MAX)
            return 0; /* no multi-pass vertical alpha amplification */
        hit=i;
        coverage=common/(x1-x0);
    }
    if(hit==SIZE_MAX || !isfinite(coverage) ||
       !(coverage>0.0 && coverage<=1.0))
        return 0;
    /* Fail conservative if ANY other source segment's width-expanded
     * bounding rectangle reaches the pixel. This excludes ambiguous
     * joins/crossings without assuming how PDFium handles their union. */
    for(size_t i=0u;i<count;++i) {
        const qp_stroke_segment *s=&segments[i];
        const double ax=fmin(s->a.x,s->b.x)-radius;
        const double bx=fmax(s->a.x,s->b.x)+radius;
        const double ay=fmin(s->a.y,s->b.y)-radius;
        const double by=fmax(s->a.y,s->b.y)+radius;
        if(i!=hit && x0<bx && x1>ax && y0<by && y1>ay)
            return 0;
    }
    if(q8_policy)
        return qp_q8_device_strip_alpha(segments[hit].a.x-radius,
            segments[hit].a.x+radius,grid->points_per_device_x,
            x,out_alpha);
    *out_alpha=(unsigned)floor(fmin(1.0,coverage)*255.0+0.5);
    return 1;
}


/* Fifth, EXPLICIT PRIVATE source-width horizontal analytical A/B.
 * Only a genuinely SINGLE source traversal, strictly horizontal 1pt
 * interior can replace the 4x4 sampled alpha. Connected caps, miters,
 * retraces, diagonals and overlaps keep the already-tested mode4 union.
 * This is a hypothesis, NOT a general PDF/Skia raster contract. */
static int qp_single_horizontal_analytic_alpha(
    const qp_stroke_segment *segments, size_t count,
    const quantapdf_native_device_grid *grid,
    size_t x, size_t y, double radius, unsigned *out_alpha,
    int quantization_policy)
{
    const double x0=(double)x*grid->points_per_device_x;
    const double x1=((double)x+1.0)*grid->points_per_device_x;
    const double y0=(double)y*grid->points_per_device_y;
    const double y1=((double)y+1.0)*grid->points_per_device_y;
    const double guard=radius+grid->points_per_device_x;
    size_t hit=SIZE_MAX;
    double coverage=0.0;
    if(radius!=0.5 || out_alpha==NULL ||
       !(x1>x0 && y1>y0) || !isfinite(guard))
        return 0;
    /* A potentially long miter from any diagonal edge invalidates this
     * entire narrow shortcut; default mode4 remains authoritative. */
    for(size_t i=0u;i<count;++i)
        if(segments[i].dx!=0.0 && segments[i].dy!=0.0)
            return 0;
    for(size_t i=0u;i<count;++i) {
        const qp_stroke_segment *h=&segments[i];
        const double left=fmin(h->a.x,h->b.x);
        const double right=fmax(h->a.x,h->b.x);
        const double top=h->a.y-radius;
        const double bottom=h->a.y+radius;
        double common;
        if(h->dy!=0.0 || h->dx==0.0 ||
           !(x0>left+guard && x1<right-guard) ||
           !(y0<bottom && y1>top))
            continue;
        common=fmin(y1,bottom)-fmax(y0,top);
        if(!(common>0.0))
            continue;
        if(hit!=SIZE_MAX)
            return 0; /* never promote genuine retraces */
        hit=i;
        coverage=common/(y1-y0);
    }
    if(hit==SIZE_MAX || !isfinite(coverage) ||
       !(coverage>0.0 && coverage<=1.0))
        return 0;
    /* Overlap-aware fail closed, conservatively bounding all other
     * segments. Do not construct a second join/cap ownership path. */
    for(size_t i=0u;i<count;++i) {
        const qp_stroke_segment *s=&segments[i];
        const double ax=fmin(s->a.x,s->b.x)-radius;
        const double bx=fmax(s->a.x,s->b.x)+radius;
        const double ay=fmin(s->a.y,s->b.y)-radius;
        const double by=fmax(s->a.y,s->b.y)+radius;
        if(i!=hit && x0<bx && x1>ax && y0<by && y1>ay)
            return 0;
    }
    /* Preserve mode5 nearest-integer quantization byte-for-byte.
     * The sixth PRIVATE Stroke A/B tests upward quantization of the
     * same source-eligible 1pt single-pass horizontal pixel only. */
    const unsigned old_alpha=(unsigned)floor(
        fmin(1.0,coverage)*255.0+0.5);
    if(quantization_policy==3) {
        return qp_q8_device_strip_alpha(
            segments[hit].a.y-radius,segments[hit].a.y+radius,
            grid->points_per_device_y,y,out_alpha);
    } else if(quantization_policy==1) {
        /* Old mode6: literal ceil hypothesis, independently callable
         * even though it may worsen the exact original-PDF result. */
        *out_alpha=(unsigned)ceil(fmin(1.0,coverage)*255.0);
    } else if(quantization_policy==2 &&
              old_alpha>0u && old_alpha<255u &&
              y1<=segments[hit].a.y) {
        /* SEVENTH PRIVATE Stroke A/B: one Alpha LSB ONLY where the
         * WHOLE device pixel row is strictly above the source horizontal
         * segment center. The lower/crossing side, endpoints, joins,
         * repeated edges and verticals keep exact old mode5 bytes.
         * No PDFium data enters this pure C11 geometric hypothesis. */
        *out_alpha=old_alpha+1u;
    } else {
        *out_alpha=old_alpha;
    }
    return 1;
}

/* Bounds-only acceleration for the THIRD experimental corner preview.
 * The source is a finite, bounded OPEN POLYLINE. Every stroked segment
 * lies within its two endpoints expanded by source radius; every admitted
 * miter join lies within radius*miter_limit of a vertex (or bevel
 * fallback within radius). Expand conservatively by another TWO full
 * device pixels in each direction to absorb float/ceil rounding.
 *
 * This rejects NOTHING and DOES NOT change the sampled geometry or
 * resource ceiling. The owned RGBA remains zero outside this rectangle.
 * Modes 0/1/2 continue scanning the exact historical whole image. */
typedef struct qp_stroke_roi {
    size_t x0,x1,y0,y1; /* exclusive upper bounds */
} qp_stroke_roi;

static size_t qp_clamp_device_bound(
    double source_coordinate, double points_per_device,
    size_t extent, int upper)
{
    double pixel=source_coordinate/points_per_device;
    pixel=upper?ceil(pixel)+2.0:floor(pixel)-2.0;
    if(!isfinite(pixel) || pixel<=0.0)return 0u;
    if(pixel>=(double)extent)return extent;
    return (size_t)pixel;
}

static qp_stroke_roi qp_corner_preview_sample_bounds(
    const qp_stroke_segment *segments,size_t count,
    const quantapdf_native_device_grid *grid,
    double radius,double miter_limit)
{
    qp_stroke_roi bounds={0u,grid->width,0u,grid->height};
    double minx,maxx,miny,maxy,fringe;
    if(count==0u)return bounds;
    minx=maxx=segments[0].a.x;
    miny=maxy=segments[0].a.y;
    for(size_t i=0u;i<count;++i) {
        const qp_stroke_segment *s=&segments[i];
        minx=fmin(minx,fmin(s->a.x,s->b.x));
        maxx=fmax(maxx,fmax(s->a.x,s->b.x));
        miny=fmin(miny,fmin(s->a.y,s->b.y));
        maxy=fmax(maxy,fmax(s->a.y,s->b.y));
    }
    fringe=radius*fmax(1.0,miter_limit);
    bounds.x0=qp_clamp_device_bound(
        minx-fringe,grid->points_per_device_x,grid->width,0);
    bounds.x1=qp_clamp_device_bound(
        maxx+fringe,grid->points_per_device_x,grid->width,1);
    bounds.y0=qp_clamp_device_bound(
        miny-fringe,grid->points_per_device_y,grid->height,0);
    bounds.y1=qp_clamp_device_bound(
        maxy+fringe,grid->points_per_device_y,grid->height,1);
    /* Conservatively fall back to full bounds if precision ever fails;
     * never silently omit source pixels due to malformed bounds. */
    if(bounds.x1<=bounds.x0 || bounds.y1<=bounds.y0)
        return (qp_stroke_roi){0u,grid->width,0u,grid->height};
    return bounds;
}

static quantapdf_status qp_stroke_render_impl(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image, int horizontal_preview)
{
    quantapdf_native_device_grid grid;
    quantapdf_native_device_grid_status gs;
    qp_stroke_segment segments[QP_DEVICE_STROKE_MAX_SEGMENTS];
    qp_stroke_join joins[QP_DEVICE_STROKE_MAX_SEGMENTS] = {0};
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

    /* The corner A/B performs extra bounded segment scans per device
     * pixel. Charge an additional entire source-edge pass, only for this
     * explicit experimental mode. Keep modes V0/1/2 byte-equivalent. */
    if(horizontal_preview>=3 &&
       (count>grid_limits->max_edges/3u ||
        count>raster_limits->max_edges/3u ||
        quantapdf_native_device_grid_admit_edges_v0(
            &grid,grid_limits,count*3u)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
        count*3u>raster_limits->max_sample_tests/grid.sample_slots))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* The single-vertical analytical trial does a fourth conservative
     * source-edge scan. Keep the old V0/1/2/3 admission unchanged. */
    if(horizontal_preview>=4 &&
       (count>grid_limits->max_edges/4u ||
        count>raster_limits->max_edges/4u ||
        quantapdf_native_device_grid_admit_edges_v0(
            &grid,grid_limits,count*4u)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
        count*4u>raster_limits->max_sample_tests/grid.sample_slots))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Preserve fifth pass admission for fifth and sixth isolated trials. */
    if(horizontal_preview>=5 &&
       (count>grid_limits->max_edges/5u ||
        count>raster_limits->max_edges/5u ||
        quantapdf_native_device_grid_admit_edges_v0(
            &grid,grid_limits,count*5u)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
        count*5u>raster_limits->max_sample_tests/grid.sample_slots))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Sixth independent source-edge work admission before allocation.
     * Older modes0..5 are not charged this extra pass or changed. */
    if(horizontal_preview==6 &&
       (count>grid_limits->max_edges/6u ||
        count>raster_limits->max_edges/6u ||
        quantapdf_native_device_grid_admit_edges_v0(
            &grid,grid_limits,count*6u)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
        count*6u>raster_limits->max_sample_tests/grid.sample_slots))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Seventh independently callable Stroke preview adds one more
     * bounded source-edge work pass. Do not charge modes0..6 for it. */
    if(horizontal_preview==7 &&
       (count>grid_limits->max_edges/7u ||
        count>raster_limits->max_edges/7u ||
        quantapdf_native_device_grid_admit_edges_v0(
            &grid,grid_limits,count*7u)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
        count*7u>raster_limits->max_sample_tests/grid.sample_slots))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Eighth Q8 source-edge A/B uses a separate bounded pass, charged
     * before allocation. Earlier modes retain exact budgets. */
    if(horizontal_preview==8 &&
       (count>grid_limits->max_edges/8u ||
        count>raster_limits->max_edges/8u ||
        quantapdf_native_device_grid_admit_edges_v0(
            &grid,grid_limits,count*8u)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
        count*8u>raster_limits->max_sample_tests/grid.sample_slots))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Ninth private orthogonal-join analytics performs an additional
     * bounded topology/other-edge scan before any output allocation. */
    if(horizontal_preview==9 &&
       (count>grid_limits->max_edges/9u ||
        count>raster_limits->max_edges/9u ||
        quantapdf_native_device_grid_admit_edges_v0(
            &grid,grid_limits,count*9u)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK ||
        count*9u>raster_limits->max_sample_tests/grid.sample_slots))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    for(size_t i=1u;i<count;++i) {
        join_kinds[i-1u]=qp_stroke_join_build(&segments[i-1u],&segments[i],
                                               radius,miter_limit,&joins[i-1u]);
        if(join_kinds[i-1u]==0) return QUANTAPDF_ERROR_UNSUPPORTED;
    }

    rgba=(unsigned char *)malloc(grid.rgba_bytes);
    if(rgba==NULL) return QUANTAPDF_ERROR_NOMEM;
    memset(rgba,0,grid.rgba_bytes);
    qp_stroke_roi sampled={0u,grid.width,0u,grid.height};
    if(horizontal_preview>=3)
        sampled=qp_corner_preview_sample_bounds(
            segments,count,&grid,radius,miter_limit);
    for(size_t y=sampled.y0;y<sampled.y1;++y) {
        for(size_t x=sampled.x0;x<sampled.x1;++x) {
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
            unsigned pixel_alpha=(covered*255u+8u)/16u;
            if(horizontal_preview && covered<16u &&
               qp_horizontal_thin_preview_hit(
                   segments,count,&grid,x,y,radius,
                   horizontal_preview>=2)) {
                /* Modes 1/2 and every non-corner pixel in mode 3 retain
                 * the exact fully saturated repeated-edge trial. */
                pixel_alpha=255u;
                if(horizontal_preview>=3) {
                    const size_t repeats=qp_repeated_right_down_corner_outer(
                        segments,count,&grid,x,y,radius);
                    if(repeats>=2u && covered!=0u)
                        pixel_alpha=qp_repeated_alpha(
                            (covered*255u+8u)/16u,repeats);
                }
            }
            if(horizontal_preview>=4) {
                unsigned analytic_alpha=0u;
                if(qp_single_vertical_analytic_alpha(
                       segments,count,&grid,x,y,radius,&analytic_alpha,
                       horizontal_preview>=8))
                    pixel_alpha=analytic_alpha;
            }
            if(horizontal_preview>=5) {
                unsigned analytic_alpha=0u;
                if(qp_single_horizontal_analytic_alpha(
                       segments,count,&grid,x,y,radius,&analytic_alpha,
                       horizontal_preview==6?1:
                       (horizontal_preview==7?2:
                        (horizontal_preview>=8?3:0))))
                    pixel_alpha=analytic_alpha;
            }
            if(horizontal_preview==9) {
                unsigned join_alpha=0u;
                if(qp_q8_isolated_inner_bevel_join_alpha(
                       segments,count,&grid,x,y,radius,miter_limit,
                       &join_alpha))
                    pixel_alpha=join_alpha;
            }
            if(pixel_alpha!=0u) {
                size_t offset=(y*grid.width+x)*4u;
                rgba[offset]=(unsigned char)((color>>16u)&255u);
                rgba[offset+1u]=(unsigned char)((color>>8u)&255u);
                rgba[offset+2u]=(unsigned char)(color&255u);
                rgba[offset+3u]=(unsigned char)pixel_alpha;
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

/* Existing private V0 semantics stay unchanged: no implicit stroke
 * alignment adjustment or native-original pixel positive. */
quantapdf_status quantapdf_ir_raster_device_polyline_stroke_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_stroke_render_impl(ir,page_index,geometry,grid_limits,
                                 raster_limits,out_image,0);
}

quantapdf_status quantapdf_ir_raster_device_polyline_stroke_thin_horizontal_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_stroke_render_impl(ir,page_index,geometry,grid_limits,
                                 raster_limits,out_image,1);
}

/* Second experimental A/B: this does NOT silently change the existing
 * private renderer or previously CI-green interior-only preview. */
quantapdf_status quantapdf_ir_raster_device_polyline_stroke_repeated_endpoint_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_stroke_render_impl(ir,page_index,geometry,grid_limits,
                                 raster_limits,out_image,2);
}


/* Right-angle corner A/B is a THIRD explicit private mode, not a change
 * to default native raster, repeated interior or endpoint preview. */
quantapdf_status quantapdf_ir_raster_device_polyline_stroke_right_down_corner_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_stroke_render_impl(ir,page_index,geometry,grid_limits,
                                 raster_limits,out_image,3);
}


/* Fourth opt-in preview retains all mode-3 repeated-horizontal/corner
 * behavior, changing only isolated source-vertical interior coverage.
 * Never invoked by the default renderer or published through the SDK. */
quantapdf_status quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_stroke_render_impl(ir,page_index,geometry,grid_limits,
                                 raster_limits,out_image,4);
}


/* Fifth PRIVATE hypothesis. Mode4 remains independently callable and
 * byte-for-byte unchanged for its previous input geometries. */
quantapdf_status quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_stroke_render_impl(ir,page_index,geometry,grid_limits,
                                 raster_limits,out_image,5);
}


/* SIXTH explicitly called, PRIVATE source-Stroke hypothesis: ceil the
 * same analytically measured single-pass 1pt horizontal interior alpha
 * without ever changing the default renderer or earlier preview modes. */
quantapdf_status
quantapdf_ir_raster_device_polyline_stroke_single_horizontal_ceil_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_stroke_render_impl(ir,page_index,geometry,grid_limits,
                                 raster_limits,out_image,6);
}


/* SEVENTH isolated PRIVATE Stroke hypothesis: upper device-row source
 * coverage LSB, never global ceil or repeated/cap/miter reinterpreting. */
quantapdf_status
quantapdf_ir_raster_device_polyline_stroke_upper_horizontal_lsb_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_stroke_render_impl(ir,page_index,geometry,grid_limits,
                                 raster_limits,out_image,7);
}


/* EIGHTH separately invoked PRIVATE Q8 edge quantization A/B.
 * Uses the same strict single-pass horizontal/vertical source geometry
 * guards; modes0..7 and both earlier quantization experiments remain
 * independently callable and unmodified. */
quantapdf_status
quantapdf_ir_raster_device_polyline_stroke_q8_strip_preview_v0(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_stroke_render_impl(ir,page_index,geometry,grid_limits,
                                 raster_limits,out_image,8);
}


/* NINTH isolated PRIVATE Q8 90-degree miter inner-bevel hypothesis.
 * Earlier 0..8 preview algorithms, budgets and output remain unchanged. */
quantapdf_status
quantapdf_ir_raster_device_polyline_stroke_q8_inner_bevel_join_preview_v0(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_native_device_grid_geometry *geometry,
    const quantapdf_native_device_grid_limits *grid_limits,
    const quantapdf_ir_raster_v1_limits *raster_limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_stroke_render_impl(ir,page_index,geometry,grid_limits,
                                 raster_limits,out_image,9);
}
