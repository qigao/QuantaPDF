#include "render/ir_raster_mixed_v1.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"%s:%d: native mixed V3: %s\n",__FILE__,__LINE__,#x); \
    return EXIT_FAILURE; \
} } while(0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)

static uint32_t pixel(const quantapdf_ir_raster_image *im,size_t x,size_t y)
{
    const unsigned char *p=im->pixels+y*im->stride+4u*x;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
           ((uint32_t)p[2]<<8u)|p[3];
}

static void rectangle(quantapdf_composer_path_command *cmd,
                      float x0,float y0,float x1,float y1)
{
    const float x[]={x0,x1,x1,x0};
    const float y[]={y0,y0,y1,y1};
    size_t i;
    memset(cmd,0,5u*sizeof(*cmd));
    for(i=0u;i<4u;++i){
        cmd[i].kind=i==0u?QUANTAPDF_COMPOSER_PATH_MOVE_TO:
                             QUANTAPDF_COMPOSER_PATH_LINE_TO;
        cmd[i].point1.x=x[i];cmd[i].point1.y=y[i];
    }
    cmd[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
}
static void init(quantapdf_ir *ir,quantapdf_composer_page_state *page,
                 quantapdf_composer_operation *ops,
                 quantapdf_composer_path_command *cmds,
                 quantapdf_composer_image_state *imgs,
                 unsigned char *blue,unsigned char *alpha,
                 unsigned char *green)
{
    size_t i;
    memset(ir,0,sizeof(*ir));
    memset(page,0,sizeof(*page));
    memset(ops,0,4u*sizeof(*ops));
    memset(cmds,0,10u*sizeof(*cmds));
    memset(imgs,0,2u*sizeof(*imgs));
    page->width_points=8.0f;page->height_points=8.0f;
    page->background_argb=UINT32_C(0xffffffff);
    ir->pages=page;ir->page_count=1u;
    ir->operations=ops;ir->operation_count=4u;
    ir->resources.images=imgs;ir->resources.image_count=2u;
    rectangle(cmds,2.0f,2.0f,6.0f,6.0f);
    rectangle(cmds+5,4.0f,4.0f,7.0f,7.0f);
    for(i=0u;i<2u;++i){
        ops[2u*i].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
        ops[2u*i].value.path.commands=cmds+5u*i;
        ops[2u*i].value.path.command_count=5u;
        ops[2u*i].value.path.options.struct_size=
            QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
        ops[2u*i].value.path.options.fill=1;
        ops[2u*i].value.path.options.fill_argb=
            i==0u?UINT32_C(0xffff0000):UINT32_C(0xffffff00);
        ops[2u*i].value.path.options.fill_rule=
            QUANTAPDF_COMPOSER_FILL_NONZERO;
    }
    imgs[0].data=blue;imgs[0].size=3u;
    imgs[0].alpha_data=alpha;imgs[0].alpha_size=1u;
    imgs[0].has_alpha=1;imgs[0].width=1u;imgs[0].height=1u;
    imgs[0].components=3;imgs[0].format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    imgs[1].data=green;imgs[1].size=3u;
    imgs[1].width=1u;imgs[1].height=1u;
    imgs[1].components=3;imgs[1].format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    ops[1].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    ops[1].value.image.image_id=1u;
    ops[1].value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    ops[1].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    ops[1].bounds=(quantapdf_rect){0,0,4,4};
    ops[3].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    ops[3].value.image.image_id=2u;
    ops[3].value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    ops[3].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    ops[3].bounds=(quantapdf_rect){3,3,7,7};
}
static int refuse(quantapdf_ir *ir,
                  const quantapdf_ir_raster_mixed_v1_limits *limits)
{
    quantapdf_ir_raster_image output;
    memset(&output,0xab,sizeof(output));
    CHECK(quantapdf_ir_raster_mixed_two_paths_two_images_v3(
        ir,0u,limits,&output)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(output.pixels==NULL && output.size==0u &&
          output.stride==0u && output.width==0u && output.height==0u);
    return EXIT_SUCCESS;
}
static int test_order_and_ownership(void)
{
    unsigned char blue[]={0u,0u,255u},alpha[]={128u};
    unsigned char green[]={0u,255u,0u};
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[4],old[4];
    quantapdf_composer_path_command cmds[10];
    quantapdf_composer_image_state imgs[2];
    quantapdf_ir_raster_image a={0},b={0},reversed={0},clear={0};
    const quantapdf_ir_raster_mixed_v1_limits limits={
        64u,128u,400000u,2u,32u
    };
    init(&ir,&page,ops,cmds,imgs,blue,alpha,green);
    memcpy(old,ops,sizeof(old));
    OK(quantapdf_ir_raster_mixed_two_paths_two_images_v3(
        &ir,0u,&limits,&a));
    OK(quantapdf_ir_raster_mixed_two_paths_two_images_v3(
        &ir,0u,&limits,&b));
    CHECK(a.size==256u && a.stride==32u && a.width==8u && a.height==8u &&
          b.size==a.size && memcmp(a.pixels,b.pixels,a.size)==0);
    CHECK(pixel(&a,0u,0u)==UINT32_C(0x7f7fffff));
    CHECK(pixel(&a,2u,2u)==UINT32_C(0x7f0080ff));
    CHECK(pixel(&a,3u,3u)==UINT32_C(0x00ff00ff));
    CHECK(pixel(&a,6u,6u)==UINT32_C(0x00ff00ff));
    CHECK(pixel(&a,7u,7u)==UINT32_C(0xffffffff));

    /* The same immutable resources in another source order paint genuinely
     * different pixels. Neither V1 nor V2 should accidentally accept four. */
    ops[0]=old[1];ops[1]=old[3];ops[2]=old[0];ops[3]=old[2];
    OK(quantapdf_ir_raster_mixed_two_paths_two_images_v3(
        &ir,0u,&limits,&reversed));
    CHECK(pixel(&reversed,2u,2u)==UINT32_C(0xffff0000));
    CHECK(pixel(&reversed,4u,4u)==UINT32_C(0xffffff00));
    CHECK(pixel(&reversed,0u,0u)==UINT32_C(0x7f7fffff));
    page.suppress_background=1;
    OK(quantapdf_ir_raster_mixed_two_paths_two_images_v3(
        &ir,0u,&limits,&clear));
    CHECK(pixel(&clear,7u,7u)==0u);
    CHECK(pixel(&clear,2u,2u)==UINT32_C(0xffff0000));
    /* Detach result: overwriting borrowed input must not alter prior pixels. */
    blue[2]=0u;green[1]=0u;alpha[0]=0u;
    cmds[0].point1.x=-10.0f;
    CHECK(pixel(&a,3u,3u)==UINT32_C(0x00ff00ff));
    quantapdf_ir_raster_image_dispose(&a);
    quantapdf_ir_raster_image_dispose(&b);
    quantapdf_ir_raster_image_dispose(&reversed);
    quantapdf_ir_raster_image_dispose(&clear);
    return EXIT_SUCCESS;
}
static int test_atomic_budgets_and_recovery(void)
{
    unsigned char blue[]={0u,0u,255u},alpha[]={128u};
    unsigned char green[]={0u,255u,0u};
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[4],valid;
    quantapdf_composer_path_command cmds[10];
    quantapdf_composer_image_state imgs[2];
    quantapdf_ir_raster_image output={0};
    quantapdf_ir_raster_mixed_v1_limits limits={
        64u,128u,400000u,2u,32u
    };
    init(&ir,&page,ops,cmds,imgs,blue,alpha,green);
    ir.operation_count=3u;CHECK(refuse(&ir,&limits)==EXIT_SUCCESS);
    ir.operation_count=5u;CHECK(refuse(&ir,&limits)==EXIT_SUCCESS);
    ir.operation_count=4u;
    valid=ops[3];ops[3].kind=QUANTAPDF_COMPOSER_OPERATION_TEXT;
    CHECK(refuse(&ir,&limits)==EXIT_SUCCESS);
    ops[3]=valid;
    ops[3].value.image.image_id=1u;CHECK(refuse(&ir,&limits)==EXIT_SUCCESS);
    ops[3].value.image.image_id=2u;
    ir.resources.image_count=1u;CHECK(refuse(&ir,&limits)==EXIT_SUCCESS);
    ir.resources.image_count=2u;
    limits.max_source_pixels=1u;CHECK(refuse(&ir,&limits)==EXIT_SUCCESS);
    limits.max_source_pixels=2u;
    limits.max_drawn_pixels=31u;CHECK(refuse(&ir,&limits)==EXIT_SUCCESS);
    limits.max_drawn_pixels=32u;
    limits.max_edges=1u;CHECK(refuse(&ir,&limits)==EXIT_SUCCESS);
    limits.max_edges=128u;
    limits.max_sample_tests=1u;CHECK(refuse(&ir,&limits)==EXIT_SUCCESS);
    limits.max_sample_tests=400000u;
    ops[2].value.path.options.fill_argb=UINT32_C(0x80ffff00);
    CHECK(refuse(&ir,&limits)==EXIT_SUCCESS);
    ops[2].value.path.options.fill_argb=UINT32_C(0xffffff00);
    ops[1].bounds.x0=0.5f;CHECK(refuse(&ir,&limits)==EXIT_SUCCESS);
    ops[1].bounds.x0=0.0f;
    ir.resources.clip_count=1u;CHECK(refuse(&ir,&limits)==EXIT_SUCCESS);
    ir.resources.clip_count=0u;
    /* Earlier failed draws cannot poison subsequent input or outputs. */
    OK(quantapdf_ir_raster_mixed_two_paths_two_images_v3(
        &ir,0u,&limits,&output));
    CHECK(pixel(&output,3u,3u)==UINT32_C(0x00ff00ff));
    quantapdf_ir_raster_image_dispose(&output);
    return EXIT_SUCCESS;
}
int main(void)
{
    if(test_order_and_ownership()!=EXIT_SUCCESS ||
       test_atomic_budgets_and_recovery()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    puts("[native-mixed-v3] two PATHs + two RAW Images source order, "
         "alpha, cumulative budgets, fail-closed recovery PASS");
    return EXIT_SUCCESS;
}
