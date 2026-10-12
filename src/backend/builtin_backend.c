#include "document_backend.h"
#include "pdfium_document.h"
#include "qpdf_document.h"
#include "qpdf_graph_write.h"
#include "qpdf_edit.h"

#include <stdlib.h>

typedef struct quantapdf_builtin_document {
    quantapdf_pdfium_document *reader;
    quantapdf_qpdf_document *writer;
} quantapdf_builtin_document;

static void builtin_close(void *document)
{
    quantapdf_builtin_document *state = document;
    if (state == NULL)
        return;
    quantapdf_pdfium_close(state->reader);
    quantapdf_qpdf_close(state->writer);
    free(state);
}

static quantapdf_status builtin_open_memory(
    const unsigned char *data,
    size_t size,
    const char *password_utf8,
    void **out_document)
{
    quantapdf_builtin_document *state;
    quantapdf_status status;

    if (out_document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_document = NULL;
    if (data == NULL || size == 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    state = (quantapdf_builtin_document *)calloc(1, sizeof(*state));
    if (state == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    status = quantapdf_pdfium_open_memory(
        data, size, password_utf8, &state->reader);
    if (status == QUANTAPDF_OK) {
        status = quantapdf_qpdf_open_memory(
            data, size, password_utf8 == NULL ? "" : password_utf8,
            &state->writer);
    }
    if (status != QUANTAPDF_OK) {
        builtin_close(state);
        return status;
    }
    *out_document = state;
    return QUANTAPDF_OK;
}

static quantapdf_status builtin_pdfium_page_count(
    void *document,
    int *out_page_count)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_pdfium_page_count(state->reader, out_page_count);
}

static quantapdf_status builtin_pdfium_load_page(
    void *document, int page_index, void **out_page)
{
    quantapdf_builtin_document *state = document;
    quantapdf_pdfium_page *page = NULL;
    quantapdf_status status;

    if (out_page == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_page = NULL;
    status = quantapdf_pdfium_load_page(state->reader, page_index, &page);
    if (status == QUANTAPDF_OK)
        *out_page = page;
    return status;
}

static quantapdf_status builtin_pdfium_page_bounds(
    void *page,
    quantapdf_rect *out_bounds)
{
    return quantapdf_pdfium_page_bounds(page, out_bounds);
}

static quantapdf_status builtin_pdfium_render_page(
    void *page,
    float dpi,
    float rotation_degrees,
    const quantapdf_rect *clip,
    double user_unit,
    int alpha,
    quantapdf_backend_bitmap *out_bitmap)
{
    return quantapdf_pdfium_render_page(
        page, dpi, rotation_degrees, clip, user_unit, alpha, out_bitmap);
}

static quantapdf_status builtin_pdfium_extract_text(
    void *page,
    char **out_utf8,
    size_t *out_size)
{
    return quantapdf_pdfium_extract_text(page, out_utf8, out_size);
}

static quantapdf_status builtin_pdfium_extract_structured_text(
    void *page,
    quantapdf_text_page **out_text)
{
    return quantapdf_pdfium_extract_structured_text(page, out_text);
}

static quantapdf_status builtin_pdfium_extract_images(
    void *page,
    quantapdf_image_page **out_images)
{
    return quantapdf_pdfium_extract_images(page, out_images);
}

static quantapdf_status builtin_pdfium_extract_links(
    void *page,
    quantapdf_link_page **out_links)
{
    return quantapdf_pdfium_extract_links(page, out_links);
}

static void builtin_pdfium_drop_page(
    void *page)
{
    quantapdf_pdfium_drop_page(page);
}

static quantapdf_status builtin_qpdf_page_user_unit(
    void *document,
    int page_index,
    double *out_user_unit)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_page_user_unit(state->writer, page_index, out_user_unit);
}

static quantapdf_status builtin_qpdf_page_box_bounds(
    void *document,
    int page_index,
    quantapdf_page_box box,
    quantapdf_rect *out_bounds)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_page_box_bounds(state->writer, page_index, box, out_bounds);
}

static quantapdf_status builtin_qpdf_extract_annotations(
    void *document,
    int page_index,
    quantapdf_annotation_page **out_annotations)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_extract_annotations(
        state->writer, page_index, out_annotations);
}

static quantapdf_status builtin_qpdf_extract_form(
    void *document,
    quantapdf_pdf_form_model **out_model)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_extract_form(state->writer, out_model);
}

static quantapdf_status builtin_qpdf_metadata(
    void *document,
    quantapdf_metadata_field field,
    char **out_utf8,
    size_t *out_size)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_metadata(state->writer, field, out_utf8, out_size);
}

static quantapdf_status builtin_qpdf_outline(
    void *document,
    quantapdf_outline **out_outline)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_outline(state->writer, out_outline);
}

static quantapdf_status builtin_qpdf_export_pages(
    void *document,
    const int *page_indices,
    size_t page_count,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_export_pages(
        state->writer, page_indices, page_count, out_data, out_size);
}

static quantapdf_status builtin_qpdf_crop_pages(
    void *document,
    const quantapdf_page_crop *crops,
    size_t crop_count,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_crop_pages(
        state->writer, crops, crop_count, out_data, out_size);
}

static quantapdf_status builtin_qpdf_trim_pages(
    void *document,
    const quantapdf_page_trim *trims,
    size_t trim_count,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_trim_pages(
        state->writer, trims, trim_count, out_data, out_size);
}

static quantapdf_status builtin_qpdf_poster_split_pages(
    void *document,
    const quantapdf_page_poster_split *splits,
    size_t split_count,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_poster_split_pages(
        state->writer, splits, split_count, out_data, out_size);
}

static quantapdf_status builtin_qpdf_rewrite_lossless(
    void *document,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_rewrite_lossless(state->writer, out_data, out_size);
}

static quantapdf_status builtin_qpdf_recompress_images(
    void *document,
    int jpeg_quality,
    size_t max_decoded_bytes_per_image,
    int test_fault,
    quantapdf_backend_image_recompression_test_stats *test_stats,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_recompress_images(
        state->writer, jpeg_quality, max_decoded_bytes_per_image,
        test_fault, test_stats, out_data, out_size);
}

static quantapdf_status builtin_qpdf_document_audit(
    void *document,
    uint32_t *out_findings)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_document_audit(state->writer, out_findings);
}

static quantapdf_status builtin_qpdf_sanitize(
    void *document,
    uint32_t flags,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_sanitize(state->writer, flags, out_data, out_size);
}

static quantapdf_status builtin_qpdf_encrypt_pdf(
    void *document,
    const quantapdf_encryption_options *options,
    int test_fault,
    quantapdf_backend_security_test_stats *test_stats,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_encrypt_pdf(
        state->writer, options, test_fault, test_stats, out_data, out_size);
}

static quantapdf_status builtin_qpdf_decrypt_pdf(
    void *document,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_decrypt_pdf(state->writer, out_data, out_size);
}

static quantapdf_status builtin_qpdf_reencrypt_pdf(
    void *document,
    const quantapdf_encryption_options *options,
    int test_fault,
    quantapdf_backend_security_test_stats *test_stats,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_reencrypt_pdf(
        state->writer, options, test_fault, test_stats, out_data, out_size);
}

static quantapdf_status builtin_qpdf_flatten_interactive(
    void *document,
    uint32_t flags,
    unsigned char **out_data,
    size_t *out_size)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_flatten_interactive(
        state->writer, flags, out_data, out_size);
}

static quantapdf_status builtin_edit_begin(void *document, void **out_edit)
{
    quantapdf_builtin_document *state = document;
    quantapdf_qpdf_edit *edit = NULL;
    quantapdf_status status;

    if (out_edit == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_edit = NULL;
    status = quantapdf_qpdf_edit_begin(state->writer, &edit);
    if (status == QUANTAPDF_OK)
        *out_edit = edit;
    return status;
}

static quantapdf_status builtin_edit_snapshot(
    void *edit,
    int *test_fault,
    unsigned char **out_data,
    size_t *out_size)
{
    return quantapdf_qpdf_edit_snapshot(edit, test_fault, out_data, out_size);
}

static quantapdf_status builtin_edit_form_snapshot(
    void *edit,
    quantapdf_pdf_form_model **out_model)
{
    return quantapdf_qpdf_edit_form_snapshot(edit, out_model);
}

static quantapdf_status builtin_edit_form_ref_at(
    void *edit,
    size_t field_index,
    quantapdf_form_field_ref *out_ref)
{
    return quantapdf_qpdf_edit_form_ref_at(edit, field_index, out_ref);
}

static quantapdf_status builtin_edit_form_set_values(
    void *edit,
    const quantapdf_form_field_ref *ref,
    const quantapdf_form_value_update *update,
    int *test_fault)
{
    return quantapdf_qpdf_edit_form_set_values(edit, ref, update, test_fault);
}

static quantapdf_status builtin_edit_annotation_count(
    void *edit,
    int page_index,
    size_t *out_count)
{
    return quantapdf_qpdf_edit_annotation_count(edit, page_index, out_count);
}

static quantapdf_status builtin_edit_annotation_ref_at(
    void *edit,
    int page_index,
    size_t index,
    quantapdf_annotation_ref *out_ref)
{
    return quantapdf_qpdf_edit_annotation_ref_at(edit, page_index, index, out_ref);
}

static quantapdf_status builtin_edit_annotation_get_info(
    void *edit,
    const quantapdf_annotation_ref *ref,
    quantapdf_annotation_info *out_info)
{
    return quantapdf_qpdf_edit_annotation_get_info(edit, ref, out_info);
}

static quantapdf_status builtin_edit_annotation_contents(
    void *edit,
    const quantapdf_annotation_ref *ref,
    char **out_utf8,
    size_t *out_size)
{
    return quantapdf_qpdf_edit_annotation_contents(edit, ref, out_utf8, out_size);
}

static quantapdf_status builtin_edit_annotation_create(
    void *edit,
    int page_index,
    const quantapdf_annotation_create_options *options,
    quantapdf_annotation_ref *out_ref,
    int *test_fault)
{
    return quantapdf_qpdf_edit_annotation_create(
        edit, page_index, options, out_ref, test_fault);
}

static quantapdf_status builtin_edit_annotation_update(
    void *edit,
    const quantapdf_annotation_ref *ref,
    const quantapdf_annotation_update *update,
    int *test_fault)
{
    return quantapdf_qpdf_edit_annotation_update(edit, ref, update, test_fault);
}

static quantapdf_status builtin_edit_annotation_delete(
    void *edit,
    const quantapdf_annotation_ref *ref)
{
    return quantapdf_qpdf_edit_annotation_delete(edit, ref);
}

static void builtin_edit_drop(
    void *edit)
{
    quantapdf_qpdf_edit_drop(edit);
}

static const quantapdf_backend_edit_ops builtin_edit_ops = {
    .snapshot = builtin_edit_snapshot,
    .form_snapshot = builtin_edit_form_snapshot,
    .form_ref_at = builtin_edit_form_ref_at,
    .form_set_values = builtin_edit_form_set_values,
    .annotation_count = builtin_edit_annotation_count,
    .annotation_ref_at = builtin_edit_annotation_ref_at,
    .annotation_get_info = builtin_edit_annotation_get_info,
    .annotation_contents = builtin_edit_annotation_contents,
    .annotation_create = builtin_edit_annotation_create,
    .annotation_update = builtin_edit_annotation_update,
    .annotation_delete = builtin_edit_annotation_delete,
    .drop = builtin_edit_drop,
};

static quantapdf_status builtin_capture_graph(void *document,
    const quantapdf_graph_limits *limits, quantapdf_graph **out_graph)
{
    quantapdf_builtin_document *state = document;
    return quantapdf_qpdf_capture_graph(state->writer, limits, out_graph);
}

static const quantapdf_backend_ops builtin_ops = {
    .open_memory = builtin_open_memory,
    .close = builtin_close,
    .page_count = builtin_pdfium_page_count,
    .load_page = builtin_pdfium_load_page,
    .page_bounds = builtin_pdfium_page_bounds,
    .render_page = builtin_pdfium_render_page,
    .extract_text = builtin_pdfium_extract_text,
    .extract_structured_text = builtin_pdfium_extract_structured_text,
    .extract_images = builtin_pdfium_extract_images,
    .extract_links = builtin_pdfium_extract_links,
    .drop_page = builtin_pdfium_drop_page,
    .page_user_unit = builtin_qpdf_page_user_unit,
    .page_box_bounds = builtin_qpdf_page_box_bounds,
    .extract_annotations = builtin_qpdf_extract_annotations,
    .extract_form = builtin_qpdf_extract_form,
    .metadata = builtin_qpdf_metadata,
    .outline = builtin_qpdf_outline,
    .export_pages = builtin_qpdf_export_pages,
    .merge_memory = quantapdf_qpdf_merge_memory,
    .crop_pages = builtin_qpdf_crop_pages,
    .trim_pages = builtin_qpdf_trim_pages,
    .poster_split_pages = builtin_qpdf_poster_split_pages,
    .rewrite_lossless = builtin_qpdf_rewrite_lossless,
    .recompress_images = builtin_qpdf_recompress_images,
    .document_audit = builtin_qpdf_document_audit,
    .sanitize = builtin_qpdf_sanitize,
    .encrypt_pdf = builtin_qpdf_encrypt_pdf,
    .decrypt_pdf = builtin_qpdf_decrypt_pdf,
    .reencrypt_pdf = builtin_qpdf_reencrypt_pdf,
    .flatten_interactive = builtin_qpdf_flatten_interactive,
    .edit_begin = builtin_edit_begin,
    .edit_ops = &builtin_edit_ops,
    .capture_graph = builtin_capture_graph,
    .write_graph = quantapdf_qpdf_write_graph,
};

const quantapdf_backend_ops *quantapdf_builtin_backend_ops(void)
{
    return &builtin_ops;
}
