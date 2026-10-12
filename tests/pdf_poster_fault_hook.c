#include "../src/document/document_internal.h"
#include "pdf_poster_test_api.h"

_Static_assert(
    (unsigned int)QUANTAPDF_TEST_PDF_POSTER_FAULT_NONE ==
        (unsigned int)QUANTAPDF_TEST_POSTER_FAULT_NONE,
    "poster fault enums must match");
_Static_assert(
    (unsigned int)QUANTAPDF_TEST_PDF_POSTER_FAULT_ANNOTATION_PREFLIGHT ==
        (unsigned int)QUANTAPDF_TEST_POSTER_FAULT_ANNOTATION_PREFLIGHT,
    "poster fault enums must match");
_Static_assert(
    (unsigned int)QUANTAPDF_TEST_PDF_POSTER_FAULT_WIDGET_PREFLIGHT ==
        (unsigned int)QUANTAPDF_TEST_POSTER_FAULT_WIDGET_PREFLIGHT,
    "poster fault enums must match");
_Static_assert(
    (unsigned int)QUANTAPDF_TEST_PDF_POSTER_FAULT_NAVIGATION_PREFLIGHT ==
        (unsigned int)QUANTAPDF_TEST_POSTER_FAULT_NAVIGATION_PREFLIGHT,
    "poster fault enums must match");

void quantapdf_test_pdf_poster_set_fault(
    quantapdf_document *document,
    quantapdf_test_pdf_poster_fault fault)
{
#if defined(QUANTAPDF_TESTING)
    if (document != NULL)
        document->test_poster_fault = (int)fault;
#else
    (void)document;
    (void)fault;
#endif
}
