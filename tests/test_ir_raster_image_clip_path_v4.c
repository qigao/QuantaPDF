#include "render/ir_raster_image_clip_path_v4.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"%s:%d: image/Q/path V4: %s\n",__FILE__,__LINE__,#x); \
    return EXIT_FAILURE; \
} } while(0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation operations[2];
    quantapdf_composer_image_state image;
    quantapdf_composer_graphics_state gs;
    quantapdf_composer_clip_state clip;
    quantapdf_composer_path_command mask[5],paint[5];
    unsigned char rgb[3],alpha;
} fixture;

static const quantapdf_ir_raster_image_v1_limits il={64u,2u,1u,64u};
static const quantapdf_ir_raster_v1_limits cl={64u,2u,256u,2000000u};

static void point(quantapdf_composer_path_command *p,
                  quantapdf_composer_path_command_kind kind,float x,float y)
{
    memset(p,0,sizeof(*p));
    p->kind=kind;p->point1.x=x;p->point1.y=y;
}
static void rectangle(quantapdf_composer_path_command *p,
                      float x0,float y0,float x1,float y1)
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,x0,y0);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y0);
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y1);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,x0,y1);
    point(&p[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0.0f,0.0f);
}
static void init(fixture *f)
{
    memset(f,0,sizeof(*f));
    f->page.width_points=8.0f;
    f->page.height_points=8.0f;
    f->page.background_argb=UINT32_C(0xffffffff);
    f->ir.pages=&f->page;
    f->ir.page_count=1u;
    f->ir.operations=f->operations;
    f->ir.operation_count=2u;
    f->ir.resources.images=&f->image;
    f->ir.resources.image_count=1u;
    f->ir.resources.graphics_states=&f->gs;
    f->ir.resources.graphics_state_count=1u;
    f->ir.resources.clips=&f->clip;
    f->ir.resources.clip_count=1u;
    f->rgb[0]=255u;f->alpha=128u;
    f->image.data=f->rgb;f->image.size=3u;
    f->image.width=f->image.height=1u;
    f->image.components=3;
    f->image.format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    f->gs.fill_alpha=f->gs.stroke_alpha=1.0f;
    f->gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
    f->gs.clip_id=1u;
    f->clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    f->clip.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    f->clip.commands=f->mask;
    f->clip.command_count=5u;
    f->clip.transform=(quantapdf_affine_transform){1,0,0,1,0,0};
    rectangle(f->mask,2.0f,2.0f,6.0f,6.0f);
    f->operations[0].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    f->operations[0].graphics_state_id=1u;
    f->operations[0].bounds=(quantapdf_rect){0,0,8,8};
    f->operations[0].value.image.image_id=1u;
    f->operations[0].value.image.options.struct_size=
        QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
    f->operations[0].value.image.options.graphics_state_id=1u;
    f->operations[0].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    f->operations[1].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    f->operations[1].value.path.commands=f->paint;
    f->operations[1].value.path.command_count=5u;
    f->operations[1].value.path.options.struct_size=
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    f->operations[1].value.path.options.fill=1;
    f->operations[1].value.path.options.fill_argb=UINT32_C(0xff0000ff);
    f->operations[1].value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    rectangle(f->paint,0.0f,0.0f,4.0f,4.0f);
}
static uint32_t rgba(const quantapdf_ir_raster_image *im,size_t x,size_t y)
{
    const unsigned char *p=im->pixels+y*im->stride+x*4u;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
           ((uint32_t)p[2]<<8u)|p[3];
}
static int positive(void)
{
    fixture f;
    quantapdf_ir_raster_image first={0},same={0};
    init(&f);
    OK(quantapdf_ir_raster_clipped_image_then_path_v4(
        &f.ir,0u,&il,&cl,&first));
    OK(quantapdf_ir_raster_clipped_image_then_path_v4(
        &f.ir,0u,&il,&cl,&same));
    CHECK(first.width==8u && first.height==8u && first.stride==32u &&
          first.size==256u && first.size==same.size &&
          memcmp(first.pixels,same.pixels,first.size)==0);
    CHECK(rgba(&first,0u,0u)==UINT32_C(0x0000ffff)); /* Post-Q Path */
    CHECK(rgba(&first,3u,3u)==UINT32_C(0x0000ffff)); /* Over earlier Image */
    CHECK(rgba(&first,4u,4u)==UINT32_C(0xff0000ff)); /* Image + Clip */
    CHECK(rgba(&first,7u,7u)==UINT32_C(0xffffffff));
    CHECK(rgba(&first,6u,4u)==UINT32_C(0xffffffff));
    f.rgb[0]=0u;
    CHECK(rgba(&first,4u,4u)==UINT32_C(0xff0000ff)); /* Detached */
    quantapdf_ir_raster_image_dispose(&first);
    quantapdf_ir_raster_image_dispose(&same);

    init(&f);
    f.page.suppress_background=1;
    f.clip.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    f.image.has_alpha=1;
    f.image.alpha_data=&f.alpha;
    f.image.alpha_size=1u;
    OK(quantapdf_ir_raster_clipped_image_then_path_v4(
        &f.ir,0u,&il,&cl,&first));
    CHECK(rgba(&first,0u,0u)==UINT32_C(0x0000ffff));
    CHECK(rgba(&first,4u,4u)==UINT32_C(0xff000080));
    CHECK(rgba(&first,7u,7u)==UINT32_C(0x00000000));
    quantapdf_ir_raster_image_dispose(&first);
    return EXIT_SUCCESS;
}
static int refused(fixture *f,
                   const quantapdf_ir_raster_image_v1_limits *image_limits,
                   const quantapdf_ir_raster_v1_limits *path_limits)
{
    quantapdf_ir_raster_image result;
    memset(&result,0xaa,sizeof(result));
    CHECK(quantapdf_ir_raster_clipped_image_then_path_v4(
        &f->ir,0u,image_limits,path_limits,&result)==
        QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(result.pixels==NULL && result.size==0u &&
          result.width==0u && result.height==0u && result.stride==0u);
    return EXIT_SUCCESS;
}
static int negatives(void)
{
    fixture f;
    quantapdf_ir_raster_image_v1_limits image_limits=il;
    quantapdf_ir_raster_v1_limits path_limits=cl;
    quantapdf_ir_raster_image good={0};
    init(&f);
    image_limits.max_drawn_pixels=63u;
    CHECK(refused(&f,&image_limits,&path_limits)==0);
    image_limits=il;
    image_limits.max_source_pixels=0u;
    {
        quantapdf_ir_raster_image out={0};
        CHECK(quantapdf_ir_raster_clipped_image_then_path_v4(
            &f.ir,0u,&image_limits,&path_limits,&out)==
              QUANTAPDF_ERROR_ARGUMENT);
        CHECK(out.pixels==NULL);
    }
    image_limits=il;
    path_limits.max_edges=1u;
    CHECK(refused(&f,&image_limits,&path_limits)==0);
    path_limits=cl;
    path_limits.max_sample_tests=1u;
    CHECK(refused(&f,&image_limits,&path_limits)==0);
    path_limits=cl;
    f.operations[1].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    CHECK(refused(&f,&image_limits,&path_limits)==0);
    f.operations[1].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    f.operations[1].graphics_state_id=1u;
    CHECK(refused(&f,&image_limits,&path_limits)==0);
    f.operations[1].graphics_state_id=0u;
    f.operations[1].value.path.options.fill=0;
    CHECK(refused(&f,&image_limits,&path_limits)==0);
    f.operations[1].value.path.options.fill=1;
    f.operations[1].value.path.options.stroke=1;
    CHECK(refused(&f,&image_limits,&path_limits)==0);
    f.operations[1].value.path.options.stroke=0;
    f.operations[0].graphics_state_id=0u;
    CHECK(refused(&f,&image_limits,&path_limits)==0);
    f.operations[0].graphics_state_id=1u;
    f.operations[0].value.image.image_id=2u;
    CHECK(refused(&f,&image_limits,&path_limits)==0);
    f.operations[0].value.image.image_id=1u;
    f.gs.clip_id=0u;
    CHECK(refused(&f,&image_limits,&path_limits)==0);
    f.gs.clip_id=1u;
    f.image.size=2u;
    CHECK(refused(&f,&image_limits,&path_limits)==0);
    f.image.size=3u;
    f.clip.command_count=0u;
    CHECK(refused(&f,&image_limits,&path_limits)==0);
    f.clip.command_count=5u;
    f.page.width_points=1.0e30f;
    CHECK(refused(&f,&image_limits,&path_limits)==0);
    f.page.width_points=8.0f;
    f.operations[1].value.path.command_count=0u;
    CHECK(refused(&f,&image_limits,&path_limits)==0);
    f.operations[1].value.path.command_count=5u;

    OK(quantapdf_ir_raster_clipped_image_then_path_v4(
        &f.ir,0u,&il,&cl,&good));
    CHECK(rgba(&good,0u,0u)==UINT32_C(0x0000ffff));
    quantapdf_ir_raster_image_dispose(&good);
    return EXIT_SUCCESS;
}
int main(void)
{
    if(positive()!=EXIT_SUCCESS || negatives()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    puts("Clipped Image then Q-restored Path V4: pure C11 source-over, "
         "budget/refusal/repeat detached RGBA PASS");
    return EXIT_SUCCESS;
}
