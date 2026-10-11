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

    output = (quantapdf_output *)calloc(1, sizeof(*output));
    if (output == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    status = document->ops->flatten_interactive(
        document->backend,
        flags,
        &output->data,
        &output->size);
    if (status != QUANTAPDF_OK) {
        free(output->data);
        free(output);
        return status;
    }

    *out_output = output;
    return QUANTAPDF_OK;
}
