#include "render/ir_raster.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #x); \
    return EXIT_FAILURE; \
} } while (0)

static void rect_commands(quantapdf_composer_path_command *c,
                          float x0, float y0, float x1, float y1)
{
    memset(c, 0, sizeof(*c) * 5u);
    c[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    c[0].point1.x = x0; c[0].point1.y = y0;
    c[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    c[1].point1.x = x1; c[1].point1.y = y0;
    c[2].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    c[2].point1.x = x1; c[2].point1.y = y1;
    c[3].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    c[3].point1.x = x0; c[3].point1.y = y1;
    c[4].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
}

static int pixel(const quantapdf_ir_raster_image *image, size_t x, size_t y,
                 uint32_t rgba)
{
    const unsigned char *p = image->pixels + y * image->stride + x * 4u;
    return p[0] == (unsigned char)(rgba >> 24u) &&
           p[1] == (unsigned char)((rgba >> 16u) & 255u) &&
           p[2] == (unsigned char)((rgba >> 8u) & 255u) &&
           p[3] == (unsigned char)(rgba & 255u);
}

int main(void)
{
    quantapdf_composer_page_state page = {0};
    quantapdf_composer_operation operations[2] = {{0}, {0}};
    quantapdf_composer_path_command red[5], blue[5];
    quantapdf_ir ir = {0};
    quantapdf_ir_raster_limits limits = {12, 2};
    quantapdf_ir_raster_image one = {0}, two = {0};
    unsigned char bytes[48];

    page.width_points = 4.0f;
    page.height_points = 3.0f;
    page.background_argb = UINT32_C(0xffffffff);
    ir.pages = &page; ir.page_count = 1;
    ir.operations = operations; ir.operation_count = 2;

    rect_commands(red, 1, 0, 3, 2);
    rect_commands(blue, 2, 1, 4, 3);
    operations[0].kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
    operations[0].value.path.commands = red;
    operations[0].value.path.command_count = 5;
    operations[0].value.path.options.struct_size =
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    operations[0].value.path.options.fill = 1;
    operations[0].value.path.options.fill_argb = UINT32_C(0xffff0000);
    operations[1].kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
    operations[1].value.path.commands = blue;
    operations[1].value.path.command_count = 5;
    operations[1].value.path.options.struct_size =
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    operations[1].value.path.options.fill = 1;
    operations[1].value.path.options.fill_argb = UINT32_C(0xff0000ff);

    CHECK(quantapdf_ir_raster_render_v0(&ir, 0, &limits, &one) == QUANTAPDF_OK);
    CHECK(one.pixels != NULL && one.width == 4 && one.height == 3);
    CHECK(one.stride == 16 && one.size == sizeof(bytes));
    CHECK(pixel(&one, 0, 0, UINT32_C(0xffffffff))); /* background */
    CHECK(pixel(&one, 1, 0, UINT32_C(0xff0000ff))); /* red fill */
    CHECK(pixel(&one, 2, 0, UINT32_C(0xff0000ff)));
    CHECK(pixel(&one, 1, 1, UINT32_C(0xff0000ff)));
    CHECK(pixel(&one, 2, 1, UINT32_C(0x0000ffff))); /* blue over red */
    CHECK(pixel(&one, 3, 2, UINT32_C(0x0000ffff)));
    CHECK(pixel(&one, 0, 2, UINT32_C(0xffffffff)));

    memcpy(bytes, one.pixels, sizeof(bytes));
    CHECK(quantapdf_ir_raster_render_v0(&ir, 0, &limits, &two) == QUANTAPDF_OK);
    CHECK(memcmp(one.pixels, two.pixels, sizeof(bytes)) == 0);
    quantapdf_ir_raster_image_dispose(&two);
    quantapdf_ir_raster_image_dispose(&one);
    CHECK(one.pixels == NULL && one.size == 0u);

    /* Background suppression means transparent RGBA, not opaque white. */
    page.suppress_background = 1;
    CHECK(quantapdf_ir_raster_render_v0(&ir, 0, &limits, &one) == QUANTAPDF_OK);
    CHECK(pixel(&one, 0, 0, UINT32_C(0x00000000)));
    CHECK(pixel(&one, 1, 0, UINT32_C(0xff0000ff)));
    quantapdf_ir_raster_image_dispose(&one);
    page.suppress_background = 0;

    /* Preflight rejects unsupported operations and preserves empty output. */
    one.pixels = (unsigned char *)(uintptr_t)1;
    one.size = SIZE_MAX;
    operations[0].value.path.options.stroke = 1;
    CHECK(quantapdf_ir_raster_render_v0(&ir, 0, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(one.pixels == NULL && one.size == 0u);
    operations[0].value.path.options.stroke = 0;

    operations[0].value.path.options.fill_argb = UINT32_C(0x80ff0000);
    CHECK(quantapdf_ir_raster_render_v0(&ir, 0, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    operations[0].value.path.options.fill_argb = UINT32_C(0xffff0000);

    operations[0].value.path.options.transform.a = 2.0f;
    CHECK(quantapdf_ir_raster_render_v0(&ir, 0, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    operations[0].value.path.options.transform.a = 0.0f;

    red[2].kind = QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    CHECK(quantapdf_ir_raster_render_v0(&ir, 0, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    red[2].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    red[2].point1.x = 2.5f;
    CHECK(quantapdf_ir_raster_render_v0(&ir, 0, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    red[2].point1.x = 3.0f;

    operations[1].kind = QUANTAPDF_COMPOSER_OPERATION_TEXT;
    CHECK(quantapdf_ir_raster_render_v0(&ir, 0, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    operations[1].kind = QUANTAPDF_COMPOSER_OPERATION_PATH;

    ir.resources.clip_count = 1;
    CHECK(quantapdf_ir_raster_render_v0(&ir, 0, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    ir.resources.clip_count = 0;

    limits.max_pixels = 11;
    CHECK(quantapdf_ir_raster_render_v0(&ir, 0, &limits, &one) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    limits.max_pixels = 12;
    limits.max_operations = 1;
    CHECK(quantapdf_ir_raster_render_v0(&ir, 0, &limits, &one) ==
          QUANTAPDF_ERROR_ARGUMENT);
    limits.max_operations = 2;
    CHECK(quantapdf_ir_raster_render_v0(&ir, 1, &limits, &one) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_render_v0(NULL, 0, &limits, &one) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_render_v0(&ir, 0, &limits, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(one.pixels == NULL && one.size == 0u);
    quantapdf_ir_raster_image_dispose(NULL);

    fprintf(stderr, "[quantapdf.native_ir_raster] complete\n");
    return EXIT_SUCCESS;
}
