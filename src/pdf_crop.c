#include "internal.h"

#include <stdlib.h>

quantapdf_status quantapdf_crop_pages(
    quantapdf_document *document,
    const quantapdf_page_crop *crops,
    size_t crop_count,
    quantapdf_output **out_output)
{
    quantapdf_output *output;
    quantapdf_status status;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;

    if (document == NULL || document->backend == NULL ||
        crops == NULL || crop_count == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    output = (quantapdf_output *)calloc(1, sizeof(*output));
    if (output == NULL)
        return QUANTAPDF_ERROR_NOMEM;

    status = document->ops->crop_pages(
        document->backend,
        crops,
        crop_count,
        &output->data,
        &output->size);
    if (status != QUANTAPDF_OK) {
        free(output);
        return status;
    }

    *out_output = output;
    return QUANTAPDF_OK;
}
