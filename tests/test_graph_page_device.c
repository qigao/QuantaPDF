#include "graph/graph_page_device.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/* Unlike CHECK(), this verifier executes with Release -DNDEBUG too. */
static void check_true(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "page-device check failed at line %d: %s\\n",
            line, expression);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(expr) check_true(!!(expr), #expr, __LINE__)

static quantapdf_graph_page_geometry page(double l, double b, double r,
    double t, double unit, int rotation)
{
    quantapdf_graph_page_geometry g = {0};
    g.media_box[0] = 0; g.media_box[1] = 0;
    g.media_box[2] = 1000; g.media_box[3] = 1000;
    g.crop_box[0] = l; g.crop_box[1] = b;
    g.crop_box[2] = r; g.crop_box[3] = t;
    g.user_unit = unit; g.rotation = rotation;
    return g;
}
static int close_to(double a, double b)
{
    return fabs(a-b) <= 1e-9;
}
static void mapped(const quantapdf_graph_device_grid *d, double x,
    double y, double *ox, double *oy)
{
    *ox = d->from_source[0]*x+d->from_source[2]*y+d->from_source[4];
    *oy = d->from_source[1]*x+d->from_source[3]*y+d->from_source[5];
}
static void accepted(quantapdf_graph_page_geometry g, unsigned dpi,
    uint32_t w, uint32_t h)
{
    quantapdf_graph_device_grid a, b;
    CHECK(quantapdf_graph_page_device_grid(&g,dpi,10000000u,
        30000000u,&a) == QUANTAPDF_OK);
    CHECK(a.width==w && a.height==h);
    CHECK(a.stride_bytes==(size_t)w*3u);
    CHECK(a.output_bytes==(size_t)w*h*3u);
    CHECK(quantapdf_graph_page_device_grid(&g,dpi,10000000u,
        30000000u,&b)==QUANTAPDF_OK);
    CHECK(memcmp(&a,&b,sizeof(a))==0);
}
static void refused(quantapdf_graph_page_geometry g, unsigned dpi,
    size_t pixels, size_t bytes, quantapdf_status expected)
{
    quantapdf_graph_device_grid out;
    const quantapdf_graph_device_grid zero = {0};
    memset(&out,0xA5,sizeof(out));
    CHECK(quantapdf_graph_page_device_grid(&g,dpi,pixels,bytes,
        &out)==expected);
    CHECK(memcmp(&out,&zero,sizeof(out))==0);
}
int main(void)
{
    const unsigned dpis[] = {72u,96u,144u};
    const double scales[] = {1.0,4.0/3.0,2.0};
    for (size_t n=0;n<3;n++) {
        double s=scales[n], x,y;
        quantapdf_graph_device_grid d;
        quantapdf_graph_page_geometry g=page(20,30,120,80,1,0);
        accepted(g,dpis[n],(uint32_t)ceil(100*s),(uint32_t)ceil(50*s));
        CHECK(quantapdf_graph_page_device_grid(&g,dpis[n],
            10000000u,30000000u,&d)==QUANTAPDF_OK);
        mapped(&d,20,80,&x,&y);
        CHECK(close_to(x,0)&&close_to(y,0));
        mapped(&d,120,30,&x,&y);
        CHECK(close_to(x,(double)d.width)&&close_to(y,(double)d.height));
        for (int rotation=90;rotation<=270;rotation+=90) {
            g.rotation=rotation;
            accepted(g,dpis[n],
                (uint32_t)ceil((rotation==180?100:50)*s),
                (uint32_t)ceil((rotation==180?50:100)*s));
            CHECK(quantapdf_graph_page_device_grid(&g,dpis[n],
                10000000u,30000000u,&d)==QUANTAPDF_OK);
            mapped(&d,20,30,&x,&y);
            if (rotation==90) {
                CHECK(close_to(x,0)&&close_to(y,0));
                mapped(&d,120,80,&x,&y);
                CHECK(close_to(x,(double)d.width)&&close_to(y,(double)d.height));
            } else if (rotation==180) {
                CHECK(close_to(x,(double)d.width)&&close_to(y,0));
                mapped(&d,120,30,&x,&y);
                CHECK(close_to(x,0)&&close_to(y,(double)d.height));
            } else {
                CHECK(close_to(x,(double)d.width)&&close_to(y,(double)d.height));
                mapped(&d,120,80,&x,&y);
                CHECK(close_to(x,0)&&close_to(y,0));
            }
        }
        for (int k=0;k<2;k++) {
            g=page(20,30,120,80,k==0?0.5:2.0,0);
            accepted(g,dpis[n],
                (uint32_t)ceil(100*g.user_unit*s),
                (uint32_t)ceil(50*g.user_unit*s));
        }
    }
    {
        quantapdf_graph_page_geometry g=page(0,0,595.28,841.89,1,0);
        {
            const unsigned dpi_values[] = {72u,96u,144u};
            const uint32_t widths[] = {596u,794u,1191u};
            const uint32_t heights[] = {842u,1123u,1684u};
            for (size_t k=0;k<3;k++) {
                quantapdf_graph_device_grid d;
                double x,y,nominal_scale=(double)dpi_values[k]/72.0;
                accepted(g,dpi_values[k],widths[k],heights[k]);
                CHECK(quantapdf_graph_page_device_grid(&g,dpi_values[k],
                    10000000u,30000000u,&d)==QUANTAPDF_OK);
                mapped(&d,0,841.89,&x,&y);
                CHECK(close_to(x,0)&&close_to(y,0));
                mapped(&d,595.28,0,&x,&y);
                CHECK(close_to(x,(double)widths[k]) &&
                    close_to(y,(double)heights[k]));
                mapped(&d,595.28/2.0,841.89/2.0,&x,&y);
                CHECK(close_to(x,(double)widths[k]/2.0) &&
                    close_to(y,(double)heights[k]/2.0));
                /* Fractional bitmap-fit phases are anisotropic and must
                 * differ from nominal DPI on every tested platform. */
                CHECK(d.from_source[0]>nominal_scale);
                CHECK(-d.from_source[3]>nominal_scale);
                CHECK(!close_to(d.from_source[0],-d.from_source[3]));
            }
        }
        refused(g,120,10000000u,30000000u,QUANTAPDF_ERROR_UNSUPPORTED);
        refused(g,72,100u,30000000u,QUANTAPDF_ERROR_UNSUPPORTED);
        refused(g,72,10000000u,100u,QUANTAPDF_ERROR_UNSUPPORTED);
        refused(g,72,0,30000000u,QUANTAPDF_ERROR_ARGUMENT);
        refused(g,72,10000000u,0,QUANTAPDF_ERROR_ARGUMENT);
        g.user_unit=1e300;
        refused(g,72,10000000u,30000000u,QUANTAPDF_ERROR_UNSUPPORTED);
        g=page(0,0,595.28,841.89,1,0);
        g.crop_box[2]=1001;
        refused(g,72,10000000u,30000000u,QUANTAPDF_ERROR_UNSUPPORTED);
        g=page(0,0,595.28,841.89,1,0);
        g.user_unit=NAN;
        refused(g,72,10000000u,30000000u,QUANTAPDF_ERROR_ARGUMENT);
        g.user_unit=INFINITY;
        refused(g,72,10000000u,30000000u,QUANTAPDF_ERROR_ARGUMENT);
    }
    return 0;
}
