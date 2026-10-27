/* Link-only qualification of the PRIVATE built-in Native page adapter.
 * Unlike the source oracle tests, this executable deliberately does NOT
 * compile/link raster OBJECT files itself: all symbols must resolve via
 * QuantaPDF::QuantaPDF's real static product/archive dependency closure.
 * This test never changes public render dispatch and has no PDFium fallback. */
#include "backend/builtin_native_page_v1.h"
#include "backend/builtin_native_bitmap_v1.h"
#include "document/private_native_facade_bitmap_v1.h"
#include "render/native_backend_rgb_v1.h"
#include <stdlib.h>
#include <stdint.h>

int main(void)
{
    quantapdf_ir_raster_image output = {0};
    quantapdf_status status;
    output.pixels = (unsigned char *)(uintptr_t)1u;
    output.width = output.height = output.stride = output.size = 123u;

    status = quantapdf_builtin_native_qualify_page_v1(
        NULL, NULL, NULL, &output);
    if (status != QUANTAPDF_ERROR_ARGUMENT)
        return 1;
    if (output.pixels != NULL || output.width != 0u ||
        output.height != 0u || output.stride != 0u ||
        output.size != 0u)
        return 2;
    /* The same pure-C11 RGB converter must also resolve only from the
     * actual product archive, not from test-local OBJECT insertions. */
    {
        unsigned char rgba_bytes[]={17u,31u,47u,255u};
        quantapdf_ir_raster_image rgba={
            rgba_bytes,sizeof(rgba_bytes),1u,1u,4u
        };
        quantapdf_backend_bitmap rgb={0};
        status=quantapdf_native_rgba_to_backend_rgb_v1(
            &rgba,1u,&rgb);
        if(status!=QUANTAPDF_OK || rgb.data==NULL ||
           rgb.size!=3u || rgb.width!=1 || rgb.height!=1 ||
           rgb.stride!=3 || rgb.components!=3)
            return 3;
        if(rgb.data[0]!=17u || rgb.data[1]!=31u ||
           rgb.data[2]!=47u) {
            free(rgb.data);
            return 4;
        }
        free(rgb.data);

        rgb.data=(unsigned char *)(uintptr_t)1u;
        rgb.size=123u;
        status=quantapdf_builtin_native_qualify_bitmap_v1(
            NULL,NULL,NULL,&rgb);
        if(status!=QUANTAPDF_ERROR_ARGUMENT ||
           rgb.data!=NULL || rgb.size!=0u ||
           rgb.width!=0 || rgb.height!=0 ||
           rgb.stride!=0 || rgb.components!=0)
            return 5;
    }
    {
        /* The PRIVATE facade function must also resolve from the
         * installed product archive dependency closure, without direct
         * backend targets, duplicated native OBJECT files, or exports. */
        quantapdf_bitmap *facade=(quantapdf_bitmap *)(uintptr_t)1u;
        status=quantapdf_render_page_native_qualified_v1(
            NULL,NULL,NULL,&facade);
        if(status!=QUANTAPDF_ERROR_ARGUMENT || facade!=NULL)
            return 6;
    }
    return 0;
}
