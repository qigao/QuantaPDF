#include "render/ir_raster_image_clip_v1.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if(!(e)) { \
    fprintf(stderr,"%s:%d: RAW Image Clip V1: %s\n",__FILE__,__LINE__,#e); \
    return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

typedef struct case_data {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation op;
    quantapdf_composer_graphics_state state;
    quantapdf_composer_clip_state clip;
    quantapdf_composer_image_state image;
    quantapdf_composer_path_command commands[10];
    unsigned char rgb[3],alpha;
} case_data;

static void point(quantapdf_composer_path_command *p,
                  quantapdf_composer_path_command_kind kind,float x,float y)
{
    memset(p,0,sizeof(*p));
    p->kind=kind;
    p->point1.x=x;
    p->point1.y=y;
}

static void rect(quantapdf_composer_path_command *cmd,
                 float x0,float y0,float x1,float y1)
{
    point(&cmd[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,x0,y0);
    point(&cmd[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y0);
    point(&cmd[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y1);
    point(&cmd[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,x0,y1);
    point(&cmd[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0.0f,0.0f);
}

static uint32_t rgba(const quantapdf_ir_raster_image *image,
                     size_t x,size_t y)
{
    const unsigned char *p=image->pixels+y*image->stride+x*4u;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
           ((uint32_t)p[2]<<8u)|p[3];
}

static void init(case_data *f)
{
    memset(f,0,sizeof(*f));
    f->page.width_points=8.0f;
    f->page.height_points=8.0f;
    f->page.background_argb=UINT32_C(0xffffffff);
    f->ir.pages=&f->page;
    f->ir.page_count=1u;
    f->ir.operations=&f->op;
    f->ir.operation_count=1u;
    f->ir.resources.graphics_states=&f->state;
    f->ir.resources.graphics_state_count=1u;
    f->ir.resources.clips=&f->clip;
    f->ir.resources.clip_count=1u;
    f->ir.resources.images=&f->image;
    f->ir.resources.image_count=1u;

    f->rgb[0]=255u;
    f->rgb[1]=0u;
    f->rgb[2]=0u;
    f->image.data=f->rgb;
    f->image.size=sizeof(f->rgb);
    f->image.width=1u;
    f->image.height=1u;
    f->image.components=3;
    f->image.format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;

    f->op.kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    f->op.graphics_state_id=1u;
    f->op.value.image.image_id=1u;
    f->op.value.image.options.struct_size=
        QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
    f->op.value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    f->op.value.image.options.graphics_state_id=1u;
    f->op.bounds=(quantapdf_rect){0.0f,0.0f,8.0f,8.0f};

    f->state.fill_alpha=f->state.stroke_alpha=1.0f;
    f->state.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
    f->state.clip_id=1u;
    f->clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    f->clip.commands=f->commands;
    f->clip.command_count=4u;
    f->clip.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    f->clip.transform=(quantapdf_affine_transform){1,0,0,1,0,0};
    point(&f->commands[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,0.0f,0.0f);
    point(&f->commands[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,8.0f,0.0f);
    point(&f->commands[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,0.0f,8.0f);
    point(&f->commands[3],QUANTAPDF_COMPOSER_PATH_CLOSE,0.0f,0.0f);
}

static const quantapdf_ir_raster_image_v1_limits image_limits=
    {64u,1u,1u,64u};
static const quantapdf_ir_raster_v1_limits clip_limits=
    {64u,1u,64u,100000u};

static int test_triangle_coverage_and_owned_repeat(void)
{
    case_data f;
    quantapdf_ir_raster_image a={0},b={0};
    init(&f);
    OK(quantapdf_ir_raster_clipped_raw_image_v1(
        &f.ir,0u,&image_limits,&clip_limits,&a));
    OK(quantapdf_ir_raster_clipped_raw_image_v1(
        &f.ir,0u,&image_limits,&clip_limits,&b));
    CHECK(a.width==8u && a.height==8u && a.stride==32u &&
          a.size==256u && b.size==a.size &&
          memcmp(a.pixels,b.pixels,a.size)==0);
    CHECK(rgba(&a,1u,1u)==UINT32_C(0xff0000ff));
    CHECK(rgba(&a,7u,7u)==UINT32_C(0xffffffff));
    CHECK((rgba(&a,3u,4u)&UINT32_C(0xff))==UINT32_C(0xff));
    CHECK(((rgba(&a,3u,4u)>>16u)&255u)>0u &&
          ((rgba(&a,3u,4u)>>16u)&255u)<255u);
    f.rgb[0]=0u;
    CHECK(rgba(&a,1u,1u)==UINT32_C(0xff0000ff));
    quantapdf_ir_raster_image_dispose(&a);
    quantapdf_ir_raster_image_dispose(&b);
    CHECK(a.pixels==NULL && b.pixels==NULL && a.size==0u);
    return EXIT_SUCCESS;
}

static int test_evenodd_and_definition_ctm(void)
{
    case_data f;
    quantapdf_ir_raster_image out={0};
    init(&f);
    f.rgb[0]=0u;f.rgb[1]=255u;
    rect(f.commands,1,1,7,7);
    rect(f.commands+5,3,3,5,5);
    f.clip.command_count=10u;
    f.clip.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    OK(quantapdf_ir_raster_clipped_raw_image_v1(
        &f.ir,0u,&image_limits,&clip_limits,&out));
    CHECK(rgba(&out,1u,1u)==UINT32_C(0x00ff00ff));
    CHECK(rgba(&out,4u,4u)==UINT32_C(0xffffffff));
    CHECK(rgba(&out,6u,6u)==UINT32_C(0x00ff00ff));
    CHECK(rgba(&out,7u,7u)==UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&out);

    /* Definition CTM moves the geometry without mutating authored commands.
     * The output remains independent of source/Clip payload after rendering. */
    f.clip.transform.e=1.0f;
    OK(quantapdf_ir_raster_clipped_raw_image_v1(
        &f.ir,0u,&image_limits,&clip_limits,&out));
    CHECK(rgba(&out,1u,1u)==UINT32_C(0xffffffff));
    CHECK(rgba(&out,2u,2u)==UINT32_C(0x00ff00ff));
    CHECK(rgba(&out,4u,4u)==UINT32_C(0xffffffff));
    CHECK(rgba(&out,7u,6u)==UINT32_C(0x00ff00ff));
    quantapdf_ir_raster_image_dispose(&out);
    return EXIT_SUCCESS;
}

static int test_transparent_alpha(void)
{
    case_data f;
    quantapdf_ir_raster_image out={0};
    init(&f);
    f.page.suppress_background=1;
    f.rgb[0]=f.rgb[1]=f.rgb[2]=0u;
    f.alpha=128u;
    f.image.has_alpha=1;
    f.image.alpha_data=&f.alpha;
    f.image.alpha_size=1u;
    OK(quantapdf_ir_raster_clipped_raw_image_v1(
        &f.ir,0u,&image_limits,&clip_limits,&out));
    CHECK(rgba(&out,1u,1u)==UINT32_C(0x00000080));
    CHECK(rgba(&out,7u,7u)==UINT32_C(0x00000000));
    CHECK((rgba(&out,3u,4u)&255u)>0u &&
          (rgba(&out,3u,4u)&255u)<128u);
    quantapdf_ir_raster_image_dispose(&out);
    return EXIT_SUCCESS;
}

static int refused(case_data *f,
                   const quantapdf_ir_raster_image_v1_limits *il,
                   const quantapdf_ir_raster_v1_limits *cl)
{
    quantapdf_ir_raster_image result;
    memset(&result,0xa5,sizeof(result));
    CHECK(quantapdf_ir_raster_clipped_raw_image_v1(
        &f->ir,0u,il,cl,&result)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(result.pixels==NULL && result.size==0u &&
          result.width==0u && result.height==0u && result.stride==0u);
    return EXIT_SUCCESS;
}

static int test_fail_closed_and_recovery(void)
{
    case_data f;
    quantapdf_ir_raster_image image={0};
    quantapdf_ir_raster_image_v1_limits il=image_limits;
    quantapdf_ir_raster_v1_limits cl=clip_limits;
    init(&f);
    f.ir.operation_count=2u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.ir.operation_count=1u;
    f.op.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.op.kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    f.op.graphics_state_id=0u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.op.graphics_state_id=1u;
    f.op.value.image.options.graphics_state_id=0u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.op.value.image.options.graphics_state_id=1u;
    f.state.fill_alpha=0.5f;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.state.fill_alpha=1.0f;
    f.state.soft_mask_id=1u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.state.soft_mask_id=0u;
    f.clip.kind=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    f.clip.members=(quantapdf_composer_clip_id *)&f.op;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.clip.members=NULL;
    f.ir.resources.clip_count=2u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.ir.resources.clip_count=1u;
    f.image.size=2u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.image.size=3u;
    il.max_drawn_pixels=63u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    il.max_drawn_pixels=64u;
    il.max_source_pixels=0u;
    {
        quantapdf_ir_raster_image bad={0};
        CHECK(quantapdf_ir_raster_clipped_raw_image_v1(
            &f.ir,0u,&il,&cl,&bad)==QUANTAPDF_ERROR_ARGUMENT &&
              bad.pixels==NULL);
    }
    il.max_source_pixels=1u;
    cl.max_edges=1u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    cl.max_edges=64u;
    cl.max_sample_tests=1u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    cl.max_sample_tests=100000u;
    f.clip.command_count=0u;
    CHECK(refused(&f,&il,&cl)==EXIT_SUCCESS);
    f.clip.command_count=4u;
    OK(quantapdf_ir_raster_clipped_raw_image_v1(
        &f.ir,0u,&il,&cl,&image));
    CHECK(rgba(&image,1u,1u)==UINT32_C(0xff0000ff));
    quantapdf_ir_raster_image_dispose(&image);
    return EXIT_SUCCESS;
}

int main(void)
{
    if(test_triangle_coverage_and_owned_repeat()!=EXIT_SUCCESS ||
       test_evenodd_and_definition_ctm()!=EXIT_SUCCESS ||
       test_transparent_alpha()!=EXIT_SUCCESS ||
       test_fail_closed_and_recovery()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    puts("[native-image-clip-v1] single RAW Image, scoped 4x4 W/W* clip "
         "sampling, alpha, CTM, detached owner and fail-closed budgets PASS");
    return EXIT_SUCCESS;
}
