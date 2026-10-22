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
static quantapdf_ir_raster_image render(void)
{
    quantapdf_ir_raster_image image={0};
    quantapdf_composer_operation original=ops[0];
    quantapdf_composer_path_command copy[8]; memcpy(copy,commands,sizeof(copy));
    OK(quantapdf_ir_raster_scoped_operations_v2(&ir,0,&limits,&image));
    CHECK(memcmp(copy,commands,sizeof(copy))==0 && memcmp(&original,ops,sizeof(original))==0);
    return image;
}
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
    dash_line(); dash(0,2,0); reject(); dash(-1,2,0); reject(); dash(NAN,2,0); reject(); dash(INFINITY,2,0); reject();
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
    init(); ops[0].value.path.options.line_cap=QUANTAPDF_COMPOSER_LINE_CAP_ROUND; reject();
    init(); ops[0].value.path.options.line_join=QUANTAPDF_COMPOSER_LINE_JOIN_ROUND; reject();
    init(); ops[0].value.path.dash_count=1; reject();
    init(); ops[0].value.path.dash_phase=1; reject();
    init(); ops[0].value.path.options.stroke_argb=0x80ff0000u; reject();
    init(); commands[2].kind=QUANTAPDF_COMPOSER_PATH_CUBIC_TO; reject();
    init(); commands[2].point1=commands[0].point1; reject();
    init(); commands[1].point1=commands[0].point1; commands[2].point1=commands[0].point1; reject();
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
    return 0;
}
