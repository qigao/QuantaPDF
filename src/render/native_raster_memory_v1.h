#ifndef QUANTAPDF_NATIVE_RASTER_MEMORY_V1_H
#define QUANTAPDF_NATIVE_RASTER_MEMORY_V1_H

#include "render/ir_raster.h"
#include "render/native_work_ledger_v1.h"
#include <stdlib.h>

/* No allocation header or new allocator: reserve before the SAME malloc,
 * and return exactly that requested byte count on failure/free. */
static inline void *quantapdf_native_raster_malloc_v1(
    quantapdf_native_work_ledger_v1 *ledger,size_t bytes,quantapdf_status *status)
{
    void *p;
    if(!quantapdf_native_memory_take_v1(ledger,bytes)) {
        *status=QUANTAPDF_ERROR_UNSUPPORTED;return NULL;
    }
    p=malloc(bytes);
    if(p==NULL) {
        (void)quantapdf_native_memory_give_v1(ledger,bytes);
        *status=QUANTAPDF_ERROR_NOMEM;
    }
    return p;
}

static inline void quantapdf_native_raster_free_v1(
    quantapdf_native_work_ledger_v1 *ledger,void *p,size_t bytes)
{
    if(p!=NULL) {
        free(p);(void)quantapdf_native_memory_give_v1(ledger,bytes);
    }
}

/* A successful child transfers its charged RGBA buffer to its parent. Only
 * the owning parent disposes it here. Detached final output leaves the page
 * ledger at the public handoff; ordinary image_dispose remains its API. */
static inline void quantapdf_native_raster_image_dispose_v1(
    quantapdf_native_work_ledger_v1 *ledger,quantapdf_ir_raster_image *image)
{
    const size_t bytes=image->pixels!=NULL?image->size:0u;
    quantapdf_ir_raster_image_dispose(image);
    (void)quantapdf_native_memory_give_v1(ledger,bytes);
}

#endif
