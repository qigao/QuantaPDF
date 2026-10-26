#include "render/ir_raster_mixed_v1.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if(!(e)) { \
    fprintf(stderr, "%s:%d: mixed V2: %s\n",__FILE__,__LINE__,#e); \
    return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

static uint32_t rgba(const quantapdf_ir_raster_image *image,size_t x,size_t y)
{
    const unsigned char *pixel=image->pixels+y*image->stride+4u*x;
    return ((uint32_t)pixel[0]<<24u)|((uint32_t)pixel[1]<<16u)|
           ((uint32_t)pixel[2]<<8u)|pixel[3];
}

static void init(quantapdf_ir *ir,quantapdf_composer_page_state *page,
                 quantapdf_composer_operation *ops,
                 quantapdf_composer_path_command *commands,
                 quantapdf_composer_image_state *images,
                 unsigned char *blue,unsigned char *half,
                 unsigned char *green)
{
    size_t i;
    const float x[]={2,6,6,2},y[]={2,2,6,6};
    memset(ir,0,sizeof(*ir));
    memset(page,0,sizeof(*page));
    memset(ops,0,3u*sizeof(*ops));
    memset(commands,0,5u*sizeof(*commands));
    memset(images,0,2u*sizeof(*images));
    page->width_points=8u;page->height_points=8u;
    page->background_argb=UINT32_C(0xffffffff);
    ir->pages=page;ir->page_count=1u;
    ir->operations=ops;ir->operation_count=3u;
    ir->resources.images=images;ir->resources.image_count=2u;
    for(i=0u;i<4u;++i) {
        commands[i].kind=i==0u? QUANTAPDF_COMPOSER_PATH_MOVE_TO :
                               QUANTAPDF_COMPOSER_PATH_LINE_TO;
        commands[i].point1.x=x[i];commands[i].point1.y=y[i];
    }
    commands[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
    ops[0].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    ops[0].value.path.commands=commands;
    ops[0].value.path.command_count=5u;
    ops[0].value.path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    ops[0].value.path.options.fill=1;
    ops[0].value.path.options.fill_argb=UINT32_C(0xffff0000);
    ops[0].value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    images[0].data=blue;images[0].size=3u;
    images[0].alpha_data=half;images[0].alpha_size=1u;images[0].has_alpha=1;
    images[0].width=1u;images[0].height=1u;images[0].components=3;
    images[0].format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    images[1].data=green;images[1].size=3u;
    images[1].width=1u;images[1].height=1u;images[1].components=3;
    images[1].format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    ops[1].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    ops[1].value.image.image_id=1u;
    ops[1].value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    ops[1].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    ops[1].bounds=(quantapdf_rect){0,0,4,4};
    ops[2].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    ops[2].value.image.image_id=2u;
    ops[2].value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    ops[2].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    ops[2].bounds=(quantapdf_rect){3,3,7,7};
}

static int refuse(quantapdf_ir *ir,
                  const quantapdf_ir_raster_mixed_v1_limits *limits)
{
    quantapdf_ir_raster_image result;
    memset(&result,0xaa,sizeof(result));
    CHECK(quantapdf_ir_raster_mixed_path_two_images_v2(
        ir,0u,limits,&result)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(result.pixels==NULL && result.size==0u &&
          result.width==0u && result.height==0u && result.stride==0u);
    return EXIT_SUCCESS;
}

static int test_three_source_orders(void)
{
    unsigned char blue[]={0,0,255},half[]={128},green[]={0,255,0};
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[3],swap;
    quantapdf_composer_path_command commands[5];
    quantapdf_composer_image_state images[2];
    quantapdf_ir ir;
    quantapdf_ir_raster_image result={0},again={0},reversed={0};
    const quantapdf_ir_raster_mixed_v1_limits limits={
        64u,128u,200000u,2u,32u
    };
    init(&ir,&page,ops,commands,images,blue,half,green);
    OK(quantapdf_ir_raster_mixed_path_two_images_v2(
        &ir,0u,&limits,&result));
    OK(quantapdf_ir_raster_mixed_path_two_images_v2(
        &ir,0u,&limits,&again));
    CHECK(result.size==256u && result.stride==32u &&
          result.width==8u && result.height==8u &&
          again.size==result.size &&
          memcmp(result.pixels,again.pixels,result.size)==0);
    CHECK(rgba(&result,1,1)==UINT32_C(0x7f7fffff));
    CHECK(rgba(&result,2,3)==UINT32_C(0x7f0080ff));
    CHECK(rgba(&result,3,3)==UINT32_C(0x00ff00ff));
    CHECK(rgba(&result,6,6)==UINT32_C(0x00ff00ff));
    CHECK(rgba(&result,7,7)==UINT32_C(0xffffffff));

    swap=ops[0];ops[0]=ops[1];ops[1]=ops[2];ops[2]=swap;
    OK(quantapdf_ir_raster_mixed_path_two_images_v2(
        &ir,0u,&limits,&reversed));
    CHECK(rgba(&reversed,1,1)==UINT32_C(0x7f7fffff));
    CHECK(rgba(&reversed,3,3)==UINT32_C(0xff0000ff));
    CHECK(rgba(&reversed,6,6)==UINT32_C(0x00ff00ff));
    page.suppress_background=1;
    quantapdf_ir_raster_image_dispose(&again);
    OK(quantapdf_ir_raster_mixed_path_two_images_v2(
        &ir,0u,&limits,&again));
    CHECK(rgba(&again,1,1)==UINT32_C(0x0000ff80));
    CHECK(rgba(&again,3,3)==UINT32_C(0xff0000ff));
    CHECK(rgba(&again,7,7)==0u);
    blue[2]=0u;half[0]=0u;green[1]=0u;
    commands[0].point1.x=-12.0f;
    CHECK(rgba(&result,3,3)==UINT32_C(0x00ff00ff));
    quantapdf_ir_raster_image_dispose(&result);
    quantapdf_ir_raster_image_dispose(&again);
    quantapdf_ir_raster_image_dispose(&reversed);
    return EXIT_SUCCESS;
}

static int test_atomic_negative_and_recovery(void)
{
    unsigned char blue[]={0,0,255},half[]={128},green[]={0,255,0};
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[3];
    quantapdf_composer_path_command commands[5];
    quantapdf_composer_image_state images[2];
    quantapdf_ir ir;
    quantapdf_ir_raster_image positive={0};
    quantapdf_ir_raster_mixed_v1_limits limits={64u,128u,200000u,2u,32u};
    init(&ir,&page,ops,commands,images,blue,half,green);
    ir.operation_count=2u;CHECK(refuse(&ir,&limits)==0);
    ir.operation_count=4u;CHECK(refuse(&ir,&limits)==0);
    ir.operation_count=3u;
    ops[2].kind=QUANTAPDF_COMPOSER_OPERATION_TEXT;
    CHECK(refuse(&ir,&limits)==0);
    ops[2].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    ops[2].value.image.image_id=3u;CHECK(refuse(&ir,&limits)==0);
    ops[2].value.image.image_id=1u;CHECK(refuse(&ir,&limits)==0);
    ops[2].value.image.image_id=2u;
    ir.resources.image_count=1u;CHECK(refuse(&ir,&limits)==0);
    ir.resources.image_count=2u;
    limits.max_source_pixels=1u;CHECK(refuse(&ir,&limits)==0);
    limits.max_source_pixels=2u;
    limits.max_drawn_pixels=31u;CHECK(refuse(&ir,&limits)==0);
    limits.max_drawn_pixels=32u;
    limits.max_pixels=63u;CHECK(refuse(&ir,&limits)==0);
    limits.max_pixels=64u;
    images[1].size=2u;CHECK(refuse(&ir,&limits)==0);
    images[1].size=3u;
    ops[1].bounds.x0=0.25f;CHECK(refuse(&ir,&limits)==0);
    ops[1].bounds.x0=0u;
    ops[0].value.path.options.fill_argb=UINT32_C(0x80ff0000);
    CHECK(refuse(&ir,&limits)==0);
    ops[0].value.path.options.fill_argb=UINT32_C(0xffff0000);
    ir.resources.graphics_state_count=1u;
    CHECK(refuse(&ir,&limits)==0);
    ir.resources.graphics_state_count=0u;
    OK(quantapdf_ir_raster_mixed_path_two_images_v2(
        &ir,0u,&limits,&positive));
    CHECK(rgba(&positive,3u,3u)==UINT32_C(0x00ff00ff));
    quantapdf_ir_raster_image_dispose(&positive);
    return EXIT_SUCCESS;
}
int main(void)
{
    if(test_three_source_orders()!=EXIT_SUCCESS ||
       test_atomic_negative_and_recovery()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    puts("[native-mixed-v2] 1 Path + 2 RAW Images source-order, "
         "alpha, cumulative budgets and fail-closed recovery PASS");
    return EXIT_SUCCESS;
}
