#include "render/ir_scoped_device_grid_v0.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { \
    fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #e); \
    return EXIT_FAILURE; \
} } while (0)
#define OK(e) CHECK((e) == QUANTAPDF_OK)
#define UNSUPPORTED(e) CHECK((e) == QUANTAPDF_ERROR_UNSUPPORTED)

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[2];
    quantapdf_composer_graphics_state state;
    quantapdf_composer_clip_state clips[3];
    quantapdf_composer_path_command paint[2][5];
    quantapdf_composer_path_command clip_paths[2][5];
    quantapdf_composer_clip_id members[2];
    quantapdf_native_device_grid_geometry geometry;
    quantapdf_native_device_grid_limits grid_limits;
    quantapdf_ir_raster_v1_limits raster_limits;
} fixture;

static void rectangle(quantapdf_composer_path_command *p,
                      float left, float top, float right, float bottom)
{
    const quantapdf_composer_path_command_kind kinds[5] = {
        QUANTAPDF_COMPOSER_PATH_MOVE_TO,
        QUANTAPDF_COMPOSER_PATH_LINE_TO,
        QUANTAPDF_COMPOSER_PATH_LINE_TO,
        QUANTAPDF_COMPOSER_PATH_LINE_TO,
        QUANTAPDF_COMPOSER_PATH_CLOSE
    };
    const quantapdf_point points[5] = {
        {left,top}, {right,top}, {right,bottom}, {left,bottom}, {0,0}
    };
    for (size_t i=0u; i<5u; ++i) {
        memset(p+i, 0, sizeof(*p));
        p[i].kind = kinds[i];
        p[i].point1 = points[i];
    }
}

static uint32_t rgba(const quantapdf_ir_raster_image *image,
                     size_t x, size_t y)
{
    const unsigned char *p = image->pixels + image->stride*y + x*4u;
    return ((uint32_t)p[0]<<24u) | ((uint32_t)p[1]<<16u) |
           ((uint32_t)p[2]<<8u) | (uint32_t)p[3];
}

static void initialize(fixture *f)
{
    memset(f, 0, sizeof(*f));
    f->geometry = (quantapdf_native_device_grid_geometry){
        10.25,8.5,0.0,0.0,1.0,0,0,72.0f
    };
    f->grid_limits = (quantapdf_native_device_grid_limits){
        1024u,4096u,64u,250000u
    };
    f->raster_limits = (quantapdf_ir_raster_v1_limits){
        1024u,2u,64u,250000u
    };
    f->page.width_points = 10.25f;
    f->page.height_points = 8.5f;
    f->page.suppress_background = 1;
    f->ir.pages = &f->page;
    f->ir.page_count = 1u;
    f->ir.operations = f->ops;
    f->ir.operation_count = 2u;
    f->ir.resources.graphics_states = &f->state;
    f->ir.resources.graphics_state_count = 1u;
    f->ir.resources.clips = f->clips;
    f->ir.resources.clip_count = 3u;

    f->state.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    f->state.fill_alpha = f->state.stroke_alpha = 1.0f;
    f->state.clip_id = 3u;

    for (size_t i=0u; i<2u; ++i) {
        f->ops[i].kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
        f->ops[i].value.path.commands = f->paint[i];
        f->ops[i].value.path.command_count = 5u;
        f->ops[i].value.path.options.struct_size =
            QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
        f->ops[i].value.path.options.fill = 1;
        f->ops[i].value.path.options.fill_rule =
            QUANTAPDF_COMPOSER_FILL_NONZERO;
        f->ops[i].value.path.options.fill_argb = i == 0u ?
            UINT32_C(0xffff0000) : UINT32_C(0xff0000ff);
        f->ops[i].value.path.options.transform =
            (quantapdf_affine_transform){1,0,0,1,0,0};
    }
    f->ops[0].graphics_state_id = 1u;
    f->ops[0].value.path.options.graphics_state_id = 1u;
    rectangle(f->paint[0],0.0f,0.0f,10.25f,8.5f);
    rectangle(f->paint[1],7.0f,0.0f,10.25f,8.5f);

    for (size_t i=0u; i<2u; ++i) {
        f->clips[i].kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        f->clips[i].commands = f->clip_paths[i];
        f->clips[i].command_count = 5u;
        f->clips[i].fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
        f->clips[i].transform = (quantapdf_affine_transform){1,0,0,1,0,0};
    }
    rectangle(f->clip_paths[0],0.0f,0.0f,5.0f,8.5f);
    rectangle(f->clip_paths[1],2.0f,0.0f,10.25f,8.5f);
    f->members[0] = 1u;
    f->members[1] = 2u;
    f->clips[2].kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    f->clips[2].members = f->members;
    f->clips[2].member_count = 2u;
}

static int test_device_space_projection(void)
{
    fixture f;
    quantapdf_ir_raster_image first = {0}, again = {0};
    quantapdf_composer_page_state original_page;
    quantapdf_composer_operation original_ops[2];
    quantapdf_composer_clip_state original_clips[3];
    initialize(&f);
    original_page = f.page;
    memcpy(original_ops,f.ops,sizeof(original_ops));
    memcpy(original_clips,f.clips,sizeof(original_clips));

    /* The old API MUST keep the original negative behavior. */
    UNSUPPORTED(quantapdf_ir_raster_scoped_operations_v1(
        &f.ir,0u,&f.raster_limits,&first));
    CHECK(first.pixels == NULL && first.size == 0u && first.width == 0u);

    /* New explicit opt-in: project BOTH fill Paths and two nested Clip leaves
     * to the same nonunit 11x9 device grid before the old C11 sampler runs. */
    OK(quantapdf_ir_raster_scoped_device_grid_v0(
        &f.ir,0u,&f.geometry,&f.grid_limits,&f.raster_limits,&first));
    CHECK(first.pixels != NULL && first.width == 11u && first.height == 9u);
    CHECK(first.stride == 44u && first.size == 396u);
    CHECK(rgba(&first,1u,3u) == UINT32_C(0x00000000)); /* below Clip X */
    CHECK(rgba(&first,3u,3u) == UINT32_C(0xff0000ff)); /* red inside Clip */
    CHECK((rgba(&first,5u,3u) & UINT32_C(0xff)) > 0u); /* scaled Clip edge */
    CHECK((rgba(&first,5u,3u) & UINT32_C(0xff)) < 255u);
    CHECK(rgba(&first,6u,3u) == UINT32_C(0x00000000)); /* past Clip X */
    CHECK(rgba(&first,8u,3u) == UINT32_C(0x0000ffff)); /* blue, no Clip */

    OK(quantapdf_ir_raster_scoped_device_grid_v0(
        &f.ir,0u,&f.geometry,&f.grid_limits,&f.raster_limits,&again));
    CHECK(again.size == first.size &&
          memcmp(first.pixels,again.pixels,first.size) == 0);
    CHECK(memcmp(&f.page,&original_page,sizeof(original_page)) == 0);
    CHECK(memcmp(f.ops,original_ops,sizeof(original_ops)) == 0);
    CHECK(memcmp(f.clips,original_clips,sizeof(original_clips)) == 0);
    quantapdf_ir_raster_image_dispose(&first);
    quantapdf_ir_raster_image_dispose(&again);
    CHECK(first.pixels == NULL && again.pixels == NULL);
    return EXIT_SUCCESS;
}

static int test_source_rebased_path_and_clips(void)
{
    fixture f;
    quantapdf_ir_raster_image normalized={0},rebased={0};
    initialize(&f);
    OK(quantapdf_ir_raster_scoped_device_grid_v0(
        &f.ir,0u,&f.geometry,&f.grid_limits,&f.raster_limits,&normalized));
    /* Identical shape in the PDF source path basis (x,-y) with one
     * compensating (0,page_height) translation on EVERY Path and W Clip.
     * Keep intersection member IDs unchanged. */
    for (size_t op=0u;op<2u;++op) {
        for (size_t c=0u;c<f.ops[op].value.path.command_count;++c) {
            quantapdf_composer_path_command *cmd=
                &f.ops[op].value.path.commands[c];
            if (cmd->kind!=QUANTAPDF_COMPOSER_PATH_CLOSE)
                cmd->point1.y-=8.5f;
        }
        f.ops[op].value.path.options.transform.f=8.5f;
    }
    for (size_t clip=0u;clip<2u;++clip) {
        for (size_t c=0u;c<f.clips[clip].command_count;++c) {
            quantapdf_composer_path_command *cmd=&f.clips[clip].commands[c];
            if (cmd->kind!=QUANTAPDF_COMPOSER_PATH_CLOSE)
                cmd->point1.y-=8.5f;
        }
        f.clips[clip].transform.f=8.5f;
    }
    OK(quantapdf_ir_raster_scoped_device_grid_v0(
        &f.ir,0u,&f.geometry,&f.grid_limits,&f.raster_limits,&rebased));
    CHECK(rebased.width==normalized.width &&
          rebased.height==normalized.height &&
          rebased.size==normalized.size &&
          memcmp(rebased.pixels,normalized.pixels,normalized.size)==0);
    quantapdf_ir_raster_image_dispose(&normalized);
    quantapdf_ir_raster_image_dispose(&rebased);
    return EXIT_SUCCESS;
}

static int test_refusal(void)
{
    fixture f;
    quantapdf_ir_raster_image result;
#define REFUSE(expr) do { \
    memset(&result,0xa5,sizeof(result)); \
    UNSUPPORTED(expr); \
    CHECK(result.pixels == NULL && result.size == 0u && \
          result.width == 0u && result.height == 0u && result.stride == 0u); \
} while (0)
    initialize(&f);
    f.ops[0].value.path.options.fill = 0;
    f.ops[0].value.path.options.stroke = 1;
    REFUSE(quantapdf_ir_raster_scoped_device_grid_v0(
        &f.ir,0u,&f.geometry,&f.grid_limits,&f.raster_limits,&result));
    f.ops[0].value.path.options.fill = 1;
    f.ops[0].value.path.options.stroke = 0;
    f.ops[0].value.path.options.transform.e = 1.0f; /* no general affine */
    REFUSE(quantapdf_ir_raster_scoped_device_grid_v0(
        &f.ir,0u,&f.geometry,&f.grid_limits,&f.raster_limits,&result));
    f.ops[0].value.path.options.transform.e = 0.0f;
    f.clips[0].transform.d = 2.0f; /* no scaled source Clip */
    REFUSE(quantapdf_ir_raster_scoped_device_grid_v0(
        &f.ir,0u,&f.geometry,&f.grid_limits,&f.raster_limits,&result));
    f.clips[0].transform.d = 1.0f;
    f.geometry.user_unit = 2.0; /* explicitly unqualified */
    REFUSE(quantapdf_ir_raster_scoped_device_grid_v0(
        &f.ir,0u,&f.geometry,&f.grid_limits,&f.raster_limits,&result));
    f.geometry.user_unit = 1.0;
    f.geometry.page_width_points = 10.000001;
    REFUSE(quantapdf_ir_raster_scoped_device_grid_v0(
        &f.ir,0u,&f.geometry,&f.grid_limits,&f.raster_limits,&result));
    f.geometry.page_width_points = 10.25;
    f.grid_limits.max_sample_tests = 11u*9u*16u-1u;
    REFUSE(quantapdf_ir_raster_scoped_device_grid_v0(
        &f.ir,0u,&f.geometry,&f.grid_limits,&f.raster_limits,&result));
    f.grid_limits.max_sample_tests = 250000u;
    f.grid_limits.max_rgba_bytes = 395u;
    REFUSE(quantapdf_ir_raster_scoped_device_grid_v0(
        &f.ir,0u,&f.geometry,&f.grid_limits,&f.raster_limits,&result));
    f.grid_limits.max_rgba_bytes = 4096u;
    f.ops[0].value.path.options.fill = 0; /* fail closed before any output */
    REFUSE(quantapdf_ir_raster_scoped_device_grid_v0(
        &f.ir,0u,&f.geometry,&f.grid_limits,&f.raster_limits,&result));
#undef REFUSE
    return EXIT_SUCCESS;
}

int main(void)
{
    CHECK(test_device_space_projection() == EXIT_SUCCESS);
    CHECK(test_source_rebased_path_and_clips() == EXIT_SUCCESS);
    CHECK(test_refusal() == EXIT_SUCCESS);
    puts("native-scoped-grid: fractional Path+Clip projection PASS; original jsPDF not admitted");
    return EXIT_SUCCESS;
}
