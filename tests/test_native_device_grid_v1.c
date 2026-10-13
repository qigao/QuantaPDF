#include "render/native_device_grid_v1.h"
#include <stdlib.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
    exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_NATIVE_GRID_OK)
#define NO(x) CHECK((x) == QUANTAPDF_NATIVE_GRID_UNSUPPORTED)

static void zeroed(const quantapdf_native_device_grid_v1 *g)
{
    const unsigned char *p = (const unsigned char *)g;
    for (size_t i = 0; i < sizeof(*g); ++i) CHECK(p[i] == 0u);
}

int main(void)
{
    const struct {unsigned dpi; size_t w,h;} cases[] = {
        {72,596,842}, {96,794,1123}, {144,1191,1684}
    };
    quantapdf_native_grid_request r = {
        (double)(float)595.28, (double)(float)841.89,
        0.0,0.0,1.0,72u,0u,0u,QUANTAPDF_NATIVE_GRID_SOURCE_F32
    };
    quantapdf_native_grid_budget budget = {
        2200u*1700u, 4u*2200u*1700u, 16u, 70000000u, 2u
    };
    quantapdf_native_device_grid_v1 g;
    double x,y;
    for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        r.dpi=cases[i].dpi;
        for(int repeat=0;repeat<5;++repeat){
            memset(&g,0xa5,sizeof(g));
            OK(quantapdf_native_device_grid_v1_create(&r,&budget,&g));
            CHECK(g.width==cases[i].w && g.height==cases[i].h);
            CHECK(g.stride==g.width*4u && g.rgba_bytes==g.pixel_count*4u);
            CHECK(g.sample_edge_tests==g.pixel_count*32u);
            CHECK(g.points_per_device_x * g.device_per_point_x > 0.999999999);
            OK(quantapdf_native_device_grid_v1_sample_point(&g,0,0,0,0,&x,&y));
            CHECK(fabs(x*g.device_per_point_x-0.125)<1e-10);
            CHECK(fabs(y*g.device_per_point_y-0.125)<1e-10);
            OK(quantapdf_native_device_grid_v1_sample_point(&g,g.width-1,g.height-1,3,3,&x,&y));
            CHECK(x<g.width_points && y<g.height_points);
            CHECK(x>0 && y>0);
        }
    }
    r.dpi=72u;
#define REFUSE(expr) do{ memset(&g,0xa5,sizeof(g)); NO(expr);zeroed(&g);}while(0)
    REFUSE((r.display_x0=0.5,quantapdf_native_device_grid_v1_create(&r,&budget,&g)));
    r.display_x0=0.0;
    REFUSE((r.user_unit=2.0,quantapdf_native_device_grid_v1_create(&r,&budget,&g)));
    r.user_unit=1.0;
    REFUSE((r.intrinsic_rotation=90u,quantapdf_native_device_grid_v1_create(&r,&budget,&g)));
    r.intrinsic_rotation=0u;
    REFUSE((r.requested_rotation=90u,quantapdf_native_device_grid_v1_create(&r,&budget,&g)));
    r.requested_rotation=0u;
    REFUSE((r.dpi=75u,quantapdf_native_device_grid_v1_create(&r,&budget,&g)));
    r.dpi=72u;
    REFUSE((r.width_points=NAN,quantapdf_native_device_grid_v1_create(&r,&budget,&g)));
    r.width_points=(double)(float)595.28;
    REFUSE((r.height_points=INFINITY,quantapdf_native_device_grid_v1_create(&r,&budget,&g)));
    r.height_points=(double)(float)841.89;
    REFUSE((r.width_points=1e100,quantapdf_native_device_grid_v1_create(&r,&budget,&g)));
    r.width_points=(double)(float)595.28;
    REFUSE((budget.max_pixels=100u,quantapdf_native_device_grid_v1_create(&r,&budget,&g)));
    budget.max_pixels=2200u*1700u;
    REFUSE((budget.max_rgba_bytes=100u,quantapdf_native_device_grid_v1_create(&r,&budget,&g)));
    budget.max_rgba_bytes=4u*2200u*1700u;
    REFUSE((budget.max_edges=1u,quantapdf_native_device_grid_v1_create(&r,&budget,&g)));
    budget.max_edges=16u;
    REFUSE((budget.max_sample_tests=100u,quantapdf_native_device_grid_v1_create(&r,&budget,&g)));
    budget.max_sample_tests=70000000u;
    REFUSE((r.width_points=(double)(float)595.000001,quantapdf_native_device_grid_v1_create(&r,&budget,&g)));
    r.width_points=(double)(float)595.28;
    REFUSE((r.width_points=595.28,quantapdf_native_device_grid_v1_create(&r,&budget,&g)));
    r.width_points=595.28;
    r.precision=QUANTAPDF_NATIVE_GRID_SOURCE_EXACT_DOUBLE;
    OK(quantapdf_native_device_grid_v1_create(&r,&budget,&g));
    CHECK(g.width==596);
    r.width_points=(double)(float)595.28;
    r.precision=QUANTAPDF_NATIVE_GRID_SOURCE_F32;
    REFUSE((budget.admitted_edges=SIZE_MAX,quantapdf_native_device_grid_v1_create(&r,&budget,&g)));
    budget.admitted_edges=2u;
    OK(quantapdf_native_device_grid_v1_create(&r,&budget,&g));
    CHECK(quantapdf_native_device_grid_v1_sample_point(&g,0,0,4,0,&x,&y)==QUANTAPDF_NATIVE_GRID_UNSUPPORTED);
    CHECK(x==0.0 && y==0.0);
    CHECK(quantapdf_native_device_grid_v1_sample_point(&g,g.width,0,0,0,&x,&y)==QUANTAPDF_NATIVE_GRID_UNSUPPORTED);
    CHECK(x==0.0 && y==0.0);
    CHECK(quantapdf_native_device_grid_v1_create(NULL,&budget,&g)==QUANTAPDF_NATIVE_GRID_ARGUMENT);
    zeroed(&g);
    puts("native-device-grid: geometry+budget+precision+sample PASS");
    return 0;
}
