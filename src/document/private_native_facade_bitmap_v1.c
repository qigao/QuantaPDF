#include "document/private_native_facade_bitmap_v1.h"

#include "backend/builtin_native_bitmap_v1.h"
#include "core/bitmap_internal.h"
#include "document/document_internal.h"

#include <math.h>
#include <stdlib.h>

/* Same lifetime transfer as public render.c, but a strictly separate
 * private Native producer. Never changes page->document->ops->render_page. */
static quantapdf_status native_facade_bitmap_impl(
    quantapdf_page *page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_bitmap **out_bitmap,
    int decode_v9)
{
    quantapdf_bitmap *bitmap;
    quantapdf_backend_bitmap rendered = {0};
    quantapdf_status status;
    double unit;

    if (out_bitmap == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_bitmap = NULL;

    if (page == NULL || limits == NULL || device == NULL ||
        page->context == NULL || page->document == NULL ||
        page->document->context != page->context ||
        page->backend == NULL || page->page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    /* The Context's provider is immutable and originates in the actual
     * product. Compare its bound ops with the Page/Document owner rather
     * than comparing addresses of backend tables duplicated by a test
     * executable and the Windows DLL. No dynamic provider substitution. */
    if (page->context->ops == NULL ||
        page->document->ops != page->context->ops ||
        (size_t)page->page_index != device->page_index)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    status = quantapdf_context_retain_internal(page->context);
    if (status != QUANTAPDF_OK)
        return status;

    bitmap = (quantapdf_bitmap *)quantapdf_context_allocate_internal(
        page->context, sizeof(*bitmap));
    if (bitmap == NULL) {
        quantapdf_drop_context(page->context);
        return QUANTAPDF_ERROR_NOMEM;
    }
    bitmap->context = page->context;

    /* Equivalent to the established facade's private user-unit
     * wrapper, but use the live owner-bound provider callback directly.
     * This also keeps the private object usable by an in-tree Windows
     * DLL test without exporting document.c's private symbol. */
    status = page->document->ops->page_user_unit(
        page->document->backend, page->page_index, &unit);
    if (status == QUANTAPDF_OK) {
        /* Source V8's integer-pixel, one-page producer is UserUnit=1
         * only. Neither scale nor device-option approximation is legal. */
        if (!isfinite(unit) || unit != 1.0)
            status = QUANTAPDF_ERROR_UNSUPPORTED;
        else
            status = decode_v9 ?
                quantapdf_builtin_native_qualify_bitmap_v9(
                    page->backend, limits, device, &rendered) :
                quantapdf_builtin_native_qualify_bitmap_v1(
                    page->backend, limits, device, &rendered);
    }

    if (status != QUANTAPDF_OK) {
        free(rendered.data);
        free(bitmap);
        quantapdf_drop_context(page->context);
        return status;
    }

    bitmap->data = rendered.data;
    bitmap->size = rendered.size;
    bitmap->width = rendered.width;
    bitmap->height = rendered.height;
    bitmap->stride = rendered.stride;
    bitmap->components = rendered.components;

    *out_bitmap = bitmap;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_render_page_native_qualified_v1(
    quantapdf_page *page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_bitmap **out_bitmap)
{
    return native_facade_bitmap_impl(
        page, limits, device, out_bitmap, 0);
}

quantapdf_status quantapdf_render_page_native_qualified_v9(
    quantapdf_page *page,
    const quantapdf_native_source_raster_bridge_limits_v1 *limits,
    const quantapdf_native_source_raster_device_v1 *device,
    quantapdf_bitmap **out_bitmap)
{
    return native_facade_bitmap_impl(
        page, limits, device, out_bitmap, 1);
}
