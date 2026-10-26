/* Backend-free C11 qualification for the canonical private RGBA8->RGB8
 * converter. No PDFium/qpdf or provider links, only the RGB OBJECT. */
#include "render/native_backend_rgb_v1.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { \
    fprintf(stderr, "%s:%d: RGB bitmap conversion: %s\n", \
        __FILE__,__LINE__,#e); return EXIT_FAILURE; \
} } while (0)

static int empty(const quantapdf_backend_bitmap *b)
{
    return b->data == NULL && b->size == 0u &&
        b->width == 0 && b->height == 0 &&
        b->stride == 0 && b->components == 0;
}

static int refused(const quantapdf_ir_raster_image *image,
                   size_t max_pixels, quantapdf_status expected)
{
    int i;
    for (i = 0; i < 3; ++i) {
        quantapdf_backend_bitmap b;
        memset(&b, 0xa5, sizeof(b));
        CHECK(quantapdf_native_rgba_to_backend_rgb_v1(
            image,max_pixels,&b) == expected);
        CHECK(empty(&b));
    }
    return EXIT_SUCCESS;
}

int main(void)
{
    unsigned char pixels[]={
        255u,0u,0u,255u, 0u,255u,0u,255u,
        0u,0u,255u,255u, 128u,64u,32u,255u
    };
    const unsigned char source_copy[]={
        255u,0u,0u,255u, 0u,255u,0u,255u,
        0u,0u,255u,255u, 128u,64u,32u,255u
    };
    const unsigned char expected[]={
        255u,0u,0u, 0u,255u,0u,
        0u,0u,255u, 128u,64u,32u
    };
    quantapdf_ir_raster_image image={
        pixels,sizeof(pixels),2u,2u,8u
    };
    quantapdf_backend_bitmap a={0},b={0};
    CHECK(quantapdf_native_rgba_to_backend_rgb_v1(&image,4u,&a)==
          QUANTAPDF_OK);
    CHECK(quantapdf_native_rgba_to_backend_rgb_v1(&image,4u,&b)==
          QUANTAPDF_OK);
    CHECK(a.width==2 && a.height==2 && a.components==3 &&
          a.stride==6 && a.size==sizeof(expected) &&
          b.size==a.size && b.data!=a.data &&
          memcmp(a.data,expected,sizeof(expected))==0 &&
          memcmp(a.data,b.data,a.size)==0);

    /* Destroy/mutate borrowed RGBA after both results have detached. */
    memset(pixels,0,sizeof(pixels));
    CHECK(memcmp(a.data,expected,sizeof(expected))==0 &&
          memcmp(b.data,expected,sizeof(expected))==0);
    free(a.data);
    free(b.data);

    memcpy(pixels,source_copy,sizeof(pixels));

    CHECK(refused(&image,3u,QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    image.stride=9u;
    CHECK(refused(&image,4u,QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    image.stride=8u;
    image.size=15u;
    CHECK(refused(&image,4u,QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    image.size=16u;
    pixels[7]=128u;
    CHECK(refused(&image,4u,QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    pixels[7]=255u;
    image.width=SIZE_MAX;
    CHECK(refused(&image,4u,QUANTAPDF_ERROR_UNSUPPORTED)==EXIT_SUCCESS);
    image.width=2u;
    CHECK(refused(&image,0u,QUANTAPDF_ERROR_ARGUMENT)==EXIT_SUCCESS);
    CHECK(refused(NULL,4u,QUANTAPDF_ERROR_ARGUMENT)==EXIT_SUCCESS);
    CHECK(quantapdf_native_rgba_to_backend_rgb_v1(
        &image,4u,NULL)==QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_native_rgba_to_backend_rgb_v1(&image,4u,&a)==
          QUANTAPDF_OK);
    CHECK(a.size==sizeof(expected) &&
          memcmp(a.data,expected,sizeof(expected))==0);
    free(a.data);
    return EXIT_SUCCESS;
}
