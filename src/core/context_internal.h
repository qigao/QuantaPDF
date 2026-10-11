#ifndef QUANTAPDF_CONTEXT_INTERNAL_H
#define QUANTAPDF_CONTEXT_INTERNAL_H

#include "../backend/document_backend.h"

/* ABI v2 requires external serialization, including retain and release. */
struct quantapdf_context {
    size_t references;
    const quantapdf_backend_ops *ops;
#if defined(QUANTAPDF_TESTING)
    int test_fail_allocation;
    size_t *test_finalizations;
#endif
};

#ifdef __cplusplus
extern "C" {
#endif

quantapdf_status quantapdf_context_retain_internal(quantapdf_context *context);
/* Default-allocator storage; a context guard must cover this allocation. */
void *quantapdf_context_allocate_internal(quantapdf_context *context, size_t size);

#ifdef __cplusplus
}
#endif
#endif
