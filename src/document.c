#include "internal.h"

#include "input_file.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void quantapdf_dispose_document(quantapdf_document *document)
{
    quantapdf_context *context;
    if (document == NULL)
        return;
    context = document->context;

    if (document->backend != NULL)
        document->ops->close(document->backend);
    free(document->source_data);
    if (document->password != NULL) {
        volatile unsigned char *cursor =
            (volatile unsigned char *)document->password;
        size_t remaining = document->password_size;
        while (remaining-- != 0)
            *cursor++ = 0;
        free(document->password);
    }
    free(document);
    quantapdf_drop_context(context);
}

static quantapdf_status quantapdf_copy_password(
    const char *password,
    char **out_password,
    size_t *out_size)
{
    const char *source = password != NULL ? password : "";
    size_t size = strlen(source);
    char *copy;

    if (size == SIZE_MAX)
        return QUANTAPDF_ERROR_NOMEM;
    copy = (char *)malloc(size + 1);
    if (copy == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    memcpy(copy, source, size + 1);
    *out_password = copy;
    *out_size = size;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_open(
    const char *filename,
    const char *password,
    quantapdf_document **out_document)
{
    quantapdf_context *context;
    quantapdf_status status;
    if (out_document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_document = NULL;
    status = quantapdf_context_create(&context);
    if (status != QUANTAPDF_OK)
        return status;
    status = quantapdf_open_with_context(
        context, filename, password, out_document);
    quantapdf_drop_context(context);
    return status;
}

quantapdf_status quantapdf_open_with_context(
    quantapdf_context *context,
    const char *filename,
    const char *password,
    quantapdf_document **out_document)
{
    quantapdf_document *document;
    quantapdf_status status;

    if (out_document == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_document = NULL;
    if (context == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    if (filename == NULL || filename[0] == '\0')
        return QUANTAPDF_ERROR_ARGUMENT;

    status = quantapdf_context_retain_internal(context);
    if (status != QUANTAPDF_OK)
        return status;
    document = (quantapdf_document *)quantapdf_context_allocate_internal(
        context, sizeof(*document));
    if (document == NULL) {
        quantapdf_drop_context(context);
        return QUANTAPDF_ERROR_NOMEM;
    }
    document->context = context;
    document->ops = context->ops;

    status = quantapdf_copy_password(
        password, &document->password, &document->password_size);
    if (status != QUANTAPDF_OK) {
        quantapdf_dispose_document(document);
        return status;
    }

    status = quantapdf_read_file(
        filename, &document->source_data, &document->source_size);
    if (status != QUANTAPDF_OK) {
        quantapdf_dispose_document(document);
        return status;
    }
    status = document->ops->open_memory(
        document->source_data,
        document->source_size,
        password == NULL ? NULL : document->password,
        &document->backend);
    if (status != QUANTAPDF_OK) {
        quantapdf_dispose_document(document);
        return status;
    }

    *out_document = document;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_page_count(
    quantapdf_document *document,
    int *out_page_count)
{
    if (document == NULL || out_page_count == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return document->ops->page_count(document->backend, out_page_count);
}

quantapdf_status quantapdf_document_page_user_unit(
    quantapdf_document *document,
    int page_index,
    double *out_user_unit)
{
    if (document == NULL || out_user_unit == NULL || page_index < 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    return document->ops->page_user_unit(
        document->backend, page_index, out_user_unit);
}

void quantapdf_close(quantapdf_document *document)
{
    quantapdf_dispose_document(document);
}
