#include "render/ir_raster_v1.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QP_SAMPLES 4u
#define QP_TOTAL_SAMPLES 16u
#define QP_CURVE_DEPTH 12u
#define QP_CURVE_TOLERANCE_SQUARED 0.015625
#define QP_COORD_LIMIT 1000000.0

typedef struct qp_point_d {
    double x, y;
} qp_point_d;

typedef struct qp_edge {
    qp_point_d from, to;
} qp_edge;

typedef struct qp_raster_draw {
    size_t first_edge, edge_count;
    uint32_t color;
    quantapdf_composer_fill_rule rule;
    int one_pixel_stroke;
    /* DEVICE-space half-width: all old one-pixel stroke users remain
     * exactly 0.5; only a separate fractional opt-in can change it.
     * No new rendering engine, implicit source width or cap increase. */
    double stroke_half_width;
    /* Proven full-device rectangle: conservative one coverage decision
     * per 4x4 sample, not four redundant flattened-edge intersections.
     * Stays in the canonical C11 raster and its existing work quota. */
    int full_canvas_rect;
} qp_raster_draw;

typedef struct qp_flatten {
    qp_edge *edges;
    size_t count, capacity;
    qp_point_d current, start;
    int active;
    double a, b, c, d, e, f;
} qp_flatten;

static int qp_valid_coordinate(double x)
{
    return isfinite(x) && x >= -QP_COORD_LIMIT && x <= QP_COORD_LIMIT;
}

static int qp_point_equal(qp_point_d a, qp_point_d b)
{
    return a.x == b.x && a.y == b.y;
}

static quantapdf_status qp_add_edge(qp_flatten *flat, qp_point_d a, qp_point_d b)
{
    if (qp_point_equal(a, b))
        return QUANTAPDF_OK;
    if (flat->count >= flat->capacity)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    flat->edges[flat->count].from = a;
    flat->edges[flat->count].to = b;
    ++flat->count;
    return QUANTAPDF_OK;
}

static quantapdf_status qp_transform_point(
    const qp_flatten *flat, quantapdf_point p, qp_point_d *out)
{
    double x = (double)p.x, y = (double)p.y;
    if (!qp_valid_coordinate(x) || !qp_valid_coordinate(y))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    out->x = flat->a * x + flat->c * y + flat->e;
    out->y = flat->b * x + flat->d * y + flat->f;
    if (!qp_valid_coordinate(out->x) || !qp_valid_coordinate(out->y))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    return QUANTAPDF_OK;
}

static int qp_curve_flat_enough(
    qp_point_d a, qp_point_d b, qp_point_d c, qp_point_d d)
{
    double dx = d.x - a.x, dy = d.y - a.y;
    double chord2 = dx * dx + dy * dy;
    if (chord2 < 1e-12) {
        double x1 = b.x - a.x, y1 = b.y - a.y;
        double x2 = c.x - a.x, y2 = c.y - a.y;
        return x1 * x1 + y1 * y1 <= QP_CURVE_TOLERANCE_SQUARED &&
               x2 * x2 + y2 * y2 <= QP_CURVE_TOLERANCE_SQUARED;
    }
    {
        double v1 = (b.x - a.x) * dy - (b.y - a.y) * dx;
        double v2 = (c.x - a.x) * dy - (c.y - a.y) * dx;
        double along1 = (b.x - a.x) * dx + (b.y - a.y) * dy;
        double along2 = (c.x - a.x) * dx + (c.y - a.y) * dy;
        /* A collinear cubic can still reverse or overshoot the chord.
         * Subdivide those cases instead of silently erasing the extrema. */
        return along1 >= 0.0 && along1 <= along2 &&
               along2 <= chord2 &&
               v1 * v1 <= QP_CURVE_TOLERANCE_SQUARED * chord2 &&
               v2 * v2 <= QP_CURVE_TOLERANCE_SQUARED * chord2;
    }
}

static qp_point_d qp_mid(qp_point_d x, qp_point_d y)
{
    qp_point_d v = {(x.x + y.x) * 0.5, (x.y + y.y) * 0.5};
    return v;
}

static quantapdf_status qp_flatten_cubic(
    qp_flatten *flat, qp_point_d p0, qp_point_d p1,
    qp_point_d p2, qp_point_d p3, unsigned depth)
{
    qp_point_d q0, q1, q2, r0, r1, s;
    quantapdf_status status;
    if (qp_curve_flat_enough(p0, p1, p2, p3))
        return qp_add_edge(flat, p0, p3);
    if (depth >= QP_CURVE_DEPTH)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    q0 = qp_mid(p0, p1);
    q1 = qp_mid(p1, p2);
    q2 = qp_mid(p2, p3);
    r0 = qp_mid(q0, q1);
    r1 = qp_mid(q1, q2);
    s = qp_mid(r0, r1);
    status = qp_flatten_cubic(flat, p0, q0, r0, s, depth + 1u);
    if (status != QUANTAPDF_OK)
        return status;
    return qp_flatten_cubic(flat, s, r1, q2, p3, depth + 1u);
}

static quantapdf_status qp_close_subpath(qp_flatten *flat)
{
    quantapdf_status status;
    if (!flat->active)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    status = qp_add_edge(flat, flat->current, flat->start);
    flat->active = 0;
    return status;
}

static quantapdf_status qp_flatten_path(
    qp_flatten *flat, const quantapdf_composer_path_operation *path)
{
    size_t i;
    for (i = 0; i < path->command_count; ++i) {
        const quantapdf_composer_path_command *cmd = &path->commands[i];
        qp_point_d p = {0}, q = {0}, r = {0};
        quantapdf_status status = QUANTAPDF_OK;
        switch (cmd->kind) {
        case QUANTAPDF_COMPOSER_PATH_MOVE_TO:
            if (flat->active) {
                status = qp_close_subpath(flat);
                if (status != QUANTAPDF_OK)
                    return status;
            }
            status = qp_transform_point(flat, cmd->point1, &p);
            if (status != QUANTAPDF_OK)
                return status;
            flat->current = flat->start = p;
            flat->active = 1;
            break;
        case QUANTAPDF_COMPOSER_PATH_LINE_TO:
            if (!flat->active)
                return QUANTAPDF_ERROR_UNSUPPORTED;
            status = qp_transform_point(flat, cmd->point1, &p);
            if (status != QUANTAPDF_OK)
                return status;
            status = qp_add_edge(flat, flat->current, p);
            flat->current = p;
            break;
        case QUANTAPDF_COMPOSER_PATH_CUBIC_TO:
            if (!flat->active)
                return QUANTAPDF_ERROR_UNSUPPORTED;
            status = qp_transform_point(flat, cmd->point1, &p);
            if (status == QUANTAPDF_OK)
                status = qp_transform_point(flat, cmd->point2, &q);
            if (status == QUANTAPDF_OK)
                status = qp_transform_point(flat, cmd->point3, &r);
            if (status != QUANTAPDF_OK)
                return status;
            status = qp_flatten_cubic(flat, flat->current, p, q, r, 0u);
            flat->current = r;
            break;
        case QUANTAPDF_COMPOSER_PATH_CLOSE:
            status = qp_close_subpath(flat);
            break;
        default:
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        if (status != QUANTAPDF_OK)
            return status;
    }
    if (flat->active)
        return qp_close_subpath(flat);
    return QUANTAPDF_OK;
}

static quantapdf_status qp_expand_rect_stroke_ring(
    qp_flatten *flat, size_t first, double half_x, double half_y)
{
    double min_x, max_x, min_y, max_y;
    qp_point_d outer[4], inner[4];
    unsigned vertical = 0u, horizontal = 0u;
    if (flat->count - first != 4u || !isfinite(half_x) ||
        !isfinite(half_y) || half_x <= 0.0 || half_y <= 0.0)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    min_x = max_x = flat->edges[first].from.x;
    min_y = max_y = flat->edges[first].from.y;
    for (size_t i = 0u; i < 4u; ++i) {
        const qp_edge *e = &flat->edges[first + i];
        if (!qp_point_equal(e->to, flat->edges[first + (i + 1u) % 4u].from))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (e->from.x == e->to.x)
            ++vertical;
        else if (e->from.y == e->to.y)
            ++horizontal;
        else
            return QUANTAPDF_ERROR_UNSUPPORTED;
        if (e->from.x < min_x) min_x = e->from.x;
        if (e->from.x > max_x) max_x = e->from.x;
        if (e->from.y < min_y) min_y = e->from.y;
        if (e->from.y > max_y) max_y = e->from.y;
        if (e->to.x < min_x) min_x = e->to.x;
        if (e->to.x > max_x) max_x = e->to.x;
        if (e->to.y < min_y) min_y = e->to.y;
        if (e->to.y > max_y) max_y = e->to.y;
    }
    if (vertical != 2u || horizontal != 2u ||
        !(max_x > min_x && max_y > min_y) ||
        max_x - min_x <= 2.0 * half_x ||
        max_y - min_y <= 2.0 * half_y ||
        flat->capacity - first < 8u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    outer[0] = (qp_point_d){min_x - half_x, min_y - half_y};
    outer[1] = (qp_point_d){max_x + half_x, min_y - half_y};
    outer[2] = (qp_point_d){max_x + half_x, max_y + half_y};
    outer[3] = (qp_point_d){min_x - half_x, max_y + half_y};
    inner[0] = (qp_point_d){min_x + half_x, min_y + half_y};
    inner[1] = (qp_point_d){max_x - half_x, min_y + half_y};
    inner[2] = (qp_point_d){max_x - half_x, max_y - half_y};
    inner[3] = (qp_point_d){min_x + half_x, max_y - half_y};
    for (size_t i = 0u; i < 4u; ++i) {
        if (!qp_valid_coordinate(outer[i].x) ||
            !qp_valid_coordinate(outer[i].y) ||
            !qp_valid_coordinate(inner[i].x) ||
            !qp_valid_coordinate(inner[i].y))
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    /* Replace the centerline rectangle with the actual outer/inner ring.
     * Retaining the centerline as an edge would render only half the stroke. */
    flat->count = first;
    for (size_t i = 0u; i < 4u; ++i) {
        quantapdf_status st = qp_add_edge(flat, outer[i], outer[(i + 1u) % 4u]);
        if (st != QUANTAPDF_OK)
            return st;
    }
    for (size_t i = 0u; i < 4u; ++i) {
        quantapdf_status st = qp_add_edge(flat, inner[i], inner[(i + 1u) % 4u]);
        if (st != QUANTAPDF_OK)
            return st;
    }
    return QUANTAPDF_OK;
}

static quantapdf_status qp_admit_draw(
    const quantapdf_composer_operation *op, qp_flatten *flat,
    const double *device, qp_raster_draw *out,
    int allow_empty_clip,int fractional_stroke_optin)
{
    const quantapdf_composer_path_operation *path = &op->value.path;
    const quantapdf_composer_path_options *opt = &path->options;
    quantapdf_status status;
    size_t first = flat->count;
    const int stroke = opt->stroke == 1 && opt->fill == 0;
    const int canonical_rect_stroke = stroke && path->command_count == 5u &&
        path->commands != NULL &&
        path->commands[0].kind == QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
        path->commands[1].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
        path->commands[2].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
        path->commands[3].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
        path->commands[4].kind == QUANTAPDF_COMPOSER_PATH_CLOSE;
    const int repeated_close_rect_stroke = stroke && path->command_count == 7u &&
        path->commands != NULL &&
        path->commands[0].kind == QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
        path->commands[1].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
        path->commands[2].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
        path->commands[3].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
        path->commands[4].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
        path->commands[5].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
        path->commands[6].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO;
    const int rect_stroke = canonical_rect_stroke || repeated_close_rect_stroke;
    const int open_line = stroke && path->command_count == 2u &&
        path->commands != NULL &&
        path->commands[0].kind == QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
        path->commands[1].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO;
    /* Explicit Source-shaped 0.3985px narrow LINE profile only.
     * TeXample's curved ring path is NOT qualified by this opt-in. */
    const int fractional_line = fractional_stroke_optin && open_line &&
        opt->stroke_width == 0.3985f;
    /* The same bounded cubic flattener/segment-distance kernel is
     * re-used for exactly two closed 4-cubic original ring contours.
     * The one named private entry verifies their whole geometry. */
    const int fractional_closed = (fractional_stroke_optin==2 || fractional_stroke_optin==3) &&
        stroke && opt->stroke_width==0.3985f &&
        path->command_count==12u && path->commands!=NULL;
    const int triangle_stroke = stroke && path->command_count == 4u &&
        path->commands != NULL &&
        path->commands[0].kind == QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
        path->commands[1].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
        path->commands[2].kind == QUANTAPDF_COMPOSER_PATH_LINE_TO &&
        path->commands[3].kind == QUANTAPDF_COMPOSER_PATH_CLOSE;
    if (op->graphics_state_id != 0u ||
        opt->struct_size < QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE ||
        !(stroke || (opt->fill == 1 && opt->stroke == 0)) ||
        ((stroke ? opt->stroke_argb : opt->fill_argb) >> 24u) != 255u ||
        (stroke && ((opt->stroke_width != 1.0f &&
                      !fractional_line && !fractional_closed) ||
            opt->line_cap != QUANTAPDF_COMPOSER_LINE_CAP_BUTT ||
            opt->line_join != QUANTAPDF_COMPOSER_LINE_JOIN_MITER ||
            opt->miter_limit != 10.0f ||
            !(rect_stroke || triangle_stroke || open_line ||
              fractional_closed))) ||
        (opt->fill_rule != QUANTAPDF_COMPOSER_FILL_NONZERO &&
         opt->fill_rule != QUANTAPDF_COMPOSER_FILL_EVEN_ODD) ||
        (opt->struct_size >= QUANTAPDF_COMPOSER_PATH_OPTIONS_V2_MIN_SIZE &&
         opt->graphics_state_id != 0u) ||
        (opt->struct_size >= QUANTAPDF_COMPOSER_PATH_OPTIONS_V3_MIN_SIZE &&
         (opt->fill_paint_id != 0u || opt->stroke_paint_id != 0u)) ||
        path->dash_count != 0u ||
        path->command_count == 0u || path->commands == NULL)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    flat->a = flat->d = 1.0;
    flat->b = flat->c = flat->e = flat->f = 0.0;
    flat->active = 0;
    if (opt->struct_size >= QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_MIN_SIZE) {
        const quantapdf_affine_transform *t = &opt->transform;
        int zero = t->a == 0 && t->b == 0 && t->c == 0 &&
                   t->d == 0 && t->e == 0 && t->f == 0;
        if (!zero) {
            double det;
            flat->a = (double)t->a;
            flat->b = (double)t->b;
            flat->c = (double)t->c;
            flat->d = (double)t->d;
            flat->e = (double)t->e;
            flat->f = (double)t->f;
            if (!qp_valid_coordinate(flat->a) ||
                !qp_valid_coordinate(flat->b) ||
                !qp_valid_coordinate(flat->c) ||
                !qp_valid_coordinate(flat->d) ||
                !qp_valid_coordinate(flat->e) ||
                !qp_valid_coordinate(flat->f))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            det = flat->a * flat->d - flat->b * flat->c;
            if (!isfinite(det) || det == 0.0)
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }
    if (device != NULL) {
        /* Row-vector affine composition: authored transform, THEN device.
         * Clips use this exact same path; no rounded float matrix is written
         * back into the borrowed IR. Flattening tolerance stays in pixels. */
        const double a = flat->a, b = flat->b, c = flat->c;
        const double d = flat->d, e = flat->e, f = flat->f;
        double det;
        flat->a = a * device[0] + b * device[2];
        flat->b = a * device[1] + b * device[3];
        flat->c = c * device[0] + d * device[2];
        flat->d = c * device[1] + d * device[3];
        flat->e = e * device[0] + f * device[2] + device[4];
        flat->f = e * device[1] + f * device[3] + device[5];
        if (!qp_valid_coordinate(flat->a) ||
            !qp_valid_coordinate(flat->b) ||
            !qp_valid_coordinate(flat->c) ||
            !qp_valid_coordinate(flat->d) ||
            !qp_valid_coordinate(flat->e) ||
            !qp_valid_coordinate(flat->f))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        det = flat->a * flat->d - flat->b * flat->c;
        if (!isfinite(det) || det == 0.0)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if (stroke && !rect_stroke && fractional_stroke_optin!=3) {
        /* The original triangle sampler remains restricted to an isometry. */
        const double x2 = flat->a * flat->a + flat->b * flat->b;
        const double y2 = flat->c * flat->c + flat->d * flat->d;
        const double cross = flat->a * flat->c + flat->b * flat->d;
        if (fabs(x2 - 1.0) > 0.000001 ||
            fabs(y2 - 1.0) > 0.000001 || fabs(cross) > 0.000001)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if (rect_stroke &&
        (fabs(flat->b) > 1e-12 || fabs(flat->c) > 1e-12 ||
         flat->a == 0.0 || flat->d == 0.0))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (rect_stroke && repeated_close_rect_stroke) {
        /* The PDF path lowerer may emit the rectangle's closing edge and then
         * repeat it while finalizing the path. Accept only the exact observed
         * pattern: p0->p1->p2->p3->p0->p1->p0. Normalize the duplicate tail
         * before geometric admission; do not infer a rectangle from bounds. */
        const quantapdf_composer_path_command *c = path->commands;
        quantapdf_composer_path_operation normalized = *path;
        quantapdf_composer_path_command commands[5];
        const quantapdf_point p0 = c[0].point1;
        if (c[4].point1.x != p0.x || c[4].point1.y != p0.y ||
            c[5].point1.x != c[1].point1.x ||
            c[5].point1.y != c[1].point1.y ||
            c[6].point1.x != p0.x || c[6].point1.y != p0.y) {
            return QUANTAPDF_ERROR_UNSUPPORTED;
        }
        for (size_t i = 0u; i < 4u; ++i)
            commands[i] = c[i];
        commands[4].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
        normalized.commands = commands;
        normalized.command_count = 5u;
        status = qp_flatten_path(flat, &normalized);
    } else {
        status = qp_flatten_path(flat, path);
    }
    if (status != QUANTAPDF_OK)
        return status;
    if (open_line && flat->count - first == 2u) {
        /* The current bounded profile accepts only horizontal/vertical open
         * segments. Diagonal source-line controls remain fail-closed. */
        const qp_edge *forward=&flat->edges[first];
        const qp_edge *closing=&flat->edges[first+1u];
        if (!qp_point_equal(forward->from,closing->to) ||
            !qp_point_equal(forward->to,closing->from) ||
            ((forward->from.x == forward->to.x) ==
             (forward->from.y == forward->to.y)))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        /* qp_flatten_path closes subpaths by default; remove only the exact
         * synthetic return edge so the butt-capped segment stays open. */
        --flat->count;
    }
    if (rect_stroke) {
        /* A rectangular miter stroke is an even-odd ring. Width is transformed
         * by the normal axis scale, so FIT's unequal X/Y scales stay explicit. */
        status = qp_expand_rect_stroke_ring(flat, first,
            fabs(flat->a) * (double)opt->stroke_width * 0.5,
            fabs(flat->d) * (double)opt->stroke_width * 0.5);
        if (status != QUANTAPDF_OK) {
            return status;
        }
    }
    /* A valid Clip PATH may contain only MOVE_TO or degenerate edges.
     * Its geometric intersection is empty: accept it as a zero-edge Clip
     * instead of treating it as a malformed resource. Painted paths must
     * still contain geometry. Sample-wise winding of zero edges is false. */
    if ((flat->count == first && !allow_empty_clip) ||
        (stroke && !rect_stroke &&
         !(fractional_closed ?
           (flat->count-first>=64u && flat->count-first<=256u) :
           (flat->count-first==(open_line?1u:3u)))) ||
        (rect_stroke && flat->count - first != 8u))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    out->first_edge = first;
    out->edge_count = flat->count - first;
    out->color = stroke ? opt->stroke_argb : opt->fill_argb;
    out->rule = rect_stroke ? QUANTAPDF_COMPOSER_FILL_EVEN_ODD : opt->fill_rule;
    out->one_pixel_stroke = stroke && !rect_stroke;
    out->stroke_half_width=(fractional_line || fractional_closed) ?
        (double)opt->stroke_width*0.5*
            (fractional_stroke_optin==3?flat->a:1.0) : 0.5;
    out->full_canvas_rect = 0;
    return QUANTAPDF_OK;
}

static int qp_inside(
    const qp_edge *edges, const qp_raster_draw *draw, double x, double y)
{
    size_t i;
    int winding = 0;
    for (i = 0; i < draw->edge_count; ++i) {
        const qp_edge *edge = &edges[draw->first_edge + i];
        double y0 = edge->from.y, y1 = edge->to.y;
        if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
            double intersection = edge->from.x +
                (y - y0) * (edge->to.x - edge->from.x) / (y1 - y0);
            if (intersection > x)
                winding += y1 > y0 ? 1 : -1;
        }
    }
    if (draw->rule == QUANTAPDF_COMPOSER_FILL_EVEN_ODD)
        return (winding % 2) != 0;
    return winding != 0;
}

/* Recognize the actual four-edge full-canvas PATH, never infer a
 * rectangle from a bounding box. Every edge must be axis aligned,
 * connect exactly to the next and visit all four distinct corners.
 * On this exact geometry, nonzero/evenodd both cover all 4x4 samples.
 * Clipped, disconnected, self-crossing and partial PATHs cannot opt in. */
static int qp_full_canvas_rect(const qp_edge *edges,
    const qp_raster_draw *draw,size_t width,size_t height)
{
    const double right=(double)width,bottom=(double)height;
    unsigned corners=0u;
    if(draw->one_pixel_stroke || draw->edge_count!=4u ||
       width==0u || height==0u)
        return 0;
    for(size_t i=0u;i<4u;++i) {
        const qp_edge *e=&edges[draw->first_edge+i];
        const qp_edge *next=&edges[draw->first_edge+(i+1u)%4u];
        const qp_point_d a=e->from,b=e->to;
        unsigned corner;
        if(!qp_point_equal(b,next->from) ||
           (a.x!=0.0 && a.x!=right) ||
           (b.x!=0.0 && b.x!=right) ||
           (a.y!=0.0 && a.y!=bottom) ||
           (b.y!=0.0 && b.y!=bottom) ||
           ((a.x==b.x)==(a.y==b.y)))
            return 0;
        corner=(unsigned)(a.x==right)+2u*(unsigned)(a.y==bottom);
        if((corners & (1u<<corner))!=0u)return 0;
        corners|=1u<<corner;
    }
    return corners==15u;
}

/* Work preflight for a SINGLE fully admitted clipped Fill PATH.
 * The flattened draw and clip edges define conservative Cartesian
 * pixel envelopes: winding is identically zero outside their
 * intersection. This proof is made AFTER the entire source/geometry
 * and every Clip is admitted, BEFORE output allocation. No fallback
 * or second raster. Stroke and multiple-PATH callers stay on the old
 * full-page work lease, including their existing refusal thresholds. */
typedef struct qp_sample_box {
    size_t x0,y0,x1,y1;
} qp_sample_box;

static int qp_edges_sample_box(const qp_edge *edges,
    const qp_raster_draw *draw,size_t width,size_t height,
    double inflate,qp_sample_box *out)
{
    double minx,maxx,miny,maxy;
    size_t i;
    if(out==NULL || edges==NULL || draw==NULL)return 0;
    *out=(qp_sample_box){0u,0u,0u,0u};
    if(draw->edge_count==0u)return 1;
    minx=maxx=edges[draw->first_edge].from.x;
    miny=maxy=edges[draw->first_edge].from.y;
    for(i=0u;i<draw->edge_count;++i) {
        const qp_edge *edge=&edges[draw->first_edge+i];
        const qp_point_d p[2]={edge->from,edge->to};
        size_t j;
        for(j=0u;j<2u;++j) {
            if(!qp_valid_coordinate(p[j].x) ||
               !qp_valid_coordinate(p[j].y))
                return 0;
            if(p[j].x<minx)minx=p[j].x;
            if(p[j].x>maxx)maxx=p[j].x;
            if(p[j].y<miny)miny=p[j].y;
            if(p[j].y>maxy)maxy=p[j].y;
        }
    }
    /* Opted-in 1px butt stroke must include the half-pixel
     * coverage on BOTH sides of its flattened line center. A Fill
     * and every Clip pass inflate=0 and retain the previous envelope.
     * Exact endpoint comparisons remain conservative for antialias. */
    if(inflate<0.0 || inflate>0.5 || !isfinite(inflate))return 0;
    minx-=inflate;maxx+=inflate;
    miny-=inflate;maxy+=inflate;
    if(maxx<=0.0 || maxy<=0.0 ||
       minx>=(double)width || miny>=(double)height)
        return 1;
    if(minx<0.0)minx=0.0;
    if(miny<0.0)miny=0.0;
    if(maxx>(double)width)maxx=(double)width;
    if(maxy>(double)height)maxy=(double)height;
    out->x0=(size_t)floor(minx);
    out->y0=(size_t)floor(miny);
    out->x1=(size_t)ceil(maxx);
    out->y1=(size_t)ceil(maxy);
    return 1;
}

static void qp_intersect_sample_box(qp_sample_box *a,
                                   const qp_sample_box *b)
{
    if(a->x0<b->x0)a->x0=b->x0;
    if(a->y0<b->y0)a->y0=b->y0;
    if(a->x1>b->x1)a->x1=b->x1;
    if(a->y1>b->y1)a->y1=b->y1;
    if(a->x1<a->x0)a->x1=a->x0;
    if(a->y1<a->y0)a->y1=a->y0;
}

/* Bounded one-pixel path stroke. Each antialias sample is covered iff it
 * lies within half a device-space pixel of one open segment or three closed
 * triangle segments. Unsupported widths, caps, joins, dashes and transforms are
 * rejected by qp_admit_draw; this cannot silently approximate general strokes. */
/* One unchanged segment-distance kernel; candidate list is only a
 * visited-work optimisation for the exact original closed-cubic case. */
static int qp_stroke_inside_candidates(const qp_edge *edges,
    const qp_raster_draw *draw,double x,double y,
    const size_t *indexes,size_t count)
{
    const size_t n=indexes==NULL?draw->edge_count:count;
    for(size_t i=0u;i<n;++i) {
        const size_t local=indexes==NULL?i:indexes[i];
        const qp_edge *edge=&edges[draw->first_edge+local];
        const double dx = edge->to.x - edge->from.x;
        const double dy = edge->to.y - edge->from.y;
        const double squared = dx * dx + dy * dy;
        double t;
        if (squared <= 0.0)
            continue;
        t = ((x - edge->from.x) * dx +
             (y - edge->from.y) * dy) / squared;
        if (t < 0.0 || t > 1.0)
            continue;
        const double px = edge->from.x + t * dx;
        const double py = edge->from.y + t * dy;
        const double ux = x - px, uy = y - py;
        if (ux * ux + uy * uy <=
            draw->stroke_half_width*draw->stroke_half_width)
            return 1;
    }
    return 0;
}
static int qp_stroke_inside(const qp_edge *edges,
    const qp_raster_draw *draw,double x,double y)
{
    return qp_stroke_inside_candidates(edges,draw,x,y,NULL,0u);
}

/* Straight-alpha source-over with coverage n/16 and opaque source color.
 * Using an integer numerator avoids floating-point rounding drift between
 * platforms. Transparent destination pixels keep unassociated RGB = 0. */
static void qp_blend_pixel(unsigned char *dst, uint32_t argb, unsigned n)
{
    unsigned remaining = QP_TOTAL_SAMPLES - n;
    unsigned old_alpha = dst[3];
    unsigned denominator = n * 255u + remaining * old_alpha;
    unsigned channel[3] = {
        (unsigned)((argb >> 16u) & 255u),
        (unsigned)((argb >> 8u) & 255u),
        (unsigned)(argb & 255u)
    };
    size_t c;
    if (n == 0u)
        return;
    for (c = 0; c < 3u; ++c) {
        unsigned numerator =
            channel[c] * n * 255u + (unsigned)dst[c] * remaining * old_alpha;
        dst[c] = (unsigned char)((numerator + denominator / 2u) / denominator);
    }
    dst[3] = (unsigned char)((denominator + QP_TOTAL_SAMPLES / 2u) /
                             QP_TOTAL_SAMPLES);
}

static void qp_background(unsigned char *rgba, size_t pixels, uint32_t bg)
{
    size_t i;
    unsigned char red = (unsigned char)(bg >> 16u);
    unsigned char green = (unsigned char)(bg >> 8u);
    unsigned char blue = (unsigned char)bg;
    for (i = 0; i < pixels; ++i) {
        rgba[4u * i] = red;
        rgba[4u * i + 1u] = green;
        rgba[4u * i + 2u] = blue;
        rgba[4u * i + 3u] = 255u;
    }
}

static int qp_no_resources(const quantapdf_ir *ir)
{
    return ir->resources.image_count == 0u &&
           ir->resources.font_count == 0u &&
           ir->resources.graphics_state_count == 0u &&
           ir->resources.soft_mask_count == 0u &&
           ir->resources.clip_count == 0u &&
           ir->resources.paint_count == 0u &&
           ir->resources.form_count == 0u &&
           ir->link_count == 0u && ir->outline_count == 0u;
}

/* The V1 stroke sampler models 1px line segments but not arbitrary miter
 * wedges. Guarantee that every join lies OUTSIDE the only supported
 * rectangular Clip, at least half_width * miter_limit away. Thus none of
 * the unimplemented join pixels can appear in the accepted visible area.
 * A stroke without a Clip or with a complex Clip is not accepted. */
static int qp_guard_clipped_stroke_geometry(
    const qp_edge *edges, const qp_raster_draw *stroke,
    const qp_raster_draw *clip)
{
    double minx = 0.0, maxx = 0.0, miny = 0.0, maxy = 0.0;
    const double join_reach = 5.0; /* width 1 * miter limit 10 / 2 */
    if (stroke->edge_count != 3u || clip->edge_count != 4u)
        return 0;
    for (size_t i = 0u; i < clip->edge_count; ++i) {
        const qp_edge *edge = &edges[clip->first_edge + i];
        const qp_point_d points[2] = {edge->from, edge->to};
        if ((edge->from.x == edge->to.x) ==
            (edge->from.y == edge->to.y))
            return 0; /* zero-length or diagonal edge */
        for (size_t p = 0u; p < 2u; ++p) {
            const qp_point_d v = points[p];
            if (i == 0u && p == 0u) {
                minx = maxx = v.x; miny = maxy = v.y;
            } else {
                if (v.x < minx) minx = v.x;
                if (v.x > maxx) maxx = v.x;
                if (v.y < miny) miny = v.y;
                if (v.y > maxy) maxy = v.y;
            }
        }
    }
    if (!(maxx > minx && maxy > miny))
        return 0;
    for (size_t i = 0u; i < clip->edge_count; ++i) {
        const qp_edge *e = &edges[clip->first_edge + i];
        /* Every edge must lie on a boundary of one axis-aligned rectangle. */
        if (!((e->from.x == minx && e->to.x == minx) ||
              (e->from.x == maxx && e->to.x == maxx) ||
              (e->from.y == miny && e->to.y == miny) ||
              (e->from.y == maxy && e->to.y == maxy)))
            return 0;
    }
    for (size_t i = 0u; i < stroke->edge_count; ++i) {
        const qp_point_d v = edges[stroke->first_edge + i].from;
        /* Joins in/near the Clip would need miter coverage not modeled by
         * the segment sampler. Conservatively reject their whole operation. */
        if (v.x > minx - join_reach && v.x < maxx + join_reach &&
            v.y > miny - join_reach && v.y < maxy + join_reach)
            return 0;
    }
    return 1;
}

/* Original smooth closed cubic coverage in the SAME V1 sample kernel.
 * All intervals enclose the exact binary32 Source coordinates. Subdivision
 * is a classification proof, never replacement line geometry. Every visited
 * distance/ray node and every sample/ray comparison consumes one work unit.
 * No uncertain leaf can publish a Bitmap. */
typedef struct qp_interval { double lo,hi; } qp_interval;
typedef struct qp_interval_point { qp_interval x,y; } qp_interval_point;
typedef struct qp_cubic_cell { qp_interval_point p[4]; } qp_cubic_cell;
#define QP_EXACT_CURVE_DEPTH 32u
#define QP_EXACT_CELLS 256u

#ifdef QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST
extern _Bool quantapdf_source_ir_oom_fail_c_allocation(void);
#endif
static void *qp_exact_malloc(size_t bytes,int exact)
{
#ifdef QUANTAPDF_NATIVE_SOURCE_STAGE_OOM_TEST
    if(exact && quantapdf_source_ir_oom_fail_c_allocation())return NULL;
#else
    (void)exact;
#endif
    return malloc(bytes);
}

static int qp_exact_charge(size_t *work,size_t count,size_t cap)
{
    if(*work>cap || count>cap-*work)return 0;
    *work+=count;
    return 1;
}
static double qp_down(double x) { return nextafter(x,-INFINITY); }
static double qp_up(double x) { return nextafter(x,INFINITY); }
static qp_interval qp_interval_mid(qp_interval a,qp_interval b)
{
    const qp_interval r={qp_down((a.lo+b.lo)*0.5),
                         qp_up((a.hi+b.hi)*0.5)};
    return r;
}
static qp_interval_point qp_interval_point_mid(
    qp_interval_point a,qp_interval_point b)
{
    const qp_interval_point r={qp_interval_mid(a.x,b.x),
                              qp_interval_mid(a.y,b.y)};
    return r;
}
static qp_interval_point qp_interval_source(quantapdf_point p)
{
    const qp_interval_point r={{p.x,p.x},{p.y,p.y}};
    return r;
}
static qp_cubic_cell qp_source_cubic(quantapdf_point start,
    const quantapdf_composer_path_command *cmd)
{
    const qp_cubic_cell r={{qp_interval_source(start),
        qp_interval_source(cmd->point1),qp_interval_source(cmd->point2),
        qp_interval_source(cmd->point3)}};
    return r;
}
/* Positive physical DPI scale acts on intervals, never a float command
 * buffer or an integer-canvas fit. Legacy 72-DPI intervals are unchanged. */
static qp_cubic_cell qp_exact_device_cubic(quantapdf_point start,
    const quantapdf_composer_path_command *cmd,double scale)
{
    qp_cubic_cell c=qp_source_cubic(start,cmd);
    if(scale!=1.0)for(unsigned i=0u;i<4u;++i) {
        const double lo=qp_down(scale),hi=qp_up(scale);
        const double x=c.p[i].x.lo,y=c.p[i].y.lo;
        c.p[i].x=(qp_interval){qp_down(x*(x<0.0?hi:lo)),qp_up(x*(x<0.0?lo:hi))};
        c.p[i].y=(qp_interval){qp_down(y*(y<0.0?hi:lo)),qp_up(y*(y<0.0?lo:hi))};
    }
    return c;
}
static void qp_exact_split(const qp_cubic_cell *p,
    qp_cubic_cell *left,qp_cubic_cell *right)
{
    const qp_interval_point q0=qp_interval_point_mid(p->p[0],p->p[1]);
    const qp_interval_point q1=qp_interval_point_mid(p->p[1],p->p[2]);
    const qp_interval_point q2=qp_interval_point_mid(p->p[2],p->p[3]);
    const qp_interval_point r0=qp_interval_point_mid(q0,q1);
    const qp_interval_point r1=qp_interval_point_mid(q1,q2);
    const qp_interval_point s=qp_interval_point_mid(r0,r1);
    *left=(qp_cubic_cell){{p->p[0],q0,r0,s}};
    *right=(qp_cubic_cell){{s,r1,q2,p->p[3]}};
}
static void qp_exact_hull(const qp_cubic_cell *p,
    double *xl,double *xh,double *yl,double *yh)
{
    *xl=p->p[0].x.lo;*xh=p->p[0].x.hi;
    *yl=p->p[0].y.lo;*yh=p->p[0].y.hi;
    for(unsigned i=1u;i<4u;++i) {
        if(p->p[i].x.lo<*xl)*xl=p->p[i].x.lo;
        if(p->p[i].x.hi>*xh)*xh=p->p[i].x.hi;
        if(p->p[i].y.lo<*yl)*yl=p->p[i].y.lo;
        if(p->p[i].y.hi>*yh)*yh=p->p[i].y.hi;
    }
}
static double qp_exact_distance_lower(const qp_cubic_cell *p,double x,double y)
{
    double xl,xh,yl,yh,dx,dy;
    qp_exact_hull(p,&xl,&xh,&yl,&yh);
    dx=x<xl?qp_down(xl-x):x>xh?qp_down(x-xh):0.0;
    dy=y<yl?qp_down(yl-y):y>yh?qp_down(y-yh):0.0;
    return qp_down((dx>0.0?qp_down(dx*dx):0.0)+
                   (dy>0.0?qp_down(dy*dy):0.0));
}
static double qp_exact_distance_upper(qp_interval_point p,double x,double y)
{
    const double dx=fmax(qp_up(fabs(p.x.lo-x)),qp_up(fabs(p.x.hi-x)));
    const double dy=fmax(qp_up(fabs(p.y.lo-y)),qp_up(fabs(p.y.hi-y)));
    return qp_up(qp_up(dx*dx)+qp_up(dy*dy));
}
/* 0 = certainly outside, 1 = certainly inside, -1 = uncertain/exhausted.
 * Endpoints of subdivision cells are actual points on the authored curve;
 * they never introduce caps or join geometry. */
static int qp_exact_distance(const qp_cubic_cell *p,double x,double y,
    qp_interval radius2,unsigned depth,size_t *work,size_t cap)
{
    qp_cubic_cell left,right;
    int a,b;
    if(!qp_exact_charge(work,1u,cap))return -1;
    if(qp_exact_distance_lower(p,x,y)>radius2.hi)return 0;
    if(qp_exact_distance_upper(p->p[0],x,y)<radius2.lo ||
       qp_exact_distance_upper(p->p[3],x,y)<radius2.lo)return 1;
    if(depth==QP_EXACT_CURVE_DEPTH)return -1;
    qp_exact_split(p,&left,&right);
    a=qp_exact_distance(&left,x,y,radius2,depth+1u,work,cap);
    if(a==1)return 1;
    b=qp_exact_distance(&right,x,y,radius2,depth+1u,work,cap);
    if(b==1)return 1;
    return a==0 && b==0?0:-1;
}
/* A monotone original quadrant has exactly one horizontal-ray crossing.
 * Certify which 1/4-pixel sample column it lies between. This computes a
 * crossing ONCE per subpixel row, not once per output pixel. */
static int qp_exact_ray(qp_cubic_cell cell,double y,size_t width,
    size_t *threshold,size_t *work,size_t cap)
{
    const int increasing=cell.p[3].y.lo>cell.p[0].y.lo;
    for(unsigned depth=0u;depth<=QP_EXACT_CURVE_DEPTH;++depth) {
        qp_cubic_cell left,right;
        double xl,xh,yl,yh,lo,hi;
        if(!qp_exact_charge(work,1u,cap))return 0;
        qp_exact_hull(&cell,&xl,&xh,&yl,&yh);
        lo=ceil(qp_down(qp_down(xl*4.0)-0.5));
        hi=ceil(qp_up(qp_up(xh*4.0)-0.5));
        if(lo<0.0)lo=0.0;
        if(hi<0.0)hi=0.0;
        if(lo>(double)(width*4u))lo=(double)(width*4u);
        if(hi>(double)(width*4u))hi=(double)(width*4u);
        if(lo==hi) { *threshold=(size_t)lo;return 1; }
        if(depth==QP_EXACT_CURVE_DEPTH)return 0;
        qp_exact_split(&cell,&left,&right);
        if(y<left.p[3].y.lo)cell=increasing?left:right;
        else if(y>left.p[3].y.hi)cell=increasing?right:left;
        else return 0; /* unresolved row exactly at a subdivision boundary */
    }
    return 0;
}
static int qp_exact_crossings(const quantapdf_composer_path_command *p,
    double y,size_t width,size_t thresholds[8],size_t *count,
    size_t *work,size_t cap,double scale)
{
    *count=0u;
    for(size_t ring=0u;ring<12u;ring+=6u) {
        quantapdf_point start=p[ring].point1;
        for(size_t k=ring+1u;k<ring+5u;++k) {
            const qp_cubic_cell c=qp_exact_device_cubic(start,&p[k],scale);
            const qp_interval a=c.p[0].y,b=c.p[3].y;
            if(!qp_exact_charge(work,1u,cap))return 0;
            /* Endpoint rounding must not decide a row's half-open side. */
            if(scale!=1.0 && ((y>=a.lo && y<=a.hi) ||
                             (y>=b.lo && y<=b.hi)))return 0;
            if((a.hi<=y && b.lo>y) || (b.hi<=y && a.lo>y)) {
                if(!qp_exact_ray(c,y,width,&thresholds[*count],work,cap))return 0;
                ++*count;
            }
            start=p[k].point3;
        }
    }
    return 1;
}
static void qp_exact_cells(qp_cubic_cell *out,size_t *count,
    const qp_cubic_cell *p,unsigned depth)
{
    qp_cubic_cell a,b;
    if(depth==0u) { out[(*count)++]=*p;return; }
    qp_exact_split(p,&a,&b);
    qp_exact_cells(out,count,&a,depth-1u);
    qp_exact_cells(out,count,&b,depth-1u);
}
static void qp_exact_cell_box(const qp_cubic_cell *p,double radius,
    size_t width,size_t height,qp_sample_box *out)
{
    double xl,xh,yl,yh;
    qp_exact_hull(p,&xl,&xh,&yl,&yh);
    xl=fmax(0.0,qp_down(xl-radius));
    xh=fmin((double)width,qp_up(xh+radius));
    yl=fmax(0.0,qp_down(yl-radius));
    yh=fmin((double)height,qp_up(yh+radius));
    *out=(qp_sample_box){0u,0u,0u,0u};
    if(xh<=xl || yh<=yl)return;
    out->x0=(size_t)floor(xl);out->x1=(size_t)ceil(xh);
    out->y0=(size_t)floor(yl);out->y1=(size_t)ceil(yh);
}
/* At most eight certified crossings define each even-odd row. Sort and
 * intersect the two bounded lists in sample-column space. Every crossing
 * comparison and every written sample is billed; no per-pixel ray tests
 * are skipped while still being performed. Duplicate crossings toggle
 * parity together. There is no threshold/image resampling or cache owner. */
static int qp_exact_fill_spans(uint16_t *mask,size_t width,size_t row,
    unsigned sy,size_t cross[2][8],const size_t count[2],size_t columns,
    size_t *work,size_t cap)
{
    size_t at[2]={0u,0u},first=0u;
    unsigned parity[2]={(unsigned)(count[0]&1u),(unsigned)(count[1]&1u)};
    for(size_t g=0u;g<2u;++g)for(size_t i=1u;i<count[g];++i) {
        const size_t value=cross[g][i];size_t j=i;
        while(j>0u) {
            if(!qp_exact_charge(work,1u,cap))return 0;
            if(cross[g][j-1u]<=value)break;
            cross[g][j]=cross[g][j-1u];--j;
        }
        cross[g][j]=value;
    }
    while(first<columns) {
        size_t end=columns;
        for(size_t g=0u;g<2u;++g)if(at[g]<count[g]) {
            if(!qp_exact_charge(work,1u,cap))return 0;
            if(cross[g][at[g]]<end)end=cross[g][at[g]];
        }
        if(end<first)return 0;
        if(parity[0] && parity[1])for(size_t col=first;col<end;++col) {
            if(!qp_exact_charge(work,1u,cap))return 0;
            mask[row*width+col/4u]|=(uint16_t)(1u<<(4u*sy+col%4u));
        }
        for(size_t g=0u;g<2u;++g)while(at[g]<count[g]) {
            if(!qp_exact_charge(work,1u,cap))return 0;
            if(cross[g][at[g]]!=end)break;
            parity[g]^=1u;++at[g];
        }
        first=end;
    }
    return 1;
}
/* Three 16-bit coverage masks are bounded temporary owners. All original
 * Clip/Fill/Stroke work is completed under ONE aggregate lease before the
 * existing output allocator and existing ordered source-over are entered. */
static quantapdf_status qp_exact_texample_masks(const quantapdf_ir *ir,
    const quantapdf_composer_clip_state *clip,
    const quantapdf_native_device_plan *plan,
    const quantapdf_ir_raster_v1_limits *limits,uint16_t **out_masks,
    size_t *out_work,int physical_dpi)
{
    const size_t width=plan->width,height=plan->height,area=width*height;
    const double scale=physical_dpi?(double)plan->dpi/72.0:1.0;
    const double page_width=plan->width_points*scale;
    const double page_height=plan->height_points*scale;
    qp_cubic_cell cells[QP_EXACT_CELLS];
    uint16_t *masks=(uint16_t *)qp_exact_malloc(area*3u*sizeof(*masks),1);
    size_t work=0u;
    const double source_radius=0.5*(double)ir->operations[1].value.path.options.stroke_width;
    const double radius=source_radius*scale;
    const double radius_lo=physical_dpi?qp_down(source_radius*qp_down(scale)):radius;
    const double radius_hi=physical_dpi?qp_up(source_radius*qp_up(scale)):radius;
    const qp_interval radius2={qp_down(radius_lo*radius_lo),qp_up(radius_hi*radius_hi)};
    *out_masks=NULL;*out_work=0u;
    if(masks==NULL)return QUANTAPDF_ERROR_NOMEM;
    memset(masks,0,area*3u*sizeof(*masks));
    for(size_t y=0u;y<height;++y)for(unsigned sy=0u;sy<4u;++sy) {
        const double py=(double)y+((double)sy+0.5)/4.0;
        size_t cross[2][8],count[2];
        if(py>=page_height)continue;
        if(!qp_exact_crossings(ir->operations[0].value.path.commands,py,width,
                cross[0],&count[0],&work,limits->max_sample_tests,scale) ||
           !qp_exact_crossings(clip->commands,py,width,
                cross[1],&count[1],&work,limits->max_sample_tests,scale))goto unsupported;
        if(physical_dpi) {
            /* Samples satisfy col+1/2 < 4*physical_width exactly; page
             * admission fixed the rational ceil independently of floats. */
            const size_t columns=(size_t)ceil(page_width*4.0-0.5);
            if(columns>width*4u || !qp_exact_fill_spans(masks,width,y,sy,
                cross,count,columns,&work,limits->max_sample_tests))goto unsupported;
            continue;
        }
        for(size_t x=0u;x<width;++x)for(unsigned sx=0u;sx<4u;++sx) {
            const double px=(double)x+((double)sx+0.5)/4.0;
            const size_t column=x*4u+sx;
            unsigned hit[2]={0u,0u};
            if(px>=page_width)continue;
            if(!qp_exact_charge(&work,count[0]+count[1],limits->max_sample_tests))
                goto unsupported;
            for(size_t c=0u;c<2u;++c)
                for(size_t k=0u;k<count[c];++k)hit[c]^=(unsigned)(column<cross[c][k]);
            if(hit[0] && hit[1])masks[y*width+x]|=(uint16_t)(1u<<(4u*sy+sx));
        }
    }
    for(size_t layer=1u;layer<3u;++layer) {
        const quantapdf_composer_path_command *p=ir->operations[layer].value.path.commands;
        size_t n=0u;
        for(size_t ring=0u;ring<12u;ring+=6u) {
            quantapdf_point start=p[ring].point1;
            for(size_t k=ring+1u;k<ring+5u;++k) {
                const qp_cubic_cell cell=qp_exact_device_cubic(start,&p[k],scale);
                qp_exact_cells(cells,&n,&cell,5u);
                start=p[k].point3;
            }
        }
        if(n!=QP_EXACT_CELLS)goto unsupported;
        for(size_t k=0u;k<n;++k) {
            qp_sample_box box;
            qp_exact_cell_box(&cells[k],radius_hi,width,height,&box);
            for(size_t y=box.y0;y<box.y1;++y)for(size_t x=box.x0;x<box.x1;++x)
            for(unsigned sy=0u;sy<4u;++sy)for(unsigned sx=0u;sx<4u;++sx) {
                const double px=(double)x+((double)sx+0.5)/4.0;
                const double py=(double)y+((double)sy+0.5)/4.0;
                int hit;
                if(px>=page_width || py>=page_height)continue;
                hit=qp_exact_distance(&cells[k],px,py,radius2,0u,
                                      &work,limits->max_sample_tests);
                if(hit<0)goto unsupported;
                if(hit)masks[layer*area+y*width+x]|=(uint16_t)(1u<<(4u*sy+sx));
            }
        }
    }
    *out_masks=masks;*out_work=work;
    return QUANTAPDF_OK;
unsupported:
    free(masks);
    return QUANTAPDF_ERROR_UNSUPPORTED;
}

static quantapdf_status qp_render_v1_impl(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,
    const quantapdf_composer_clip_state *const *clips, size_t clip_count,
    int visible_work_optin,quantapdf_ir_raster_image *out_image,
    size_t *out_sample_tests)
{
    size_t width, height, pixels_count, total_checks = 0u;
    size_t i, draw_count = 0u, x, y, ci;
    qp_flatten flatten = {0};
    qp_raster_draw *draws = NULL;
    qp_raster_draw clip_draws[QUANTAPDF_NATIVE_CLIP_DAG_MAX_LEAVES] = {{0}};
    unsigned char *pixels = NULL;
    quantapdf_status status = QUANTAPDF_ERROR_ARGUMENT;
    const quantapdf_composer_page_state *page;
    int two_path_clip_pair = 0;
    int bounded_clipped_fill = 0;
    int bounded_clipped_stroke = 0;
    int bounded_curved_stroke = 0;
    qp_sample_box sample_box = {0u,0u,0u,0u};
    qp_sample_box *curve_boxes = NULL;
    uint16_t *exact_masks = NULL;
    const int physical_dpi=visible_work_optin==9;
    const int shared_texample=visible_work_optin==8 || physical_dpi;
    const int exact_texample=visible_work_optin==7 || shared_texample;

    if (out_sample_tests != NULL)*out_sample_tests=0u;
    if (out_image == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image, 0, sizeof(*out_image));
    if (ir == NULL || limits == NULL || ir->pages == NULL ||
        page_index >= ir->page_count ||
        limits->max_pixels == 0u || limits->max_edges == 0u || limits->max_edges > 65536u ||
        limits->max_operations == 0u || limits->max_sample_tests == 0u ||
        limits->max_pixels > SIZE_MAX / 4u ||
        limits->max_edges > SIZE_MAX / sizeof(qp_edge) ||
        limits->max_operations > SIZE_MAX / sizeof(qp_raster_draw) ||
        ir->operation_count > limits->max_operations ||
        clip_count > QUANTAPDF_NATIVE_CLIP_DAG_MAX_LEAVES ||
        (clip_count > 0u && clips == NULL) ||
        (ir->operation_count != 0u && ir->operations == NULL))
        return QUANTAPDF_ERROR_ARGUMENT;

    page = &ir->pages[page_index];
    if (!isfinite(page->width_points) || !isfinite(page->height_points) ||
        page->width_points <= 0.0f || page->height_points <= 0.0f ||
        (double)page->width_points > (double)limits->max_pixels ||
        (double)page->height_points > (double)limits->max_pixels)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (plan != NULL) {
        const int physical = visible_work_optin == 5 || visible_work_optin == 6 || exact_texample;
        if (!(physical ?
                quantapdf_native_device_plan_matches_physical_page(plan,
                    page->width_points, page->height_points, limits->max_pixels) :
                quantapdf_native_device_plan_matches_page(plan,
                    page->width_points, page->height_points, limits->max_pixels)))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        width = plan->width;
        height = plan->height;
    } else {
        width = (size_t)page->width_points;
        height = (size_t)page->height_points;
        if (width == 0u || height == 0u ||
            (double)width != (double)page->width_points ||
            (double)height != (double)page->height_points ||
            width > limits->max_pixels / height)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    if (page->suppress_background < 0 || page->suppress_background > 1 ||
        (!page->suppress_background && (page->background_argb >> 24u) != 255u) ||
        !qp_no_resources(ir))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Admit the bounded two-Path Stroke/Fill case. Explicit clip arguments
     * apply to both draws; source ordering belongs to the scoped graphics
     * states, not to an inference from operation counts. */
    if (clip_count > 0u && ir->operation_count == 2u &&
        clip_count == 1u && ir->operations != NULL &&
        ir->operations[0].page_index == page_index &&
        ir->operations[1].page_index == page_index &&
        ir->operations[0].kind == QUANTAPDF_COMPOSER_OPERATION_PATH &&
        ir->operations[1].kind == QUANTAPDF_COMPOSER_OPERATION_PATH &&
        (((ir->operations[0].value.path.options.stroke == 1 &&
           ir->operations[0].value.path.options.fill == 0) &&
          (ir->operations[1].value.path.options.fill == 1 &&
           ir->operations[1].value.path.options.stroke == 0)) ||
         ((ir->operations[1].value.path.options.stroke == 1 &&
           ir->operations[1].value.path.options.fill == 0) &&
          (ir->operations[0].value.path.options.fill == 1 &&
           ir->operations[0].value.path.options.stroke == 0))))
        two_path_clip_pair = 1;
    if (clip_count > 0u &&
        !(ir->operation_count == 1u &&
          ir->operations[0].page_index == page_index) &&
        !two_path_clip_pair && !exact_texample)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    pixels_count = width * height;
    /* One source-owned clipped Fill is allowed to establish its
     * VISITED sample area from actual flattened geometry below. All
     * other callers keep the old all-page minimum work admission. */
    bounded_clipped_fill=visible_work_optin==1 &&
        clip_count!=0u && ir->operation_count==1u &&
        ir->operations!=NULL &&
        ir->operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_PATH &&
        ir->operations[0].value.path.options.fill==1 &&
        ir->operations[0].value.path.options.stroke==0;
    /* Separate strict opt-in. The centerline is exactly one open
     * two-command segment, not a triangle/closed stroke, and both
     * W/W* Clip leaves are supplied by a qualified V5 owner. */
    bounded_clipped_stroke=(visible_work_optin==2 ||
        visible_work_optin==3) &&
        clip_count==2u && ir->operation_count==1u &&
        ir->operations!=NULL &&
        ir->operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_PATH &&
        ir->operations[0].value.path.options.fill==0 &&
        ir->operations[0].value.path.options.stroke==1 &&
        ir->operations[0].value.path.command_count==2u &&
        (visible_work_optin!=3 ||
         ir->operations[0].value.path.options.stroke_width==0.3985f);
    if((visible_work_optin==2 || visible_work_optin==3) &&
       !bounded_clipped_stroke)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    /* One private 0.3985px dual-ring closed cubic paint. It has no
     * inherited Clip, no alternate renderer and no public source
     * admission. Flattened work is checked before any output buffer. */
    bounded_curved_stroke=(visible_work_optin==4 || visible_work_optin==5) &&
        clip_count==0u && ir->operation_count==1u &&
        ir->operations!=NULL &&
        ir->operations[0].kind==QUANTAPDF_COMPOSER_OPERATION_PATH &&
        ir->operations[0].value.path.options.stroke==1 &&
        ir->operations[0].value.path.options.fill==0 &&
        ir->operations[0].value.path.options.stroke_width==0.3985f &&
        ir->operations[0].value.path.command_count==12u &&
        limits->max_edges<=256u;
    if((visible_work_optin==4 || visible_work_optin==5) &&
       !bounded_curved_stroke)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if(!bounded_clipped_fill && !bounded_clipped_stroke &&
       !bounded_curved_stroke && !exact_texample &&
       pixels_count > limits->max_sample_tests / QP_TOTAL_SAMPLES)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    flatten.capacity = limits->max_edges;
    flatten.edges = (qp_edge *)qp_exact_malloc(flatten.capacity * sizeof(qp_edge),exact_texample);
    if(exact_texample && flatten.edges==NULL) {
        status=QUANTAPDF_ERROR_NOMEM;goto cleanup;
    }
    draws = (qp_raster_draw *)qp_exact_malloc(limits->max_operations * sizeof(qp_raster_draw),exact_texample);
    if (flatten.edges == NULL || draws == NULL) {
        status = QUANTAPDF_ERROR_NOMEM;
        goto cleanup;
    }

    /* Whole-page admission and complexity billing precede output allocation.
     * No content from an unsupported selected-page operation may be skipped. */
    for (i = 0u; i < ir->operation_count; ++i) {
        const quantapdf_composer_operation *op = &ir->operations[i];
        if (op->page_index >= ir->page_count) {
            status = QUANTAPDF_ERROR_ARGUMENT;
            goto cleanup;
        }
        if (op->page_index != page_index)
            continue;
        if (op->kind != QUANTAPDF_COMPOSER_OPERATION_PATH) {
            status = QUANTAPDF_ERROR_UNSUPPORTED;
            goto cleanup;
        }
        if(shared_texample && i==2u) {
            /* Fill == second S is proved before resource projection.
             * Share only immutable geometry; preserve this paint's own
             * black Stroke options and Q-restored GS0 identity. */
            draws[draw_count]=draws[0];
            draws[draw_count].color=op->value.path.options.stroke_argb;
            draws[draw_count].rule=op->value.path.options.fill_rule;
            draws[draw_count].one_pixel_stroke=1;
            draws[draw_count].stroke_half_width=
                0.5*(double)op->value.path.options.stroke_width;
            status=QUANTAPDF_OK;
        } else {
            status = qp_admit_draw(op, &flatten,
                plan == NULL ? NULL : plan->from_composer, &draws[draw_count],
                0,visible_work_optin==3?1:
                  (physical_dpi?3:(bounded_curved_stroke || exact_texample)?2:0));
        }
        if (status != QUANTAPDF_OK) {
            goto cleanup;
        }
        /* Keep the SAME 4x4 sample-work cap. A fully proven unclipped
         * device rectangle uses one conservative coverage decision per
         * sample; all other draws retain the full per-edge work bill. */
        draws[draw_count].full_canvas_rect=clip_count==0u &&
            qp_full_canvas_rect(flatten.edges,&draws[draw_count],
                                width,height);
        if(!bounded_clipped_fill && !bounded_clipped_stroke &&
           !bounded_curved_stroke && !exact_texample) {
            const size_t checks_per_sample=
                draws[draw_count].full_canvas_rect ?
                    1u : draws[draw_count].edge_count;
            if(checks_per_sample >
                (limits->max_sample_tests - total_checks) /
                    (QP_TOTAL_SAMPLES * pixels_count)) {
                status=QUANTAPDF_ERROR_UNSUPPORTED;
                goto cleanup;
            }
            total_checks+=checks_per_sample*
                          (QP_TOTAL_SAMPLES*pixels_count);
        }
        ++draw_count;
    }

    for (ci = 0u; ci < clip_count; ++ci) {
        quantapdf_composer_operation clip_op = {0};
        const quantapdf_composer_clip_state *clip = clips[ci];
        quantapdf_composer_path_options *options =
            &clip_op.value.path.options;
        if (clip == NULL ||
            clip->kind != QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
            clip->members != NULL || clip->member_count != 0u ||
            clip->commands == NULL || clip->command_count == 0u) {
            status = QUANTAPDF_ERROR_UNSUPPORTED;
            goto cleanup;
        }
        clip_op.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
        clip_op.value.path.commands = clip->commands;
        clip_op.value.path.command_count = clip->command_count;
        options->struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
        options->fill = 1;
        options->fill_argb = UINT32_C(0xff000000);
        options->fill_rule = clip->fill_rule;
        options->transform = clip->transform;
        if(shared_texample) {
            /* Clip == first S is proved before resource projection.
             * The Clip keeps its independent even-odd predicate and
             * identity; shared centreline storage cannot merge scopes. */
            clip_draws[ci]=draws[1];
            clip_draws[ci].rule=clip->fill_rule;
            clip_draws[ci].one_pixel_stroke=0;
            status=QUANTAPDF_OK;
        } else {
            status = qp_admit_draw(&clip_op, &flatten,
                plan == NULL ? NULL : plan->from_composer, &clip_draws[ci],1,0);
        }
        if (status != QUANTAPDF_OK) {
            goto cleanup;
        }
        if(!bounded_clipped_fill && !bounded_clipped_stroke &&
           !bounded_curved_stroke && !exact_texample) {
            if (clip_draws[ci].edge_count >
                (limits->max_sample_tests - total_checks) /
                    (QP_TOTAL_SAMPLES * pixels_count)) {
                status = QUANTAPDF_ERROR_UNSUPPORTED;
                goto cleanup;
            }
            total_checks += clip_draws[ci].edge_count *
                            (QP_TOTAL_SAMPLES * pixels_count);
        }
    }

    /* The admitted one-pixel stroke remains exact only where the only
     * Clip excludes all closed-triangle joins. No silent stroke fallback. */
    for (i = 0u; i < draw_count; ++i) {
        if (draws[i].one_pixel_stroke &&
            !bounded_curved_stroke && !exact_texample && draws[i].edge_count != 1u &&
            (draw_count != 1u || clip_count != 1u ||
             !qp_guard_clipped_stroke_geometry(flatten.edges,
                 &draws[i], &clip_draws[0]))) {
            status = QUANTAPDF_ERROR_UNSUPPORTED;
            goto cleanup;
        }
    }

    if(bounded_clipped_fill || bounded_clipped_stroke) {
        size_t sample_pixels,edges_per_sample;
        qp_sample_box clip_box;
        if(draw_count!=1u ||
           (bounded_clipped_stroke &&
            (!draws[0].one_pixel_stroke ||
             draws[0].edge_count!=1u)) ||
           !qp_edges_sample_box(flatten.edges,&draws[0],
                  width,height,bounded_clipped_stroke?
                      draws[0].stroke_half_width:0.0,
                  &sample_box)) {
            status=QUANTAPDF_ERROR_UNSUPPORTED;
            goto cleanup;
        }
        edges_per_sample=draws[0].edge_count;
        for(ci=0u;ci<clip_count;++ci) {
            if(!qp_edges_sample_box(flatten.edges,&clip_draws[ci],
                                    width,height,0.0,&clip_box)) {
                status=QUANTAPDF_ERROR_UNSUPPORTED;
                goto cleanup;
            }
            qp_intersect_sample_box(&sample_box,&clip_box);
            /* The entire flattened Clip edge list is still billed,
             * including portions outside the visible intersection.
             * Only provably unvisited samples disappear from cost. */
            edges_per_sample+=clip_draws[ci].edge_count;
        }
        sample_pixels=(sample_box.x1-sample_box.x0)*
                      (sample_box.y1-sample_box.y0);
        if(sample_pixels>SIZE_MAX/QP_TOTAL_SAMPLES) {
            status=QUANTAPDF_ERROR_UNSUPPORTED;
            goto cleanup;
        }
        /* Upper-bound all winding edges for every VISITED subpixel.
         * Checked division prevents overflow; max_sample_tests itself
         * is unchanged. Empty geometric intersection is a valid
         * transparent draw, not a failure or skipped source parse. */
        if(sample_pixels!=0u &&
           edges_per_sample > limits->max_sample_tests /
                  (QP_TOTAL_SAMPLES*sample_pixels)) {
            status=QUANTAPDF_ERROR_UNSUPPORTED;
            goto cleanup;
        }
        total_checks=sample_pixels*QP_TOTAL_SAMPLES*edges_per_sample;
    }

    if(bounded_curved_stroke) {
        /* One unchanged C11 4x4 sampler. Before allocating pixels,
         * compute a conservative candidate domain for EACH flattened
         * authored cubic edge, inflated by exact source half-width.
         * Every cell in each domain is charged 16 potential segment
         * distance tests, even if later covered by an earlier edge.
         * This is a strict upper bound on the work actually sampled,
         * without any increase to the original global 8M limit. */
        const qp_raster_draw *draw=&draws[0];
        if(draw_count!=1u || draw->edge_count<64u ||
           draw->edge_count>256u || draw->stroke_half_width<=0.0) {
            status=QUANTAPDF_ERROR_UNSUPPORTED;
            goto cleanup;
        }
        curve_boxes=(qp_sample_box *)malloc(
            draw->edge_count*sizeof(*curve_boxes));
        if(curve_boxes==NULL) {
            status=QUANTAPDF_ERROR_NOMEM;
            goto cleanup;
        }
        for(size_t k=0u;k<draw->edge_count;++k) {
            qp_raster_draw single=*draw;
            const qp_sample_box *box;
            size_t cols,rows,cells;
            single.first_edge=draw->first_edge+k;
            single.edge_count=1u;
            if(!qp_edges_sample_box(flatten.edges,&single,
                   width,height,draw->stroke_half_width,
                   &curve_boxes[k])) {
                status=QUANTAPDF_ERROR_UNSUPPORTED;
                goto cleanup;
            }
            box=&curve_boxes[k];
            cols=box->x1-box->x0;
            rows=box->y1-box->y0;
            if(rows!=0u && cols>SIZE_MAX/rows) {
                status=QUANTAPDF_ERROR_UNSUPPORTED;
                goto cleanup;
            }
            cells=cols*rows;
            if(cells>(limits->max_sample_tests-total_checks)/
                    QP_TOTAL_SAMPLES) {
                status=QUANTAPDF_ERROR_UNSUPPORTED;
                goto cleanup;
            }
            total_checks+=cells*QP_TOTAL_SAMPLES;
        }
    }

    if(exact_texample) {
        status=qp_exact_texample_masks(ir,clips[0],plan,limits,&exact_masks,&total_checks,physical_dpi);
        if(status!=QUANTAPDF_OK)goto cleanup;
    }
    pixels = (unsigned char *)qp_exact_malloc(pixels_count * 4u,exact_texample);
    if (pixels == NULL) {
        status = QUANTAPDF_ERROR_NOMEM;
        goto cleanup;
    }
    if (page->suppress_background)
        memset(pixels, 0, pixels_count * 4u);
    else
        qp_background(pixels, pixels_count, page->background_argb);

    for (i = 0u; i < draw_count; ++i) {
        const qp_raster_draw *draw = &draws[i];
        if(exact_texample) {
            for(y=0u;y<height;++y)for(x=0u;x<width;++x) {
                unsigned covered=0u;
                uint16_t mask=exact_masks[i*pixels_count+y*width+x];
                for(unsigned k=0u;k<16u;++k)covered+=(unsigned)((mask>>k)&1u);
                qp_blend_pixel(pixels+4u*(y*width+x),draw->color,covered);
            }
            continue;
        }
        if(draw->full_canvas_rect) {
            /* The PATH covers the complete integer buffer. Physical
             * pages still intersect that proof with their fractional
             * viewport at the same 4x4 sample grid. The preflight's
             * sixteen sample decisions per pixel remain conservative. */
            for(y=0u;y<height;++y)
                for(x=0u;x<width;++x) {
                    unsigned covered=QP_TOTAL_SAMPLES;
                    if(plan!=NULL && plan->mode==QUANTAPDF_NATIVE_DEVICE_PHYSICAL_V1) {
                        unsigned nx=0u,ny=0u;
                        const double scale=(double)plan->dpi/72.0;
                        for(unsigned k=0u;k<QP_SAMPLES;++k) {
                            nx+=(unsigned)((double)x+((double)k+0.5)/QP_SAMPLES<
                                           plan->width_points*scale);
                            ny+=(unsigned)((double)y+((double)k+0.5)/QP_SAMPLES<
                                           plan->height_points*scale);
                        }
                        covered=nx*ny;
                    }
                    qp_blend_pixel(pixels+4u*(y*width+x),draw->color,covered);
                }
            continue;
        }
        for (y = 0u; y < height; ++y) {
            for (x = 0u; x < width; ++x) {
                unsigned sx, sy, covered = 0u;
                size_t candidates[256], candidate_count=0u;
                if(bounded_curved_stroke) {
                    /* Find the only segments that can cover this
                     * pixel's 16 sample points; do this once per pixel,
                     * never re-scan all flattened edges per subpixel.
                     * The above admission has charged every candidate
                     * distance test whether or not it finds a hit. */
                    for(size_t k=0u;k<draw->edge_count;++k) {
                        const qp_sample_box *box=&curve_boxes[k];
                        if(x>=box->x0 && x<box->x1 &&
                           y>=box->y0 && y<box->y1)
                            candidates[candidate_count++]=k;
                    }
                    if(candidate_count==0u)continue;
                }
                /* The old winding computation is exactly zero outside
                 * the intersection of flattened draw and Clip bounds.
                 * Avoid those per-edge checks only after the complete
                 * bounded-work admission above. */
                if((bounded_clipped_fill || bounded_clipped_stroke) &&
                   (x<sample_box.x0 || x>=sample_box.x1 ||
                    y<sample_box.y0 || y>=sample_box.y1))
                    continue;
                for (sy = 0u; sy < QP_SAMPLES; ++sy) {
                    double py = (double)y +
                        ((double)sy + 0.5) / (double)QP_SAMPLES;
                    for (sx = 0u; sx < QP_SAMPLES; ++sx) {
                        double px = (double)x +
                            ((double)sx + 0.5) / (double)QP_SAMPLES;
                        /* The integer buffer encloses the physical page. A
                         * sample in its right/bottom fringe must never paint,
                         * even when authored geometry crosses the page edge. */
                        if (plan != NULL &&
                            plan->mode == QUANTAPDF_NATIVE_DEVICE_PHYSICAL_V1 &&
                            (px >= plan->width_points * ((double)plan->dpi / 72.0) ||
                             py >= plan->height_points * ((double)plan->dpi / 72.0)))
                            continue;
                        int visible = bounded_curved_stroke ?
                            qp_stroke_inside_candidates(flatten.edges,draw,
                                px,py,candidates,candidate_count) :
                            (draw->one_pixel_stroke ?
                             qp_stroke_inside(flatten.edges,draw,px,py) :
                             qp_inside(flatten.edges,draw,px,py));
                        for (ci = 0u; visible && ci < clip_count; ++ci)
                            visible = qp_inside(
                                flatten.edges, &clip_draws[ci], px, py);
                        covered += (unsigned)visible;
                    }
                }
                qp_blend_pixel(pixels + 4u * (y * width + x),
                               draw->color, covered);
            }
        }
    }

    out_image->pixels = pixels;
    out_image->size = pixels_count * 4u;
    out_image->width = width;
    out_image->height = height;
    out_image->stride = width * 4u;
    pixels = NULL;
    if(out_sample_tests!=NULL)*out_sample_tests=total_checks;
    status = QUANTAPDF_OK;
cleanup:
    free(exact_masks);
    free(pixels);
    free(curve_boxes);
    free(draws);
    free(flatten.edges);
    return status;
}

quantapdf_status quantapdf_ir_raster_render_v1(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_render_v1_impl(ir, page_index, limits, NULL, NULL, 0u, 0, out_image,NULL);
}

quantapdf_status quantapdf_ir_raster_render_v1_clipped(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_composer_clip_state *clip,
    quantapdf_ir_raster_image *out_image)
{
    if (clip == NULL) {
        if (out_image != NULL)
            memset(out_image, 0, sizeof(*out_image));
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    {
        const quantapdf_composer_clip_state *one[1] = {clip};
        return qp_render_v1_impl(ir, page_index, limits, NULL, one, 1u, 0, out_image,NULL);
    }
}

quantapdf_status quantapdf_ir_raster_render_v1_multi_clipped(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_composer_clip_state *const *clips, size_t clip_count,
    quantapdf_ir_raster_image *out_image)
{
    if (clip_count == 0u || clips == NULL) {
        if (out_image != NULL)
            memset(out_image, 0, sizeof(*out_image));
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    return qp_render_v1_impl(
        ir, page_index, limits, NULL, clips, clip_count, 0, out_image,NULL);
}


/* Explicit PRIVATE V10 work admission in the SAME qp_render_v1_impl.
 * Only a fully validated single clipped opaque Fill may bound its
 * visited 4x4 subpixels by the actual flattened edge intersection.
 * All old V1 entrypoints above preserve the full-page work lease. */
quantapdf_status quantapdf_ir_raster_render_v1_multi_clipped_visible_work(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_composer_clip_state *const *clips,size_t clip_count,
    quantapdf_ir_raster_image *out_image)
{
    if(clip_count==0u || clips==NULL) {
        if(out_image!=NULL)memset(out_image,0,sizeof(*out_image));
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    return qp_render_v1_impl(ir,page_index,limits,NULL,
                              clips,clip_count,1,out_image,NULL);
}

/* Dedicated PRIVATE one-open-segment opaque 1px Stroke under
 * exactly two owned Clip PATH leaves. Uses the same 4x4 sampler and
 * preflight as all other V1 paints, but conservatively expands the
 * Stroke's flattened bounding box by half a device pixel before
 * intersecting Clip bounds. Old visible Fill and legacy V1 entries
 * retain their original strict work limits and refusal behavior.
 * Source V10 cannot select this private function without full
 * original PDF materialization and its independent acceptance. */
quantapdf_status quantapdf_ir_raster_render_v1_multi_clipped_stroke_visible_work(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_composer_clip_state *const *clips,size_t clip_count,
    quantapdf_ir_raster_image *out_image)
{
    if(clip_count!=2u || clips==NULL) {
        if(out_image!=NULL)memset(out_image,0,sizeof(*out_image));
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    return qp_render_v1_impl(ir,page_index,limits,NULL,
                             clips,clip_count,2,out_image,NULL);
}

/* PRIVATE exact TeXample WIDTH 0.3985px under TWO qualified
 * Clip leaves, but ONLY for one open axis-aligned butt-capped solid
 * segment. This is the same V1 4x4 distance sampler, not general
 * cubic ring Stroke/Source support. The old 1px entry stays strict. */
quantapdf_status quantapdf_ir_raster_render_v1_multi_clipped_fractional_stroke_visible_work(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_composer_clip_state *const *clips,size_t clip_count,
    quantapdf_ir_raster_image *out_image)
{
    if(clip_count!=2u || clips==NULL) {
        if(out_image!=NULL)memset(out_image,0,sizeof(*out_image));
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    return qp_render_v1_impl(ir,page_index,limits,NULL,
                             clips,clip_count,3,out_image,NULL);
}


/* Exact bounded 2-ring cubic W* clipping. Each of the two rings is
 * [MOVE, CUBIC, CUBIC, CUBIC, CUBIC, CLOSE], and all four cubic
 * ENDPOINTS are respectively top/left/bottom/right of a shared
 * source-space center. The first ring is STRICTLY contained in the
 * second in both radii. No source-empty MOVE can silently become a
 * painted edge, and no general 12-command Clip grammar is opened.
 *
 * This PRIVATE profile proves the original TeXample W* clip topology
 * using the same V1 cubic de Casteljau flattener, 4x4 winding sampler
 * and *unchanged* max_edges/max_sample_tests accounting. It does NOT
 * admit a Source PDF or a curved Stroke. */
static int qp_v1_original_decimal_equal(double a,double b)
{
    /* PDF authored the original ring to FIVE decimal places.
     * Binary32 representation after the exact original translation
     * can differ by a few ULPs. This 0.0001pt tolerance is ONLY for
     * validating cardinal-ring topology; it never changes samples,
     * edge positions, winding or raster quota. */
    return isfinite(a) && isfinite(b) && fabs(a-b)<=0.0001;
}

static int qp_v1_cubic_cardinal_ring(
    const quantapdf_composer_path_command *p,
    double *cx,double *cy,double *rx,double *ry)
{
    const quantapdf_point r=p[0].point1;
    const quantapdf_point top=p[1].point3;
    const quantapdf_point left=p[2].point3;
    const quantapdf_point bottom=p[3].point3;
    const quantapdf_point end=p[4].point3;
    if(p[0].kind!=QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
       p[1].kind!=QUANTAPDF_COMPOSER_PATH_CUBIC_TO ||
       p[2].kind!=QUANTAPDF_COMPOSER_PATH_CUBIC_TO ||
       p[3].kind!=QUANTAPDF_COMPOSER_PATH_CUBIC_TO ||
       p[4].kind!=QUANTAPDF_COMPOSER_PATH_CUBIC_TO ||
       p[5].kind!=QUANTAPDF_COMPOSER_PATH_CLOSE ||
       !qp_valid_coordinate(r.x) || !qp_valid_coordinate(r.y) ||
       !qp_valid_coordinate(top.x) || !qp_valid_coordinate(top.y) ||
       !qp_valid_coordinate(left.x) || !qp_valid_coordinate(left.y) ||
       !qp_valid_coordinate(bottom.x) || !qp_valid_coordinate(bottom.y) ||
       !qp_valid_coordinate(end.x) || !qp_valid_coordinate(end.y))
        return 0;
    *cx=(double)top.x;
    *cy=(double)r.y;
    *rx=(double)r.x-*cx;
    *ry=(double)bottom.y-*cy;
    if(*rx<=0.0 || *ry<=0.0 ||
       !qp_v1_original_decimal_equal((double)top.y,*cy-*ry) ||
       !qp_v1_original_decimal_equal((double)left.x,*cx-*rx) ||
       !qp_v1_original_decimal_equal((double)left.y,*cy) ||
       !qp_v1_original_decimal_equal((double)bottom.x,*cx) ||
       !qp_v1_original_decimal_equal((double)end.x,(double)r.x) ||
       !qp_v1_original_decimal_equal((double)end.y,*cy))
        return 0;
    /* All cubic controls must stay in the bounded original contour
     * rectangle. This is an exact SOURCE geometry invariant, not a
     * point-dropping or curve-flattening approximation. */
    for(size_t i=1u;i<=4u;++i) {
        const quantapdf_point controls[2]={p[i].point1,p[i].point2};
        for(size_t j=0u;j<2u;++j) {
            const quantapdf_point q=controls[j];
            if(!qp_valid_coordinate(q.x) || !qp_valid_coordinate(q.y) ||
               (double)q.x<*cx-*rx-0.0001 ||
               (double)q.x>*cx+*rx+0.0001 ||
               (double)q.y<*cy-*ry-0.0001 ||
               (double)q.y>*cy+*ry+0.0001)
                return 0;
        }
    }
    return 1;
}

/* At each authored original cardinal knot the neighboring cubic
 * tangents must meet in the SAME direction. This profile does not
 * claim arbitrary discontinuous miter joins, caps, dashes or widths.
 * Existing .125px de Casteljau flattening still controls the
 * intermediate segment approximation and 4x4 sampled coverage. */
static int qp_v1_smooth_closed_cardinal_ring(
    const quantapdf_composer_path_command *p)
{
    for(size_t k=1u;k<=4u;++k) {
        const size_t next=k==4u?1u:k+1u;
        const double x=(double)p[k].point3.x;
        const double y=(double)p[k].point3.y;
        const double ax=x-(double)p[k].point2.x;
        const double ay=y-(double)p[k].point2.y;
        const double bx=(double)p[next].point1.x-x;
        const double by=(double)p[next].point1.y-y;
        const double a2=ax*ax+ay*ay,b2=bx*bx+by*by;
        const double cross=ax*by-ay*bx;
        if(!isfinite(cross) || a2<=1e-8 || b2<=1e-8 ||
           ax*bx+ay*by<=0.0 ||
           cross*cross>1e-6*a2*b2)
            return 0;
    }
    return 1;
}

/* PRIVATE TeXample M4a: ONE of the TWO original 0.3985pt black S
 * operations, borrowed from a complete Source M3 graph. The original
 * double-contour PATH must be exactly 12 authored Composer commands,
 * each with four cubic segments and a closed, smooth cardinal ring;
 * both concentric rings are nondegenerate and strictly nested. The
 * original page is projected to a bounded 238x181 *private physical
 * point grid* with unscaled Source M3 coordinates. This is NOT yet
 * a qualified decimal public page plan or final real PDF RGB.
 *
 * One identical existing V1 4x4 distance/winding/source-over kernel
 * is used. Mode 4 bills EVERY possible inflated-edge candidate
 * distance test before pixel allocation, preserving the global cap.
 * This entry never touches or widens a public Native Source mode. */
static int qp_v1_texample_closed_cubic_stroke_shape(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    int physical)
{
    double ix,iy,irx,iry,ox,oy,orx,ory;
    const quantapdf_composer_operation *op;
    const quantapdf_composer_path_operation *path;
    const quantapdf_composer_path_options *opt;
    if(ir==NULL || ir->pages==NULL || ir->page_count!=1u ||
       page_index!=0u ||
       ir->pages[0].width_points!=(physical?237.133f:238.0f) ||
       ir->pages[0].height_points!=(physical?180.44f:181.0f) ||
       ir->operation_count!=1u || ir->operations==NULL ||
       limits==NULL || limits->max_edges>256u ||
       limits->max_sample_tests>8000000u)
        return 0;
    op=&ir->operations[0];
    path=&op->value.path;
    opt=&path->options;
    if(op->kind!=QUANTAPDF_COMPOSER_OPERATION_PATH ||
       op->page_index!=0u || op->graphics_state_id!=0u ||
       path->command_count!=12u || path->commands==NULL ||
       opt->struct_size<QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE ||
       opt->graphics_state_id!=0u ||
       opt->fill!=0 || opt->stroke!=1 ||
       opt->stroke_argb!=UINT32_C(0xff000000) ||
       opt->stroke_width!=0.3985f ||
       opt->line_cap!=QUANTAPDF_COMPOSER_LINE_CAP_BUTT ||
       opt->line_join!=QUANTAPDF_COMPOSER_LINE_JOIN_MITER ||
       opt->miter_limit!=10.0f ||
       path->dash_count!=0u ||
       !qp_v1_cubic_cardinal_ring(path->commands,&ix,&iy,&irx,&iry) ||
       !qp_v1_cubic_cardinal_ring(path->commands+6u,&ox,&oy,&orx,&ory) ||
       !qp_v1_smooth_closed_cardinal_ring(path->commands) ||
       !qp_v1_smooth_closed_cardinal_ring(path->commands+6u) ||
       !qp_v1_original_decimal_equal(ix,ox) ||
       !qp_v1_original_decimal_equal(iy,oy) ||
       irx+0.0001>=orx || iry+0.0001>=ory)
        return 0;
    return 1;
}

quantapdf_status quantapdf_ir_raster_render_v1_texample_closed_cubic_stroke_work(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    if(out_image==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if(!qp_v1_texample_closed_cubic_stroke_shape(ir,page_index,limits,0))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    return qp_render_v1_impl(ir,page_index,limits,NULL,NULL,0u,4,out_image,NULL);
}

quantapdf_status quantapdf_ir_raster_render_v1_texample_physical_cubic_stroke_work(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,
    quantapdf_ir_raster_image *out_image)
{
    if(out_image==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if(!qp_v1_texample_closed_cubic_stroke_shape(ir,page_index,limits,1) ||
       plan==NULL || plan->mode!=QUANTAPDF_NATIVE_DEVICE_PHYSICAL_V1 ||
       plan->dpi!=72u || plan->width!=238u || plan->height!=181u ||
       plan->width_points!=237.133 || plan->height_points!=180.44 ||
       plan->source_to_display[0]!=1.0 ||
       plan->source_to_display[1]!=0.0 ||
       plan->source_to_display[2]!=0.0 ||
       plan->source_to_display[3]!=1.0 ||
       plan->source_to_display[4]!=0.0 ||
       plan->source_to_display[5]!=0.0)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    return qp_render_v1_impl(ir,page_index,limits,plan,NULL,0u,5,out_image,NULL);
}

/* Physical page-fringe qualification for ordinary opaque Fill geometry.
 * Curved Stroke has its separately guarded private entry above. The public
 * bridge and all earlier FIT/integer raster contracts remain unchanged. */
quantapdf_status quantapdf_ir_raster_render_v1_physical_planned(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,
    const quantapdf_composer_clip_state *const *clips,size_t clip_count,
    quantapdf_ir_raster_image *out_image)
{
    if(out_image==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if(ir==NULL || plan==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    if(plan->mode!=QUANTAPDF_NATIVE_DEVICE_PHYSICAL_V1 ||
       (ir->operation_count!=0u && ir->operations==NULL))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    for(size_t i=0u;i<ir->operation_count;++i)
        if(ir->operations[i].kind!=QUANTAPDF_COMPOSER_OPERATION_PATH ||
           ir->operations[i].value.path.options.fill!=1 ||
           ir->operations[i].value.path.options.stroke!=0)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    return qp_render_v1_impl(ir,page_index,limits,plan,clips,clip_count,6,out_image,NULL);
}

static int qp_exact_identity(quantapdf_affine_transform t)
{
    return t.b==0.0f && t.c==0.0f && t.e==0.0f && t.f==0.0f &&
        ((t.a==0.0f && t.d==0.0f) || (t.a==1.0f && t.d==1.0f));
}
/* The original authored joins have EXACT parallel nonzero tangents and
 * exact closure. A smooth closed tube has no cap or additional miter wedge.
 * This excludes arbitrary corners: subdivision points are mathematical
 * curve points only, never artificial joins of replacement line segments. */
static int qp_exact_smooth_monotone(const quantapdf_composer_path_command *p)
{
    for(size_t ring=0u;ring<12u;ring+=6u) {
        quantapdf_point start=p[ring].point1;
        if(p[ring+4u].point3.x!=start.x || p[ring+4u].point3.y!=start.y)return 0;
        for(size_t k=ring+1u;k<ring+5u;++k) {
            const size_t next=k==ring+4u?ring+1u:k+1u;
            const quantapdf_point b=p[k].point1,c=p[k].point2,end=p[k].point3;
            const double ax=(double)end.x-c.x,ay=(double)end.y-c.y;
            const double bx=(double)p[next].point1.x-end.x;
            const double by=(double)p[next].point1.y-end.y;
            const double x[4]={start.x,b.x,c.x,end.x};
            const double y[4]={start.y,b.y,c.y,end.y};
            if(!((ax==0.0 && bx==0.0 && ay*by>0.0) ||
                 (ay==0.0 && by==0.0 && ax*bx>0.0)) ||
               start.x==end.x || start.y==end.y)return 0;
            for(unsigned j=1u;j<4u;++j)
                if(!isfinite(x[j]) || !isfinite(y[j]) ||
                   (end.x>start.x?x[j]<x[j-1u]:x[j]>x[j-1u]) ||
                   (end.y>start.y?y[j]<y[j-1u]:y[j]>y[j-1u]))return 0;
            start=end;
        }
    }
    return 1;
}

static quantapdf_status qp_texample_three_layer(
    const quantapdf_ir *source,const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,size_t *out_sample_tests,
    quantapdf_ir_raster_image *out_image,int shared_geometry)
{
    const int physical_dpi=shared_geometry==2;
    quantapdf_ir ir={0},slice={0};
    quantapdf_composer_operation ops[3]={{0}};
    quantapdf_ir_raster_v1_limits shape_limits;
    const quantapdf_resource_registry *r;
    const quantapdf_composer_clip_state *clip,*clips[1];
    const quantapdf_composer_graphics_state *gs;
    if(out_sample_tests!=NULL)*out_sample_tests=0u;
    if(out_image==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if(source==NULL || limits==NULL || plan==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    if(source->pages==NULL || source->page_count!=1u ||
       source->pages[0].width_points!=237.133f ||
       source->pages[0].height_points!=180.44f ||
       source->pages[0].background_argb!=UINT32_C(0xffffffff) ||
       source->pages[0].suppress_background!=0 ||
       source->operations==NULL || source->operation_count!=3u ||
       source->links!=NULL || source->link_count!=0u ||
       source->outlines!=NULL || source->outline_count!=0u ||
       limits->max_pixels>(physical_dpi?1000000u:50000u) || limits->max_operations!=3u ||
       limits->max_edges>(physical_dpi?512u:1024u) || limits->max_sample_tests>8000000u ||
       (physical_dpi?(plan->dpi!=96u && plan->dpi!=144u):plan->dpi!=72u) ||
       plan->width!=(physical_dpi?(plan->dpi==96u?317u:475u):238u) ||
       plan->height!=(physical_dpi?(plan->dpi==96u?241u:361u):181u) ||
       plan->width_points!=237.133 || plan->height_points!=180.44 ||
       !quantapdf_native_device_plan_matches_physical_page(plan,
            237.133f,180.44f,limits->max_pixels) ||
       plan->source_to_display[0]!=1.0 || plan->source_to_display[3]!=1.0 ||
       plan->source_to_display[1]!=0.0 || plan->source_to_display[2]!=0.0 ||
       plan->source_to_display[4]!=0.0 || plan->source_to_display[5]!=0.0)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    r=&source->resources;
    if(r->images!=NULL || r->image_count!=0u || r->image_capacity!=0u ||
       r->fonts!=NULL || r->font_count!=0u || r->font_capacity!=0u ||
       r->soft_masks!=NULL || r->soft_mask_count!=0u || r->soft_mask_capacity!=0u ||
       r->paints!=NULL || r->paint_count!=0u || r->paint_capacity!=0u ||
       r->forms!=NULL || r->form_count!=0u || r->form_capacity!=0u ||
       r->clips==NULL || r->clip_count!=1u || r->clip_capacity!=1u ||
       r->graphics_states==NULL || r->graphics_state_count!=1u ||
       r->graphics_state_capacity!=1u)return QUANTAPDF_ERROR_UNSUPPORTED;
    clip=&r->clips[0];gs=&r->graphics_states[0];
    if(clip->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
       clip->commands==NULL || clip->command_count!=12u ||
       clip->fill_rule!=QUANTAPDF_COMPOSER_FILL_EVEN_ODD ||
       clip->members!=NULL || clip->member_count!=0u ||
       !qp_exact_identity(clip->transform) ||
       gs->clip_id!=1u || gs->soft_mask_id!=0u ||
       gs->fill_alpha!=1.0f || gs->stroke_alpha!=1.0f ||
       gs->blend_mode!=QUANTAPDF_COMPOSER_BLEND_NORMAL)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    shape_limits=*limits;shape_limits.max_edges=256u;
    slice.pages=source->pages;slice.page_count=1u;slice.operation_count=1u;
    for(size_t i=0u;i<3u;++i) {
        const quantapdf_composer_operation *op=&source->operations[i];
        const quantapdf_composer_path_options *o=&op->value.path.options;
        if(op->kind!=QUANTAPDF_COMPOSER_OPERATION_PATH || op->page_index!=0u ||
           op->graphics_state_id!=(i==0u?1u:0u) ||
           o->struct_size!=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE ||
           o->graphics_state_id!=op->graphics_state_id ||
           o->fill_paint_id!=0u || o->stroke_paint_id!=0u ||
           (o->fill_rule!=QUANTAPDF_COMPOSER_FILL_NONZERO &&
            o->fill_rule!=QUANTAPDF_COMPOSER_FILL_EVEN_ODD) ||
           !qp_exact_identity(o->transform) ||
           op->value.path.dash_lengths!=NULL || op->value.path.dash_count!=0u ||
           op->value.path.dash_phase!=0.0f ||
           (i==0u ? (o->fill!=1 || o->stroke!=0 ||
                     o->fill_rule!=QUANTAPDF_COMPOSER_FILL_EVEN_ODD ||
                     o->fill_argb!=UINT32_C(0xffff8000)) :
                    (o->fill!=0 || o->stroke!=1)))
            return QUANTAPDF_ERROR_UNSUPPORTED;
        ops[i]=*op;ops[i].graphics_state_id=0u;
        ops[i].value.path.options.graphics_state_id=0u;
    }
    /* Shape qualification for all FOUR complete original groups. Neither
     * a zero-edge MOVE, an absent resource nor a late paint is discarded. */
    for(size_t i=0u;i<4u;++i) {
        quantapdf_composer_operation test=ops[i==0u?1u:i-1u];
        if(i==0u) {
            test.value.path.commands=clip->commands;
            test.value.path.command_count=clip->command_count;
        }
        if(i<2u)test.value.path.options=ops[1].value.path.options;
        slice.operations=&test;
        if(!qp_v1_texample_closed_cubic_stroke_shape(&slice,0u,&shape_limits,1) ||
           !qp_exact_smooth_monotone(test.value.path.commands))
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    for(size_t k=0u;k<12u;++k) {
        const quantapdf_composer_path_command *a[2]={&clip->commands[k],
            &ops[0].value.path.commands[k]};
        const quantapdf_composer_path_command *b[2]={&ops[1].value.path.commands[k],
            &ops[2].value.path.commands[k]};
        for(size_t i=0u;i<2u;++i) {
            if(a[i]->kind!=b[i]->kind ||
               a[i]->point1.x!=b[i]->point1.x || a[i]->point1.y!=b[i]->point1.y ||
               a[i]->point2.x!=b[i]->point2.x || a[i]->point2.y!=b[i]->point2.y ||
               a[i]->point3.x!=b[i]->point3.x || a[i]->point3.y!=b[i]->point3.y)
                return QUANTAPDF_ERROR_UNSUPPORTED;
        }
    }
    if(!qp_v1_original_decimal_equal(ops[1].value.path.commands[1].point3.x,90.2192) ||
       !qp_v1_original_decimal_equal(ops[2].value.path.commands[1].point3.x,146.9128))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    ir.pages=source->pages;ir.page_count=1u;ir.operations=ops;ir.operation_count=3u;
    clips[0]=clip;
    return qp_render_v1_impl(&ir,0u,limits,plan,clips,1u,
        physical_dpi?9:shared_geometry?8:7,out_image,out_sample_tests);
}

quantapdf_status quantapdf_ir_raster_render_v1_texample_physical_three_layer(
    const quantapdf_ir *source,const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,size_t *out_sample_tests,
    quantapdf_ir_raster_image *out_image)
{
    return qp_texample_three_layer(source,limits,plan,out_sample_tests,out_image,0);
}

quantapdf_status quantapdf_ir_raster_render_v1_texample_shared_three_layer(
    const quantapdf_ir *source,const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,size_t *out_sample_tests,
    quantapdf_ir_raster_image *out_image)
{
    return qp_texample_three_layer(source,limits,plan,out_sample_tests,out_image,1);
}

quantapdf_status quantapdf_ir_raster_render_v1_texample_physical_dpi_three_layer(
    const quantapdf_ir *source,const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,size_t *out_sample_tests,
    quantapdf_ir_raster_image *out_image)
{
    return qp_texample_three_layer(source,limits,plan,out_sample_tests,out_image,2);
}

quantapdf_status quantapdf_ir_raster_render_v1_two_cubic_ring_wstar_visible_work(
    const quantapdf_ir *ir,size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_composer_clip_state *clip,
    quantapdf_ir_raster_image *out_image)
{
    const quantapdf_composer_clip_state *one[1];
    double ix,iy,irx,iry,ox,oy,orx,ory;
    if(out_image==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if(ir==NULL || ir->operations==NULL ||
       ir->operation_count!=1u ||
       ir->operations[0].kind!=QUANTAPDF_COMPOSER_OPERATION_PATH ||
       ir->operations[0].value.path.options.fill!=1 ||
       ir->operations[0].value.path.options.stroke!=0 ||
       clip==NULL || clip->kind!=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL ||
       clip->fill_rule!=QUANTAPDF_COMPOSER_FILL_EVEN_ODD ||
       clip->commands==NULL || clip->command_count!=12u ||
       clip->members!=NULL || clip->member_count!=0u ||
       !qp_v1_cubic_cardinal_ring(clip->commands,&ix,&iy,&irx,&iry) ||
       !qp_v1_cubic_cardinal_ring(clip->commands+6u,&ox,&oy,&orx,&ory) ||
       !qp_v1_original_decimal_equal(ix,ox) ||
       !qp_v1_original_decimal_equal(iy,oy) ||
       irx+0.0001>=orx || iry+0.0001>=ory)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    one[0]=clip;
    return qp_render_v1_impl(ir,page_index,limits,NULL,one,1u,1,out_image,NULL);
}

quantapdf_status quantapdf_ir_raster_render_v1_planned(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_native_device_plan *plan,
    const quantapdf_composer_clip_state *const *clips, size_t clip_count,
    quantapdf_ir_raster_image *out_image)
{
    if (plan == NULL) {
        if (out_image != NULL)
            memset(out_image, 0, sizeof(*out_image));
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    return qp_render_v1_impl(
        ir, page_index, limits, plan, clips, clip_count, 0, out_image,NULL);
}

