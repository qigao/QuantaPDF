#ifndef QUANTAPDF_RENDER_PLAN_H
#define QUANTAPDF_RENDER_PLAN_H

#include <quantapdf/quantapdf.h>

/* Source -> displayed page coordinates, x'=a*x+c*y+e, y'=b*x+d*y+f.
 * The producer owns page geometry; the planner stores no provider handles. */
typedef struct quantapdf_render_transform {
    double a, b, c, d, e, f;
} quantapdf_render_transform;

/* Completed value: freely copyable, no borrowed storage or cleanup. Fields are
 * private implementation state and must not be modified by consumers. */
typedef struct quantapdf_render_plan {
    int width, height, components, stride;
    size_t size;
    uint32_t background_argb;
    int aligned, start_x, start_y, size_x, size_y;
    double scale, cosine, sine;
    double source_x0, source_y0, pixel_x0, pixel_y0;
} quantapdf_render_plan;

#ifdef __cplusplus
extern "C" {
#endif

/* Extent is in displayed page points; clip may extend outside it. No allocation
 * or provider call. Malformed extent: FORMAT; invalid options/unrepresentable
 * output: ARGUMENT. Clears output first. Inputs and output must not alias. */
quantapdf_status quantapdf_render_plan_create(double page_width, double page_height,
    float dpi, float rotation_degrees, const quantapdf_rect *clip, int alpha,
    quantapdf_render_plan *out_plan);

/* Composes a source transform with a completed plan for a float-based raster
 * consumer. Empty plan: STATE; nonfinite source: FORMAT; unrepresentable result:
 * ARGUMENT, including a singular float result. Output resets first. Inputs and
 * output must not alias. */
quantapdf_status quantapdf_render_plan_map_source(const quantapdf_render_plan *plan,
    const quantapdf_render_transform *source, quantapdf_affine_transform *out_matrix);

#ifdef __cplusplus
}
#endif
#endif
