#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int verify_export_contract(
    quantapdf_document *source,
    const int *indices,
    size_t index_count,
    const char *const *expected_text,
    const float *expected_width,
    const float *expected_height)
{
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_document *reopened = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    size_t first_size = 0;
    size_t second_size = 0;
    int reopened_count = 0;
    size_t i;
    int ok = 0;

    (void)remove(ORDER_OUTPUT_PDF);

    if (quantapdf_export_pages(source, indices, index_count, &first) != QUANTAPDF_OK ||
        first == NULL)
        goto done;
    if (quantapdf_export_pages(source, indices, index_count, &second) != QUANTAPDF_OK ||
        second == NULL)
        goto done;

    if (quantapdf_output_data(first, &first_data, &first_size) != QUANTAPDF_OK ||
        first_data == NULL || first_size < 5 ||
        memcmp(first_data, "%PDF-", 5) != 0)
        goto done;
    if (quantapdf_output_data(second, &second_data, &second_size) != QUANTAPDF_OK ||
        second_data == NULL || second_size != first_size ||
        memcmp(first_data, second_data, first_size) != 0)
        goto done;

    if (!write_bytes(ORDER_OUTPUT_PDF, first_data, first_size))
        goto done;
    if (quantapdf_open(test_context(), ORDER_OUTPUT_PDF, NULL, &reopened) != QUANTAPDF_OK)
        goto done;
    if (quantapdf_page_count(reopened, &reopened_count) != QUANTAPDF_OK ||
        reopened_count != (int)index_count)
        goto done;

    for (i = 0; i < index_count; ++i) {
        if (!expect_page(
                reopened,
                (int)i,
                expected_text[i],
                expected_width[i],
                expected_height[i]))
            goto done;
    }

    ok = 1;

done:
    quantapdf_close(reopened);
    quantapdf_drop_output(first);
    quantapdf_drop_output(second);
    (void)remove(ORDER_OUTPUT_PDF);
    return ok;
}

int main(void)
{
    quantapdf_document *source = NULL;
    int page_count = 0;
    int reorder_indices[] = {2, 0, 1};
    const char *reorder_text[] = {"PAGE-C", "PAGE-A", "PAGE-B"};
    float reorder_width[] = {300.0f, 200.0f, 240.0f};
    float reorder_height[] = {150.0f, 200.0f, 180.0f};
    int duplicate_indices[] = {1, 1, 2, 1};
    const char *duplicate_text[] = {"PAGE-B", "PAGE-B", "PAGE-C", "PAGE-B"};
    float duplicate_width[] = {240.0f, 240.0f, 300.0f, 240.0f};
    float duplicate_height[] = {180.0f, 180.0f, 150.0f, 180.0f};
    int result = 1;

    if (quantapdf_open(test_context(), COMPOSITION_PDF, NULL, &source) != QUANTAPDF_OK) {
        fprintf(stderr, "could not open composition source PDF\n");
        goto cleanup;
    }

    if (quantapdf_page_count(source, &page_count) != QUANTAPDF_OK || page_count != 3 ||
        !expect_page(source, 0, "PAGE-A", 200.0f, 200.0f) ||
        !expect_page(source, 1, "PAGE-B", 240.0f, 180.0f) ||
        !expect_page(source, 2, "PAGE-C", 300.0f, 150.0f)) {
        fprintf(stderr, "source fixture contract failed\n");
        goto cleanup;
    }

    if (!verify_export_contract(
            source,
            reorder_indices,
            sizeof(reorder_indices) / sizeof(reorder_indices[0]),
            reorder_text,
            reorder_width,
            reorder_height)) {
        fprintf(stderr, "reorder contract failed\n");
        goto cleanup;
    }

    if (!verify_export_contract(
            source,
            duplicate_indices,
            sizeof(duplicate_indices) / sizeof(duplicate_indices[0]),
            duplicate_text,
            duplicate_width,
            duplicate_height)) {
        fprintf(stderr, "duplicate contract failed\n");
        goto cleanup;
    }

    if (quantapdf_page_count(source, &page_count) != QUANTAPDF_OK || page_count != 3 ||
        !expect_page(source, 0, "PAGE-A", 200.0f, 200.0f) ||
        !expect_page(source, 1, "PAGE-B", 240.0f, 180.0f) ||
        !expect_page(source, 2, "PAGE-C", 300.0f, 150.0f)) {
        fprintf(stderr, "source document changed after composition\n");
        goto cleanup;
    }

    result = 0;

cleanup:
    quantapdf_close(source);
    (void)remove(ORDER_OUTPUT_PDF);
    return result;
}
