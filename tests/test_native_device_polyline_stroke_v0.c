#include "render/ir_device_polyline_stroke_v0.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); \
    return EXIT_FAILURE; } } while (0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)
#define BAD(x) CHECK((x)==QUANTAPDF_ERROR_UNSUPPORTED)

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation op;
    quantapdf_composer_path_command cmds[5];
    quantapdf_native_device_grid_geometry geom;
    quantapdf_native_device_grid_limits grid_budget;
    quantapdf_ir_raster_v1_limits raster_budget;
} fixture;

static uint32_t pixel(const quantapdf_ir_raster_image *im,size_t x,size_t y)
{
    const unsigned char *p=im->pixels+y*im->stride+x*4u;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
           ((uint32_t)p[2]<<8u)|(uint32_t)p[3];
}
static unsigned alpha(const quantapdf_ir_raster_image *im,size_t x,size_t y)
{
    return im->pixels[y*im->stride+x*4u+3u];
}
static void setup(fixture *f)
{
    memset(f,0,sizeof(*f));
    f->geom=(quantapdf_native_device_grid_geometry){
        32.5,24.75,0.0,0.0,1.0,0,0,72.0f
    };
    f->grid_budget=(quantapdf_native_device_grid_limits){
        1200u,4800u,64u,2000000u
    };
    f->raster_budget=(quantapdf_ir_raster_v1_limits){
        1200u,1u,64u,2000000u
    };
    f->page.width_points=32.5f;
    f->page.height_points=24.75f;
    f->page.suppress_background=1;
    f->ir.pages=&f->page;
    f->ir.page_count=1u;
    f->ir.operations=&f->op;
    f->ir.operation_count=1u;
    f->op.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    f->op.value.path.commands=f->cmds;
    f->op.value.path.command_count=3u;
    f->op.value.path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    f->op.value.path.options.stroke=1;
    f->op.value.path.options.fill=0;
    f->op.value.path.options.stroke_argb=UINT32_C(0xffbf4010);
    f->op.value.path.options.stroke_width=4.0f;
    f->op.value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    f->op.value.path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    f->op.value.path.options.miter_limit=10.0f;
    f->op.value.path.options.transform=
        (quantapdf_affine_transform){1,0,0,1,0,0};
    f->cmds[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    f->cmds[0].point1=(quantapdf_point){2.0f,8.0f};
    f->cmds[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f->cmds[1].point1=(quantapdf_point){20.0f,8.0f};
    f->cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f->cmds[2].point1=(quantapdf_point){20.0f,20.0f};
}
#define RENDER(f,image) quantapdf_ir_raster_device_polyline_stroke_v0(\
    &(f).ir,0u,&(f).geom,&(f).grid_budget,&(f).raster_budget,&(image))

static int test_miter_and_butt_and_repeat(void)
{
    fixture f;
    quantapdf_ir_raster_image miter={0},repeat={0},bevel={0};
    quantapdf_composer_path_command original[5];
    quantapdf_composer_path_options original_opt;
    setup(&f);
    memcpy(original,f.cmds,sizeof(original));
    original_opt=f.op.value.path.options;
    OK(RENDER(f,miter));
    CHECK(miter.width==33u && miter.height==25u && miter.stride==132u);
    CHECK(miter.size==3300u);
    CHECK(pixel(&miter,10u,8u)==UINT32_C(0xbf4010ff)); /* horizontal */
    CHECK(pixel(&miter,20u,12u)==UINT32_C(0xbf4010ff)); /* vertical */
    CHECK(alpha(&miter,0u,8u)==0u); /* butt end does not extend backward */
    CHECK(alpha(&miter,25u,8u)==0u);
    CHECK(alpha(&miter,21u,6u)==255u); /* outer miter wedge */
    CHECK(alpha(&miter,22u,6u)>0u && alpha(&miter,22u,6u)<255u);
    OK(RENDER(f,repeat));
    CHECK(repeat.size==miter.size &&
          memcmp(repeat.pixels,miter.pixels,miter.size)==0);
    CHECK(memcmp(f.cmds,original,sizeof(original))==0);
    CHECK(memcmp(&f.op.value.path.options,&original_opt,sizeof(original_opt))==0);
    quantapdf_ir_raster_image_dispose(&repeat);

    f.op.value.path.options.miter_limit=1.0f; /* bevel fallback */
    OK(RENDER(f,bevel));
    CHECK(alpha(&bevel,21u,6u)<alpha(&miter,21u,6u));
    CHECK(alpha(&bevel,10u,8u)==255u);
    quantapdf_ir_raster_image_dispose(&bevel);

    /* Repeated overlapping segments within one source Stroke are a SET,
     * not repeated source-over alpha; retrace the same L path. */
    f.op.value.path.options.miter_limit=10.0f;
    f.cmds[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[3].point1=(quantapdf_point){20.0f,8.0f};
    f.cmds[4].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[4].point1=(quantapdf_point){2.0f,8.0f};
    f.op.value.path.command_count=5u;
    OK(RENDER(f,repeat));
    CHECK(repeat.size==miter.size);
    CHECK(memcmp(repeat.pixels,miter.pixels,miter.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&miter);
    return EXIT_SUCCESS;
}
static int test_pixel_width_and_rejections(void)
{
    fixture f;
    quantapdf_ir_raster_image im={0};
    setup(&f);
    f.op.value.path.options.stroke_width=1.0f;
    OK(RENDER(f,im));
    CHECK(alpha(&im,10u,8u)>0u && alpha(&im,10u,8u)<255u);
    CHECK(alpha(&im,10u,6u)==0u);
    quantapdf_ir_raster_image_dispose(&im);
#define REFUSE(call) do{ \
    memset(&im,0xa5,sizeof(im)); \
    BAD(call); \
    CHECK(im.pixels==NULL&&im.width==0u&&im.height==0u&&im.size==0u&&im.stride==0u); \
}while(0)
    f.op.value.path.options.stroke_width=0.0f;
    REFUSE(RENDER(f,im)); f.op.value.path.options.stroke_width=1.0f;
    f.op.value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    REFUSE(RENDER(f,im)); f.op.value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    f.op.value.path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL;
    REFUSE(RENDER(f,im)); f.op.value.path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    f.op.value.path.options.fill=1;
    REFUSE(RENDER(f,im)); f.op.value.path.options.fill=0;
    f.op.value.path.options.transform.e=1.0f;
    REFUSE(RENDER(f,im)); f.op.value.path.options.transform.e=0.0f;
    f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    REFUSE(RENDER(f,im)); f.cmds[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    f.cmds[2].point1.x=33.0f;
    REFUSE(RENDER(f,im)); f.cmds[2].point1.x=20.0f;
    f.op.value.path.options.stroke_argb=UINT32_C(0x80bf4010);
    REFUSE(RENDER(f,im)); f.op.value.path.options.stroke_argb=UINT32_C(0xffbf4010);
    f.geom.user_unit=2.0;
    REFUSE(RENDER(f,im)); f.geom.user_unit=1.0;
    f.geom.page_width_points=32.000001;
    REFUSE(RENDER(f,im)); f.geom.page_width_points=32.5;
    f.raster_budget.max_edges=2u; /* 2*N edge+join accounting */
    REFUSE(RENDER(f,im)); f.raster_budget.max_edges=64u;
    f.grid_budget.max_sample_tests=30000u;
    REFUSE(RENDER(f,im)); f.grid_budget.max_sample_tests=2000000u;
    f.grid_budget.max_rgba_bytes=3299u;
    REFUSE(RENDER(f,im)); f.grid_budget.max_rgba_bytes=4800u;
#undef REFUSE
    return EXIT_SUCCESS;
}
int main(void)
{
    CHECK(test_miter_and_butt_and_repeat()==EXIT_SUCCESS);
    CHECK(test_pixel_width_and_rejections()==EXIT_SUCCESS);
    puts("native-device-polyline-stroke: butt+miter+bevel+union+limits PASS");
    return EXIT_SUCCESS;
}
