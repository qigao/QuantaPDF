#include "graph/graph_page_device_sample.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_check(int condition, const char *expr, int line)
{
    if (!condition) {
        fprintf(stderr,"source sampling conformance line %d: %s\n",line,expr);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) test_check(!!(x), #x, __LINE__)

static int near(double x, double y)
{
    return isfinite(x) && isfinite(y) && fabs(x-y) < 1e-8;
}
static quantapdf_graph_page_geometry geometry(int shifted,
    double unit, int rotation)
{
    quantapdf_graph_page_geometry g = {0};
    g.media_box[2]=1300.0;
    g.media_box[3]=1300.0;
    g.crop_box[0]=shifted ? 20.0 : 0.0;
    g.crop_box[1]=shifted ? 30.0 : 0.0;
    g.crop_box[2]=g.crop_box[0]+595.28;
    g.crop_box[3]=g.crop_box[1]+841.89;
    g.user_unit=unit;
    g.rotation=rotation;
    return g;
}
static void forward(const quantapdf_graph_device_grid *g,
    double x, double y, double *ox, double *oy)
{
    *ox=g->from_source[0]*x+g->from_source[2]*y+g->from_source[4];
    *oy=g->from_source[1]*x+g->from_source[3]*y+g->from_source[5];
}

/* #269/#343: oversized UserUnit=7 source geometry is inspired by a
 * PUBLIC REPORTER'S description, NOT by verified/licensed source PDF bytes.
 * No PDFium rendering, bitmap allocation or formal external-positive.
 * Exercise bounds before any work, on all right-angle page rotations. */
static void test_large_userunit7_preallocation_refusal(void)
{
    quantapdf_graph_page_geometry g={0};
    g.media_box[0]=10.0;
    g.media_box[1]=20.0;
    g.media_box[2]=14183.2;
    g.media_box[3]=360.562;
    memcpy(g.crop_box,g.media_box,sizeof(g.crop_box));
    g.user_unit=7.0;
    const unsigned dpis[]={72u,96u,144u};
    const quantapdf_graph_device_grid zero={0};
    size_t refused=0u;
    for(int rotation=0;rotation<360;rotation+=90){
        g.rotation=rotation;
        quantapdf_graph_page_display display={0};
        CHECK(quantapdf_graph_page_display_map(&g,&display)==QUANTAPDF_OK);
        CHECK(display.width_points>2000.0 && display.height_points>2000.0);
        /* The page is a >200-million-pixel geometry at 72 DPI. Only
         * STRUCTURAL metadata (no image buffer) may be produced below. */
        quantapdf_graph_device_grid diagnostic={0};
        CHECK(quantapdf_graph_page_device_grid(&g,72u,
            300000000u,900000000u,&diagnostic)==QUANTAPDF_OK);
        CHECK((size_t)diagnostic.width*(size_t)diagnostic.height>
              200000000u);
        CHECK(diagnostic.output_bytes>600000000u &&
              diagnostic.output_bytes<=900000000u);
        /* Any 4x4 geometry sample budget permitted by the private
         * test caps must refuse before pixel/edge iteration. */
        CHECK(quantapdf_graph_device_grid_admit_q4_work(
            &diagnostic,1u,1u,50000000u)==QUANTAPDF_ERROR_UNSUPPORTED);
        double x=1.0,y=2.0;
        CHECK(quantapdf_graph_device_grid_sample_q4(
            &diagnostic,0u,0u,0u,0u,&x,&y)==QUANTAPDF_OK);
        const double px=diagnostic.from_source[0]*x+
            diagnostic.from_source[2]*y+diagnostic.from_source[4];
        const double py=diagnostic.from_source[1]*x+
            diagnostic.from_source[3]*y+diagnostic.from_source[5];
        CHECK(near(px,0.125) && near(py,0.125));

        for(size_t n=0u;n<3u;++n){
            quantapdf_graph_device_grid denied;
            memset(&denied,0xa5,sizeof(denied));
            CHECK(quantapdf_graph_page_device_grid(&g,dpis[n],
                1000000u,3000000u,&denied)==QUANTAPDF_ERROR_UNSUPPORTED);
            CHECK(memcmp(&denied,&zero,sizeof(denied))==0);
            ++refused;
        }
        quantapdf_graph_device_grid denied;
        memset(&denied,0xa5,sizeof(denied));
        CHECK(quantapdf_graph_page_device_grid(&g,72u,
            300000000u,3000000u,&denied)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(memcmp(&denied,&zero,sizeof(denied))==0);
        ++refused;
    }
    CHECK(refused==16u);
}

int main(void)
{
    const unsigned dpis[]={72u,96u,144u};
    const double units[]={0.5,1.0,2.0};
    size_t positive=0u, refused=0u;
    for (size_t k=0;k<3u;k++)
        for (size_t u=0;u<3u;u++)
            for (int rotation=0;rotation<360;rotation+=90)
                for (int shifted=0;shifted<2;shifted++) {
                    quantapdf_graph_page_geometry g=
                        geometry(shifted,units[u],rotation);
                    quantapdf_graph_device_grid d;
                    CHECK(quantapdf_graph_page_device_grid(&g,dpis[k],
                        60000000u,200000000u,&d)==QUANTAPDF_OK);
                    CHECK(d.width>0u && d.height>0u);
                    for(size_t ii=0;ii<2u;ii++)
                        for(size_t jj=0;jj<2u;jj++)
                            for(unsigned sx=0u;sx<4u;sx++)
                                for(unsigned sy=0u;sy<4u;sy++) {
                                    uint32_t px=ii==0?0u:d.width-1u;
                                    uint32_t py=jj==0?0u:d.height-1u;
                                    double x=0.0,y=0.0,ox,oy,x2=0.0,y2=0.0;
                                    double target_x=(double)px+
                                        ((double)sx+0.5)/4.0;
                                    double target_y=(double)py+
                                        ((double)sy+0.5)/4.0;
                                    CHECK(quantapdf_graph_device_grid_sample_q4(
                                        &d,px,py,sx,sy,&x,&y)==QUANTAPDF_OK);
                                    forward(&d,x,y,&ox,&oy);
                                    CHECK(near(ox,target_x));
                                    CHECK(near(oy,target_y));
                                    CHECK(quantapdf_graph_device_grid_sample_q4(
                                        &d,px,py,sx,sy,&x2,&y2)==QUANTAPDF_OK);
                                    CHECK(memcmp(&x,&x2,sizeof(x))==0);
                                    CHECK(memcmp(&y,&y2,sizeof(y))==0);
                                    ++positive;
                                }
                    {
                        size_t pixels=(size_t)d.width*(size_t)d.height;
                        size_t work=pixels*16u;
                        CHECK(quantapdf_graph_device_grid_admit_q4_work(
                            &d,1u,2u,work)==QUANTAPDF_OK);
                        CHECK(quantapdf_graph_device_grid_admit_q4_work(
                            &d,1u,2u,work-1u)==QUANTAPDF_ERROR_UNSUPPORTED);
                        CHECK(quantapdf_graph_device_grid_admit_q4_work(
                            &d,2u,2u,work*2u)==QUANTAPDF_OK);
                        CHECK(quantapdf_graph_device_grid_admit_q4_work(
                            &d,3u,2u,work*3u)==QUANTAPDF_ERROR_UNSUPPORTED);
                        CHECK(quantapdf_graph_device_grid_admit_q4_work(
                            &d,0u,2u,work)==QUANTAPDF_ERROR_UNSUPPORTED);
                        CHECK(quantapdf_graph_device_grid_admit_q4_work(
                            &d,1u,0u,work)==QUANTAPDF_ERROR_ARGUMENT);
                        CHECK(quantapdf_graph_device_grid_admit_q4_work(
                            &d,1u,2u,0u)==QUANTAPDF_ERROR_ARGUMENT);
                        refused+=5u;
                    }
                    {
                        double x=73.0,y=74.0;
                        CHECK(quantapdf_graph_device_grid_sample_q4(
                            &d,d.width,0,0,0,&x,&y)==QUANTAPDF_ERROR_UNSUPPORTED);
                        CHECK(x==0.0 && y==0.0);
                        x=73.0; y=74.0;
                        CHECK(quantapdf_graph_device_grid_sample_q4(
                            &d,0,d.height,0,0,&x,&y)==QUANTAPDF_ERROR_UNSUPPORTED);
                        CHECK(x==0.0 && y==0.0);
                        x=73.0; y=74.0;
                        CHECK(quantapdf_graph_device_grid_sample_q4(
                            &d,0,0,4,0,&x,&y)==QUANTAPDF_ERROR_UNSUPPORTED);
                        CHECK(x==0.0 && y==0.0);
                        x=73.0; y=74.0;
                        CHECK(quantapdf_graph_device_grid_sample_q4(
                            &d,0,0,0,4,&x,&y)==QUANTAPDF_ERROR_UNSUPPORTED);
                        CHECK(x==0.0 && y==0.0);
                        x=73.0;
                        CHECK(quantapdf_graph_device_grid_sample_q4(
                            &d,0,0,0,0,&x,&x)==QUANTAPDF_ERROR_ARGUMENT);
                        CHECK(x==0.0);
                        refused+=5u;
                    }
                }
    {
        quantapdf_graph_page_geometry g=geometry(1,1.0,90);
        quantapdf_graph_device_grid d;
        double x=1.0,y=2.0;
        CHECK(quantapdf_graph_page_device_grid(&g,72,
            60000000u,200000000u,&d)==QUANTAPDF_OK);
        d.from_source[0]=0.1; /* Shear violates the quarter-turn contract. */
        CHECK(quantapdf_graph_device_grid_sample_q4(
            &d,0,0,0,0,&x,&y)==QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(x==0.0 && y==0.0);
        d.from_source[0]=0.0;
        d.output_bytes++;
        CHECK(quantapdf_graph_device_grid_sample_q4(
            &d,0,0,0,0,&x,&y)==QUANTAPDF_ERROR_ARGUMENT);
        CHECK(x==0.0 && y==0.0);
        CHECK(quantapdf_graph_device_grid_admit_q4_work(
            &d,1u,1u,SIZE_MAX)==QUANTAPDF_ERROR_ARGUMENT);
        refused+=3u;
    }
    CHECK(positive==4608u);
    CHECK(refused==723u);
    test_large_userunit7_preallocation_refusal();
    puts("private device inverse Q4 source sampling: OK");
    return EXIT_SUCCESS;
}
