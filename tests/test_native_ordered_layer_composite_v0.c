#include "render/ir_ordered_layer_composite_v0.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#e); \
    return EXIT_FAILURE; \
} } while (0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)
#define BAD(e) CHECK((e)==QUANTAPDF_ERROR_UNSUPPORTED)

static int check_order_and_repeat(void)
{
    unsigned char black_stroke[8]={0u,0u,0u,255u,0u,0u,0u,128u};
    unsigned char red_fill[8]={255u,0u,0u,255u,255u,0u,0u,128u};
    quantapdf_ir_raster_image stroke={0},fill={0},out={0},again={0},reverse={0};
    quantapdf_ir_ordered_composite_v0_limits limits={2u,8u};
    stroke.pixels=black_stroke;stroke.width=2u;stroke.height=1u;
    stroke.stride=8u;stroke.size=8u;
    fill.pixels=red_fill;fill.width=2u;fill.height=1u;
    fill.stride=8u;fill.size=8u;
    OK(quantapdf_ir_ordered_layer_composite_v0(&stroke,&fill,&limits,&out));
    CHECK(out.width==2u && out.height==1u && out.size==8u);
    CHECK(out.stride==8u && out.pixels!=NULL);
    CHECK(out.pixels[0]==255u && out.pixels[1]==0u &&
          out.pixels[2]==0u && out.pixels[3]==255u);
    CHECK(out.pixels[4]==170u && out.pixels[5]==0u &&
          out.pixels[6]==0u && out.pixels[7]==192u);
    CHECK(memcmp(stroke.pixels,black_stroke,sizeof(black_stroke))==0);
    CHECK(memcmp(fill.pixels,red_fill,sizeof(red_fill))==0);
    OK(quantapdf_ir_ordered_layer_composite_v0(&stroke,&fill,&limits,&again));
    CHECK(out.size==again.size &&
          memcmp(out.pixels,again.pixels,out.size)==0);
    OK(quantapdf_ir_ordered_layer_composite_v0(&fill,&stroke,&limits,&reverse));
    CHECK(reverse.pixels[0]==0u && reverse.pixels[3]==255u);
    CHECK(memcmp(reverse.pixels,out.pixels,out.size)!=0);
    quantapdf_ir_raster_image_dispose(&reverse);
    quantapdf_ir_raster_image_dispose(&again);
    quantapdf_ir_raster_image_dispose(&out);
    return EXIT_SUCCESS;
}

static int check_fail_closed(void)
{
    unsigned char earlier_pixels[8]={0u,0u,0u,255u,0u,0u,0u,128u};
    unsigned char later_pixels[16]={255u,0u,0u,255u,255u,0u,0u,128u};
    quantapdf_ir_raster_image earlier={0},later={0},output={0};
    quantapdf_ir_ordered_composite_v0_limits limits={2u,8u};
    earlier.pixels=earlier_pixels;
    earlier.width=2u;earlier.height=1u;earlier.stride=8u;earlier.size=8u;
    later.pixels=later_pixels;
    later.width=2u;later.height=1u;later.stride=8u;later.size=8u;
#define REFUSE(expr) do { \
    memset(&output,0xa5,sizeof(output)); \
    BAD(expr); \
    CHECK(output.pixels==NULL && output.width==0u && \
          output.height==0u && output.stride==0u && output.size==0u); \
} while(0)
    limits.max_pixels=1u;
    REFUSE(quantapdf_ir_ordered_layer_composite_v0(
        &earlier,&later,&limits,&output));
    limits.max_pixels=2u;limits.max_output_bytes=7u;
    REFUSE(quantapdf_ir_ordered_layer_composite_v0(
        &earlier,&later,&limits,&output));
    limits.max_output_bytes=8u;
    later.width=1u;later.stride=4u;later.size=4u;
    REFUSE(quantapdf_ir_ordered_layer_composite_v0(
        &earlier,&later,&limits,&output));
    later.width=2u;later.stride=8u;later.size=8u;
    later.height=2u;later.size=16u;
    REFUSE(quantapdf_ir_ordered_layer_composite_v0(
        &earlier,&later,&limits,&output));
    later.height=1u;later.size=8u;
    later.stride=4u;
    memset(&output,0xa5,sizeof(output));
    CHECK(quantapdf_ir_ordered_layer_composite_v0(
        &earlier,&later,&limits,&output)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(output.pixels==NULL && output.size==0u);
    later.stride=8u;
    memset(&output,0xa5,sizeof(output));
    CHECK(quantapdf_ir_ordered_layer_composite_v0(
        NULL,&later,&limits,&output)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(output.pixels==NULL && output.size==0u);
#undef REFUSE
    return EXIT_SUCCESS;
}
/* Opaque-white floor A/B: the reference pixel here is a deterministic
 * arithmetic fixture, not a PDFium lookup or an asserted general rule.
 * Keep the pre-existing straight RGBA8 compositing test unchanged. */
static int check_opaque_white_floor_preview(void)
{
    unsigned char stroke_bytes[16]={
        0u,0u,0u,143u, 0u,0u,0u,0u,
        0u,0u,0u,255u, 0u,0u,0u,128u
    };
    unsigned char fill_bytes[16]={
        255u,0u,0u,240u, 255u,0u,0u,0u,
        255u,0u,0u,255u, 255u,0u,0u,128u
    };
    const unsigned char expected[16]={
        246u,6u,6u,255u, 255u,255u,255u,255u,
        255u,0u,0u,255u, 191u,63u,63u,255u
    };
    quantapdf_ir_raster_image stroke={0},fill={0},out={0},repeat={0};
    quantapdf_ir_ordered_composite_v0_limits limits={4u,16u};
    stroke.pixels=stroke_bytes;stroke.width=4u;stroke.height=1u;
    stroke.stride=16u;stroke.size=16u;
    fill.pixels=fill_bytes;fill.width=4u;fill.height=1u;
    fill.stride=16u;fill.size=16u;
    OK(quantapdf_ir_ordered_layer_composite_opaque_white_floor_preview_v0(
        &stroke,&fill,&limits,&out));
    CHECK(out.size==16u && out.stride==16u && out.width==4u &&
          out.height==1u && out.pixels!=NULL);
    CHECK(memcmp(out.pixels,expected,sizeof(expected))==0);
    OK(quantapdf_ir_ordered_layer_composite_opaque_white_floor_preview_v0(
        &stroke,&fill,&limits,&repeat));
    CHECK(repeat.size==out.size &&
          memcmp(out.pixels,repeat.pixels,out.size)==0);
    CHECK(memcmp(stroke.pixels,stroke_bytes,sizeof(stroke_bytes))==0);
    CHECK(memcmp(fill.pixels,fill_bytes,sizeof(fill_bytes))==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    quantapdf_ir_raster_image_dispose(&out);

#define FLOOR_REFUSE(expr) do { \
    memset(&out,0xa5,sizeof(out)); \
    BAD(expr); \
    CHECK(out.pixels==NULL && out.width==0u && \
          out.height==0u && out.stride==0u && out.size==0u); \
} while(0)
    limits.max_pixels=3u;
    FLOOR_REFUSE(
        quantapdf_ir_ordered_layer_composite_opaque_white_floor_preview_v0(
            &stroke,&fill,&limits,&out));
    limits.max_pixels=4u;
    limits.max_output_bytes=15u;
    FLOOR_REFUSE(
        quantapdf_ir_ordered_layer_composite_opaque_white_floor_preview_v0(
            &stroke,&fill,&limits,&out));
    limits.max_output_bytes=16u;
    fill.width=3u;fill.stride=12u;fill.size=12u;
    FLOOR_REFUSE(
        quantapdf_ir_ordered_layer_composite_opaque_white_floor_preview_v0(
            &stroke,&fill,&limits,&out));
    fill.width=4u;fill.stride=16u;fill.size=16u;
    memset(&out,0xa5,sizeof(out));
    CHECK(quantapdf_ir_ordered_layer_composite_opaque_white_floor_preview_v0(
        NULL,&fill,&limits,&out)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(out.pixels==NULL && out.size==0u);
    memset(&out,0xa5,sizeof(out));
    CHECK(quantapdf_ir_ordered_layer_composite_opaque_white_floor_preview_v0(
        &stroke,&fill,&limits,&stroke)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(stroke.pixels==stroke_bytes && stroke.width==4u &&
          stroke.size==16u);
    CHECK(stroke_bytes[3]==143u && stroke_bytes[7]==0u &&
          stroke_bytes[11]==255u && stroke_bytes[15]==128u);
#undef FLOOR_REFUSE
    return EXIT_SUCCESS;
}

int main(void)
{
    CHECK(check_order_and_repeat()==EXIT_SUCCESS);
    CHECK(check_fail_closed()==EXIT_SUCCESS);
    CHECK(check_opaque_white_floor_preview()==EXIT_SUCCESS);
    puts("native ordered layers: source-over order, repeat and bounds PASS");
    return EXIT_SUCCESS;
}
