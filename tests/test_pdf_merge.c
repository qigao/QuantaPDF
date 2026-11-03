#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

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

static quantapdf_document *open_output(
    const quantapdf_output *output,
    const char *path)
{
    const unsigned char *data = NULL;
    size_t size = 0;
    quantapdf_document *document = NULL;

    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(data != NULL);
    CHECK(size >= 5);
    CHECK(memcmp(data, "%PDF-", 5) == 0);
    CHECK(write_bytes(path, data, size));
    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
    return document;
}

static void expect_page(
    quantapdf_document *document,
    int page_index,
    const char *needle,
    int check_geometry,
    float width,
    float height)
{
    quantapdf_page *page = NULL;
    quantapdf_rect bounds;
    char *text = NULL;
    size_t text_size = 0;

    CHECK(quantapdf_load_page(document, page_index, &page) == QUANTAPDF_OK);
    if (check_geometry) {
        CHECK(quantapdf_page_bounds(page, &bounds) == QUANTAPDF_OK);
        CHECK(bounds.x0 == 0.0f);
        CHECK(bounds.y0 == 0.0f);
        CHECK(bounds.x1 == width);
        CHECK(bounds.y1 == height);
    }
    CHECK(quantapdf_extract_text(page, &text, &text_size) == QUANTAPDF_OK);
    CHECK(text != NULL);
    CHECK(strstr(text, needle) != NULL);

    quantapdf_free(text);
    quantapdf_drop_page(page);
}

static void expect_same_bytes(
    const quantapdf_output *left,
    const quantapdf_output *right)
{
    const unsigned char *left_data = NULL;
    const unsigned char *right_data = NULL;
    size_t left_size = 0;
    size_t right_size = 0;

    CHECK(quantapdf_output_data(left, &left_data, &left_size) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(right, &right_data, &right_size) == QUANTAPDF_OK);
    CHECK(left_size == right_size);
    CHECK(memcmp(left_data, right_data, left_size) == 0);
}

int main(void)
{
    quantapdf_document *composition = NULL;
    quantapdf_document *text = NULL;
    quantapdf_document *reopened = NULL;
    quantapdf_output *output_a = NULL;
    quantapdf_output *output_b = NULL;
    quantapdf_output *merged_ab_1 = NULL;
    quantapdf_output *merged_ab_2 = NULL;
    quantapdf_output *merged_single = NULL;
    quantapdf_output *merged_duplicate = NULL;
    int composition_indices[] = {2, 0};
    int text_indices[] = {0};
    int page_count = 0;

    (void)remove(MERGE_OUTPUT_PDF);

    CHECK(quantapdf_open(test_context(), COMPOSITION_PDF, NULL, &composition) == QUANTAPDF_OK);
    CHECK(quantapdf_open(test_context(), TEXT_PDF, NULL, &text) == QUANTAPDF_OK);

    CHECK(quantapdf_export_pages(
        composition,
        composition_indices,
        sizeof(composition_indices) / sizeof(composition_indices[0]),
        &output_a) == QUANTAPDF_OK);
    CHECK(output_a != NULL);

    CHECK(quantapdf_export_pages(
        text,
        text_indices,
        sizeof(text_indices) / sizeof(text_indices[0]),
        &output_b) == QUANTAPDF_OK);
    CHECK(output_b != NULL);

    quantapdf_close(composition);
    quantapdf_close(text);
    composition = NULL;
    text = NULL;

    {
        const quantapdf_output *inputs[] = {output_a, output_b};
        CHECK(quantapdf_merge_outputs(test_context(), inputs, 2, &merged_ab_1) == QUANTAPDF_OK);
        CHECK(quantapdf_merge_outputs(test_context(), inputs, 2, &merged_ab_2) == QUANTAPDF_OK);
        CHECK(merged_ab_1 != NULL);
        CHECK(merged_ab_2 != NULL);
        expect_same_bytes(merged_ab_1, merged_ab_2);
    }

    {
        const quantapdf_output *inputs[] = {output_a};
        CHECK(quantapdf_merge_outputs(test_context(), inputs, 1, &merged_single) == QUANTAPDF_OK);
        CHECK(merged_single != NULL);
    }

    {
        const quantapdf_output *inputs[] = {output_b, output_a, output_b};
        CHECK(quantapdf_merge_outputs(test_context(), inputs, 3, &merged_duplicate) == QUANTAPDF_OK);
        CHECK(merged_duplicate != NULL);
    }

    quantapdf_drop_output(output_a);
    quantapdf_drop_output(output_b);
    output_a = NULL;
    output_b = NULL;

    reopened = open_output(merged_ab_1, MERGE_OUTPUT_PDF);
    CHECK(quantapdf_page_count(reopened, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 3);
    expect_page(reopened, 0, "PAGE-C", 1, 300.0f, 150.0f);
    expect_page(reopened, 1, "PAGE-A", 1, 200.0f, 200.0f);
    expect_page(reopened, 2, "Hello Caf", 0, 0.0f, 0.0f);
    quantapdf_close(reopened);
    reopened = NULL;

    reopened = open_output(merged_single, MERGE_OUTPUT_PDF);
    CHECK(quantapdf_page_count(reopened, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 2);
    expect_page(reopened, 0, "PAGE-C", 1, 300.0f, 150.0f);
    expect_page(reopened, 1, "PAGE-A", 1, 200.0f, 200.0f);
    quantapdf_close(reopened);
    reopened = NULL;

    reopened = open_output(merged_duplicate, MERGE_OUTPUT_PDF);
    CHECK(quantapdf_page_count(reopened, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 4);
    expect_page(reopened, 0, "Hello Caf", 0, 0.0f, 0.0f);
    expect_page(reopened, 1, "PAGE-C", 1, 300.0f, 150.0f);
    expect_page(reopened, 2, "PAGE-A", 1, 200.0f, 200.0f);
    expect_page(reopened, 3, "Hello Caf", 0, 0.0f, 0.0f);
    quantapdf_close(reopened);
    reopened = NULL;

    quantapdf_drop_output(merged_ab_1);
    quantapdf_drop_output(merged_ab_2);
    quantapdf_drop_output(merged_single);
    quantapdf_drop_output(merged_duplicate);
    (void)remove(MERGE_OUTPUT_PDF);
    return EXIT_SUCCESS;
}
