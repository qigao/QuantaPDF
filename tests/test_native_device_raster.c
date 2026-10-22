#include "render/ir_scoped_operations_v1.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
    return 1; } } while (0)

static void rectangle(quantapdf_composer_path_command out[5],
    float x0, float y0, float x1, float y1)
{
    memset(out, 0, 5u * sizeof(*out));
    out[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    out[1].kind = out[2].kind = out[3].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO;
    out[4].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
    out[0].point1.x = out[3].point1.x = x0;
    out[1].point1.x = out[2].point1.x = x1;
    out[0].point1.y = out[1].point1.y = y0;
    out[2].point1.y = out[3].point1.y = y1;
}

static int empty(const quantapdf_ir_raster_image *image)
{
    return image->pixels == NULL && image->width == 0u &&
        image->height == 0u && image->stride == 0u && image->size == 0u;
}

int main(void)
{
    quantapdf_native_device_plan plan = {0}, original_plan, bad;
    quantapdf_composer_page_state page = {10.5f, 8.25f, 0xffffffffu, 0};
    quantapdf_composer_path_command paint[5], clip_commands[5];
    quantapdf_composer_operation op = {0}, original_op;
    quantapdf_composer_clip_state clip = {0}, original_clip;
    quantapdf_composer_graphics_state gs = {0};
    quantapdf_ir ir = {0};
    quantapdf_ir_raster_v1_limits limits = {1000000u, 4u, 64u, 1000000u};
    quantapdf_ir_raster_image image = {0}, repeat = {0};
    quantapdf_status status;

    CHECK(quantapdf_native_device_plan_create("0 0 10.5 8.25",
        "0 0 10.5 8.25", "1", 0, 72u, 1000000u, &plan) == QUANTAPDF_OK);
    CHECK(plan.width == 11u && plan.height == 9u);
    rectangle(paint, -1.5f, 0.0f, 9.0f, 8.25f);
    rectangle(clip_commands, 2.625f, 2.0625f, 7.875f, 6.1875f);
    op.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
    op.graphics_state_id = 1u;
    op.value.path.commands = paint;
    op.value.path.command_count = 5u;
    op.value.path.options.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    op.value.path.options.fill = 1;
    op.value.path.options.fill_argb = 0xffff0000u;
    op.value.path.options.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    op.value.path.options.graphics_state_id = 1u;
    op.value.path.options.transform.a = op.value.path.options.transform.d = 1.0f;
    /* A later paint CTM must not move the already frozen Clip. */
    op.value.path.options.transform.e = 1.5f;
    clip.kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    clip.commands = clip_commands;
    clip.command_count = 5u;
    clip.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    clip.transform.a = clip.transform.d = 1.0f;
    gs.fill_alpha = gs.stroke_alpha = 1.0f;
    gs.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    gs.clip_id = 1u;
    ir.pages = &page;
    ir.page_count = 1u;
    ir.operations = &op;
    ir.operation_count = 1u;
    ir.resources.graphics_states = &gs;
    ir.resources.graphics_state_count = 1u;
    ir.resources.clips = &clip;
    ir.resources.clip_count = 1u;
    original_plan = plan;
    original_op = op;
    original_clip = clip;

    CHECK(quantapdf_ir_raster_scoped_operations_v1_planned(
        &ir, 0u, &limits, &plan, &image) == QUANTAPDF_OK);
    CHECK(quantapdf_ir_raster_scoped_operations_v1_planned(
        &ir, 0u, &limits, &plan, &repeat) == QUANTAPDF_OK);
    CHECK(image.width == 11u && image.height == 9u && image.stride == 44u &&
        image.size == 396u && memcmp(image.pixels, repeat.pixels, image.size) == 0);
    /* Independent integer eighth-pixel oracle: clipped rectangle boundaries
     * are x=[22,66]/8 and y=[18,54]/8 after fitting the integer viewport. */
    for (size_t y = 0u; y < 9u; ++y) {
        for (size_t x = 0u; x < 11u; ++x) {
            unsigned covered = 0u;
            for (size_t sy = 0u; sy < 4u; ++sy) {
                for (size_t sx = 0u; sx < 4u; ++sx) {
                    size_t px = 8u * x + 2u * sx + 1u;
                    size_t py = 8u * y + 2u * sy + 1u;
                    covered += px >= 22u && px < 66u && py >= 18u && py < 54u;
                }
            }
            const unsigned char *p = image.pixels + (y * 11u + x) * 4u;
            const unsigned white = 255u - (covered * 255u + 8u) / 16u;
            CHECK(p[0] == 255u && p[1] == white && p[2] == white && p[3] == 255u);
        }
    }
    CHECK(memcmp(&plan, &original_plan, sizeof(plan)) == 0);
    CHECK(memcmp(&op, &original_op, sizeof(op)) == 0);
    CHECK(memcmp(&clip, &original_clip, sizeof(clip)) == 0);
    quantapdf_ir_raster_image_dispose(&image);
    quantapdf_ir_raster_image_dispose(&repeat);

    memset(&image, 0xa5, sizeof(image));
    CHECK(quantapdf_ir_raster_scoped_operations_v1(
        &ir, 0u, &limits, &image) == QUANTAPDF_ERROR_UNSUPPORTED && empty(&image));
    for (unsigned i = 0u; i < 7u; ++i) {
        bad = plan;
        switch (i) {
        case 0: bad.width = 0u; break;
        case 1: bad.stride += 4u; break;
        case 2: bad.size -= 1u; break;
        case 3: bad.from_composer[0] = NAN; break;
        case 4: bad.from_composer[3] = 0.0; break;
        case 5: bad.width_points = 11.0; break;
        default: bad.from_composer[5] += 0.5; break;
        }
        memset(&image, 0xa5, sizeof(image));
        status = quantapdf_ir_raster_scoped_operations_v1_planned(
            &ir, 0u, &limits, &bad, &image);
        CHECK(status == QUANTAPDF_ERROR_UNSUPPORTED && empty(&image));
    }
    memset(&image, 0xa5, sizeof(image));
    CHECK(quantapdf_ir_raster_scoped_operations_v1_planned(
        &ir, 0u, &limits, NULL, &image) == QUANTAPDF_ERROR_ARGUMENT && empty(&image));
    limits.max_pixels = 98u;
    CHECK(quantapdf_ir_raster_scoped_operations_v1_planned(
        &ir, 0u, &limits, &plan, &image) == QUANTAPDF_ERROR_UNSUPPORTED && empty(&image));
    limits.max_pixels = 1000000u;
    limits.max_sample_tests = 99u * 16u * 8u - 1u;
    CHECK(quantapdf_ir_raster_scoped_operations_v1_planned(
        &ir, 0u, &limits, &plan, &image) == QUANTAPDF_ERROR_UNSUPPORTED && empty(&image));
    limits.max_sample_tests = 1000000u;
    limits.max_edges = 7u;
    CHECK(quantapdf_ir_raster_scoped_operations_v1_planned(
        &ir, 0u, &limits, &plan, &image) == QUANTAPDF_ERROR_UNSUPPORTED && empty(&image));
    limits.max_edges = 64u;
    page.width_points = 10.75f;
    CHECK(quantapdf_ir_raster_scoped_operations_v1_planned(
        &ir, 0u, &limits, &plan, &image) == QUANTAPDF_ERROR_UNSUPPORTED && empty(&image));
    page.width_points = 10.5f;

    /* No M2 stroke is silently admitted by giving it a device plan. */
    op.value.path.options.fill = 0;
    op.value.path.options.stroke = 1;
    op.value.path.options.stroke_argb = 0xff000000u;
    op.value.path.options.stroke_width = 1.0f;
    op.value.path.options.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    op.value.path.options.line_join = QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    op.value.path.options.miter_limit = 10.0f;
    CHECK(quantapdf_ir_raster_scoped_operations_v1_planned(
        &ir, 0u, &limits, &plan, &image) == QUANTAPDF_ERROR_UNSUPPORTED && empty(&image));
    paint[3].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
    op.value.path.command_count = 4u;
    CHECK(quantapdf_ir_raster_scoped_operations_v1_planned(
        &ir, 0u, &limits, &plan, &image) == QUANTAPDF_ERROR_UNSUPPORTED && empty(&image));
    puts("Native FIT device raster: independent coverage, frozen Clip, repeat, bounds and reset passed");
    return 0;
}
