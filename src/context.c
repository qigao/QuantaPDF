#include "internal.h"

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

quantapdf_status quantapdf_context_retain_internal(quantapdf_context *context)
{
    if (context == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    assert(context->references != 0);
    if (context->references == SIZE_MAX)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    ++context->references;
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

quantapdf_status quantapdf_output_create_internal(
    quantapdf_context *context, quantapdf_output **out_output)
{
    quantapdf_output *output;
    quantapdf_status status;
    *out_output = NULL;
    status = quantapdf_context_retain_internal(context);
    if (status != QUANTAPDF_OK)
        return status;
    output = (quantapdf_output *)quantapdf_context_allocate_internal(
        context, sizeof(*output));
    if (output == NULL) {
        quantapdf_drop_context(context);
        return QUANTAPDF_ERROR_NOMEM;
    }
    output->context = context;
    *out_output = output;
    return QUANTAPDF_OK;
}
