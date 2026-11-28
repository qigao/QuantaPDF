#include "context_internal.h"

#include <stdlib.h>

quantapdf_status quantapdf_context_retain_internal(quantapdf_context *context)
{
    if (context == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return quantapdf_refcount_retain(
        &context->references, QUANTAPDF_ERROR_UNSUPPORTED);
}

void *quantapdf_context_allocate_internal(quantapdf_context *context, size_t size)
{
#if defined(QUANTAPDF_TESTING)
    if (context->test_fail_allocation) {
        context->test_fail_allocation = 0;
        return NULL;
    }
#else
    (void)context;
#endif
    return calloc(1, size);
}
