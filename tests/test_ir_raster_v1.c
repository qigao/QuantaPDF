#include "render/ir_raster_v1.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #x); \
    return EXIT_FAILURE; \
} } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

static void set_point(quantapdf_composer_path_command *cmd,
                      quantapdf_composer_path_command_kind kind,
                      float x, float y)
{
    memset(cmd, 0, sizeof(*cmd));
    cmd->kind = kind;
    cmd->point1.x = x;
    cmd->point1.y = y;
}

static void rectangle(quantapdf_composer_path_command *cmd,
                      float x0, float y0, float x1, float y1, int reverse)
{
    set_point(cmd, QUANTAPDF_COMPOSER_PATH_MOVE_TO, x0, y0);
    if (reverse) {
        set_point(cmd + 1, QUANTAPDF_COMPOSER_PATH_LINE_TO, x0, y1);
        set_point(cmd + 2, QUANTAPDF_COMPOSER_PATH_LINE_TO, x1, y1);
        set_point(cmd + 3, QUANTAPDF_COMPOSER_PATH_LINE_TO, x1, y0);
    } else {
        set_point(cmd + 1, QUANTAPDF_COMPOSER_PATH_LINE_TO, x1, y0);
        set_point(cmd + 2, QUANTAPDF_COMPOSER_PATH_LINE_TO, x1, y1);
        set_point(cmd + 3, QUANTAPDF_COMPOSER_PATH_LINE_TO, x0, y1);
    }
    set_point(cmd + 4, QUANTAPDF_COMPOSER_PATH_CLOSE, 0, 0);
}

static uint32_t rgba_at(const quantapdf_ir_raster_image *image,
                        size_t x, size_t y)
{
    const unsigned char *p = image->pixels + y * image->stride + x * 4u;
    return ((uint32_t)p[0] << 24u) | ((uint32_t)p[1] << 16u) |
           ((uint32_t)p[2] << 8u) | p[3];
}

static int setup(quantapdf_ir *ir, quantapdf_composer_page_state *page,
                 quantapdf_composer_operation *op,
                 quantapdf_composer_path_command *commands,
                 size_t count)
{
    memset(ir, 0, sizeof(*ir));
    memset(page, 0, sizeof(*page));
    memset(op, 0, sizeof(*op));
    page->width_points = 8.0f;
    page->height_points = 8.0f;
    page->background_argb = UINT32_C(0xffffffff);
    ir->pages = page;
    ir->page_count = 1u;
    ir->operations = op;
    ir->operation_count = 1u;
    op->kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
    op->value.path.commands = commands;
    op->value.path.command_count = count;
    op->value.path.options.struct_size =
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    op->value.path.options.fill = 1;
    op->value.path.options.fill_argb = UINT32_C(0xffff0000);
    op->value.path.options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    return 0;
}

static int test_winding_and_subpaths(void)
{
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation op;
    quantapdf_composer_path_command commands[10];
    quantapdf_ir_raster_image image = {0};
    quantapdf_ir_raster_v1_limits limits = {64, 2, 128, 1000000};

    rectangle(commands, 1, 1, 7, 7, 0);
    rectangle(commands + 5, 2, 2, 6, 6, 0);
    setup(&ir, &page, &op, commands, 10u);
    OK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image));
    CHECK(image.width == 8 && image.height == 8 && image.stride == 32);
    CHECK(rgba_at(&image, 0, 0) == UINT32_C(0xffffffff));
    CHECK(rgba_at(&image, 1, 1) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&image, 3, 3) == UINT32_C(0xff0000ff));
    quantapdf_ir_raster_image_dispose(&image);

    op.value.path.options.fill_rule = QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    OK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image));
    CHECK(rgba_at(&image, 1, 1) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&image, 3, 3) == UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&image);

    /* With opposite winding the same center becomes a hole under nonzero. */
    rectangle(commands + 5, 2, 2, 6, 6, 1);
    op.value.path.options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    OK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image));
    CHECK(rgba_at(&image, 3, 3) == UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&image);
    return EXIT_SUCCESS;
}

static int test_coverage_transform_and_curves(void)
{
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation op;
    quantapdf_composer_path_command commands[5];
    quantapdf_ir_raster_image image = {0}, second = {0};
    quantapdf_ir_raster_v1_limits limits = {64, 2, 256, 1000000};
    unsigned char saved[8u * 8u * 4u];

    setup(&ir, &page, &op, commands, 3u);
    set_point(commands, QUANTAPDF_COMPOSER_PATH_MOVE_TO, 0, 0);
    set_point(commands + 1, QUANTAPDF_COMPOSER_PATH_LINE_TO, 8, 0);
    set_point(commands + 2, QUANTAPDF_COMPOSER_PATH_LINE_TO, 0, 8);
    OK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image));
    CHECK(rgba_at(&image, 1, 1) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&image, 7, 7) == UINT32_C(0xffffffff));
    /* Diagonal crosses this pixel. The 4x4 sampler must give partial red. */
    CHECK(image.pixels[4u * (4u * 8u + 3u) + 1u] > 0u);
    CHECK(image.pixels[4u * (4u * 8u + 3u) + 1u] < 255u);
    memcpy(saved, image.pixels, sizeof(saved));
    OK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &second));
    CHECK(memcmp(saved, second.pixels, sizeof(saved)) == 0);
    quantapdf_ir_raster_image_dispose(&image);
    quantapdf_ir_raster_image_dispose(&second);

    page.suppress_background = 1;
    OK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image));
    CHECK(rgba_at(&image, 7, 7) == UINT32_C(0x00000000));
    CHECK(image.pixels[4u * (4u * 8u + 3u) + 3u] > 0u);
    CHECK(image.pixels[4u * (4u * 8u + 3u) + 3u] < 255u);
    quantapdf_ir_raster_image_dispose(&image);
    page.suppress_background = 0;

    /* Cubic arch, filled down to its implicit closing chord. */
    setup(&ir, &page, &op, commands, 2u);
    set_point(commands, QUANTAPDF_COMPOSER_PATH_MOVE_TO, 1, 7);
    memset(commands + 1, 0, sizeof(*commands));
    commands[1].kind = QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    commands[1].point1 = (quantapdf_point){1, 0};
    commands[1].point2 = (quantapdf_point){7, 0};
    commands[1].point3 = (quantapdf_point){7, 7};
    OK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image));
    CHECK(rgba_at(&image, 4, 5) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&image, 0, 0) == UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&image);

    /* 90-degree affine rotation of the 2x3 rectangle. */
    rectangle(commands, 2, 2, 4, 5, 0);
    op.value.path.command_count = 5u;
    op.value.path.options.transform = (quantapdf_affine_transform){
        0, 1, -1, 0, 8, 0
    };
    OK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image));
    CHECK(rgba_at(&image, 4, 2) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&image, 1, 1) == UINT32_C(0xffffffff));
    CHECK(rgba_at(&image, 6, 3) == UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&image);

    /* Skew makes partial coverage without approximating the affine geometry. */
    op.value.path.options.transform = (quantapdf_affine_transform){
        1, 0, 0.5f, 1, 0, 0
    };
    OK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image));
    CHECK(rgba_at(&image, 4, 3) == UINT32_C(0xff0000ff));
    quantapdf_ir_raster_image_dispose(&image);
    return EXIT_SUCCESS;
}

static int test_rejections(void)
{
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation op;
    quantapdf_composer_path_command commands[5];
    quantapdf_ir_raster_image image = {0};
    quantapdf_ir_raster_v1_limits limits = {64, 2, 64, 1000000};
    setup(&ir, &page, &op, commands, 5u);
    rectangle(commands, 1, 1, 7, 7, 0);

    image.pixels = (unsigned char *)(uintptr_t)1u;
    image.size = SIZE_MAX;
    op.value.path.options.stroke = 1;
    CHECK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.size == 0u);
    op.value.path.options.stroke = 0;

    op.value.path.options.fill_argb = UINT32_C(0x80ff0000);
    CHECK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    op.value.path.options.fill_argb = UINT32_C(0xffff0000);

    ir.resources.clip_count = 1;
    CHECK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    ir.resources.clip_count = 0;

    op.value.path.options.transform = (quantapdf_affine_transform){
        0, 0, 0, 1, 2, 0
    };
    CHECK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    memset(&op.value.path.options.transform, 0,
           sizeof(op.value.path.options.transform));

    commands[1].point1.x = NAN;
    CHECK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    commands[1].point1.x = 7;

    limits.max_edges = 2;
    CHECK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    limits.max_edges = 64;
    limits.max_sample_tests = 100;
    CHECK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    limits.max_sample_tests = 1000000;

    op.kind = QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    CHECK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    op.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;

    CHECK(quantapdf_ir_raster_render_v1(&ir, 1, &limits, &image) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_render_v1(NULL, 0, &limits, &image) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_render_v1(&ir, 0, NULL, &image) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(image.pixels == NULL);
    return EXIT_SUCCESS;
}

static int test_concave_and_self_intersecting(void)
{
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation op;
    quantapdf_composer_path_command commands[7];
    quantapdf_ir_raster_image image = {0};
    quantapdf_ir_raster_v1_limits limits = {64, 1, 128, 1000000};

    /* Concave L: its excluded quadrant must remain entirely white. */
    setup(&ir, &page, &op, commands, 7u);
    set_point(commands, QUANTAPDF_COMPOSER_PATH_MOVE_TO, 1, 1);
    set_point(commands + 1, QUANTAPDF_COMPOSER_PATH_LINE_TO, 6, 1);
    set_point(commands + 2, QUANTAPDF_COMPOSER_PATH_LINE_TO, 6, 2);
    set_point(commands + 3, QUANTAPDF_COMPOSER_PATH_LINE_TO, 2, 2);
    set_point(commands + 4, QUANTAPDF_COMPOSER_PATH_LINE_TO, 2, 6);
    set_point(commands + 5, QUANTAPDF_COMPOSER_PATH_LINE_TO, 1, 6);
    set_point(commands + 6, QUANTAPDF_COMPOSER_PATH_CLOSE, 0, 0);
    OK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image));
    CHECK(rgba_at(&image, 1, 4) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&image, 4, 1) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&image, 4, 4) == UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&image);

    /* Self-intersection is admitted and obeys even-odd ray crossing. */
    op.value.path.command_count = 5u;
    op.value.path.options.fill_rule = QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    set_point(commands, QUANTAPDF_COMPOSER_PATH_MOVE_TO, 1, 1);
    set_point(commands + 1, QUANTAPDF_COMPOSER_PATH_LINE_TO, 7, 7);
    set_point(commands + 2, QUANTAPDF_COMPOSER_PATH_LINE_TO, 1, 7);
    set_point(commands + 3, QUANTAPDF_COMPOSER_PATH_LINE_TO, 7, 1);
    set_point(commands + 4, QUANTAPDF_COMPOSER_PATH_CLOSE, 0, 0);
    OK(quantapdf_ir_raster_render_v1(&ir, 0, &limits, &image));
    CHECK(rgba_at(&image, 4, 2) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&image, 0, 4) == UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&image);
    return EXIT_SUCCESS;
}

static int test_open_segment_butt_stroke(void)
{
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation op;
    quantapdf_composer_path_command commands[2];
    quantapdf_ir_raster_image image = {0};
    quantapdf_ir_raster_v1_limits limits = {64u,1u,64u,1000000u};
    const uint32_t white=UINT32_C(0xffffffff);

    set_point(commands,QUANTAPDF_COMPOSER_PATH_MOVE_TO,1.0f,4.0f);
    set_point(commands+1,QUANTAPDF_COMPOSER_PATH_LINE_TO,7.0f,4.0f);
    setup(&ir,&page,&op,commands,2u);
    op.value.path.options.fill=0;
    op.value.path.options.stroke=1;
    op.value.path.options.stroke_argb=UINT32_C(0xffff0000);
    op.value.path.options.stroke_width=1.0f;
    op.value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    op.value.path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    op.value.path.options.miter_limit=10.0f;
    OK(quantapdf_ir_raster_render_v1(&ir,0u,&limits,&image));
    CHECK(rgba_at(&image,3u,3u)!=white);
    CHECK(rgba_at(&image,3u,4u)!=white);
    CHECK(rgba_at(&image,3u,2u)==white);
    CHECK(rgba_at(&image,0u,3u)==white);
    CHECK(rgba_at(&image,7u,3u)==white);
    quantapdf_ir_raster_image_dispose(&image);

    op.value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    CHECK(quantapdf_ir_raster_render_v1(&ir,0u,&limits,&image)==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels==NULL && image.size==0u);
    return EXIT_SUCCESS;
}

int main(void)
{
    CHECK(test_concave_and_self_intersecting() == EXIT_SUCCESS);
    CHECK(test_winding_and_subpaths() == EXIT_SUCCESS);
    CHECK(test_coverage_transform_and_curves() == EXIT_SUCCESS);
    CHECK(test_rejections() == EXIT_SUCCESS);
    CHECK(test_open_segment_butt_stroke() == EXIT_SUCCESS);
    fprintf(stderr, "[quantapdf.native_ir_raster_v1] complete\n");
    return EXIT_SUCCESS;
}
