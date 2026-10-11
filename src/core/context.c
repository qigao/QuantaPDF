#include "core/context_internal.h"

#include <assert.h>
#include <stdlib.h>

quantapdf_status quantapdf_context_create(quantapdf_context **out_context)
{
    quantapdf_context *context;
    if (out_context == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_context = NULL;
    context = (quantapdf_context *)calloc(1, sizeof(*context));
    if (context == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    context->references = 1;
    context->ops = quantapdf_builtin_backend_ops();
    *out_context = context;
    return QUANTAPDF_OK;
}

void quantapdf_drop_context(quantapdf_context *context)
{
    if (context == NULL)
        return;
    assert(context->references != 0);
    if (--context->references == 0) {
#if defined(QUANTAPDF_TESTING)
        if (context->test_finalizations != NULL)
            ++*context->test_finalizations;
#endif
        free(context);
    }
}
