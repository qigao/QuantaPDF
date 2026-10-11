#include "output_internal.h"
#include "context_internal.h"

#include <stdlib.h>

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

quantapdf_status quantapdf_output_data(
    const quantapdf_output *output,
    const unsigned char **out_data,
    size_t *out_size)
{
    if (out_data != NULL)
        *out_data = NULL;
    if (out_size != NULL)
        *out_size = 0;
    if (output == NULL || out_data == NULL || out_size == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = output->data;
    *out_size = output->size;
    return QUANTAPDF_OK;
}

void quantapdf_drop_output(quantapdf_output *output)
{
    quantapdf_context *context;
    if (output == NULL)
        return;
    context = output->context;
    free(output->data);
    free(output);
    quantapdf_drop_context(context);
}
