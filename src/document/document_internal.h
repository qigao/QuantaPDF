#ifndef QUANTAPDF_DOCUMENT_INTERNAL_H
#define QUANTAPDF_DOCUMENT_INTERNAL_H

#include <quantapdf/quantapdf.h>
#include "../core/context_internal.h"

struct quantapdf_document {
    size_t references;
    quantapdf_context *context;
    unsigned char *source_data;
    size_t source_size;
    const quantapdf_backend_ops *ops;
    void *backend;
    char *password;
    size_t password_size;
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
