#include "render/ir_scoped_operations_v0.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { \
    fprintf(stderr, "%s:%d: failed: %s\n",__FILE__,__LINE__,#e); \
    return EXIT_FAILURE; \
} } while (0)
#define OK(e) CHECK((e) == QUANTAPDF_OK)

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation operations[3];
    quantapdf_composer_graphics_state states[2];
    quantapdf_composer_clip_state clips[2];
    quantapdf_composer_path_command shapes[3][5];
    quantapdf_composer_path_command masks[2][5];
} fixture;

static void rect(quantapdf_composer_path_command *p,
                 float left, float top, float right, float bottom)
{
    size_t i;
    static const quantapdf_composer_path_command_kind kinds[5] = {
        QUANTAPDF_COMPOSER_PATH_MOVE_TO, QUANTAPDF_COMPOSER_PATH_LINE_TO,
        QUANTAPDF_COMPOSER_PATH_LINE_TO, QUANTAPDF_COMPOSER_PATH_LINE_TO,
        QUANTAPDF_COMPOSER_PATH_CLOSE
    };
    const quantapdf_point points[5] = {
        {left,top}, {right,top}, {right,bottom}, {left,bottom}, {0,0}
    };
    for (i = 0u; i < 5u; ++i) {
        memset(p + i, 0, sizeof(*p));
        p[i].kind = kinds[i];
        p[i].point1 = points[i];
    }
}

static uint32_t rgba(const quantapdf_ir_raster_image *im, size_t x, size_t y)
{
    const unsigned char *p = im->pixels + y * im->stride + x * 4u;
    return ((uint32_t)p[0] << 24u) | ((uint32_t)p[1] << 16u) |
           ((uint32_t)p[2] << 8u) | p[3];
}

static void setup(fixture *f)
{
    size_t i;
    static const uint32_t colors[3] = {
        UINT32_C(0xffff0000), UINT32_C(0xff0000ff),
        UINT32_C(0xff00ff00)
    };
    memset(f, 0, sizeof(*f));
    f->page.width_points = f->page.height_points = 8.0f;
    f->page.background_argb = UINT32_C(0xffffffff);
    f->ir.pages = &f->page;
    f->ir.page_count = 1u;
    f->ir.operations = f->operations;
    f->ir.operation_count = 3u;
    f->ir.resources.graphics_states = f->states;
    f->ir.resources.graphics_state_count = 2u;
    f->ir.resources.clips = f->clips;
    f->ir.resources.clip_count = 2u;
    for (i = 0u; i < 3u; ++i) {
        quantapdf_composer_operation *op = &f->operations[i];
        op->kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
        op->value.path.commands = f->shapes[i];
        op->value.path.command_count = 5u;
        op->value.path.options.struct_size =
            QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
        op->value.path.options.fill = 1;
        op->value.path.options.fill_argb = colors[i];
        op->value.path.options.transform =
            (quantapdf_affine_transform){1,0,0,1,0,0};
        rect(f->shapes[i], 0, 0, 8, 8);
    }
    rect(f->shapes[2], 4, 4, 7, 7);
    for (i = 0u; i < 2u; ++i) {
        f->operations[i].graphics_state_id =
            (quantapdf_composer_graphics_state_id)(i + 1u);
        f->operations[i].value.path.options.graphics_state_id =
            (quantapdf_composer_graphics_state_id)(i + 1u);
        f->states[i].fill_alpha = f->states[i].stroke_alpha = 1.0f;
        f->states[i].clip_id = (quantapdf_composer_clip_id)(i + 1u);
        f->states[i].blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
        f->clips[i].kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        f->clips[i].commands = f->masks[i];
        f->clips[i].command_count = 5u;
        f->clips[i].fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
        f->clips[i].transform =
            (quantapdf_affine_transform){1,0,0,1,0,0};
    }
    rect(f->masks[0], 1,1,6,6);
    rect(f->masks[1], 3,1,8,6);
}

static int test_scoped_overlay(void)
{
    fixture f;
    quantapdf_ir_raster_v1_limits limits = {64,3,64,1000000};
    quantapdf_ir_raster_image result = {0}, repeat = {0};
    unsigned char reference[8u * 8u * 4u];
    setup(&f);
    OK(quantapdf_ir_raster_scoped_operations_v0(&f.ir,0,&limits,&result));
    CHECK(result.size == sizeof(reference) && result.width == 8);
    CHECK(rgba(&result,0,0) == UINT32_C(0xffffffff));
    CHECK(rgba(&result,2,2) == UINT32_C(0xff0000ff)); /* red in first clip */
    CHECK(rgba(&result,4,2) == UINT32_C(0x0000ffff)); /* blue over red */
    CHECK(rgba(&result,7,2) == UINT32_C(0x0000ffff)); /* second clip only */
    CHECK(rgba(&result,4,4) == UINT32_C(0x00ff00ff)); /* un-clipped green */
    CHECK(rgba(&result,6,6) == UINT32_C(0x00ff00ff));
    CHECK(rgba(&result,2,6) == UINT32_C(0xffffffff));

    memcpy(reference,result.pixels,sizeof(reference));
    OK(quantapdf_ir_raster_scoped_operations_v0(&f.ir,0,&limits,&repeat));
    CHECK(repeat.size == result.size &&
          memcmp(reference,repeat.pixels,sizeof(reference)) == 0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&result);

    /* Transparent output retains normal per-layer source-over semantics. */
    f.page.suppress_background = 1;
    OK(quantapdf_ir_raster_scoped_operations_v0(&f.ir,0,&limits,&result));
    CHECK(rgba(&result,0,0) == UINT32_C(0x00000000));
    CHECK(rgba(&result,2,2) == UINT32_C(0xff0000ff));
    CHECK(rgba(&result,4,2) == UINT32_C(0x0000ffff));
    CHECK(rgba(&result,4,4) == UINT32_C(0x00ff00ff));
    quantapdf_ir_raster_image_dispose(&result);
    f.page.suppress_background = 0;

    /* One fractional clip boundary must not leak blue paint outside it. */
    f.clips[1].transform.e = 0.5f;
    OK(quantapdf_ir_raster_scoped_operations_v0(&f.ir,0,&limits,&result));
    CHECK(rgba(&result,2,2) == UINT32_C(0xff0000ff));
    CHECK(rgba(&result,4,2) == UINT32_C(0x0000ffff));
    CHECK(rgba(&result,0,0) == UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&result);
    return EXIT_SUCCESS;
}

static int test_rejections(void)
{
    fixture f;
    quantapdf_ir_raster_v1_limits limits = {64,3,64,1000000};
    quantapdf_ir_raster_image image = {0};
    setup(&f);
    image.pixels = (unsigned char *)(uintptr_t)1;
    image.size = SIZE_MAX;
    f.states[0].fill_alpha = 0.5f;
    CHECK(quantapdf_ir_raster_scoped_operations_v0(&f.ir,0,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.size == 0u);
    f.states[0].fill_alpha = 1.0f;

    f.clips[1].kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    CHECK(quantapdf_ir_raster_scoped_operations_v0(&f.ir,0,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    f.clips[1].kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;

    f.operations[1].value.path.options.graphics_state_id = 1u;
    CHECK(quantapdf_ir_raster_scoped_operations_v0(&f.ir,0,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    f.operations[1].value.path.options.graphics_state_id = 2u;

    /* The LAST operation is unsupported: prior layers cannot leak out. */
    f.operations[2].value.path.options.stroke = 1;
    CHECK(quantapdf_ir_raster_scoped_operations_v0(&f.ir,0,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.size == 0u);
    f.operations[2].value.path.options.stroke = 0;

    f.operations[1].kind = QUANTAPDF_COMPOSER_OPERATION_FORM;
    CHECK(quantapdf_ir_raster_scoped_operations_v0(&f.ir,0,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    f.operations[1].kind = QUANTAPDF_COMPOSER_OPERATION_PATH;

    f.states[1].clip_id = 3u;
    CHECK(quantapdf_ir_raster_scoped_operations_v0(&f.ir,0,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    f.states[1].clip_id = 2u;

    f.clips[0].transform.a = 0.0f;
    CHECK(quantapdf_ir_raster_scoped_operations_v0(&f.ir,0,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    f.clips[0].transform.a = 1.0f;

    f.ir.operation_count = 5u;
    CHECK(quantapdf_ir_raster_scoped_operations_v0(&f.ir,0,&limits,&image)
          == QUANTAPDF_ERROR_ARGUMENT);
    f.ir.operation_count = 3u;

    limits.max_operations = 2u;
    CHECK(quantapdf_ir_raster_scoped_operations_v0(&f.ir,0,&limits,&image)
          == QUANTAPDF_ERROR_ARGUMENT);
    limits.max_operations = 3u;

    limits.max_sample_tests = 10u;
    CHECK(quantapdf_ir_raster_scoped_operations_v0(&f.ir,0,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    limits.max_sample_tests = 1000000u;

    CHECK(quantapdf_ir_raster_scoped_operations_v0(NULL,0,&limits,&image)
          == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_scoped_operations_v0(&f.ir,0,&limits,NULL)
          == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(image.pixels == NULL && image.size == 0u);
    return EXIT_SUCCESS;
}

int main(void)
{
    CHECK(test_scoped_overlay() == EXIT_SUCCESS);
    CHECK(test_rejections() == EXIT_SUCCESS);
    fprintf(stderr,"[quantapdf.native_scoped_operations_v0] complete\n");
    return EXIT_SUCCESS;
}
