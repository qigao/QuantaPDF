#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rewrite_create_gc_fixture(const char *source_path, const char *output_path);
int rewrite_marker_mask(const unsigned char *data, size_t size);
int rewrite_graph_invariants(
    const unsigned char *before,
    size_t before_size,
    const unsigned char *after,
    size_t after_size);
int rewrite_create_catalog_signature_fixture(
    const char *source_path,
    const char *output_path);
int rewrite_create_metadata_fixture(
    const char *source_path,
    const char *output_path);
int rewrite_create_strict_fixture(
    const char *source_path,
    const char *output_path);
int rewrite_create_root_parent_signature_fixture(
    const char *source_path,
    const char *output_path);
int rewrite_create_mismatched_parent_signature_fixture(
    const char *source_path,
    const char *output_path);
int rewrite_create_policy_fixture(
    const char *source_path,
    const char *output_path,
    int kind);

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n",
                __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static unsigned char *read_file(const char *path, size_t *out_size)
{
    FILE *file = NULL;
    unsigned char *data;
    long length;

    *out_size = 0;
#if defined(_WIN32)
    if (fopen_s(&file, path, "rb") != 0)
        return NULL;
#else
    file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
#endif
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = (unsigned char *)malloc((size_t)length);
    if (data == NULL ||
        fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_size = (size_t)length;
    return data;
}

static void open_rewritten_pair(
    const char *path,
    quantapdf_document **out_source,
    quantapdf_document **out_rewritten)
{
    quantapdf_output *output = NULL;
    quantapdf_status status;

    *out_source = NULL;
    *out_rewritten = NULL;
    CHECK(quantapdf_open(test_context(), path, NULL, out_source) == QUANTAPDF_OK);
    status = quantapdf_rewrite_lossless(*out_source, &output);
    if (status != QUANTAPDF_OK)
        fprintf(stderr, "rewrite failed for %s: %s\n",
                path, quantapdf_status_string(status));
    CHECK(status == QUANTAPDF_OK);
    CHECK(output != NULL);
    CHECK(quantapdf_output_save_file(output, REWRITE_SEMANTIC_PDF) ==
          QUANTAPDF_OK);
    quantapdf_drop_output(output);
    CHECK(quantapdf_open(test_context(), REWRITE_SEMANTIC_PDF, NULL, out_rewritten) ==
          QUANTAPDF_OK);
}

typedef struct rendered_page {
    unsigned char *data;
    size_t size;
    int width;
    int height;
    int stride;
    int components;
} rendered_page;

static rendered_page capture_page(quantapdf_document *document)
{
    rendered_page capture = {0};
    quantapdf_page *page = NULL;
    quantapdf_bitmap *bitmap = NULL;
    const unsigned char *borrowed = NULL;

    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_render_page(page, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &capture.width, &capture.height,
              &capture.stride, &capture.components) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_data(bitmap, &borrowed, &capture.size) ==
          QUANTAPDF_OK);
    capture.data = (unsigned char *)malloc(capture.size);
    CHECK(capture.data != NULL);
    memcpy(capture.data, borrowed, capture.size);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    return capture;
}

static void check_point_equal(quantapdf_point left, quantapdf_point right)
{
    CHECK(left.x == right.x);
    CHECK(left.y == right.y);
}

static void check_rect_equal(quantapdf_rect left, quantapdf_rect right)
{
    CHECK(left.x0 == right.x0);
    CHECK(left.y0 == right.y0);
    CHECK(left.x1 == right.x1);
    CHECK(left.y1 == right.y1);
}

static void check_borrowed_equal(
    quantapdf_status left_status,
    const char *left,
    size_t left_size,
    quantapdf_status right_status,
    const char *right,
    size_t right_size)
{
    CHECK(left_status == right_status);
    CHECK((left == NULL) == (right == NULL));
    CHECK(left_size == right_size);
    if (left != NULL)
        CHECK(memcmp(left, right, left_size) == 0);
}

static void check_outline_equal(
    const quantapdf_outline *left,
    const quantapdf_outline *right)
{
    size_t left_count = 0;
    size_t right_count = 0;
    size_t index;

    CHECK(quantapdf_outline_count(left, &left_count) == QUANTAPDF_OK);
    CHECK(quantapdf_outline_count(right, &right_count) == QUANTAPDF_OK);
    CHECK(left_count == right_count);
    for (index = 0; index < left_count; ++index) {
        quantapdf_outline_info left_info = {0};
        quantapdf_outline_info right_info = {0};
        const char *left_text = NULL;
        const char *right_text = NULL;
        size_t left_size = 0;
        size_t right_size = 0;
        quantapdf_status left_status;
        quantapdf_status right_status;

        CHECK(quantapdf_outline_get_info(left, index, &left_info) ==
              QUANTAPDF_OK);
        CHECK(quantapdf_outline_get_info(right, index, &right_info) ==
              QUANTAPDF_OK);
        CHECK(left_info.parent_index == right_info.parent_index);
        CHECK(left_info.first_child_index == right_info.first_child_index);
        CHECK(left_info.next_sibling_index == right_info.next_sibling_index);
        CHECK(left_info.destination_kind == right_info.destination_kind);
        CHECK(left_info.target_page == right_info.target_page);
        check_point_equal(left_info.target, right_info.target);
        CHECK(left_info.is_open == right_info.is_open);

        left_status = quantapdf_outline_title(
            left, index, &left_text, &left_size);
        right_status = quantapdf_outline_title(
            right, index, &right_text, &right_size);
        check_borrowed_equal(
            left_status, left_text, left_size,
            right_status, right_text, right_size);
        left_text = NULL;
        right_text = NULL;
        left_size = 0;
        right_size = 0;
        left_status = quantapdf_outline_uri(
            left, index, &left_text, &left_size);
        right_status = quantapdf_outline_uri(
            right, index, &right_text, &right_size);
        check_borrowed_equal(
            left_status, left_text, left_size,
            right_status, right_text, right_size);
    }
}

static void check_links_equal(
    const quantapdf_link_page *left,
    const quantapdf_link_page *right)
{
    size_t left_count = 0;
    size_t right_count = 0;
    size_t index;

    CHECK(quantapdf_link_count(left, &left_count) == QUANTAPDF_OK);
    CHECK(quantapdf_link_count(right, &right_count) == QUANTAPDF_OK);
    CHECK(left_count == right_count);
    for (index = 0; index < left_count; ++index) {
        quantapdf_link_info left_info = {0};
        quantapdf_link_info right_info = {0};
        const char *left_uri = NULL;
        const char *right_uri = NULL;
        size_t left_size = 0;
        size_t right_size = 0;
        quantapdf_status left_status;
        quantapdf_status right_status;

        CHECK(quantapdf_link_get_info(left, index, &left_info) ==
              QUANTAPDF_OK);
        CHECK(quantapdf_link_get_info(right, index, &right_info) ==
              QUANTAPDF_OK);
        check_rect_equal(left_info.hotspot, right_info.hotspot);
        CHECK(left_info.kind == right_info.kind);
        CHECK(left_info.target_page == right_info.target_page);
        check_point_equal(left_info.target, right_info.target);
        left_status = quantapdf_link_uri(
            left, index, &left_uri, &left_size);
        right_status = quantapdf_link_uri(
            right, index, &right_uri, &right_size);
        check_borrowed_equal(
            left_status, left_uri, left_size,
            right_status, right_uri, right_size);
    }
}

static void check_annotations_equal(
    const quantapdf_annotation_page *left,
    const quantapdf_annotation_page *right)
{
    size_t left_count = 0;
    size_t right_count = 0;
    size_t index;

    CHECK(quantapdf_annotation_count(left, &left_count) == QUANTAPDF_OK);
    CHECK(quantapdf_annotation_count(right, &right_count) == QUANTAPDF_OK);
    CHECK(left_count == right_count);
    for (index = 0; index < left_count; ++index) {
        quantapdf_annotation_info left_info = {0};
        quantapdf_annotation_info right_info = {0};
        const char *left_contents = NULL;
        const char *right_contents = NULL;
        size_t left_size = 0;
        size_t right_size = 0;
        quantapdf_status left_status;
        quantapdf_status right_status;

        CHECK(quantapdf_annotation_get_info(left, index, &left_info) ==
              QUANTAPDF_OK);
        CHECK(quantapdf_annotation_get_info(right, index, &right_info) ==
              QUANTAPDF_OK);
        CHECK(left_info.type == right_info.type);
        check_rect_equal(left_info.bounds, right_info.bounds);
        CHECK(left_info.flags == right_info.flags);
        left_status = quantapdf_annotation_contents(
            left, index, &left_contents, &left_size);
        right_status = quantapdf_annotation_contents(
            right, index, &right_contents, &right_size);
        check_borrowed_equal(
            left_status, left_contents, left_size,
            right_status, right_contents, right_size);
    }
}

static void check_form_equal(
    const quantapdf_form *left,
    const quantapdf_form *right)
{
    size_t left_count = 0;
    size_t right_count = 0;
    size_t field_index;

    CHECK(quantapdf_form_field_count(left, &left_count) == QUANTAPDF_OK);
    CHECK(quantapdf_form_field_count(right, &right_count) == QUANTAPDF_OK);
    CHECK(left_count == right_count);
    for (field_index = 0; field_index < left_count; ++field_index) {
        quantapdf_form_field_info left_info = {0};
        quantapdf_form_field_info right_info = {0};
        const char *left_text = NULL;
        const char *right_text = NULL;
        size_t left_size = 0;
        size_t right_size = 0;
        size_t index;
        quantapdf_status left_status;
        quantapdf_status right_status;

        CHECK(quantapdf_form_field_get_info(
                  left, field_index, &left_info) == QUANTAPDF_OK);
        CHECK(quantapdf_form_field_get_info(
                  right, field_index, &right_info) == QUANTAPDF_OK);
        CHECK(left_info.type == right_info.type);
        CHECK(left_info.flags == right_info.flags);
        CHECK(left_info.value_presence == right_info.value_presence);
        CHECK(left_info.value_count == right_info.value_count);
        CHECK(left_info.option_count == right_info.option_count);
        CHECK(left_info.widget_count == right_info.widget_count);
        CHECK(left_info.is_multiselect == right_info.is_multiselect);
        CHECK(left_info.is_signed == right_info.is_signed);

        left_status = quantapdf_form_field_name(
            left, field_index, &left_text, &left_size);
        right_status = quantapdf_form_field_name(
            right, field_index, &right_text, &right_size);
        check_borrowed_equal(
            left_status, left_text, left_size,
            right_status, right_text, right_size);
        left_text = NULL;
        right_text = NULL;
        left_size = 0;
        right_size = 0;
        left_status = quantapdf_form_field_label(
            left, field_index, &left_text, &left_size);
        right_status = quantapdf_form_field_label(
            right, field_index, &right_text, &right_size);
        check_borrowed_equal(
            left_status, left_text, left_size,
            right_status, right_text, right_size);

        for (index = 0; index < left_info.value_count; ++index) {
            quantapdf_form_value_info left_value = {0};
            quantapdf_form_value_info right_value = {0};

            CHECK(quantapdf_form_field_value_get_info(
                      left, field_index, index, &left_value) == QUANTAPDF_OK);
            CHECK(quantapdf_form_field_value_get_info(
                      right, field_index, index, &right_value) == QUANTAPDF_OK);
            CHECK(left_value.kind == right_value.kind);
            CHECK(left_value.option_index == right_value.option_index);
            if (left_value.kind == QUANTAPDF_FORM_VALUE_UTF8) {
                left_text = NULL;
                right_text = NULL;
                left_size = 0;
                right_size = 0;
                left_status = quantapdf_form_field_value_utf8(
                    left, field_index, index, &left_text, &left_size);
                right_status = quantapdf_form_field_value_utf8(
                    right, field_index, index, &right_text, &right_size);
                check_borrowed_equal(
                    left_status, left_text, left_size,
                    right_status, right_text, right_size);
            }
        }
        for (index = 0; index < left_info.option_count; ++index) {
            quantapdf_form_option_info left_option = {0};
            quantapdf_form_option_info right_option = {0};

            CHECK(quantapdf_form_field_option_get_info(
                      left, field_index, index, &left_option) == QUANTAPDF_OK);
            CHECK(quantapdf_form_field_option_get_info(
                      right, field_index, index, &right_option) == QUANTAPDF_OK);
            CHECK(left_option.kind == right_option.kind);
            left_text = NULL;
            right_text = NULL;
            left_size = 0;
            right_size = 0;
            left_status = quantapdf_form_field_option_export(
                left, field_index, index, &left_text, &left_size);
            right_status = quantapdf_form_field_option_export(
                right, field_index, index, &right_text, &right_size);
            check_borrowed_equal(
                left_status, left_text, left_size,
                right_status, right_text, right_size);
            left_text = NULL;
            right_text = NULL;
            left_size = 0;
            right_size = 0;
            left_status = quantapdf_form_field_option_display(
                left, field_index, index, &left_text, &left_size);
            right_status = quantapdf_form_field_option_display(
                right, field_index, index, &right_text, &right_size);
            check_borrowed_equal(
                left_status, left_text, left_size,
                right_status, right_text, right_size);
        }
    }

    CHECK(quantapdf_form_widget_count(left, &left_count) == QUANTAPDF_OK);
    CHECK(quantapdf_form_widget_count(right, &right_count) == QUANTAPDF_OK);
    CHECK(left_count == right_count);
    for (field_index = 0; field_index < left_count; ++field_index) {
        quantapdf_form_widget_info left_info = {0};
        quantapdf_form_widget_info right_info = {0};

        CHECK(quantapdf_form_widget_get_info(
                  left, field_index, &left_info) == QUANTAPDF_OK);
        CHECK(quantapdf_form_widget_get_info(
                  right, field_index, &right_info) == QUANTAPDF_OK);
        CHECK(left_info.field_index == right_info.field_index);
        CHECK(left_info.page_index == right_info.page_index);
        check_rect_equal(left_info.bounds, right_info.bounds);
        CHECK(left_info.flags == right_info.flags);
        CHECK(left_info.button_option_index == right_info.button_option_index);
    }
}

static void test_render_text_and_page_semantics(void)
{
    quantapdf_document *source = NULL;
    quantapdf_document *rewritten = NULL;
    quantapdf_page *source_page = NULL;
    quantapdf_page *rewritten_page = NULL;
    quantapdf_rect source_bounds = {0};
    quantapdf_rect rewritten_bounds = {0};
    rendered_page source_render;
    rendered_page rewritten_render;
    char *source_text = NULL;
    char *rewritten_text = NULL;
    size_t source_text_size = 0;
    size_t rewritten_text_size = 0;
    int source_pages = 0;
    int rewritten_pages = 0;

    open_rewritten_pair(TEXT_PDF, &source, &rewritten);
    CHECK(quantapdf_page_count(source, &source_pages) == QUANTAPDF_OK);
    CHECK(quantapdf_page_count(rewritten, &rewritten_pages) == QUANTAPDF_OK);
    CHECK(source_pages == rewritten_pages);
    CHECK(quantapdf_load_page(source, 0, &source_page) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(rewritten, 0, &rewritten_page) == QUANTAPDF_OK);
    CHECK(quantapdf_page_bounds(source_page, &source_bounds) == QUANTAPDF_OK);
    CHECK(quantapdf_page_bounds(rewritten_page, &rewritten_bounds) ==
          QUANTAPDF_OK);
    CHECK(memcmp(&source_bounds, &rewritten_bounds, sizeof(source_bounds)) == 0);
    CHECK(quantapdf_extract_text(source_page, &source_text,
                                 &source_text_size) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_text(rewritten_page, &rewritten_text,
                                 &rewritten_text_size) == QUANTAPDF_OK);
    CHECK(source_text_size == rewritten_text_size);
    CHECK(memcmp(source_text, rewritten_text, source_text_size) == 0);
    quantapdf_free(rewritten_text);
    quantapdf_free(source_text);
    quantapdf_drop_page(rewritten_page);
    quantapdf_drop_page(source_page);

    source_render = capture_page(source);
    rewritten_render = capture_page(rewritten);
    CHECK(source_render.width == rewritten_render.width);
    CHECK(source_render.height == rewritten_render.height);
    CHECK(source_render.stride == rewritten_render.stride);
    CHECK(source_render.components == rewritten_render.components);
    CHECK(source_render.size == rewritten_render.size);
    CHECK(memcmp(source_render.data, rewritten_render.data,
                 source_render.size) == 0);
    free(rewritten_render.data);
    free(source_render.data);
    quantapdf_close(rewritten);
    quantapdf_close(source);
}

static void test_snapshot_semantics(void)
{
    quantapdf_document *source = NULL;
    quantapdf_document *rewritten = NULL;
    quantapdf_outline *source_outline = NULL;
    quantapdf_outline *rewritten_outline = NULL;
    quantapdf_form *source_form = NULL;
    quantapdf_form *rewritten_form = NULL;
    quantapdf_page *source_page = NULL;
    quantapdf_page *rewritten_page = NULL;
    quantapdf_link_page *source_links = NULL;
    quantapdf_link_page *rewritten_links = NULL;
    quantapdf_annotation_page *source_annotations = NULL;
    quantapdf_annotation_page *rewritten_annotations = NULL;
    char *source_title = NULL;
    char *rewritten_title = NULL;
    size_t source_size = 0;
    size_t rewritten_size = 0;

    CHECK(rewrite_create_metadata_fixture(
        ONE_PAGE_PDF, REWRITE_METADATA_PDF));
    open_rewritten_pair(REWRITE_METADATA_PDF, &source, &rewritten);
    CHECK(quantapdf_document_metadata(source, QUANTAPDF_METADATA_TITLE,
                                       &source_title, &source_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_document_metadata(rewritten, QUANTAPDF_METADATA_TITLE,
                                       &rewritten_title, &rewritten_size) ==
          QUANTAPDF_OK);
    CHECK(source_size == rewritten_size);
    CHECK(memcmp(source_title, rewritten_title, source_size) == 0);
    quantapdf_free(rewritten_title);
    quantapdf_free(source_title);
    quantapdf_close(rewritten);
    quantapdf_close(source);

    CHECK(rewrite_create_strict_fixture(
        OUTLINE_PDF, REWRITE_VALIDATED_PDF));
    open_rewritten_pair(REWRITE_VALIDATED_PDF, &source, &rewritten);
    CHECK(quantapdf_document_outline(source, &source_outline) == QUANTAPDF_OK);
    CHECK(quantapdf_document_outline(rewritten, &rewritten_outline) ==
          QUANTAPDF_OK);
    check_outline_equal(source_outline, rewritten_outline);
    quantapdf_drop_outline(rewritten_outline);
    quantapdf_drop_outline(source_outline);
    quantapdf_close(rewritten);
    quantapdf_close(source);

    CHECK(rewrite_create_strict_fixture(
        LINKS_PDF, REWRITE_VALIDATED_PDF));
    open_rewritten_pair(REWRITE_VALIDATED_PDF, &source, &rewritten);
    CHECK(quantapdf_load_page(source, 0, &source_page) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(rewritten, 0, &rewritten_page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_links(source_page, &source_links) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_links(rewritten_page, &rewritten_links) ==
          QUANTAPDF_OK);
    check_links_equal(source_links, rewritten_links);
    quantapdf_drop_link_page(rewritten_links);
    quantapdf_drop_link_page(source_links);
    quantapdf_drop_page(rewritten_page);
    quantapdf_drop_page(source_page);
    quantapdf_close(rewritten);
    quantapdf_close(source);

    CHECK(rewrite_create_strict_fixture(
        ANNOTATIONS_PDF, REWRITE_VALIDATED_PDF));
    open_rewritten_pair(REWRITE_VALIDATED_PDF, &source, &rewritten);
    CHECK(quantapdf_load_page(source, 0, &source_page) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(rewritten, 0, &rewritten_page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_annotations(source_page, &source_annotations) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_extract_annotations(
              rewritten_page, &rewritten_annotations) == QUANTAPDF_OK);
    check_annotations_equal(source_annotations, rewritten_annotations);
    quantapdf_drop_annotation_page(rewritten_annotations);
    quantapdf_drop_annotation_page(source_annotations);
    quantapdf_drop_page(rewritten_page);
    quantapdf_drop_page(source_page);
    quantapdf_close(rewritten);
    quantapdf_close(source);

    CHECK(rewrite_create_strict_fixture(
        ACROFORM_PDF, REWRITE_VALIDATED_PDF));
    open_rewritten_pair(REWRITE_VALIDATED_PDF, &source, &rewritten);
    CHECK(quantapdf_document_form(source, &source_form) == QUANTAPDF_OK);
    CHECK(quantapdf_document_form(rewritten, &rewritten_form) == QUANTAPDF_OK);
    check_form_equal(source_form, rewritten_form);
    quantapdf_drop_form(rewritten_form);
    quantapdf_drop_form(source_form);
    quantapdf_close(rewritten);
    quantapdf_close(source);
}

static void expect_rewrite_error(
    const char *path,
    const char *password,
    quantapdf_status expected)
{
    quantapdf_document *document = NULL;
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;
    quantapdf_status status;

    CHECK(quantapdf_open(test_context(), path, password, &document) == QUANTAPDF_OK);
    status = quantapdf_rewrite_lossless(document, &output);
    if (status != expected)
        fprintf(stderr, "rewrite policy mismatch for %s: expected %s, got %s\n",
                path, quantapdf_status_string(expected),
                quantapdf_status_string(status));
    CHECK(status == expected);
    CHECK(output == NULL);
    quantapdf_close(document);
}

static void test_fail_closed_policy(void)
{
    quantapdf_document *unsigned_document = NULL;
    quantapdf_output *unsigned_output = NULL;

    CHECK(rewrite_create_catalog_signature_fixture(
        ONE_PAGE_PDF, REWRITE_SIGNED_PDF));
    expect_rewrite_error(
        REWRITE_SIGNED_PDF, NULL, QUANTAPDF_ERROR_UNSUPPORTED);
    expect_rewrite_error(
        ENCRYPTED_PDF, "user-pass", QUANTAPDF_ERROR_UNSUPPORTED);
    expect_rewrite_error(
        SIGNED_FIELD_PDF, NULL, QUANTAPDF_ERROR_UNSUPPORTED);
    expect_rewrite_error(
        REPAIRABLE_BAD_PDF, NULL, QUANTAPDF_ERROR_FORMAT);

    CHECK(rewrite_create_root_parent_signature_fixture(
        ONE_PAGE_PDF, REWRITE_ROOT_PARENT_SIGNED_PDF));
    expect_rewrite_error(
        REWRITE_ROOT_PARENT_SIGNED_PDF, NULL, QUANTAPDF_ERROR_FORMAT);
    CHECK(rewrite_create_mismatched_parent_signature_fixture(
        ONE_PAGE_PDF, REWRITE_PARENT_MISMATCH_SIGNED_PDF));
    expect_rewrite_error(
        REWRITE_PARENT_MISMATCH_SIGNED_PDF, NULL, QUANTAPDF_ERROR_FORMAT);

    CHECK(rewrite_create_policy_fixture(
        ONE_PAGE_PDF, REWRITE_POLICY_PDF, 1));
    expect_rewrite_error(
        REWRITE_POLICY_PDF, NULL, QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(rewrite_create_policy_fixture(
        ONE_PAGE_PDF, REWRITE_POLICY_PDF, 2));
    expect_rewrite_error(
        REWRITE_POLICY_PDF, NULL, QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(rewrite_create_policy_fixture(
        ONE_PAGE_PDF, REWRITE_POLICY_PDF, 3));
    expect_rewrite_error(
        REWRITE_POLICY_PDF, NULL, QUANTAPDF_ERROR_FORMAT);
    CHECK(rewrite_create_policy_fixture(
        ONE_PAGE_PDF, REWRITE_POLICY_PDF, 4));
    expect_rewrite_error(
        REWRITE_POLICY_PDF, NULL, QUANTAPDF_ERROR_FORMAT);
    CHECK(rewrite_create_policy_fixture(
        ONE_PAGE_PDF, REWRITE_POLICY_PDF, 5));
    expect_rewrite_error(
        REWRITE_POLICY_PDF, NULL, QUANTAPDF_ERROR_FORMAT);
    CHECK(rewrite_create_policy_fixture(
        ONE_PAGE_PDF, REWRITE_POLICY_PDF, 6));
    expect_rewrite_error(
        REWRITE_POLICY_PDF, NULL, QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(rewrite_create_policy_fixture(
        ONE_PAGE_PDF, REWRITE_POLICY_PDF, 7));
    expect_rewrite_error(
        REWRITE_POLICY_PDF, NULL, QUANTAPDF_ERROR_UNSUPPORTED);

    CHECK(quantapdf_open(test_context(),
              UNSIGNED_SIGNATURE_PDF, NULL, &unsigned_document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_rewrite_lossless(unsigned_document, &unsigned_output) ==
          QUANTAPDF_OK);
    CHECK(unsigned_output != NULL);
    quantapdf_drop_output(unsigned_output);
    quantapdf_close(unsigned_document);
}

static void test_gc_and_repeated_determinism(void)
{
    quantapdf_document *document = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_output *third = NULL;
    quantapdf_document *rewritten_document = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    unsigned char *source_data;
    size_t source_size = 0;
    size_t first_size = 0;
    size_t second_size = 0;
    const unsigned char *third_data = NULL;
    size_t third_size = 0;
    int page_count = 0;

    CHECK(rewrite_create_gc_fixture(ONE_PAGE_PDF, REWRITE_GC_PDF));
    source_data = read_file(REWRITE_GC_PDF, &source_size);
    CHECK(source_data != NULL);
    CHECK(rewrite_marker_mask(source_data, source_size) == 7);

    CHECK(quantapdf_open(test_context(), REWRITE_GC_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    CHECK(quantapdf_rewrite_lossless(document, &first) == QUANTAPDF_OK);
    CHECK(first != NULL);
    CHECK(quantapdf_rewrite_lossless(document, &second) == QUANTAPDF_OK);
    CHECK(second != NULL);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_data != NULL && second_data != NULL);
    CHECK(first_size != 0 && first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);
    CHECK(rewrite_marker_mask(first_data, first_size) == 4);
    CHECK(rewrite_graph_invariants(
        source_data, source_size, first_data, first_size));
    free(source_data);
    CHECK(quantapdf_page_count(document, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 1);
    CHECK(quantapdf_output_save_file(first, REWRITE_OUTPUT_PDF) ==
          QUANTAPDF_OK);

    quantapdf_close(document);
    document = NULL;
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(first_data != NULL && first_size != 0);
    CHECK(quantapdf_open(test_context(), REWRITE_OUTPUT_PDF, NULL, &rewritten_document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_rewrite_lossless(rewritten_document, &third) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(third, &third_data, &third_size) ==
          QUANTAPDF_OK);
    CHECK(first_size == third_size);
    CHECK(memcmp(first_data, third_data, first_size) == 0);
    quantapdf_drop_output(third);
    quantapdf_close(rewritten_document);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
}

int main(void)
{
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;

    CHECK(quantapdf_rewrite_lossless(NULL, &output) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(output == NULL);
    CHECK(quantapdf_rewrite_lossless(NULL, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);
    test_gc_and_repeated_determinism();
    test_render_text_and_page_semantics();
    test_snapshot_semantics();
    test_fail_closed_policy();
    return EXIT_SUCCESS;
}
