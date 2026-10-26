#ifndef QUANTAPDF_BUILTIN_SOURCE_VIEW_V1_H
#define QUANTAPDF_BUILTIN_SOURCE_VIEW_V1_H

#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Private, read-only borrowed view of an existing built-in backend Page.
 * Nothing is copied or retained: Source bytes and password remain valid
 * only until the page is dropped and its parent document is closed.
 * This accessor never loads or invokes PDFium and has no public ABI. */
typedef struct quantapdf_builtin_source_view_v1 {
    const unsigned char *data;
    size_t size;
    const char *password;
    size_t page_index;
} quantapdf_builtin_source_view_v1;

quantapdf_status quantapdf_builtin_page_source_view_v1(
    void *builtin_page,
    quantapdf_builtin_source_view_v1 *out_view);

#ifdef __cplusplus
}
#endif
#endif
