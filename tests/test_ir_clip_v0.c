#include "render/ir_clip_v0.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
    return EXIT_FAILURE; \
} } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

static void rectangle(quantapdf_composer_path_command *p,
                      float x0, float y0, float x1, float y1)
{
    memset(p, 0, sizeof(*p) * 5u);
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
    return ((uint32_t)p[0] << 24u) | ((uint32_t)p[1] << 16u) |
           ((uint32_t)p[2] << 8u) | p[3];
}

int main(void)
{
    quantapdf_ir ir = {0};
    quantapdf_composer_page_state page = {0};
    quantapdf_composer_operation op = {0};
    quantapdf_composer_graphics_state state = {0};
    quantapdf_composer_clip_state clip = {0};
    quantapdf_composer_path_command paint[5], mask[5];
    quantapdf_ir_raster_v1_limits limits = {64, 2, 32, 1000000};
    quantapdf_ir_raster_image image = {0}, repeated = {0};
    unsigned char saved[8u * 8u * 4u];

    page.width_points = page.height_points = 8.0f;
    page.background_argb = UINT32_C(0xffffffff);
    ir.pages = &page;
    ir.page_count = 1u;
    ir.operations = &op;
    ir.operation_count = 1u;
    ir.resources.graphics_states = &state;
    ir.resources.graphics_state_count = 1u;
    ir.resources.clips = &clip;
    ir.resources.clip_count = 1u;

    op.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
    op.graphics_state_id = 1u;
    op.value.path.commands = paint;
    op.value.path.command_count = 5u;
    op.value.path.options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    op.value.path.options.fill = 1;
    op.value.path.options.fill_argb = UINT32_C(0xffff0000);
    op.value.path.options.graphics_state_id = 1u;
    op.value.path.options.transform =
        (quantapdf_affine_transform){1, 0, 0, 1, 0, 0};
    rectangle(paint, 1, 1, 7, 7);

    state.fill_alpha = state.stroke_alpha = 1.0f;
    state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    state.clip_id = 1u;
    clip.kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    clip.commands = mask;
    clip.command_count = 5u;
    clip.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    clip.transform = (quantapdf_affine_transform){1, 0, 0, 1, 0, 0};
    rectangle(mask, 3, 3, 5, 5);

    OK(quantapdf_ir_raster_clip_rect_v0(&ir, 0u, &limits, &image));
    CHECK(image.width == 8 && image.height == 8 && image.size == sizeof(saved));
    CHECK(rgba_at(&image, 3, 3) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&image, 4, 4) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&image, 2, 2) == UINT32_C(0xffffffff));
    CHECK(rgba_at(&image, 5, 5) == UINT32_C(0xffffffff));
    CHECK(rgba_at(&image, 0, 0) == UINT32_C(0xffffffff));

    memcpy(saved, image.pixels, sizeof(saved));
    OK(quantapdf_ir_raster_clip_rect_v0(&ir, 0u, &limits, &repeated));
    CHECK(repeated.size == image.size &&
          memcmp(repeated.pixels, saved, sizeof(saved)) == 0);
    quantapdf_ir_raster_image_dispose(&image);
    quantapdf_ir_raster_image_dispose(&repeated);

    page.suppress_background = 1;
    OK(quantapdf_ir_raster_clip_rect_v0(&ir, 0u, &limits, &image));
    CHECK(rgba_at(&image, 3, 3) == UINT32_C(0xff0000ff));
    CHECK(rgba_at(&image, 2, 2) == UINT32_C(0x00000000));
    quantapdf_ir_raster_image_dispose(&image);
    page.suppress_background = 0;

    /* Unsupported state, geometry and budgets leave a cleared result. */
    image.pixels = (unsigned char *)(uintptr_t)1;
    image.size = SIZE_MAX;
    state.fill_alpha = 0.5f;
    CHECK(quantapdf_ir_raster_clip_rect_v0(&ir, 0u, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.size == 0u);
    state.fill_alpha = 1.0f;

    clip.kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    CHECK(quantapdf_ir_raster_clip_rect_v0(&ir, 0u, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    clip.kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;

    mask[2].point1.x = 4.5f;
    CHECK(quantapdf_ir_raster_clip_rect_v0(&ir, 0u, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    mask[2].point1.x = 5.0f;

    clip.transform.e = 1.0f;
    CHECK(quantapdf_ir_raster_clip_rect_v0(&ir, 0u, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    clip.transform.e = 0.0f;

    op.value.path.options.stroke = 1;
    CHECK(quantapdf_ir_raster_clip_rect_v0(&ir, 0u, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    op.value.path.options.stroke = 0;

    limits.max_sample_tests = 1u;
    CHECK(quantapdf_ir_raster_clip_rect_v0(&ir, 0u, &limits, &image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    limits.max_sample_tests = 1000000u;

    CHECK(quantapdf_ir_raster_clip_rect_v0(&ir, 1u, &limits, &image) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_clip_rect_v0(NULL, 0u, &limits, &image) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(image.pixels == NULL && image.size == 0u);
    fprintf(stderr, "[quantapdf.native_ir_clip_rect_v0] complete\n");
    return EXIT_SUCCESS;
}
