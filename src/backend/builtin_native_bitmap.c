#include "backend/builtin_native_bitmap.h"
#include "render/native_backend_rgb_v1.h"
#include <string.h>

quantapdf_status quantapdf_builtin_render_bitmap(
    void *builtin_page,
    const quantapdf_native_page_limits *limits,
    const quantapdf_native_page_device *device,
    quantapdf_context_lease_internal *lease,quantapdf_backend_bitmap *out)
{
    quantapdf_ir_raster_image image={0};
    memset(out,0,sizeof(*out));
    quantapdf_status status=quantapdf_builtin_render_page(
        builtin_page,limits,device,lease,&image);
    if(status==QUANTAPDF_OK)
        status=quantapdf_native_rgba_to_backend_rgb_v1(&image,limits->raster.max_pixels,out);
    quantapdf_ir_raster_image_dispose(&image);
    return status;
}
