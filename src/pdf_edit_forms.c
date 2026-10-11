#include "pdf_edit_internal.h"
#include "form_snapshot.h"

#include <stdlib.h>
#include <string.h>

struct quantapdf_form {
    quantapdf_pdf_form_model *model;
};

quantapdf_status quantapdf_pdf_edit_form_snapshot(
    quantapdf_pdf_edit *edit,
    quantapdf_form **out_form)
{
    quantapdf_pdf_form_model *model = NULL;
    quantapdf_form *form;
    quantapdf_status status;

    if (out_form == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_form = NULL;
    if (edit == NULL || edit->backend == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    status = edit->ops->form_snapshot(edit->backend, &model);
    if (status != QUANTAPDF_OK)
        return status;
    form = (quantapdf_form *)calloc(1, sizeof(*form));
    if (form == NULL) {
        quantapdf_pdf_form_drop_model(model);
        return QUANTAPDF_ERROR_NOMEM;
    }
    form->model = model;
    *out_form = form;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_pdf_edit_form_field_ref_at(
    quantapdf_pdf_edit *edit,
    size_t field_index,
    quantapdf_form_field_ref *out_ref)
{
    if (out_ref != NULL)
        memset(out_ref, 0, sizeof(*out_ref));
    if (edit == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return edit->ops->form_ref_at(
        edit->backend, field_index, out_ref);
}

quantapdf_status quantapdf_pdf_edit_form_set_values(
    quantapdf_pdf_edit *edit,
    const quantapdf_form_field_ref *ref,
    const quantapdf_form_value_update *update)
{
    if (edit == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return edit->ops->form_set_values(
        edit->backend, ref, update,
#if defined(QUANTAPDF_TESTING)
        &edit->test_fault
#else
        NULL
#endif
    );
}
