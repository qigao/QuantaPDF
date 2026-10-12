#include "core/output_internal.h"
#include "document/document_internal.h"

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

    status = quantapdf_output_create_internal(document->context, &output);
    if (status != QUANTAPDF_OK)
        return status;

    status = document->ops->crop_pages(
        document->backend,
        crops,
        crop_count,
        &output->data,
        &output->size);
    if (status != QUANTAPDF_OK) {
        quantapdf_drop_output(output);
        return status;
    }

    *out_output = output;
    return QUANTAPDF_OK;
}
