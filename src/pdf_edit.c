#include "pdf_edit_internal.h"

#include <stdlib.h>

quantapdf_status quantapdf_pdf_edit_begin(
    quantapdf_document *source,
    quantapdf_pdf_edit **out_edit)
{
    quantapdf_pdf_edit *edit;
    quantapdf_status status;

    if (out_edit == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_edit = NULL;
    if (source == NULL || source->backend == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    edit = (quantapdf_pdf_edit *)calloc(1, sizeof(*edit));
    if (edit == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    edit->ops = source->ops->edit_ops;
    status = source->ops->edit_begin(source->backend, &edit->backend);
    if (status != QUANTAPDF_OK) {
        free(edit);
        return status;
    }
    *out_edit = edit;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_edit_snapshot(
    quantapdf_pdf_edit *edit,
    quantapdf_output **out_output)
{
    quantapdf_output *output;
    quantapdf_status status;
    unsigned char *data = NULL;
    size_t size = 0;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (edit == NULL || edit->backend == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    status = edit->ops->snapshot(
        edit->backend,
#if defined(QUANTAPDF_TESTING)
        &edit->test_fault,
#else
        NULL,
#endif
        &data, &size);
    if (status != QUANTAPDF_OK)
        return status;
    output = (quantapdf_output *)calloc(1, sizeof(*output));
    if (output == NULL) {
        free(data);
        return QUANTAPDF_ERROR_NOMEM;
    }
    output->data = data;
    output->size = size;
    *out_output = output;
    return QUANTAPDF_OK;
}

void quantapdf_drop_pdf_edit(quantapdf_pdf_edit *edit)
{
    if (edit == NULL)
        return;
    edit->ops->drop(edit->backend);
    free(edit);
}
