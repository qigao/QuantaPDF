#include "internal.h"

#include <stdlib.h>

quantapdf_status quantapdf_poster_split_pages(
    quantapdf_document *document,
    const quantapdf_page_poster_split *splits,
    size_t split_count,
    quantapdf_output **out_output)
{
    quantapdf_output *output;
    quantapdf_status status;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (document == NULL || document->backend == NULL ||
        splits == NULL || split_count == 0)
        return QUANTAPDF_ERROR_ARGUMENT;

#if defined(QUANTAPDF_TESTING)
    if (document->test_poster_fault != QUANTAPDF_TEST_POSTER_FAULT_NONE) {
        document->test_poster_fault = QUANTAPDF_TEST_POSTER_FAULT_NONE;
        return QUANTAPDF_ERROR_FORMAT;
    }
#endif

    output = (quantapdf_output *)calloc(1, sizeof(*output));
    if (output == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    status = document->ops->poster_split_pages(
        document->backend,
        splits,
        split_count,
        &output->data,
        &output->size);
    if (status != QUANTAPDF_OK) {
        free(output);
        return status;
    }
    *out_output = output;
    return QUANTAPDF_OK;
}
