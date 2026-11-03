#include "test_context_fixture.h"
#include "test_pdf_poster_split_internal.h"

#include <quantapdf/quantapdf.h>

#include <math.h>
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

static int close_float(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

static quantapdf_document *open_document(const char *path)
{
    quantapdf_document *document = NULL;
    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    return document;
}

static void expect_source_form_valid(quantapdf_document *source)
{
    quantapdf_form *form = NULL;
    CHECK(quantapdf_document_form(source, &form) == QUANTAPDF_OK);
    CHECK(form != NULL);
    quantapdf_drop_form(form);
}

static quantapdf_output *split_interactive(quantapdf_document *source)
{
    quantapdf_page_poster_split split;
    quantapdf_output *output = NULL;

    split.page_index = 0;
    split.columns = 2;
    split.rows = 2;
    CHECK(quantapdf_poster_split_pages(source, &split, 1, &output) ==
          QUANTAPDF_OK);
    CHECK(output != NULL);
    CHECK(quantapdf_output_save_file(output, POSTER_OUTPUT_PDF) == QUANTAPDF_OK);
    return output;
}

static void expect_uri_link(
    quantapdf_document *document,
    int page_index,
    size_t expected_count,
    const char *uri,
    quantapdf_rect expected_rect)
{
    quantapdf_page *page = NULL;
    quantapdf_link_page *links = NULL;
    size_t count = 0;
    size_t index;
    int found = 0;

    CHECK(quantapdf_load_page(document, page_index, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_links(page, &links) == QUANTAPDF_OK);
    CHECK(quantapdf_link_count(links, &count) == QUANTAPDF_OK);
    CHECK(count == expected_count);
    for (index = 0; index < count; ++index) {
        quantapdf_link_info info;
        const char *actual_uri = NULL;
        size_t uri_size = 0;

        CHECK(quantapdf_link_get_info(links, index, &info) == QUANTAPDF_OK);
        if (info.kind != QUANTAPDF_LINK_URI)
            continue;
        CHECK(quantapdf_link_uri(links, index, &actual_uri, &uri_size) == QUANTAPDF_OK);
        if (strlen(uri) == uri_size && memcmp(actual_uri, uri, uri_size) == 0) {
            CHECK(close_float(info.hotspot.x0, expected_rect.x0));
            CHECK(close_float(info.hotspot.y0, expected_rect.y0));
            CHECK(close_float(info.hotspot.x1, expected_rect.x1));
            CHECK(close_float(info.hotspot.y1, expected_rect.y1));
            found = 1;
        }
    }
    CHECK(found);
    quantapdf_drop_link_page(links);
    quantapdf_drop_page(page);
}

static void expect_no_links(quantapdf_document *document, int page_index)
{
    quantapdf_page *page = NULL;
    quantapdf_link_page *links = NULL;
    size_t count = 1;
    CHECK(quantapdf_load_page(document, page_index, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_links(page, &links) == QUANTAPDF_OK);
    CHECK(quantapdf_link_count(links, &count) == QUANTAPDF_OK);
    CHECK(count == 0);
    quantapdf_drop_link_page(links);
    quantapdf_drop_page(page);
}

static void expect_square(quantapdf_document *document)
{
    quantapdf_page *page = NULL;
    quantapdf_annotation_page *annotations = NULL;
    quantapdf_annotation_info info;
    const char *contents = NULL;
    size_t contents_size = 0;
    size_t count = 0;

    CHECK(quantapdf_load_page(document, 2, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_annotations(page, &annotations) == QUANTAPDF_OK);
    CHECK(quantapdf_annotation_count(annotations, &count) == QUANTAPDF_OK);
    CHECK(count == 1);

    CHECK(quantapdf_annotation_get_info(annotations, 0, &info) == QUANTAPDF_OK);
    CHECK(info.type == QUANTAPDF_ANNOTATION_SQUARE);
    CHECK(close_float(info.bounds.x0, 30.0f));
    CHECK(close_float(info.bounds.y0, 60.0f));
    CHECK(close_float(info.bounds.x1, 80.0f));
    CHECK(close_float(info.bounds.y1, 110.0f));
    CHECK(quantapdf_annotation_contents(
              annotations, 0, &contents, &contents_size) == QUANTAPDF_OK);
    CHECK(contents_size == strlen("POSTER-ANNOT"));
    CHECK(memcmp(contents, "POSTER-ANNOT", contents_size) == 0);
    quantapdf_drop_annotation_page(annotations);
    quantapdf_drop_page(page);
}

static void expect_widget(quantapdf_document *document)
{
    quantapdf_form *form = NULL;
    quantapdf_form_widget_info widget;
    quantapdf_form_field_info field;
    const char *name = NULL;
    const char *value = NULL;
    size_t name_size = 0;
    size_t value_size = 0;
    size_t field_count = 0;
    size_t widget_count = 0;
    quantapdf_form_value_info value_info;

    CHECK(quantapdf_document_form(document, &form) == QUANTAPDF_OK);
    CHECK(form != NULL);
    CHECK(quantapdf_form_field_count(form, &field_count) == QUANTAPDF_OK);
    CHECK(field_count >= 1);
    CHECK(quantapdf_form_widget_count(form, &widget_count) == QUANTAPDF_OK);
    CHECK(widget_count == 1);

    CHECK(quantapdf_form_widget_get_info(form, 0, &widget) == QUANTAPDF_OK);
    CHECK(widget.page_index == 3);
    CHECK(close_float(widget.bounds.x0, 50.0f));
    CHECK(close_float(widget.bounds.y0, 70.0f));
    CHECK(close_float(widget.bounds.x1, 150.0f));
    CHECK(close_float(widget.bounds.y1, 110.0f));

    CHECK(quantapdf_form_field_get_info(form, widget.field_index, &field) == QUANTAPDF_OK);
    CHECK(field.widget_count == 1);
    CHECK(field.value_count == 1);
    CHECK(quantapdf_form_field_name(
              form, widget.field_index, &name, &name_size) == QUANTAPDF_OK);
    CHECK(name_size == strlen("poster.text"));
    CHECK(memcmp(name, "poster.text", name_size) == 0);

    CHECK(quantapdf_form_field_value_get_info(
              form, widget.field_index, 0, &value_info) == QUANTAPDF_OK);
    CHECK(value_info.kind == QUANTAPDF_FORM_VALUE_UTF8);
    CHECK(quantapdf_form_field_value_utf8(
              form, widget.field_index, 0, &value, &value_size) == QUANTAPDF_OK);
    CHECK(value_size == strlen("POSTER-VALUE"));
    CHECK(memcmp(value, "POSTER-VALUE", value_size) == 0);

    quantapdf_drop_form(form);
}

int poster_run_interactive_tests(void)
{
    quantapdf_document *source = open_document(POSTER_INTERACTIVE_PDF);
    quantapdf_output *output;
    quantapdf_document *reopened;
    const unsigned char *data = NULL;
    size_t size = 0;
    int count = 0;

    expect_source_form_valid(source);
    output = split_interactive(source);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(data != NULL && size != 0);
    CHECK(poster_raw_check_interactive(data, size));
    reopened = open_document(POSTER_OUTPUT_PDF);

    CHECK(quantapdf_page_count(reopened, &count) == QUANTAPDF_OK);
    CHECK(count == 5);

    expect_uri_link(
        reopened, 0, 2, "https://example.com/contained",
        (quantapdf_rect){20.0f, 40.0f, 120.0f, 80.0f});
    expect_uri_link(
        reopened, 0, 2, "https://example.com/crossing",
        (quantapdf_rect){150.0f, 40.0f, 200.0f, 90.0f});
    expect_uri_link(
        reopened, 1, 1, "https://example.com/crossing",
        (quantapdf_rect){0.0f, 40.0f, 50.0f, 90.0f});
    expect_no_links(reopened, 2);
    expect_no_links(reopened, 3);

    expect_square(reopened);
    expect_widget(reopened);

    quantapdf_close(reopened);
    quantapdf_drop_output(output);
    quantapdf_close(source);
    return 0;
}
