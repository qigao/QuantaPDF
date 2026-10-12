#include "internal.h"

#include <stdlib.h>

quantapdf_status quantapdf_flatten_interactive(
    quantapdf_document *document,
    uint32_t flags,
    quantapdf_output **out_output)
{
    const uint32_t known =
        QUANTAPDF_FLATTEN_ANNOTATIONS | QUANTAPDF_FLATTEN_WIDGETS;
    quantapdf_output *output;
    quantapdf_status status;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || document->backend == NULL ||
        flags == 0 || (flags & ~known) != 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_output_create_internal(document->context, &output);
    if (status != QUANTAPDF_OK)
        return status;

    status = document->ops->flatten_interactive(
        document->backend,
        flags,
        &output->data,
        &output->size);
    if (status != QUANTAPDF_OK) {
        quantapdf_drop_output(output);
        return status;
    }

    *out_output = output;
    return QUANTAPDF_OK;
}
