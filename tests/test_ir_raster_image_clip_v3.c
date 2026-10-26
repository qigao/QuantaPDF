#include "render/ir_raster_image_clip_v3.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"%s:%d: restored image V3: %s\n",__FILE__,__LINE__,#x); \
    return EXIT_FAILURE; \
} } while(0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[2];
    quantapdf_composer_image_state images[2];
    quantapdf_composer_graphics_state gs;
    quantapdf_composer_clip_state clip;
    quantapdf_composer_path_command commands[5];
    unsigned char red[3],blue[3],blue_alpha;
} fixture;

static const quantapdf_ir_raster_image_v1_limits image_limits =
    {64u,2u,2u,80u};
static const quantapdf_ir_raster_v1_limits clip_limits =
    {64u,2u,256u,400000u};

static void point(quantapdf_composer_path_command *p,
                  quantapdf_composer_path_command_kind kind,float x,float y)
{
    memset(p,0,sizeof(*p));
    p->kind=kind;p->point1.x=x;p->point1.y=y;
}

static void init(fixture *f)
{
    size_t i;
    memset(f,0,sizeof(*f));
    f->page.width_points=8.0f;
    f->page.height_points=8.0f;
    f->page.background_argb=UINT32_C(0xffffffff);
    f->ir.pages=&f->page;
    f->ir.page_count=1u;
    f->ir.operations=f->ops;
    f->ir.operation_count=2u;
    f->ir.resources.images=f->images;
    f->ir.resources.image_count=2u;
    f->ir.resources.graphics_states=&f->gs;
    f->ir.resources.graphics_state_count=1u;
    f->ir.resources.clips=&f->clip;
    f->ir.resources.clip_count=1u;
    f->gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
    f->gs.stroke_alpha=1.0f;f->gs.fill_alpha=1.0f;f->gs.clip_id=1u;
    f->clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    f->clip.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    f->clip.commands=f->commands;
    f->clip.command_count=5u;
    f->clip.transform=(quantapdf_affine_transform){1,0,0,1,0,0};
    point(&f->commands[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,1.0f,1.0f);
    point(&f->commands[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,7.0f,1.0f);
    point(&f->commands[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,7.0f,7.0f);
    point(&f->commands[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,1.0f,7.0f);
    point(&f->commands[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0.0f,0.0f);
    f->red[0]=255u;f->blue[2]=255u;f->blue_alpha=128u;
    for(i=0u;i<2u;++i) {
        f->images[i].data=i==0u?f->red:f->blue;
        f->images[i].width=1u;f->images[i].height=1u;
        f->images[i].size=3u;f->images[i].components=3;
        f->images[i].format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
        f->ops[i].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
        f->ops[i].graphics_state_id=i==0u?1u:0u;
        f->ops[i].value.image.image_id=
            (quantapdf_composer_image_id)(i+1u);
        f->ops[i].value.image.options.struct_size=
            QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
        f->ops[i].value.image.options.graphics_state_id=i==0u?1u:0u;
        f->ops[i].value.image.options.fit=
            QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    }
    f->images[1].has_alpha=1;
    f->images[1].alpha_data=&f->blue_alpha;
    f->images[1].alpha_size=1u;
    f->ops[0].bounds=(quantapdf_rect){0.0f,0.0f,8.0f,8.0f};
    f->ops[1].bounds=(quantapdf_rect){0.0f,0.0f,4.0f,4.0f};
}

static uint32_t rgba(const quantapdf_ir_raster_image *im,size_t x,size_t y)
{
    const unsigned char *p=im->pixels+y*im->stride+x*4u;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
           ((uint32_t)p[2]<<8u)|p[3];
}

static int test_restored_clip_and_order(void)
{
    fixture f;
    quantapdf_ir_raster_image first={0},same={0};
    init(&f);
    OK(quantapdf_ir_raster_restored_raw_images_v3(
        &f.ir,0u,&image_limits,&clip_limits,&first));
    OK(quantapdf_ir_raster_restored_raw_images_v3(
        &f.ir,0u,&image_limits,&clip_limits,&same));
    CHECK(first.width==8u && first.height==8u &&
          first.size==256u && first.stride==32u &&
          first.size==same.size &&
          memcmp(first.pixels,same.pixels,first.size)==0);
    CHECK(rgba(&first,0u,0u)==UINT32_C(0x7f7fffff));
    CHECK(rgba(&first,1u,1u)==UINT32_C(0x7f0080ff));
    CHECK(rgba(&first,3u,3u)==UINT32_C(0x7f0080ff));
    CHECK(rgba(&first,7u,7u)==UINT32_C(0xffffffff));
    CHECK(rgba(&first,5u,5u)==UINT32_C(0xff0000ff));
    f.red[0]=0u;f.blue[2]=0u;f.blue_alpha=0u;
    CHECK(rgba(&first,3u,3u)==UINT32_C(0x7f0080ff));
    quantapdf_ir_raster_image_dispose(&first);
    quantapdf_ir_raster_image_dispose(&same);
    return EXIT_SUCCESS;
}

static int test_old_v2_remains_separate(void)
{
    fixture f;
    quantapdf_ir_raster_image output={0};
    init(&f);
    CHECK(quantapdf_ir_raster_two_clipped_raw_images_v2(
        &f.ir,0u,&image_limits,&clip_limits,&output)
        ==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(output.pixels==NULL && output.size==0u);
    f.ops[1].graphics_state_id=1u;
    f.ops[1].value.image.options.graphics_state_id=1u;
    CHECK(quantapdf_ir_raster_restored_raw_images_v3(
        &f.ir,0u,&image_limits,&clip_limits,&output)
        ==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(output.pixels==NULL && output.size==0u);
    OK(quantapdf_ir_raster_two_clipped_raw_images_v2(
        &f.ir,0u,&image_limits,&clip_limits,&output));
    quantapdf_ir_raster_image_dispose(&output);
    return EXIT_SUCCESS;
}

static int test_transparency_and_evenodd(void)
{
    fixture f;
    quantapdf_ir_raster_image image={0};
    init(&f);
    f.page.suppress_background=1;
    f.clip.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    OK(quantapdf_ir_raster_restored_raw_images_v3(
        &f.ir,0u,&image_limits,&clip_limits,&image));
    CHECK(rgba(&image,0u,0u)==UINT32_C(0x0000ff80));
    CHECK(rgba(&image,1u,1u)==UINT32_C(0x7f0080ff));
    CHECK(rgba(&image,3u,3u)==UINT32_C(0x7f0080ff));
    quantapdf_ir_raster_image_dispose(&image);
    return EXIT_SUCCESS;
}

static int refused(fixture *f,
                   const quantapdf_ir_raster_image_v1_limits *il,
                   const quantapdf_ir_raster_v1_limits *cl)
{
    quantapdf_ir_raster_image output;
    memset(&output,0xa5,sizeof(output));
    CHECK(quantapdf_ir_raster_restored_raw_images_v3(
        &f->ir,0u,il,cl,&output)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(output.pixels==NULL && output.size==0u &&
          output.width==0u && output.height==0u && output.stride==0u);
    return EXIT_SUCCESS;
}

static int test_atomic_negatives(void)
{
    fixture f;
    quantapdf_ir_raster_image_v1_limits il=image_limits;
    quantapdf_ir_raster_v1_limits cl=clip_limits;
    quantapdf_ir_raster_image good={0};
    init(&f);
    il.max_drawn_pixels=79u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    il=image_limits;
    il.max_source_pixels=1u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    il=image_limits;
    cl.max_edges=1u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    cl=clip_limits;
    cl.max_sample_tests=1u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    cl=clip_limits;
    f.ops[1].value.image.image_id=3u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.ops[1].value.image.image_id=2u;
    f.ops[1].graphics_state_id=1u; /* Restored second must be unclipped. */
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.ops[1].graphics_state_id=0u;
    f.ops[1].value.image.options.graphics_state_id=1u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.ops[1].value.image.options.graphics_state_id=0u;
    f.ops[0].graphics_state_id=0u; /* The first must be clipped. */
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.ops[0].graphics_state_id=1u;
    f.ops[1].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_COVER;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.ops[1].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    f.images[1].format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_PNG;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.images[1].format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    f.images[1].alpha_size=0u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.images[1].alpha_size=1u;
    /* Valid first V1 layer followed by invalid second RAW payload:
     * output must remain entirely empty even on a late intermediate fail. */
    f.images[1].size=2u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.images[1].size=3u;
    f.gs.clip_id=0u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.gs.clip_id=1u;
    f.ops[1].kind=QUANTAPDF_COMPOSER_OPERATION_TEXT;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.ops[1].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    f.ir.resources.image_count=1u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.ir.resources.image_count=2u;
    f.clip.command_count=0u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.clip.command_count=5u;
    f.page.width_points=1.0e30f; /* Reject before float->size_t conversion. */
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.page.width_points=8.0f;

    /* Failed second-layer preflight must not poison a later valid call. */
    OK(quantapdf_ir_raster_restored_raw_images_v3(
        &f.ir,0u,&image_limits,&clip_limits,&good));
    CHECK(rgba(&good,3u,3u)==UINT32_C(0x7f0080ff));
    quantapdf_ir_raster_image_dispose(&good);
    return EXIT_SUCCESS;
}

int main(void)
{
    if(test_restored_clip_and_order()!=EXIT_SUCCESS ||
       test_old_v2_remains_separate()!=EXIT_SUCCESS ||
       test_transparency_and_evenodd()!=EXIT_SUCCESS ||
       test_atomic_negatives()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    puts("Native first RAW Image clipped, second Q-restored: source-order "
         "alpha, global budget, independent owners and atomic failure PASS");
    return EXIT_SUCCESS;
}
