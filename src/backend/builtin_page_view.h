#ifndef QUANTAPDF_BUILTIN_PAGE_VIEW_H
#define QUANTAPDF_BUILTIN_PAGE_VIEW_H
#include "backend/qpdf_document.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Synchronous borrow: the facade Page retains Document through rendering.
 * No bytes, password, parser or pixel owner is transferred by this view. */
typedef struct quantapdf_builtin_page_view {
    quantapdf_qpdf_document *document;
    size_t page_index;
} quantapdf_builtin_page_view;
quantapdf_builtin_page_view quantapdf_builtin_page_borrow(void *builtin_page);
#ifdef __cplusplus
}
#endif
#endif
