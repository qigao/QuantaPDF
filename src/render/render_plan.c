#include "render_plan.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static int checked_int(double value, int *out_value)
{
    if (!isfinite(value) || value < (double)INT_MIN || value > (double)INT_MAX)
        return 0;
    *out_value = (int)value;
    return 1;
}

static int checked_float(double value, float *out_value)
{
    if (!isfinite(value) || value < -(double)FLT_MAX || value > (double)FLT_MAX)
        return 0;
    *out_value = (float)value;
    return 1;
}

quantapdf_status quantapdf_render_plan_create(double page_width, double page_height,
    float dpi, float rotation_degrees, const quantapdf_rect *clip, int alpha,
    quantapdf_render_plan *out_plan)
{
    const double rotation_zero_tolerance = 1.0e-12;
    quantapdf_render_plan plan = {0};
    double radians, source_x1, source_y1, source_width, source_height;
    double xs[4], ys[4], min_x, min_y, max_x, max_y;
    size_t stride;
    int i;
    if (out_plan == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_plan, 0, sizeof(*out_plan));
    if (!isfinite(dpi) || dpi <= 0.0f || !isfinite(rotation_degrees) ||
        (alpha != 0 && alpha != 1)) return QUANTAPDF_ERROR_ARGUMENT;
    if (!isfinite(page_width) || !isfinite(page_height) ||
        page_width <= 0.0 || page_height <= 0.0) return QUANTAPDF_ERROR_FORMAT;
    plan.source_x0 = clip == NULL ? 0.0 : clip->x0;
    plan.source_y0 = clip == NULL ? 0.0 : clip->y0;
    source_x1 = clip == NULL ? page_width : clip->x1;
    source_y1 = clip == NULL ? page_height : clip->y1;
    if (!isfinite(plan.source_x0) || !isfinite(plan.source_y0) ||
        !isfinite(source_x1) || !isfinite(source_y1) ||
        source_x1 <= plan.source_x0 || source_y1 <= plan.source_y0)
        return QUANTAPDF_ERROR_ARGUMENT;
    radians = (double)rotation_degrees * 3.14159265358979323846 / 180.0;
    plan.cosine = cos(radians);
    plan.sine = sin(radians);
    if (fabs(plan.cosine) < rotation_zero_tolerance) plan.cosine = 0.0;
    if (fabs(plan.sine) < rotation_zero_tolerance) plan.sine = 0.0;
    plan.scale = (double)dpi / 72.0;
    source_width = (source_x1 - plan.source_x0) * plan.scale;
    source_height = (source_y1 - plan.source_y0) * plan.scale;
    xs[0] = 0.0;
    xs[1] = plan.cosine * source_width;
    xs[2] = -plan.sine * source_height;
    xs[3] = xs[1] + xs[2];
    ys[0] = 0.0;
    ys[1] = plan.sine * source_width;
    ys[2] = plan.cosine * source_height;
    ys[3] = ys[1] + ys[2];
    min_x = max_x = xs[0];
    min_y = max_y = ys[0];
    for (i = 0; i < 4; ++i) {
        if (!isfinite(xs[i]) || !isfinite(ys[i])) return QUANTAPDF_ERROR_ARGUMENT;
        if (xs[i] < min_x) min_x = xs[i];
        if (xs[i] > max_x) max_x = xs[i];
        if (ys[i] < min_y) min_y = ys[i];
        if (ys[i] > max_y) max_y = ys[i];
    }
    plan.pixel_x0 = floor(min_x);
    plan.pixel_y0 = floor(min_y);
    if (!checked_int(ceil(max_x) - plan.pixel_x0, &plan.width) ||
        !checked_int(ceil(max_y) - plan.pixel_y0, &plan.height) ||
        plan.width <= 0 || plan.height <= 0) return QUANTAPDF_ERROR_ARGUMENT;
    plan.components = alpha ? 4 : 3;
    if (plan.width > INT_MAX / plan.components) return QUANTAPDF_ERROR_ARGUMENT;
    stride = (size_t)plan.width * (size_t)plan.components;
    if ((size_t)plan.height > SIZE_MAX / stride) return QUANTAPDF_ERROR_ARGUMENT;
    plan.stride = (int)stride;
    plan.size = stride * (size_t)plan.height;
    plan.background_argb = alpha ? UINT32_C(0) : UINT32_C(0xffffffff);
    plan.aligned = plan.cosine == 1.0 && plan.sine == 0.0;
    if (plan.aligned &&
        (!checked_int(floor(-plan.source_x0 * plan.scale), &plan.start_x) ||
         !checked_int(floor(-plan.source_y0 * plan.scale), &plan.start_y) ||
         !checked_int(ceil(page_width * plan.scale), &plan.size_x) ||
         !checked_int(ceil(page_height * plan.scale), &plan.size_y) ||
         plan.size_x <= 0 || plan.size_y <= 0)) return QUANTAPDF_ERROR_ARGUMENT;
    *out_plan = plan;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_render_plan_map_source(const quantapdf_render_plan *plan,
    const quantapdf_render_transform *source, quantapdf_affine_transform *out_matrix)
{
    quantapdf_affine_transform matrix = {0};
    double scale, cosine, sine;
    if (out_matrix == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_matrix, 0, sizeof(*out_matrix));
    if (plan == NULL || source == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    if (plan->width <= 0 || plan->height <= 0) return QUANTAPDF_ERROR_STATE;
    if (!isfinite(source->a) || !isfinite(source->b) || !isfinite(source->c) ||
        !isfinite(source->d) || !isfinite(source->e) || !isfinite(source->f))
        return QUANTAPDF_ERROR_FORMAT;
    scale = plan->scale; cosine = plan->cosine; sine = plan->sine;
    /* Preserve the established translate/rotate/scale evaluation order. */
    if (!checked_float(scale * (cosine * source->a - sine * source->b), &matrix.a) ||
        !checked_float(scale * (sine * source->a + cosine * source->b), &matrix.b) ||
        !checked_float(scale * (cosine * source->c - sine * source->d), &matrix.c) ||
        !checked_float(scale * (sine * source->c + cosine * source->d), &matrix.d) ||
        !checked_float(scale * (cosine * (source->e - plan->source_x0) -
                               sine * (source->f - plan->source_y0)) - plan->pixel_x0, &matrix.e) ||
        !checked_float(scale * (sine * (source->e - plan->source_x0) +
                               cosine * (source->f - plan->source_y0)) - plan->pixel_y0, &matrix.f))
        return QUANTAPDF_ERROR_ARGUMENT;
    /* A float conversion can collapse a tiny but finite source transform. */
    if ((double)matrix.a * matrix.d - (double)matrix.b * matrix.c == 0.0)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_matrix = matrix;
    return QUANTAPDF_OK;
}
