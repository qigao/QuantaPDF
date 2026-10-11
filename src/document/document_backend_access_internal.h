#ifndef QUANTAPDF_DOCUMENT_BACKEND_ACCESS_INTERNAL_H
#define QUANTAPDF_DOCUMENT_BACKEND_ACCESS_INTERNAL_H

#include "../backend/backend_internal.h"

/*
 * Temporary migration boundary for document code.
 *
 * Document modules should request backend services through this header
 * instead of directly reaching PDFium/qpdf implementation objects.
 */

static inline quantapdf_backend *
quantapdf_document_backend_internal(quantapdf_document *document)
{
    if (document == NULL) {
        return NULL;
    }
    return document->backend;
}

#endif
