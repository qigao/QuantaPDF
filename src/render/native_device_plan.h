#ifndef QUANTAPDF_NATIVE_DEVICE_PLAN_H
#define QUANTAPDF_NATIVE_DEVICE_PLAN_H

#include <quantapdf/quantapdf.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

/* Private, bounded pure-C11 device geometry planner. Public 72-DPI
 * quarter turns and the native V10 96/144-DPI device-space IR projection
 * use its exact target dimensions and pixel budgets. It never paints,
 * selects providers, decodes images or changes the public ABI. */
#define QUANTAPDF_NATIVE_DEVICE_FIT_V1 1u
#define QUANTAPDF_NATIVE_DEVICE_MAX_PIXELS ((size_t)1000000u)

/* A completed plan owns every value. There are no Graph, source, IR or
 * provider pointers and no cleanup. Consumers must not edit its fields.
 * All affine arrays use x'=a*x+c*y+e, y'=b*x+d*y+f. */
typedef struct quantapdf_native_device_plan {
    size_t width, height, stride, size;
    unsigned dpi, mode;
    double width_points, height_points;
    double source_to_display[6]; /* source PDF -> displayed PDF Y-up */
    double source_to_device[6];  /* source PDF -> integer viewport Y-down */
    double from_composer[6];     /* mapped Composer IR -> viewport Y-down */
} quantapdf_native_device_plan;

#ifdef __cplusplus
extern "C" {
#endif

/* Exact checked decimal endpoint subtraction, Unit/DPI multiplication and
 * integer quotient/remainder ceil precede any floating conversion. Inputs
 * are bounded ASCII boxes (four space-separated decimals, <=95 bytes),
 * tokens <=20 bytes, <=12 fractional digits, significands <=2^53-1.
 * Unit is positive and <=128; DPI is 72,96,144; rotation is 0,90,180,270.
 * Crop must lie inside Media. Work is allocation-free and provider-free.
 * Invalid arguments: ARGUMENT; outside the admitted numeric/geometry/budget
 * envelope: UNSUPPORTED. All failures clear the complete output. Inputs
 * and output must not alias. No retry through approximate arithmetic. */
quantapdf_status quantapdf_native_device_plan_create(
    const char *media_ascii, const char *crop_ascii, const char *unit_ascii,
    int rotation, unsigned dpi, size_t max_pixels,
    quantapdf_native_device_plan *out);

#ifdef __cplusplus
}
#endif

/* Header-only validation keeps the existing separately compiled pure-C11
 * raster tests independent of a planner link dependency. This validates a
 * completed plan, not a new floating-point ceil decision. In particular,
 * output dimensions are never recovered from the float Composer page. */
static inline int quantapdf_native_device_plan_matches_page(
    const quantapdf_native_device_plan *p, float width_points,
    float height_points, size_t max_pixels)
{
    double sx, sy, unit;
    const double *s;
    size_t i;
    if (p == NULL || p->mode != QUANTAPDF_NATIVE_DEVICE_FIT_V1 ||
        (p->dpi != 72u && p->dpi != 96u && p->dpi != 144u) ||
        max_pixels == 0u || p->width == 0u || p->height == 0u ||
        p->width > QUANTAPDF_NATIVE_DEVICE_MAX_PIXELS ||
        p->height > QUANTAPDF_NATIVE_DEVICE_MAX_PIXELS ||
        p->width > QUANTAPDF_NATIVE_DEVICE_MAX_PIXELS / p->height ||
        p->width > max_pixels / p->height ||
        p->width > SIZE_MAX / 4u ||
        p->height > SIZE_MAX / (p->width * 4u) ||
        p->stride != p->width * 4u ||
        p->size != p->stride * p->height ||
        !isfinite(p->width_points) || !isfinite(p->height_points) ||
        p->width_points <= 0.0 || p->height_points <= 0.0 ||
        p->width_points > 65536.0 || p->height_points > 65536.0 ||
        !isfinite(width_points) || !isfinite(height_points) ||
        width_points <= 0.0f || height_points <= 0.0f ||
        width_points != (float)p->width_points ||
        height_points != (float)p->height_points)
        return 0;
    sx = (double)p->width / p->width_points;
    sy = (double)p->height / p->height_points;
    if (!isfinite(sx) || !isfinite(sy) ||
        sx <= 0.0 || sy <= 0.0 || sx > 1000000.0 || sy > 1000000.0)
        return 0;
    for (i = 0u; i < 6u; ++i) {
        if (!isfinite(p->source_to_display[i]) ||
            !isfinite(p->source_to_device[i]) ||
            !isfinite(p->from_composer[i]) ||
            fabs(p->source_to_display[i]) > 1000000.0 ||
            fabs(p->source_to_device[i]) > 1000000.0 ||
            fabs(p->from_composer[i]) > 1000000.0)
            return 0;
    }
    s = p->source_to_display;
    /* Exactly one admitted scaled quarter rotation, without shear. */
    if (s[1] == 0.0 && s[2] == 0.0 && s[0] == s[3])
        unit = fabs(s[0]);
    else if (s[0] == 0.0 && s[3] == 0.0 && s[1] == -s[2])
        unit = fabs(s[1]);
    else
        return 0;
    if (unit <= 0.0 || unit > 128.0)
        return 0;
    if (p->source_to_device[0] != s[0] * sx ||
        p->source_to_device[1] != -s[1] * sy ||
        p->source_to_device[2] != s[2] * sx ||
        p->source_to_device[3] != -s[3] * sy ||
        p->source_to_device[4] != s[4] * sx ||
        p->source_to_device[5] != (p->height_points - s[5]) * sy ||
        p->from_composer[0] != sx || p->from_composer[1] != 0.0 ||
        p->from_composer[2] != 0.0 || p->from_composer[3] != sy ||
        p->from_composer[4] != 0.0 ||
        p->from_composer[5] !=
            (p->height_points - (double)height_points) * sy)
        return 0;
    return 1;
}

#endif
