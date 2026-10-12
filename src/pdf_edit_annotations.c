#include "pdf_edit_internal.h"

#include <string.h>

quantapdf_status quantapdf_pdf_edit_annotation_count(
    quantapdf_pdf_edit *edit,
    int page_index,
    size_t *out_count)
{
    if (out_count != NULL)
        *out_count = 0;
    if (edit == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return edit->ops->annotation_count(
        edit->backend, page_index, out_count);
}

quantapdf_status quantapdf_pdf_edit_annotation_ref_at(
    quantapdf_pdf_edit *edit,
    int page_index,
    size_t index,
    quantapdf_annotation_ref *out_ref)
{
    if (out_ref != NULL)
        memset(out_ref, 0, sizeof(*out_ref));
    if (edit == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return edit->ops->annotation_ref_at(
        edit->backend, page_index, index, out_ref);
}

quantapdf_status quantapdf_pdf_edit_annotation_get_info(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    quantapdf_annotation_info *out_info)
{
    if (edit == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return edit->ops->annotation_get_info(
        edit->backend, ref, out_info);
}

quantapdf_status quantapdf_pdf_edit_annotation_contents(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    char **out_utf8,
    size_t *out_size)
{
    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;
    if (edit == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return edit->ops->annotation_contents(
        edit->backend, ref, out_utf8, out_size);
}

quantapdf_status quantapdf_pdf_edit_annotation_create(
    quantapdf_pdf_edit *edit,
    int page_index,
    const quantapdf_annotation_create_options *options,
    quantapdf_annotation_ref *out_ref)
{
    if (out_ref != NULL)
        memset(out_ref, 0, sizeof(*out_ref));
    if (edit == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return edit->ops->annotation_create(
        edit->backend, page_index, options, out_ref,
#if defined(QUANTAPDF_TESTING)
        &edit->test_fault
#else
        NULL
#endif
    );
}

quantapdf_status quantapdf_pdf_edit_annotation_update(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    const quantapdf_annotation_update *update)
{
    if (edit == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return edit->ops->annotation_update(
        edit->backend, ref, update,
#if defined(QUANTAPDF_TESTING)
        &edit->test_fault
#else
        NULL
#endif
    );
}

quantapdf_status quantapdf_pdf_edit_annotation_delete(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref)
{
    if (edit == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return edit->ops->annotation_delete(edit->backend, ref);
}
