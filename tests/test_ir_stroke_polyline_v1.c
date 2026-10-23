/* Pure C11 numerical goldens. No source backend or oracle linkage. */
#include "render/ir_scoped_operations_v1.h"
#include "render/ir_stroke_polyline_v1.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
#define OK(x) CHECK((x)==QUANTAPDF_OK)
static quantapdf_composer_path_command commands[8];
static quantapdf_composer_operation ops[2];
static quantapdf_composer_page_state page;
static quantapdf_ir ir;
static const quantapdf_ir_raster_v1_limits limits={1024,2,512,16000000};
static void init(void)
{
    memset(commands,0,sizeof(commands)); memset(ops,0,sizeof(ops));
    memset(&page,0,sizeof(page)); memset(&ir,0,sizeof(ir));
    page.width_points=page.height_points=32; page.suppress_background=1;
    ir.pages=&page; ir.page_count=1; ir.operations=ops; ir.operation_count=1;
    ops[0].kind=QUANTAPDF_COMPOSER_OPERATION_PATH;
    ops[0].value.path.commands=commands; ops[0].value.path.command_count=3;
    quantapdf_composer_path_options *o=&ops[0].value.path.options;
    o->struct_size=QUANTAPDF_COMPOSER_PATH_OPTIONS_V4_SIZE;
    o->stroke=1; o->stroke_argb=0xffff0000u; o->stroke_width=4;
    o->miter_limit=10; o->line_cap=QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    o->line_join=QUANTAPDF_COMPOSER_LINE_JOIN_MITER;
    o->transform=(quantapdf_affine_transform){1,0,0,1,0,0};
    commands[0].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO; commands[0].point1=(quantapdf_point){8,16};
    commands[1].kind=commands[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[1].point1=(quantapdf_point){16,16}; commands[2].point1=(quantapdf_point){16,8};
}
static unsigned alpha(const quantapdf_ir_raster_image *im,size_t x,size_t y)
{ return im->pixels[y*im->stride+x*4+3]; }
static quantapdf_ir_raster_image render_with(const quantapdf_ir_raster_v1_limits *bounds)
{
    quantapdf_ir_raster_image image={0};
    quantapdf_composer_operation original=ops[0];
    quantapdf_composer_path_command copy[8]; memcpy(copy,commands,sizeof(copy));
    OK(quantapdf_ir_raster_scoped_operations_v2(&ir,0,bounds,&image));
    CHECK(memcmp(copy,commands,sizeof(copy))==0 && memcmp(&original,ops,sizeof(original))==0);
    return image;
}
static quantapdf_ir_raster_image render(void)
{ return render_with(&limits); }
static void reject(void)
{
    quantapdf_ir_raster_image image; memset(&image,0x5a,sizeof(image));
    CHECK(quantapdf_ir_raster_scoped_operations_v2(&ir,0,&limits,&image)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(!image.pixels && !image.size && !image.width && !image.height && !image.stride);
}
static float dashes[65];
static void dash(float a,float b,float phase)
{
    dashes[0]=a; dashes[1]=b;
    ops[0].value.path.dash_lengths=dashes; ops[0].value.path.dash_count=2;
    ops[0].value.path.dash_phase=phase;
}
static void dash_line(void)
{
    init(); ops[0].value.path.command_count=2; ops[0].value.path.options.stroke_width=2;
    commands[0].point1=(quantapdf_point){4,16}; commands[1].point1=(quantapdf_point){28,16};
}
static void dash_square(void)
{
    init(); ops[0].value.path.command_count=5;
    commands[0].point1=(quantapdf_point){8,8}; commands[1].point1=(quantapdf_point){16,8};
    commands[2].point1=(quantapdf_point){16,16};
    commands[3].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO; commands[3].point1=(quantapdf_point){8,16};
    commands[4].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
}
static void test_dashes(void)
{
    quantapdf_ir_raster_image image={0},reference={0};
    dash_line(); dash(4,4,0); image=render();
    CHECK(alpha(&image,5,15)==255 && alpha(&image,9,15)==0 && alpha(&image,13,15)==255 && alpha(&image,25,15)==0);
    /* Odd dash arrays repeat their on/off parity over TWO copies. */
    ops[0].value.path.dash_count=1; reference=render();
    CHECK(image.size==reference.size && memcmp(image.pixels,reference.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&image); quantapdf_ir_raster_image_dispose(&reference);
    dash_line(); dash(4,2,0); dashes[2]=2; ops[0].value.path.dash_count=3;
    image=render(); CHECK(alpha(&image,11,15)==255 && alpha(&image,13,15)==0 && alpha(&image,17,15)==255);
    quantapdf_ir_raster_image_dispose(&image);
    dash_line(); dash(4,4,2); image=render();
    CHECK(alpha(&image,5,15)==255 && alpha(&image,6,15)==0 && alpha(&image,10,15)==255 && alpha(&image,14,15)==0);
    ops[0].value.path.dash_phase=8002; reference=render();
    CHECK(memcmp(image.pixels,reference.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&image); quantapdf_ir_raster_image_dispose(&reference);
    dash_line(); dash(3,5,6); image=render();
    CHECK(alpha(&image,4,15)==0 && alpha(&image,6,15)==255 && alpha(&image,9,15)==0);
    quantapdf_ir_raster_image_dispose(&image);
    dash_line(); dash(4,4,2); reference=render();
    for(size_t i=0;i<64;++i) dashes[i]=4;
    ops[0].value.path.dash_count=64; image=render();
    CHECK(memcmp(image.pixels,reference.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&image); quantapdf_ir_raster_image_dispose(&reference);
    /* Reset dash phase at every authored MOVE, not every edge. */
    dash_line(); dash(4,4,0); commands[1].point1.x=14;
    commands[2]=commands[0]; commands[2].point1.y=24;
    commands[3]=commands[1]; commands[3].point1=(quantapdf_point){28,24}; ops[0].value.path.command_count=4;
    image=render(); CHECK(alpha(&image,5,23)==255 && alpha(&image,9,23)==0);
    quantapdf_ir_raster_image_dispose(&image);
    init(); dash(12,4,0); image=render(); CHECK(alpha(&image,17,17)==255);
    quantapdf_ir_raster_image_dispose(&image);
    dash(8,4,0); image=render(); CHECK(alpha(&image,17,17)==0);
    quantapdf_ir_raster_image_dispose(&image);
    /* Dash fragments have caps at a closed seam; coincident head/tail
     * coordinates do not turn their separate endpoints into a join. */
    dash_square(); reference=render(); dash(32,8,0); image=render();
    CHECK(alpha(&image,7,7)==0 && alpha(&reference,7,7)==255);
    quantapdf_ir_raster_image_dispose(&image); quantapdf_ir_raster_image_dispose(&reference);
    dash(10,4,0); image=render(); CHECK(alpha(&image,7,7)==0); quantapdf_ir_raster_image_dispose(&image);
    dash(10,6,0); image=render(); CHECK(alpha(&image,7,7)==0); quantapdf_ir_raster_image_dispose(&image);
    dash_line(); dash(4,6,0); ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_SQUARE;
    image=render(); CHECK(alpha(&image,3,15)==255 && alpha(&image,8,15)==255 && alpha(&image,10,15)==0);
    quantapdf_ir_raster_image_dispose(&image);
    /* Entirely off geometry is a successful transparent result, not refusal.
     * It must still pass transform, Clip and later-operation validation. */
    dash_line(); dash(1,100,2); image=render();
    for(size_t y=0;y<32;++y) for(size_t x=0;x<32;++x) CHECK(alpha(&image,x,y)==0);
    quantapdf_ir_raster_image_dispose(&image);
    ops[0].value.path.options.transform.a=NAN; reject();
    init(); dash(1,100,2); commands[2].point1=commands[0].point1; reject();
    dash_line(); dash(1,100,2); ops[1]=ops[0]; ops[1].value.path.options.stroke_width=0; ir.operation_count=2; reject();
    dash_line(); dash(0,0,0); reject(); dash(-1,2,0); reject(); dash(NAN,2,0); reject(); dash(INFINITY,2,0); reject();
    dash(1,2,-1); reject(); dash(1,2,NAN); reject(); dash(1,2,INFINITY); reject();
    dash(1,2,0); ops[0].value.path.dash_count=65; reject();
    dash_line(); dash(4,4,0);
    quantapdf_ir_raster_v1_limits exact=limits; exact.max_edges=12;
    OK(quantapdf_ir_raster_scoped_operations_v2(&ir,0,&exact,&image)); quantapdf_ir_raster_image_dispose(&image);
    exact.max_edges=11;
    CHECK(quantapdf_ir_raster_scoped_operations_v2(&ir,0,&exact,&image)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(!image.pixels && !image.size);
    dash_line(); dash(4,4,0); ops[1]=ops[0]; ops[1].value.path.options.stroke=0;
    ops[1].value.path.options.fill=1; ops[1].value.path.options.fill_argb=0x800000ffu;
    ops[1].value.path.dash_count=0; ir.operation_count=2; reject();
    dash_line(); dash(0.001f,0.001f,0); reject(); /* expansion capacity */
    dash_line(); dash(4,4,0); float copy[2]={dashes[0],dashes[1]}; image=render();
    CHECK(memcmp(copy,dashes,sizeof(copy))==0); quantapdf_ir_raster_image_dispose(&image);
}
/* Independent zero-dash geometry and state goldens. Dots start at path
 * distances 0,8,16, excluding a new dot at the terminal distance 24. */
static void test_zero_dashes(void)
{
    quantapdf_ir_raster_image image={0},reference={0};
    dash_line(); dash(0,8,0); image=render();
    for(size_t y=0;y<32;++y) for(size_t x=0;x<32;++x) CHECK(alpha(&image,x,y)==0);
    quantapdf_ir_raster_image_dispose(&image);
    ops[0].value.path.options.stroke_width=4;
    ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_SQUARE;
    image=render();
    for(size_t y=0;y<32;++y) for(size_t x=0;x<32;++x) {
        int inside=y>=14 && y<18 && ((x>=2 && x<6) || (x>=10 && x<14) || (x>=18 && x<22));
        CHECK(alpha(&image,x,y)==(inside ? 255u : 0u));
    }
    quantapdf_ir_raster_image_dispose(&image);
    ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    image=render();
    CHECK(alpha(&image,3,15)==255 && alpha(&image,11,15)==255 && alpha(&image,19,15)==255);
    CHECK(alpha(&image,2,14)>0 && alpha(&image,2,14)<100);
    CHECK(alpha(&image,7,15)==0 && alpha(&image,27,15)==0 && alpha(&image,28,15)==0);
    /* A dot at an internal source vertex is retained exactly once. */
    commands[1].point1.x=12; commands[2]=commands[1]; commands[2].point1.x=28;
    ops[0].value.path.command_count=3; reference=render();
    CHECK(memcmp(image.pixels,reference.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&image); quantapdf_ir_raster_image_dispose(&reference);
    /* Leading/trailing consecutive zeros are traversed, not discarded when
     * phase lands on an exact boundary, and odd arrays retain parity. */
    dash_line(); dash(0,0,0); dashes[2]=0; dashes[3]=8; ops[0].value.path.dash_count=4;
    ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_SQUARE;
    image=render(); dash(0,8,0); reference=render();
    CHECK(memcmp(image.pixels,reference.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&image); quantapdf_ir_raster_image_dispose(&reference);
    dash(0,8,3); image=render(); ops[0].value.path.dash_phase=8003; reference=render();
    CHECK(alpha(&image,8,15)==255 && alpha(&image,4,15)==0);
    CHECK(memcmp(image.pixels,reference.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&image); quantapdf_ir_raster_image_dispose(&reference);
    dash(0,8,8); image=render(); ops[0].value.path.dash_phase=0; reference=render();
    CHECK(memcmp(image.pixels,reference.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&image); quantapdf_ir_raster_image_dispose(&reference);
    dash(0,4,0); dashes[2]=4; ops[0].value.path.dash_count=3; image=render();
    for(size_t y=0;y<32;++y) for(size_t x=0;x<32;++x) {
        int inside=y>=15 && y<17 && ((x>=3 && x<5) || (x>=7 && x<17) || (x>=19 && x<21) || (x>=23 && x<29));
        CHECK(alpha(&image,x,y)==(inside ? 255u : 0u));
    }
    quantapdf_ir_raster_image_dispose(&image);
    /* A zero gap separates two dashes at a corner; their caps do not turn
     * into a miter join. Square caps still cover that outer corner. */
    init(); dash(8,0,0); image=render(); CHECK(alpha(&image,17,17)==0);
    quantapdf_ir_raster_image_dispose(&image);
    ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_SQUARE;
    image=render(); CHECK(alpha(&image,17,17)==255); quantapdf_ir_raster_image_dispose(&image);
    /* Square dot orientation follows the outgoing oblique segment. Its
     * center is (12,16), and projection along/perpendicular to (1,1)
     * gives x-extents 12 +/- 2*sqrt(2), not an axis-aligned square. */
    dash_line(); commands[1].point1=(quantapdf_point){12,16};
    commands[2].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO; commands[2].point1=(quantapdf_point){20,24};
    ops[0].value.path.command_count=3; ops[0].value.path.options.stroke_width=4;
    ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_SQUARE;
    dash(0,8,0); image=render(); CHECK(alpha(&image,14,15)>0 && alpha(&image,13,17)<255);
    quantapdf_ir_raster_image_dispose(&image);
    /* All-empty butt dots still validate later layers/transforms, charge
     * transitions, and refuse malformed periods before modulo arithmetic. */
    dash_line(); dash(0,8,0); ops[1]=ops[0]; ops[1].value.path.options.stroke_width=0; ir.operation_count=2; reject();
    dash_line(); dash(0,8,0); ops[0].value.path.options.transform.a=NAN; reject();
    dash_line(); dash(0,0,0); reject(); ops[0].value.path.dash_count=1; reject();
    dash_line(); dash(0,.0001f,0); reject(); /* transition limit without output */
    ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_ROUND; reject(); /* expanded edges */
    /* Maximum authored array with long chains of zero entries is bounded
     * even when every butt dot is invisible. */
    dash_line(); dash(0,8,0); memset(dashes,0,sizeof(dashes)); dashes[63]=8;
    ops[0].value.path.dash_count=64; image=render();
    for(size_t y=0;y<32;++y) for(size_t x=0;x<32;++x) CHECK(alpha(&image,x,y)==0);
    quantapdf_ir_raster_image_dispose(&image);
    /* Exact square-dot edge budget, immutable arrays and owned output after
     * all authored commands/options/dashes have been overwritten. */
    dash_line(); dash(0,8,0); ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_SQUARE;
    quantapdf_composer_path_operation fill={0}; float saved[2]={0,8};
    OK(quantapdf_ir_stroke_polyline_outline_v1(&ops[0].value.path,24,&fill));
    CHECK(memcmp(saved,dashes,sizeof(saved))==0 && fill.command_count==30);
    quantapdf_composer_path_operation failed; memset(&failed,0x5a,sizeof(failed));
    CHECK(quantapdf_ir_stroke_polyline_outline_v1(&ops[0].value.path,23,&failed)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(!failed.commands && !failed.command_count);
    memset(dashes,0,sizeof(dashes)); memset(commands,0,sizeof(commands)); memset(ops,0,sizeof(ops));
    ops[0].kind=QUANTAPDF_COMPOSER_OPERATION_PATH; ops[0].value.path=fill;
    OK(quantapdf_ir_raster_render_v1(&ir,0,&limits,&image));
    CHECK(alpha(&image,3,15)==255 && alpha(&image,7,15)==0);
    quantapdf_ir_raster_image_dispose(&image); quantapdf_ir_stroke_polyline_dispose_v1(&fill);
}
/* Measure the emitted float chords against an independent radius-2 circle,
 * rather than duplicating the tessellator's segment-count formula. */
static void check_round_chords(const quantapdf_composer_path_operation *fill,double scale)
{
    size_t begin=0,arcs=0;
    while(begin<fill->command_count) {
        size_t end=begin;
        while(end<fill->command_count && fill->commands[end].kind!=QUANTAPDF_COMPOSER_PATH_CLOSE) ++end;
        CHECK(end<fill->command_count);
        if(end-begin>4u) {
            quantapdf_point center={(float)(arcs==0u ? 8 : 16),16};
            size_t ci=begin,vertices=end-begin;
            while(ci<end && (fill->commands[ci].point1.x!=center.x || fill->commands[ci].point1.y!=center.y)) ++ci;
            CHECK(ci<end); ++arcs;
            /* Polygon winding normalization may put the known center first
             * or last. Traverse its arc boundary without assuming ordering. */
            for(size_t i=1u;i+1u<vertices;++i) {
                quantapdf_point a=fill->commands[begin+(ci-begin+i)%vertices].point1;
                quantapdf_point b=fill->commands[begin+(ci-begin+i+1u)%vertices].point1;
                double mx=((double)a.x+b.x)/2.0-center.x,my=((double)a.y+b.y)/2.0-center.y;
                CHECK(fabs(hypot((double)a.x-center.x,(double)a.y-center.y)-2.0)*scale<0.00001);
                CHECK((2.0-hypot(mx,my))*scale<=1.0/64.0);
            }
        }
        begin=end+1u;
    }
    CHECK(arcs==2u);
}
static void test_round(void)
{
    quantapdf_ir_raster_image image={0};
    /* Independent 4x4 coverage probes distinguish a radius-2 circular cap
     * from both a butt edge and the square corner outside that circle. */
    dash_line(); ops[0].value.path.options.stroke_width=4;
    ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    image=render(); CHECK(alpha(&image,2,15)>0 && alpha(&image,2,14)<100 && alpha(&image,4,15)==255);
    CHECK(alpha(&image,29,15)>0 && alpha(&image,29,14)<100); quantapdf_ir_raster_image_dispose(&image);
    init(); ops[0].value.path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_ROUND;
    image=render(); CHECK(alpha(&image,17,17)>0 && alpha(&image,17,17)<100);
    CHECK(alpha(&image,16,16)==255); quantapdf_ir_raster_image_dispose(&image);
    /* A round join is only its outer sector. A full disk would spill past
     * the butt end of this short last segment (pixel 16,14). */
    init(); commands[2].point1.y=15.5f; ops[0].value.path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_ROUND;
    image=render(); CHECK(alpha(&image,16,14)==0 && alpha(&image,15,15)==255);
    quantapdf_ir_raster_image_dispose(&image);
    dash_line(); dash(4,6,0); ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    image=render(); CHECK(alpha(&image,3,15)>0 && alpha(&image,8,15)>0 && alpha(&image,10,15)==0);
    quantapdf_ir_raster_image_dispose(&image);
    /* Device scaling must increase arc detail as well as stroke width. */
    init(); ops[0].value.path.command_count=2;
    ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    quantapdf_composer_path_operation native={0},fitted={0}; quantapdf_native_device_plan plan={0};
    OK(quantapdf_native_device_plan_create("0 0 32 32","0 0 32 32","1",0,144,4096,&plan));
    OK(quantapdf_ir_stroke_polyline_outline_v1(&ops[0].value.path,4096,&native));
    OK(quantapdf_ir_stroke_polyline_outline_v1_planned(&ops[0].value.path,4096,&plan,&fitted));
    CHECK(fitted.command_count>native.command_count);
    check_round_chords(&native,1.0); check_round_chords(&fitted,2.0);
    quantapdf_ir_stroke_polyline_dispose_v1(&native); quantapdf_ir_stroke_polyline_dispose_v1(&fitted);
    quantapdf_ir_raster_v1_limits device_limits={4096,2,4096,64000000};
    OK(quantapdf_ir_raster_scoped_operations_v2_planned(&ir,0,&device_limits,&plan,&image));
    CHECK(alpha(&image,13,31)>0 && alpha(&image,12,28)<100); quantapdf_ir_raster_image_dispose(&image);
    /* Shear/reflection affect the bound; finite but excessive scale refuses
     * when 512 segments cannot satisfy device tolerance. */
    ops[0].value.path.options.transform=(quantapdf_affine_transform){-2,0,.5f,1,30,0}; image=render();
    quantapdf_ir_raster_image_dispose(&image);
    ops[0].value.path.options.transform=(quantapdf_affine_transform){1000000,0,0,1000000,0,0}; reject();
    init(); ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    commands[0].point1=(quantapdf_point){999990,16}; commands[1].point1=(quantapdf_point){999995,16};
    ops[0].value.path.command_count=2; reject(); /* float arc precision at large origin */
    init(); ops[0].value.path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_ROUND;
    commands[2].point1=commands[0].point1; reject();
    init(); ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    quantapdf_ir_raster_v1_limits tiny=limits; tiny.max_edges=8;
    CHECK(quantapdf_ir_raster_scoped_operations_v2(&ir,0,&tiny,&image)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(!image.pixels && !image.size);
    CHECK(quantapdf_ir_stroke_polyline_outline_v1_planned(&ops[0].value.path,512,NULL,&native)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(!native.commands && !native.command_count);
}

static const quantapdf_ir_raster_v1_limits curve_limits={4096,2,4096,200000000};
static void cubic_arch(void)
{
    init(); ops[0].value.path.command_count=2; ops[0].value.path.options.stroke_width=2;
    commands[0].point1=(quantapdf_point){4,16};
    commands[1].kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    commands[1].point1=(quantapdf_point){4,4}; commands[1].point2=(quantapdf_point){28,4};
    commands[1].point3=(quantapdf_point){28,16};
}
static double point_segment_distance(double x,double y,quantapdf_point a,quantapdf_point z)
{
    double dx=z.x-a.x,dy=z.y-a.y,den=dx*dx+dy*dy;
    double t=den==0.0 ? 0.0 : ((x-a.x)*dx+(y-a.y)*dy)/den;
    t=fmax(0.0,fmin(1.0,t)); return hypot(x-a.x-t*dx,y-a.y-t*dy);
}
/* Independent polynomial/differential samples on the two true offset
 * boundaries, compared with emitted float strip sides. No tessellator formula
 * or subdivision parameters are reused to decide the expected boundary. */
static void check_arch_offset(const quantapdf_composer_path_operation *fill,double scale)
{
    CHECK(fill->command_count%5u==0u);
    for(size_t k=0;k<2000u;++k) {
        double t=((double)k+.5)/2000.0;
        double x=4+72*t*t-48*t*t*t,y=16-36*t+36*t*t;
        double dx=144*t-144*t*t,dy=-36+72*t,n=hypot(dx,dy);
        for(int side=-1;side<=1;side+=2) {
            double qx=x-side*dy/n,qy=y+side*dx/n,best=1000000;
            for(size_t i=0;i<fill->command_count;i+=5u) {
                CHECK(fill->commands[i+4u].kind==QUANTAPDF_COMPOSER_PATH_CLOSE);
                best=fmin(best,point_segment_distance(qx,qy,fill->commands[i].point1,fill->commands[i+1u].point1));
                best=fmin(best,point_segment_distance(qx,qy,fill->commands[i+2u].point1,fill->commands[i+3u].point1));
            }
            CHECK(best*scale<1.0/32.0+1.0/128.0+0.000001);
        }
    }
}
static void test_zero_length_caps(void)
{
    quantapdf_ir_raster_image image={0};
    init(); ops[0].value.path.command_count=2;
    commands[1].point1=commands[0].point1;
    ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    image=render();
    for(size_t i=0;i<image.size;i+=4u) CHECK(image.pixels[i+3u]==0u);
    quantapdf_ir_raster_image_dispose(&image);

    init(); ops[0].value.path.command_count=2;
    commands[1].point1=commands[0].point1;
    ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    image=render();
    CHECK(alpha(&image,8,15)==255);
    CHECK(alpha(&image,9,15)>0 && alpha(&image,9,15)<255); /* circle boundary coverage */
    CHECK(alpha(&image,11,15)==0 && alpha(&image,8,18)==0);
    quantapdf_ir_raster_image_dispose(&image);

    init(); ops[0].value.path.command_count=2;
    commands[1].point1=commands[0].point1;
    ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_SQUARE;
    image=render();
    for(size_t i=0;i<image.size;i+=4u) CHECK(image.pixels[i+3u]==0u);
    quantapdf_ir_raster_image_dispose(&image);

    /* A degenerate open line subpath remains paintable when the same
     * operation also contains a cubic subpath. */
    init(); ops[0].value.path.command_count=4;
    ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_ROUND;
    ops[0].value.path.options.stroke_width=2;
    commands[0].point1=(quantapdf_point){2.5f,2.5f};
    commands[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[1].point1=(quantapdf_point){2.5f,2.5f};
    commands[2].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[2].point1=(quantapdf_point){4,16};
    commands[3].kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    commands[3].point1=(quantapdf_point){4,4};
    commands[3].point2=(quantapdf_point){28,4};
    commands[3].point3=(quantapdf_point){28,16};
    image=render_with(&curve_limits);
    CHECK(alpha(&image,2,2)==255 && alpha(&image,4,16)>0);
    CHECK(alpha(&image,0,0)==0);
    quantapdf_ir_raster_image_dispose(&image);

    init(); ops[0].value.path.command_count=2;
    commands[1].point1=commands[0].point1;
    dash(4,4,0); reject();
}

/* Inert MOVE subpaths are semantically empty, not malformed Stroke. */
static void test_inert_move_subpaths(void)
{
    quantapdf_ir_raster_image image={0},reference={0};
    /* No authored segment: success and exact transparent pixels. */
    init(); ops[0].value.path.command_count=2;
    commands[1].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[1].point1=(quantapdf_point){20,10};
    image=render();
    for(size_t i=0;i<image.size;i+=4u) CHECK(image.pixels[i+3u]==0u);
    quantapdf_ir_raster_image_dispose(&image);

    /* Dash/phase state cannot invent painted length on MOVE-only paths. */
    init(); ops[0].value.path.command_count=2;
    commands[1].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[1].point1=(quantapdf_point){20,10};
    dash(4,4,1);
    image=render();
    for(size_t i=0;i<image.size;i+=4u) CHECK(image.pixels[i+3u]==0u);
    quantapdf_ir_raster_image_dispose(&image);

    /* An inert MOVE prelude must not change a subsequent real line. */
    init(); commands[0].point1=(quantapdf_point){2,2};
    commands[1].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[1].point1=(quantapdf_point){8,16};
    commands[2].point1=(quantapdf_point){16,16};
    image=render();
    init(); ops[0].value.path.command_count=2;
    commands[1].point1=(quantapdf_point){16,16};
    reference=render();
    CHECK(image.size==reference.size &&
        memcmp(image.pixels,reference.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&image);
    quantapdf_ir_raster_image_dispose(&reference);

    /* Inert MOVE preceding a solid cubic must not fail the curved compiler. */
    init(); ops[0].value.path.command_count=3;
    ops[0].value.path.options.stroke_width=2;
    commands[0].point1=(quantapdf_point){2,2};
    commands[1].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[1].point1=(quantapdf_point){4,16};
    commands[2].kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    commands[2].point1=(quantapdf_point){4,4};
    commands[2].point2=(quantapdf_point){28,4};
    commands[2].point3=(quantapdf_point){28,16};
    image=render_with(&curve_limits);
    cubic_arch();
    reference=render_with(&curve_limits);
    CHECK(image.size==reference.size &&
        memcmp(image.pixels,reference.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&image);
    quantapdf_ir_raster_image_dispose(&reference);

    /* A close-only degenerate and late invalid geometry still fail atomically.
     * An inert MOVE must not bypass transform and expanded-edge validation. */
    init(); ops[0].value.path.command_count=2;
    commands[1].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
    reject();
    init(); commands[1].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[1].point1=(quantapdf_point){20,10};
    commands[2].point1.x=NAN; reject();
    init(); ops[0].value.path.command_count=2;
    commands[1].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[1].point1=(quantapdf_point){20,10};
    ops[0].value.path.options.transform.a=NAN; reject();
    init(); ops[0].value.path.command_count=2;
    commands[1].kind=QUANTAPDF_COMPOSER_PATH_MOVE_TO;
    commands[1].point1=(quantapdf_point){20,10};
    quantapdf_ir_raster_v1_limits bounded=limits; bounded.max_edges=1;
    CHECK(quantapdf_ir_raster_scoped_operations_v2(&ir,0,&bounded,&image)
        ==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(!image.pixels && !image.size);
}

static void test_cubics(void)
{
    quantapdf_ir_raster_image image={0},reference={0};
    quantapdf_composer_path_operation fill={0},fit={0},failed={0};
    quantapdf_native_device_plan plan={0};
    cubic_arch(); image=render_with(&curve_limits);
    CHECK(alpha(&image,16,7)==255 && alpha(&image,16,10)==0 && alpha(&image,4,16)==0);
    /* A smooth primitive's artificial vertices must ignore authored join
     * style. Only authored segment boundaries receive joins. */
    ops[0].value.path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL;
    reference=render_with(&curve_limits); CHECK(memcmp(image.pixels,reference.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&reference);
    ops[0].value.path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_ROUND;
    reference=render_with(&curve_limits); CHECK(memcmp(image.pixels,reference.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&reference);
    /* Exact de Casteljau split at 1/2 leaves the geometric path unchanged. */
    commands[1].point1=(quantapdf_point){4,10}; commands[1].point2=(quantapdf_point){10,7}; commands[1].point3=(quantapdf_point){16,7};
    commands[2]=commands[1]; commands[2].point1=(quantapdf_point){22,7}; commands[2].point2=(quantapdf_point){28,10}; commands[2].point3=(quantapdf_point){28,16};
    ops[0].value.path.command_count=3;
    reference=render_with(&curve_limits); CHECK(memcmp(image.pixels,reference.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&image); quantapdf_ir_raster_image_dispose(&reference);
    cubic_arch();
    OK(quantapdf_ir_stroke_polyline_outline_v1(&ops[0].value.path,4096,&fill));
    CHECK(fill.commands[0].point1.y==16 && fill.commands[3].point1.y==16);
    CHECK(fill.commands[0].point1.x==3 || fill.commands[0].point1.x==5);
    check_arch_offset(&fill,1.0);
    OK(quantapdf_native_device_plan_create("0 0 32 32","0 0 32 32","1",0,144,4096,&plan));
    OK(quantapdf_ir_stroke_polyline_outline_v1_planned(&ops[0].value.path,4096,&plan,&fit));
    CHECK(fit.command_count>fill.command_count); check_arch_offset(&fit,2.0);
    OK(quantapdf_ir_raster_scoped_operations_v2_planned(&ir,0,&curve_limits,&plan,&image));
    CHECK(image.width==64 && alpha(&image,32,14)==255); quantapdf_ir_raster_image_dispose(&image);
    size_t edges=fill.command_count/5u*4u;
    OK(quantapdf_ir_stroke_polyline_outline_v1(&ops[0].value.path,edges,&failed)); quantapdf_ir_stroke_polyline_dispose_v1(&failed);
    memset(&failed,0x5a,sizeof(failed));
    CHECK(quantapdf_ir_stroke_polyline_outline_v1(&ops[0].value.path,edges-1u,&failed)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(!failed.commands && !failed.command_count);
    quantapdf_ir_stroke_polyline_dispose_v1(&fill); quantapdf_ir_stroke_polyline_dispose_v1(&fit);
    /* Caps use the true endpoint tangents, including coincident endpoint
     * controls from PDF v/y shorthand. */
    cubic_arch(); ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_SQUARE;
    image=render_with(&curve_limits); CHECK(alpha(&image,4,16)==255 && alpha(&image,28,16)==255);
    quantapdf_ir_raster_image_dispose(&image);
    commands[1].point1=commands[0].point1; image=render_with(&curve_limits); quantapdf_ir_raster_image_dispose(&image);
    cubic_arch(); commands[1].point2=commands[1].point3; image=render_with(&curve_limits); quantapdf_ir_raster_image_dispose(&image);
    /* Nonuniform authored transforms apply after the complete outline. */
    cubic_arch(); ops[0].value.path.options.transform=(quantapdf_affine_transform){-.7f,.2f,.3f,1,24,0};
    image=render_with(&curve_limits); quantapdf_ir_raster_image_dispose(&image);
    /* Explicit closure adds its authored closing line, and ignores all
     * unused CLOSE point payload. Repeated contours remain a nonzero union. */
    cubic_arch(); commands[2].kind=QUANTAPDF_COMPOSER_PATH_CLOSE;
    commands[2].point1=commands[2].point2=commands[2].point3=(quantapdf_point){NAN,NAN};
    ops[0].value.path.command_count=3; image=render_with(&curve_limits);
    CHECK(alpha(&image,16,16)==255 && alpha(&image,16,12)==0);
    quantapdf_ir_raster_image_dispose(&image);
    cubic_arch(); image=render_with(&curve_limits);
    commands[2]=commands[0]; commands[3]=commands[1]; ops[0].value.path.command_count=4;
    reference=render_with(&curve_limits); CHECK(memcmp(image.pixels,reference.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&image); quantapdf_ir_raster_image_dispose(&reference);
    /* Monotone collinear cubic equals a line despite nonuniform speed. */
    cubic_arch(); commands[1].point1=commands[0].point1; commands[1].point2=commands[1].point3;
    image=render_with(&curve_limits); commands[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO; commands[1].point1=commands[1].point3;
    reference=render_with(&curve_limits); CHECK(memcmp(image.pixels,reference.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&image); quantapdf_ir_raster_image_dispose(&reference);
    /* Exact monotone collinear cubics have line-equivalent arc length, so
     * dashed rendering must match the endpoint segment byte-for-byte. */
    dash_line();
    commands[1].kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    commands[1].point1=(quantapdf_point){8,16};
    commands[1].point2=(quantapdf_point){20,16};
    commands[1].point3=(quantapdf_point){28,16};
    dash(4,4,1.5f);
    image=render_with(&curve_limits);
    commands[1].kind=QUANTAPDF_COMPOSER_PATH_LINE_TO;
    commands[1].point1=commands[1].point3;
    reference=render_with(&curve_limits);
    CHECK(image.size==reference.size && memcmp(image.pixels,reference.pixels,image.size)==0);
    quantapdf_ir_raster_image_dispose(&image);
    quantapdf_ir_raster_image_dispose(&reference);
    /* A collinear but reversing cubic is not a line trace and remains refused. */
    dash_line();
    commands[1].kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO;
    commands[1].point1=(quantapdf_point){28,16};
    commands[1].point2=(quantapdf_point){4,16};
    commands[1].point3=(quantapdf_point){28,16};
    dash(4,4,0); reject();
    /* Curved dash arc-length, interior stationary/cusp, precision and late
     * layer failures are explicit, atomic refusals. */
    cubic_arch(); dash(4,4,0); reject();
    cubic_arch(); commands[1].point1=(quantapdf_point){28,16}; commands[1].point2=(quantapdf_point){4,16}; reject();
    cubic_arch(); commands[1].point2.x=NAN; reject();
    cubic_arch(); commands[1].point3.y=1000001; reject();
    cubic_arch(); ops[0].value.path.options.transform=(quantapdf_affine_transform){1000000,0,0,1000000,0,0}; reject();
    cubic_arch();
    commands[0].point1.x+=999960; commands[1].point1.x+=999960;
    commands[1].point2.x+=999960; commands[1].point3.x+=999960;
    ops[0].value.path.options.transform.e=-999960; reject(); /* user-space float precision */
    cubic_arch(); ops[0].value.path.options.transform=(quantapdf_affine_transform){1000,0,0,1000,0,0};
    CHECK(quantapdf_ir_stroke_polyline_outline_v1(&ops[0].value.path,10000,&failed)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(!failed.commands && !failed.command_count);
    cubic_arch(); ops[1]=ops[0]; ops[1].value.path.options.stroke_width=0; ir.operation_count=2; reject();
    cubic_arch(); OK(quantapdf_ir_stroke_polyline_outline_v1(&ops[0].value.path,4096,&fill));
    memset(commands,0,sizeof(commands)); memset(ops,0,sizeof(ops));
    ops[0].kind=QUANTAPDF_COMPOSER_OPERATION_PATH; ops[0].value.path=fill;
    OK(quantapdf_ir_raster_render_v1(&ir,0,&curve_limits,&image)); CHECK(alpha(&image,16,7)==255);
    quantapdf_ir_raster_image_dispose(&image); quantapdf_ir_stroke_polyline_dispose_v1(&fill);
}
int main(void)
{
    quantapdf_ir_raster_image image={0};
    init(); image=render();
    CHECK(alpha(&image,17,17)==255 && alpha(&image,7,15)==0 && alpha(&image,15,6)==0);
    CHECK(alpha(&image,10,13)==0); quantapdf_ir_raster_image_dispose(&image);
    ops[0].value.path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_BEVEL;
    image=render(); CHECK(alpha(&image,17,17)==0 && alpha(&image,16,16)==255);
    quantapdf_ir_raster_image_dispose(&image);
    init(); ops[0].value.path.options.miter_limit=1;
    image=render(); CHECK(alpha(&image,17,17)==0); quantapdf_ir_raster_image_dispose(&image);
    init(); ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_SQUARE;
    image=render(); CHECK(alpha(&image,6,15)==255 && alpha(&image,15,6)==255);
    quantapdf_ir_raster_image_dispose(&image);
    init(); ops[0].value.path.options.stroke_width=2; commands[3].kind=QUANTAPDF_COMPOSER_PATH_CLOSE; ops[0].value.path.command_count=4;
    image=render(); CHECK(alpha(&image,10,13)>0 && alpha(&image,13,13)==0);
    quantapdf_ir_raster_image_dispose(&image);
    /* Duplicated overlapping contours are a union, never even-odd cancellation. */
    init(); commands[3]=commands[0]; commands[4]=commands[1]; commands[5]=commands[2];
    ops[0].value.path.command_count=6; ops[0].value.path.options.fill_rule=QUANTAPDF_COMPOSER_FILL_EVEN_ODD;
    image=render(); CHECK(alpha(&image,12,15)==255); quantapdf_ir_raster_image_dispose(&image);
    /* Width is expanded BEFORE nonuniform scale/shear/reflection. */
    init(); ops[0].value.path.command_count=2;
    ops[0].value.path.options.transform=(quantapdf_affine_transform){-1,0,0.5f,0.5f,20,0};
    image=render(); CHECK(alpha(&image,15,7)==255 && alpha(&image,15,10)==0);
    quantapdf_ir_raster_image_dispose(&image);
    /* Detached outline retains neither options nor commands from the source. */
    init(); quantapdf_composer_path_operation fill={0};
    OK(quantapdf_ir_stroke_polyline_outline_v1(&ops[0].value.path,512,&fill));
    memset(commands,0,sizeof(commands)); CHECK(fill.command_count>0 && fill.commands[0].point1.x!=0);
    quantapdf_ir_stroke_polyline_dispose_v1(&fill); CHECK(!fill.commands && !fill.command_count);
    init(); ops[0].value.path.options.stroke_width=0; reject();
    init(); ops[0].value.path.options.stroke_width=NAN; reject();
    init(); ops[0].value.path.options.line_cap=(quantapdf_composer_line_cap)99; reject();
    init(); ops[0].value.path.options.line_join=(quantapdf_composer_line_join)99; reject();
    init(); ops[0].value.path.dash_count=1; reject();
    init(); ops[0].value.path.dash_phase=1; reject();
    init(); ops[0].value.path.options.stroke_argb=0x80ff0000u; reject();
    init(); commands[2].kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO; dash(4,4,0); reject();
    init(); commands[2].point1=commands[0].point1; reject(); /* reversing cusp */
    /* Repeated zero-length LINE_TO commands are a single degenerate open
     * subpath: Butt caps succeed with exact empty coverage. */
    init(); ops[0].value.path.command_count=3;
    commands[1].point1=commands[0].point1;
    commands[2].point1=commands[0].point1;
    image=render();
    for(size_t i=0;i<image.size;i+=4u) CHECK(image.pixels[i+3u]==0u);
    quantapdf_ir_raster_image_dispose(&image);
    init(); commands[2].point1.x=1000001; reject();
    init(); ops[0].value.path.options.stroke_paint_id=1; reject();
    /* A late unsupported layer must discard all previously sampled pixels. */
    init(); ops[1]=ops[0]; ops[1].value.path.options.stroke_width=0; ir.operation_count=2; reject();
    /* FIT device scaling must also happen after user-space stroke expansion. */
    init(); quantapdf_native_device_plan plan={0};
    OK(quantapdf_native_device_plan_create("0 0 32 32","0 0 32 32","1",0,144,4096,&plan));
    quantapdf_ir_raster_v1_limits fitted={4096,2,512,64000000};
    OK(quantapdf_ir_raster_scoped_operations_v2_planned(&ir,0,&fitted,&plan,&image));
    CHECK(image.width==64 && image.height==64 && alpha(&image,35,35)==255);
    CHECK(alpha(&image,15,30)==0); quantapdf_ir_raster_image_dispose(&image);
    plan.from_composer[0]=1;
    CHECK(quantapdf_ir_raster_scoped_operations_v2_planned(&ir,0,&fitted,&plan,&image)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(!image.pixels && !image.size);
    init(); ops[0].value.path.command_count=1025; reject();
    init(); quantapdf_ir_raster_v1_limits small=limits; small.max_edges=4;
    CHECK(quantapdf_ir_raster_scoped_operations_v2(&ir,0,&small,&image)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(!image.pixels && !image.size);
    small=limits; small.max_sample_tests=16;
    CHECK(quantapdf_ir_raster_scoped_operations_v2(&ir,0,&small,&image)==QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(!image.pixels && !image.size);
    CHECK(quantapdf_ir_raster_scoped_operations_v2_planned(&ir,0,&limits,NULL,&image)==QUANTAPDF_ERROR_ARGUMENT);
    CHECK(!image.pixels && !image.size);
    test_dashes();
    test_round();
    test_zero_dashes();
    test_zero_length_caps();
    test_inert_move_subpaths();
    test_cubics();
    return 0;
}
