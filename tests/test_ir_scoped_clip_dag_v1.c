#include "render/ir_scoped_operations_v1.h"

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
    quantapdf_composer_operation ops[3];
    quantapdf_composer_graphics_state states[2];
    quantapdf_composer_clip_state clips[8];
    quantapdf_composer_path_command shapes[3][5];
    quantapdf_composer_path_command paths[3][5];
    quantapdf_composer_clip_id inner[2], outer[2];
} fixture;

static void rectangle(quantapdf_composer_path_command *p,
                      float left, float top, float right, float bottom)
{
    const quantapdf_composer_path_command_kind kinds[5] = {
        QUANTAPDF_COMPOSER_PATH_MOVE_TO, QUANTAPDF_COMPOSER_PATH_LINE_TO,
        QUANTAPDF_COMPOSER_PATH_LINE_TO, QUANTAPDF_COMPOSER_PATH_LINE_TO,
        QUANTAPDF_COMPOSER_PATH_CLOSE
    };
    const quantapdf_point points[5] = {
        {left,top}, {right,top}, {right,bottom}, {left,bottom}, {0,0}
    };
    size_t i;
    for (i = 0u; i < 5u; ++i) {
        memset(p+i, 0, sizeof(*p));
        p[i].kind = kinds[i];
        p[i].point1 = points[i];
    }
}

static uint32_t rgba(const quantapdf_ir_raster_image *im,
                     size_t x, size_t y)
{
    const unsigned char *p = im->pixels + y * im->stride + x * 4u;
    return ((uint32_t)p[0]<<24u) | ((uint32_t)p[1]<<16u) |
           ((uint32_t)p[2]<<8u) | (uint32_t)p[3];
}

static void initialize(fixture *f)
{
    static const uint32_t colors[3] = {
        UINT32_C(0xffff0000), UINT32_C(0xff0000ff),
        UINT32_C(0xff00ff00)
    };
    size_t i;
    memset(f, 0, sizeof(*f));
    f->page.width_points = f->page.height_points = 8.0f;
    f->page.background_argb = UINT32_C(0xffffffff);
    f->ir.pages = &f->page;
    f->ir.page_count = 1u;
    f->ir.operations = f->ops;
    f->ir.operation_count = 3u;
    f->ir.resources.graphics_states = f->states;
    f->ir.resources.graphics_state_count = 2u;
    f->ir.resources.clips = f->clips;
    f->ir.resources.clip_count = 5u;

    for (i = 0u; i < 3u; ++i) {
        f->ops[i].kind = QUANTAPDF_COMPOSER_OPERATION_PATH;
        f->ops[i].value.path.commands = f->shapes[i];
        f->ops[i].value.path.command_count = 5u;
        f->ops[i].value.path.options.struct_size =
            QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
        f->ops[i].value.path.options.fill = 1;
        f->ops[i].value.path.options.fill_argb = colors[i];
        f->ops[i].value.path.options.transform =
            (quantapdf_affine_transform){1,0,0,1,0,0};
    }
    rectangle(f->shapes[0],0,0,8,8);
    rectangle(f->shapes[1],4,0,8,8);
    rectangle(f->shapes[2],0,0,2,2);
    for (i = 0u; i < 2u; ++i) {
        f->ops[i].graphics_state_id =
            (quantapdf_composer_graphics_state_id)(i+1u);
        f->ops[i].value.path.options.graphics_state_id =
            (quantapdf_composer_graphics_state_id)(i+1u);
        f->states[i].fill_alpha = f->states[i].stroke_alpha = 1.0f;
        f->states[i].blend_mode = QUANTAPDF_COMPOSER_BLEND_NORMAL;
    }
    f->states[0].clip_id = 5u; /* nested root */
    f->states[1].clip_id = 1u; /* leaf shared with nested root */
    for (i = 0u; i < 3u; ++i) {
        quantapdf_composer_clip_state *clip = &f->clips[i == 2u ? 3u : i];
        clip->kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        clip->commands = f->paths[i];
        clip->command_count = 5u;
        clip->fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
        clip->transform = (quantapdf_affine_transform){1,0,0,1,0,0};
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

static int test_scoped_dag(void)
{
    fixture f;
    const quantapdf_ir_raster_v1_limits budget = {64u,3u,192u,120000u};
    quantapdf_ir_raster_image image = {0}, repeated = {0};
    unsigned char bytes[8u*8u*4u];
    initialize(&f);
    OK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image));
    CHECK(image.size == sizeof(bytes) && image.width == 8u);
    CHECK(rgba(&image,0,0) == UINT32_C(0x00ff00ff)); /* green outside clips */
    CHECK(rgba(&image,3,3) == UINT32_C(0xff0000ff)); /* first scoped red */
    CHECK(rgba(&image,4,4) == UINT32_C(0x0000ffff)); /* blue over red */
    CHECK(rgba(&image,6,6) == UINT32_C(0x0000ffff)); /* blue outside root */
    CHECK(rgba(&image,2,2) == UINT32_C(0xffffffff));
    CHECK(rgba(&image,7,7) == UINT32_C(0xffffffff));
    memcpy(bytes,image.pixels,sizeof(bytes));
    OK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&repeated));
    CHECK(repeated.size == image.size &&
          memcmp(bytes,repeated.pixels,sizeof(bytes)) == 0);
    quantapdf_ir_raster_image_dispose(&image);
    quantapdf_ir_raster_image_dispose(&repeated);

    /* Reverse two scoped draws: red now owns the overlapping x=4 pixel. */
    {
        quantapdf_composer_operation temp = f.ops[0];
        f.ops[0] = f.ops[1];
        f.ops[1] = temp;
    }
    OK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image));
    CHECK(rgba(&image,4,4) == UINT32_C(0xff0000ff));
    CHECK(rgba(&image,6,6) == UINT32_C(0x0000ffff));
    CHECK(rgba(&image,0,0) == UINT32_C(0x00ff00ff));
    quantapdf_ir_raster_image_dispose(&image);
    return EXIT_SUCCESS;
}

/* Single opaque PATH is now admitted without artificial second operations.
 * This pure C11 gate must stay independent of any PDFium/qpdf source. */
static int test_single_opaque_path(void)
{
    fixture f;
    const quantapdf_ir_raster_v1_limits budget = {64u,1u,192u,120000u};
    quantapdf_ir_raster_image image = {0}, repeated = {0};
    initialize(&f);
    f.ir.operation_count = 1u;
    f.ir.resources.graphics_state_count = 1u;
    f.ir.resources.clip_count = 0u;
    f.ir.resources.clips = NULL;
    f.states[0].clip_id = 0u;
    f.page.suppress_background = 1;
    OK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image));
    CHECK(image.size == 8u*8u*4u && image.width==8u && image.height==8u);
    CHECK(rgba(&image,0u,0u) == UINT32_C(0xff0000ff));
    CHECK(rgba(&image,7u,7u) == UINT32_C(0xff0000ff));
    OK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&repeated));
    CHECK(repeated.size == image.size &&
          memcmp(image.pixels,repeated.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&image);
    quantapdf_ir_raster_image_dispose(&repeated);

    f.ir.resources.graphics_state_count = 2u; /* unused state rejected */
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels==NULL && image.size==0u);
    f.ir.resources.graphics_state_count = 1u;
    f.ir.operation_count = 0u; /* empty path list is not a scoped draw */
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(image.pixels==NULL && image.size==0u);
    return EXIT_SUCCESS;
}


/* Provider-free reproduction of the independently published one-pixel,
 * closed triangle stroke inside a rectangular PDF W n clip. */
static int test_clipped_one_pixel_stroke(void)
{
    fixture f;
    const quantapdf_ir_raster_v1_limits budget = {5000u,1u,32u,1500000u};
    quantapdf_ir_raster_image result = {0}, repeated = {0};
    size_t blue_samples = 0u;
    initialize(&f);
    f.page.width_points = 100.0f;
    f.page.height_points = 50.0f;
    f.page.suppress_background = 1;
    f.ir.operation_count = 1u;
    f.ir.resources.graphics_state_count = 1u;
    f.ir.resources.clip_count = 1u;
    f.states[0].clip_id = 1u;
    f.states[0].fill_alpha = f.states[0].stroke_alpha = 1.0f;
    f.clips[0].kind = QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    f.clips[0].commands = f.paths[0];
    f.clips[0].command_count = 4u;
    f.clips[0].fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    f.clips[0].transform = (quantapdf_affine_transform){1,0,0,1,0,0};
    rectangle(f.paths[0],10,15,40,35);
    f.ops[0].value.path.command_count = 4u;
    f.ops[0].value.path.options.fill = 0;
    f.ops[0].value.path.options.stroke = 1;
    f.ops[0].value.path.options.stroke_argb = UINT32_C(0xff0000ff);
    f.ops[0].value.path.options.stroke_width = 1.0f;
    f.ops[0].value.path.options.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    f.ops[0].value.path.options.line_join = QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    f.ops[0].value.path.options.miter_limit = 10.0f;
    rectangle(f.shapes[0],10,10,40,40);
    f.shapes[0][0].point1 = (quantapdf_point){10,40};
    f.shapes[0][1].point1 = (quantapdf_point){25,10};
    f.shapes[0][2].point1 = (quantapdf_point){40,40};
    f.shapes[0][3].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
    OK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&result));
    OK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&repeated));
    CHECK(result.width==100u && result.height==50u &&
          result.size==100u*50u*4u &&
          result.size==repeated.size &&
          memcmp(result.pixels,repeated.pixels,result.size)==0);
    CHECK(rgba(&result,0u,0u)==0u);
    CHECK(rgba(&result,25u,25u)==0u);
    for (size_t i=0u;i<100u*50u;++i)
        if (result.pixels[4u*i+3u]!=0u &&
            result.pixels[4u*i+2u]>=128u) ++blue_samples;
    CHECK(blue_samples>12u);
    quantapdf_ir_raster_image_dispose(&result);
    quantapdf_ir_raster_image_dispose(&repeated);

    /* An unsupported miter join cannot be silently exposed by enlarging
     * the Clip, nor can the triangle stroke be drawn without a Clip. */
    rectangle(f.paths[0],0,0,100,50);
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&result)==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(result.pixels==NULL && result.size==0u);
    rectangle(f.paths[0],10,15,40,35);
    f.states[0].clip_id=0u;
    f.ir.resources.clip_count=0u;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&result)==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(result.pixels==NULL && result.size==0u);
    f.ir.resources.clip_count=1u;
    f.states[0].clip_id=1u;

    f.ops[0].value.path.options.stroke_width=2.0f;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&result)==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(result.pixels==NULL && result.size==0u);
    f.ops[0].value.path.options.stroke_width=1.0f;
    f.ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&result)==
          QUANTAPDF_ERROR_UNSUPPORTED);
    f.ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    f.ops[0].value.path.options.transform.a=2.0f;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&result)==
          QUANTAPDF_ERROR_UNSUPPORTED);
    f.ops[0].value.path.options.transform.a=1.0f;
    f.ops[0].value.path.options.fill=1;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&result)==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(result.pixels==NULL && result.size==0u);
    return EXIT_SUCCESS;
}

static int test_fractional_and_transparent(void)
{
    fixture f;
    const quantapdf_ir_raster_v1_limits budget = {64u,3u,192u,120000u};
    quantapdf_ir_raster_image image = {0};
    initialize(&f);
    f.clips[3].transform.e = 0.5f;
    OK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image));
    CHECK(image.pixels[(3u*8u+3u)*4u+1u] > 0u);
    CHECK(image.pixels[(3u*8u+3u)*4u+1u] < 255u);
    CHECK(rgba(&image,4,4) == UINT32_C(0x0000ffff));
    quantapdf_ir_raster_image_dispose(&image);

    f.page.suppress_background = 1;
    OK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image));
    CHECK(rgba(&image,7,7) == UINT32_C(0x00000000));
    CHECK(rgba(&image,0,0) == UINT32_C(0x00ff00ff));
    quantapdf_ir_raster_image_dispose(&image);
    return EXIT_SUCCESS;
}


/* A clip with an admitted MOVE_TO but no nondegenerate edges is empty,
 * not a corrupt Clip resource. No samples in the red nested scope may
 * contribute ink; later, independently owned Q-restored states still paint.
 * The standalone native path is provider-free and never uses PDFium. */
static int test_empty_leaf_clip_and_restore(void)
{
    fixture f;
    const quantapdf_ir_raster_v1_limits budget = {64u,3u,192u,120000u};
    quantapdf_ir_raster_image image = {0}, repeated = {0};
    unsigned char saved[8u*8u*4u];
    initialize(&f);
    CHECK(f.paths[1][0].kind == QUANTAPDF_COMPOSER_PATH_MOVE_TO);
    f.clips[1].command_count = 1u; /* valid zero-area PATH, not missing data */
    OK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image));
    CHECK(image.size == sizeof(saved) && image.width == 8u);
    CHECK(rgba(&image,3,3) == UINT32_C(0xffffffff)); /* empty red scope */
    CHECK(rgba(&image,4,4) == UINT32_C(0x0000ffff)); /* restored shared leaf */
    CHECK(rgba(&image,6,6) == UINT32_C(0x0000ffff));
    CHECK(rgba(&image,0,0) == UINT32_C(0x00ff00ff)); /* final un-clipped */
    CHECK(rgba(&image,7,7) == UINT32_C(0xffffffff));
    memcpy(saved,image.pixels,sizeof(saved));
    OK(quantapdf_ir_raster_scoped_operations_v2(&f.ir,0u,&budget,&repeated));
    CHECK(repeated.size == image.size &&
          memcmp(saved,repeated.pixels,sizeof(saved)) == 0);
    quantapdf_ir_raster_image_dispose(&repeated);
    OK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&repeated));
    CHECK(repeated.size == image.size &&
          memcmp(saved,repeated.pixels,sizeof(saved)) == 0);
    quantapdf_ir_raster_image_dispose(&image);
    quantapdf_ir_raster_image_dispose(&repeated);

    f.clips[1].fill_rule = QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    f.page.suppress_background = 1;
    OK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image));
    CHECK(rgba(&image,3,3) == UINT32_C(0x00000000));
    CHECK(rgba(&image,4,4) == UINT32_C(0x0000ffff));
    CHECK(rgba(&image,0,0) == UINT32_C(0x00ff00ff));
    quantapdf_ir_raster_image_dispose(&image);

    /* Distinguish a valid empty geometry from absent or inconsistent Clip
     * commands. Fail closed, fully reset every published image field. */
    f.clips[1].commands = NULL;
    image.pixels = (unsigned char *)(uintptr_t)1u;
    image.width = image.height = image.stride = image.size = SIZE_MAX;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.width == 0u && image.height == 0u &&
          image.stride == 0u && image.size == 0u);
    f.clips[1].commands = f.paths[1];
    f.clips[1].command_count = 0u;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.width == 0u && image.height == 0u &&
          image.stride == 0u && image.size == 0u);
    return EXIT_SUCCESS;
}

/* Definition-time Clip transforms stay frozen; paint transforms do not move
 * shared Clip resources. A disjoint translated leaf is a supported empty
 * intersection. A later unsupported operation must not publish red/blue. */
static int test_clip_definition_ctm_disjoint_and_failure(void)
{
    fixture f;
    const quantapdf_ir_raster_v1_limits budget = {64u,3u,192u,120000u};
    quantapdf_ir_raster_image image = {0};
    initialize(&f);
    rectangle(f.shapes[1],0,6,8,8);
    f.clips[3].transform.e = 1.0f; /* definition CTM: x=4..6 */
    f.ops[0].value.path.options.transform.e = -2.0f; /* paint x=-2..6 */
    f.ops[1].value.path.options.transform.e = 1.0f; /* paint x=1..9 */
    OK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image));
    CHECK(rgba(&image,3,3) == UINT32_C(0xffffffff));
    CHECK(rgba(&image,4,3) == UINT32_C(0xff0000ff));
    CHECK(rgba(&image,5,3) == UINT32_C(0xff0000ff));
    CHECK(rgba(&image,6,3) == UINT32_C(0xffffffff));
    CHECK(rgba(&image,4,6) == UINT32_C(0x0000ffff));
    CHECK(rgba(&image,7,6) == UINT32_C(0xffffffff));
    CHECK(rgba(&image,0,0) == UINT32_C(0x00ff00ff));
    quantapdf_ir_raster_image_dispose(&image);

    f.clips[3].transform.e = 8.0f; /* empty, disjoint but valid CTM */
    OK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image));
    CHECK(rgba(&image,4,3) == UINT32_C(0xffffffff));
    CHECK(rgba(&image,4,6) == UINT32_C(0x0000ffff));
    CHECK(rgba(&image,0,0) == UINT32_C(0x00ff00ff));
    quantapdf_ir_raster_image_dispose(&image);

    f.ops[2].value.path.options.fill = 0; /* late unsupported paint */
    image.pixels = (unsigned char *)(uintptr_t)1u;
    image.width = image.height = image.stride = image.size = SIZE_MAX;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.width == 0u && image.height == 0u &&
          image.stride == 0u && image.size == 0u);
    CHECK(f.clips[3].transform.e == 8.0f && f.states[1].clip_id == 1u &&
          f.ops[0].value.path.options.transform.e == -2.0f);
    return EXIT_SUCCESS;
}

static int test_rejection_and_atomicity(void)
{
    fixture f;
    quantapdf_ir_raster_v1_limits budget = {64u,3u,192u,120000u};
    quantapdf_ir_raster_image image = {0};
    initialize(&f);
    image.pixels = (unsigned char *)(uintptr_t)1u;
    image.size = SIZE_MAX;
    f.outer[0] = 5u; /* graph cycle */
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.size == 0u);
    f.outer[0] = 3u;

    f.inner[1] = 5u; /* indirect cycle */
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    f.inner[1] = 2u;

    /* Invalid child, duplicate child and out-of-budget DAG shape must
     * reject before publishing any pixels; restore the same fixture each
     * time so the other regression cases remain independent. */
    f.inner[0] = 0u;
    image.pixels = (unsigned char *)(uintptr_t)1u;
    image.size = SIZE_MAX;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.size == 0u);
    f.inner[0] = 1u;

    f.inner[1] = 1u; /* duplicate intersection member */
    image.pixels = (unsigned char *)(uintptr_t)1u;
    image.size = SIZE_MAX;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.size == 0u);
    f.inner[1] = 2u;

    f.inner[1] = 6u; /* child ID exceeds registered resource count */
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.size == 0u);
    f.inner[1] = 2u;

    budget.max_pixels = 63u; /* 8x8 page exceeds budget */
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.size == 0u);
    budget.max_pixels = 64u;

    f.states[1].clip_id = 6u; /* bad state -> Clip reference */
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    f.states[1].clip_id = 1u;

    f.clips[3].transform.a = 0.0f;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    f.clips[3].transform.a = 1.0f;

    /* The last operation fails after red and blue layers have been drawn.
     * No partial output may escape even in this late failure case. */
    f.ops[2].value.path.options.stroke = 1;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.size == 0u);
    f.ops[2].value.path.options.stroke = 0;

    /* A weighted global budget can admit the nested operation that needs
     * 16 edges; the old equal split (36 / 3 = 12) rejected it despite total
     * work fitting within 36. The aggregate cap must still reject 24 edges. */
    budget.max_edges = 36u;
    OK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image));
    CHECK(image.pixels != NULL && image.size == 8u * 8u * 4u);
    quantapdf_ir_raster_image_dispose(&image);
    budget.max_edges = 24u;
    image.pixels = (unsigned char *)(uintptr_t)1u;
    image.size = SIZE_MAX;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.size == 0u);
    budget.max_edges = 192u;

    budget.max_sample_tests = 12u;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    budget.max_sample_tests = 120000u;

    /* Global node ceiling is eight, checked before indexing resources. */
    f.ir.resources.clip_count = 9u;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(image.pixels == NULL && image.size == 0u);
    f.ir.resources.clip_count = 5u;

    /* Depth-five path with seven registered nodes, below the node ceiling:
     * 7 -> 6 -> 5 -> 3 -> 1. The original five nodes retain ownership and
     * every new node is initialized; depth rejection must be atomic. */
    {
        quantapdf_composer_clip_id chain6[2] = {5u,4u};
        quantapdf_composer_clip_id chain7[2] = {6u,2u};
        f.clips[5].kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
        f.clips[5].members = chain6;
        f.clips[5].member_count = 2u;
        f.clips[6].kind = QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
        f.clips[6].members = chain7;
        f.clips[6].member_count = 2u;
        f.ir.resources.clip_count = 7u;
        f.states[0].clip_id = 7u;
        CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
              QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(image.pixels == NULL && image.size == 0u);
        f.states[0].clip_id = 5u;
        f.ir.resources.clip_count = 5u;
        memset(&f.clips[5],0,sizeof(f.clips[5]));
        memset(&f.clips[6],0,sizeof(f.clips[6]));
    }

    f.ir.resources.clip_count = 6u; /* unowned extra resource */
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    f.ir.resources.clip_count = 5u;

    f.states[0].fill_alpha = 0.5f;
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,&image) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    f.states[0].fill_alpha = 1.0f;

    CHECK(quantapdf_ir_raster_scoped_operations_v1(NULL,0u,&budget,&image) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_scoped_operations_v1(&f.ir,0u,&budget,NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(image.pixels == NULL && image.size == 0u);
    return EXIT_SUCCESS;
}

int main(void)
{
    CHECK(test_single_opaque_path() == EXIT_SUCCESS);
    CHECK(test_clipped_one_pixel_stroke() == EXIT_SUCCESS);
    CHECK(test_scoped_dag() == EXIT_SUCCESS);
    CHECK(test_fractional_and_transparent() == EXIT_SUCCESS);
    CHECK(test_empty_leaf_clip_and_restore() == EXIT_SUCCESS);
    CHECK(test_clip_definition_ctm_disjoint_and_failure() == EXIT_SUCCESS);
    CHECK(test_rejection_and_atomicity() == EXIT_SUCCESS);
    fprintf(stderr,"[quantapdf.native_scoped_clip_dag_v1] complete\n");
    return EXIT_SUCCESS;
}
