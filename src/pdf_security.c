#include "internal.h"

#include <stdlib.h>

quantapdf_status quantapdf_document_audit(
    quantapdf_document *document,
    quantapdf_audit_result *out_result)
{
    if (out_result == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (out_result->struct_size >= QUANTAPDF_AUDIT_RESULT_V1_MIN_SIZE)
        out_result->findings = 0;
    if (document == NULL ||
        out_result->struct_size < QUANTAPDF_AUDIT_RESULT_V1_MIN_SIZE)
        return QUANTAPDF_ERROR_ARGUMENT;
    return document->ops->document_audit(
        document->backend, &out_result->findings);
}

quantapdf_status quantapdf_sanitize(
    quantapdf_document *document,
    uint32_t flags,
    quantapdf_output **out_output)
{
    quantapdf_output *output;
    quantapdf_status status;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (document == NULL || document->backend == NULL || flags == 0 ||
        (flags & ~QUANTAPDF_SANITIZE_ALL) != 0)
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_output_create_internal(document->context, &output);
    if (status != QUANTAPDF_OK)
        return status;
    status = document->ops->sanitize(
        document->backend,
        flags,
        &output->data,
        &output->size);
    if (status != QUANTAPDF_OK) {
        quantapdf_drop_output(output);
        return status;
    }
    *out_output = output;
    return QUANTAPDF_OK;
}
