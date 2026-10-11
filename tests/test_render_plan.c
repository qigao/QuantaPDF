#include "render/render_plan.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

static const quantapdf_render_transform identity = {1, 0, 0, 1, 0, 0};

static void zeroed(const void *value, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)value;
    size_t i;
    for (i = 0; i < size; ++i) CHECK(bytes[i] == 0);
}

static void point(const quantapdf_affine_transform *matrix, double x, double y,
    double expected_x, double expected_y)
{
    CHECK(fabs(matrix->a * x + matrix->c * y + matrix->e - expected_x) < 0.0001);
    CHECK(fabs(matrix->b * x + matrix->d * y + matrix->f - expected_y) < 0.0001);
}

static void test_extent_clip_and_copy(void)
{
    quantapdf_render_plan plan, saved;
    quantapdf_affine_transform matrix;
    quantapdf_rect clip = {20, 10, 80, 40};
    OK(quantapdf_render_plan_create(200, 100, 72, 0, NULL, 0, &plan));
    CHECK(plan.width == 200 && plan.height == 100 && plan.stride == 600);
    CHECK(plan.size == 60000 && plan.components == 3 && plan.background_argb == 0xffffffffu);
    CHECK(plan.aligned && plan.start_x == 0 && plan.start_y == 0);
    CHECK(plan.size_x == 200 && plan.size_y == 100);
    OK(quantapdf_render_plan_map_source(&plan, &identity, &matrix));
    point(&matrix, 0, 0, 0, 0); point(&matrix, 200, 100, 200, 100);

    OK(quantapdf_render_plan_create(200, 100, 144, 0, &clip, 1, &plan));
    CHECK(plan.width == 120 && plan.height == 60 && plan.stride == 480);
    CHECK(plan.size == 28800 && plan.components == 4 && plan.background_argb == 0);
    CHECK(plan.aligned && plan.start_x == -40 && plan.start_y == -20);
    CHECK(plan.size_x == 400 && plan.size_y == 200);
    saved = plan;
    memset(&plan, 0, sizeof(plan));
    memset(&clip, 0, sizeof(clip));
    OK(quantapdf_render_plan_map_source(&saved, &identity, &matrix));
    point(&matrix, 20, 10, 0, 0); point(&matrix, 80, 40, 120, 60);

    clip = (quantapdf_rect){-10.25f, -20.5f, 9.75f, 9.5f};
    OK(quantapdf_render_plan_create(200, 100, 72, 0, &clip, 0, &plan));
    CHECK(plan.width == 20 && plan.height == 30);
    CHECK(plan.start_x == 10 && plan.start_y == 20);
    OK(quantapdf_render_plan_map_source(&plan, &identity, &matrix));
    point(&matrix, -10.25, -20.5, 0, 0);
    /* Matrix and aligned execution intentionally retain their distinct subpixel
     * rounding; the integer rectangle remains the established raster policy. */
}

static void test_rotation_and_source_mapping(void)
{
    quantapdf_render_plan plan;
    quantapdf_affine_transform matrix;
    quantapdf_rect clip = {20, 10, 80, 40};
    quantapdf_render_transform raw = {1, 0, 0, -1, -10, 110};
    OK(quantapdf_render_plan_create(200, 100, 144, 90, &clip, 0, &plan));
    CHECK(!plan.aligned && plan.width == 60 && plan.height == 120);
    OK(quantapdf_render_plan_map_source(&plan, &identity, &matrix));
    point(&matrix, 20, 10, 60, 0); point(&matrix, 80, 40, 0, 120);

    OK(quantapdf_render_plan_create(200, 100, 72, 90, NULL, 0, &plan));
    OK(quantapdf_render_plan_map_source(&plan, &raw, &matrix));
    point(&matrix, 10, 110, 100, 0); point(&matrix, 210, 10, 0, 200);
    OK(quantapdf_render_plan_create(200, 100, 72, -90, NULL, 0, &plan));
    OK(quantapdf_render_plan_map_source(&plan, &identity, &matrix));
    point(&matrix, 0, 0, 0, 200); point(&matrix, 200, 100, 100, 0);
    OK(quantapdf_render_plan_create(200, 100, 72, 360, NULL, 0, &plan));
    CHECK(plan.aligned && plan.width == 200 && plan.height == 100);

    OK(quantapdf_render_plan_create(2, 1, 72, 45, NULL, 0, &plan));
    CHECK(!plan.aligned && plan.width == 3 && plan.height == 3);
    OK(quantapdf_render_plan_map_source(&plan, &identity, &matrix));
    point(&matrix, 0, 0, 1, 0);
    point(&matrix, 2, 1, 1 + sqrt(0.5), 3 * sqrt(0.5));
    {
        int angle, corner;
        for (angle = -180; angle <= 180; angle += 15) {
            const double xs[] = {20, 80, 20, 80};
            const double ys[] = {10, 10, 40, 40};
            OK(quantapdf_render_plan_create(200, 100, 96, (float)angle, &clip, 0, &plan));
            OK(quantapdf_render_plan_map_source(&plan, &identity, &matrix));
            for (corner = 0; corner < 4; ++corner) {
                double x = matrix.a * xs[corner] + matrix.c * ys[corner] + matrix.e;
                double y = matrix.b * xs[corner] + matrix.d * ys[corner] + matrix.f;
                CHECK(x >= -0.0001 && y >= -0.0001);
                CHECK(x <= plan.width + 0.0001 && y <= plan.height + 0.0001);
            }
        }
    }
}

static void test_rejection_and_reset(void)
{
    quantapdf_render_plan plan;
    quantapdf_affine_transform matrix;
    quantapdf_render_transform source = identity;
    quantapdf_rect clip = {0, 0, 1, 1};
#define FAIL_PLAN(width, height, dpi, rotation, region, alpha, expected) do { \
    memset(&plan, 0xa5, sizeof(plan)); \
    CHECK(quantapdf_render_plan_create(width, height, dpi, rotation, region, alpha, &plan) == expected); \
    zeroed(&plan, sizeof(plan)); \
} while (0)
    CHECK(quantapdf_render_plan_create(1, 1, 72, 0, NULL, 0, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    FAIL_PLAN(0, 1, 72, 0, NULL, 0, QUANTAPDF_ERROR_FORMAT);
    FAIL_PLAN(1, INFINITY, 72, 0, NULL, 0, QUANTAPDF_ERROR_FORMAT);
    FAIL_PLAN(NAN, 1, 72, 0, NULL, 0, QUANTAPDF_ERROR_FORMAT);
    FAIL_PLAN(1, 1, 0, 0, NULL, 0, QUANTAPDF_ERROR_ARGUMENT);
    FAIL_PLAN(1, 1, INFINITY, 0, NULL, 0, QUANTAPDF_ERROR_ARGUMENT);
    FAIL_PLAN(1, 1, 72, NAN, NULL, 0, QUANTAPDF_ERROR_ARGUMENT);
    FAIL_PLAN(1, 1, 72, 0, NULL, 2, QUANTAPDF_ERROR_ARGUMENT);
    clip.x1 = 0;
    FAIL_PLAN(1, 1, 72, 0, &clip, 0, QUANTAPDF_ERROR_ARGUMENT);
    clip.x1 = NAN;
    FAIL_PLAN(1, 1, 72, 0, &clip, 0, QUANTAPDF_ERROR_ARGUMENT);
    FAIL_PLAN(DBL_MAX, 1, FLT_MAX, 45, NULL, 0, QUANTAPDF_ERROR_ARGUMENT);
    FAIL_PLAN((double)INT_MAX, 1, 72, 0, NULL, 1, QUANTAPDF_ERROR_ARGUMENT);
    FAIL_PLAN(1, (double)INT_MAX + 0.25, 72, 0, NULL, 0, QUANTAPDF_ERROR_ARGUMENT);
    clip = (quantapdf_rect){0, 0, 1, 1};
    FAIL_PLAN((double)INT_MAX + 1, 1, 72, 0, &clip, 0, QUANTAPDF_ERROR_ARGUMENT);
    clip = (quantapdf_rect){-1.0e10f, 0, -1.0e10f + 4096, 1};
    FAIL_PLAN(1, 1, 72, 0, &clip, 0, QUANTAPDF_ERROR_ARGUMENT);
#undef FAIL_PLAN
    /* Representable large plans need no raster allocation in this pure stage. */
    OK(quantapdf_render_plan_create(1, 1000000, 72, 0, NULL, 0, &plan));
    CHECK(plan.height == 1000000 && plan.size == 3000000);
    memset(&plan, 0, sizeof(plan));
    memset(&matrix, 0xa5, sizeof(matrix));
    CHECK(quantapdf_render_plan_map_source(&plan, &identity, &matrix) == QUANTAPDF_ERROR_STATE);
    zeroed(&matrix, sizeof(matrix));
    OK(quantapdf_render_plan_create(2, 1, 144, 90, NULL, 0, &plan));
    source.a = NAN;
    CHECK(quantapdf_render_plan_map_source(&plan, &source, &matrix) == QUANTAPDF_ERROR_FORMAT);
    zeroed(&matrix, sizeof(matrix));
    source = identity; source.a = DBL_MAX;
    CHECK(quantapdf_render_plan_map_source(&plan, &source, &matrix) == QUANTAPDF_ERROR_ARGUMENT);
    zeroed(&matrix, sizeof(matrix));
    source = identity; source.a = 0;
    CHECK(quantapdf_render_plan_map_source(&plan, &source, &matrix) == QUANTAPDF_ERROR_ARGUMENT);
    zeroed(&matrix, sizeof(matrix));
    source = identity; source.a = DBL_MIN;
    CHECK(quantapdf_render_plan_map_source(&plan, &source, &matrix) == QUANTAPDF_ERROR_ARGUMENT);
    zeroed(&matrix, sizeof(matrix));
    CHECK(quantapdf_render_plan_map_source(NULL, &identity, &matrix) == QUANTAPDF_ERROR_ARGUMENT);
    zeroed(&matrix, sizeof(matrix));
    CHECK(quantapdf_render_plan_map_source(&plan, NULL, &matrix) == QUANTAPDF_ERROR_ARGUMENT);
    zeroed(&matrix, sizeof(matrix));
    CHECK(quantapdf_render_plan_map_source(&plan, &identity, NULL) == QUANTAPDF_ERROR_ARGUMENT);
}

int main(void)
{
    test_extent_clip_and_copy();
    test_rotation_and_source_mapping();
    test_rejection_and_reset();
    return EXIT_SUCCESS;
}
