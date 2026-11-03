#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int ok, const char *expr, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expr);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check_impl((x), #x, __LINE__)

static quantapdf_output *output_sentinel(void)
{
    return (quantapdf_output *)(uintptr_t)1;
}

static quantapdf_page_poster_split make_split(
    int page_index, size_t columns, size_t rows)
{
    quantapdf_page_poster_split split;

    split.page_index = page_index;
    split.columns = columns;
    split.rows = rows;
    return split;
}

static void sibling_fixture_path(
    const char *name,
    char *out_path,
    size_t capacity)
{
    const char *fixture = POSTER_BASIC_PDF;
    const char *slash = strrchr(fixture, '/');
    const char *backslash = strrchr(fixture, '\\');
    const char *separator = slash;
    size_t prefix;
    size_t name_size = strlen(name);

    if (backslash != NULL && (separator == NULL || backslash > separator))
        separator = backslash;
    CHECK(separator != NULL);
    prefix = (size_t)(separator - fixture) + 1;
    CHECK(prefix + name_size + 1 <= capacity);
    memcpy(out_path, fixture, prefix);
    memcpy(out_path + prefix, name, name_size + 1);
}

static quantapdf_document *open_document(const char *path, const char *password)
{
    quantapdf_document *document = NULL;
    CHECK(quantapdf_open(test_context(), path, password, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    return document;
}

static void expect_split_error(
    quantapdf_document *document,
    const quantapdf_page_poster_split *splits,
    size_t split_count,
    quantapdf_status expected)
{
    quantapdf_output *output = output_sentinel();
    CHECK(quantapdf_poster_split_pages(
              document, splits, split_count, &output) == expected);
    CHECK(output == NULL);
}

static quantapdf_rect page_bounds(quantapdf_document *document, int page_index)
{
    quantapdf_page *page = NULL;
    quantapdf_rect bounds = {0};
    CHECK(quantapdf_load_page(document, page_index, &page) == QUANTAPDF_OK);
    CHECK(page != NULL);
    CHECK(quantapdf_page_bounds(page, &bounds) == QUANTAPDF_OK);
    quantapdf_drop_page(page);
    return bounds;
}

static void test_noop(quantapdf_document *document)
{
    quantapdf_page_poster_split split = make_split(1, 1, 1);
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    size_t first_size = 0;
    size_t second_size = 0;
    int count = 0;
    quantapdf_rect before = page_bounds(document, 1);
    quantapdf_rect after;

    CHECK(quantapdf_poster_split_pages(document, &split, 1, &first) ==
          QUANTAPDF_OK);
    CHECK(first != NULL);
    CHECK(quantapdf_poster_split_pages(document, &split, 1, &second) ==
          QUANTAPDF_OK);
    CHECK(second != NULL);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_data != NULL && second_data != NULL);
    CHECK(first_size != 0 && first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    CHECK(quantapdf_page_count(document, &count) == QUANTAPDF_OK);
    CHECK(count == 3);
    after = page_bounds(document, 1);
    CHECK(before.x0 == after.x0 && before.y0 == after.y0 &&
          before.x1 == after.x1 && before.y1 == after.y1);

    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
}

int main(void)
{
    quantapdf_document *document = NULL;
    quantapdf_document *other = NULL;
    quantapdf_output *output = output_sentinel();
    quantapdf_page_poster_split split;
    quantapdf_page_poster_split bad;
    quantapdf_page_poster_split pair[2];
    char non_pdf[1024];
    char encrypted_pdf[1024];
    char signed_pdf[1024];
    char malformed_box_pdf[1024];
    char malformed_rotate_pdf[1024];
    char malformed_userunit_pdf[1024];

    sibling_fixture_path("composition-non-pdf.txt", non_pdf, sizeof(non_pdf));
    sibling_fixture_path("encrypted-one-page.pdf", encrypted_pdf, sizeof(encrypted_pdf));
    sibling_fixture_path("annotation-mutation-signed.pdf", signed_pdf, sizeof(signed_pdf));
    sibling_fixture_path("crop-malformed-box.pdf", malformed_box_pdf, sizeof(malformed_box_pdf));
    sibling_fixture_path("crop-malformed-rotate.pdf", malformed_rotate_pdf, sizeof(malformed_rotate_pdf));
    sibling_fixture_path("crop-malformed-userunit.pdf", malformed_userunit_pdf, sizeof(malformed_userunit_pdf));

    document = open_document(POSTER_BASIC_PDF, NULL);
    split = make_split(1, 2, 2);

    CHECK(quantapdf_poster_split_pages(document, &split, 1, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);
    expect_split_error(NULL, &split, 1, QUANTAPDF_ERROR_ARGUMENT);
    expect_split_error(document, NULL, 1, QUANTAPDF_ERROR_ARGUMENT);
    expect_split_error(document, &split, 0, QUANTAPDF_ERROR_ARGUMENT);

    bad = split;

    bad = split;

    expect_split_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    bad = split;
    bad.page_index = -1;
    expect_split_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);
    bad = split;
    bad.page_index = 3;
    expect_split_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    pair[0] = split;
    pair[1] = make_split(1, 3, 1);
    expect_split_error(document, pair, 2, QUANTAPDF_ERROR_ARGUMENT);

    bad = split;
    bad.columns = 0;
    expect_split_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);
    bad = split;
    bad.rows = 0;
    expect_split_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    bad = split;
    bad.columns = SIZE_MAX;
    bad.rows = 2;
    expect_split_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    bad = split;
    bad.columns = (size_t)INT_MAX;
    bad.rows = 2;
    expect_split_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_open(test_context(), non_pdf, NULL, &other) == QUANTAPDF_ERROR_FORMAT);
    CHECK(other == NULL);
    bad = make_split(0, 1, 1);

    other = open_document(encrypted_pdf, "user-pass");
    expect_split_error(other, &bad, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_close(other);
    other = NULL;

    other = open_document(signed_pdf, NULL);
    expect_split_error(other, &bad, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_close(other);
    other = NULL;

    other = open_document(malformed_box_pdf, NULL);
    expect_split_error(other, &bad, 1, QUANTAPDF_ERROR_FORMAT);
    quantapdf_close(other);
    other = NULL;

    other = open_document(malformed_rotate_pdf, NULL);
    expect_split_error(other, &bad, 1, QUANTAPDF_ERROR_FORMAT);
    quantapdf_close(other);
    other = NULL;

    other = open_document(malformed_userunit_pdf, NULL);
    expect_split_error(other, &bad, 1, QUANTAPDF_ERROR_FORMAT);
    quantapdf_close(other);
    other = NULL;

    test_noop(document);

    output = output_sentinel();
    CHECK(quantapdf_poster_split_pages(document, &split, 1, &output) ==
          QUANTAPDF_OK);
    CHECK(output != NULL && output != output_sentinel());
    quantapdf_drop_output(output);

    quantapdf_close(document);
    return 0;
}
