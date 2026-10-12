#ifndef QUANTAPDF_PDF_EDIT_INTERNAL_H
#define QUANTAPDF_PDF_EDIT_INTERNAL_H

#include "internal.h"

struct quantapdf_pdf_edit {
    const quantapdf_backend_edit_ops *ops;
    void *backend;
#if defined(QUANTAPDF_TESTING)
    int test_fault;
#endif
};

#endif
