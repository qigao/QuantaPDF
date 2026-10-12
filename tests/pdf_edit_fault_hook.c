#include "../src/document/pdf_edit_internal.h"
#include "pdf_edit_test_api.h"

void quantapdf_test_pdf_edit_set_fault(
    quantapdf_pdf_edit *edit,
    quantapdf_test_pdf_edit_fault fault)
{
#if defined(QUANTAPDF_TESTING)
    if (edit != NULL)
        edit->test_fault = (int)fault;
#else
    (void)edit;
    (void)fault;
#endif
}
