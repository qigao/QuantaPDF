#ifndef QUANTAPDF_PDF_EDIT_TEST_API_H
#define QUANTAPDF_PDF_EDIT_TEST_API_H

#include <quantapdf/quantapdf.h>
#include "../src/backend/edit_fault.h"

void quantapdf_test_pdf_edit_set_fault(
    quantapdf_pdf_edit *edit,
    quantapdf_test_pdf_edit_fault fault);

#endif
