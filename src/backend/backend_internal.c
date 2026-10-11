#include "backend_ops_internal.h"

/*
 * Internal backend lifecycle bridge.
 *
 * This file intentionally contains no PDFium/qpdf policy yet. It provides
 * the migration point for moving document code away from direct backend
 * ownership.
 */

quantapdf_status quantapdf_backend_open_internal(
    quantapdf_backend *backend,
    void *document)
{
    if (backend == NULL || backend->ops == NULL ||
        backend->ops->open == NULL) {
        return QUANTAPDF_STATUS_INVALID_ARGUMENT;
    }

    return backend->ops->open(document);
}

void quantapdf_backend_close_internal(
    quantapdf_backend *backend,
    void *document)
{
    if (backend == NULL || backend->ops == NULL ||
        backend->ops->close == NULL) {
        return;
    }

    backend->ops->close(document);
}
