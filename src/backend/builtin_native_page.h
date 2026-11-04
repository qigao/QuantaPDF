#ifndef QUANTAPDF_BUILTIN_NATIVE_PAGE_H
#define QUANTAPDF_BUILTIN_NATIVE_PAGE_H
#include "backend/native_page.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Page retains its Document for this synchronous call. Source publishes owned
 * IR, preparation adopts IR and the Context lease, pixels are detached. */
quantapdf_status quantapdf_builtin_render_page(
    void *builtin_page,
    const quantapdf_native_page_limits *limits,
    const quantapdf_native_page_device *device,
    quantapdf_context_lease_internal *lease,quantapdf_ir_raster_image *out);
#ifdef __cplusplus
}
#endif
#endif
