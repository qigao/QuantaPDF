#include "render/ir_stroke_v0.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
    return EXIT_FAILURE; \
} } while (0)
#define OK(expr) CHECK((expr) == QUANTAPDF_OK)

static uint32_t rgba_at(
    const quantapdf_ir_raster_image *image, size_t x, size_t y)
{
    const unsigned char *p = image->pixels + image->stride * y + x * 4u;
    return ((uint32_t)p[0] << 24u) | ((uint32_t)p[1] << 16u) |
           ((uint32_t)p[2] << 8u) | p[3];
}

int main(void)
{
    quantapdf_ir ir = {0};
    quantapdf_composer_page_state page = {0};
    quantapdf_composer_operation op = {0};
    quantapdf_composer_path_command cmd[2] = {{0}, {0}};
    quantapdf_ir_raster_v1_limits limits = {144, 1, 32, 1000000};
    quantapdf_ir_raster_image one = {0}, second = {0};
    unsigned char saved[12u * 12u * 4u];

    page.width_points = 12.0f;
    page.height_points = 12.0f;
    page.background_argb = UINT32_C(0xffffffff);
    ir.pages = &page;
    ir.page_count = 1u;
    ir.operations = &op;
    ir.operation_count = 1u;
    op.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
    op.value.path.commands = cmd;
    op.value.path.command_count = 2u;
    op.value.path.options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    op.value.path.options.stroke = 1;
    op.value.path.options.stroke_width = 2.0f;
    op.value.path.options.stroke_argb = UINT32_C(0xffff0000);
    op.value.path.options.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    op.value.path.options.line_join = QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    op.value.path.options.transform = (quantapdf_affine_transform){
        1, 0, 0, 1, 0, 0
    };
    cmd[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    cmd[0].point1 = (quantapdf_point){2, 5};
    cmd[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    cmd[1].point1 = (quantapdf_point){10, 5};

    OK(quantapdf_ir_raster_stroke_butt_v0(&ir, 0u, &limits, &one));
    CHECK(one.size == sizeof(saved) && one.width == 12 && one.height == 12);
    CHECK(rgba_at(&one, 3, 4) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&one, 3, 5) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&one, 3, 3) == UINT32_C(0xffffffff));
    CHECK(rgba_at(&one, 1, 5) == UINT32_C(0xffffffff));
    CHECK(rgba_at(&one, 10, 5) == UINT32_C(0xffffffff));

    memcpy(saved, one.pixels, sizeof(saved));
    OK(quantapdf_ir_raster_stroke_butt_v0(&ir, 0u, &limits, &second));
    CHECK(second.size == one.size && memcmp(saved, second.pixels, one.size) == 0);
    quantapdf_ir_raster_image_dispose(&one);
    quantapdf_ir_raster_image_dispose(&second);

    cmd[0].point1 = (quantapdf_point){2, 2};
    cmd[1].point1 = (quantapdf_point){10, 10};
    page.suppress_background = 1;
    OK(quantapdf_ir_raster_stroke_butt_v0(&ir, 0u, &limits, &one));
    CHECK(rgba_at(&one, 5, 5) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&one, 0, 0) == UINT32_C(0x00000000));
    quantapdf_ir_raster_image_dispose(&one);
    page.suppress_background = 0;

    /* An unadmitted semantic must not produce even partial output. */
    one.pixels = (unsigned char *)(uintptr_t)1;
    one.size = SIZE_MAX;
    op.value.path.options.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    CHECK(quantapdf_ir_raster_stroke_butt_v0(&ir, 0u, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(one.pixels == NULL && one.size == 0u);
    op.value.path.options.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_BUTT;

    op.value.path.options.fill = 1;
    CHECK(quantapdf_ir_raster_stroke_butt_v0(&ir, 0u, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    op.value.path.options.fill = 0;

    op.value.path.options.transform = (quantapdf_affine_transform){
        1, 0, 0.2f, 1, 0, 0
    };
    CHECK(quantapdf_ir_raster_stroke_butt_v0(&ir, 0u, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    op.value.path.options.transform = (quantapdf_affine_transform){
        1, 0, 0, 1, 0, 0
    };

    op.value.path.dash_count = 1u;
    CHECK(quantapdf_ir_raster_stroke_butt_v0(&ir, 0u, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    op.value.path.dash_count = 0u;

    ir.resources.clip_count = 1u;
    CHECK(quantapdf_ir_raster_stroke_butt_v0(&ir, 0u, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    ir.resources.clip_count = 0u;

    op.value.path.options.stroke_width = 0.0f;
    CHECK(quantapdf_ir_raster_stroke_butt_v0(&ir, 0u, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    op.value.path.options.stroke_width = 2.0f;

    cmd[1].point1 = cmd[0].point1;
    CHECK(quantapdf_ir_raster_stroke_butt_v0(&ir, 0u, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    cmd[1].point1 = (quantapdf_point){10, 10};

    limits.max_sample_tests = 1u;
    CHECK(quantapdf_ir_raster_stroke_butt_v0(&ir, 0u, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    limits.max_sample_tests = 1000000u;

    CHECK(quantapdf_ir_raster_stroke_butt_v0(NULL, 0u, &limits, &one) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_stroke_butt_v0(&ir, 1u, &limits, &one) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(one.pixels == NULL && one.size == 0u);
    fprintf(stderr, "[quantapdf.native_ir_stroke_butt_v0] complete\n");
    return EXIT_SUCCESS;
}
