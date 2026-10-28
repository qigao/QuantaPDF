#include "render/native_device_ir_v1.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"device IR at %d: %s\n",__LINE__,#x); return 1; } }while(0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)

static uint32_t at(const quantapdf_ir_raster_image *im,size_t x,size_t y) {
    const unsigned char *p=im->pixels+y*im->stride+x*4u;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|((uint32_t)p[2]<<8u)|p[3];
}
static int denied(const quantapdf_ir *ir,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *lim) {
    quantapdf_ir_raster_image out;
    memset(&out,0xa5,sizeof(out));
    CHECK(quantapdf_native_device_ir_render_v1(ir,dpi,lim,&out)
          ==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(out.pixels==NULL && out.size==0u && out.width==0u);
    return 0;
}
int main(void) {
    quantapdf_composer_page_state page={0};
    quantapdf_composer_path_command commands[5]={{0}};
    quantapdf_composer_operation op={0};
    quantapdf_composer_image_state raw={0};
    quantapdf_ir ir={0};
    quantapdf_ir_raster_image out={0},again={0};
    quantapdf_ir_raster_scoped_mixed_v5_limits lim={
        512u,4u,512u,8000000u,512u,512u
    };
    unsigned char sample[3]={255u,0u,0u};
    page.width_points=8.0f;page.height_points=8.0f;
    page.background_argb=UINT32_C(0xffffffff);
    ir.pages=&page;ir.page_count=1u;
    ir.operations=&op;ir.operation_count=1u;
    op.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    op.value.path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    op.value.path.options.fill=1;
    op.value.path.options.fill_argb=UINT32_C(0xff0000ff);
    op.value.path.commands=commands;op.value.path.command_count=5u;
    commands[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[1].point1.x=4.0f;
    commands[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[2].point1.x=4.0f;commands[2].point1.y=4.0f;
    commands[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[3].point1.y=4.0f;
    commands[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
    /* True 4/3-device-space subpixel edge, not 72-DPI enlargement. */
    OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&out));
    CHECK(out.width==11u && out.height==11u && out.size==484u);
    CHECK(at(&out,4u,2u)==UINT32_C(0x0000ffff));
    CHECK(at(&out,5u,2u)==UINT32_C(0xbfbfffff));
    CHECK(at(&out,6u,2u)==UINT32_C(0xffffffff));
    CHECK(page.width_points==8.0f && commands[1].point1.x==4.0f);
    OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&again));
    CHECK(out.size==again.size && memcmp(out.pixels,again.pixels,out.size)==0);
    quantapdf_ir_raster_image_dispose(&again);
    quantapdf_ir_raster_image_dispose(&out);
    OK(quantapdf_native_device_ir_render_v1(&ir,144u,&lim,&out));
    CHECK(out.width==16u && out.height==16u &&
          at(&out,7u,2u)==UINT32_C(0x0000ffff) &&
          at(&out,8u,2u)==UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&out);
    lim.max_pixels=100u;CHECK(denied(&ir,96u,&lim)==0);
    lim.max_pixels=512u;CHECK(denied(&ir,72u,&lim)==0);
    /* RAW image source is re-sampled on true target grid. */
    op=(quantapdf_composer_operation){0};
    op.kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    op.value.image.image_id=(quantapdf_composer_image_id)1;
    op.value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    op.value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    op.bounds=(quantapdf_rect){0,0,8,8};
    raw.format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    raw.data=sample;raw.size=3u;raw.width=raw.height=1u;raw.components=3;
    ir.resources.images=&raw;ir.resources.image_count=1u;
    ir.resources.image_capacity=1u;
    OK(quantapdf_native_device_ir_render_v1(&ir,144u,&lim,&out));
    CHECK(out.width==16u && at(&out,15u,15u)==UINT32_C(0xff0000ff));
    quantapdf_ir_raster_image_dispose(&out);
    CHECK(denied(&ir,96u,&lim)==0); /* fractional 4/3 Image edges */
    op.bounds=(quantapdf_rect){0,0,6,6};
    OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&out));
    CHECK(at(&out,7u,7u)==UINT32_C(0xff0000ff) &&
          at(&out,8u,8u)==UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&out);
    lim.max_drawn_pixels=63u;CHECK(denied(&ir,96u,&lim)==0);
    lim.max_drawn_pixels=512u;
    ir.resources.fonts=(quantapdf_composer_font_state *)(uintptr_t)1u;
    CHECK(denied(&ir,96u,&lim)==0);
    ir.resources.fonts=NULL;
    op.kind=QUANTAPDF_COMPOSER_OPERATION_TEXT;
    CHECK(denied(&ir,144u,&lim)==0);
    ir.operations=NULL;ir.operation_count=0u;
    ir.resources.images=NULL;ir.resources.image_count=0u;
    ir.resources.image_capacity=0u;
    OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&out));
    CHECK(out.width==11u && at(&out,10u,10u)==UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&out);
    return 0;
}
