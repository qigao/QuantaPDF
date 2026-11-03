#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>

#include <stddef.h>
#include <stdint.h>
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

static int close_float(float a, float b)
{
    float d = a - b;
    if (d < 0.0f)
        d = -d;
    return d < 0.01f;
}

static void expect_info(
    const quantapdf_annotation_page *annotations,
    size_t index,
    quantapdf_annotation_type type,
    float x0,
    float y0,
    float x1,
    float y1,
    uint32_t flags)
{
    quantapdf_annotation_info info = { 0 };

    CHECK(quantapdf_annotation_get_info(annotations, index, &info) ==
          QUANTAPDF_OK);

    CHECK(info.type == type);
    CHECK(close_float(info.bounds.x0, x0));
    CHECK(close_float(info.bounds.y0, y0));
    CHECK(close_float(info.bounds.x1, x1));
    CHECK(close_float(info.bounds.y1, y1));
    CHECK(info.flags == flags);
}

static void expect_contents(
    const quantapdf_annotation_page *annotations,
    size_t index,
    const char *expected)
{
    const char *text = (const char *)(uintptr_t)1;
    size_t size = (size_t)-1;

    CHECK(quantapdf_annotation_contents(
              annotations, index, &text, &size) == QUANTAPDF_OK);
    if (expected == NULL) {
        CHECK(text == NULL);
        CHECK(size == 0);
        return;
    }

    CHECK(text != NULL);
    CHECK(size == strlen(expected));
    CHECK(memcmp(text, expected, size) == 0);
    CHECK(text[size] == '\0');
}

static void open_page(
    const char *path,
    quantapdf_document **out_document,
    quantapdf_page **out_page)
{
    *out_document = NULL;
    *out_page = NULL;
    CHECK(quantapdf_open(test_context(), path, NULL, out_document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(*out_document, 0, out_page) == QUANTAPDF_OK);
    CHECK(*out_page != NULL);
}

static void test_mixed_order_identity_and_lifetime(void)
{
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_annotation_page *first = NULL;
    quantapdf_annotation_page *second = NULL;
    size_t count = 0;

    open_page(ANNOTATIONS_MIXED_PDF, &document, &page);

    CHECK(quantapdf_extract_annotations(page, &first) == QUANTAPDF_OK);
    CHECK(first != NULL);
    CHECK(quantapdf_extract_annotations(page, &second) == QUANTAPDF_OK);
    CHECK(second != NULL);
    CHECK(first != second);

    quantapdf_drop_page(page);
    quantapdf_close(document);

    CHECK(quantapdf_annotation_count(first, &count) == QUANTAPDF_OK);
    CHECK(count == 3);

    expect_info(first, 0, QUANTAPDF_ANNOTATION_TEXT,
                10.0f, 160.0f, 30.0f, 180.0f, 4u);
    expect_contents(first, 0, "alpha");

    expect_info(first, 1, QUANTAPDF_ANNOTATION_UNKNOWN,
                50.0f, 120.0f, 70.0f, 140.0f, 64u);
    expect_contents(first, 1, "unknown");

    expect_info(first, 2, QUANTAPDF_ANNOTATION_HIGHLIGHT,
                90.0f, 70.0f, 120.0f, 100.0f, 0u);
    expect_contents(first, 2, "bravo");

    CHECK(quantapdf_annotation_count(second, &count) == QUANTAPDF_OK);
    CHECK(count == 3);
    expect_info(second, 0, QUANTAPDF_ANNOTATION_TEXT,
                10.0f, 160.0f, 30.0f, 180.0f, 4u);
    expect_info(second, 1, QUANTAPDF_ANNOTATION_UNKNOWN,
                50.0f, 120.0f, 70.0f, 140.0f, 64u);
    expect_info(second, 2, QUANTAPDF_ANNOTATION_HIGHLIGHT,
                90.0f, 70.0f, 120.0f, 100.0f, 0u);

    quantapdf_drop_annotation_page(first);
    quantapdf_drop_annotation_page(second);
}

static void expect_empty_snapshot(const char *path)
{
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_annotation_page *annotations = NULL;
    size_t count = 99;

    open_page(path, &document, &page);
    CHECK(quantapdf_extract_annotations(page, &annotations) == QUANTAPDF_OK);
    CHECK(annotations != NULL);

    quantapdf_drop_page(page);
    quantapdf_close(document);

    CHECK(quantapdf_annotation_count(annotations, &count) == QUANTAPDF_OK);
    CHECK(count == 0);
    quantapdf_drop_annotation_page(annotations);
}

static void test_empty_snapshot_tolerance(void)
{
    expect_empty_snapshot(EMPTY_ANNOTATIONS_PDF);
    expect_empty_snapshot(ANNOTATIONS_NONARRAY_PDF);
    expect_empty_snapshot(ANNOTATIONS_FILTERED_ONLY_PDF);
}

static void test_late_failure_is_atomic_and_repeatable(void)
{
    int sentinel = 0;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_annotation_page *annotations =
        (quantapdf_annotation_page *)&sentinel;

    open_page(ANNOTATIONS_LATE_MALFORMED_PDF, &document, &page);

    CHECK(quantapdf_extract_annotations(page, &annotations) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(annotations == NULL);

    annotations = (quantapdf_annotation_page *)&sentinel;
    CHECK(quantapdf_extract_annotations(page, &annotations) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(annotations == NULL);

    quantapdf_drop_page(page);
    quantapdf_close(document);
}

static void test_argument_and_reset_contract(void)
{
    int sentinel = 0;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_annotation_page *annotations =
        (quantapdf_annotation_page *)&sentinel;
    quantapdf_annotation_info info = { 0 };
    quantapdf_annotation_info small = { 0 };
    const char *text = (const char *)&sentinel;
    size_t size = 99;
    size_t count = 99;

    CHECK(quantapdf_extract_annotations(NULL, &annotations) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(annotations == NULL);
    CHECK(quantapdf_extract_annotations(NULL, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    open_page(ANNOTATIONS_MIXED_PDF, &document, &page);
    CHECK(quantapdf_extract_annotations(page, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_extract_annotations(page, &annotations) ==
          QUANTAPDF_OK);
    CHECK(annotations != NULL);

    CHECK(quantapdf_annotation_count(NULL, &count) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(count == 0);
    CHECK(quantapdf_annotation_count(annotations, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_annotation_get_info(annotations, 0, &small) ==
          QUANTAPDF_ERROR_ARGUMENT);

    info.type = QUANTAPDF_ANNOTATION_HIGHLIGHT;
    info.bounds.x0 = 99.0f;
    info.bounds.y0 = 99.0f;
    info.bounds.x1 = 99.0f;
    info.bounds.y1 = 99.0f;
    info.flags = UINT32_MAX;
    CHECK(quantapdf_annotation_get_info(annotations, 99, &info) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(info.type == QUANTAPDF_ANNOTATION_UNKNOWN);
    CHECK(close_float(info.bounds.x0, 0.0f));
    CHECK(close_float(info.bounds.y0, 0.0f));
    CHECK(close_float(info.bounds.x1, 0.0f));
    CHECK(close_float(info.bounds.y1, 0.0f));
    CHECK(info.flags == 0);

    info.type = QUANTAPDF_ANNOTATION_TEXT;
    CHECK(quantapdf_annotation_get_info(NULL, 0, &info) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(info.type == QUANTAPDF_ANNOTATION_UNKNOWN);
    CHECK(quantapdf_annotation_get_info(NULL, 0, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_annotation_contents(NULL, 0, &text, &size) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);
    CHECK(size == 0);

    text = (const char *)&sentinel;
    size = 99;
    CHECK(quantapdf_annotation_contents(annotations, 99, &text, &size) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);
    CHECK(size == 0);

    CHECK(quantapdf_annotation_contents(annotations, 0, NULL, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_drop_annotation_page(annotations);
    quantapdf_drop_annotation_page(NULL);
}

int main(void)
{
    test_mixed_order_identity_and_lifetime();
    test_empty_snapshot_tolerance();
    test_late_failure_is_atomic_and_repeatable();
    test_argument_and_reset_contract();
    return EXIT_SUCCESS;
}
