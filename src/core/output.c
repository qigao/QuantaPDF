#include "output_internal.h"

#include <stdlib.h>

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
