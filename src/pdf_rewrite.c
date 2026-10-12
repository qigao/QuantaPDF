#include "internal.h"

#include <stdlib.h>

quantapdf_status quantapdf_rewrite_lossless(
    quantapdf_document *document,
    quantapdf_output **out_output)
{
    quantapdf_output *output;
    quantapdf_status status;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || document->backend == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_output_create_internal(document->context, &output);
    if (status != QUANTAPDF_OK)
        return status;

    status = document->ops->rewrite_lossless(
        document->backend,
        &output->data,
        &output->size);
    if (status != QUANTAPDF_OK) {
        quantapdf_drop_output(output);
        return status;
    }

    *out_output = output;
    return QUANTAPDF_OK;
}
