#include "render/ir_raster_mixed_v1.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if(!(c)) { \
    fprintf(stderr,"%s:%d: mixed Path/Image: %s\n",__FILE__,__LINE__,#c); \
    return EXIT_FAILURE; \
} } while(0)
#define OK(c) CHECK((c)==QUANTAPDF_OK)

static uint32_t at(const quantapdf_ir_raster_image *im,size_t x,size_t y)
{
    const unsigned char *p=im->pixels+y*im->stride+4u*x;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
        ((uint32_t)p[2]<<8u)|p[3];
}
static void setup(quantapdf_ir *ir,quantapdf_composer_page_state *page,
                  quantapdf_composer_operation *ops,
                  quantapdf_composer_path_command *commands,
                  quantapdf_composer_image_state *resource,
                  unsigned char *rgb,unsigned char *alpha)
{
    size_t i;
    const float x[]={2.0f,6.0f,6.0f,2.0f};
    const float y[]={2.0f,2.0f,6.0f,6.0f};
    memset(ir,0,sizeof(*ir));memset(page,0,sizeof(*page));
    memset(ops,0,2u*sizeof(*ops));memset(resource,0,sizeof(*resource));
    memset(commands,0,5u*sizeof(*commands));
    for(i=0u;i<4u;++i){
        commands[i].kind=i==0u ? QUANTAPDF_COMPOSER_PATH_MOVE_TO :
            QUANTAPDF_COMPOSER_PATH_LINE_TO;
        commands[i].point1.x=x[i];commands[i].point1.y=y[i];
    }
    commands[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
    page->width_points=8.0f;page->height_points=8.0f;
    page->background_argb=UINT32_C(0xffffffff);
    ir->pages=page;ir->page_count=1u;
    ir->operations=ops;ir->operation_count=2u;
    ir->resources.images=resource;ir->resources.image_count=1u;
    resource->data=rgb;resource->size=3u;
    resource->alpha_data=alpha;resource->alpha_size=1u;resource->has_alpha=1;
    resource->width=1u;resource->height=1u;resource->components=3;
    resource->format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    ops[0].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    ops[0].value.path.commands=commands;
    ops[0].value.path.command_count=5u;
    ops[0].value.path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    ops[0].value.path.options.fill=1;
    ops[0].value.path.options.fill_argb=UINT32_C(0xffff0000);
    ops[0].value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    ops[1].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    ops[1].value.image.image_id=1u;
    ops[1].value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    ops[1].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    ops[1].bounds=(quantapdf_rect){0.0f,0.0f,4.0f,4.0f};
}

static int expect_refusal(quantapdf_ir *ir,
                          quantapdf_ir_raster_mixed_v1_limits *limits)
{
    quantapdf_ir_raster_image output;
    memset(&output,0xa5,sizeof(output));
    CHECK(quantapdf_ir_raster_mixed_path_image_v1(ir,0u,limits,&output)==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(output.pixels==NULL && output.size==0u && output.width==0u &&
          output.height==0u && output.stride==0u);
    quantapdf_ir_raster_image_dispose(&output);
    return EXIT_SUCCESS;
}
static int test_order_and_ownership(void)
{
    unsigned char blue[]={0u,0u,255u},alpha[]={128u};
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[2];
    quantapdf_composer_path_command commands[5];
    quantapdf_composer_image_state resource;
    quantapdf_ir ir;
    quantapdf_ir_raster_image a={0},b={0},reversed={0};
    const quantapdf_ir_raster_mixed_v1_limits limits={
        64u,128u,200000u,1u,16u
    };
    quantapdf_composer_operation tmp;
    setup(&ir,&page,ops,commands,&resource,blue,alpha);
    OK(quantapdf_ir_raster_mixed_path_image_v1(&ir,0u,&limits,&a));
    OK(quantapdf_ir_raster_mixed_path_image_v1(&ir,0u,&limits,&b));
    CHECK(a.pixels!=NULL && a.size==256u && a.width==8u &&
          a.height==8u && a.stride==32u && a.size==b.size &&
          memcmp(a.pixels,b.pixels,a.size)==0);
    CHECK(at(&a,1u,1u)==UINT32_C(0x7f7fffff));
    CHECK(at(&a,3u,3u)==UINT32_C(0x7f0080ff));
    CHECK(at(&a,5u,5u)==UINT32_C(0xff0000ff));
    CHECK(at(&a,7u,7u)==UINT32_C(0xffffffff));
    tmp=ops[0];ops[0]=ops[1];ops[1]=tmp;
    OK(quantapdf_ir_raster_mixed_path_image_v1(&ir,0u,&limits,&reversed));
    CHECK(at(&reversed,1u,1u)==UINT32_C(0x7f7fffff));
    CHECK(at(&reversed,3u,3u)==UINT32_C(0xff0000ff));
    page.suppress_background=1;
    quantapdf_ir_raster_image_dispose(&b);
    OK(quantapdf_ir_raster_mixed_path_image_v1(&ir,0u,&limits,&b));
    CHECK(at(&b,1u,1u)==UINT32_C(0x0000ff80));
    CHECK(at(&b,3u,3u)==UINT32_C(0xff0000ff));
    CHECK(at(&b,7u,7u)==UINT32_C(0x00000000));
    memset(blue,0,sizeof(blue));alpha[0]=0;
    commands[0].point1.x=-1.0f;
    CHECK(at(&a,3u,3u)==UINT32_C(0x7f0080ff) &&
          at(&reversed,3u,3u)==UINT32_C(0xff0000ff));
    quantapdf_ir_raster_image_dispose(&a);
    quantapdf_ir_raster_image_dispose(&b);
    quantapdf_ir_raster_image_dispose(&reversed);
    return EXIT_SUCCESS;
}
static int test_negative_recovery(void)
{
    unsigned char blue[]={0u,0u,255u},alpha[]={128u};
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[2];
    quantapdf_composer_path_command commands[5];
    quantapdf_composer_image_state image;
    quantapdf_ir ir;
    quantapdf_ir_raster_image output={0};
    quantapdf_ir_raster_mixed_v1_limits limits={64u,128u,200000u,1u,16u};
    setup(&ir,&page,ops,commands,&image,blue,alpha);
    ops[1].kind=QUANTAPDF_COMPOSER_OPERATION_TEXT;
    CHECK(expect_refusal(&ir,&limits)==0);
    ops[1].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    ops[1].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_COVER;
    CHECK(expect_refusal(&ir,&limits)==0);
    ops[1].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    ops[1].value.image.image_id=2u;
    CHECK(expect_refusal(&ir,&limits)==0);
    ops[1].value.image.image_id=1u;
    ops[1].bounds.x1=8.1f;
    CHECK(expect_refusal(&ir,&limits)==0);
    ops[1].bounds.x1=4.0f;
    image.size=2u;
    CHECK(expect_refusal(&ir,&limits)==0);
    image.size=3u;
    ops[0].value.path.options.fill_argb=UINT32_C(0x80ff0000);
    CHECK(expect_refusal(&ir,&limits)==0);
    ops[0].value.path.options.fill_argb=UINT32_C(0xffff0000);
    limits.max_pixels=63u;CHECK(expect_refusal(&ir,&limits)==0);
    limits.max_pixels=64u;
    limits.max_drawn_pixels=15u;CHECK(expect_refusal(&ir,&limits)==0);
    limits.max_drawn_pixels=16u;
    ir.resources.graphics_state_count=1u;
    CHECK(expect_refusal(&ir,&limits)==0);
    ir.resources.graphics_state_count=0u;
    ir.resources.font_count=1u;CHECK(expect_refusal(&ir,&limits)==0);
    ir.resources.font_count=0u;
    ir.operation_count=1u;CHECK(expect_refusal(&ir,&limits)==0);
    ir.operation_count=2u;
    OK(quantapdf_ir_raster_mixed_path_image_v1(&ir,0u,&limits,&output));
    CHECK(at(&output,3u,3u)==UINT32_C(0x7f0080ff));
    quantapdf_ir_raster_image_dispose(&output);
    return EXIT_SUCCESS;
}
int main(void)
{
    if(test_order_and_ownership()!=0 || test_negative_recovery()!=0)
        return EXIT_FAILURE;
    puts("Native mixed Path/RAW Image V1: source order, alpha, "
         "detached ownership, bounded late failure and recovery PASS");
    return EXIT_SUCCESS;
}
