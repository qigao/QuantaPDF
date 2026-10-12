#include "render/ir_clip_intersection_v1.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #x); \
    return EXIT_FAILURE; \
} } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

static void rectangle(quantapdf_composer_path_command *p,
                      float x0, float y0, float x1, float y1)
{
    memset(p, 0, 5u * sizeof(*p));
    p[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    p[0].point1 = (quantapdf_point){x0, y0};
    p[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    p[1].point1 = (quantapdf_point){x1, y0};
    p[2].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    p[2].point1 = (quantapdf_point){x1, y1};
    p[3].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    p[3].point1 = (quantapdf_point){x0, y1};
    p[4].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
}

static uint32_t rgba_at(const quantapdf_ir_raster_image *image,
                        size_t x, size_t y)
{
    const unsigned char *p = image->pixels + y * image->stride + x * 4u;
    return ((uint32_t)p[0] << 24u) |
           ((uint32_t)p[1] << 16u) |
           ((uint32_t)p[2] << 8u) | p[3];
}

int main(void)
{
    quantapdf_ir ir = {0};
    quantapdf_composer_page_state page = {0};
    quantapdf_composer_operation operation = {0};
    quantapdf_composer_graphics_state state = {0}, previous;
    quantapdf_composer_clip_state clips[4] = {{0}};
    quantapdf_composer_path_command paint[5];
    quantapdf_composer_path_command mask0[5], mask1[5], mask2[5];
    quantapdf_composer_clip_id members[3] = {1, 2, 3};
    quantapdf_ir_raster_v1_limits limits = {64, 1, 32, 1000000};
    quantapdf_ir_raster_image a = {0}, b = {0};
    unsigned char output[8u * 8u * 4u];

    page.width_points = page.height_points = 8.0f;
    page.background_argb = UINT32_C(0xffffffff);
    ir.pages = &page;
    ir.page_count = 1u;
    ir.operations = &operation;
    ir.operation_count = 1u;
    ir.resources.graphics_states = &state;
    ir.resources.graphics_state_count = 1u;
    ir.resources.clips = clips;
    ir.resources.clip_count = 3u;

    operation.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
    operation.graphics_state_id = 1u;
    operation.value.path.commands = paint;
    operation.value.path.command_count = 5u;
    operation.value.path.options.struct_size =
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    operation.value.path.options.fill = 1;
    operation.value.path.options.fill_argb = UINT32_C(0xffff0000);
    operation.value.path.options.graphics_state_id = 1u;
    operation.value.path.options.transform =
        (quantapdf_affine_transform){1, 0, 0, 1, 0, 0};
    rectangle(paint, 0, 0, 8, 8);

    state.fill_alpha = state.stroke_alpha = 1.0f;
    state.clip_id = 3u;
    state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;

    clips[0].kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    clips[0].commands = mask0;
    clips[0].command_count = 5u;
    clips[0].transform = (quantapdf_affine_transform){1, 0, 0, 1, 0, 0};
    rectangle(mask0, 1, 1, 6, 6);

    clips[1].kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    clips[1].commands = mask1;
    clips[1].command_count = 5u;
    clips[1].transform = (quantapdf_affine_transform){1, 0, 0, 1, 0, 0};
    rectangle(mask1, 3, 2, 7, 5);

    clips[2].kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    clips[2].members = members;
    clips[2].member_count = 2u;
    previous = state;

    OK(quantapdf_ir_raster_clip_intersection_v1(&ir, 0u, &limits, &a));
    CHECK(a.size == sizeof(output) && a.width == 8 && a.height == 8);
    CHECK(rgba_at(&a, 3, 2) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&a, 5, 4) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&a, 2, 3) == UINT32_C(0xffffffff));
    CHECK(rgba_at(&a, 6, 4) == UINT32_C(0xffffffff));
    CHECK(rgba_at(&a, 3, 5) == UINT32_C(0xffffffff));
    CHECK(memcmp(&state, &previous, sizeof(previous)) == 0);
    CHECK(clips[2].kind == QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL);

    memcpy(output, a.pixels, sizeof(output));
    OK(quantapdf_ir_raster_clip_intersection_v1(&ir, 0u, &limits, &b));
    CHECK(b.size == a.size && memcmp(output, b.pixels, b.size) == 0);
    quantapdf_ir_raster_image_dispose(&a);
    quantapdf_ir_raster_image_dispose(&b);

    page.suppress_background = 1;
    OK(quantapdf_ir_raster_clip_intersection_v1(&ir, 0u, &limits, &a));
    CHECK(rgba_at(&a, 3, 2) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&a, 2, 3) == UINT32_C(0x00000000));
    quantapdf_ir_raster_image_dispose(&a);
    page.suppress_background = 0;

    /* Three-leaf intersection with a different root resource ID. */
    clips[2].kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    clips[2].members = NULL;
    clips[2].member_count = 0u;
    clips[2].commands = mask2;
    clips[2].command_count = 5u;
    clips[2].transform = (quantapdf_affine_transform){1, 0, 0, 1, 0, 0};
    rectangle(mask2, 4, 3, 7, 6);
    clips[3].kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    clips[3].members = members;
    clips[3].member_count = 3u;
    ir.resources.clip_count = 4u;
    state.clip_id = 4u;
    OK(quantapdf_ir_raster_clip_intersection_v1(&ir, 0u, &limits, &a));
    CHECK(rgba_at(&a, 4, 3) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&a, 3, 3) == UINT32_C(0xffffffff));
    CHECK(rgba_at(&a, 6, 4) == UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&a);

    /* All failures must zero caller-visible result, without painting. */
    a.pixels = (unsigned char *)(uintptr_t)1;
    a.size = SIZE_MAX;
    members[2] = 2u; /* duplicate leaf ID */
    CHECK(quantapdf_ir_raster_clip_intersection_v1(&ir, 0u, &limits, &a) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(a.pixels == NULL && a.size == 0u);
    members[2] = 3u;

    members[2] = 4u; /* self-reference (cycle) */
    CHECK(quantapdf_ir_raster_clip_intersection_v1(&ir, 0u, &limits, &a) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    members[2] = 3u;

    members[0] = 0u;
    CHECK(quantapdf_ir_raster_clip_intersection_v1(&ir, 0u, &limits, &a) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    members[0] = 1u;

    clips[1].kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    CHECK(quantapdf_ir_raster_clip_intersection_v1(&ir, 0u, &limits, &a) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    clips[1].kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;

    mask2[2].point1.x = 6.5f;
    CHECK(quantapdf_ir_raster_clip_intersection_v1(&ir, 0u, &limits, &a) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    mask2[2].point1.x = 7.0f;

    clips[0].transform.e = 1.0f;
    CHECK(quantapdf_ir_raster_clip_intersection_v1(&ir, 0u, &limits, &a) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    clips[0].transform.e = 0.0f;

    rectangle(mask2, 6, 6, 8, 8); /* empty intersection */
    CHECK(quantapdf_ir_raster_clip_intersection_v1(&ir, 0u, &limits, &a) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    rectangle(mask2, 4, 3, 7, 6);

    state.fill_alpha = 0.5f;
    CHECK(quantapdf_ir_raster_clip_intersection_v1(&ir, 0u, &limits, &a) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    state.fill_alpha = 1.0f;

    limits.max_sample_tests = 1u;
    CHECK(quantapdf_ir_raster_clip_intersection_v1(&ir, 0u, &limits, &a) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    limits.max_sample_tests = 1000000u;

    CHECK(quantapdf_ir_raster_clip_intersection_v1(&ir, 1u, &limits, &a) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_clip_intersection_v1(NULL, 0u, &limits, &a) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_clip_intersection_v1(&ir, 0u, &limits, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(a.pixels == NULL && a.size == 0u);
    fprintf(stderr, "[quantapdf.native_clip_intersection_v1] complete\n");
    return EXIT_SUCCESS;
}
