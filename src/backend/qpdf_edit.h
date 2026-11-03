#ifndef QUANTAPDF_BACKEND_QPDF_EDIT_H
#define QUANTAPDF_BACKEND_QPDF_EDIT_H

#include <stddef.h>

#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct quantapdf_qpdf_document quantapdf_qpdf_document;
typedef struct quantapdf_qpdf_edit quantapdf_qpdf_edit;
typedef struct quantapdf_pdf_form_model quantapdf_pdf_form_model;

quantapdf_status quantapdf_qpdf_edit_begin(
    quantapdf_qpdf_document *source,
    quantapdf_qpdf_edit **out_edit);
quantapdf_status quantapdf_qpdf_edit_snapshot(
    quantapdf_qpdf_edit *edit,
    int *test_fault,
    unsigned char **out_data,
    size_t *out_size);
quantapdf_status quantapdf_qpdf_edit_form_snapshot(
    quantapdf_qpdf_edit *edit,
    quantapdf_pdf_form_model **out_model);
quantapdf_status quantapdf_qpdf_edit_form_ref_at(
    quantapdf_qpdf_edit *edit,
    size_t field_index,
    quantapdf_form_field_ref *out_ref);
quantapdf_status quantapdf_qpdf_edit_form_set_values(
    quantapdf_qpdf_edit *edit,
    const quantapdf_form_field_ref *ref,
    const quantapdf_form_value_update *update,
    int *test_fault);
quantapdf_status quantapdf_qpdf_edit_annotation_count(
    quantapdf_qpdf_edit *edit,
    int page_index,
    size_t *out_count);
quantapdf_status quantapdf_qpdf_edit_annotation_ref_at(
    quantapdf_qpdf_edit *edit,
    int page_index,
    size_t index,
    quantapdf_annotation_ref *out_ref);
quantapdf_status quantapdf_qpdf_edit_annotation_get_info(
    quantapdf_qpdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    quantapdf_annotation_info *out_info);
quantapdf_status quantapdf_qpdf_edit_annotation_contents(
    quantapdf_qpdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    char **out_utf8,
    size_t *out_size);
quantapdf_status quantapdf_qpdf_edit_annotation_create(
    quantapdf_qpdf_edit *edit,
    int page_index,
    const quantapdf_annotation_create_options *options,
    quantapdf_annotation_ref *out_ref,
    int *test_fault);
quantapdf_status quantapdf_qpdf_edit_annotation_update(
    quantapdf_qpdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    const quantapdf_annotation_update *update,
    int *test_fault);
quantapdf_status quantapdf_qpdf_edit_annotation_delete(
    quantapdf_qpdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    int *test_fault);
void quantapdf_qpdf_edit_drop(quantapdf_qpdf_edit *edit);

#ifdef __cplusplus
}
#endif

#endif
