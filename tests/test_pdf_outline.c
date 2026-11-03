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
    const quantapdf_outline *outline,
    size_t index,
    size_t parent_index,
    size_t first_child_index,
    size_t next_sibling_index,
    quantapdf_outline_destination_kind kind,
    int target_page,
    float x,
    float y,
    int is_open)
{
    quantapdf_outline_info info = { 0 };

    CHECK(quantapdf_outline_get_info(outline, index, &info) == QUANTAPDF_OK);

    CHECK(info.parent_index == parent_index);
    CHECK(info.first_child_index == first_child_index);
    CHECK(info.next_sibling_index == next_sibling_index);
    CHECK(info.destination_kind == kind);
    CHECK(info.target_page == target_page);
    CHECK(close_float(info.target.x, x));
    CHECK(close_float(info.target.y, y));
    CHECK(info.is_open == is_open);
}

static void expect_title(
    const quantapdf_outline *outline,
    size_t index,
    const char *expected)
{
    const char *text = (const char *)(uintptr_t)1;
    size_t size = (size_t)-1;

    CHECK(quantapdf_outline_title(outline, index, &text, &size) == QUANTAPDF_OK);
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

static void expect_uri(
    const quantapdf_outline *outline,
    size_t index,
    const char *expected)
{
    const char *uri = (const char *)(uintptr_t)1;
    size_t size = (size_t)-1;

    CHECK(quantapdf_outline_uri(outline, index, &uri, &size) == QUANTAPDF_OK);
    CHECK(uri != NULL);
    CHECK(size == strlen(expected));
    CHECK(memcmp(uri, expected, size) == 0);
    CHECK(uri[size] == '\0');
}

static void expect_uri_unavailable(
    const quantapdf_outline *outline,
    size_t index)
{
    const char *uri = (const char *)(uintptr_t)1;
    size_t size = (size_t)-1;

    CHECK(quantapdf_outline_uri(outline, index, &uri, &size) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(uri == NULL);
    CHECK(size == 0);
}

static void test_valid_outline_and_independent_lifetime(void)
{
    static const char unicode_title[] = "Chapter 1 Caf\xC3\xA9";
    static const char uri[] = "https://example.com/quantapdf-outline";
    quantapdf_document *document = NULL;
    quantapdf_outline *outline = NULL;
    size_t count = 0;

    CHECK(quantapdf_open(test_context(), OUTLINE_TREE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_document_outline(document, &outline) == QUANTAPDF_OK);
    CHECK(outline != NULL);
    quantapdf_close(document);

    CHECK(quantapdf_outline_count(outline, &count) == QUANTAPDF_OK);
    CHECK(count == 6);

    expect_info(outline, 0, SIZE_MAX, 1, 4,
                QUANTAPDF_OUTLINE_DESTINATION_INTERNAL,
                0, 30.0f, 50.0f, 1);
    expect_info(outline, 1, 0, SIZE_MAX, 2,
                QUANTAPDF_OUTLINE_DESTINATION_INTERNAL,
                1, 10.0f, 20.0f, 0);
    expect_info(outline, 2, 0, SIZE_MAX, 3,
                QUANTAPDF_OUTLINE_DESTINATION_URI,
                -1, 0.0f, 0.0f, 0);
    expect_info(outline, 3, 0, SIZE_MAX, SIZE_MAX,
                QUANTAPDF_OUTLINE_DESTINATION_NONE,
                -1, 0.0f, 0.0f, 0);
    expect_info(outline, 4, SIZE_MAX, 5, SIZE_MAX,
                QUANTAPDF_OUTLINE_DESTINATION_INTERNAL,
                2, 40.0f, 40.0f, 0);
    expect_info(outline, 5, 4, SIZE_MAX, SIZE_MAX,
                QUANTAPDF_OUTLINE_DESTINATION_NONE,
                -1, 0.0f, 0.0f, 0);

    expect_title(outline, 0, unicode_title);
    expect_title(outline, 1, "Section 1.1");
    expect_title(outline, 2, "Website");
    expect_title(outline, 3, NULL);
    expect_title(outline, 4, "Chapter 2");
    expect_title(outline, 5, "");

    expect_uri(outline, 2, uri);
    expect_uri_unavailable(outline, 0);
    expect_uri_unavailable(outline, 3);

    quantapdf_drop_outline(outline);
}

static void test_empty_outline(void)
{
    quantapdf_document *document = NULL;
    quantapdf_outline *outline = NULL;
    size_t count = 99;

    CHECK(quantapdf_open(test_context(), EMPTY_OUTLINE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_document_outline(document, &outline) == QUANTAPDF_OK);
    CHECK(outline != NULL);
    quantapdf_close(document);

    CHECK(quantapdf_outline_count(outline, &count) == QUANTAPDF_OK);
    CHECK(count == 0);
    quantapdf_drop_outline(outline);
}

static void test_repairable_outline_is_rejected_without_repair(void)
{
    int sentinel = 0;
    quantapdf_document *document = NULL;
    quantapdf_outline *outline = (quantapdf_outline *)&sentinel;

    CHECK(quantapdf_open(test_context(), OUTLINE_REPAIRABLE_BAD_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_document_outline(document, &outline) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(outline == NULL);

    outline = (quantapdf_outline *)&sentinel;
    CHECK(quantapdf_document_outline(document, &outline) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(outline == NULL);
    quantapdf_close(document);
}

static void test_cycle_depth_and_pdf_only_boundaries(void)
{
    int sentinel = 0;
    quantapdf_document *document = NULL;
    quantapdf_outline *outline = (quantapdf_outline *)&sentinel;

    CHECK(quantapdf_open(test_context(), OUTLINE_CYCLE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_document_outline(document, &outline) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(outline == NULL);
    quantapdf_close(document);

    document = NULL;
    outline = (quantapdf_outline *)&sentinel;
    CHECK(quantapdf_open(test_context(), OUTLINE_DEPTH_257_PDF, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_document_outline(document, &outline) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(outline == NULL);
    quantapdf_close(document);

    document = NULL;
    outline = (quantapdf_outline *)&sentinel;
    CHECK(quantapdf_open(test_context(), COMPOSITION_NON_PDF, NULL, &document) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(document == NULL);
}

static void test_argument_and_reset_contract(void)
{
    int sentinel = 0;
    quantapdf_document *document = NULL;
    quantapdf_outline *outline = (quantapdf_outline *)&sentinel;
    quantapdf_outline_info info = { 0 };
    quantapdf_outline_info small = { 0 };
    const char *text = (const char *)&sentinel;
    size_t size = 99;
    size_t count = 99;

    CHECK(quantapdf_document_outline(NULL, &outline) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(outline == NULL);
    CHECK(quantapdf_document_outline(NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_open(test_context(), OUTLINE_TREE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_document_outline(document, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_document_outline(document, &outline) == QUANTAPDF_OK);
    CHECK(outline != NULL);

    CHECK(quantapdf_outline_count(NULL, &count) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(count == 0);
    CHECK(quantapdf_outline_count(outline, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_outline_get_info(outline, 0, &small) ==
          QUANTAPDF_ERROR_ARGUMENT);

    info.parent_index = 0;
    info.first_child_index = 0;
    info.next_sibling_index = 0;
    info.destination_kind = QUANTAPDF_OUTLINE_DESTINATION_URI;
    info.target_page = 99;
    info.target.x = 99.0f;
    info.target.y = 99.0f;
    info.is_open = 99;
    CHECK(quantapdf_outline_get_info(outline, 99, &info) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(info.parent_index == SIZE_MAX);
    CHECK(info.first_child_index == SIZE_MAX);
    CHECK(info.next_sibling_index == SIZE_MAX);
    CHECK(info.destination_kind == QUANTAPDF_OUTLINE_DESTINATION_NONE);
    CHECK(info.target_page == -1);
    CHECK(close_float(info.target.x, 0.0f));
    CHECK(close_float(info.target.y, 0.0f));
    CHECK(info.is_open == 0);

    info.parent_index = 0;
    CHECK(quantapdf_outline_get_info(NULL, 0, &info) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(info.parent_index == SIZE_MAX);

    CHECK(quantapdf_outline_get_info(NULL, 0, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_outline_title(NULL, 0, &text, &size) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);
    CHECK(size == 0);

    text = (const char *)&sentinel;
    size = 99;
    CHECK(quantapdf_outline_title(outline, 99, &text, &size) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);
    CHECK(size == 0);

    text = (const char *)&sentinel;
    size = 99;
    CHECK(quantapdf_outline_uri(outline, 0, &text, &size) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);
    CHECK(size == 0);

    text = (const char *)&sentinel;
    size = 99;
    CHECK(quantapdf_outline_uri(NULL, 0, &text, &size) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);
    CHECK(size == 0);

    CHECK(quantapdf_outline_uri(outline, 2, NULL, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_close(document);
    quantapdf_drop_outline(outline);
    quantapdf_drop_outline(NULL);
}

int main(void)
{
    test_valid_outline_and_independent_lifetime();
    test_empty_outline();
    test_repairable_outline_is_rejected_without_repair();
    test_cycle_depth_and_pdf_only_boundaries();
    test_argument_and_reset_contract();
    return EXIT_SUCCESS;
}
