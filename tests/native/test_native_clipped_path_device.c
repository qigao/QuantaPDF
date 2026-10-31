/* Bounded clipped PATH-only admission through the existing C11 device
 * projector and V5/V1 sampler. Device references are authored directly;
 * no PDF parser, alternate renderer or averaged-mask multiplication. */
#include "render/native_device_ir_v1.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"clipped PATH device:%d: %s\n",__LINE__,#x); \
    return EXIT_FAILURE; } } while(0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[4];
    quantapdf_composer_graphics_state gs;
    quantapdf_composer_clip_state clips[3];
    quantapdf_composer_clip_id members[2];
    quantapdf_composer_path_command shapes[2][5],paths[4][5];
} fixture;

typedef struct geometry {
    unsigned dpi;
    float one,three,five,seven,eight,canvas,fraction_canvas;
    double viewport,fraction_viewport;
    size_t blue_x,red_x,inside_y;
} geometry;

/* Physical coordinates and device coordinates are authored independently
 * of the production projection, including the 96-DPI rational grid. */
static const geometry grids[3]={
    {72u,1,3,5,7,8,8,8,8.0,7.25,4u,5u,6u},
    {96u,4.0f/3.0f,4,20.0f/3.0f,28.0f/3.0f,32.0f/3.0f,
        11,10,32.0/3.0,29.0/3.0,5u,7u,8u},
    {144u,2,6,10,14,16,16,15,16.0,14.5,8u,10u,12u}
};
static const quantapdf_ir_raster_scoped_mixed_v5_limits budget={
    512u,4u,512u,8000000u,1u,1u
};

static void point(quantapdf_composer_path_command *p,
    quantapdf_composer_path_command_kind kind,float x,float y)
{
    memset(p,0,sizeof(*p));p->kind=kind;
    p->point1=(quantapdf_point){x,y};
}

static void rectangle(quantapdf_composer_path_command p[5],
    float x0,float y0,float x1,float y1)
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,x0,y0);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y0);
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y1);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,x0,y1);
    point(&p[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
}

static void arch(quantapdf_composer_path_command p[5],
    float x0,float x1,const geometry *g)
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,x0,g->five);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_CUBIC_TO,x0,g->one);
    p[1].point2=(quantapdf_point){x1,g->one};
    p[1].point3=(quantapdf_point){x1,g->five};
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,g->seven);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,x0,g->seven);
    point(&p[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
}

static void setup(fixture *f,const geometry *g,int two,
    int fractional,size_t count)
{
    static const uint32_t colors[4]={UINT32_C(0xffff0000),
        UINT32_C(0xff0000ff),UINT32_C(0xff00ff00),UINT32_C(0xffffff00)};
    memset(f,0,sizeof(*f));
    f->page.width_points=f->page.height_points=
        fractional?g->fraction_canvas:g->canvas;
    f->page.background_argb=UINT32_C(0xffffffff);
    f->ir.pages=&f->page;f->ir.page_count=1u;
    f->ir.operations=f->ops;f->ir.operation_count=count;
    f->ir.resources.graphics_states=&f->gs;
    f->ir.resources.graphics_state_count=
        f->ir.resources.graphics_state_capacity=1u;
    f->ir.resources.clips=f->clips;
    f->ir.resources.clip_count=f->ir.resources.clip_capacity=two?3u:1u;
    f->gs.fill_alpha=f->gs.stroke_alpha=1;
    f->gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
    f->gs.clip_id=two?3u:1u;
    arch(f->shapes[0],g->one,g->seven,g);
    arch(f->shapes[1],g->three,g->eight,g);
    for(size_t i=0u;i<2u;++i) {
        f->clips[i].kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        f->clips[i].commands=f->shapes[i];f->clips[i].command_count=5u;
        f->clips[i].fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
        f->clips[i].transform=(quantapdf_affine_transform){1,0,0,1,0,0};
    }
    f->clips[2].kind=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    f->clips[2].members=f->members;f->clips[2].member_count=2u;
    f->members[0]=1u;f->members[1]=2u;
    rectangle(f->paths[0],0,0,g->eight,g->eight);
    rectangle(f->paths[1],0,0,g->five,g->eight);
    rectangle(f->paths[2],0,0,g->eight,g->one);
    rectangle(f->paths[3],0,0,g->one,g->eight);
    for(size_t i=0u;i<4u;++i) {
        quantapdf_composer_path_operation *p=&f->ops[i].value.path;
        f->ops[i].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
        f->ops[i].graphics_state_id=i<2u?1u:0u;
        p->commands=f->paths[i];p->command_count=5u;
        p->options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
        p->options.graphics_state_id=f->ops[i].graphics_state_id;
        p->options.fill=1;p->options.fill_argb=colors[i];
    }
}

static void source(fixture *f,int two,int fractional,size_t count)
{
    setup(f,&grids[0],two,fractional,count);
    if(fractional)f->page.width_points=f->page.height_points=7.25f;
}

static int cleared(const quantapdf_ir_raster_image *im)
{
    return im->pixels==NULL && im->size==0u && im->width==0u &&
        im->height==0u && im->stride==0u;
}

static int equal(const quantapdf_ir_raster_image *a,
    const quantapdf_ir_raster_image *b)
{
    return a->pixels!=NULL && b->pixels!=NULL && a->size==b->size &&
        a->width==b->width && a->height==b->height &&
        a->stride==b->stride && memcmp(a->pixels,b->pixels,a->size)==0;
}

static uint32_t rgba(const quantapdf_ir_raster_image *im,size_t x,size_t y)
{
    const unsigned char *p=im->pixels+y*im->stride+x*4u;
    return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|
        ((uint32_t)p[2]<<8u)|p[3];
}

static int projection_and_scope(void)
{
    for(size_t d=0u;d<3u;++d)
    for(int two=0;two<2;++two)
    for(int fractional=0;fractional<2;++fractional)
    for(size_t count=1u;count<=4u;++count) {
        fixture f,reference,saved;
        const geometry *g=&grids[d];
        const double viewport=fractional?g->fraction_viewport:g->viewport;
        quantapdf_ir_raster_image actual={0},expected={0},again={0};
        source(&f,two,fractional,count);
        setup(&reference,g,two,fractional,count);
        OK(quantapdf_ir_raster_scoped_mixed_v5_clipped_paths_device_viewport(
            &reference.ir,0u,&budget,viewport,viewport,&expected));
        for(unsigned rule=0u;rule<(two?4u:2u);++rule) {
            f.clips[0].fill_rule=(rule&1u)?
                QUANTAPDF_COMPOSER_FILL_EVEN_ODD:QUANTAPDF_COMPOSER_FILL_NONZERO;
            f.clips[1].fill_rule=(rule&2u)?
                QUANTAPDF_COMPOSER_FILL_EVEN_ODD:QUANTAPDF_COMPOSER_FILL_NONZERO;
            f.members[0]=(rule&1u)?2u:1u;f.members[1]=(rule&1u)?1u:2u;
            memcpy(&saved,&f,sizeof(saved));
            OK(quantapdf_native_device_ir_render_v1_clipped_paths(
                &f.ir,g->dpi,&budget,&actual));
            CHECK(equal(&actual,&expected));
            CHECK(memcmp(&saved,&f,sizeof(saved))==0);
            CHECK(rgba(&actual,g->red_x,g->inside_y)==UINT32_C(0xff0000ff));
            CHECK(rgba(&actual,g->blue_x,g->inside_y)==
                (count>=2u?UINT32_C(0x0000ffff):UINT32_C(0xff0000ff)));
            CHECK(rgba(&actual,0u,g->inside_y)==
                (count>=4u?UINT32_C(0xffff00ff):UINT32_C(0xffffffff)));
            CHECK(rgba(&actual,g->red_x,0u)==
                (count>=3u?UINT32_C(0x00ff00ff):UINT32_C(0xffffffff)));
            quantapdf_ir_raster_image_dispose(&actual);
        }
        OK(quantapdf_native_device_ir_render_v1_clipped_paths(
            &f.ir,g->dpi,&budget,&actual));
        OK(quantapdf_native_device_ir_render_v1_clipped_paths(
            &f.ir,g->dpi,&budget,&again));
        memset(&f,0,sizeof(f));
        CHECK(equal(&actual,&again) && equal(&actual,&expected));
        quantapdf_ir_raster_image_dispose(&actual);
        quantapdf_ir_raster_image_dispose(&again);
        quantapdf_ir_raster_image_dispose(&expected);
        CHECK(cleared(&actual) && cleared(&again) && cleared(&expected));
    }
    return EXIT_SUCCESS;
}

static int zero_and_same_sample(void)
{
    fixture f;
    quantapdf_ir_raster_image im={0};
    for(int two=0;two<2;++two) {
        source(&f,two,0,3u);
        point(&f.shapes[0][0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,0,0);
        point(&f.shapes[0][1],QUANTAPDF_COMPOSER_PATH_LINE_TO,0,0);
        point(&f.shapes[0][2],QUANTAPDF_COMPOSER_PATH_LINE_TO,0,0);
        point(&f.shapes[0][3],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
        f.clips[0].command_count=4u;
        OK(quantapdf_native_device_ir_render_v1_clipped_paths(
            &f.ir,72u,&budget,&im));
        for(size_t y=0u;y<8u;++y)
            for(size_t x=0u;x<8u;++x)
                CHECK(rgba(&im,x,y)==(y==0u?
                    UINT32_C(0x00ff00ff):UINT32_C(0xffffffff)));
        quantapdf_ir_raster_image_dispose(&im);
    }
    /* Two adjacent half-covered Clips share no sample. Multiplication
     * of their separately averaged masks would wrongly produce ink. */
    source(&f,0,0,1u);rectangle(f.shapes[0],0,0,4.5f,8);
    OK(quantapdf_native_device_ir_render_v1_clipped_paths(
        &f.ir,72u,&budget,&im));
    CHECK(rgba(&im,4u,4u)==UINT32_C(0xff7f7fff));
    quantapdf_ir_raster_image_dispose(&im);
    rectangle(f.shapes[0],4.5f,0,8,8);
    OK(quantapdf_native_device_ir_render_v1_clipped_paths(
        &f.ir,72u,&budget,&im));
    CHECK(rgba(&im,4u,4u)==UINT32_C(0xff7f7fff));
    quantapdf_ir_raster_image_dispose(&im);
    source(&f,1,0,1u);
    rectangle(f.shapes[0],0,0,4.5f,8);
    rectangle(f.shapes[1],4.5f,0,8,8);
    OK(quantapdf_native_device_ir_render_v1_clipped_paths(
        &f.ir,72u,&budget,&im));
    for(size_t i=0u;i<im.size;++i)CHECK(im.pixels[i]==255u);
    quantapdf_ir_raster_image_dispose(&im);
    return EXIT_SUCCESS;
}

static int all_scope_prefixes(void)
{
    for(int two=0;two<2;++two)
    for(size_t prefix=1u;prefix<=4u;++prefix) {
        fixture f;
        quantapdf_ir_raster_image im={0};
        source(&f,two,0,4u);
        for(size_t i=0u;i<4u;++i) {
            const quantapdf_composer_graphics_state_id state=i<prefix?1u:0u;
            f.ops[i].graphics_state_id=state;
            f.ops[i].value.path.options.graphics_state_id=state;
        }
        OK(quantapdf_native_device_ir_render_v1_clipped_paths(
            &f.ir,72u,&budget,&im));
        CHECK(rgba(&im,0u,4u)==(prefix<4u?
            UINT32_C(0xffff00ff):UINT32_C(0xffffffff)));
        CHECK(rgba(&im,4u,0u)==(prefix<3u?
            UINT32_C(0x00ff00ff):UINT32_C(0xffffffff)));
        CHECK(rgba(&im,4u,6u)==UINT32_C(0x0000ffff));
        quantapdf_ir_raster_image_dispose(&im);
    }
    return EXIT_SUCCESS;
}

static int refusal(const quantapdf_ir *ir,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limit)
{
    quantapdf_ir_raster_image im;
    memset(&im,0xa5,sizeof(im));
    CHECK(quantapdf_native_device_ir_render_v1_clipped_paths(
        ir,dpi,limit,&im)!=QUANTAPDF_OK);
    CHECK(cleared(&im));
    return EXIT_SUCCESS;
}

static int malformed_and_legacy(void)
{
    fixture f,saved;
    quantapdf_composer_image_state image={0};
    quantapdf_composer_font_state font={0};
    quantapdf_composer_form_state form={0};
    quantapdf_composer_paint_state paint={0};
    quantapdf_composer_soft_mask_state soft_mask={0};
    quantapdf_composer_link_state link={0};
    quantapdf_composer_outline_state outline={0};
    quantapdf_ir_raster_image im;
    for(int two=0;two<2;++two) {
        source(&f,two,0,4u);
        memset(&im,0xa5,sizeof(im));
        CHECK(quantapdf_native_device_ir_render_v1(&f.ir,72u,&budget,&im)!=QUANTAPDF_OK);
        CHECK(cleared(&im));
        memset(&im,0xa5,sizeof(im));
        CHECK(quantapdf_native_device_ir_render_v1_two_clip(&f.ir,72u,&budget,&im)!=QUANTAPDF_OK);
        CHECK(cleared(&im));
        memset(&im,0xa5,sizeof(im));
        CHECK(quantapdf_ir_raster_scoped_mixed_v5(&f.ir,0u,&budget,&im)!=QUANTAPDF_OK);
        CHECK(cleared(&im));
        memset(&im,0xa5,sizeof(im));
        CHECK(quantapdf_ir_raster_scoped_mixed_v5_device(&f.ir,0u,&budget,&im)!=QUANTAPDF_OK);
        CHECK(cleared(&im));
        memset(&im,0xa5,sizeof(im));
        CHECK(quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
            &f.ir,0u,&budget,8,8,&im)!=QUANTAPDF_OK);
        CHECK(cleared(&im));
        memset(&im,0xa5,sizeof(im));
        CHECK(quantapdf_ir_raster_scoped_mixed_v5_two_clip_device_viewport(
            &f.ir,0u,&budget,8,8,&im)!=QUANTAPDF_OK);
        CHECK(cleared(&im));
    }
    CHECK(refusal(NULL,72u,&budget)==EXIT_SUCCESS);
    source(&f,1,0,4u);
    CHECK(refusal(&f.ir,90u,&budget)==EXIT_SUCCESS);
    CHECK(refusal(&f.ir,72u,NULL)==EXIT_SUCCESS);
    CHECK(quantapdf_native_device_ir_render_v1_clipped_paths(
        &f.ir,72u,&budget,NULL)==QUANTAPDF_ERROR_ARGUMENT);
    for(unsigned bad=0u;bad<46u;++bad) {
        source(&f,1,0,4u);
        switch(bad) {
        case 0u:f.ir.operation_count=0u;break;
        case 1u:f.ir.operation_count=5u;break;
        case 2u:f.ir.operations=NULL;break;
        case 3u:f.ir.resources.images=&image;break;
        case 4u:f.ir.resources.image_count=1u;break;
        case 5u:f.ir.resources.image_capacity=1u;break;
        case 6u:f.ir.resources.graphics_states=NULL;break;
        case 7u:f.ir.resources.graphics_state_capacity=2u;break;
        case 8u:f.ir.resources.clips=NULL;break;
        case 9u:f.ir.resources.clip_count=2u;break;
        case 10u:f.ir.resources.clip_capacity=4u;break;
        case 11u:f.clips[2].members=NULL;break;
        case 12u:f.clips[2].member_count=3u;break;
        case 13u:f.members[1]=1u;break;
        case 14u:f.members[1]=3u;break;
        case 15u:f.clips[1].commands=NULL;break;
        case 16u:f.clips[1].command_count=3u;break;
        case 17u:f.clips[0].command_count=10u;break;
        case 18u:f.clips[0].transform.e=0.25f;break;
        case 19u:f.shapes[1][1].point2.x=NAN;break;
        case 20u:f.shapes[0][1].point3.y=1000000.0f;break;
        case 21u:f.gs.clip_id=1u;break;
        case 22u:f.gs.fill_alpha=0.5f;break;
        case 23u:f.ops[0].graphics_state_id=0u;f.ops[0].value.path.options.graphics_state_id=0u;break;
        case 24u:f.ops[3].graphics_state_id=1u;f.ops[3].value.path.options.graphics_state_id=1u;break;
        case 25u:f.ops[1].value.path.options.graphics_state_id=0u;break;
        case 26u:f.ops[3].kind=QUANTAPDF_COMPOSER_OPERATION_TEXT;break;
        case 27u:f.ops[3].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;break;
        case 28u:f.ops[0].value.path.command_count=3u;break;
        case 29u:f.ops[0].value.path.command_count=10u;break;
        case 30u:f.ops[1].value.path.options.transform.a=1.0f;break;
        case 31u:f.ops[1].value.path.options.stroke=1;break;
        case 32u:f.ir.resources.fonts=&font;break;
        case 33u:f.ir.resources.font_capacity=1u;break;
        case 34u:f.ir.resources.forms=&form;break;
        case 35u:f.ir.resources.form_capacity=1u;break;
        case 36u:f.ir.resources.paints=&paint;break;
        case 37u:f.ir.resources.paint_capacity=1u;break;
        case 38u:f.ir.resources.soft_masks=&soft_mask;break;
        case 39u:f.ir.resources.soft_mask_capacity=1u;break;
        case 40u:f.ir.links=&link;break;
        case 41u:f.ir.outlines=&outline;break;
        case 42u:f.ir.resources.graphics_state_count=0u;break;
        case 43u:f.clips[0].members=f.members;break;
        case 44u:f.ops[0].value.path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE;break;
        case 45u:f.page.width_points=7.1f;break;
        default:CHECK(0);break;
        }
        memcpy(&saved,&f,sizeof(saved));
        CHECK(refusal(&f.ir,144u,&budget)==EXIT_SUCCESS);
        CHECK(memcmp(&saved,&f,sizeof(saved))==0);
    }
    source(&f,1,0,4u);
    for(unsigned quota=0u;quota<6u;++quota) {
        quantapdf_ir_raster_scoped_mixed_v5_limits limit=budget;
        switch(quota) {
        case 0u:limit.max_pixels=255u;break;
        case 1u:limit.max_operations=3u;break;
        case 2u:limit.max_edges=3u;break;
        case 3u:limit.max_sample_tests=1u;break;
        case 4u:limit.max_source_pixels=0u;break;
        case 5u:limit.max_drawn_pixels=0u;break;
        default:CHECK(0);break;
        }
        CHECK(refusal(&f.ir,144u,&limit)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

int main(void)
{
    CHECK(projection_and_scope()==EXIT_SUCCESS);
    CHECK(zero_and_same_sample()==EXIT_SUCCESS);
    CHECK(all_scope_prefixes()==EXIT_SUCCESS);
    CHECK(malformed_and_legacy()==EXIT_SUCCESS);
    fprintf(stderr,"[native.clipped_path_device] completed\n");
    return EXIT_SUCCESS;
}
