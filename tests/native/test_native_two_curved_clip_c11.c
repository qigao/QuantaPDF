/* Independent exact production-C11 two-curved Clip acceptance (#528).
 * Private PATH raster contract, NOT public Source W/W* Image admission.
 * No QPDF, PDFium, provider, separate renderer or alpha-mask averaging. */
#include "render/ir_raster_v1.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { \
    fprintf(stderr,"native two-curved Clip C11:%d: %s\n",__LINE__,#e); \
    return EXIT_FAILURE; } } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)
typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation paint;
    quantapdf_composer_path_command painted[5],shapes[2][5];
    quantapdf_composer_clip_state clips[2];
} fixture;
static const quantapdf_ir_raster_v1_limits budget={256u,1u,512u,3000000u};
static void point(quantapdf_composer_path_command *p,
                  quantapdf_composer_path_command_kind kind,float x,float y)
{
    memset(p,0,sizeof(*p));
    p->kind=kind;p->point1=(quantapdf_point){x,y};
}
static void rect(quantapdf_composer_path_command p[5],
                 float x0,float y0,float x1,float y1)
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,x0,y0);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y0);
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y1);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,x0,y1);
    point(&p[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0.0f,0.0f);
}
static void arch(quantapdf_composer_path_command p[5],
                 float x0,float x1)
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,x0,12.0f);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_CUBIC_TO,x0,2.0f);
    p[1].point2=(quantapdf_point){x1,2.0f};
    p[1].point3=(quantapdf_point){x1,12.0f};
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,15.0f);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,x0,15.0f);
    point(&p[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0.0f,0.0f);
}
static void setup(fixture *f)
{
    memset(f,0,sizeof(*f));
    f->page.width_points=f->page.height_points=16.0f;
    f->page.background_argb=UINT32_C(0xffffffff);
    f->ir.pages=&f->page;f->ir.page_count=1u;
    f->ir.operations=&f->paint;f->ir.operation_count=1u;
    f->paint.kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    f->paint.value.path.commands=f->painted;
    f->paint.value.path.command_count=5u;
    f->paint.value.path.options.struct_size=
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    f->paint.value.path.options.fill=1;
    f->paint.value.path.options.fill_argb=UINT32_C(0xffff0000);
    f->paint.value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    f->paint.value.path.options.transform=
        (quantapdf_affine_transform){1,0,0,1,0,0};
    rect(f->painted,0,0,16,16);
    arch(f->shapes[0],2,14);
    arch(f->shapes[1],5,16);
    for(size_t i=0;i<2u;++i) {
        f->clips[i].kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        f->clips[i].commands=f->shapes[i];
        f->clips[i].command_count=5u;
        f->clips[i].fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
        f->clips[i].transform=
            (quantapdf_affine_transform){1,0,0,1,0,0};
    }
}
static uint32_t rgba(const quantapdf_ir_raster_image *im,size_t x,size_t y)
{
    const unsigned char *p=im->pixels+y*im->stride+x*4u;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
           ((uint32_t)p[2]<<8u)|p[3];
}
static int same(const quantapdf_ir_raster_image *a,
                const quantapdf_ir_raster_image *b)
{
    return a->pixels && b->pixels && a->width==b->width &&
        a->height==b->height && a->stride==b->stride &&
        a->size==b->size && memcmp(a->pixels,b->pixels,a->size)==0;
}
static int clear(const quantapdf_ir_raster_image *im)
{
    return !im->pixels && !im->width && !im->height &&
           !im->stride && !im->size;
}
static int genuine_curves(void)
{
    fixture f,snapshot;
    const quantapdf_composer_clip_state *leaves[2];
    quantapdf_ir_raster_image base={0},got={0};
    int partial=0;
    setup(&f);snapshot=f;
    leaves[0]=&f.clips[0];leaves[1]=&f.clips[1];
    OK(quantapdf_ir_raster_render_v1_multi_clipped(
        &f.ir,0u,&budget,leaves,2u,&base));
    CHECK(base.width==16u && base.height==16u &&
          base.stride==64u && base.size==1024u);
    CHECK(rgba(&base,8u,12u)==UINT32_C(0xff0000ff));
    CHECK(rgba(&base,3u,12u)==UINT32_C(0xffffffff));
    CHECK(rgba(&base,15u,12u)==UINT32_C(0xffffffff));
    CHECK(rgba(&base,8u,3u)==UINT32_C(0xffffffff));
    for(size_t y=0;y<16u;++y)
        for(size_t x=0;x<16u;++x) {
            const uint32_t value=rgba(&base,x,y);
            const unsigned green=(unsigned)((value>>16u)&255u);
            CHECK((value&255u)==255u);
            if(green>0u && green<255u)partial=1;
        }
    CHECK(partial); /* True arc, not a rectangular proxy. */
    for(int a=0;a<2;++a)
    for(int b=0;b<2;++b) {
        f.clips[0].fill_rule=a?QUANTAPDF_COMPOSER_FILL_EVEN_ODD:
                               QUANTAPDF_COMPOSER_FILL_NONZERO;
        f.clips[1].fill_rule=b?QUANTAPDF_COMPOSER_FILL_EVEN_ODD:
                               QUANTAPDF_COMPOSER_FILL_NONZERO;
        OK(quantapdf_ir_raster_render_v1_multi_clipped(
            &f.ir,0u,&budget,leaves,2u,&got));
        CHECK(same(&base,&got));
        quantapdf_ir_raster_image_dispose(&got);
        const quantapdf_composer_clip_state *reverse[2]={
            leaves[1],leaves[0]};
        OK(quantapdf_ir_raster_render_v1_multi_clipped(
            &f.ir,0u,&budget,reverse,2u,&got));
        CHECK(same(&base,&got));
        quantapdf_ir_raster_image_dispose(&got);
    }
    f.clips[0].fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    f.clips[1].fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    CHECK(memcmp(&f,&snapshot,sizeof(f))==0); /* Borrowed and read-only. */
    f.shapes[0][1].point2.x=1000.0f;
    CHECK(rgba(&base,8u,12u)==UINT32_C(0xff0000ff)); /* detached */
    quantapdf_ir_raster_image_dispose(&base);
    CHECK(clear(&base));
    return EXIT_SUCCESS;
}
static int collinear_cubic_oracle_and_ctm(void)
{
    fixture f;
    const quantapdf_composer_clip_state *leaves[2];
    quantapdf_ir_raster_image reference={0},got={0};
    setup(&f);leaves[0]=&f.clips[0];leaves[1]=&f.clips[1];
    rect(f.shapes[0],2,4,14,14);rect(f.shapes[1],5,6,15,14);
    OK(quantapdf_ir_raster_render_v1_multi_clipped(
        &f.ir,0u,&budget,leaves,2u,&reference));
    f.shapes[0][1].kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    f.shapes[0][1].point1=(quantapdf_point){6,4};
    f.shapes[0][1].point2=(quantapdf_point){10,4};
    f.shapes[0][1].point3=(quantapdf_point){14,4};
    f.shapes[1][1].kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    f.shapes[1][1].point1=(quantapdf_point){8,6};
    f.shapes[1][1].point2=(quantapdf_point){12,6};
    f.shapes[1][1].point3=(quantapdf_point){15,6};
    OK(quantapdf_ir_raster_render_v1_multi_clipped(
        &f.ir,0u,&budget,leaves,2u,&got));
    CHECK(same(&reference,&got));
    CHECK(rgba(&got,8u,8u)==UINT32_C(0xff0000ff));
    CHECK(rgba(&got,3u,8u)==UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&reference);
    quantapdf_ir_raster_image_dispose(&got);
    /* Immutable clip definition CTM versus precisely translated
     * authored control points: paint CTM cannot retarget Clip #2. */
    f.clips[1].transform.e=0.5f;
    OK(quantapdf_ir_raster_render_v1_multi_clipped(
        &f.ir,0u,&budget,leaves,2u,&reference));
    f.clips[1].transform.e=0.0f;
    for(size_t i=0;i<4u;++i) f.shapes[1][i].point1.x+=0.5f;
    f.shapes[1][1].point2.x+=0.5f;
    f.shapes[1][1].point3.x+=0.5f;
    OK(quantapdf_ir_raster_render_v1_multi_clipped(
        &f.ir,0u,&budget,leaves,2u,&got));
    CHECK(same(&reference,&got));
    quantapdf_ir_raster_image_dispose(&reference);
    quantapdf_ir_raster_image_dispose(&got);
    return EXIT_SUCCESS;
}
static int same_subpixel_not_multiplied_masks(void)
{
    fixture f;
    quantapdf_ir_raster_image a={0},b={0},both={0};
    const quantapdf_composer_clip_state *one_a[1],*one_b[1],*pair[2];
    setup(&f);
    rect(f.shapes[0],1.0f,1.0f,8.5f,15.0f);
    rect(f.shapes[1],8.5f,1.0f,15.0f,15.0f);
    one_a[0]=&f.clips[0];one_b[0]=&f.clips[1];
    pair[0]=one_a[0];pair[1]=one_b[0];
    OK(quantapdf_ir_raster_render_v1_multi_clipped(
        &f.ir,0u,&budget,one_a,1u,&a));
    OK(quantapdf_ir_raster_render_v1_multi_clipped(
        &f.ir,0u,&budget,one_b,1u,&b));
    CHECK(rgba(&a,8u,8u)!=UINT32_C(0xffffffff) &&
          rgba(&a,8u,8u)!=UINT32_C(0xff0000ff));
    CHECK(rgba(&b,8u,8u)!=UINT32_C(0xffffffff) &&
          rgba(&b,8u,8u)!=UINT32_C(0xff0000ff));
    OK(quantapdf_ir_raster_render_v1_multi_clipped(
        &f.ir,0u,&budget,pair,2u,&both));
    for(size_t y=0u;y<16u;++y)
        for(size_t x=0;x<16u;++x)
            CHECK(rgba(&both,x,y)==UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&a);
    quantapdf_ir_raster_image_dispose(&b);
    quantapdf_ir_raster_image_dispose(&both);
    return EXIT_SUCCESS;
}
static int valid_empty_intersections(void)
{
    fixture f;
    quantapdf_ir_raster_image out={0};
    const quantapdf_composer_clip_state *pair[2];
    setup(&f);pair[0]=&f.clips[0];pair[1]=&f.clips[1];
    f.clips[1].command_count=1u; /* Valid MOVE-only empty set. */
    OK(quantapdf_ir_raster_render_v1_multi_clipped(
        &f.ir,0u,&budget,pair,2u,&out));
    for(size_t y=0;y<16u;++y)
        for(size_t x=0;x<16u;++x)
            CHECK(rgba(&out,x,y)==UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&out);
    f.clips[1].command_count=5u;
    f.clips[1].transform.e=20.0f;
    OK(quantapdf_ir_raster_render_v1_multi_clipped(
        &f.ir,0u,&budget,pair,2u,&out));
    for(size_t y=0;y<16u;++y)
        for(size_t x=0;x<16u;++x)
            CHECK(rgba(&out,x,y)==UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&out);
    f.page.suppress_background=1;
    OK(quantapdf_ir_raster_render_v1_multi_clipped(
        &f.ir,0u,&budget,pair,2u,&out));
    for(size_t y=0;y<16u;++y)
        for(size_t x=0;x<16u;++x)
            CHECK(rgba(&out,x,y)==UINT32_C(0x00000000));
    quantapdf_ir_raster_image_dispose(&out);
    return EXIT_SUCCESS;
}
static int rejects_and_clears(fixture *f,
    const quantapdf_composer_clip_state *const *clips,size_t count,
    const quantapdf_ir_raster_v1_limits *lim)
{
    quantapdf_ir_raster_image out={0};
    out.pixels=(unsigned char *)(uintptr_t)1u;
    out.width=out.height=out.size=out.stride=SIZE_MAX;
    CHECK(quantapdf_ir_raster_render_v1_multi_clipped(
        &f->ir,0u,lim,clips,count,&out)!=QUANTAPDF_OK);
    CHECK(clear(&out));
    return EXIT_SUCCESS;
}
static int invalid_and_bounded(void)
{
    fixture f;
    quantapdf_ir_raster_v1_limits lim;
    const quantapdf_composer_clip_state *pair[2];
    setup(&f);pair[0]=&f.clips[0];pair[1]=&f.clips[1];
    lim=budget;lim.max_edges=4u;
    CHECK(rejects_and_clears(&f,pair,2u,&lim)==EXIT_SUCCESS);
    lim=budget;lim.max_sample_tests=4096u;
    CHECK(rejects_and_clears(&f,pair,2u,&lim)==EXIT_SUCCESS);
    lim=budget;lim.max_pixels=10u;
    CHECK(rejects_and_clears(&f,pair,2u,&lim)==EXIT_SUCCESS);
    CHECK(rejects_and_clears(&f,NULL,0u,&budget)==EXIT_SUCCESS);
    pair[1]=NULL;
    CHECK(rejects_and_clears(&f,pair,2u,&budget)==EXIT_SUCCESS);
    pair[1]=&f.clips[1];
    f.shapes[1][1].point2.x=NAN;
    CHECK(rejects_and_clears(&f,pair,2u,&budget)==EXIT_SUCCESS);
    f.shapes[1][1].point2.x=16.0f;
    f.clips[1].kind=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    CHECK(rejects_and_clears(&f,pair,2u,&budget)==EXIT_SUCCESS);
    f.clips[1].kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    f.clips[1].commands=NULL;
    CHECK(rejects_and_clears(&f,pair,2u,&budget)==EXIT_SUCCESS);
    f.clips[1].commands=f.shapes[1];
    /* Unexpected retained resource identity is not allowed in
     * this direct resource-free sample consumer. */
    f.ir.resources.clips=f.clips;
    f.ir.resources.clip_count=2u;
    CHECK(rejects_and_clears(&f,pair,2u,&budget)==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
int main(void)
{
    CHECK(genuine_curves()==EXIT_SUCCESS);
    CHECK(collinear_cubic_oracle_and_ctm()==EXIT_SUCCESS);
    CHECK(same_subpixel_not_multiplied_masks()==EXIT_SUCCESS);
    CHECK(valid_empty_intersections()==EXIT_SUCCESS);
    CHECK(invalid_and_bounded()==EXIT_SUCCESS);
    fprintf(stderr,"[native.two_curved_clip_c11] exact production C11 OK\n");
    return EXIT_SUCCESS;
}
