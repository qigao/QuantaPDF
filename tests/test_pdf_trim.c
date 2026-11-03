#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>

#include <math.h>
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

static int close_float(float left, float right)
{
    return fabsf(left - right) < 0.01f;
}

static void check_rect_close(quantapdf_rect actual, quantapdf_rect expected)
{
    CHECK(close_float(actual.x0, expected.x0));
    CHECK(close_float(actual.y0, expected.y0));
    CHECK(close_float(actual.x1, expected.x1));
    CHECK(close_float(actual.y1, expected.y1));
}

static quantapdf_output *output_sentinel(void)
{
    return (quantapdf_output *)(uintptr_t)1;
}

static quantapdf_page_trim make_trim(
    int page_index,
    float x0,
    float y0,
    float x1,
    float y1)
{
    quantapdf_page_trim trim;

    trim.page_index = page_index;
    trim.bounds.x0 = x0;
    trim.bounds.y0 = y0;
    trim.bounds.x1 = x1;
    trim.bounds.y1 = y1;
    return trim;
}

static void sibling_fixture_path(
    const char *name,
    char *out_path,
    size_t capacity)
{
    const char *fixture = TRIM_INTERACTIVE_PDF;
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

static void expect_trim_error(
    quantapdf_document *document,
    const quantapdf_page_trim *trims,
    size_t trim_count,
    quantapdf_status expected)
{
    quantapdf_output *output = output_sentinel();

    CHECK(quantapdf_trim_pages(document, trims, trim_count, &output) == expected);
    CHECK(output == NULL);
}

static quantapdf_rect page_box(
    quantapdf_document *document,
    int page_index,
    quantapdf_page_box box)
{
    quantapdf_page *page = NULL;
    quantapdf_rect bounds = {0};

    CHECK(quantapdf_load_page(document, page_index, &page) == QUANTAPDF_OK);
    CHECK(page != NULL);
    CHECK(quantapdf_page_box_bounds(page, box, &bounds) == QUANTAPDF_OK);
    quantapdf_drop_page(page);
    return bounds;
}

static void test_noop_determinism(
    quantapdf_document *document,
    quantapdf_rect source_media)
{
    quantapdf_page_trim full = make_trim(
        0,
        source_media.x0,
        source_media.y0,
        source_media.x1,
        source_media.y1);
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    size_t first_size = 0;
    size_t second_size = 0;
    quantapdf_rect after;

    CHECK(quantapdf_trim_pages(document, &full, 1, &first) == QUANTAPDF_OK);
    CHECK(first != NULL);
    CHECK(quantapdf_trim_pages(document, &full, 1, &second) == QUANTAPDF_OK);
    CHECK(second != NULL);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) == QUANTAPDF_OK);
    CHECK(first_data != NULL && second_data != NULL);
    CHECK(first_size != 0);
    CHECK(second_size == first_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    after = page_box(document, 0, QUANTAPDF_PAGE_BOX_MEDIA);
    check_rect_close(after, source_media);

    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
}

int main(void)
{
    quantapdf_document *document = NULL;
    quantapdf_document *other = NULL;
    quantapdf_output *output = output_sentinel();
    quantapdf_rect source_media;
    quantapdf_page_trim trim;
    quantapdf_page_trim bad;
    quantapdf_page_trim pair[2];
    char non_pdf[1024];
    char encrypted_pdf[1024];
    char signed_pdf[1024];
    char malformed_box_pdf[1024];
    char malformed_rotate_pdf[1024];
    char malformed_userunit_pdf[1024];

    sibling_fixture_path("composition-non-pdf.txt", non_pdf, sizeof(non_pdf));
    sibling_fixture_path("encrypted-one-page.pdf", encrypted_pdf, sizeof(encrypted_pdf));
    sibling_fixture_path("annotation-mutation-signed.pdf", signed_pdf, sizeof(signed_pdf));
    sibling_fixture_path("trim-malformed-box.pdf", malformed_box_pdf, sizeof(malformed_box_pdf));
    sibling_fixture_path("trim-malformed-rotate.pdf", malformed_rotate_pdf, sizeof(malformed_rotate_pdf));
    sibling_fixture_path("trim-malformed-userunit.pdf", malformed_userunit_pdf, sizeof(malformed_userunit_pdf));

    document = open_document(TRIM_INTERACTIVE_PDF, NULL);
    source_media = page_box(document, 0, QUANTAPDF_PAGE_BOX_MEDIA);
    check_rect_close(
        source_media,
        (quantapdf_rect){0.0f, 0.0f, 400.0f, 300.0f});
    trim = make_trim(0, 40.0f, 30.0f, 360.0f, 270.0f);

    CHECK(quantapdf_trim_pages(document, &trim, 1, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);
    expect_trim_error(NULL, &trim, 1, QUANTAPDF_ERROR_ARGUMENT);
    expect_trim_error(document, NULL, 1, QUANTAPDF_ERROR_ARGUMENT);
    expect_trim_error(document, &trim, 0, QUANTAPDF_ERROR_ARGUMENT);

    bad = trim;

    bad = trim;

    expect_trim_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    bad = trim;
    bad.page_index = -1;
    expect_trim_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);
    bad = trim;
    bad.page_index = 2;
    expect_trim_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    pair[0] = trim;
    pair[1] = make_trim(0, 50.0f, 40.0f, 350.0f, 260.0f);
    expect_trim_error(document, pair, 2, QUANTAPDF_ERROR_ARGUMENT);

    bad = trim;
    bad.bounds.x0 = NAN;
    expect_trim_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);
    bad = trim;
    bad.bounds.y0 = INFINITY;
    expect_trim_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);
    bad = trim;
    bad.bounds.x1 = -INFINITY;
    expect_trim_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    bad = trim;
    bad.bounds.x1 = bad.bounds.x0;
    expect_trim_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);
    bad = trim;
    bad.bounds.y1 = bad.bounds.y0;
    expect_trim_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);
    bad = trim;
    bad.bounds.x0 = 370.0f;
    bad.bounds.x1 = 360.0f;
    expect_trim_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    bad = trim;
    bad.bounds.x0 = source_media.x0 - 1.0f;
    expect_trim_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);
    bad = trim;
    bad.bounds.x1 = source_media.x1 + 1.0f;
    expect_trim_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_open(test_context(), non_pdf, NULL, &other) == QUANTAPDF_ERROR_FORMAT);
    CHECK(other == NULL);

    other = open_document(encrypted_pdf, "user-pass");
    bad = make_trim(0, 0.0f, 0.0f, 100.0f, 100.0f);
    expect_trim_error(other, &bad, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_close(other);
    other = NULL;

    other = open_document(signed_pdf, NULL);
    bad = make_trim(0, 0.0f, 0.0f, 100.0f, 100.0f);
    expect_trim_error(other, &bad, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_close(other);
    other = NULL;

    other = open_document(malformed_box_pdf, NULL);
    bad = make_trim(0, 0.0f, 0.0f, 100.0f, 100.0f);
    expect_trim_error(other, &bad, 1, QUANTAPDF_ERROR_FORMAT);
    bad = make_trim(1, 0.0f, 0.0f, 100.0f, 100.0f);
    expect_trim_error(other, &bad, 1, QUANTAPDF_ERROR_FORMAT);
    quantapdf_close(other);
    other = NULL;

    other = open_document(malformed_rotate_pdf, NULL);
    bad = make_trim(0, 0.0f, 0.0f, 100.0f, 100.0f);
    expect_trim_error(other, &bad, 1, QUANTAPDF_ERROR_FORMAT);
    quantapdf_close(other);
    other = NULL;

    other = open_document(malformed_userunit_pdf, NULL);
    bad = make_trim(0, 0.0f, 0.0f, 100.0f, 100.0f);
    expect_trim_error(other, &bad, 1, QUANTAPDF_ERROR_FORMAT);
    quantapdf_close(other);
    other = NULL;

    test_noop_determinism(document, source_media);

    output = output_sentinel();
    if (quantapdf_trim_pages(document, &trim, 1, &output) != QUANTAPDF_OK ||
        output == NULL) {
        fprintf(stderr, "valid trim failed\n");
        CHECK(output == NULL);
        quantapdf_close(document);
        return EXIT_FAILURE;
    }

    quantapdf_drop_output(output);
    quantapdf_close(document);
    return EXIT_SUCCESS;
}
