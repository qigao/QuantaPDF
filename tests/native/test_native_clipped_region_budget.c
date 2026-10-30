/* A 600x600 source page with a small bounded clipped Fill.
 * Unlike the old full-page edge precharge, this C11 regression proves
 * real winding/sample work is paid only in the geometrically possible
 * draw/Clip intersection. The maximum 8m tests NEVER increase.
 * QPDF/PDFium are not used; this is the sole existing C11 V1 raster. */
#include "ir/ir.h"
#include "render/ir_raster_v1.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if(!(e)) { \
    fprintf(stderr,"C11 visible Clip budget:%d: %s\n",__LINE__,#e); \
    return EXIT_FAILURE; } } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

enum { W=600, H=600 };
enum { DRAW_LEFT=10, DRAW_RIGHT=50, DRAW_TOP=10, DRAW_BOTTOM=50 };
/* Draw 4 edges + two Clip triangles of 3 edges each, all samples
 * in the 40x40 visible bounding-pixel intersection are conservatively
 * billed, including each edge no matter where the ray intersects. */
static const size_t exact_upper_bound=40u*40u*16u*10u;

static void point(quantapdf_composer_path_command *c,
    quantapdf_composer_path_command_kind kind,float x,float y)
{
    memset(c,0,sizeof(*c));
    c->kind=kind;
    c->point1.x=x;c->point1.y=y;
}
static void build(quantapdf_composer_path_command fill[5],
                  quantapdf_composer_path_command clip[8])
{
    point(&fill[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,10.0f,10.0f);
    point(&fill[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,50.0f,10.0f);
    point(&fill[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,50.0f,50.0f);
    point(&fill[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,10.0f,50.0f);
    memset(&fill[4],0,sizeof(fill[4]));
    fill[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
    point(&clip[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,8.0f,8.0f);
    point(&clip[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,52.0f,8.0f);
    point(&clip[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,30.0f,50.0f);
    memset(&clip[3],0,sizeof(clip[3]));
    clip[3].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
    point(&clip[4],QUANTAPDF_COMPOSER_PATH_MOVE_TO,22.0f,16.0f);
    point(&clip[5],QUANTAPDF_COMPOSER_PATH_LINE_TO,38.0f,16.0f);
    point(&clip[6],QUANTAPDF_COMPOSER_PATH_LINE_TO,30.0f,32.0f);
    memset(&clip[7],0,sizeof(clip[7]));
    clip[7].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
}
static uint32_t pixel(const quantapdf_ir_raster_image *img,size_t x,size_t y)
{
    const unsigned char *p=img->pixels+4u*(y*(size_t)W+x);
    return ((uint32_t)p[0]<<16u)|((uint32_t)p[1]<<8u)|p[2];
}
static int run(void)
{
    quantapdf_composer_path_command fill[5],clip_commands[8];
    quantapdf_composer_page_state page={0};
    quantapdf_composer_operation op={0};
    quantapdf_composer_clip_state clip={0};
    quantapdf_ir ir={0};
    quantapdf_ir_raster_v1_limits limits={0};
    quantapdf_ir_raster_image image={0},repeat={0};
    size_t positives=0u,refusals=0u;

    build(fill,clip_commands);
    page.width_points=(float)W;
    page.height_points=(float)H;
    page.background_argb=UINT32_C(0xffffffff);
    op.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    op.page_index=0u;
    op.value.path.commands=fill;
    op.value.path.command_count=5u;
    op.value.path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    op.value.path.options.fill=1;
    op.value.path.options.fill_argb=UINT32_C(0xffff0000);
    op.value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    ir.pages=&page;
    ir.page_count=1u;
    ir.operations=&op;
    ir.operation_count=1u;

    clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    clip.commands=clip_commands;
    clip.command_count=8u;
    clip.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    clip.transform.a=clip.transform.d=1.0f;

    limits.max_pixels=500000u;
    limits.max_operations=1u;
    limits.max_edges=64u;
    limits.max_sample_tests=8000000u;
    CHECK(exact_upper_bound==256000u);
    /* The former all-page precharge was
     * (4+6) * 16 * 600 * 600 = 57,600,000; now the actual sampled
     * 40x40 intersection needs at most 256,000 edge checks. */
    OK(quantapdf_ir_raster_render_v1_clipped(
        &ir,0u,&limits,&clip,&image));
    CHECK(image.pixels!=NULL && image.width==(size_t)W &&
          image.height==(size_t)H &&
          image.stride==(size_t)W*4u &&
          image.size==(size_t)W*(size_t)H*4u);
    CHECK(pixel(&image,0u,0u)==UINT32_C(0xffffff));
    CHECK(pixel(&image,599u,599u)==UINT32_C(0xffffff));
    CHECK(pixel(&image,12u,20u)==UINT32_C(0xffffff));
    CHECK(pixel(&image,30u,20u)==UINT32_C(0xffffff)); /* W-star hole */
    CHECK(pixel(&image,30u,35u)==UINT32_C(0xff0000)); /* outer lobe */
    ++positives;

    /* Same Clip winding, same source, identical output bytes and owner. */
    OK(quantapdf_ir_raster_render_v1_clipped(
        &ir,0u,&limits,&clip,&repeat));
    CHECK(repeat.pixels!=NULL && repeat.size==image.size &&
          memcmp(repeat.pixels,image.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    ++positives;

    clip.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    OK(quantapdf_ir_raster_render_v1_clipped(
        &ir,0u,&limits,&clip,&repeat));
    CHECK(pixel(&repeat,30u,20u)==UINT32_C(0xff0000) &&
          pixel(&repeat,30u,35u)==UINT32_C(0xff0000));
    quantapdf_ir_raster_image_dispose(&repeat);
    ++positives;
    clip.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;

    /* This is a charged optimization, NOT a waiver of the work cap.
     * One below the EXACT conservative geometry-edge bill must refuse
     * with a fully cleared, unowned output. */
    limits.max_sample_tests=exact_upper_bound-1u;
    memset(&repeat,0xa5,sizeof(repeat));
    CHECK(quantapdf_ir_raster_render_v1_clipped(
        &ir,0u,&limits,&clip,&repeat)==QUANTAPDF_ERROR_UNSUPPORTED &&
          repeat.pixels==NULL && repeat.size==0u &&
          repeat.width==0u && repeat.height==0u);
    ++refusals;
    limits.max_sample_tests=exact_upper_bound;
    OK(quantapdf_ir_raster_render_v1_clipped(
        &ir,0u,&limits,&clip,&repeat));
    CHECK(repeat.pixels!=NULL && repeat.size==image.size &&
          memcmp(repeat.pixels,image.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&repeat);
    ++positives;

    /* No-Clip input keeps the OLD full-canvas admission, not the
     * clipped-region discount. One 40x40 Fill on 600x600 still
     * costs 4 * 16 * 360000 = 23,040,000. */
    limits.max_sample_tests=8000000u;
    memset(&repeat,0xa5,sizeof(repeat));
    CHECK(quantapdf_ir_raster_render_v1(
        &ir,0u,&limits,&repeat)==QUANTAPDF_ERROR_UNSUPPORTED &&
          repeat.pixels==NULL && repeat.size==0u);
    ++refusals;

    /* The original full-page rectangle and Stroke budget/refusal
     * contracts are independently exercised by existing tests. */
    quantapdf_ir_raster_image_dispose(&image);
    CHECK(positives==4u && refusals==2u);
    printf("Native C11 600x600 clipped winding: 4 exact RGB positives, "
           "2 atomic budget/profile refusals, 8m ceiling unchanged\n");
    return EXIT_SUCCESS;
}
int main(void) { return run(); }
