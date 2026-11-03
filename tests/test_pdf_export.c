#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static quantapdf_output *output_sentinel(void)
{
    return (quantapdf_output *)(uintptr_t)1;
}

static int write_bytes(const char *path, const unsigned char *data, size_t size)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL)
        return 0;
    if (size != 0 && fwrite(data, 1, size, file) != size) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static int expect_page(
    quantapdf_document *document,
    int index,
    const char *needle,
    float width,
    float height)
{
    quantapdf_page *page = NULL;
    quantapdf_rect bounds;
    char *text = NULL;
    size_t text_size = 0;
    int ok = 0;

    if (quantapdf_load_page(document, index, &page) != QUANTAPDF_OK)
        goto done;
    if (quantapdf_page_bounds(page, &bounds) != QUANTAPDF_OK)
        goto done;
    if (bounds.x0 != 0.0f || bounds.y0 != 0.0f ||
        bounds.x1 != width || bounds.y1 != height)
        goto done;
    if (quantapdf_extract_text(page, &text, &text_size) != QUANTAPDF_OK)
        goto done;
    if (text == NULL || strstr(text, needle) == NULL)
        goto done;

    ok = 1;

done:
    quantapdf_free(text);
    quantapdf_drop_page(page);
    return ok;
}

static int expect_export_error(
    quantapdf_document *document,
    const int *indices,
    size_t count,
    quantapdf_status expected)
{
    quantapdf_output *output = output_sentinel();
    quantapdf_status status =
        quantapdf_export_pages(document, indices, count, &output);

    if (status != expected || output != NULL) {
        if (output != NULL && output != output_sentinel())
            quantapdf_drop_output(output);
        return 0;
    }
    return 1;
}

int main(void)
{
    quantapdf_document *document = NULL;
    quantapdf_document *reopened = NULL;
    quantapdf_document *text_document = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    const unsigned char *reset_data = NULL;
    size_t first_size = 0;
    size_t second_size = 0;
    size_t reset_size = 0;
    int page_count = 0;
    int indices[] = {2, 0, 2};
    int negative[] = {-1};
    int high[] = {3};
    int mixed[] = {0, 3, 1};
    int result = 1;

    (void)remove(COMPOSITION_OUTPUT_PDF);

    if (quantapdf_open(test_context(), COMPOSITION_PDF, NULL, &document) != QUANTAPDF_OK) {
        fprintf(stderr, "could not open composition source PDF\n");
        goto cleanup;
    }

    if (!expect_export_error(NULL, indices, 3, QUANTAPDF_ERROR_ARGUMENT)) {
        fprintf(stderr, "NULL document contract failed\n");
        goto cleanup;
    }

    if (quantapdf_export_pages(document, indices, 3, NULL) !=
        QUANTAPDF_ERROR_ARGUMENT) {
        fprintf(stderr, "NULL out_output contract failed\n");
        goto cleanup;
    }

    if (!expect_export_error(document, NULL, 3, QUANTAPDF_ERROR_ARGUMENT) ||
        !expect_export_error(document, indices, 0, QUANTAPDF_ERROR_ARGUMENT) ||
        !expect_export_error(document, negative, 1, QUANTAPDF_ERROR_ARGUMENT) ||
        !expect_export_error(document, high, 1, QUANTAPDF_ERROR_ARGUMENT) ||
        !expect_export_error(document, mixed, 3, QUANTAPDF_ERROR_ARGUMENT)) {
        fprintf(stderr, "index validation/reset contract failed\n");
        goto cleanup;
    }

    if (quantapdf_export_pages(document, indices, 3, &first) != QUANTAPDF_OK ||
        first == NULL) {
        fprintf(stderr, "first export failed\n");
        goto cleanup;
    }

    if (quantapdf_export_pages(document, indices, 3, &second) != QUANTAPDF_OK ||
        second == NULL) {
        fprintf(stderr, "second export failed\n");
        goto cleanup;
    }

    if (quantapdf_output_data(first, &first_data, &first_size) != QUANTAPDF_OK ||
        first_data == NULL || first_size < 5 ||
        memcmp(first_data, "%PDF-", 5) != 0) {
        fprintf(stderr, "first output data contract failed\n");
        goto cleanup;
    }

    if (quantapdf_output_data(second, &second_data, &second_size) != QUANTAPDF_OK ||
        second_data == NULL || second_size != first_size ||
        memcmp(second_data, first_data, first_size) != 0) {
        fprintf(stderr, "deterministic output contract failed\n");
        goto cleanup;
    }

    reset_data = (const unsigned char *)(uintptr_t)1;
    reset_size = 123;
    if (quantapdf_output_data(NULL, &reset_data, &reset_size) !=
            QUANTAPDF_ERROR_ARGUMENT ||
        reset_data != NULL || reset_size != 0) {
        fprintf(stderr, "NULL output accessor reset contract failed\n");
        goto cleanup;
    }

    reset_size = 123;
    if (quantapdf_output_data(first, NULL, &reset_size) !=
            QUANTAPDF_ERROR_ARGUMENT ||
        reset_size != 0) {
        fprintf(stderr, "NULL out_data reset contract failed\n");
        goto cleanup;
    }

    reset_data = (const unsigned char *)(uintptr_t)1;
    if (quantapdf_output_data(first, &reset_data, NULL) !=
            QUANTAPDF_ERROR_ARGUMENT ||
        reset_data != NULL) {
        fprintf(stderr, "NULL out_size reset contract failed\n");
        goto cleanup;
    }

    quantapdf_close(document);
    document = NULL;

    if (first_data[0] != '%' || first_data[1] != 'P' ||
        !write_bytes(COMPOSITION_OUTPUT_PDF, first_data, first_size)) {
        fprintf(stderr, "output did not survive source close\n");
        goto cleanup;
    }

    if (quantapdf_open(test_context(), COMPOSITION_OUTPUT_PDF, NULL, &reopened) != QUANTAPDF_OK) {
        fprintf(stderr, "could not reopen exported PDF\n");
        goto cleanup;
    }

    if (quantapdf_page_count(reopened, &page_count) != QUANTAPDF_OK ||
        page_count != 3) {
        fprintf(stderr, "exported page count mismatch\n");
        goto cleanup;
    }

    if (!expect_page(reopened, 0, "PAGE-C", 300.0f, 150.0f) ||
        !expect_page(reopened, 1, "PAGE-A", 200.0f, 200.0f) ||
        !expect_page(reopened, 2, "PAGE-C", 300.0f, 150.0f)) {
        fprintf(stderr, "exported page order/text/geometry mismatch\n");
        goto cleanup;
    }

    quantapdf_close(reopened);
    reopened = NULL;

    if (quantapdf_open(test_context(), COMPOSITION_NON_PDF, NULL, &text_document) !=
            QUANTAPDF_ERROR_FORMAT ||
        text_document != NULL) {
        fprintf(stderr, "non-PDF open contract failed\n");
        goto cleanup;
    }

    quantapdf_drop_output(NULL);
    result = 0;

cleanup:
    quantapdf_close(document);
    quantapdf_close(reopened);
    quantapdf_close(text_document);
    quantapdf_drop_output(first);
    quantapdf_drop_output(second);
    (void)remove(COMPOSITION_OUTPUT_PDF);
    return result;
}
