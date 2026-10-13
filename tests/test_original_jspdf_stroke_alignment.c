#include <fpdfview.h>
#include "render/native_device_grid.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define AB_REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: stroke alignment A/B: %s\n", \
            __FILE__, __LINE__, #condition); \
    goto cleanup; \
} } while (0)

typedef struct alignment_case {
    unsigned dpi;
    int width, height;
} alignment_case;

static const alignment_case cases[3] = {
    {72u,596,842}, {96u,794,1123}, {144u,1191,1684}
};

enum {
    DPI_CASES = 3,
    X_PROBES = 2,
    Y_ROWS = 7,
    SAMPLE_SIDE = 4
};

/* Source S: an opaque 1pt black multi-segment path includes the horizontal
 * segment (50,821.89)-(250,821.89) in PDF user-space (Y-up).
 * In displayed Y-down page points, the centre is 20.0. At x=200/220 there
 * is no later red fill, vertical stroke, join or butt-cap edge.
 *
 * Native coverage is a hypothesis using the ACTUAL private C11 device grid.
 * PDFium's AA sample centers are NOT inferred from PageToDevice. */
static int c11_coverage(
    const quantapdf_native_device_grid *grid,
    size_t x, size_t y, double device_y_phase,
    unsigned *out)
{
    unsigned inside = 0u, sx, sy;
    for (sy=0u; sy<SAMPLE_SIDE; ++sy) {
        for (sx=0u; sx<SAMPLE_SIDE; ++sx) {
            double px=0.0,py=0.0;
            if (quantapdf_native_device_grid_sample_v0(
                    grid,x,y,sx,sy,&px,&py) != QUANTAPDF_NATIVE_DEVICE_GRID_OK)
                return 0;
            py += device_y_phase * grid->points_per_device_y;
            if (px > 50.0 && px < 250.0 &&
                py >= 19.5 && py <= 20.5)
                ++inside;
        }
    }
    *out=(inside*255u+8u)/16u;
    return 1;
}

static unsigned absdiff(unsigned a,unsigned b)
{
    return a>b ? a-b : b-a;
}

/* Same ORIGINAL source: the unoccluded vertical black segment at PDF
 * x=250 runs across displayed y=20..140. At y=60/80 there is neither a
 * miter/join nor the later red Fill. The X scan isolates width/coverage. */
static int c11_vertical_coverage(
    const quantapdf_native_device_grid *grid,
    size_t x,size_t y,double device_x_phase,unsigned *out)
{
    unsigned inside=0u;
    for(unsigned sy=0u;sy<SAMPLE_SIDE;++sy) {
        for(unsigned sx=0u;sx<SAMPLE_SIDE;++sx) {
            double px=0.0,py=0.0;
            if(quantapdf_native_device_grid_sample_v0(
                   grid,x,y,sx,sy,&px,&py)!=QUANTAPDF_NATIVE_DEVICE_GRID_OK)
                return 0;
            px+=device_x_phase*grid->points_per_device_x;
            if(py>20.0 && py<140.0 && px>=249.5 && px<=250.5)
                ++inside;
        }
    }
    *out=(inside*255u+8u)/16u;
    return 1;
}

static int compare_vertical(
    FPDF_PAGE page,const alignment_case *c,
    const quantapdf_native_device_grid *grid,
    const unsigned char *raw,int stride,
    double page_width,double page_height,
    size_t *out_samples,double *out_coordinate_max)
{
    const unsigned source_y[2]={60u,80u};
    const double phases[3]={-0.5,0.0,0.5};
    const int center=(int)floor(250.0*(double)c->width/page_width);
    unsigned long long error[3]={0u,0u,0u};
    unsigned max_delta[3]={0u,0u,0u};
    size_t samples=0u;
    double max_coord=0.0;
    if(center<3 || center+3>=c->width)
        return 0;
    printf(",\"vertical_stroke\":{\"source_x_points\":250,"
           "\"source_y_display_points\":[60,80],\"observations\":[\n");
    for(size_t iy=0u;iy<2u;++iy) {
        int anchored_x=0,anchored_y=0;
        const double page_y=page_height-(double)source_y[iy];
        if(!FPDF_PageToDevice(
               page,0,0,c->width,c->height,0,250.0,page_y,
               &anchored_x,&anchored_y) ||
           anchored_y<3 || anchored_y>=c->height-3 ||
           absdiff((unsigned)anchored_x,(unsigned)center)>1u ||
           fabs((double)anchored_y-
                (double)source_y[iy]*(double)c->height/page_height)>1.5)
            return 0;
        for(int step=-3;step<=3;++step) {
            const int col=center+step;
            double px=NAN,py=NAN;
            double ex,ey;
            const unsigned char *pixel;
            unsigned pdfium_rgb[3],candidate_alpha[3],delta[3];
            if(!FPDF_DeviceToPage(
                   page,0,0,c->width,c->height,0,
                   col,anchored_y,&px,&py))
                return 0;
            ex=fabs(px-(double)col*grid->points_per_device_x);
            ey=fabs((page_height-py)-
                    (double)anchored_y*grid->points_per_device_y);
            if(!isfinite(ex)||!isfinite(ey) || ex>0.0005 || ey>0.0005)
                return 0;
            if(ex>max_coord)max_coord=ex;
            if(ey>max_coord)max_coord=ey;
            pixel=raw+(size_t)anchored_y*(size_t)stride+(size_t)col*4u;
            pdfium_rgb[0]=pixel[2];
            pdfium_rgb[1]=pixel[1];
            pdfium_rgb[2]=pixel[0];
            for(size_t phase=0u;phase<3u;++phase) {
                unsigned predicted;
                if(!c11_vertical_coverage(grid,(size_t)col,
                                          (size_t)anchored_y,
                                          phases[phase],&candidate_alpha[phase]))
                    return 0;
                predicted=255u-candidate_alpha[phase];
                delta[phase]=absdiff(pdfium_rgb[0],predicted);
                error[phase]+=(unsigned long long)delta[phase];
                if(delta[phase]>max_delta[phase])
                    max_delta[phase]=delta[phase];
            }
            if(samples!=0u)printf(",\n");
            printf("      {\"source_y_display\":%u,\"device_x\":%d,"
                   "\"device_y\":%d,\"pdfium_rgb\":[%u,%u,%u],"
                   "\"c11_unshifted_4x4_alpha\":%u,"
                   "\"phase_trial_alpha\":[%u,%u,%u],"
                   "\"abs_delta_trial\":[%u,%u,%u],"
                   "\"device_anchor_error_points\":%.9f}",
                   source_y[iy],col,anchored_y,
                   pdfium_rgb[0],pdfium_rgb[1],pdfium_rgb[2],
                   candidate_alpha[1],
                   candidate_alpha[0],candidate_alpha[1],candidate_alpha[2],
                   delta[0],delta[1],delta[2],fmax(ex,ey));
            ++samples;
        }
    }
    printf("\n    ],\"samples\":%zu,\"max_page_api_error_points\":%.9f,"
           "\"phase_trial_abs_gray_error\":[%llu,%llu,%llu],"
           "\"phase_trial_max_gray_delta\":[%u,%u,%u],"
           "\"sample_phase_is_test_only\":true,"
           "\"pdfium_aa_sample_centers_proven\":false}",
           samples,max_coord,error[0],error[1],error[2],
           max_delta[0],max_delta[1],max_delta[2]);
    *out_samples+=samples;
    if(max_coord>*out_coordinate_max)*out_coordinate_max=max_coord;
    return samples==14u;
}

static int compare_dpi(FPDF_PAGE page,const alignment_case *c,
                       double page_width,double page_height,
                       size_t *out_samples,size_t *out_vertical_samples,
                       double *out_coordinate_max)
{
    const double centre_down=20.0;
    const double centre_pdf_y=page_height-centre_down;
    const unsigned source_x[X_PROBES]={200u,220u};
    const double phases[3]={-0.5,0.0,0.5};
    const quantapdf_native_device_grid_geometry geometry={
        595.28,841.89,0.0,0.0,1.0,0,0,(float)c->dpi
    };
    const quantapdf_native_device_grid_limits limits={
        2100000u,8400000u,64u,50000000u
    };
    quantapdf_native_device_grid grid={0};
    FPDF_BITMAP bitmap=NULL;
    unsigned char *raw;
    int stride;
    unsigned long long errors[3]={0u,0u,0u};
    unsigned error_max[3]={0u,0u,0u};
    size_t samples=0u;
    double max_coord_error=0.0;
    int centre_device, anchor_device_x=0, anchor_device_y=0;
    int success=0;

    AB_REQUIRE(c->width>0 && c->height>0);
    AB_REQUIRE(quantapdf_native_device_grid_create_v0(
        &geometry,&limits,&grid)==QUANTAPDF_NATIVE_DEVICE_GRID_OK);
    AB_REQUIRE(grid.width==(size_t)c->width &&
               grid.height==(size_t)c->height);
    AB_REQUIRE(FPDF_PageToDevice(
        page,0,0,c->width,c->height,0,
        200.0,centre_pdf_y,&anchor_device_x,&anchor_device_y));
    AB_REQUIRE(anchor_device_x>=0 && anchor_device_x<c->width);
    AB_REQUIRE(anchor_device_y>=0 && anchor_device_y<c->height);
    AB_REQUIRE(fabs((double)anchor_device_y-
                    centre_down*(double)c->height/page_height)<=1.5);
    centre_device=(int)floor(centre_down*(double)c->height/page_height);
    AB_REQUIRE(centre_device>=3 && centre_device+3<c->height);

    bitmap=FPDFBitmap_Create(c->width,c->height,0);
    if(bitmap==NULL) {
        fprintf(stderr,"stroke alignment A/B: bitmap allocation failed\n");
        return 0;
    }
    FPDFBitmap_FillRect(bitmap,0,0,c->width,c->height,0xffffffffu);
    FPDF_RenderPageBitmap(bitmap,page,0,0,c->width,c->height,0,
                          FPDF_ANNOT | FPDF_LCD_TEXT);
    raw=(unsigned char *)FPDFBitmap_GetBuffer(bitmap);
    stride=FPDFBitmap_GetStride(bitmap);
    if (raw==NULL || stride<=0 || (size_t)stride<(size_t)c->width*4u)
        goto cleanup;

    printf("    {\"dpi\":%u,\"device_width\":%d,\"device_height\":%d,"
           "\"line_center_y_device_float\":%.10f,"
           "\"pdfium_page_to_device_y\":%d,\"observations\":[\n",
           c->dpi,c->width,c->height,
           centre_down*(double)c->height/page_height,anchor_device_y);

    for (size_t ix=0u;ix<X_PROBES;++ix) {
        int dx=0,dy=0;
        AB_REQUIRE(FPDF_PageToDevice(
            page,0,0,c->width,c->height,0,
            (double)source_x[ix],centre_pdf_y,&dx,&dy));
        AB_REQUIRE(dx>=3 && dx<c->width-3);
        AB_REQUIRE(fabs((double)dx-(double)source_x[ix]*
                         (double)c->width/page_width)<=1.5);
        AB_REQUIRE(dy>=0 && dy<c->height);
        AB_REQUIRE(absdiff((unsigned)dy,(unsigned)anchor_device_y)<=1u);
        for (int step=-3;step<=3;++step) {
            const int device_row=centre_device+step;
            double observed_page_x=NAN,observed_page_y=NAN;
            const unsigned char *pixel;
            unsigned pdfium_rgb[3],candidate_alpha[3],candidate_rgb[3];
            unsigned delta[3];
            double error_x,error_y;
            AB_REQUIRE(device_row>=0 && device_row<c->height);
            AB_REQUIRE(FPDF_DeviceToPage(
                page,0,0,c->width,c->height,0,
                dx,device_row,&observed_page_x,&observed_page_y));
            error_x=fabs(observed_page_x-(double)dx*grid.points_per_device_x);
            error_y=fabs((page_height-observed_page_y)-
                         (double)device_row*grid.points_per_device_y);
            if (!isfinite(error_x) || !isfinite(error_y) ||
                error_x>0.0005 || error_y>0.0005)
                goto cleanup;
            if (error_x>max_coord_error) max_coord_error=error_x;
            if (error_y>max_coord_error) max_coord_error=error_y;
            pixel=raw+(size_t)device_row*(size_t)stride+(size_t)dx*4u;
            /* FPDFBitmap_Create(no alpha) is BGRx, as in the production
             * PDFium backend. This region has NO later red Fill. */
            pdfium_rgb[0]=pixel[2];
            pdfium_rgb[1]=pixel[1];
            pdfium_rgb[2]=pixel[0];
            for (size_t i=0u;i<3u;++i) {
                if (!c11_coverage(&grid,(size_t)dx,(size_t)device_row,
                                  phases[i],&candidate_alpha[i]))
                    goto cleanup;
                candidate_rgb[i]=255u-candidate_alpha[i];
                delta[i]=absdiff(pdfium_rgb[0],candidate_rgb[i]);
                errors[i]+=(unsigned long long)delta[i];
                if(delta[i]>error_max[i])error_max[i]=delta[i];
            }
            if (samples!=0u) printf(",\n");
            printf("      {\"source_x\":%u,\"device_x\":%d,\"device_y\":%d,"
                   "\"pdfium_rgb\":[%u,%u,%u],"
                   "\"c11_unshifted_4x4_alpha\":%u,"
                   "\"phase_trial_alpha\":[%u,%u,%u],"
                   "\"abs_delta_trial\":[%u,%u,%u],"
                   "\"device_anchor_error_points\":%.9f}",
                   source_x[ix],dx,device_row,
                   pdfium_rgb[0],pdfium_rgb[1],pdfium_rgb[2],
                   candidate_alpha[1],
                   candidate_alpha[0],candidate_alpha[1],candidate_alpha[2],
                   delta[0],delta[1],delta[2],
                   fmax(error_x,error_y));
            ++samples;
        }
    }
    printf("\n    ],\"samples\":%zu,\"max_page_api_error_points\":%.9f,"
           "\"phase_trial_abs_gray_error\":[%llu,%llu,%llu],"
           "\"phase_trial_max_gray_delta\":[%u,%u,%u],"
           "\"sample_phase_is_test_only\":true,"
           "\"pdfium_aa_sample_centers_proven\":false",
           samples,max_coord_error,
           errors[0],errors[1],errors[2],
           error_max[0],error_max[1],error_max[2]);
    if(!compare_vertical(page,c,&grid,raw,stride,page_width,page_height,
                         out_vertical_samples,&max_coord_error))
        goto cleanup;
    printf("}");
    success=1;
cleanup:
    FPDFBitmap_Destroy(bitmap);
    if(success) {
        *out_samples+=samples;
        if(max_coord_error>*out_coordinate_max)
            *out_coordinate_max=max_coord_error;
    }
    return success;
}

int main(int argc,char **argv)
{
    FPDF_DOCUMENT doc=NULL;
    FPDF_PAGE page=NULL;
    size_t total_samples=0u,total_vertical_samples=0u;
    double coordinate_max=0.0;
    double w=0.0,h=0.0;
    int success=0;
    if(argc!=2 || argv[1]==NULL || argv[1][0]=='\0')return EXIT_FAILURE;
    FPDF_InitLibrary();
    doc=FPDF_LoadDocument(argv[1],NULL);
    if(doc==NULL || FPDF_GetPageCount(doc)!=1)goto cleanup;
    page=FPDF_LoadPage(doc,0);
    if(page==NULL)goto cleanup;
    w=(double)FPDF_GetPageWidthF(page);
    h=(double)FPDF_GetPageHeightF(page);
    if(!isfinite(w)||!isfinite(h) ||
       fabs(w-595.28)>0.0001 || fabs(h-841.89)>0.0001)
        goto cleanup;

    printf("{\"schema_version\":1,"
           "\"result\":\"original_jspdf_stroke_pixel_phase_diagnostic\","
           "\"document_id\":\"jspdf.w3s-clip-v1\","
           "\"original_pdfium_raster_invocations\":3,"
           "\"native_raster_invocations\":0,"
           "\"dpi_count\":3,"
           "\"source_coordinate_basis\":\"PDF_user_Y_up_to_display_Y_down\","
           "\"source_x_points\":[200,220],"
           "\"source_line_center_y_display_points\":20.0,"
           "\"source_stroke_width_points\":1.0,"
           "\"pixel_parity_qualified\":false,"
           "\"original_pixel_positive_count\":0,"
           "\"official_corpus_admitted\":false,"
           "\"cases\":[\n");
    for(size_t i=0u;i<DPI_CASES;++i) {
        if(i!=0u)printf(",\n");
        if(!compare_dpi(page,&cases[i],w,h,
                        &total_samples,&total_vertical_samples,
                        &coordinate_max))
            goto cleanup;
    }
    printf("\n],\"total_row_observations\":%zu,"
           "\"total_vertical_observations\":%zu,"
           "\"max_pdfium_api_coordinate_error_points\":%.9f,"
           "\"sample_centers_independently_qualified\":false}\n",
           total_samples,total_vertical_samples,coordinate_max);
    if(total_vertical_samples!=DPI_CASES*X_PROBES*Y_ROWS ||
       total_samples!=DPI_CASES*X_PROBES*Y_ROWS ||
       !isfinite(coordinate_max) || coordinate_max>0.0005)
        goto cleanup;
    if(fflush(stdout)!=0 || ferror(stdout))goto cleanup;
    success=1;
cleanup:
    if(!success)fprintf(stderr,
        "original jsPDF Stroke device alignment A/B failed\n");
    if(page!=NULL)FPDF_ClosePage(page);
    if(doc!=NULL)FPDF_CloseDocument(doc);
    FPDF_DestroyLibrary();
    return success?EXIT_SUCCESS:EXIT_FAILURE;
}
