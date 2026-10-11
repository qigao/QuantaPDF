#ifndef QUANTAPDF_DOCUMENT_INTERNAL_H
#define QUANTAPDF_DOCUMENT_INTERNAL_H

/*
 * Document runtime internal boundary.
 * Keeps document graph evolution separate from public C ABI.
 */

#include "../backend/backend_internal.h"

typedef struct quantapdf_document quantapdf_document;

/*
 * Backend ownership is introduced incrementally.
 * Existing PDFium/qpdf fields remain in the legacy internal model while
 * callers migrate through the backend boundary.
 */
struct quantapdf_document_backend_state {
    quantapdf_backend *backend;
};

#endif
