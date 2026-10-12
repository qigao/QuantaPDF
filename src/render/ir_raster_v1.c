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

static quantapdf_status qp_admit_draw(
    const quantapdf_composer_operation *op, qp_flatten *flat,
    qp_raster_draw *out)
{
    const quantapdf_composer_path_operation *path = &op->value.path;
    const quantapdf_composer_path_options *opt = &path->options;
    quantapdf_status status;
    size_t first = flat->count;
    const int stroke = opt->stroke == 1 && opt->fill == 0;
    if (op->graphics_state_id != 0u ||
        opt->struct_size < QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE ||
        !(stroke || (opt->fill == 1 && opt->stroke == 0)) ||
        ((stroke ? opt->stroke_argb : opt->fill_argb) >> 24u) != 255u ||
        (stroke && (opt->stroke_width != 1.0f ||
            opt->line_cap != QUANTAPDF_COMPOSER_LINE_CAP_BUTT ||
            opt->line_join != QUANTAPDF_COMPOSER_LINE_JOIN_MITER ||
            opt->miter_limit != 10.0f ||
            path->command_count != 4u ||
            path->commands == NULL ||
            path->commands[0].kind != QUANTAPDF_COMPOSER_PATH_MOVE_TO ||
            path->commands[1].kind != QUANTAPDF_COMPOSER_PATH_LINE_TO ||
            path->commands[2].kind != QUANTAPDF_COMPOSER_PATH_LINE_TO ||
            path->commands[3].kind != QUANTAPDF_COMPOSER_PATH_CLOSE)) ||
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
    if (stroke) {
        /* One-pixel distance-based stroke is only correct under an isometry.
         * Reject scaling/shearing instead of silently changing stroke width. */
        const double x2 = flat->a * flat->a + flat->b * flat->b;
        const double y2 = flat->c * flat->c + flat->d * flat->d;
        const double cross = flat->a * flat->c + flat->b * flat->d;
        if (fabs(x2 - 1.0) > 0.000001 ||
            fabs(y2 - 1.0) > 0.000001 || fabs(cross) > 0.000001)
            return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    status = qp_flatten_path(flat, path);
    if (status != QUANTAPDF_OK)
        return status;
    if (flat->count == first || (stroke && flat->count - first != 3u))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    out->first_edge = first;
    out->edge_count = flat->count - first;
    out->color = stroke ? opt->stroke_argb : opt->fill_argb;
    out->rule = opt->fill_rule;
    out->one_pixel_stroke = stroke;
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

/* Bounded one-pixel path stroke. Each antialias sample is covered iff it
 * lies within half a device-space pixel of one of three closed segments.
 * Unsupported widths, caps, joins, dashes and non-isometric transforms are
 * rejected by qp_admit_draw; this cannot silently approximate general strokes. */
static int qp_stroke_inside(const qp_edge *edges,
    const qp_raster_draw *draw, double x, double y)
{
    for (size_t i = 0u; i < draw->edge_count; ++i) {
        const qp_edge *edge = &edges[draw->first_edge + i];
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
        if (ux * ux + uy * uy <= 0.25)
            return 1;
    }
    return 0;
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

static quantapdf_status qp_render_v1_impl(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    const quantapdf_composer_clip_state *const *clips, size_t clip_count,
    quantapdf_ir_raster_image *out_image)
{
    size_t width, height, pixels_count, total_checks = 0u;
    size_t i, draw_count = 0u, x, y, ci;
    qp_flatten flatten = {0};
    qp_raster_draw *draws = NULL;
    qp_raster_draw clip_draws[QUANTAPDF_NATIVE_CLIP_DAG_MAX_LEAVES] = {{0}};
    unsigned char *pixels = NULL;
    quantapdf_status status = QUANTAPDF_ERROR_ARGUMENT;
    const quantapdf_composer_page_state *page;

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
    width = (size_t)page->width_points;
    height = (size_t)page->height_points;
    if (width == 0u || height == 0u ||
        (double)width != (double)page->width_points ||
        (double)height != (double)page->height_points ||
        width > limits->max_pixels / height ||
        page->suppress_background < 0 || page->suppress_background > 1 ||
        (!page->suppress_background && (page->background_argb >> 24u) != 255u) ||
        !qp_no_resources(ir) ||
        (clip_count > 0u && (ir->operation_count != 1u ||
         ir->operations[0].page_index != page_index)))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    pixels_count = width * height;
    if (pixels_count > limits->max_sample_tests / QP_TOTAL_SAMPLES)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    flatten.capacity = limits->max_edges;
    flatten.edges = (qp_edge *)malloc(flatten.capacity * sizeof(qp_edge));
    draws = (qp_raster_draw *)malloc(limits->max_operations * sizeof(qp_raster_draw));
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
        status = qp_admit_draw(op, &flatten, &draws[draw_count]);
        if (status != QUANTAPDF_OK)
            goto cleanup;
        if (draws[draw_count].edge_count >
            (limits->max_sample_tests - total_checks) /
                (QP_TOTAL_SAMPLES * pixels_count)) {
            status = QUANTAPDF_ERROR_UNSUPPORTED;
            goto cleanup;
        }
        total_checks += draws[draw_count].edge_count *
                        (QP_TOTAL_SAMPLES * pixels_count);
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
        status = qp_admit_draw(&clip_op, &flatten, &clip_draws[ci]);
        if (status != QUANTAPDF_OK)
            goto cleanup;
        if (clip_draws[ci].edge_count >
            (limits->max_sample_tests - total_checks) /
                (QP_TOTAL_SAMPLES * pixels_count)) {
            status = QUANTAPDF_ERROR_UNSUPPORTED;
            goto cleanup;
        }
        total_checks += clip_draws[ci].edge_count *
                        (QP_TOTAL_SAMPLES * pixels_count);
    }

    /* The admitted one-pixel stroke remains exact only where the only
     * Clip excludes all closed-triangle joins. No silent stroke fallback. */
    for (i = 0u; i < draw_count; ++i) {
        if (draws[i].one_pixel_stroke &&
            (draw_count != 1u || clip_count != 1u ||
             !qp_guard_clipped_stroke_geometry(flatten.edges,
                 &draws[i], &clip_draws[0]))) {
            status = QUANTAPDF_ERROR_UNSUPPORTED;
            goto cleanup;
        }
    }

    pixels = (unsigned char *)malloc(pixels_count * 4u);
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
        for (y = 0u; y < height; ++y) {
            for (x = 0u; x < width; ++x) {
                unsigned sx, sy, covered = 0u;
                for (sy = 0u; sy < QP_SAMPLES; ++sy) {
                    double py = (double)y +
                        ((double)sy + 0.5) / (double)QP_SAMPLES;
                    for (sx = 0u; sx < QP_SAMPLES; ++sx) {
                        double px = (double)x +
                            ((double)sx + 0.5) / (double)QP_SAMPLES;
                        int visible = draw->one_pixel_stroke ?
                            qp_stroke_inside(flatten.edges, draw, px, py) :
                            qp_inside(flatten.edges, draw, px, py);
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
    status = QUANTAPDF_OK;
cleanup:
    free(pixels);
    free(draws);
    free(flatten.edges);
    return status;
}

quantapdf_status quantapdf_ir_raster_render_v1(
    const quantapdf_ir *ir, size_t page_index,
    const quantapdf_ir_raster_v1_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    return qp_render_v1_impl(ir, page_index, limits, NULL, 0u, out_image);
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
        return qp_render_v1_impl(ir, page_index, limits, one, 1u, out_image);
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
        ir, page_index, limits, clips, clip_count, out_image);
}
