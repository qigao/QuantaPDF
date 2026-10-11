#include "document/document_internal.h"

#include <stdlib.h>

quantapdf_status quantapdf_extract_text(
    quantapdf_page *page,
    char **out_utf8,
    size_t *out_size)
{
    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;

    if (page == NULL || out_utf8 == NULL || out_size == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return page->document->ops->extract_text(
        page->backend, out_utf8, out_size);
}
