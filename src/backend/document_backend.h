#ifndef QUANTAPDF_BACKEND_DOCUMENT_BACKEND_H
#define QUANTAPDF_BACKEND_DOCUMENT_BACKEND_H

#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct quantapdf_pdf_form_model quantapdf_pdf_form_model;
typedef struct quantapdf_graph quantapdf_graph;
typedef struct quantapdf_graph_limits quantapdf_graph_limits;
typedef struct quantapdf_graph_write_limits quantapdf_graph_write_limits;
typedef struct quantapdf_graph_stream_limits quantapdf_graph_stream_limits;
typedef struct quantapdf_graph_reference quantapdf_graph_reference;

/* Snapshot handles returned by providers have a zeroed, unbound snapshot owner.
 * The facade holds the context guard and binds the finalizer before publication.
 * On failure providers release partial payloads and leave outputs empty. */

/* Successful rendering transfers malloc-owned samples to the C facade.
 * Failure leaves an empty bitmap; the provider cleans any partial allocation. */
typedef struct quantapdf_backend_bitmap {
    unsigned char *data;
    size_t size;
    int width;
    int height;
    int stride;
    int components;
} quantapdf_backend_bitmap;

typedef struct quantapdf_backend_image_recompression_test_stats {
    size_t unique_images;
    size_t provider_registrations;
    size_t provider_invocations;
    size_t decoded_preflight_bytes;
    int every_provider_once;
} quantapdf_backend_image_recompression_test_stats;

typedef struct quantapdf_backend_security_test_stats {
    size_t context_entries;
    size_t configure_requests;
    size_t write_requests;
    size_t context_exits;
} quantapdf_backend_security_test_stats;

/* An edit owns its independent state and retains this immutable table.
 * Neither state nor table may borrow the source document after begin succeeds. */
typedef struct quantapdf_backend_edit_ops {
    quantapdf_status (*snapshot)(
        void *edit,
        int *test_fault,
        unsigned char **out_data,
        size_t *out_size);
    quantapdf_status (*form_snapshot)(
        void *edit,
        quantapdf_pdf_form_model **out_model);
    quantapdf_status (*form_ref_at)(
        void *edit,
        size_t field_index,
        quantapdf_form_field_ref *out_ref);
    quantapdf_status (*form_set_values)(
        void *edit,
        const quantapdf_form_field_ref *ref,
        const quantapdf_form_value_update *update,
        int *test_fault);
    quantapdf_status (*annotation_count)(
        void *edit,
        int page_index,
        size_t *out_count);
    quantapdf_status (*annotation_ref_at)(
        void *edit,
        int page_index,
        size_t index,
        quantapdf_annotation_ref *out_ref);
    quantapdf_status (*annotation_get_info)(
        void *edit,
        const quantapdf_annotation_ref *ref,
        quantapdf_annotation_info *out_info);
    quantapdf_status (*annotation_contents)(
        void *edit,
        const quantapdf_annotation_ref *ref,
        char **out_utf8,
        size_t *out_size);
    quantapdf_status (*annotation_create)(
        void *edit,
        int page_index,
        const quantapdf_annotation_create_options *options,
        quantapdf_annotation_ref *out_ref,
        int *test_fault);
    quantapdf_status (*annotation_update)(
        void *edit,
        const quantapdf_annotation_ref *ref,
        const quantapdf_annotation_update *update,
        int *test_fault);
    quantapdf_status (*annotation_delete)(
        void *edit,
        const quantapdf_annotation_ref *ref);
    void (*drop)(
        void *edit);
} quantapdf_backend_edit_ops;

/* Private, trusted C dispatch contract, not a plugin/public ABI.
 * Tables are immutable and outlive their document, pages, and edits. Every
 * callback is required. Existing argument validation remains with its facade
 * or bridge owner; dispatch does not change error semantics.
 * Open borrows source bytes/password until close; successful load_page returns
 * a page that borrows its document. Drop pages before closing that document.
 * Snapshot and transform output ownership follows the existing C11 facade.
 * All callbacks execute under the existing externally serialized v2 contract. */
typedef struct quantapdf_backend_ops {
    quantapdf_status (*open_memory)(
        const unsigned char *data,
        size_t size,
        const char *password_utf8,
        void **out_document);
    void (*close)(void *document);
    quantapdf_status (*page_count)(
        void *document,
        int *out_page_count);
    quantapdf_status (*load_page)(
        void *document,
        int page_index,
        void **out_page);
    quantapdf_status (*page_bounds)(
        void *page,
        quantapdf_rect *out_bounds);
    quantapdf_status (*extract_text)(
        void *page,
        char **out_utf8,
        size_t *out_size);
    quantapdf_status (*extract_structured_text)(
        void *page,
        quantapdf_text_page **out_text);
    quantapdf_status (*extract_images)(
        void *page,
        quantapdf_image_page **out_images);
    quantapdf_status (*extract_links)(
        void *page,
        quantapdf_link_page **out_links);
    void (*drop_page)(
        void *page);
    quantapdf_status (*page_user_unit)(
        void *document,
        int page_index,
        double *out_user_unit);
    quantapdf_status (*page_box_bounds)(
        void *document,
        int page_index,
        quantapdf_page_box box,
        quantapdf_rect *out_bounds);
    quantapdf_status (*extract_annotations)(
        void *document,
        int page_index,
        quantapdf_annotation_page **out_annotations);
    quantapdf_status (*extract_form)(
        void *document,
        quantapdf_pdf_form_model **out_model);
    quantapdf_status (*metadata)(
        void *document,
        quantapdf_metadata_field field,
        char **out_utf8,
        size_t *out_size);
    quantapdf_status (*outline)(
        void *document,
        quantapdf_outline **out_outline);
    quantapdf_status (*export_pages)(
        void *document,
        const int *page_indices,
        size_t page_count,
        unsigned char **out_data,
        size_t *out_size);
    quantapdf_status (*merge_memory)(
        const unsigned char *const *input_data,
        const size_t *input_sizes,
        size_t input_count,
        unsigned char **out_data,
        size_t *out_size);
    quantapdf_status (*crop_pages)(
        void *document,
        const quantapdf_page_crop *crops,
        size_t crop_count,
        unsigned char **out_data,
        size_t *out_size);
    quantapdf_status (*trim_pages)(
        void *document,
        const quantapdf_page_trim *trims,
        size_t trim_count,
        unsigned char **out_data,
        size_t *out_size);
    quantapdf_status (*poster_split_pages)(
        void *document,
        const quantapdf_page_poster_split *splits,
        size_t split_count,
        unsigned char **out_data,
        size_t *out_size);
    quantapdf_status (*rewrite_lossless)(
        void *document,
        unsigned char **out_data,
        size_t *out_size);
    quantapdf_status (*recompress_images)(
        void *document,
        int jpeg_quality,
        size_t max_decoded_bytes_per_image,
        int test_fault,
        quantapdf_backend_image_recompression_test_stats *test_stats,
        unsigned char **out_data,
        size_t *out_size);
    quantapdf_status (*document_audit)(
        void *document,
        uint32_t *out_findings);
    quantapdf_status (*sanitize)(
        void *document,
        uint32_t flags,
        unsigned char **out_data,
        size_t *out_size);
    quantapdf_status (*encrypt_pdf)(
        void *document,
        const quantapdf_encryption_options *options,
        int test_fault,
        quantapdf_backend_security_test_stats *test_stats,
        unsigned char **out_data,
        size_t *out_size);
    quantapdf_status (*decrypt_pdf)(
        void *document,
        unsigned char **out_data,
        size_t *out_size);
    quantapdf_status (*reencrypt_pdf)(
        void *document,
        const quantapdf_encryption_options *options,
        int test_fault,
        quantapdf_backend_security_test_stats *test_stats,
        unsigned char **out_data,
        size_t *out_size);
    quantapdf_status (*flatten_interactive)(
        void *document,
        uint32_t flags,
        unsigned char **out_data,
        size_t *out_size);
    quantapdf_status (*edit_begin)(void *document, void **out_edit);
    const quantapdf_backend_edit_ops *edit_ops;
    /* Transfers a sealed, fully copied graph. No source/provider borrow. */
    quantapdf_status (*capture_graph)(void *document,
        const quantapdf_graph_limits *limits, quantapdf_graph **out_graph);
    /* Borrows a sealed graph; transfers detached malloc-owned output bytes. */
    quantapdf_status (*write_graph)(const quantapdf_graph *graph,
        const quantapdf_graph_write_limits *limits, unsigned char **out_data,
        size_t *out_size);
    /* Input is borrowed PDF text bytes; output is detached malloc-owned UTF-8. */
    quantapdf_status (*decode_pdf_text)(const unsigned char *data, size_t size,
        char **out_utf8, size_t *out_size);
    /* Selected immutable stream -> detached non-lossy decoded bytes. */
    quantapdf_status (*read_graph_stream)(const quantapdf_graph *graph,
        const quantapdf_graph_reference *reference, const quantapdf_graph_stream_limits *limits,
        unsigned char **out_data, size_t *out_size);
} quantapdf_backend_ops;

/* One built-in QPDF structure provider; public Raster uses owned native IR. */
const quantapdf_backend_ops *quantapdf_builtin_backend_ops(void);

#ifdef __cplusplus
}
#endif

#endif
