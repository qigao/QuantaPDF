#include "render/ir_clip_dag_v3.h"

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
    quantapdf_composer_operation op;
    quantapdf_composer_graphics_state gs;
    quantapdf_composer_clip_state clips[5];
    quantapdf_composer_path_command paint[5];
    quantapdf_composer_path_command paths[3][5];
    quantapdf_composer_clip_id inner[2];
    quantapdf_composer_clip_id outer[2];
} fixture;

static void rectangle(quantapdf_composer_path_command *p,
                      float x0, float y0, float x1, float y1)
{
    const quantapdf_composer_path_command_kind kinds[5] = {
        QUANTAPDF_COMPOSER_PATH_MOVE_TO, QUANTAPDF_COMPOSER_PATH_LINE_TO,
        QUANTAPDF_COMPOSER_PATH_LINE_TO, QUANTAPDF_COMPOSER_PATH_LINE_TO,
        QUANTAPDF_COMPOSER_PATH_CLOSE
    };
    const quantapdf_point points[5] = {
        {x0,y0}, {x1,y0}, {x1,y1}, {x0,y1}, {0,0}
    };
    size_t i;
    for (i = 0u; i < 5u; ++i) {
        memset(p + i, 0, sizeof(*p));
        p[i].kind = kinds[i];
        p[i].point1 = points[i];
    }
}

static uint32_t color(const quantapdf_ir_raster_image *image, size_t x, size_t y)
{
    const unsigned char *p = image->pixels + y * image->stride + x * 4u;
    return ((uint32_t)p[0] << 24u) | ((uint32_t)p[1] << 16u) |
           ((uint32_t)p[2] << 8u) | p[3];
}

static void setup(fixture *f)
{
    size_t i;
    memset(f, 0, sizeof(*f));
    f->page.width_points = f->page.height_points = 8.0f;
    f->page.background_argb = UINT32_C(0xffffffff);
    f->ir.pages = &f->page;
    f->ir.page_count = 1u;
    f->ir.operations = &f->op;
    f->ir.operation_count = 1u;
    f->ir.resources.graphics_states = &f->gs;
    f->ir.resources.graphics_state_count = 1u;
    f->ir.resources.clips = f->clips;
    f->ir.resources.clip_count = 5u;

    f->op.kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
    f->op.graphics_state_id = 1u;
    f->op.value.path.commands = f->paint;
    f->op.value.path.command_count = 5u;
    f->op.value.path.options.struct_size =
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    f->op.value.path.options.fill = 1;
    f->op.value.path.options.fill_argb = UINT32_C(0xffff0000);
    f->op.value.path.options.graphics_state_id = 1u;
    f->op.value.path.options.transform =
        (quantapdf_affine_transform){1,0,0,1,0,0};
    rectangle(f->paint,0,0,8,8);

    f->gs.blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    f->gs.fill_alpha = f->gs.stroke_alpha = 1.0f;
    f->gs.clip_id = 5u;
    for (i = 0u; i < 3u; ++i) {
        quantapdf_composer_clip_state *p = &f->clips[i == 2u ? 3u : i];
        p->kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        p->commands = f->paths[i];
        p->command_count = 5u;
        p->transform = (quantapdf_affine_transform){1,0,0,1,0,0};
    }
    rectangle(f->paths[0],1,1,7,7);
    rectangle(f->paths[1],2,2,6,6);
    rectangle(f->paths[2],3,3,5,5);
    f->inner[0] = 1u;
    f->inner[1] = 2u;
    f->clips[2].kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    f->clips[2].members = f->inner;
    f->clips[2].member_count = 2u;
    f->outer[0] = 3u;
    f->outer[1] = 4u;
    f->clips[4].kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    f->clips[4].members = f->outer;
    f->clips[4].member_count = 2u;
}

static int success_cases(void)
{
    fixture f;
    quantapdf_ir_raster_v1_limits limits = {64u,1u,64u,30000u};
    quantapdf_ir_raster_image image = {0}, again = {0};
    unsigned char saved[8u * 8u * 4u];
    setup(&f);
    OK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image));
    CHECK(image.size == sizeof(saved) && image.width == 8u);
    CHECK(color(&image,3u,3u) == UINT32_C(0xff0000ff));
    CHECK(color(&image,4u,4u) == UINT32_C(0xff0000ff));
    CHECK(color(&image,2u,2u) == UINT32_C(0xffffffff));
    CHECK(color(&image,5u,5u) == UINT32_C(0xffffffff));
    memcpy(saved, image.pixels, sizeof(saved));
    OK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&again));
    CHECK(again.size == image.size &&
          memcmp(saved, again.pixels, image.size) == 0);
    quantapdf_ir_raster_image_dispose(&again);
    quantapdf_ir_raster_image_dispose(&image);

    /* One fractional leaf transform is applied BEFORE intersection; the
     * red on x=3 is only half covered, not multiplied with an averaged
     * postpaint mask. */
    f.clips[3].transform.e = 0.5f;
    OK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image));
    CHECK(image.pixels[4u * (3u*8u+3u) + 1u] > 0u);
    CHECK(image.pixels[4u * (3u*8u+3u) + 1u] < 255u);
    CHECK(color(&image,4u,4u) == UINT32_C(0xff0000ff));
    quantapdf_ir_raster_image_dispose(&image);
    f.clips[3].transform.e = 0.0f;

    f.page.suppress_background = 1;
    OK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image));
    CHECK(color(&image,0u,0u) == UINT32_C(0x00000000));
    CHECK(color(&image,4u,4u) == UINT32_C(0xff0000ff));
    quantapdf_ir_raster_image_dispose(&image);
    f.page.suppress_background = 0;

    /* Shared leaf in two branches (a DAG, not just a tree). Eliminate
     * repeated intersection evaluation without changing semantics. */
    f.ir.resources.clip_count = 4u;
    f.gs.clip_id = 4u;
    f.clips[3] = f.clips[4];
    f.clips[3].members = f.outer;
    f.outer[0] = 3u;
    f.outer[1] = 2u;
    OK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image));
    CHECK(color(&image,3u,3u) == UINT32_C(0xff0000ff));
    CHECK(color(&image,6u,6u) == UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&image);
    return EXIT_SUCCESS;
}


/* Standalone V3 is also required to accept a syntactically valid but
 * zero-area Clip leaf without turning an empty intersection into a refusal.
 * The original IR and its owned leaf commands are borrowed, never mutated. */
static int empty_intersection_cases(void)
{
    fixture f;
    quantapdf_ir_raster_v1_limits limits = {64u,1u,64u,30000u};
    quantapdf_ir_raster_image image = {0}, again = {0};
    unsigned char saved[8u*8u*4u];
    setup(&f);
    f.clips[1].command_count = 1u; /* only a valid MOVE_TO command */
    CHECK(f.paths[1][0].kind == QUANTAPDF_COMPOSER_PATH_MOVE_TO);
    OK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image));
    CHECK(image.size == sizeof(saved));
    CHECK(color(&image,3u,3u) == UINT32_C(0xffffffff));
    CHECK(color(&image,4u,4u) == UINT32_C(0xffffffff));
    memcpy(saved,image.pixels,sizeof(saved));
    OK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&again));
    CHECK(again.size == image.size &&
          memcmp(saved,again.pixels,sizeof(saved)) == 0);
    quantapdf_ir_raster_image_dispose(&image);
    quantapdf_ir_raster_image_dispose(&again);

    f.page.suppress_background = 1;
    f.clips[1].fill_rule = QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    OK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image));
    CHECK(color(&image,3u,3u) == UINT32_C(0x00000000));
    CHECK(color(&image,4u,4u) == UINT32_C(0x00000000));
    quantapdf_ir_raster_image_dispose(&image);

    /* A missing pointer is not an empty Clip; reject and clear output. */
    f.clips[1].commands = NULL;
    image.pixels = (unsigned char *)(uintptr_t)1u;
    image.width = image.height = image.stride = image.size = SIZE_MAX;
    CHECK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.width == 0u && image.height == 0u &&
          image.stride == 0u && image.size == 0u);
    return EXIT_SUCCESS;
}

/* Clip V3 semantics: disjoint but otherwise well-formed W/W* leaves
 * are a VALID empty intersection. Do not confuse their zero coverage
 * with malformed geometry, a missing resource or an unsupported DAG.
 * This is a provider-free C11 test; source-PDF Q restoration remains a
 * separate test under the broader Clip V3 milestone. */
static int disjoint_intersection_cases(void)
{
    fixture f;
    quantapdf_ir_raster_v1_limits limits = {64u,1u,64u,30000u};
    quantapdf_ir_raster_image image = {0}, again = {0};
    unsigned char saved[8u*8u*4u];
    size_t x,y;

    setup(&f);
    /* Third leaf is [3,5] x [3,5]. Move the second leaf to [6,8] x
     * [6,8] so every object is structurally valid but the shared
     * intersection is geometrically empty with a positive gap. */
    rectangle(f.paths[1],6,6,8,8);
    OK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image));
    CHECK(image.size == sizeof(saved) &&
          image.width == 8u && image.height == 8u &&
          image.stride == 8u*4u);
    for (y = 0u; y < 8u; ++y)
        for (x = 0u; x < 8u; ++x)
            CHECK(color(&image,x,y) == UINT32_C(0xffffffff));
    memcpy(saved,image.pixels,sizeof(saved));
    OK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&again));
    CHECK(again.size == image.size &&
          memcmp(saved,again.pixels,sizeof(saved)) == 0);
    quantapdf_ir_raster_image_dispose(&image);
    quantapdf_ir_raster_image_dispose(&again);

    /* Transparency changes the page background, not Clip validity.
     * Even a full-page red Path cannot leak through empty coverage. */
    f.page.suppress_background = 1;
    OK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image));
    for (y = 0u; y < 8u; ++y)
        for (x = 0u; x < 8u; ++x)
            CHECK(color(&image,x,y) == UINT32_C(0x00000000));
    quantapdf_ir_raster_image_dispose(&image);

    /* Preserve the original borrowed geometry: restoring its second
     * leaf should recover the exact red interior without a new owner. */
    f.page.suppress_background = 0;
    rectangle(f.paths[1],2,2,6,6);
    OK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image));
    CHECK(color(&image,3u,3u) == UINT32_C(0xff0000ff));
    CHECK(color(&image,2u,2u) == UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&image);
    return EXIT_SUCCESS;
}

static int rejection_cases(void)
{
    fixture f;
    quantapdf_ir_raster_v1_limits limits = {64u,1u,64u,30000u};
    quantapdf_ir_raster_image image = {0};
    setup(&f);
    image.pixels = (unsigned char *)(uintptr_t)1;
    image.size = SIZE_MAX;
    f.outer[0] = 5u; /* direct cycle */
    CHECK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.size == 0u);
    f.outer[0] = 3u;

    f.outer[1] = 3u; /* same child twice */
    CHECK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    f.outer[1] = 4u;

    f.inner[1] = 5u; /* indirect cycle: root -> inner -> root */
    CHECK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    f.inner[1] = 2u;

    f.inner[0] = 0u;
    CHECK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    f.inner[0] = 1u;

    f.clips[1].transform.a = 0.0f; /* singular Clip leaf */
    CHECK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    f.clips[1].transform.a = 1.0f;

    f.op.value.path.options.stroke = 1;
    CHECK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    f.op.value.path.options.stroke = 0;

    f.ir.resources.clip_count = 6u; /* unowned extra node */
    CHECK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    f.ir.resources.clip_count = 5u;

    limits.max_edges = 4u;
    CHECK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    limits.max_edges = 64u;
    limits.max_sample_tests = 100u;
    CHECK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,&image)
          == QUANTAPDF_ERROR_UNSUPPORTED);
    limits.max_sample_tests = 30000u;
    CHECK(quantapdf_ir_raster_clip_dag_v3(NULL,0u,&limits,&image)
          == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_clip_dag_v3(&f.ir,0u,&limits,NULL)
          == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(image.pixels == NULL && image.size == 0u);
    return EXIT_SUCCESS;
}

int main(void)
{
    CHECK(success_cases() == EXIT_SUCCESS);
    CHECK(empty_intersection_cases() == EXIT_SUCCESS);
    CHECK(disjoint_intersection_cases() == EXIT_SUCCESS);
    CHECK(rejection_cases() == EXIT_SUCCESS);
    fprintf(stderr,"[quantapdf.native_clip_dag_v3] complete\n");
    return EXIT_SUCCESS;
}
