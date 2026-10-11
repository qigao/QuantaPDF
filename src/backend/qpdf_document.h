#ifndef QUANTAPDF_BACKEND_QPDF_DOCUMENT_H
#define QUANTAPDF_BACKEND_QPDF_DOCUMENT_H

#include <stddef.h>

#include <quantapdf/quantapdf.h>
#include "document_backend.h"
#include "graph/graph.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct quantapdf_qpdf_document quantapdf_qpdf_document;
typedef struct quantapdf_annotation_page quantapdf_annotation_page;
typedef struct quantapdf_pdf_form_model quantapdf_pdf_form_model;
typedef struct quantapdf_outline quantapdf_outline;

quantapdf_status quantapdf_qpdf_capture_graph(
    quantapdf_qpdf_document *document, const quantapdf_graph_limits *limits,
    quantapdf_graph **out_graph);

static inline quantapdf_status quantapdf_qpdf_compute_work_budget_limit(
    size_t source_size,
    size_t *out_limit)
{
    if (out_limit == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (source_size > SIZE_MAX / 64u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    *out_limit = source_size * 64u < 4096u
        ? 4096u
        : source_size * 64u;
    return QUANTAPDF_OK;
}

typedef quantapdf_backend_image_recompression_test_stats
    quantapdf_qpdf_image_recompression_test_stats;
typedef quantapdf_backend_security_test_stats quantapdf_qpdf_security_test_stats;

quantapdf_status quantapdf_qpdf_open_memory(
    const unsigned char *data,
    size_t size,
    const char *password_utf8,
    quantapdf_qpdf_document **out_document);

quantapdf_status quantapdf_qpdf_page_count(
    quantapdf_qpdf_document *document,
    int *out_page_count);

quantapdf_status quantapdf_qpdf_page_user_unit(
    quantapdf_qpdf_document *document,
    int page_index,
    double *out_user_unit);

quantapdf_status quantapdf_qpdf_page_box_bounds(
    quantapdf_qpdf_document *document,
    int page_index,
    quantapdf_page_box box,
    quantapdf_rect *out_bounds);

quantapdf_status quantapdf_qpdf_extract_annotations(
    quantapdf_qpdf_document *document,
    int page_index,
    quantapdf_annotation_page **out_annotations);

quantapdf_status quantapdf_qpdf_extract_form(
    quantapdf_qpdf_document *document,
    quantapdf_pdf_form_model **out_model);

quantapdf_status quantapdf_qpdf_metadata(
    quantapdf_qpdf_document *document,
    quantapdf_metadata_field field,
    char **out_utf8,
    size_t *out_size);

quantapdf_status quantapdf_qpdf_outline(
    quantapdf_qpdf_document *document,
    quantapdf_outline **out_outline);

quantapdf_status quantapdf_qpdf_export_pages(
    quantapdf_qpdf_document *document,
    const int *page_indices,
    size_t page_count,
    unsigned char **out_data,
    size_t *out_size);

quantapdf_status quantapdf_qpdf_merge_memory(
    const unsigned char *const *input_data,
    const size_t *input_sizes,
    size_t input_count,
    unsigned char **out_data,
    size_t *out_size);

quantapdf_status quantapdf_qpdf_crop_pages(
    quantapdf_qpdf_document *document,
    const quantapdf_page_crop *crops,
    size_t crop_count,
    unsigned char **out_data,
    size_t *out_size);

quantapdf_status quantapdf_qpdf_trim_pages(
    quantapdf_qpdf_document *document,
    const quantapdf_page_trim *trims,
    size_t trim_count,
    unsigned char **out_data,
    size_t *out_size);

quantapdf_status quantapdf_qpdf_poster_split_pages(
    quantapdf_qpdf_document *document,
    const quantapdf_page_poster_split *splits,
    size_t split_count,
    unsigned char **out_data,
    size_t *out_size);

quantapdf_status quantapdf_qpdf_rewrite_memory(
    const unsigned char *data,
    size_t size,
    unsigned char **out_data,
    size_t *out_size);

quantapdf_status quantapdf_qpdf_rewrite_lossless(
    quantapdf_qpdf_document *document,
    unsigned char **out_data,
    size_t *out_size);

quantapdf_status quantapdf_qpdf_recompress_images(
    quantapdf_qpdf_document *document,
    int jpeg_quality,
    size_t max_decoded_bytes_per_image,
    int test_fault,
    quantapdf_qpdf_image_recompression_test_stats *test_stats,
    unsigned char **out_data,
    size_t *out_size);

quantapdf_status quantapdf_qpdf_document_audit(
    quantapdf_qpdf_document *document,
    uint32_t *out_findings);

quantapdf_status quantapdf_qpdf_sanitize(
    quantapdf_qpdf_document *document,
    uint32_t flags,
    unsigned char **out_data,
    size_t *out_size);

quantapdf_status quantapdf_qpdf_encrypt_pdf(
    quantapdf_qpdf_document *document,
    const quantapdf_encryption_options *options,
    int test_fault,
    quantapdf_qpdf_security_test_stats *test_stats,
    unsigned char **out_data,
    size_t *out_size);

quantapdf_status quantapdf_qpdf_decrypt_pdf(
    quantapdf_qpdf_document *document,
    unsigned char **out_data,
    size_t *out_size);

quantapdf_status quantapdf_qpdf_reencrypt_pdf(
    quantapdf_qpdf_document *document,
    const quantapdf_encryption_options *options,
    int test_fault,
    quantapdf_qpdf_security_test_stats *test_stats,
    unsigned char **out_data,
    size_t *out_size);

quantapdf_status quantapdf_qpdf_flatten_interactive(
    quantapdf_qpdf_document *document,
    uint32_t flags,
    unsigned char **out_data,
    size_t *out_size);

void quantapdf_qpdf_close(quantapdf_qpdf_document *document);

#ifdef __cplusplus
}
#endif

#endif
