#ifndef QUANTAPDF_DOCUMENT_INTERNAL_H
#define QUANTAPDF_DOCUMENT_INTERNAL_H

#include <quantapdf/quantapdf.h>
#include "../core/context_internal.h"

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
    quantapdf_context *context;
    unsigned char *source_data;
    size_t source_size;
    const quantapdf_backend_ops *ops;
    void *backend;
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
    quantapdf_context *context;
    quantapdf_document *document;
    void *backend;
    int page_index;
};

#ifdef __cplusplus
extern "C" {
#endif

quantapdf_status quantapdf_document_page_user_unit(
    quantapdf_document *document,
    int page_index,
    double *out_user_unit);

#ifdef __cplusplus
}
#endif

#endif
