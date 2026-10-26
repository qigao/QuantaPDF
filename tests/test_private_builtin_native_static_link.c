/* Link-only qualification of the PRIVATE built-in Native page adapter.
 * Unlike the source oracle tests, this executable deliberately does NOT
 * compile/link raster OBJECT files itself: all symbols must resolve via
 * QuantaPDF::QuantaPDF's real static product/archive dependency closure.
 * This test never changes public render dispatch and has no PDFium fallback. */
#include "backend/builtin_native_page_v1.h"
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
    return 0;
}
