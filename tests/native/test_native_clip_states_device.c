/* Exact owned first-Clip/intersection GS states through the existing
 * native projector and V5 sampler. Independent device coordinates and
 * analytically intersected PATH references qualify geometry and order. */
#include "render/native_device_ir_v1.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"clip states device:%d: %s\n",__LINE__,#x); \
    return EXIT_FAILURE; } } while(0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)

typedef struct fixture {
    quantapdf_ir ir;
    quantapdf_composer_page_state page;
    quantapdf_composer_operation ops[4];
    quantapdf_composer_image_state images[2];
    quantapdf_composer_graphics_state gs[2];
    quantapdf_composer_clip_state clips[3];
    quantapdf_composer_clip_id members[2];
    quantapdf_composer_path_command shapes[2][9],paths[4][9];
    unsigned char pixels[2][12];
} fixture;

typedef struct geometry {
    unsigned dpi;
    float one,two,three,four,five,seven,eight,canvas,fraction_canvas;
    double viewport,fraction_viewport;
    size_t first_x,both_x,right_x,inside_y;
} geometry;

/* Authored device coordinates; the tested native projector is not used
 * to generate the reference points, dimensions or physical viewport. */
static const geometry grids[3]={
    {72u,1,2,3,4,5,7,8,8,8,8.0,7.25,2u,4u,6u,6u},
    {96u,4.0f/3.0f,8.0f/3.0f,4,16.0f/3.0f,20.0f/3.0f,
        28.0f/3.0f,32.0f/3.0f,11,10,32.0/3.0,29.0/3.0,3u,5u,8u,8u},
    {144u,2,4,6,8,10,14,16,16,15,16.0,14.5,4u,8u,12u,12u}
};
typedef struct profile { const char *kinds,*states; } profile;
static const profile profiles[]={
    {"PPPP","1210"}, {"II","12"}, {"II","21"},
    {"IP","12"}, {"PI","21"}, {"IPP","121"},
    {"IPI","221"}, {"IIPP","1210"}, {"PPII","1221"}, {"PIPI","1210"}
};
static const uint32_t colors[4]={
    UINT32_C(0xffff0000),UINT32_C(0xff0000ff),
    UINT32_C(0xff00ff00),UINT32_C(0xffffff00)
};
static const quantapdf_ir_raster_scoped_mixed_v5_limits budget={
    512u,4u,512u,8000000u,8u,1024u
};

static void point(quantapdf_composer_path_command *p,
    quantapdf_composer_path_command_kind kind,float x,float y)
{
    memset(p,0,sizeof(*p));p->kind=kind;
    p->point1=(quantapdf_point){x,y};
}

static void rectangle(quantapdf_composer_path_command *p,
    float x0,float y0,float x1,float y1)
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,x0,y0);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y0);
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y1);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,x0,y1);
    point(&p[4],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
}

static void zero_contour(quantapdf_composer_path_command *p)
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,0,0);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,0,0);
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,0,0);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
}

static void arch(quantapdf_composer_path_command *p,
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

static void state(quantapdf_composer_operation *op,unsigned id)
{
    op->graphics_state_id=(quantapdf_composer_graphics_state_id)id;
    if(op->kind==QUANTAPDF_COMPOSER_OPERATION_PATH)
        op->value.path.options.graphics_state_id=op->graphics_state_id;
    else op->value.image.options.graphics_state_id=op->graphics_state_id;
}

static void path_op(fixture *f,size_t index,unsigned id,size_t count)
{
    quantapdf_composer_operation *op=&f->ops[index];
    memset(op,0,sizeof(*op));op->kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    op->value.path.commands=f->paths[index];op->value.path.command_count=count;
    op->value.path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    op->value.path.options.fill=1;
    op->value.path.options.fill_argb=colors[index];
    state(op,id);
}

static void setup(fixture *f,const geometry *g,int fractional,const profile *p)
{
    size_t images=0u;
    const quantapdf_rect bounds[4]={
        {0,0,g->eight,g->eight}, {0,0,g->five,g->eight},
        {g->one,0,g->three,g->eight}, {0,0,g->eight,g->one}
    };
    memset(f,0,sizeof(*f));
    f->page.width_points=f->page.height_points=
        fractional?g->fraction_canvas:g->canvas;
    f->page.background_argb=UINT32_C(0xffffffff);
    f->ir.pages=&f->page;f->ir.page_count=1u;
    f->ir.operations=f->ops;f->ir.operation_count=strlen(p->kinds);
    f->ir.resources.graphics_states=f->gs;
    f->ir.resources.graphics_state_count=f->ir.resources.graphics_state_capacity=2u;
    f->ir.resources.clips=f->clips;
    f->ir.resources.clip_count=f->ir.resources.clip_capacity=3u;
    arch(f->shapes[0],g->one,g->seven,g);
    arch(f->shapes[1],g->three,g->eight,g);
    for(size_t i=0u;i<2u;++i) {
        f->gs[i].fill_alpha=f->gs[i].stroke_alpha=1;
        f->gs[i].blend_mode=QUANTAPDF_COMPOSER_BLEND_NORMAL;
        f->gs[i].clip_id=i==0u?1u:3u;
        f->clips[i].kind=QUANTAPDF_COMPOSER_CLIP_PATH_INTERNAL;
        f->clips[i].commands=f->shapes[i];f->clips[i].command_count=5u;
        f->clips[i].fill_rule=QUANTAPDF_COMPOSER_FILL_NONZERO;
        f->clips[i].transform=(quantapdf_affine_transform){1,0,0,1,0,0};
    }
    f->clips[2].kind=QUANTAPDF_COMPOSER_CLIP_INTERSECTION_INTERNAL;
    f->clips[2].members=f->members;f->clips[2].member_count=2u;
    f->members[0]=1u;f->members[1]=2u;
    for(size_t i=0u;i<f->ir.operation_count;++i) {
        const unsigned id=(unsigned)(p->states[i]-'0');
        rectangle(f->paths[i],bounds[i].x0,bounds[i].y0,bounds[i].x1,bounds[i].y1);
        if(p->kinds[i]=='P')path_op(f,i,id,5u);
        else {
            quantapdf_composer_operation *op=&f->ops[i];
            quantapdf_composer_image_state *im=&f->images[images];
            unsigned char *pixels=f->pixels[images];
            for(size_t j=0u;j<4u;++j) {
                pixels[j*3u]=(unsigned char)(colors[i]>>16u);
                pixels[j*3u+1u]=(unsigned char)(colors[i]>>8u);
                pixels[j*3u+2u]=(unsigned char)colors[i];
            }
            im->data=pixels;im->size=12u;im->width=im->height=2u;
            im->components=3;im->format=QUANTAPDF_COMPOSER_IMAGE_FORMAT_RAW;
            op->kind=QUANTAPDF_COMPOSER_OPERATION_IMAGE;
            op->bounds=bounds[i];op->value.image.image_id=
                (quantapdf_composer_image_id)(images+1u);
            op->value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V2_SIZE;
            op->value.image.options.fit=QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
            state(op,id);++images;
        }
    }
    if(images!=0u)f->ir.resources.images=f->images;
    f->ir.resources.image_count=f->ir.resources.image_capacity=images;
}

static void source(fixture *f,int fractional,const profile *p)
{
    setup(f,&grids[0],fractional,p);
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

static int projection_and_ownership(void)
{
    for(size_t d=0u;d<3u;++d)
    for(int fractional=0;fractional<2;++fractional)
    for(size_t p=0u;p<sizeof(profiles)/sizeof(profiles[0]);++p) {
        const geometry *g=&grids[d];
        const double viewport=fractional?g->fraction_viewport:g->viewport;
        fixture f,reference,saved;
        quantapdf_ir_raster_image actual={0},expected={0},again={0};
        source(&f,fractional,&profiles[p]);
        setup(&reference,g,fractional,&profiles[p]);
        OK(quantapdf_ir_raster_scoped_mixed_v5_clip_states_device_viewport(
            &reference.ir,0u,&budget,viewport,viewport,&expected));
        for(unsigned rule=0u;rule<4u;++rule) {
            f.clips[0].fill_rule=(rule&1u)?
                QUANTAPDF_COMPOSER_FILL_EVEN_ODD:QUANTAPDF_COMPOSER_FILL_NONZERO;
            f.clips[1].fill_rule=(rule&2u)?
                QUANTAPDF_COMPOSER_FILL_EVEN_ODD:QUANTAPDF_COMPOSER_FILL_NONZERO;
            f.members[0]=(rule&1u)?2u:1u;f.members[1]=(rule&1u)?1u:2u;
            memcpy(&saved,&f,sizeof(saved));
            OK(quantapdf_native_device_ir_render_v1_clip_states(
                &f.ir,g->dpi,&budget,&actual));
            CHECK(equal(&actual,&expected));
            CHECK(memcmp(&saved,&f,sizeof(saved))==0);
            CHECK(rgba(&actual,g->right_x,g->inside_y)==UINT32_C(0xff0000ff));
            CHECK(rgba(&actual,g->both_x,g->inside_y)==UINT32_C(0x0000ffff));
            CHECK(rgba(&actual,g->first_x,g->inside_y)==
                (strlen(profiles[p].kinds)>=3u && profiles[p].states[2]=='1'?
                    UINT32_C(0x00ff00ff):profiles[p].states[1]=='1'?
                    UINT32_C(0x0000ffff):UINT32_C(0xff0000ff)));
            quantapdf_ir_raster_image_dispose(&actual);
        }
        OK(quantapdf_native_device_ir_render_v1_clip_states(
            &f.ir,g->dpi,&budget,&actual));
        OK(quantapdf_native_device_ir_render_v1_clip_states(
            &f.ir,g->dpi,&budget,&again));
        memset(&f,0,sizeof(f));
        CHECK(equal(&actual,&expected) && equal(&again,&expected));
        quantapdf_ir_raster_image_dispose(&actual);
        quantapdf_ir_raster_image_dispose(&again);
        quantapdf_ir_raster_image_dispose(&expected);
        CHECK(cleared(&actual) && cleared(&again) && cleared(&expected));
    }
    return EXIT_SUCCESS;
}

/* For rectangular leaves, explicitly author each resulting filled
 * rectangle in device coordinates, and use the old resource-free PATH
 * entrypoint. Solid RAW paints have identical geometric coverage. */
static int analytic_rectangles_and_empty(void)
{
    for(size_t d=0u;d<3u;++d)
    for(int fractional=0;fractional<2;++fractional)
    for(size_t p=0u;p<sizeof(profiles)/sizeof(profiles[0]);++p)
    for(unsigned empty=0u;empty<3u;++empty) {
        fixture f,reference;
        const geometry *g=&grids[d];
        const double viewport=fractional?g->fraction_viewport:g->viewport;
        quantapdf_ir_raster_image actual={0},expected={0};
        source(&f,fractional,&profiles[p]);
        rectangle(f.shapes[0],1,1,7,7);
        rectangle(f.shapes[1],3,2,8,8);
        if(empty!=0u) {
            zero_contour(f.shapes[empty-1u]);f.clips[empty-1u].command_count=4u;
        }
        setup(&reference,g,fractional,&profiles[p]);
        memset(&reference.ir.resources,0,sizeof(reference.ir.resources));
        for(size_t i=0u;i<f.ir.operation_count;++i) {
            const unsigned id=(unsigned)(profiles[p].states[i]-'0');
            float x0=0,y0=0,x1=g->eight,y1=g->eight;
            size_t count=5u;
            if(i==1u)x1=g->five;
            if(i==2u){x0=g->one;x1=g->three;}
            if(i==3u)y1=g->one;
            if(id!=0u) {
                if(x0<g->one)x0=g->one;
                y0=g->one;
                if(x1>g->seven)x1=g->seven;
                if(y1>g->seven)y1=g->seven;
                if(id==2u){if(x0<g->three)x0=g->three;y0=g->two;}
            }
            if((id!=0u && empty==1u) || (id==2u && empty==2u) ||
               x1<=x0 || y1<=y0) {
                /* The old Fill-only API rejects a zero-edge PATH.
                 * An authored rectangle beyond the physical viewport
                 * supplies the independent no-ink reference instead. */
                rectangle(reference.paths[i],g->eight,g->eight,
                    g->eight+g->one,g->eight+g->one);
            } else rectangle(reference.paths[i],x0,y0,x1,y1);
            path_op(&reference,i,0u,count);
        }
        {
            const quantapdf_status status=quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
                &reference.ir,0u,&budget,viewport,viewport,&expected);
            if(status!=QUANTAPDF_OK) {
                fprintf(stderr,"rectangle reference dpi=%u fractional=%d profile=%zu empty=%u status=%d\n",
                    g->dpi,fractional,p,empty,(int)status);
                return EXIT_FAILURE;
            }
        }
        OK(quantapdf_native_device_ir_render_v1_clip_states(
            &f.ir,g->dpi,&budget,&actual));
        CHECK(equal(&actual,&expected));
        quantapdf_ir_raster_image_dispose(&actual);
        quantapdf_ir_raster_image_dispose(&expected);
    }
    return EXIT_SUCCESS;
}

static void triangle(quantapdf_composer_path_command *p,
    float x0,float y0,float x1,float y1,float x2,float y2)
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,x0,y0);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,x1,y1);
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,x2,y2);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
}

static int analytic_line_pair(void)
{
    const profile pair={"PP","12"};
    for(size_t d=0u;d<3u;++d)
    for(int fractional=0;fractional<2;++fractional) {
        fixture f,reference;
        const geometry *g=&grids[d];
        const double viewport=fractional?g->fraction_viewport:g->viewport;
        quantapdf_ir_raster_image actual={0},expected={0};
        source(&f,fractional,&pair);setup(&reference,g,fractional,&pair);
        triangle(f.shapes[0],1,1,7,1,1,7);
        triangle(f.shapes[1],1,1,7,1,7,7);
        f.clips[0].command_count=f.clips[1].command_count=4u;
        rectangle(f.paths[1],0,0,8,8);
        memset(&reference.ir.resources,0,sizeof(reference.ir.resources));
        triangle(reference.paths[0],g->one,g->one,g->seven,g->one,g->one,g->seven);
        triangle(reference.paths[1],g->one,g->one,g->seven,g->one,g->four,g->four);
        path_op(&reference,0u,0u,4u);path_op(&reference,1u,0u,4u);
        OK(quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
            &reference.ir,0u,&budget,viewport,viewport,&expected));
        OK(quantapdf_native_device_ir_render_v1_clip_states(
            &f.ir,g->dpi,&budget,&actual));
        CHECK(equal(&actual,&expected));
        quantapdf_ir_raster_image_dispose(&actual);
        quantapdf_ir_raster_image_dispose(&expected);
    }
    return EXIT_SUCCESS;
}

static int shared_samples(void)
{
    static const unsigned char pattern[12]={255,0,0,0,0,255,0,255,0,255,255,0};
    fixture f;
    quantapdf_ir_raster_image im={0};
    for(unsigned order=0u;order<2u;++order) {
        source(&f,0,&profiles[1]);
        rectangle(f.shapes[0],0,0,4.5f,8);
        rectangle(f.shapes[1],4.5f,0,8,8);
        f.members[0]=order?2u:1u;f.members[1]=order?1u:2u;
        memcpy(f.pixels[0],pattern,sizeof(pattern));
        for(size_t i=0u;i<4u;++i) {
            f.pixels[1][i*3u]=255u;
            f.pixels[1][i*3u+1u]=f.pixels[1][i*3u+2u]=0u;
        }
        f.ops[1].bounds=(quantapdf_rect){0,0,8,8};
        OK(quantapdf_native_device_ir_render_v1_clip_states(&f.ir,72u,&budget,&im));
        /* First leaf covers half the boundary pixel; the disjoint root
         * covers no shared sample. Multiplying two half masks would
         * wrongly add a red contribution over this half-blue pixel. */
        CHECK(rgba(&im,4u,2u)==UINT32_C(0x7f7fffff));
        CHECK(rgba(&im,3u,2u)==UINT32_C(0xff0000ff));
        CHECK(rgba(&im,5u,2u)==UINT32_C(0xffffffff));
        quantapdf_ir_raster_image_dispose(&im);
    }
    return EXIT_SUCCESS;
}

static void nine_commands(quantapdf_composer_path_command *p,float four,float eight)
{
    point(&p[0],QUANTAPDF_COMPOSER_PATH_MOVE_TO,0,0);
    point(&p[1],QUANTAPDF_COMPOSER_PATH_LINE_TO,four,0);
    point(&p[2],QUANTAPDF_COMPOSER_PATH_LINE_TO,eight,0);
    point(&p[3],QUANTAPDF_COMPOSER_PATH_LINE_TO,eight,four);
    point(&p[4],QUANTAPDF_COMPOSER_PATH_LINE_TO,eight,eight);
    point(&p[5],QUANTAPDF_COMPOSER_PATH_LINE_TO,four,eight);
    point(&p[6],QUANTAPDF_COMPOSER_PATH_LINE_TO,0,eight);
    point(&p[7],QUANTAPDF_COMPOSER_PATH_LINE_TO,0,four);
    point(&p[8],QUANTAPDF_COMPOSER_PATH_CLOSE,0,0);
}

static int command_boundary(void)
{
    const profile pair={"PP","12"};
    for(size_t d=0u;d<3u;++d)
    for(int fractional=0;fractional<2;++fractional) {
        fixture f,reference;
        const geometry *g=&grids[d];
        const double viewport=fractional?g->fraction_viewport:g->viewport;
        quantapdf_ir_raster_image actual={0},expected={0};
        source(&f,fractional,&pair);setup(&reference,g,fractional,&pair);
        memset(&reference.ir.resources,0,sizeof(reference.ir.resources));
        for(size_t i=0u;i<2u;++i) {
            nine_commands(f.shapes[i],4,8);f.clips[i].command_count=9u;
            nine_commands(f.paths[i],4,8);f.ops[i].value.path.command_count=9u;
            rectangle(reference.paths[i],0,0,g->eight,g->eight);
            path_op(&reference,i,0u,5u);
        }
        OK(quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
            &reference.ir,0u,&budget,viewport,viewport,&expected));
        OK(quantapdf_native_device_ir_render_v1_clip_states(
            &f.ir,g->dpi,&budget,&actual));
        CHECK(equal(&actual,&expected));
        quantapdf_ir_raster_image_dispose(&actual);
        quantapdf_ir_raster_image_dispose(&expected);
    }
    return EXIT_SUCCESS;
}

static int all_state_orders(void)
{
    /* Explicit accepted words, independent of the implementation's
     * phase state machine. Enumerate all 117 words of length 2..4. */
    static const char *const admitted[]={
        "12","21",
        "112","120","121","122","210","211","221",
        "1112","1120","1121","1122","1200","1210","1211",
        "1220","1221","1222","2100","2110","2111","2210","2211","2221"
    };
    size_t accepted=0u,rejected=0u;
    for(size_t count=2u,total=9u;count<=4u;++count,total*=3u)
    for(size_t word=0u;word<total;++word) {
        fixture f;
        char order[5]={0},kinds[5]={0};
        profile p={kinds,order};
        size_t value=word;
        int accept=0;
        for(size_t i=count;i>0u;--i) {
            order[i-1u]=(char)('0'+value%3u);value/=3u;kinds[i-1u]='P';
        }
        for(size_t i=0u;i<sizeof(admitted)/sizeof(admitted[0]);++i)
            if(strcmp(order,admitted[i])==0)accept=1;
        source(&f,0,&p);
        for(unsigned entry=0u;entry<2u;++entry) {
            quantapdf_ir_raster_image im;
            quantapdf_status status;
            memset(&im,0xa5,sizeof(im));
            status=entry==0u?
                quantapdf_native_device_ir_render_v1_clip_states(&f.ir,72u,&budget,&im):
                quantapdf_ir_raster_scoped_mixed_v5_clip_states_device_viewport(
                    &f.ir,0u,&budget,8,8,&im);
            if(accept) {
                CHECK(status==QUANTAPDF_OK && im.pixels!=NULL);
                quantapdf_ir_raster_image_dispose(&im);
            } else CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED && cleared(&im));
        }
        if(accept)++accepted;else ++rejected;
    }
    CHECK(accepted==25u && rejected==92u);
    return EXIT_SUCCESS;
}

static int refusal(const quantapdf_ir *ir,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limit,int direct)
{
    quantapdf_ir_raster_image im;
    memset(&im,0xa5,sizeof(im));
    CHECK(quantapdf_native_device_ir_render_v1_clip_states(ir,dpi,limit,&im)!=QUANTAPDF_OK);
    CHECK(cleared(&im));
    if(direct) {
        memset(&im,0xa5,sizeof(im));
        CHECK(quantapdf_ir_raster_scoped_mixed_v5_clip_states_device_viewport(
            ir,0u,limit,8,8,&im)!=QUANTAPDF_OK);
        CHECK(cleared(&im));
    }
    return EXIT_SUCCESS;
}

static int old_entrypoints(void)
{
    for(size_t p=0u;p<sizeof(profiles)/sizeof(profiles[0]);++p) {
        fixture f;
        quantapdf_ir_raster_image im;
        source(&f,0,&profiles[p]);
        for(unsigned entry=0u;entry<8u;++entry) {
            quantapdf_status status=QUANTAPDF_OK;
            memset(&im,0xa5,sizeof(im));
            switch(entry) {
            case 0u:status=quantapdf_native_device_ir_render_v1(&f.ir,72u,&budget,&im);break;
            case 1u:status=quantapdf_native_device_ir_render_v1_two_clip(&f.ir,72u,&budget,&im);break;
            case 2u:status=quantapdf_native_device_ir_render_v1_clipped_paths(&f.ir,72u,&budget,&im);break;
            case 3u:status=quantapdf_ir_raster_scoped_mixed_v5(&f.ir,0u,&budget,&im);break;
            case 4u:status=quantapdf_ir_raster_scoped_mixed_v5_device(&f.ir,0u,&budget,&im);break;
            case 5u:status=quantapdf_ir_raster_scoped_mixed_v5_device_viewport(
                &f.ir,0u,&budget,8,8,&im);break;
            case 6u:status=quantapdf_ir_raster_scoped_mixed_v5_two_clip_device_viewport(
                &f.ir,0u,&budget,8,8,&im);break;
            case 7u:status=quantapdf_ir_raster_scoped_mixed_v5_clipped_paths_device_viewport(
                &f.ir,0u,&budget,8,8,&im);break;
            default:CHECK(0);break;
            }
            CHECK(status==QUANTAPDF_ERROR_UNSUPPORTED && cleared(&im));
        }
    }
    return EXIT_SUCCESS;
}

static int malformed(void)
{
    fixture f,saved;
    quantapdf_composer_font_state font={0};
    quantapdf_composer_form_state form={0};
    quantapdf_composer_paint_state paint={0};
    quantapdf_composer_soft_mask_state soft_mask={0};
    quantapdf_composer_link_state link={0};
    quantapdf_composer_outline_state outline={0};
    CHECK(refusal(NULL,72u,&budget,1)==EXIT_SUCCESS);
    source(&f,0,&profiles[0]);
    CHECK(refusal(&f.ir,90u,&budget,0)==EXIT_SUCCESS);
    CHECK(refusal(&f.ir,72u,NULL,1)==EXIT_SUCCESS);
    CHECK(quantapdf_native_device_ir_render_v1_clip_states(
        &f.ir,72u,&budget,NULL)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_ir_raster_scoped_mixed_v5_clip_states_device_viewport(
        &f.ir,0u,&budget,8,8,NULL)==QUANTAPDF_ERROR_ARGUMENT);
    for(unsigned bad=0u;bad<69u;++bad) {
        source(&f,0,&profiles[0]);
        switch(bad) {
        case 0u:f.ir.operation_count=0u;break;
        case 1u:f.ir.operation_count=1u;break;
        case 2u:f.ir.operation_count=5u;break;
        case 3u:f.ir.operations=NULL;break;
        case 4u:f.ir.resources.images=f.images;break;
        case 5u:f.ir.resources.image_count=1u;break;
        case 6u:f.ir.resources.image_capacity=1u;break;
        case 7u:f.ir.resources.graphics_states=NULL;break;
        case 8u:f.ir.resources.graphics_state_count=1u;break;
        case 9u:f.ir.resources.graphics_state_count=3u;break;
        case 10u:f.ir.resources.graphics_state_capacity=1u;break;
        case 11u:f.ir.resources.graphics_state_capacity=3u;break;
        case 12u:f.ir.resources.clips=NULL;break;
        case 13u:f.ir.resources.clip_count=2u;break;
        case 14u:f.ir.resources.clip_capacity=4u;break;
        case 15u:f.clips[2].members=NULL;break;
        case 16u:f.clips[2].member_count=3u;break;
        case 17u:f.members[1]=1u;break;
        case 18u:f.members[1]=3u;break;
        case 19u:f.clips[2].commands=f.shapes[0];break;
        case 20u:f.clips[2].command_count=1u;break;
        case 21u:f.clips[2].transform.e=0.25f;break;
        case 22u:f.clips[1].commands=NULL;break;
        case 23u:f.clips[1].command_count=3u;break;
        case 24u:f.clips[0].command_count=10u;break;
        case 25u:f.clips[0].transform.e=0.25f;break;
        case 26u:f.shapes[1][1].point2.x=NAN;break;
        case 27u:f.shapes[0][1].point3.y=1000001.0f;break;
        case 28u:f.shapes[1][2].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;break;
        case 29u:f.shapes[1][4].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;break;
        case 30u:f.clips[1].members=f.members;break;
        case 31u:f.clips[1].member_count=1u;break;
        case 32u:f.gs[0].clip_id=3u;break;
        case 33u:f.gs[1].clip_id=1u;break;
        case 34u:f.gs[0].fill_alpha=0.5f;break;
        case 35u:f.gs[1].stroke_alpha=0.5f;break;
        case 36u:f.gs[1].soft_mask_id=1u;break;
        case 37u:state(&f.ops[1],1u);break;
        case 38u:state(&f.ops[0],2u);state(&f.ops[2],2u);break;
        case 39u:f.ops[1].value.path.options.graphics_state_id=1u;break;
        case 40u:f.ops[3].kind=QUANTAPDF_COMPOSER_OPERATION_TEXT;break;
        case 41u:f.ops[3].page_index=1u;break;
        case 42u:f.ops[3].value.path.command_count=3u;break;
        case 43u:f.ops[3].value.path.command_count=10u;break;
        case 44u:f.ops[3].value.path.commands=NULL;break;
        case 45u:f.paths[3][3].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;break;
        case 46u:f.paths[3][4].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;break;
        case 47u:f.ops[3].value.path.options.stroke=1;break;
        case 48u:f.ops[3].value.path.options.fill_argb=UINT32_C(0x8000ff00);break;
        case 49u:f.ops[3].value.path.options.struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_MIN_SIZE;break;
        case 50u:f.ops[3].value.path.options.fill_paint_id=1u;break;
        case 51u:f.ir.resources.fonts=&font;break;
        case 52u:f.ir.resources.font_capacity=1u;break;
        case 53u:f.ir.resources.forms=&form;break;
        case 54u:f.ir.resources.form_capacity=1u;break;
        case 55u:f.ir.resources.paints=&paint;break;
        case 56u:f.ir.resources.paint_capacity=1u;break;
        case 57u:f.ir.resources.soft_masks=&soft_mask;break;
        case 58u:f.ir.resources.soft_mask_capacity=1u;break;
        case 59u:f.ir.links=&link;break;
        case 60u:f.ir.outlines=&outline;break;
        case 61u:f.ir.resources.graphics_state_count=0u;break;
        case 62u:f.ir.page_count=2u;break;
        case 63u:f.page.width_points=7.1f;break;
        case 64u:f.ir.pages=NULL;break;
        case 65u:state(&f.ops[3],3u);break;
        case 66u:f.clips[1].fill_rule=(quantapdf_composer_fill_rule)55;break;
        case 67u:f.gs[1].blend_mode=(quantapdf_composer_blend_mode)55;break;
        case 68u:f.paths[3][2].point1.y=NAN;break;
        default:CHECK(0);break;
        }
        memcpy(&saved,&f,sizeof(saved));
        if(refusal(&f.ir,144u,&budget,1)!=EXIT_SUCCESS) {
            fprintf(stderr,"malformed PATH case %u\n",bad);return EXIT_FAILURE;
        }
        CHECK(memcmp(&saved,&f,sizeof(saved))==0);
    }
    for(unsigned bad=0u;bad<15u;++bad) {
        source(&f,0,&profiles[7]);
        switch(bad) {
        case 0u:f.ir.resources.image_capacity=3u;break;
        case 1u:f.ir.resources.images=NULL;break;
        case 2u:f.ir.resources.image_count=3u;f.ir.resources.image_capacity=3u;break;
        case 3u:f.images[1].size=11u;break;
        case 4u:f.images[1].data=NULL;break;
        case 5u:f.images[1].width=0u;break;
        case 6u:f.images[1].components=4;break;
        case 7u:f.images[1].has_alpha=1;break;
        case 8u:f.images[1].alpha_data=f.pixels[0];break;
        case 9u:f.ops[1].value.image.image_id=1u;break;
        case 10u:f.ops[1].value.image.image_id=3u;break;
        case 11u:f.ops[1].value.image.options.graphics_state_id=1u;break;
        case 12u:f.ops[1].value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_MIN_SIZE;break;
        case 13u:f.ops[1].bounds.x1=NAN;break;
        case 14u:f.ops[1].bounds.x1=0.0f;break;
        default:CHECK(0);break;
        }
        memcpy(&saved,&f,sizeof(saved));
        if(refusal(&f.ir,144u,&budget,1)!=EXIT_SUCCESS) {
            fprintf(stderr,"malformed IMAGE case %u\n",bad);return EXIT_FAILURE;
        }
        CHECK(memcmp(&saved,&f,sizeof(saved))==0);
    }
    source(&f,0,&profiles[9]);
    f.ops[3].value.image.options.struct_size=QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_MIN_SIZE;
    CHECK(refusal(&f.ir,144u,&budget,1)==EXIT_SUCCESS);
    source(&f,0,&profiles[9]);
    f.ops[3].value.image.options.graphics_state_id=1u;
    CHECK(refusal(&f.ir,144u,&budget,1)==EXIT_SUCCESS);
    /* These valid operations/resources violate only the retained profile
     * cardinalities: three IMAGEs, four IMAGEs, or three PATHs + IMAGE. */
    for(unsigned bad=0u;bad<4u;++bad) {
        source(&f,0,&profiles[7]);
        if(bad<3u) {
            f.ops[2]=f.ops[0];state(&f.ops[2],1u);
            if(bad==0u)f.ir.operation_count=3u;
            if(bad==1u){f.ops[3]=f.ops[1];state(&f.ops[3],0u);}
        } else {
            path_op(&f,1u,2u,5u);
            f.ir.resources.image_count=f.ir.resources.image_capacity=1u;
        }
        CHECK(refusal(&f.ir,72u,&budget,1)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

static int quotas_and_reused_source(void)
{
    fixture f;
    quantapdf_ir_raster_image im={0};
    source(&f,0,&profiles[0]);
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
        CHECK(refusal(&f.ir,144u,&limit,0)==EXIT_SUCCESS);
    }
    /* Both Do operations reuse one 2x2 RAW input. The source charge is
     * four pixels, while both full destination rectangles are charged. */
    source(&f,0,&profiles[1]);
    f.ir.resources.image_count=f.ir.resources.image_capacity=1u;
    f.ops[1].value.image.image_id=1u;
    f.ops[1].bounds=(quantapdf_rect){0,0,8,8};
    rectangle(f.shapes[0],1,1,7,7);rectangle(f.shapes[1],3,2,8,8);
    for(size_t d=0u;d<3u;++d) {
        quantapdf_ir_raster_scoped_mixed_v5_limits limit=budget;
        const size_t side=(size_t)grids[d].canvas;
        limit.max_source_pixels=4u;limit.max_drawn_pixels=2u*side*side;
        OK(quantapdf_native_device_ir_render_v1_clip_states(
            &f.ir,grids[d].dpi,&limit,&im));
        quantapdf_ir_raster_image_dispose(&im);
        --limit.max_source_pixels;
        CHECK(refusal(&f.ir,grids[d].dpi,&limit,0)==EXIT_SUCCESS);
        ++limit.max_source_pixels;--limit.max_drawn_pixels;
        CHECK(refusal(&f.ir,grids[d].dpi,&limit,0)==EXIT_SUCCESS);
    }
    {
        /* Largest layer has four IMAGE edges plus two four-edge leaves.
         * The existing equal per-layer division is unchanged: two times
         * 12 edges and two times 64 pixels x 16 samples x 12 edges. */
        quantapdf_ir_raster_scoped_mixed_v5_limits limit=budget;
        limit.max_edges=24u;limit.max_sample_tests=24576u;
        OK(quantapdf_native_device_ir_render_v1_clip_states(&f.ir,72u,&limit,&im));
        quantapdf_ir_raster_image_dispose(&im);
        --limit.max_edges;
        CHECK(refusal(&f.ir,72u,&limit,1)==EXIT_SUCCESS);
        ++limit.max_edges;--limit.max_sample_tests;
        CHECK(refusal(&f.ir,72u,&limit,1)==EXIT_SUCCESS);
    }
    return EXIT_SUCCESS;
}

int main(void)
{
    CHECK(projection_and_ownership()==EXIT_SUCCESS);
    CHECK(analytic_rectangles_and_empty()==EXIT_SUCCESS);
    CHECK(analytic_line_pair()==EXIT_SUCCESS);
    CHECK(shared_samples()==EXIT_SUCCESS);
    CHECK(command_boundary()==EXIT_SUCCESS);
    CHECK(all_state_orders()==EXIT_SUCCESS);
    CHECK(old_entrypoints()==EXIT_SUCCESS);
    CHECK(malformed()==EXIT_SUCCESS);
    CHECK(quotas_and_reused_source()==EXIT_SUCCESS);
    fprintf(stderr,"[native.clip_states_device] completed\n");
    return EXIT_SUCCESS;
}
