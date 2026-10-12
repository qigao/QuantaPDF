#include "internal.h"

#include <limits.h>
#include <stdlib.h>

quantapdf_status quantapdf_export_pages(
    quantapdf_document *document,
    const int *page_indices,
    size_t page_count,
    quantapdf_output **out_output)
{
    quantapdf_output *output;
    quantapdf_status status;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (document == NULL || page_indices == NULL || page_count == 0 ||
        page_count > (size_t)INT_MAX)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_output_create_internal(document->context, &output);
    if (status != QUANTAPDF_OK)
        return status;
    status = document->ops->export_pages(
        document->backend,
        page_indices,
        page_count,
        &output->data,
        &output->size);
    if (status != QUANTAPDF_OK) {
        quantapdf_drop_output(output);
        return status;
    }
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
