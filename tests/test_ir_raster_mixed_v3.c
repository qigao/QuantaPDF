#include "render/ir_raster_mixed_v1.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if(!(e)) { \
    fprintf(stderr,"%s:%d: mixed V3: %s\n",__FILE__,__LINE__,#e); \
    return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

static uint32_t rgba(const quantapdf_ir_raster_image *im,size_t x,size_t y)
{
    const unsigned char *pixel=im->pixels+y*im->stride+4u*x;
    return ((uint32_t)pixel[0]<<24u)|((uint32_t)pixel[1]<<16u)|
           ((uint32_t)pixel[2]<<8u)|pixel[3];
}

static void rectangle(quantapdf_composer_path_command *cmd,
                      float x0,float y0,float x1,float y1)
{
    const float x[4]={x0,x1,x1,x0},y[4]={y0,y0,y1,y1};
    size_t i;
    memset(cmd,0,5u*sizeof(*cmd));
    for(i=0u;i<4u;++i) {
        cmd[i].kind=i==0u? QUANTAPDF_COMPOSER_PATH_MOVE_TO:
                            QUANTAPDF_COMPOSER_PATH_LINE_TO;
        cmd[i].point1.x=x[i];cmd[i].point1.y=y[i];
    }
    cmd[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
}

static void init(quantapdf_ir *ir,quantapdf_composer_page_state *page,
                 quantapdf_composer_operation ops[4],
                 quantapdf_composer_path_command p1[5],
                 quantapdf_composer_path_command p2[5],
                 quantapdf_composer_image_state imgs[2],
                 unsigned char blue[3],unsigned char *half,
                 unsigned char green[3])
{
    size_t i;
    memset(ir,0,sizeof(*ir));memset(page,0,sizeof(*page));
    memset(ops,0,4u*sizeof(*ops));memset(imgs,0,2u*sizeof(*imgs));
    page->width_points=8.0f;page->height_points=8.0f;
    page->background_argb=UINT32_C(0xffffffff);
    ir->pages=page;ir->page_count=1u;
    ir->operations=ops;ir->operation_count=4u;
    ir->resources.images=imgs;ir->resources.image_count=2u;
    rectangle(p1,1.0f,1.0f,7.0f,7.0f);
    rectangle(p2,3.0f,3.0f,5.0f,5.0f);
    for(i=0u;i<2u;++i) {
        ops[i].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
        ops[i].value.path.commands=i==0u?p1:p2;
        ops[i].value.path.command_count=5u;
        ops[i].value.path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
        ops[i].value.path.options.fill=1;
        ops[i].value.path.options.fill_argb=i==0u?
            UINT32_C(0xffff0000):UINT32_C(0xffffff00);
        ops[i].value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    }
    imgs[0].data=blue;imgs[0].size=3u;imgs[0].width=1u;imgs[0].height=1u;
    imgs[0].alpha_data=half;imgs[0].alpha_size=1u;imgs[0].has_alpha=1;
    imgs[0].components=3;imgs[0].format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    imgs[1].data=green;imgs[1].size=3u;imgs[1].width=1u;imgs[1].height=1u;
    imgs[1].components=3;imgs[1].format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    for(i=2u;i<4u;++i) {
        ops[i].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
        ops[i].value.image.image_id=(quantapdf_composer_image_id)(i-1u);
        ops[i].value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
        ops[i].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    }
    ops[2].bounds=(quantapdf_rect){0,0,4,4};
    ops[3].bounds=(quantapdf_rect){4,4,8,8};
}

static int refused(quantapdf_ir *ir,
                   const quantapdf_ir_raster_mixed_v1_limits *limits)
{
    quantapdf_ir_raster_image output;
    memset(&output,0xaa,sizeof(output));
    CHECK(quantapdf_ir_raster_mixed_two_paths_two_images_v3(
        ir,0u,limits,&output)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(output.pixels==NULL && output.size==0u &&
          output.width==0u && output.height==0u && output.stride==0u);
    return EXIT_SUCCESS;
}

static int six_orders(void)
{
    static const unsigned order[6][4]={
        {0u,1u,2u,3u},{0u,2u,1u,3u},{0u,2u,3u,1u},
        {2u,0u,1u,3u},{2u,0u,3u,1u},{2u,3u,0u,1u}
    };
    unsigned char blue[3]={0,0,255},green[3]={0,255,0},half=128u;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[4],original_ops[4];
    quantapdf_composer_path_command p1[5],p2[5];
    quantapdf_composer_image_state imgs[2];
    quantapdf_ir ir;
    const quantapdf_ir_raster_mixed_v1_limits limits={64u,128u,400000u,2u,32u};
    size_t n,i;
    init(&ir,&page,ops,p1,p2,imgs,blue,&half,green);
    memcpy(original_ops,ops,sizeof(ops));
    for(n=0u;n<6u;++n) {
        quantapdf_ir_raster_image out={0},repeat={0};
        for(i=0u;i<4u;++i)ops[i]=original_ops[order[n][i]];
        OK(quantapdf_ir_raster_mixed_two_paths_two_images_v3(
            &ir,0u,&limits,&out));
        OK(quantapdf_ir_raster_mixed_two_paths_two_images_v3(
            &ir,0u,&limits,&repeat));
        CHECK(out.width==8u && out.height==8u && out.stride==32u &&
              out.size==256u && out.size==repeat.size &&
              memcmp(out.pixels,repeat.pixels,out.size)==0);
        CHECK(rgba(&out,0u,0u)==UINT32_C(0x7f7fffff));
        CHECK(rgba(&out,7u,0u)==UINT32_C(0xffffffff));
        CHECK(rgba(&out,3u,3u)==(n==0u ?
              UINT32_C(0x7f7f80ff):UINT32_C(0xffff00ff)));
        CHECK(rgba(&out,2u,2u)==(n<3u ?
              UINT32_C(0x7f0080ff):UINT32_C(0xff0000ff)));
        CHECK(rgba(&out,4u,4u)==((n==2u || n>=4u) ?
              UINT32_C(0xffff00ff):UINT32_C(0x00ff00ff)));
        CHECK(rgba(&out,6u,6u)==(n==5u ?
              UINT32_C(0xff0000ff):UINT32_C(0x00ff00ff)));
        quantapdf_ir_raster_image_dispose(&out);
        quantapdf_ir_raster_image_dispose(&repeat);
    }
    /* Reversing source and mutating borrowed IR can never change a
     * previously returned independently owned RGBA result. */
    {
        quantapdf_ir_raster_image out={0};
        for(i=0u;i<4u;++i)ops[i]=original_ops[i];
        OK(quantapdf_ir_raster_mixed_two_paths_two_images_v3(
            &ir,0u,&limits,&out));
        blue[2]=0u;green[1]=0u;p1[0].point1.x=-1.0f;
        CHECK(rgba(&out,3u,3u)==UINT32_C(0x7f7f80ff));
        quantapdf_ir_raster_image_dispose(&out);
    }
    return EXIT_SUCCESS;
}

static int atomic_negative_and_recovery(void)
{
    unsigned char blue[3]={0,0,255},green[3]={0,255,0},half=128u;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[4];
    quantapdf_composer_path_command p1[5],p2[5];
    quantapdf_composer_image_state imgs[2];
    quantapdf_ir ir;
    quantapdf_ir_raster_image positive={0};
    quantapdf_ir_raster_mixed_v1_limits limits={64u,128u,400000u,2u,32u};
    init(&ir,&page,ops,p1,p2,imgs,blue,&half,green);
    ir.operation_count=3u;CHECK(refused(&ir,&limits)==EXIT_SUCCESS);
    ir.operation_count=5u;CHECK(refused(&ir,&limits)==EXIT_SUCCESS);
    ir.operation_count=4u;
    ops[3].kind=QUANTAPDF_COMPOSER_OPERATION_TEXT;
    CHECK(refused(&ir,&limits)==EXIT_SUCCESS);
    ops[3].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    ops[3].value.image.image_id=1u;CHECK(refused(&ir,&limits)==EXIT_SUCCESS);
    ops[3].value.image.image_id=2u;
    ir.resources.image_count=1u;CHECK(refused(&ir,&limits)==EXIT_SUCCESS);
    ir.resources.image_count=2u;
    limits.max_source_pixels=1u;CHECK(refused(&ir,&limits)==EXIT_SUCCESS);
    limits.max_source_pixels=2u;
    limits.max_drawn_pixels=31u;CHECK(refused(&ir,&limits)==EXIT_SUCCESS);
    limits.max_drawn_pixels=32u;
    limits.max_pixels=63u;CHECK(refused(&ir,&limits)==EXIT_SUCCESS);
    limits.max_pixels=64u;
    limits.max_edges=7u;CHECK(refused(&ir,&limits)==EXIT_SUCCESS);
    limits.max_edges=128u;
    limits.max_sample_tests=1u;CHECK(refused(&ir,&limits)==EXIT_SUCCESS);
    limits.max_sample_tests=400000u;
    ops[1].value.path.options.fill_argb=UINT32_C(0x80ffff00);
    CHECK(refused(&ir,&limits)==EXIT_SUCCESS);
    ops[1].value.path.options.fill_argb=UINT32_C(0xffffff00);
    ops[1].value.path.command_count=0u;CHECK(refused(&ir,&limits)==EXIT_SUCCESS);
    ops[1].value.path.command_count=5u;
    ops[2].bounds.x0=0.25f;CHECK(refused(&ir,&limits)==EXIT_SUCCESS);
    ops[2].bounds.x0=0u;
    imgs[1].size=2u;CHECK(refused(&ir,&limits)==EXIT_SUCCESS);
    imgs[1].size=3u;
    ir.resources.clip_count=1u;CHECK(refused(&ir,&limits)==EXIT_SUCCESS);
    ir.resources.clip_count=0u;
    OK(quantapdf_ir_raster_mixed_two_paths_two_images_v3(
        &ir,0u,&limits,&positive));
    CHECK(rgba(&positive,3u,3u)==UINT32_C(0x7f7f80ff));
    quantapdf_ir_raster_image_dispose(&positive);
    return EXIT_SUCCESS;
}

int main(void)
{
    if(six_orders()!=EXIT_SUCCESS ||
       atomic_negative_and_recovery()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    puts("[native-mixed-v3] two PATH and two RAW Image paints in all six "
         "source orders; cumulative budgets/atomic failure PASS");
    return EXIT_SUCCESS;
}
