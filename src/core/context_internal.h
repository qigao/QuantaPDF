#ifndef QUANTAPDF_CONTEXT_INTERNAL_H
#define QUANTAPDF_CONTEXT_INTERNAL_H

#include "../backend/document_backend.h"
#include "context_lease_internal.h"

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
/* out must be empty. One additional retained reference for a private prepared payload owner.
 * This is the same externally serialized Core lifetime implementation. */
static inline quantapdf_status quantapdf_context_lease_acquire_internal(
    quantapdf_context *context,quantapdf_context_lease_internal *out)
{
    quantapdf_status status;
    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));
    status=quantapdf_context_retain_internal(context);
    if(status==QUANTAPDF_OK) {out->context=context;out->drop=quantapdf_drop_context;}
    return status;
}

/* Default-allocator storage; a context guard must cover this allocation. */
void *quantapdf_context_allocate_internal(quantapdf_context *context, size_t size);

#ifdef __cplusplus
}
#endif
#endif
