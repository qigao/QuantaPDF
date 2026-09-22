#ifndef QUANTAPDF_INTERNAL_H
#define QUANTAPDF_INTERNAL_H

#include <quantapdf/quantapdf.h>

#include "text_snapshot.h"
#include "image_snapshot.h"
#include "link_snapshot.h"
#include "annotation_snapshot.h"
#include "outline_snapshot.h"

typedef struct quantapdf_pdfium_document quantapdf_pdfium_document;
typedef struct quantapdf_pdfium_page quantapdf_pdfium_page;
typedef struct quantapdf_qpdf_document quantapdf_qpdf_document;

#if defined(QUANTAPDF_TESTING)
typedef enum quantapdf_test_poster_fault_internal {
    QUANTAPDF_TEST_POSTER_FAULT_NONE = 0,
    QUANTAPDF_TEST_POSTER_FAULT_ANNOTATION_PREFLIGHT = 1,
    QUANTAPDF_TEST_POSTER_FAULT_WIDGET_PREFLIGHT = 2,
    QUANTAPDF_TEST_POSTER_FAULT_NAVIGATION_PREFLIGHT = 3
} quantapdf_test_poster_fault_internal;

typedef enum quantapdf_test_security_fault_internal {
    QUANTAPDF_TEST_SECURITY_FAULT_NONE = 0,
    QUANTAPDF_TEST_SECURITY_FAULT_ENTROPY_CONFIGURE = 1,
    QUANTAPDF_TEST_SECURITY_FAULT_ENTROPY_WRITE = 2,
    QUANTAPDF_TEST_SECURITY_FAULT_OUTPUT_NOMEM = 3,
    QUANTAPDF_TEST_SECURITY_FAULT_BEFORE_PUBLICATION = 4
} quantapdf_test_security_fault_internal;
#endif

struct quantapdf_document {
    unsigned char *source_data;
    size_t source_size;
    quantapdf_pdfium_document *pdfium_document;
    quantapdf_qpdf_document *qpdf_document;
    char *password;
    size_t password_size;
#if defined(QUANTAPDF_TESTING)
    int test_poster_fault;
    size_t test_image_unique_count;
    size_t test_image_provider_registrations;
    size_t test_image_provider_invocations;
    size_t test_image_decoded_preflight_bytes;
    int test_image_every_provider_once;
    int test_image_fault;
    int test_security_fault;
    size_t test_security_context_entries;
    size_t test_security_configure_requests;
    size_t test_security_write_requests;
    size_t test_security_context_exits;
#endif
};

struct quantapdf_page {
    quantapdf_document *document;
    quantapdf_pdfium_page *pdfium_page;
    int page_index;
};

struct quantapdf_bitmap {
    unsigned char *data;
    size_t size;
    int width;
    int height;
    int stride;
    int components;
};

struct quantapdf_output {
    unsigned char *data;
    size_t size;
};

typedef struct quantapdf_composer_page_state {
    float width_points;
    float height_points;
    uint32_t background_argb;
} quantapdf_composer_page_state;

typedef enum quantapdf_composer_operation_kind {
    QUANTAPDF_COMPOSER_OPERATION_TEXT = 1,
    QUANTAPDF_COMPOSER_OPERATION_IMAGE = 2,
    QUANTAPDF_COMPOSER_OPERATION_PATH = 3
} quantapdf_composer_operation_kind;

typedef struct quantapdf_composer_text_operation {
    char *text_utf8;
    quantapdf_composer_text_options options;
} quantapdf_composer_text_operation;

typedef enum quantapdf_composer_image_format_internal {
    QUANTAPDF_COMPOSER_IMAGE_FORMAT_JPEG = 1,
    QUANTAPDF_COMPOSER_IMAGE_FORMAT_PNG = 2
} quantapdf_composer_image_format_internal;

typedef struct quantapdf_composer_image_state {
    unsigned char *data;
    size_t size;
    unsigned char *alpha_data;
    size_t alpha_size;
    uint32_t width;
    uint32_t height;
    int components;
    int has_alpha;
    quantapdf_composer_image_format_internal format;
} quantapdf_composer_image_state;

typedef struct quantapdf_composer_image_operation {
    quantapdf_composer_image_id image_id;
    quantapdf_composer_image_options options;
} quantapdf_composer_image_operation;

typedef struct quantapdf_composer_path_operation {
    quantapdf_composer_path_command *commands;
    size_t command_count;
    quantapdf_composer_path_options options;
} quantapdf_composer_path_operation;

typedef struct quantapdf_composer_operation {
    quantapdf_composer_operation_kind kind;
    size_t page_index;
    quantapdf_rect bounds;
    union {
        quantapdf_composer_text_operation text;
        quantapdf_composer_image_operation image;
        quantapdf_composer_path_operation path;
    } value;
} quantapdf_composer_operation;

struct quantapdf_composer {
    size_t max_pages;
    size_t max_operations;
    size_t max_resource_bytes;
    quantapdf_composer_page_state *pages;
    size_t page_count;
    size_t page_capacity;
    quantapdf_composer_operation *operations;
    size_t operation_count;
    size_t operation_capacity;
    quantapdf_composer_image_state *images;
    size_t image_count;
    size_t image_capacity;
    size_t resource_bytes;
};

quantapdf_status quantapdf_document_page_user_unit(
    quantapdf_document *document,
    int page_index,
    double *out_user_unit);
#endif
