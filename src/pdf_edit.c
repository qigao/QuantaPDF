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
    status = quantapdf_context_retain_internal(source->context);
    if (status != QUANTAPDF_OK)
        return status;
    edit = (quantapdf_pdf_edit *)quantapdf_context_allocate_internal(
        source->context, sizeof(*edit));
    if (edit == NULL) {
        quantapdf_drop_context(source->context);
        return QUANTAPDF_ERROR_NOMEM;
    }
    edit->context = source->context;
    edit->ops = source->ops->edit_ops;
    status = source->ops->edit_begin(source->backend, &edit->backend);
    if (status != QUANTAPDF_OK) {
        free(edit);
        quantapdf_drop_context(source->context);
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

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (edit == NULL || edit->backend == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    status = quantapdf_output_create_internal(edit->context, &output);
    if (status != QUANTAPDF_OK)
        return status;
    status = edit->ops->snapshot(
        edit->backend,
#if defined(QUANTAPDF_TESTING)
        &edit->test_fault,
#else
        NULL,
#endif
        &output->data, &output->size);
    if (status != QUANTAPDF_OK) {
        quantapdf_drop_output(output);
        return status;
    }
    *out_output = output;
    return QUANTAPDF_OK;
}

void quantapdf_drop_pdf_edit(quantapdf_pdf_edit *edit)
{
    quantapdf_context *context;
    if (edit == NULL)
        return;
    context = edit->context;
    edit->ops->drop(edit->backend);
    free(edit);
    quantapdf_drop_context(context);
}
