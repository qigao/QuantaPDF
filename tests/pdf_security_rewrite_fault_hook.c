#include "pdf_security_rewrite_test_api.h"

#include "document/document_internal.h"

void quantapdf_security_test_set_fault(
    quantapdf_document *document,
    int fault)
{
    if (document != NULL)
        document->test_security_fault = fault;
}

void quantapdf_security_test_get_random_context_stats(
    const quantapdf_document *document,
    size_t *out_context_entries,
    size_t *out_configure_requests,
    size_t *out_write_requests,
    size_t *out_context_exits)
{
    if (out_context_entries != NULL)
        *out_context_entries = document == NULL
            ? 0u : document->test_security_context_entries;
    if (out_configure_requests != NULL)
        *out_configure_requests = document == NULL
            ? 0u : document->test_security_configure_requests;
    if (out_write_requests != NULL)
        *out_write_requests = document == NULL
            ? 0u : document->test_security_write_requests;
    if (out_context_exits != NULL)
        *out_context_exits = document == NULL
            ? 0u : document->test_security_context_exits;
}
