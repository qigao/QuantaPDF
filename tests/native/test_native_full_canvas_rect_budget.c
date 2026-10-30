/* C11 full-canvas rectangle work preflight and exact coverage.
 * The SAME v1 raster may charge one proven coverage decision per 4x4
 * sample instead of four redundant winding-edge intersections. The
 * old numeric max_sample_tests, Clip and arbitrary-PATH gates are NOT
 * increased. All refusal outputs must be reset atomically. */
#include "ir/ir.h"
#include "render/ir_raster_v1.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if(!(e)) { \
    fprintf(stderr,"C11 full-canvas work:%d: %s\n",__LINE__,#e); \
    return EXIT_FAILURE; } } while(0)

enum {WIDTH=612,HEIGHT=792};
static const size_t physical_samples=(size_t)WIDTH*HEIGHT*16u;

static void rect(quantapdf_composer_path_command cmd[5])
{
    memset(cmd,0,5u*sizeof(*cmd));
    cmd[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    cmd[0].point1.x=0.0f;cmd[0].point1.y=0.0f;
    cmd[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    cmd[1].point1.x=(float)WIDTH;cmd[1].point1.y=0.0f;
    cmd[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    cmd[2].point1.x=(float)WIDTH;cmd[2].point1.y=(float)HEIGHT;
    cmd[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    cmd[3].point1.x=0.0f;cmd[3].point1.y=(float)HEIGHT;
    cmd[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
}

static int qualify(void)
{
    quantapdf_composer_path_command commands[5];
    quantapdf_composer_page_state page={0};
    quantapdf_composer_operation op={0};
    quantapdf_ir ir={0};
    quantapdf_ir_raster_v1_limits limits={0};
    quantapdf_ir_raster_image output={0},repeat={0};
    size_t positives=0u,refusals=0u;

    rect(commands);
    page.width_points=(float)WIDTH;
    page.height_points=(float)HEIGHT;
    page.background_argb=UINT32_C(0xffffffff);
    op.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    op.page_index=0u;
    op.value.path.commands=commands;
    op.value.path.command_count=5u;
    op.value.path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    op.value.path.options.fill=1;
    op.value.path.options.fill_argb=UINT32_C(0xff000000);
    op.value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    ir.pages=&page;ir.page_count=1u;
    ir.operations=&op;ir.operation_count=1u;
    limits.max_pixels=1000000u;
    limits.max_operations=1u;
    limits.max_edges=64u;
    limits.max_sample_tests=8000000u;

    /* The previously unsupported 31m edge comparisons reduce to one
     * provably equivalent 16-sample coverage per full-canvas pixel:
     * physical_samples=7,755,264 < 8m without increasing the cap. */
    CHECK(physical_samples==7755264u);
    CHECK(quantapdf_ir_raster_render_v1(
        &ir,0u,&limits,&output)==QUANTAPDF_OK);
    CHECK(output.pixels!=NULL && output.width==(size_t)WIDTH &&
          output.height==(size_t)HEIGHT &&
          output.stride==WIDTH*4u &&
          output.size==(size_t)WIDTH*HEIGHT*4u);
    for(size_t i=0u;i<output.size;i+=4u)
        CHECK(output.pixels[i]==0u &&
              output.pixels[i+1u]==0u &&
              output.pixels[i+2u]==0u &&
              output.pixels[i+3u]==255u);
    ++positives;

    CHECK(quantapdf_ir_raster_render_v1(
        &ir,0u,&limits,&repeat)==QUANTAPDF_OK);
    CHECK(repeat.pixels!=NULL && repeat.size==output.size &&
          memcmp(repeat.pixels,output.pixels,output.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    ++positives;

    /* Same closed polygon, reversed winding and even-odd rule must
     * retain exact full coverage, not an inferred bounding rectangle. */
    {
        const quantapdf_point p1=commands[1].point1;
        const quantapdf_point p3=commands[3].point1;
        commands[1].point1=p3;
        commands[3].point1=p1;
        op.value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
        CHECK(quantapdf_ir_raster_render_v1(
            &ir,0u,&limits,&repeat)==QUANTAPDF_OK &&
              repeat.size==output.size &&
              memcmp(repeat.pixels,output.pixels,output.size)==0);
        quantapdf_ir_raster_image_dispose(&repeat);
        ++positives;
    }
    rect(commands);
    op.value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    quantapdf_ir_raster_image_dispose(&output);

    /* Even a full, proven rectangle must fit the SAME pre-existing
     * sample cap. Charge cannot be zero or bypass a one-short limit. */
    limits.max_sample_tests=physical_samples-1u;
    memset(&output,0xa5,sizeof(output));
    CHECK(quantapdf_ir_raster_render_v1(
        &ir,0u,&limits,&output)==QUANTAPDF_ERROR_UNSUPPORTED &&
          output.pixels==NULL && output.size==0u &&
          output.width==0u && output.height==0u);
    ++refusals;
    limits.max_sample_tests=8000000u;

    /* A 611-wide rectangle is NOT a full canvas; it must keep its
     * original four-edge work charge and fail under 8m. */
    commands[1].point1.x=(float)WIDTH-1.0f;
    commands[2].point1.x=(float)WIDTH-1.0f;
    memset(&output,0xa5,sizeof(output));
    CHECK(quantapdf_ir_raster_render_v1(
        &ir,0u,&limits,&output)==QUANTAPDF_ERROR_UNSUPPORTED &&
          output.pixels==NULL && output.size==0u);
    ++refusals;
    rect(commands);

    /* Four diagonal/self-crossing edges never receive the rectangle
     * optimization even if their bounds match the whole canvas. */
    commands[1].point1.x=(float)WIDTH;
    commands[1].point1.y=(float)HEIGHT;
    commands[2].point1.x=0.0f;
    commands[2].point1.y=(float)HEIGHT;
    commands[3].point1.x=(float)WIDTH;
    commands[3].point1.y=0.0f;
    memset(&output,0xa5,sizeof(output));
    CHECK(quantapdf_ir_raster_render_v1(
        &ir,0u,&limits,&output)==QUANTAPDF_ERROR_UNSUPPORTED &&
          output.pixels==NULL && output.size==0u);
    ++refusals;

    /* Even a valid full rectangle on a million-pixel canvas cannot
     * silently escape an 8m sample-work cap. */
    rect(commands);
    page.width_points=1000.0f;
    page.height_points=1000.0f;
    commands[1].point1.x=1000.0f;
    commands[2].point1.x=1000.0f;
    commands[2].point1.y=1000.0f;
    commands[3].point1.y=1000.0f;
    memset(&output,0xa5,sizeof(output));
    CHECK(quantapdf_ir_raster_render_v1(
        &ir,0u,&limits,&output)==QUANTAPDF_ERROR_UNSUPPORTED &&
          output.pixels==NULL && output.size==0u);
    ++refusals;
    CHECK(positives==3u && refusals==4u);
    printf("Native C11 full-canvas: 3 exact positives, 4 budget/"
           "geometry refusals, unchanged 8m sample cap\n");
    return EXIT_SUCCESS;
}
int main(void)
{
    return qualify();
}
