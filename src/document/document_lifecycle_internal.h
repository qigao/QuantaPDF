#ifndef QUANTAPDF_DOCUMENT_LIFECYCLE_INTERNAL_H
#define QUANTAPDF_DOCUMENT_LIFECYCLE_INTERNAL_H

#include "document_backend_access_internal.h"

/*
 * Transitional lifecycle boundary.
 * The document layer will migrate close/open operations through this helper
 * while backend implementations remain private.
 */

static inline void
quantapdf_document_backend_close_internal(quantapdf_document *document)
{
    quantapdf_backend *backend =
        quantapdf_document_backend_internal(document);

    if (backend != NULL) {
        quantapdf_backend_close_internal(backend);
    }
}

#endif
