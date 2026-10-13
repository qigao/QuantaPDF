#include "render/native_device_grid.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "native grid: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    exit(EXIT_FAILURE); \
} } while (0)
#define OK(expr) CHECK((expr) == QUANTAPDF_NATIVE_DEVICE_GRID_OK)
#define BAD(expr) CHECK((expr) == QUANTAPDF_NATIVE_DEVICE_GRID_UNSUPPORTED)

static const quantapdf_native_device_grid_geometry js_pdf = {
    595.28, 841.89, 0.0, 0.0, 1.0, 0, 0, 72.0f
};
static const quantapdf_native_device_grid_limits sufficient = {
    600u * 850u * 4u, 600u * 850u * 16u, 768u, 70000000u
};
static void reset(const void *p, size_t n)
{
    const unsigned char *bytes = (const unsigned char *)p;
    for (size_t i = 0u; i < n; ++i) CHECK(bytes[i] == 0u);
}
static void check_extents_and_samples(void)
{
    static const struct {float dpi; size_t width, height;} cases[] = {
        {72.0f, 596u, 842u}, {96.0f, 794u, 1123u},
        {144.0f, 1191u, 1684u}
    };
    for (size_t i = 0u; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        quantapdf_native_device_grid grid, copy;
        quantapdf_native_device_grid_geometry g = js_pdf;
        double x, y;
        g.dpi = cases[i].dpi;
        memset(&grid, 0xa5, sizeof(grid));
        OK(quantapdf_native_device_grid_create_v0(&g, &sufficient, &grid));
        CHECK(grid.width == cases[i].width && grid.height == cases[i].height);
        CHECK(grid.stride == grid.width * 4u);
        CHECK(grid.rgba_bytes == grid.width * grid.height * 4u);
        CHECK(grid.sample_slots == grid.width * grid.height * 16u);
        CHECK(fabs(grid.points_per_device_x - g.page_width_points / (double)grid.width) < 1e-14);
        CHECK(fabs(grid.points_per_device_y - g.page_height_points / (double)grid.height) < 1e-14);
        copy = grid;
        OK(quantapdf_native_device_grid_sample_v0(&copy,0u,0u,0u,0u,&x,&y));
        CHECK(fabs(x - g.page_width_points / ((double)grid.width * 8.0)) < 1e-13);
        CHECK(fabs(y - g.page_height_points / ((double)grid.height * 8.0)) < 1e-13);
        OK(quantapdf_native_device_grid_sample_v0(&copy,grid.width-1u,
            grid.height-1u,3u,3u,&x,&y));
        CHECK(x > 0.0 && x < g.page_width_points);
        CHECK(y > 0.0 && y < g.page_height_points);
        CHECK(fabs(x - ((double)grid.width - 0.125) *
            grid.points_per_device_x) < 1e-11);
        OK(quantapdf_native_device_grid_check_ir_v0(&grid,
            (float)g.page_width_points,(float)g.page_height_points));
        for (int repeat = 0; repeat < 20; ++repeat) {
            quantapdf_native_device_grid again;
            OK(quantapdf_native_device_grid_create_v0(&g, &sufficient, &again));
            CHECK(memcmp(&grid, &again, sizeof(grid)) == 0);
        }
    }
}
static void check_work_and_limits(void)
{
    quantapdf_native_device_grid grid;
    quantapdf_native_device_grid_limits lim = sufficient;
    const size_t p = 596u * 842u;
    OK(quantapdf_native_device_grid_create_v0(&js_pdf, &lim, &grid));
    OK(quantapdf_native_device_grid_admit_edges_v0(&grid, &lim, 2u));
    lim.max_sample_tests = grid.sample_slots * 2u;
    OK(quantapdf_native_device_grid_admit_edges_v0(&grid, &lim, 2u));
    BAD(quantapdf_native_device_grid_admit_edges_v0(&grid, &lim, 3u));
    lim.max_edges = 1u;
    BAD(quantapdf_native_device_grid_admit_edges_v0(&grid, &lim, 2u));
    lim = sufficient;
    lim.max_pixels = p-1u;
    memset(&grid,0xa5,sizeof(grid));
    BAD(quantapdf_native_device_grid_create_v0(&js_pdf,&lim,&grid));
    reset(&grid,sizeof(grid));
    lim=sufficient; lim.max_rgba_bytes=p*4u-1u;
    memset(&grid,0xa5,sizeof(grid));
    BAD(quantapdf_native_device_grid_create_v0(&js_pdf,&lim,&grid));
    reset(&grid,sizeof(grid));
    lim=sufficient; lim.max_sample_tests=p*16u-1u;
    memset(&grid,0xa5,sizeof(grid));
    BAD(quantapdf_native_device_grid_create_v0(&js_pdf,&lim,&grid));
    reset(&grid,sizeof(grid));
    lim=sufficient; lim.max_pixels=0u;
    memset(&grid,0xa5,sizeof(grid));
    CHECK(quantapdf_native_device_grid_create_v0(&js_pdf,&lim,&grid) ==
        QUANTAPDF_NATIVE_DEVICE_GRID_ARGUMENT);
    reset(&grid,sizeof(grid));
}
static void check_refusal_and_precision(void)
{
    quantapdf_native_device_grid grid;
    quantapdf_native_device_grid_geometry g;
    static const float invalid_dpi[] = {0.0f, 73.0f, INFINITY, NAN};
    for(size_t i=0;i<sizeof(invalid_dpi)/sizeof(invalid_dpi[0]);++i){
        g=js_pdf;g.dpi=invalid_dpi[i];
        memset(&grid,0xa5,sizeof(grid));
        BAD(quantapdf_native_device_grid_create_v0(&g,&sufficient,&grid));
        reset(&grid,sizeof(grid));
    }
    for(int n=0;n<10;++n){
        g=js_pdf;
        switch(n){
        case 0:g.visible_origin_x_points=-0.25;break;
        case 1:g.visible_origin_y_points=0.5;break;
        case 2:g.user_unit=2.0;break;
        case 3:g.intrinsic_rotation_quarters=1;break;
        case 4:g.requested_rotation_quarters=3;break;
        case 5:g.page_width_points=NAN;break;
        case 6:g.page_height_points=INFINITY;break;
        case 7:g.page_width_points=-1.0;break;
        case 8:g.page_width_points=1e9;break;
        default:g.page_height_points=0;break;
        }
        memset(&grid,0xa5,sizeof(grid));
        BAD(quantapdf_native_device_grid_create_v0(&g,&sufficient,&grid));
        reset(&grid,sizeof(grid));
    }
    g=js_pdf;
    g.page_width_points=595.000001;
    OK(quantapdf_native_device_grid_create_v0(&g,&sufficient,&grid));
    CHECK(grid.width==596u);
    BAD(quantapdf_native_device_grid_check_ir_v0(&grid,(float)g.page_width_points,1.0f));
    g=js_pdf;
    OK(quantapdf_native_device_grid_create_v0(&g,&sufficient,&grid));
    BAD(quantapdf_native_device_grid_check_ir_v0(&grid,NAN,841.89f));
    BAD(quantapdf_native_device_grid_check_ir_v0(&grid,595.3f,841.89f));
    {
        double x=123.,y=456.;
        BAD(quantapdf_native_device_grid_sample_v0(&grid,grid.width,0u,0u,0u,&x,&y));
        CHECK(x==0.0&&y==0.0);
        BAD(quantapdf_native_device_grid_sample_v0(&grid,0u,grid.height,0u,0u,&x,&y));
        BAD(quantapdf_native_device_grid_sample_v0(&grid,0u,0u,4u,0u,&x,&y));
        CHECK(quantapdf_native_device_grid_sample_v0(&grid,0u,0u,0u,0u,&x,&x)==
            QUANTAPDF_NATIVE_DEVICE_GRID_ARGUMENT);
        CHECK(x==0.0);
    }
}
int main(void)
{
    check_extents_and_samples();
    check_work_and_limits();
    check_refusal_and_precision();
    puts("native-device-grid: pure C11 bounded geometry PASS; no raster output");
    return EXIT_SUCCESS;
}
