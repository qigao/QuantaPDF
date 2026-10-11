#include "internal.h"

quantapdf_status quantapdf_document_metadata(
    quantapdf_document *document,
    quantapdf_metadata_field field,
    char **out_utf8,
    size_t *out_size)
{
    if (out_utf8 != NULL)
        *out_utf8 = NULL;
    if (out_size != NULL)
        *out_size = 0;
    if (document == NULL || out_utf8 == NULL || out_size == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return document->ops->metadata(
        document->backend, field, out_utf8, out_size);
}
