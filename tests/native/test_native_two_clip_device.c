/* Private Source-space two-Clip DPI projection. Expected device geometry
 * is authored below, independently of the projector. The one existing
 * C11 V5 raster supplies both sides; this is not an original-PDF test. */
#include "render/native_device_ir_v1.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"two Clip device:%d: %s\n",__LINE__,#x); \
    return EXIT_FAILURE; } } while (0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[3];
    quantapdf_composer_image_state image;
    quantapdf_composer_graphics_state gs;
    quantapdf_composer_clip_state clips[3];
    quantapdf_composer_clip_id members[2];
    quantapdf_composer_path_command shapes[2][5],blue[5],green[5];
    unsigned char pixels[12];
} fixture;

/* Explicit physical-to-device coordinates, not a second implementation
 * of qp_device_commands or an invocation of the tested bridge. */
typedef struct authored_geometry {
    unsigned dpi;
    float half,one,three,five,seven,seven_half,eight;
    float full_canvas,fraction_canvas;
    double full_viewport,fraction_viewport;
    size_t blue_x,red_x,inside_y;
    uint32_t full_terminal_green,fraction_terminal_green;
} authored_geometry;

static const authored_geometry geometry[3]={
    {72u,0.5f,1.0f,3.0f,5.0f,7.0f,7.5f,8.0f,
        8.0f,8.0f,8.0,7.25,4u,5u,6u,
        UINT32_C(0x00ff00ff),UINT32_C(0xbfffbfff)},
    {96u,2.0f/3.0f,4.0f/3.0f,4.0f,20.0f/3.0f,
        28.0f/3.0f,10.0f,32.0f/3.0f,
        11.0f,10.0f,32.0/3.0,29.0/3.0,5u,7u,8u,
        UINT32_C(0x40ff40ff),UINT32_C(0x40ff40ff)},
    {144u,1.0f,2.0f,6.0f,10.0f,14.0f,15.0f,16.0f,
        16.0f,15.0f,16.0,14.5,8u,10u,12u,
        UINT32_C(0x00ff00ff),UINT32_C(0x7fff7fff)}
};

static const quantapdf_ir_raster_scoped_mixed_v5_limits budget={
    512u,4u,512u,8000000u,4u,512u
};

static void point(quantapdf_composer_path_command *p,
                  quantapdf_composer_path_command_kind kind,float x,float y)
{
    memset(p,0,sizeof(*p));
    p->kind=kind;p->point1=(quantapdf_point){x,y};
}

static void rectangle(quantapdf_composer_path_command p[5],
                      float x1,float y1)
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,0,0);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,0);
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y1);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,0,y1);
    point(&p[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
}

static void arch(quantapdf_composer_path_command p[5],
                 float x0,float x1,const authored_geometry *g)
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,x0,g->five);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_CUBIC_TO,x0,g->one);
    p[1].point2=(quantapdf_point){x1,g->one};
    p[1].point3=(quantapdf_point){x1,g->five};
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,g->seven);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,x0,g->seven);
    point(&p[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
}

static void setup_authored(fixture *f,const authored_geometry *g,int fractional)
{
    memset(f,0,sizeof(*f));
    f->page.width_points=f->page.height_points=
        fractional?g->fraction_canvas:g->full_canvas;
    f->page.background_argb=UINT32_C(0xffffffff);
    f->ir.pages=&f->page;f->ir.page_count=1u;
    f->ir.operations=f->ops;f->ir.operation_count=3u;
    f->ir.resources.images=&f->image;
    f->ir.resources.image_count=f->ir.resources.image_capacity=1u;
    f->ir.resources.graphics_states=&f->gs;
    f->ir.resources.graphics_state_count=
        f->ir.resources.graphics_state_capacity=1u;
    f->ir.resources.clips=f->clips;
    f->ir.resources.clip_count=f->ir.resources.clip_capacity=3u;
    f->pixels[0]=255u;
    f->image.data=f->pixels;f->image.size=3u;
    f->image.width=f->image.height=1u;f->image.components=3;
    f->image.format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
    f->gs.fill_alpha=f->gs.stroke_alpha=1.0f;
    f->gs.blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
    f->gs.clip_id=3u;
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
    f->ops[0].kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
    f->ops[0].graphics_state_id=1u;
    f->ops[0].value.image.image_id=1u;
    f->ops[0].value.image.options.struct_size=
        QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
    f->ops[0].value.image.options.graphics_state_id=1u;
    f->ops[0].value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    f->ops[0].bounds=fractional?
        (quantapdf_rect){g->half,g->half,g->seven_half,g->seven_half}:
        (quantapdf_rect){0,0,g->eight,g->eight};
    f->ops[1].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    f->ops[1].graphics_state_id=1u;
    f->ops[1].value.path.commands=f->blue;
    f->ops[1].value.path.command_count=5u;
    f->ops[1].value.path.options.struct_size=
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    f->ops[1].value.path.options.graphics_state_id=1u;
    f->ops[1].value.path.options.fill=1;
    f->ops[1].value.path.options.fill_argb=UINT32_C(0xff0000ff);
    rectangle(f->blue,g->five,g->eight);
    f->ops[2].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    f->ops[2].value.path.commands=f->green;
    f->ops[2].value.path.command_count=5u;
    f->ops[2].value.path.options.struct_size=
        QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    f->ops[2].value.path.options.fill=1;
    f->ops[2].value.path.options.fill_argb=UINT32_C(0xff00ff00);
    rectangle(f->green,g->eight,g->one); /* Q-restored GS=0. */
}

static void setup_source(fixture *f,int fractional)
{
    setup_authored(f,&geometry[0],fractional);
    if(fractional)f->page.width_points=f->page.height_points=7.25f;
}

static int reset(const quantapdf_ir_raster_image *im)
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

static int projection_and_owned_output(void)
{
    for(size_t d=0u;d<3u;++d)
    for(int fractional=0;fractional<2;++fractional) {
        const authored_geometry *g=&geometry[d];
        const double viewport=fractional?g->fraction_viewport:g->full_viewport;
        fixture source,reference,saved;
        quantapdf_ir_raster_image expected={0},actual={0},again={0};
        setup_source(&source,fractional);
        setup_authored(&reference,g,fractional);
        OK(quantapdf_ir_raster_scoped_mixed_v5_two_clip_device_viewport(
            &reference.ir,0u,&budget,viewport,viewport,&expected));
        for(unsigned rule=0u;rule<4u;++rule)
        for(unsigned order=0u;order<2u;++order) {
            source.clips[0].fill_rule=(rule&1u)?
                QUANTAPDF_COMPOSER_FILL_EVEN_ODD:QUANTAPDF_COMPOSER_FILL_NONZERO;
            source.clips[1].fill_rule=(rule&2u)?
                QUANTAPDF_COMPOSER_FILL_EVEN_ODD:QUANTAPDF_COMPOSER_FILL_NONZERO;
            source.members[0]=order?2u:1u;source.members[1]=order?1u:2u;
            memcpy(&saved,&source,sizeof(saved));
            OK(quantapdf_native_device_ir_render_v1_two_clip(
                &source.ir,g->dpi,&budget,&actual));
            CHECK(equal(&actual,&expected));
            CHECK(memcmp(&saved,&source,sizeof(saved))==0);
            CHECK(rgba(&actual,g->blue_x,g->inside_y)==UINT32_C(0x0000ffff));
            CHECK(rgba(&actual,g->red_x,g->inside_y)==UINT32_C(0xff0000ff));
            CHECK(rgba(&actual,0u,g->inside_y)==UINT32_C(0xffffffff));
            CHECK(rgba(&actual,0u,0u)==UINT32_C(0x00ff00ff));
            CHECK(rgba(&actual,actual.width-1u,0u)==
                (fractional?g->fraction_terminal_green:g->full_terminal_green));
            quantapdf_ir_raster_image_dispose(&actual);
        }
        OK(quantapdf_native_device_ir_render_v1_two_clip(
            &source.ir,g->dpi,&budget,&actual));
        OK(quantapdf_native_device_ir_render_v1_two_clip(
            &source.ir,g->dpi,&budget,&again));
        CHECK(equal(&actual,&again));
        /* A completed RGBA result owns its bytes independently of ALL
         * borrowed source records, commands, membership and RAW data. */
        memset(&source,0,sizeof(source));
        CHECK(equal(&actual,&expected) && equal(&again,&expected));
        quantapdf_ir_raster_image_dispose(&actual);
        quantapdf_ir_raster_image_dispose(&again);
        quantapdf_ir_raster_image_dispose(&expected);
        CHECK(reset(&actual) && reset(&again) && reset(&expected));
    }
    return EXIT_SUCCESS;
}

static void patterned_image(fixture *f)
{
    static const unsigned char pixels[12]={
        255u,0u,0u, 0u,255u,0u, 0u,0u,255u, 255u,255u,0u
    };
    memcpy(f->pixels,pixels,sizeof(pixels));
    f->image.width=f->image.height=2u;f->image.size=sizeof(pixels);
    f->ir.operation_count=1u;
}

static int image_sampling_and_zero_clip(void)
{
    for(size_t d=0u;d<3u;++d)
    for(int fractional=0;fractional<2;++fractional) {
        const authored_geometry *g=&geometry[d];
        const double viewport=fractional?g->fraction_viewport:g->full_viewport;
        fixture source,reference,saved;
        quantapdf_ir_raster_image actual={0},expected={0};
        setup_source(&source,fractional);patterned_image(&source);
        setup_authored(&reference,g,fractional);patterned_image(&reference);
        memcpy(&saved,&source,sizeof(saved));
        OK(quantapdf_native_device_ir_render_v1_two_clip(
            &source.ir,g->dpi,&budget,&actual));
        OK(quantapdf_ir_raster_scoped_mixed_v5_two_clip_device_viewport(
            &reference.ir,0u,&budget,viewport,viewport,&expected));
        CHECK(equal(&actual,&expected));
        CHECK(memcmp(&saved,&source,sizeof(saved))==0);
        CHECK(rgba(&actual,g->red_x,g->inside_y)==UINT32_C(0xffff00ff));
        CHECK(rgba(&actual,0u,0u)==UINT32_C(0xffffffff));
        quantapdf_ir_raster_image_dispose(&actual);
        quantapdf_ir_raster_image_dispose(&expected);
        source.clips[1].command_count=1u; /* valid zero-edge second Clip */
        OK(quantapdf_native_device_ir_render_v1_two_clip(
            &source.ir,g->dpi,&budget,&actual));
        for(size_t y=0u;y<actual.height;++y)
            for(size_t x=0u;x<actual.width;++x)
                CHECK(rgba(&actual,x,y)==UINT32_C(0xffffffff));
        quantapdf_ir_raster_image_dispose(&actual);
    }
    return EXIT_SUCCESS;
}

static int refused(const quantapdf_ir *ir,unsigned dpi,
                    const quantapdf_ir_raster_scoped_mixed_v5_limits *limit,
                    int legacy)
{
    quantapdf_ir_raster_image out={0};
    quantapdf_status status;
    out.pixels=(unsigned char *)(uintptr_t)1u;
    out.size=out.width=out.height=out.stride=SIZE_MAX;
    status=legacy?quantapdf_native_device_ir_render_v1(ir,dpi,limit,&out):
        quantapdf_native_device_ir_render_v1_two_clip(ir,dpi,limit,&out);
    CHECK(status!=QUANTAPDF_OK && reset(&out));
    return EXIT_SUCCESS;
}

static int malformed_projection_and_budget(void)
{
    fixture f;
    quantapdf_ir_raster_scoped_mixed_v5_limits limit;
    quantapdf_composer_font_state hidden_font={0};
    quantapdf_composer_link_state hidden_link={0};
    setup_source(&f,0);
    for(size_t d=0u;d<3u;++d)
        CHECK(refused(&f.ir,geometry[d].dpi,&budget,1)==EXIT_SUCCESS);
    CHECK(refused(NULL,96u,&budget,0)==EXIT_SUCCESS);
    CHECK(refused(&f.ir,96u,NULL,0)==EXIT_SUCCESS);
    CHECK(quantapdf_native_device_ir_render_v1_two_clip(
        &f.ir,96u,&budget,NULL)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(refused(&f.ir,0u,&budget,0)==EXIT_SUCCESS);
    CHECK(refused(&f.ir,73u,&budget,0)==EXIT_SUCCESS);
    CHECK(refused(&f.ir,192u,&budget,0)==EXIT_SUCCESS);

    /* Every case starts from valid borrowed storage. Invalid counts and
     * capacities must be rejected before indexing/copying that storage. */
    for(unsigned mutation=0u;mutation<34u;++mutation) {
        fixture saved;
        setup_source(&f,0);
        switch(mutation) {
        case 0u:f.ir.pages=NULL;break;
        case 1u:f.ir.page_count=2u;break;
        case 2u:f.ir.operations=NULL;break;
        case 3u:f.ir.operation_count=5u;break;
        case 4u:f.ir.resources.clips=NULL;break;
        case 5u:f.ir.resources.clip_count=2u;break;
        case 6u:f.ir.resources.clip_capacity=2u;break;
        case 7u:f.ir.resources.graphics_states=NULL;break;
        case 8u:f.ir.resources.graphics_state_count=0u;break;
        case 9u:f.ir.resources.graphics_state_capacity=2u;break;
        case 10u:f.clips[2].members=NULL;break;
        case 11u:f.clips[2].member_count=3u;break;
        case 12u:f.members[0]=0u;break;
        case 13u:f.members[1]=1u;break;
        case 14u:f.members[1]=3u;break;
        case 15u:f.clips[1].kind=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;break;
        case 16u:f.clips[1].commands=NULL;break;
        case 17u:f.clips[0].command_count=0u;break;
        case 18u:f.clips[1].command_count=10u;break;
        case 19u:f.clips[0].transform.e=0.25f;break;
        case 20u:f.clips[1].transform.a=NAN;break;
        case 21u:f.shapes[0][1].point2.x=NAN;break;
        case 22u:f.shapes[1][1].point3.y=INFINITY;break;
        case 23u:f.shapes[1][1].point1.x=1000000.0f;break;
        case 24u:f.ops[1].value.path.options.transform.a=1.0f;break;
        case 25u:f.ops[2].kind=QUANTAPDF_COMPOSER_OPERATION_TEXT;break;
        case 26u:f.gs.clip_id=1u;break;
        case 27u:f.ops[0].value.image.options.graphics_state_id=0u;break;
        case 28u:f.ir.resources.image_capacity=0u;break;
        case 29u:f.ir.resources.image_capacity=2u;break;
        case 30u:f.ir.resources.fonts=&hidden_font;break;
        case 31u:f.ir.resources.form_capacity=1u;break;
        case 32u:f.ir.links=&hidden_link;break;
        case 33u:f.page.width_points=7.1f;break;
        default:CHECK(0);break;
        }
        memcpy(&saved,&f,sizeof(saved));
        CHECK(refused(&f.ir,144u,&budget,0)==EXIT_SUCCESS);
        CHECK(memcmp(&saved,&f,sizeof(saved))==0);
    }
    setup_source(&f,0);
    for(unsigned quota=0u;quota<6u;++quota) {
        limit=budget;
        switch(quota) {
        case 0u:limit.max_pixels=255u;break;
        case 1u:limit.max_operations=2u;break;
        case 2u:limit.max_edges=3u;break;
        case 3u:limit.max_sample_tests=100u;break;
        case 4u:limit.max_source_pixels=0u;break;
        case 5u:limit.max_drawn_pixels=255u;break;
        default:CHECK(0);break;
        }
        CHECK(refused(&f.ir,144u,&limit,0)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

int main(void)
{
    CHECK(projection_and_owned_output()==EXIT_SUCCESS);
    CHECK(image_sampling_and_zero_clip()==EXIT_SUCCESS);
    CHECK(malformed_projection_and_budget()==EXIT_SUCCESS);
    fprintf(stderr,"[native.two_clip_device] completed\n");
    return EXIT_SUCCESS;
}
