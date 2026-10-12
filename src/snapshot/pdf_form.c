#include "snapshot/form_snapshot.h"
#include "document/document_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

void quantapdf_pdf_form_drop_model(quantapdf_pdf_form_model *model)
{
    size_t index;

    if (model == NULL)
        return;
    for (index = 0; index < model->option_count; ++index)
        free(model->options[index].button_state);
    free(model->fields);
    free(model->values);
    free(model->options);
    free(model->widgets);
    free(model->strings);
    free(model);
}

static void quantapdf_form_reset_rect(quantapdf_rect *rect)
{
    rect->x0 = 0.0f;
    rect->y0 = 0.0f;
    rect->x1 = 0.0f;
    rect->y1 = 0.0f;
}

static const char *quantapdf_form_string_data(
    const quantapdf_pdf_form_model *model,
    const quantapdf_pdf_form_string *string)
{
    return string->present ? model->strings + string->offset : NULL;
}

quantapdf_status quantapdf_document_form(
    quantapdf_document *document,
    quantapdf_form **out_form)
{
    quantapdf_form *form;
    quantapdf_status status;

    if (out_form == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_form = NULL;

    if (document == NULL || document->backend == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    status = quantapdf_context_retain_internal(document->context);
    if (status != QUANTAPDF_OK)
        return status;
    form = (quantapdf_form *)quantapdf_context_allocate_internal(
        document->context, sizeof(*form));
    if (form == NULL) {
        quantapdf_drop_context(document->context);
        return QUANTAPDF_ERROR_NOMEM;
    }
    quantapdf_snapshot_bind(&form->snapshot, document->context,
        quantapdf_form_dispose_internal);
    status = document->ops->extract_form(document->backend, &form->model);
    if (status != QUANTAPDF_OK) {
        quantapdf_drop_form(form);
        return status;
    }
    *out_form = form;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_form_field_count(
    const quantapdf_form *form,
    size_t *out_count)
{
    if (out_count != NULL)
        *out_count = 0;
    if (form == NULL || form->model == NULL || out_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_count = form->model->field_count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_form_field_get_info(
    const quantapdf_form *form,
    size_t field_index,
    quantapdf_form_field_info *out_info)
{
    const quantapdf_pdf_form_field_internal *field;
    size_t minimum_size;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(quantapdf_form_field_info, is_signed) +
        sizeof(out_info->is_signed);
    if (out_info->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->type = QUANTAPDF_FORM_FIELD_UNKNOWN;
    out_info->flags = 0;
    out_info->value_presence = QUANTAPDF_FORM_VALUE_NOT_APPLICABLE;
    out_info->value_count = 0;
    out_info->option_count = 0;
    out_info->widget_count = 0;
    out_info->is_multiselect = 0;
    out_info->is_signed = 0;

    if (form == NULL || form->model == NULL ||
        field_index >= form->model->field_count)
        return QUANTAPDF_ERROR_ARGUMENT;

    field = &form->model->fields[field_index];
    out_info->type = field->type;
    out_info->flags = field->flags;
    out_info->value_presence = field->value_presence;
    out_info->value_count = field->value_count;
    out_info->option_count = field->option_count;
    out_info->widget_count = field->widget_count;
    out_info->is_multiselect = field->is_multiselect;
    out_info->is_signed = field->is_signed;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_form_field_string(
    const quantapdf_form *form,
    size_t field_index,
    const quantapdf_pdf_form_string *string,
    const char **out_utf8,
    size_t *out_size)
{
    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;

    if (form == NULL || form->model == NULL || out_utf8 == NULL ||
        out_size == NULL || field_index >= form->model->field_count)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (!string->present)
        return QUANTAPDF_OK;

    *out_utf8 = quantapdf_form_string_data(form->model, string);
    *out_size = string->size;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_form_field_name(
    const quantapdf_form *form,
    size_t field_index,
    const char **out_utf8,
    size_t *out_size)
{
    if (form == NULL || form->model == NULL ||
        field_index >= form->model->field_count) {
        if (out_utf8 != NULL)
            *out_utf8 = NULL;
        if (out_size != NULL)
            *out_size = 0;
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    return quantapdf_form_field_string(
        form, field_index, &form->model->fields[field_index].name,
        out_utf8, out_size);
}

quantapdf_status quantapdf_form_field_label(
    const quantapdf_form *form,
    size_t field_index,
    const char **out_utf8,
    size_t *out_size)
{
    if (form == NULL || form->model == NULL ||
        field_index >= form->model->field_count) {
        if (out_utf8 != NULL)
            *out_utf8 = NULL;
        if (out_size != NULL)
            *out_size = 0;
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    return quantapdf_form_field_string(
        form, field_index, &form->model->fields[field_index].label,
        out_utf8, out_size);
}

quantapdf_status quantapdf_form_field_value_get_info(
    const quantapdf_form *form,
    size_t field_index,
    size_t value_index,
    quantapdf_form_value_info *out_info)
{
    const quantapdf_pdf_form_field_internal *field;
    const quantapdf_pdf_form_value_internal *value;
    size_t minimum_size;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(quantapdf_form_value_info, option_index) +
        sizeof(out_info->option_index);
    if (out_info->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->kind = QUANTAPDF_FORM_VALUE_UTF8;
    out_info->option_index = SIZE_MAX;
    if (form == NULL || form->model == NULL ||
        field_index >= form->model->field_count)
        return QUANTAPDF_ERROR_ARGUMENT;

    field = &form->model->fields[field_index];
    if (value_index >= field->value_count)
        return QUANTAPDF_ERROR_ARGUMENT;

    value = &form->model->values[field->first_value + value_index];
    out_info->kind = value->kind;
    out_info->option_index = value->kind == QUANTAPDF_FORM_VALUE_OPTION ?
        value->option_index : SIZE_MAX;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_form_field_value_utf8(
    const quantapdf_form *form,
    size_t field_index,
    size_t value_index,
    const char **out_utf8,
    size_t *out_size)
{
    const quantapdf_pdf_form_field_internal *field;
    const quantapdf_pdf_form_value_internal *value;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;
    if (form == NULL || form->model == NULL || out_utf8 == NULL ||
        out_size == NULL || field_index >= form->model->field_count)
        return QUANTAPDF_ERROR_ARGUMENT;

    field = &form->model->fields[field_index];
    if (value_index >= field->value_count)
        return QUANTAPDF_ERROR_ARGUMENT;
    value = &form->model->values[field->first_value + value_index];
    if (value->kind != QUANTAPDF_FORM_VALUE_UTF8)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    *out_utf8 = quantapdf_form_string_data(form->model, &value->utf8);
    *out_size = value->utf8.size;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_form_field_option_get_info(
    const quantapdf_form *form,
    size_t field_index,
    size_t option_index,
    quantapdf_form_option_info *out_info)
{
    const quantapdf_pdf_form_field_internal *field;
    const quantapdf_pdf_form_option_internal *option;
    size_t minimum_size;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(quantapdf_form_option_info, kind) +
        sizeof(out_info->kind);
    if (out_info->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->kind = QUANTAPDF_FORM_OPTION_BUTTON_STATE;
    if (form == NULL || form->model == NULL ||
        field_index >= form->model->field_count)
        return QUANTAPDF_ERROR_ARGUMENT;

    field = &form->model->fields[field_index];
    if (option_index >= field->option_count)
        return QUANTAPDF_ERROR_ARGUMENT;
    option = &form->model->options[field->first_option + option_index];
    out_info->kind = option->kind;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_form_option_string(
    const quantapdf_form *form,
    size_t field_index,
    size_t option_index,
    int display,
    const char **out_utf8,
    size_t *out_size)
{
    const quantapdf_pdf_form_field_internal *field;
    const quantapdf_pdf_form_option_internal *option;
    const quantapdf_pdf_form_string *string;

    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;
    if (form == NULL || form->model == NULL || out_utf8 == NULL ||
        out_size == NULL || field_index >= form->model->field_count)
        return QUANTAPDF_ERROR_ARGUMENT;

    field = &form->model->fields[field_index];
    if (option_index >= field->option_count)
        return QUANTAPDF_ERROR_ARGUMENT;
    option = &form->model->options[field->first_option + option_index];
    if (option->kind != QUANTAPDF_FORM_OPTION_CHOICE)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    string = display ? &option->display_text : &option->export_text;
    *out_utf8 = quantapdf_form_string_data(form->model, string);
    *out_size = string->size;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_form_field_option_export(
    const quantapdf_form *form,
    size_t field_index,
    size_t option_index,
    const char **out_utf8,
    size_t *out_size)
{
    return quantapdf_form_option_string(
        form, field_index, option_index, 0, out_utf8, out_size);
}

quantapdf_status quantapdf_form_field_option_display(
    const quantapdf_form *form,
    size_t field_index,
    size_t option_index,
    const char **out_utf8,
    size_t *out_size)
{
    return quantapdf_form_option_string(
        form, field_index, option_index, 1, out_utf8, out_size);
}

quantapdf_status quantapdf_form_widget_count(
    const quantapdf_form *form,
    size_t *out_count)
{
    if (out_count != NULL)
        *out_count = 0;
    if (form == NULL || form->model == NULL || out_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_count = form->model->widget_count;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_form_widget_get_info(
    const quantapdf_form *form,
    size_t widget_index,
    quantapdf_form_widget_info *out_info)
{
    const quantapdf_pdf_form_widget_internal *widget;
    size_t minimum_size;

    if (out_info == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    minimum_size = offsetof(quantapdf_form_widget_info, button_option_index) +
        sizeof(out_info->button_option_index);
    if (out_info->struct_size < minimum_size)
        return QUANTAPDF_ERROR_ARGUMENT;

    out_info->field_index = SIZE_MAX;
    out_info->page_index = -1;
    quantapdf_form_reset_rect(&out_info->bounds);
    out_info->flags = 0;
    out_info->button_option_index = SIZE_MAX;
    if (form == NULL || form->model == NULL ||
        widget_index >= form->model->widget_count)
        return QUANTAPDF_ERROR_ARGUMENT;

    widget = &form->model->widgets[widget_index];
    out_info->field_index = widget->field_index;
    out_info->page_index = widget->page_index;
    out_info->bounds = widget->bounds;
    out_info->flags = widget->flags;
    out_info->button_option_index = widget->button_option_index;
    return QUANTAPDF_OK;
}

void quantapdf_form_dispose_internal(quantapdf_snapshot *snapshot)
{
    quantapdf_form *form = (quantapdf_form *)snapshot;
    quantapdf_pdf_form_drop_model(form->model);
    free(form);
}

void quantapdf_drop_form(quantapdf_form *form)
{
    quantapdf_snapshot_drop(form == NULL ? NULL : &form->snapshot);
}
