#include "backend_internal.h"
#include "backend_ops_internal.h"
#include "providers/backend_provider_internal.h"

/*
 * Backend selection bridge.
 *
 * The first migration stage keeps the existing provider implementations
 * unchanged while routing future document ownership through a single
 * provider-neutral backend object.
 */

const quantapdf_backend_ops *
quantapdf_backend_ops_for_default_provider_internal(void)
{
    return quantapdf_pdfium_provider_ops_internal();
}

quantapdf_status
quantapdf_backend_create_internal(quantapdf_backend **out_backend)
{
    if (out_backend == NULL) {
        return QUANTAPDF_STATUS_INVALID_ARGUMENT;
    }

    *out_backend = NULL;

    return QUANTAPDF_STATUS_OK;
}

void
quantapdf_backend_destroy_internal(quantapdf_backend *backend)
{
    (void)backend;
}
