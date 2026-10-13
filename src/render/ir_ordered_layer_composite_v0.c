#include "render/ir_ordered_layer_composite_v0.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Straight-alpha source-over, mathematically the same integer contract as
 * the existing C11 Scoped Raster operation compositor. The two input layers
 * already contain coverage; this stage must not rasterize or infer Clip. */
static void qp_composite_over(unsigned char *dst, const unsigned char *src)
{
    unsigned sa=src[3], da=dst[3], keep=255u-sa;
    unsigned denominator=255u*sa+keep*da;
    if (sa==0u)
        return;
    for (size_t c=0u;c<3u;++c) {
        unsigned numerator=255u*sa*(unsigned)src[c]+
                           keep*da*(unsigned)dst[c];
        dst[c]=(unsigned char)((numerator+denominator/2u)/denominator);
    }
    dst[3]=(unsigned char)((denominator+127u)/255u);
}

static int qp_composite_layout_valid(
    const quantapdf_ir_raster_image *image)
{
    size_t count,bytes;
    if (image==NULL || image->pixels==NULL ||
        image->width==0u || image->height==0u ||
        image->width>SIZE_MAX/4u)
        return 0;
    if (image->height>SIZE_MAX/image->width)
        return 0;
    count=image->height*image->width;
    if (count>SIZE_MAX/4u)
        return 0;
    bytes=count*4u;
    return image->stride==image->width*4u && image->size==bytes;
}

quantapdf_status quantapdf_ir_ordered_layer_composite_v0(
    const quantapdf_ir_raster_image *earlier_stroke,
    const quantapdf_ir_raster_image *later_clipped_fill,
    const quantapdf_ir_ordered_composite_v0_limits *limits,
    quantapdf_ir_raster_image *out_image)
{
    unsigned char *pixels;
    size_t pixels_count,bytes;
    if (out_image==NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    /* An output structure may not alias a borrowed input; no safe way to
     * clear such a slot without changing the borrowed source itself. */
    if (out_image==earlier_stroke || out_image==later_clipped_fill)
        return QUANTAPDF_ERROR_ARGUMENT;
    memset(out_image,0,sizeof(*out_image));
    if (limits==NULL || limits->max_pixels==0u ||
        limits->max_output_bytes==0u ||
        !qp_composite_layout_valid(earlier_stroke) ||
        !qp_composite_layout_valid(later_clipped_fill))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (earlier_stroke->width!=later_clipped_fill->width ||
        earlier_stroke->height!=later_clipped_fill->height ||
        earlier_stroke->size!=later_clipped_fill->size)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    pixels_count=earlier_stroke->width*earlier_stroke->height;
    bytes=earlier_stroke->size;
    /* The two input images were admitted by their own prior pixel/work
     * budgets. Charge the newly owned output separately, before malloc. */
    if (pixels_count>limits->max_pixels ||
        bytes>limits->max_output_bytes)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    pixels=(unsigned char *)malloc(bytes);
    if (pixels==NULL)
        return QUANTAPDF_ERROR_NOMEM;
    memset(pixels,0,bytes);
    for(size_t i=0u;i<pixels_count;++i) {
        unsigned char *dst=pixels+i*4u;
        qp_composite_over(dst,earlier_stroke->pixels+i*4u);
        qp_composite_over(dst,later_clipped_fill->pixels+i*4u);
    }
    out_image->pixels=pixels;
    out_image->width=earlier_stroke->width;
    out_image->height=earlier_stroke->height;
    out_image->stride=earlier_stroke->stride;
    out_image->size=bytes;
    return QUANTAPDF_OK;
}
