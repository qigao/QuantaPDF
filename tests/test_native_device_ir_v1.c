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
    unsigned char old_fractional_pixels[11u*11u*4u]={0};
    unsigned char source_alpha=128u;
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
    /* The same 1x1 RAW resource is sampled against the real 11x11
     * device grid. Its bottom/right fractional 4/3 edge covers 12/16
     * subpixel samples, and the corner covers 9/16. */
    OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&out));
    CHECK(out.width==11u && out.height==11u &&
          at(&out,0u,0u)==UINT32_C(0xff0000ff) &&
          at(&out,10u,5u)==UINT32_C(0xff4040ff) &&
          at(&out,10u,10u)==UINT32_C(0xff7070ff));
    OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&again));
    CHECK(out.size==again.size &&
          memcmp(out.pixels,again.pixels,out.size)==0);
    quantapdf_ir_raster_image_dispose(&again);
    quantapdf_ir_raster_image_dispose(&out);

    op.bounds=(quantapdf_rect){2,2,6,6};
    OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&out));
    CHECK(out.width==11u && out.height==11u &&
          out.size==sizeof(old_fractional_pixels) &&
          at(&out,2u,2u)==UINT32_C(0xffefefff) &&
          at(&out,3u,3u)==UINT32_C(0xff0000ff) &&
          at(&out,8u,8u)==UINT32_C(0xffffffff));
    memcpy(old_fractional_pixels,out.pixels,out.size);
    quantapdf_ir_raster_image_dispose(&out);

    /* A frozen Clip matching the fractional source bounds must NOT
     * square its coverage alpha: both shapes share one 4x4 grid. */
    {
        quantapdf_composer_graphics_state gs={0};
        quantapdf_composer_clip_state clip={0};
        quantapdf_composer_path_command clip_path[5]={{0}};
        gs.fill_alpha=gs.stroke_alpha=1.0f;
        gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
        gs.clip_id=(quantapdf_composer_clip_id)1u;
        clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        clip.commands=clip_path;clip.command_count=5u;
        clip.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
        clip.transform=(quantapdf_affine_transform){1,0,0,1,0,0};
        clip_path[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
        clip_path[0].point1=(quantapdf_point){2,2};
        clip_path[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
        clip_path[1].point1=(quantapdf_point){6,2};
        clip_path[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
        clip_path[2].point1=(quantapdf_point){6,6};
        clip_path[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
        clip_path[3].point1=(quantapdf_point){2,6};
        clip_path[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
        ir.resources.graphics_states=&gs;
        ir.resources.graphics_state_count=1u;
        ir.resources.graphics_state_capacity=1u;
        ir.resources.clips=&clip;
        ir.resources.clip_count=1u;
        ir.resources.clip_capacity=1u;
        op.graphics_state_id=(quantapdf_composer_graphics_state_id)1u;
        op.value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
        op.value.image.options.graphics_state_id=
            (quantapdf_composer_graphics_state_id)1u;
        OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&out));
        CHECK(out.size==sizeof(old_fractional_pixels) &&
              memcmp(out.pixels,old_fractional_pixels,out.size)==0 &&
              at(&out,2u,2u)==UINT32_C(0xffefefff));
        quantapdf_ir_raster_image_dispose(&out);
        lim.max_sample_tests=128u;
        CHECK(denied(&ir,96u,&lim)==0);
        lim.max_sample_tests=8000000u;
        /* The color remains source-owned; only the completed pixels
         * survive. Effective alpha is source-alpha times ONE mask. */
        raw.has_alpha=1;
        raw.alpha_data=&source_alpha;
        raw.alpha_size=1u;
        OK(quantapdf_native_device_ir_render_v1(&ir,96u,&lim,&out));
        CHECK(at(&out,2u,2u)==UINT32_C(0xfff7f7ff));
        quantapdf_ir_raster_image_dispose(&out);
        raw.has_alpha=0;raw.alpha_data=NULL;raw.alpha_size=0u;
        ir.resources.graphics_states=NULL;
        ir.resources.graphics_state_count=0u;
        ir.resources.graphics_state_capacity=0u;
        ir.resources.clips=NULL;
        ir.resources.clip_count=0u;
        ir.resources.clip_capacity=0u;
        op.graphics_state_id=(quantapdf_composer_graphics_state_id)0u;
        op.value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
        op.value.image.options.graphics_state_id=
            (quantapdf_composer_graphics_state_id)0u;
    }
    /* Exact 72-DPI/int-only V5 entry remains restrictive; only the
     * explicit native device entry admits fractional image geometry. */
    {
        quantapdf_ir borrowed=ir;
        quantapdf_composer_page_state device_page=page;
        quantapdf_composer_operation device_op=op;
        quantapdf_ir_raster_image rejected={0};
        device_page.width_points=11.0f;
        device_page.height_points=11.0f;
        device_op.bounds=(quantapdf_rect){
            8.0f/3.0f,8.0f/3.0f,8.0f,8.0f};
        borrowed.pages=&device_page;
        borrowed.operations=&device_op;
        CHECK(quantapdf_ir_raster_scoped_mixed_v5(
                &borrowed,0u,&lim,&rejected)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(rejected.pixels==NULL && rejected.size==0u);
    }
    lim.max_drawn_pixels=35u;
    CHECK(denied(&ir,96u,&lim)==0); /* ceil-box 6x6, not 4x4 */
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
