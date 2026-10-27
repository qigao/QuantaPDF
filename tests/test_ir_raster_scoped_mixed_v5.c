#include "render/ir_raster_scoped_mixed_v5.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if(!(e)) { \
    fprintf(stderr,"%s:%d: mixed scoped v5: %s\n",__FILE__,__LINE__,#e); \
    return EXIT_FAILURE; \
} } while(0)
#define OK(e) CHECK((e)==QUANTAPDF_OK)

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[4];
    quantapdf_composer_image_state images[2];
    quantapdf_composer_graphics_state gs;
    quantapdf_composer_clip_state clip;
    quantapdf_composer_path_command mask[5],blue[5],yellow[5];
    unsigned char red[3],green[3],red_alpha;
} fixture;

static const quantapdf_ir_raster_scoped_mixed_v5_limits budgets={
    64u,4u,512u,8000000u,2u,128u
};

static void point(quantapdf_composer_path_command *p,
                  quantapdf_composer_path_command_kind kind,
                  float x,float y)
{
    memset(p,0,sizeof(*p));
    p->kind=kind;p->point1.x=x;p->point1.y=y;
}
static void rect(quantapdf_composer_path_command *p,
                 float x0,float y0,float x1,float y1)
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,x0,y0);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y0);
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y1);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,x0,y1);
    point(&p[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0.0f,0.0f);
}

static void setup(fixture *f)
{
    memset(f,0,sizeof(*f));
    f->page.width_points=8.0f;f->page.height_points=8.0f;
    f->page.background_argb=UINT32_C(0xffffffff);
    f->ir.pages=&f->page;f->ir.page_count=1u;
    f->ir.operations=f->ops;f->ir.operation_count=4u;
    f->ir.resources.images=f->images;f->ir.resources.image_count=2u;
    f->ir.resources.graphics_states=&f->gs;
    f->ir.resources.graphics_state_count=1u;
    f->ir.resources.clips=&f->clip;f->ir.resources.clip_count=1u;
    f->gs.fill_alpha=1.0f;f->gs.stroke_alpha=1.0f;
    f->gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
    f->gs.clip_id=1u;
    f->clip.kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
    f->clip.fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
    f->clip.commands=f->mask;f->clip.command_count=5u;
    f->clip.transform=(quantapdf_affine_transform){1,0,0,1,0,0};
    rect(f->mask,2.0f,2.0f,6.0f,6.0f);
    f->red[0]=255u;f->green[1]=255u;f->red_alpha=128u;
    f->images[0].format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    f->images[0].data=f->red;f->images[0].size=3u;
    f->images[0].width=f->images[0].height=1u;
    f->images[0].components=3;f->images[0].has_alpha=1;
    f->images[0].alpha_data=&f->red_alpha;
    f->images[0].alpha_size=1u;
    f->images[1].format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    f->images[1].data=f->green;f->images[1].size=3u;
    f->images[1].width=f->images[1].height=1u;
    f->images[1].components=3;

    f->ops[0].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    f->ops[0].graphics_state_id=1u;
    f->ops[0].value.image.image_id=1u;
    f->ops[0].value.image.options.struct_size=
        QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
    f->ops[0].value.image.options.graphics_state_id=1u;
    f->ops[0].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    f->ops[0].bounds=(quantapdf_rect){0,0,8,8};

    f->ops[1].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    f->ops[1].graphics_state_id=1u;
    f->ops[1].value.path.options.struct_size=
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    f->ops[1].value.path.options.graphics_state_id=1u;
    f->ops[1].value.path.options.fill=1;
    f->ops[1].value.path.options.fill_rule=
        QUANTAPDF_COMPOSER_FILL_NONZERO;
    f->ops[1].value.path.options.fill_argb=UINT32_C(0xff0000ff);
    f->ops[1].value.path.commands=f->blue;
    f->ops[1].value.path.command_count=5u;
    rect(f->blue,0.0f,0.0f,4.0f,4.0f);

    /* A real source Q restores GS #0. These two draws must not accidentally
     * inherit either the previous Image or PATH Clip. */
    f->ops[2].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    f->ops[2].value.image.image_id=2u;
    f->ops[2].value.image.options.struct_size=
        QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
    f->ops[2].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    f->ops[2].bounds=(quantapdf_rect){4,0,8,8};

    f->ops[3].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    f->ops[3].value.path.options.struct_size=
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    f->ops[3].value.path.options.fill=1;
    f->ops[3].value.path.options.fill_rule=
        QUANTAPDF_COMPOSER_FILL_NONZERO;
    f->ops[3].value.path.options.fill_argb=UINT32_C(0xffffff00);
    f->ops[3].value.path.commands=f->yellow;
    f->ops[3].value.path.command_count=5u;
    rect(f->yellow,0.0f,6.0f,8.0f,8.0f);
}
static uint32_t color(const quantapdf_ir_raster_image *im,
                      size_t x,size_t y)
{
    const unsigned char *p=im->pixels+y*im->stride+x*4u;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
           ((uint32_t)p[2]<<8u)|p[3];
}
static int positive(void)
{
    fixture f;
    quantapdf_ir_raster_image a={0},b={0};
    setup(&f);
    OK(quantapdf_ir_raster_scoped_mixed_v5(&f.ir,0u,&budgets,&a));
    OK(quantapdf_ir_raster_scoped_mixed_v5(&f.ir,0u,&budgets,&b));
    CHECK(a.width==8u && a.height==8u && a.stride==32u &&
          a.size==256u && b.size==a.size &&
          memcmp(a.pixels,b.pixels,a.size)==0);
    CHECK(color(&a,0u,0u)==UINT32_C(0xffffffff));
    CHECK(color(&a,3u,3u)==UINT32_C(0x0000ffff)); /* masked blue */
    CHECK(color(&a,3u,5u)==UINT32_C(0xff7f7fff)); /* red alpha */
    CHECK(color(&a,5u,3u)==UINT32_C(0x00ff00ff)); /* Q unclips Image */
    CHECK(color(&a,7u,3u)==UINT32_C(0x00ff00ff)); /* outside old Clip */
    CHECK(color(&a,1u,3u)==UINT32_C(0xffffffff));
    CHECK(color(&a,1u,7u)==UINT32_C(0xffff00ff)); /* Q unclips Path */
    CHECK(color(&a,7u,7u)==UINT32_C(0xffff00ff));
    f.red[0]=0u;f.green[1]=0u;
    CHECK(color(&a,3u,5u)==UINT32_C(0xff7f7fff));
    quantapdf_ir_raster_image_dispose(&a);
    quantapdf_ir_raster_image_dispose(&b);

    setup(&f);
    f.page.suppress_background=1;
    f.clip.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    OK(quantapdf_ir_raster_scoped_mixed_v5(&f.ir,0u,&budgets,&a));
    CHECK(color(&a,0u,0u)==UINT32_C(0x00000000));
    CHECK(color(&a,3u,5u)==UINT32_C(0xff000080));
    CHECK(color(&a,1u,7u)==UINT32_C(0xffff00ff));
    quantapdf_ir_raster_image_dispose(&a);
    return EXIT_SUCCESS;
}

static int expect_unsupported(fixture *f,
                 const quantapdf_ir_raster_scoped_mixed_v5_limits *limits)
{
    quantapdf_ir_raster_image im;
    memset(&im,0xa5,sizeof(im));
    CHECK(quantapdf_ir_raster_scoped_mixed_v5(&f->ir,0u,limits,&im)
          ==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(im.pixels==NULL && im.width==0u && im.height==0u &&
          im.size==0u && im.stride==0u);
    return EXIT_SUCCESS;
}

static int negatives(void)
{
    fixture f;
    quantapdf_ir_raster_scoped_mixed_v5_limits limit=budgets;
    quantapdf_ir_raster_image recovered={0};
    setup(&f);
    f.ops[3].kind=QUANTAPDF_COMPOSER_OPERATION_TEXT;
    CHECK(expect_unsupported(&f,&limit)==0);
    setup(&f);
    f.ops[2].graphics_state_id=1u;
    CHECK(expect_unsupported(&f,&limit)==0);
    setup(&f);
    f.ops[1].value.path.options.graphics_state_id=0u;
    CHECK(expect_unsupported(&f,&limit)==0);
    setup(&f);
    f.gs.fill_alpha=0.5f;
    CHECK(expect_unsupported(&f,&limit)==0);
    setup(&f);
    f.gs.clip_id=0u;
    CHECK(expect_unsupported(&f,&limit)==0);
    setup(&f);
    f.clip.command_count=0u;
    CHECK(expect_unsupported(&f,&limit)==0);
    setup(&f);
    f.ops[0].value.image.image_id=4u;
    CHECK(expect_unsupported(&f,&limit)==0);
    setup(&f);
    f.images[1].size=2u;
    CHECK(expect_unsupported(&f,&limit)==0);
    setup(&f);
    f.images[0].has_alpha=0;
    CHECK(expect_unsupported(&f,&limit)==0);
    setup(&f);
    f.ops[2].bounds.x0=4.5f;
    CHECK(expect_unsupported(&f,&limit)==0);
    setup(&f);
    f.ops[0].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_COVER;
    CHECK(expect_unsupported(&f,&limit)==0);
    setup(&f);
    f.page.width_points=1.0e30f;
    CHECK(expect_unsupported(&f,&limit)==0);
    setup(&f);
    /* The borrowed backing array holds only two resources. Reducing the
     * declared count is a safe missing-reference negative (no OOB access). */
    f.ir.resources.image_count=1u;
    CHECK(expect_unsupported(&f,&limit)==0);
    setup(&f);
    limit.max_pixels=63u;
    CHECK(expect_unsupported(&f,&limit)==0);
    limit=budgets;
    limit.max_source_pixels=1u;
    CHECK(expect_unsupported(&f,&limit)==0);
    limit=budgets;
    limit.max_drawn_pixels=95u; /* first IMAGE 64 + second IMAGE 32 */
    CHECK(expect_unsupported(&f,&limit)==0);
    limit=budgets;
    limit.max_edges=2u;
    CHECK(expect_unsupported(&f,&limit)==0);
    limit=budgets;
    limit.max_sample_tests=63u;
    CHECK(expect_unsupported(&f,&limit)==0);
    limit=budgets;
    OK(quantapdf_ir_raster_scoped_mixed_v5(&f.ir,0u,&limit,&recovered));
    CHECK(color(&recovered,3u,3u)==UINT32_C(0x0000ffff));
    quantapdf_ir_raster_image_dispose(&recovered);
    return EXIT_SUCCESS;
}

/* No Image or Path paint is required to render a legal blank PDF
 * page. This must use the SAME bounded C11 raster and reject resources
 * that could conceal dropped or silently ignored source operations. */
static int blank_page_cases(void)
{
    quantapdf_ir ir={0};
    quantapdf_composer_page_state page={0};
    quantapdf_ir_raster_image rendered={0},again={0};
    quantapdf_ir_raster_scoped_mixed_v5_limits limit=budgets;
    unsigned char expected[8u*8u*4u];
    memset(expected,255,sizeof(expected));
    page.width_points=8.0f;
    page.height_points=8.0f;
    page.background_argb=UINT32_C(0xffffffff);
    ir.pages=&page;
    ir.page_count=1u;
    OK(quantapdf_ir_raster_scoped_mixed_v5(&ir,0u,&limit,&rendered));
    CHECK(rendered.pixels!=NULL && rendered.width==8u &&
          rendered.height==8u && rendered.stride==32u &&
          rendered.size==sizeof(expected) &&
          memcmp(rendered.pixels,expected,sizeof(expected))==0);
    OK(quantapdf_ir_raster_scoped_mixed_v5(&ir,0u,&limit,&again));
    CHECK(again.size==rendered.size &&
          memcmp(rendered.pixels,again.pixels,rendered.size)==0);
    quantapdf_ir_raster_image_dispose(&again);
    quantapdf_ir_raster_image_dispose(&rendered);

    page.suppress_background=1;
    CHECK(quantapdf_ir_raster_scoped_mixed_v5(&ir,0u,&limit,&rendered)
          ==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(rendered.pixels==NULL && rendered.size==0u);
    page.suppress_background=0;
    page.background_argb=UINT32_C(0xff000000);
    CHECK(quantapdf_ir_raster_scoped_mixed_v5(&ir,0u,&limit,&rendered)
          ==QUANTAPDF_ERROR_UNSUPPORTED);
    page.background_argb=UINT32_C(0xffffffff);

    ir.resources.image_count=1u;
    CHECK(quantapdf_ir_raster_scoped_mixed_v5(&ir,0u,&limit,&rendered)
          ==QUANTAPDF_ERROR_UNSUPPORTED);
    ir.resources.image_count=0u;
    page.width_points=8.5f;
    CHECK(quantapdf_ir_raster_scoped_mixed_v5(&ir,0u,&limit,&rendered)
          ==QUANTAPDF_ERROR_UNSUPPORTED);
    page.width_points=8.0f;
    limit.max_pixels=63u;
    CHECK(quantapdf_ir_raster_scoped_mixed_v5(&ir,0u,&limit,&rendered)
          ==QUANTAPDF_ERROR_UNSUPPORTED);
    limit=budgets;
    OK(quantapdf_ir_raster_scoped_mixed_v5(&ir,0u,&limit,&rendered));
    CHECK(color(&rendered,4u,4u)==UINT32_C(0xffffffff));
    quantapdf_ir_raster_image_dispose(&rendered);
    return EXIT_SUCCESS;
}

int main(void)
{
    if(blank_page_cases()!=EXIT_SUCCESS ||
       positive()!=EXIT_SUCCESS || negatives()!=EXIT_SUCCESS)
        return EXIT_FAILURE;
    puts("Scoped mixed V5: ordered clipped Path/Image and Q-restored "
         "source-over, bounded resources/atomic refusal/detached RGBA PASS");
    return EXIT_SUCCESS;
}
