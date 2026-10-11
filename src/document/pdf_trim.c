#include "core/output_internal.h"
#include "document/document_internal.h"

#include <stdlib.h>

quantapdf_status quantapdf_trim_pages(
    quantapdf_document *document,
    const quantapdf_page_trim *trims,
    size_t trim_count,
    quantapdf_output **out_output)
{
    quantapdf_output *output;
    quantapdf_status status;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (document == NULL || document->backend == NULL ||
        trims == NULL || trim_count == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_output_create_internal(document->context, &output);
    if (status != QUANTAPDF_OK)
        return status;
    status = document->ops->trim_pages(
        document->backend,
        trims,
        trim_count,
        &output->data,
        &output->size);
    if (status != QUANTAPDF_OK) {
        quantapdf_drop_output(output);
        return status;
    }
    *out_output = output;
    return QUANTAPDF_OK;
}
