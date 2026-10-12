#include "render/ir_clip_path_v2.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { \
    fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #e); \
    return EXIT_FAILURE; \
} } while (0)
#define OK(e) CHECK((e) == QUANTAPDF_OK)

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation operation;
    quantapdf_composer_graphics_state state;
    quantapdf_composer_clip_state clip;
    quantapdf_composer_path_command paint[10], mask[10];
} fixture;

static uint32_t rgba(const quantapdf_ir_raster_image *image, size_t x, size_t y)
{
    const unsigned char *p = image->pixels + y * image->stride + x * 4u;
    return ((uint32_t)p[0] << 24u) | ((uint32_t)p[1] << 16u) |
           ((uint32_t)p[2] << 8u) | p[3];
}

static void command(quantapdf_composer_path_command *p,
                    quantapdf_composer_path_command_kind kind,
                    float x, float y)
{
    memset(p, 0, sizeof(*p));
    p->kind = kind;
    p->point1 = (quantapdf_point){x, y};
}

static void rectangle(quantapdf_composer_path_command *p,
                      float x0, float y0, float x1, float y1)
{
    command(p, QUANTAPDF_COMPOSER_PATH_MOVE_TO, x0, y0);
    command(p + 1, QUANTAPDF_COMPOSER_PATH_LINE_TO, x1, y0);
    command(p + 2, QUANTAPDF_COMPOSER_PATH_LINE_TO, x1, y1);
    command(p + 3, QUANTAPDF_COMPOSER_PATH_LINE_TO, x0, y1);
    command(p + 4, QUANTAPDF_COMPOSER_PATH_CLOSE, 0, 0);
}

static void triangle(quantapdf_composer_path_command *p)
{
    command(p, QUANTAPDF_COMPOSER_PATH_MOVE_TO, 0, 0);
    command(p + 1, QUANTAPDF_COMPOSER_PATH_LINE_TO, 8, 0);
    command(p + 2, QUANTAPDF_COMPOSER_PATH_LINE_TO, 0, 8);
    command(p + 3, QUANTAPDF_COMPOSER_PATH_CLOSE, 0, 0);
}

static void init(fixture *f)
{
    memset(f, 0, sizeof(*f));
    f->page.width_points = f->page.height_points = 8.0f;
    f->page.background_argb = UINT32_C(0xffffffff);
    f->ir.pages = &f->page;
    f->ir.page_count = 1u;
    f->ir.operations = &f->operation;
    f->ir.operation_count = 1u;
    f->ir.resources.graphics_states = &f->state;
    f->ir.resources.graphics_state_count = 1u;
    f->ir.resources.clips = &f->clip;
    f->ir.resources.clip_count = 1u;
    f->state.fill_alpha = f->state.stroke_alpha = 1.0f;
    f->state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    f->state.clip_id = 1u;
    f->operation.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
    f->operation.graphics_state_id = 1u;
    f->operation.value.path.commands = f->paint;
    f->operation.value.path.command_count = 4u;
    f->operation.value.path.options.struct_size =
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    f->operation.value.path.options.fill = 1;
    f->operation.value.path.options.fill_argb = UINT32_C(0xffff0000);
    f->operation.value.path.options.graphics_state_id = 1u;
    f->operation.value.path.options.transform =
        (quantapdf_affine_transform){1, 0, 0, 1, 0, 0};
    f->clip.kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    f->clip.commands = f->mask;
    f->clip.command_count = 4u;
    f->clip.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    f->clip.transform =
        (quantapdf_affine_transform){1, 0, 0, 1, 0, 0};
    triangle(f->paint);
    triangle(f->mask);
}

static int test_per_sample_intersection(void)
{
    fixture f;
    quantapdf_ir_raster_v1_limits limits = {64, 1, 64, 100000};
    quantapdf_ir_raster_image clipped = {0}, plain = {0};
    quantapdf_ir projected;
    quantapdf_composer_operation op;
    init(&f);
    OK(quantapdf_ir_raster_clip_path_v2(&f.ir, 0, &limits, &clipped));
    /* Identical paint and Clip triangles must produce exactly the same
     * coverage as the un-clipped fill; multiplying averaged masks would
     * produce incorrect transparency on diagonal boundary pixels. */
    projected = f.ir;
    op = f.operation;
    op.graphics_state_id = 0u;
    op.value.path.options.graphics_state_id = 0u;
    projected.operations = &op;
    memset(&projected.resources, 0, sizeof(projected.resources));
    OK(quantapdf_ir_raster_render_v1(&projected, 0, &limits, &plain));
    CHECK(clipped.size == plain.size);
    CHECK(memcmp(clipped.pixels, plain.pixels, plain.size) == 0);
    CHECK(rgba(&clipped, 1, 1) == UINT32_C(0xff0000ff));
    CHECK(rgba(&clipped, 7, 7) == UINT32_C(0xffffffff));
    CHECK(clipped.pixels[4u * (4u * 8u + 3u) + 1u] > 0u);
    CHECK(clipped.pixels[4u * (4u * 8u + 3u) + 1u] < 255u);
    quantapdf_ir_raster_image_dispose(&plain);
    quantapdf_ir_raster_image_dispose(&clipped);
    return EXIT_SUCCESS;
}

static int test_evenodd_hole_and_transform(void)
{
    fixture f;
    quantapdf_ir_raster_v1_limits limits = {64, 1, 64, 100000};
    quantapdf_ir_raster_image image = {0};
    init(&f);
    rectangle(f.paint, 0, 0, 8, 8);
    f.operation.value.path.command_count = 5u;
    rectangle(f.mask, 1, 1, 7, 7);
    rectangle(f.mask + 5, 3, 3, 5, 5);
    f.clip.command_count = 10u;
    f.clip.fill_rule = QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    OK(quantapdf_ir_raster_clip_path_v2(&f.ir, 0, &limits, &image));
    CHECK(rgba(&image, 2, 2) == UINT32_C(0xff0000ff));
    CHECK(rgba(&image, 4, 4) == UINT32_C(0xffffffff));
    CHECK(rgba(&image, 0, 0) == UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&image);

    rectangle(f.mask, 1, 1, 6, 6);
    f.clip.command_count = 5u;
    f.clip.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    f.clip.transform = (quantapdf_affine_transform){1, 0, 0, 1, 0.5f, 0};
    OK(quantapdf_ir_raster_clip_path_v2(&f.ir, 0, &limits, &image));
    CHECK(rgba(&image, 3, 3) == UINT32_C(0xff0000ff));
    CHECK(rgba(&image, 0, 3) == UINT32_C(0xffffffff));
    CHECK(image.pixels[4u * (3u * 8u + 1u) + 1u] > 0u);
    CHECK(image.pixels[4u * (3u * 8u + 1u) + 1u] < 255u);
    quantapdf_ir_raster_image_dispose(&image);

    f.page.suppress_background = 1;
    OK(quantapdf_ir_raster_clip_path_v2(&f.ir, 0, &limits, &image));
    CHECK(rgba(&image, 0, 0) == UINT32_C(0x00000000));
    CHECK(rgba(&image, 3, 3) == UINT32_C(0xff0000ff));
    quantapdf_ir_raster_image_dispose(&image);
    return EXIT_SUCCESS;
}

static int test_rejections(void)
{
    fixture f;
    quantapdf_ir_raster_v1_limits limits = {64, 1, 64, 100000};
    quantapdf_ir_raster_image image = {0};
    init(&f);
    image.pixels = (unsigned char *)(uintptr_t)1;
    image.size = SIZE_MAX;
    f.clip.transform = (quantapdf_affine_transform){0, 0, 0, 0, 0, 0};
    CHECK(quantapdf_ir_raster_clip_path_v2(&f.ir, 0, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.size == 0u);
    f.clip.transform = (quantapdf_affine_transform){1, 0, 0, 1, 0, 0};
    f.state.clip_id = 0;
    CHECK(quantapdf_ir_raster_clip_path_v2(&f.ir, 0, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    f.state.clip_id = 1;
    f.state.fill_alpha = 0.5f;
    CHECK(quantapdf_ir_raster_clip_path_v2(&f.ir, 0, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    f.state.fill_alpha = 1.0f;
    f.clip.kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    CHECK(quantapdf_ir_raster_clip_path_v2(&f.ir, 0, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    f.clip.kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    limits.max_edges = 4u;
    CHECK(quantapdf_ir_raster_clip_path_v2(&f.ir, 0, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    limits.max_edges = 64u;
    limits.max_sample_tests = 100u;
    CHECK(quantapdf_ir_raster_clip_path_v2(&f.ir, 0, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    limits.max_sample_tests = 100000u;
    f.clip.commands[1].point1.x = 1e30f;
    CHECK(quantapdf_ir_raster_clip_path_v2(&f.ir, 0, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.size == 0u);
    return EXIT_SUCCESS;
}

int main(void)
{
    CHECK(test_per_sample_intersection() == EXIT_SUCCESS);
    CHECK(test_evenodd_hole_and_transform() == EXIT_SUCCESS);
    CHECK(test_rejections() == EXIT_SUCCESS);
    fprintf(stderr, "[quantapdf.native_clip_path_v2] complete\n");
    return EXIT_SUCCESS;
}
