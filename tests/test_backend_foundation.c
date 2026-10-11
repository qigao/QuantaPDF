#include "backend/pdfium_document.h"
#include "backend/qpdf_document.h"
#include "backend/document_backend.h"

#include <stdio.h>
#include <stdlib.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

void quantapdf_test_backend_dispatch(void);

static unsigned char *read_fixture(const char *path, size_t *out_size)
{
    FILE *file;
    long length;
    unsigned char *data;

    *out_size = 0;
#if defined(_WIN32)
    if (fopen_s(&file, path, "rb") != 0)
        return NULL;
#else
    file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
#endif
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length <= 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    data = (unsigned char *)malloc((size_t)length);
    if (data == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_size = (size_t)length;
    return data;
}

int main(void)
{
    unsigned char *data;
    size_t size;
    int sentinel = 0;
    quantapdf_pdfium_document *pdfium_document = NULL;
    quantapdf_pdfium_page *pdfium_page = NULL;
    quantapdf_qpdf_document *qpdf_document = NULL;
    quantapdf_rect bounds = { -1.0f, -2.0f, -3.0f, -4.0f };
    int pdfium_page_count = 0;
    int qpdf_page_count = 0;
    unsigned char *rewritten_data = NULL;
    size_t rewritten_size = 0;
    int iteration;

    quantapdf_test_backend_dispatch();
    data = read_fixture(ONE_PAGE_PDF, &size);
    CHECK(data != NULL);

    {
        const quantapdf_backend_ops *ops = quantapdf_builtin_backend_ops();
        void *backend = &sentinel;
        void *backend_page = NULL;
        CHECK(ops->open_memory(NULL, size, NULL, &backend) == QUANTAPDF_ERROR_ARGUMENT);
        CHECK(backend == NULL);
        CHECK(ops->open_memory(data, size, NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);
        CHECK(ops->open_memory(data, size, NULL, &backend) == QUANTAPDF_OK);
        CHECK(ops->page_count(backend, &pdfium_page_count) == QUANTAPDF_OK);
        CHECK(pdfium_page_count == 1);
        CHECK(ops->load_page(backend, 0, &backend_page) == QUANTAPDF_OK);
        CHECK(ops->page_bounds(backend_page, &bounds) == QUANTAPDF_OK);
        CHECK(bounds.x1 == 72.0f && bounds.y1 == 72.0f);
        ops->drop_page(backend_page);
        ops->close(backend);
        ops->close(NULL);
    }

    CHECK(quantapdf_pdfium_open_memory(
        data, size, NULL, &pdfium_document) == QUANTAPDF_OK);
    CHECK(pdfium_document != NULL);
    CHECK(quantapdf_pdfium_page_count(
        pdfium_document, &pdfium_page_count) == QUANTAPDF_OK);
    CHECK(pdfium_page_count == 1);
    CHECK(quantapdf_pdfium_load_page(
        pdfium_document, 0, &pdfium_page) == QUANTAPDF_OK);
    CHECK(pdfium_page != NULL);
    CHECK(quantapdf_pdfium_page_bounds(pdfium_page, &bounds) == QUANTAPDF_OK);
    CHECK(bounds.x0 == 0.0f);
    CHECK(bounds.y0 == 0.0f);
    CHECK(bounds.x1 == 72.0f);
    CHECK(bounds.y1 == 72.0f);
    quantapdf_pdfium_drop_page(pdfium_page);
    quantapdf_pdfium_close(pdfium_document);

    pdfium_document = (quantapdf_pdfium_document *)&sentinel;
    CHECK(quantapdf_pdfium_open_memory(
        NULL, size, NULL, &pdfium_document) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(pdfium_document == NULL);
    CHECK(quantapdf_pdfium_open_memory(
        data, size, NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    for (iteration = 0; iteration < 100; ++iteration) {
        pdfium_document = NULL;
        pdfium_page = NULL;
        CHECK(quantapdf_pdfium_open_memory(
            data, size, NULL, &pdfium_document) == QUANTAPDF_OK);
        CHECK(quantapdf_pdfium_load_page(
            pdfium_document, 0, &pdfium_page) == QUANTAPDF_OK);
        quantapdf_pdfium_drop_page(pdfium_page);
        quantapdf_pdfium_close(pdfium_document);
    }

    CHECK(quantapdf_qpdf_open_memory(
        data, size, NULL, &qpdf_document) == QUANTAPDF_OK);
    CHECK(qpdf_document != NULL);
    CHECK(quantapdf_qpdf_page_count(
        qpdf_document, &qpdf_page_count) == QUANTAPDF_OK);
    CHECK(qpdf_page_count == 1);
    quantapdf_qpdf_close(qpdf_document);

    CHECK(quantapdf_qpdf_rewrite_memory(
        data, size, &rewritten_data, &rewritten_size) == QUANTAPDF_OK);
    CHECK(rewritten_data != NULL);
    CHECK(rewritten_size != 0);
    pdfium_document = NULL;
    pdfium_page = NULL;
    CHECK(quantapdf_pdfium_open_memory(
        rewritten_data,
        rewritten_size,
        NULL,
        &pdfium_document) == QUANTAPDF_OK);
    CHECK(quantapdf_pdfium_load_page(
        pdfium_document, 0, &pdfium_page) == QUANTAPDF_OK);
    quantapdf_pdfium_drop_page(pdfium_page);
    quantapdf_pdfium_close(pdfium_document);
    free(rewritten_data);

    free(data);
    data = read_fixture(POSTER_BASIC_PDF, &size);
    CHECK(data != NULL);
    pdfium_document = NULL;
    pdfium_page = NULL;
    pdfium_page_count = 0;
    bounds = (quantapdf_rect){ -1.0f, -2.0f, -3.0f, -4.0f };
    CHECK(quantapdf_pdfium_open_memory(
        data, size, NULL, &pdfium_document) == QUANTAPDF_OK);
    CHECK(quantapdf_pdfium_page_count(
        pdfium_document, &pdfium_page_count) == QUANTAPDF_OK);
    CHECK(pdfium_page_count == 3);
    CHECK(quantapdf_pdfium_load_page(
        pdfium_document, 0, &pdfium_page) == QUANTAPDF_OK);
    CHECK(quantapdf_pdfium_page_bounds(pdfium_page, &bounds) == QUANTAPDF_OK);
    CHECK(bounds.x0 == 0.0f);
    CHECK(bounds.y0 == 0.0f);
    CHECK(bounds.x1 == 200.0f);
    CHECK(bounds.y1 == 200.0f);
    quantapdf_pdfium_drop_page(pdfium_page);
    quantapdf_pdfium_close(pdfium_document);
    free(data);
    fprintf(stderr, "[quantapdf.backend_foundation] complete\n");
    return EXIT_SUCCESS;
}
