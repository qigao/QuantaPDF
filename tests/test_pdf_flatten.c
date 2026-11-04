#include <quantapdf/quantapdf.h>

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

int quantapdf_flatten_raw_check_combined(
    const unsigned char *data,
    size_t size);
int quantapdf_flatten_raw_check_form_closure(
    const unsigned char *data,
    size_t size,
    int ancestor_survives);
int quantapdf_flatten_raw_check_calculation_order(
    const unsigned char *data,
    size_t size);
int quantapdf_flatten_raw_check_contents(
    const unsigned char *data,
    size_t size);
int quantapdf_flatten_raw_check_number_format(
    const unsigned char *data,
    size_t size);
int quantapdf_flatten_make_variant(
    const char *source_path,
    const char *output_path,
    int variant);

enum {
    FLATTEN_VARIANT_BAD_APPEARANCE_STREAM = 1,
    FLATTEN_VARIANT_CA_HALF = 2,
    FLATTEN_VARIANT_CA_MALFORMED = 3,
    FLATTEN_VARIANT_BAD_PAGE_CONTENT_STREAM = 4,
    FLATTEN_VARIANT_TRUNCATED_APPEARANCE_STREAM = 5,
    FLATTEN_VARIANT_BAD_APPEARANCE_OPERATOR = 6,
    FLATTEN_VARIANT_UNBALANCED_PAGE_Q = 7,
    FLATTEN_VARIANT_UNBALANCED_PAGE_TEXT = 8,
    FLATTEN_VARIANT_VALID_WIDGET_P_MISMATCH = 9,
    FLATTEN_VARIANT_BAD_APPEARANCE_ARITY = 10,
    FLATTEN_VARIANT_BAD_PAGE_OPERAND_TYPE = 11,
    FLATTEN_VARIANT_BAD_APPEARANCE_CONTEXT = 12,
    FLATTEN_VARIANT_BAD_PAGE_PATH_CONTEXT = 13,
    FLATTEN_VARIANT_BAD_PAGE_TEXT_PATH = 14,
    FLATTEN_VARIANT_VALID_INLINE_APPEARANCE = 15,
    FLATTEN_VARIANT_VALID_TEXT_GSTATE = 16,
    FLATTEN_VARIANT_CROSSED_SCOPES = 17
};

static size_t annotation_count(const char *path)
{
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_annotation_page *annotations = NULL;
    size_t count = 0;

    CHECK(quantapdf_open(path, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_annotations(page, &annotations) == QUANTAPDF_OK);
    CHECK(quantapdf_annotation_count(annotations, &count) == QUANTAPDF_OK);
    quantapdf_drop_annotation_page(annotations);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    return count;
}

static quantapdf_status flatten_status(const char *path, uint32_t flags)
{
    quantapdf_document *document = NULL;
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;
    quantapdf_status status;

    CHECK(quantapdf_open(path, NULL, &document) == QUANTAPDF_OK);
    status = quantapdf_flatten_interactive(document, flags, &output);
    if (status == QUANTAPDF_OK)
        quantapdf_drop_output(output);
    else
        CHECK(output == NULL);
    quantapdf_close(document);
    return status;
}

static quantapdf_status flatten_status_with_password(
    const char *path,
    const char *password,
    uint32_t flags)
{
    quantapdf_document *document = NULL;
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;
    quantapdf_status status;

    CHECK(quantapdf_open(path, password, &document) == QUANTAPDF_OK);
    status = quantapdf_flatten_interactive(document, flags, &output);
    if (status == QUANTAPDF_OK)
        quantapdf_drop_output(output);
    else
        CHECK(output == NULL);
    quantapdf_close(document);
    return status;
}

static void check_form_counts(
    quantapdf_document *document,
    size_t expected_fields,
    size_t expected_widgets)
{
    quantapdf_form *form = NULL;
    size_t fields = SIZE_MAX;
    size_t widgets = SIZE_MAX;

    CHECK(quantapdf_document_form(document, &form) == QUANTAPDF_OK);
    CHECK(quantapdf_form_field_count(form, &fields) == QUANTAPDF_OK);
    CHECK(quantapdf_form_widget_count(form, &widgets) == QUANTAPDF_OK);
    CHECK(fields == expected_fields);
    CHECK(widgets == expected_widgets);
    quantapdf_drop_form(form);
}

static void check_form_transform(
    const char *path,
    size_t source_fields,
    size_t source_widgets,
    size_t result_fields,
    size_t result_widgets)
{
    quantapdf_document *source = NULL;
    quantapdf_document *result = NULL;
    quantapdf_output *output = NULL;

    CHECK(quantapdf_open(path, NULL, &source) == QUANTAPDF_OK);
    check_form_counts(source, source_fields, source_widgets);
    CHECK(quantapdf_flatten_interactive(
        source, QUANTAPDF_FLATTEN_WIDGETS, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(output, FLATTEN_OUTPUT_PDF) == QUANTAPDF_OK);
    CHECK(quantapdf_open(FLATTEN_OUTPUT_PDF, NULL, &result) == QUANTAPDF_OK);
    check_form_counts(result, result_fields, result_widgets);
    check_form_counts(source, source_fields, source_widgets);
    quantapdf_close(result);
    quantapdf_drop_output(output);
    quantapdf_close(source);
}

static void check_raw_form_closure(const char *path, int ancestor_survives)
{
    quantapdf_document *document = NULL;
    quantapdf_output *output = NULL;
    const unsigned char *data = NULL;
    size_t size = 0;

    CHECK(quantapdf_open(path, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_flatten_interactive(
        document, QUANTAPDF_FLATTEN_WIDGETS, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(quantapdf_flatten_raw_check_form_closure(
        data, size, ancestor_survives));
    quantapdf_drop_output(output);
    quantapdf_close(document);
}

static void check_raw_combined(void)
{
    quantapdf_document *document = NULL;
    quantapdf_output *output = NULL;
    const unsigned char *data = NULL;
    size_t size = 0;

    CHECK(quantapdf_open(FLATTEN_COMBINED_ORDER_PDF, NULL, &document) ==
        QUANTAPDF_OK);
    CHECK(quantapdf_flatten_interactive(
        document,
        QUANTAPDF_FLATTEN_ANNOTATIONS | QUANTAPDF_FLATTEN_WIDGETS,
        &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(quantapdf_flatten_raw_check_combined(data, size));
    quantapdf_drop_output(output);
    quantapdf_close(document);
}

static void check_raw_calculation_order(void)
{
    quantapdf_document *document = NULL;
    quantapdf_output *output = NULL;
    const unsigned char *data = NULL;
    size_t size = 0;

    CHECK(quantapdf_open(FLATTEN_FORM_CO_COW_PDF, NULL, &document) ==
        QUANTAPDF_OK);
    CHECK(quantapdf_flatten_interactive(
        document, QUANTAPDF_FLATTEN_WIDGETS, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(quantapdf_flatten_raw_check_calculation_order(data, size));
    quantapdf_drop_output(output);
    quantapdf_close(document);
}

static void check_raw_contents(void)
{
    quantapdf_document *document = NULL;
    quantapdf_output *output = NULL;
    const unsigned char *data = NULL;
    size_t size = 0;

    CHECK(quantapdf_open(FLATTEN_CONTENTS_PDF, NULL, &document) ==
        QUANTAPDF_OK);
    CHECK(quantapdf_flatten_interactive(
        document, QUANTAPDF_FLATTEN_ANNOTATIONS, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(quantapdf_flatten_raw_check_contents(data, size));
    quantapdf_drop_output(output);
    quantapdf_close(document);
}

static void check_raw_number_format(void)
{
    quantapdf_document *document = NULL;
    quantapdf_output *output = NULL;
    const unsigned char *data = NULL;
    size_t size = 0;

    CHECK(quantapdf_open(FLATTEN_NUMBER_FORMAT_PDF, NULL, &document) ==
        QUANTAPDF_OK);
    CHECK(quantapdf_flatten_interactive(
        document, QUANTAPDF_FLATTEN_ANNOTATIONS, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(quantapdf_flatten_raw_check_number_format(data, size));
    quantapdf_drop_output(output);
    quantapdf_close(document);
}

static void compare_rendered_page(
    quantapdf_document *source,
    quantapdf_document *result,
    int page_index)
{
    quantapdf_page *source_page = NULL;
    quantapdf_page *result_page = NULL;
    quantapdf_bitmap *source_bitmap = NULL;
    quantapdf_bitmap *result_bitmap = NULL;
    quantapdf_render_options options = {0};
    const unsigned char *source_data = NULL;
    const unsigned char *result_data = NULL;
    size_t source_size = 0;
    size_t result_size = 0;
    int sw = 0, sh = 0, ss = 0, sc = 0;
    int rw = 0, rh = 0, rs = 0, rc = 0;

    options.dpi = 72.0f;
    CHECK(quantapdf_load_page(source, page_index, &source_page) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(result, page_index, &result_page) == QUANTAPDF_OK);
    CHECK(quantapdf_render_page_with_options(
        source_page, &options, &source_bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_render_page_with_options(
        result_page, &options, &result_bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(source_bitmap, &sw, &sh, &ss, &sc) ==
        QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(result_bitmap, &rw, &rh, &rs, &rc) ==
        QUANTAPDF_OK);
    CHECK(sw == rw && sh == rh && ss == rs && sc == rc);
    CHECK(quantapdf_bitmap_data(source_bitmap, &source_data, &source_size) ==
        QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_data(result_bitmap, &result_data, &result_size) ==
        QUANTAPDF_OK);
    CHECK(source_size == result_size);
    CHECK(memcmp(source_data, result_data, source_size) == 0);
    quantapdf_drop_bitmap(result_bitmap);
    quantapdf_drop_bitmap(source_bitmap);
    quantapdf_drop_page(result_page);
    quantapdf_drop_page(source_page);
}

static void check_render_fixture(
    const char *path,
    uint32_t flags,
    int page_count,
    int expect_empty_form)
{
    quantapdf_document *source = NULL;
    quantapdf_document *result = NULL;
    quantapdf_output *output = NULL;
    quantapdf_status status;
    int page_index;

    CHECK(quantapdf_open(path, NULL, &source) == QUANTAPDF_OK);
    status = quantapdf_flatten_interactive(source, flags, &output);
    if (status != QUANTAPDF_OK)
        fprintf(stderr, "flatten failed for %s: %s\n", path,
            quantapdf_status_string(status));
    CHECK(status == QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(output, FLATTEN_OUTPUT_PDF) == QUANTAPDF_OK);
    CHECK(quantapdf_open(FLATTEN_OUTPUT_PDF, NULL, &result) == QUANTAPDF_OK);
    for (page_index = 0; page_index < page_count; ++page_index)
        compare_rendered_page(source, result, page_index);
    if (expect_empty_form) {
        quantapdf_form *form = NULL;
        size_t fields = SIZE_MAX;
        size_t widgets = SIZE_MAX;
        CHECK(quantapdf_document_form(result, &form) == QUANTAPDF_OK);
        CHECK(quantapdf_form_field_count(form, &fields) == QUANTAPDF_OK);
        CHECK(quantapdf_form_widget_count(form, &widgets) == QUANTAPDF_OK);
        CHECK(fields == 0 && widgets == 0);
        quantapdf_drop_form(form);
    }
    quantapdf_close(result);
    quantapdf_drop_output(output);
    quantapdf_close(source);
}

static void check_deterministic(const char *path, uint32_t flags)
{
    quantapdf_document *document = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    size_t first_size = 0;
    size_t second_size = 0;

    CHECK(quantapdf_open(path, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_flatten_interactive(document, flags, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_flatten_interactive(document, flags, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) == QUANTAPDF_OK);
    CHECK(first_size != 0 && first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_close(document);
}

static void check_idempotent(const char *path, uint32_t flags)
{
    quantapdf_document *source = NULL;
    quantapdf_document *result = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    size_t first_size = 0;
    size_t second_size = 0;

    CHECK(quantapdf_open(path, NULL, &source) == QUANTAPDF_OK);
    CHECK(quantapdf_flatten_interactive(source, flags, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(first, FLATTEN_OUTPUT_PDF) == QUANTAPDF_OK);
    CHECK(quantapdf_open(FLATTEN_OUTPUT_PDF, NULL, &result) == QUANTAPDF_OK);
    CHECK(quantapdf_flatten_interactive(result, flags, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) == QUANTAPDF_OK);
    CHECK(first_size == second_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_close(result);
    quantapdf_close(source);
}

int main(void)
{
    quantapdf_document *document = NULL;
    quantapdf_document *no_annotations = NULL;
    quantapdf_output *output = NULL;
    quantapdf_output *sentinel = (quantapdf_output *)(uintptr_t)1;

    CHECK(quantapdf_open(FLATTEN_BASIC_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    CHECK(annotation_count(FLATTEN_BASIC_PDF) == 1);

    CHECK(quantapdf_flatten_interactive(
        NULL, QUANTAPDF_FLATTEN_ANNOTATIONS, &sentinel) ==
        QUANTAPDF_ERROR_ARGUMENT);
    CHECK(sentinel == NULL);

    sentinel = (quantapdf_output *)(uintptr_t)1;
    CHECK(quantapdf_flatten_interactive(document, 0, &sentinel) ==
        QUANTAPDF_ERROR_ARGUMENT);
    CHECK(sentinel == NULL);

    sentinel = (quantapdf_output *)(uintptr_t)1;
    CHECK(quantapdf_flatten_interactive(document, 1u << 31, &sentinel) ==
        QUANTAPDF_ERROR_ARGUMENT);
    CHECK(sentinel == NULL);

    CHECK(quantapdf_flatten_interactive(
        document, QUANTAPDF_FLATTEN_ANNOTATIONS, NULL) ==
        QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_open(
        FLATTEN_NO_ANNOTATIONS_PDF, NULL, &no_annotations) == QUANTAPDF_OK);
    CHECK(quantapdf_flatten_interactive(
        no_annotations, QUANTAPDF_FLATTEN_ANNOTATIONS, &output) ==
        QUANTAPDF_OK);
    CHECK(output != NULL);
    quantapdf_drop_output(output);
    quantapdf_close(no_annotations);
    output = NULL;

    CHECK(quantapdf_flatten_interactive(
        document, QUANTAPDF_FLATTEN_ANNOTATIONS, &output) == QUANTAPDF_OK);
    CHECK(output != NULL);
    CHECK(quantapdf_output_save_file(output, FLATTEN_OUTPUT_PDF) ==
        QUANTAPDF_OK);
    CHECK(annotation_count(FLATTEN_OUTPUT_PDF) == 0);

    quantapdf_drop_output(output);
    quantapdf_close(document);

    check_render_fixture(
        FLATTEN_APPEARANCE_PDF, QUANTAPDF_FLATTEN_ANNOTATIONS, 6, 0);
    check_render_fixture(
        FLATTEN_WIDGETS_PDF, QUANTAPDF_FLATTEN_WIDGETS, 1, 1);
    check_render_fixture(
        FLATTEN_WIDGET_AS_PDF, QUANTAPDF_FLATTEN_WIDGETS, 1, 1);
    check_render_fixture(
        FLATTEN_WIDGET_RADIO_AS_PDF, QUANTAPDF_FLATTEN_WIDGETS, 1, 1);
    check_render_fixture(
        FLATTEN_COMBINED_ORDER_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS | QUANTAPDF_FLATTEN_WIDGETS,
        1,
        1);
    CHECK(flatten_status(
        FLATTEN_FLAG_ISOLATION_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_OK);
    check_form_transform(FLATTEN_FLAG_ISOLATION_PDF, 1, 1, 0, 0);
    check_deterministic(
        FLATTEN_NUMBER_FORMAT_PDF, QUANTAPDF_FLATTEN_ANNOTATIONS);
    check_deterministic(
        FLATTEN_APPEARANCE_PDF, QUANTAPDF_FLATTEN_ANNOTATIONS);
    check_deterministic(FLATTEN_WIDGET_AS_PDF, QUANTAPDF_FLATTEN_WIDGETS);
    check_idempotent(FLATTEN_BASIC_PDF, QUANTAPDF_FLATTEN_ANNOTATIONS);
    check_idempotent(FLATTEN_WIDGET_AS_PDF, QUANTAPDF_FLATTEN_WIDGETS);
    check_form_transform(FLATTEN_FORM_COW_PDF, 2, 1, 1, 0);
    check_form_transform(FLATTEN_FORM_KIDS_COW_PDF, 2, 1, 1, 0);
    check_form_transform(FLATTEN_FORM_CO_COW_PDF, 3, 1, 2, 0);
    check_form_transform(FLATTEN_FORM_CLOSURE_PDF, 2, 1, 1, 0);
    check_form_transform(
        FLATTEN_FORM_ANCESTOR_SURVIVES_PDF, 2, 1, 1, 0);
    check_form_transform(FLATTEN_MULTI_WIDGET_PDF, 1, 2, 0, 0);
    check_form_transform(FLATTEN_FORM_MULTI_ROOT_PDF, 2, 2, 0, 0);
    check_form_transform(FLATTEN_FORM_DEEP_PDF, 1, 1, 0, 0);
    check_form_transform(FLATTEN_FORM_DEEP_SURVIVOR_PDF, 2, 1, 1, 0);
    check_raw_form_closure(FLATTEN_FORM_CLOSURE_PDF, 0);
    check_raw_form_closure(FLATTEN_FORM_ANCESTOR_SURVIVES_PDF, 1);
    check_raw_combined();
    check_raw_calculation_order();
    check_raw_contents();
    check_raw_number_format();

    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF,
        FLATTEN_BAD_APPEARANCE_STREAM_PDF,
        FLATTEN_VARIANT_BAD_APPEARANCE_STREAM));
    CHECK(flatten_status(
        FLATTEN_BAD_APPEARANCE_STREAM_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF,
        FLATTEN_BAD_PAGE_CONTENT_STREAM_PDF,
        FLATTEN_VARIANT_BAD_PAGE_CONTENT_STREAM));
    CHECK(flatten_status(
        FLATTEN_BAD_PAGE_CONTENT_STREAM_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF,
        FLATTEN_TRUNCATED_APPEARANCE_STREAM_PDF,
        FLATTEN_VARIANT_TRUNCATED_APPEARANCE_STREAM));
    CHECK(flatten_status(
        FLATTEN_TRUNCATED_APPEARANCE_STREAM_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF,
        FLATTEN_BAD_APPEARANCE_OPERATOR_PDF,
        FLATTEN_VARIANT_BAD_APPEARANCE_OPERATOR));
    CHECK(flatten_status(
        FLATTEN_BAD_APPEARANCE_OPERATOR_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF,
        FLATTEN_UNBALANCED_PAGE_Q_PDF,
        FLATTEN_VARIANT_UNBALANCED_PAGE_Q));
    CHECK(flatten_status(
        FLATTEN_UNBALANCED_PAGE_Q_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF,
        FLATTEN_UNBALANCED_PAGE_TEXT_PDF,
        FLATTEN_VARIANT_UNBALANCED_PAGE_TEXT));
    CHECK(flatten_status(
        FLATTEN_UNBALANCED_PAGE_TEXT_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF,
        FLATTEN_BAD_APPEARANCE_ARITY_PDF,
        FLATTEN_VARIANT_BAD_APPEARANCE_ARITY));
    CHECK(flatten_status(
        FLATTEN_BAD_APPEARANCE_ARITY_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF,
        FLATTEN_BAD_PAGE_OPERAND_TYPE_PDF,
        FLATTEN_VARIANT_BAD_PAGE_OPERAND_TYPE));
    CHECK(flatten_status(
        FLATTEN_BAD_PAGE_OPERAND_TYPE_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF,
        FLATTEN_BAD_APPEARANCE_CONTEXT_PDF,
        FLATTEN_VARIANT_BAD_APPEARANCE_CONTEXT));
    CHECK(flatten_status(
        FLATTEN_BAD_APPEARANCE_CONTEXT_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF,
        FLATTEN_BAD_PAGE_PATH_CONTEXT_PDF,
        FLATTEN_VARIANT_BAD_PAGE_PATH_CONTEXT));
    CHECK(flatten_status(
        FLATTEN_BAD_PAGE_PATH_CONTEXT_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF,
        FLATTEN_BAD_PAGE_TEXT_PATH_PDF,
        FLATTEN_VARIANT_BAD_PAGE_TEXT_PATH));
    CHECK(flatten_status(
        FLATTEN_BAD_PAGE_TEXT_PATH_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF,
        FLATTEN_VALID_INLINE_APPEARANCE_PDF,
        FLATTEN_VARIANT_VALID_INLINE_APPEARANCE));
    {
        quantapdf_status inline_status = flatten_status(
            FLATTEN_VALID_INLINE_APPEARANCE_PDF,
            QUANTAPDF_FLATTEN_ANNOTATIONS);
        if (inline_status != QUANTAPDF_OK)
            fprintf(stderr, "inline appearance: %s\n",
                quantapdf_status_string(inline_status));
        CHECK(inline_status == QUANTAPDF_OK);
    }
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF,
        FLATTEN_VALID_TEXT_GSTATE_PDF,
        FLATTEN_VARIANT_VALID_TEXT_GSTATE));
    CHECK(flatten_status(
        FLATTEN_VALID_TEXT_GSTATE_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_OK);
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF,
        FLATTEN_CROSSED_SCOPES_PDF,
        FLATTEN_VARIANT_CROSSED_SCOPES));
    CHECK(flatten_status(
        FLATTEN_CROSSED_SCOPES_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF, FLATTEN_CA_HALF_PDF, FLATTEN_VARIANT_CA_HALF));
    CHECK(flatten_status(
        FLATTEN_CA_HALF_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_BASIC_PDF,
        FLATTEN_CA_MALFORMED_PDF,
        FLATTEN_VARIANT_CA_MALFORMED));
    CHECK(flatten_status(
        FLATTEN_CA_MALFORMED_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);

    CHECK(flatten_status(
        FLATTEN_APPEARANCE_MALFORMED_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_CONTENTS_MALFORMED_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(flatten_status(
        FLATTEN_REDACT_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_LINK_DEFAULT_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_TAGGED_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_TAGGED_NOOP_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_OK);
    CHECK(flatten_status(
        FLATTEN_NEED_APPEARANCES_PDF,
        QUANTAPDF_FLATTEN_WIDGETS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_XFA_PDF,
        QUANTAPDF_FLATTEN_WIDGETS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_ANNOTATION_WITH_WIDGET_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_ANNOTATION_WITH_WIDGET_PDF,
        QUANTAPDF_FLATTEN_WIDGETS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_LINK_BS0_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_OK);
    CHECK(flatten_status(
        FLATTEN_LINK_BORDER0_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_OK);
    CHECK(flatten_status(
        FLATTEN_LINK_BS0_BORDER4_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_OK);
    CHECK(flatten_status(
        FLATTEN_LINK_BS4_BORDER0_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_LINK_BS_MISSING_W_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_LINK_APN_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_LINK_BS_MALFORMED_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(flatten_status(
        FLATTEN_LINK_BAD_W_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(flatten_status(
        FLATTEN_LINK_BAD_BORDER_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(flatten_status(
        FLATTEN_POPUP_PARENT_REVERSE_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_POPUP_DIRECT_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_IRT_FORWARD_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_IRT_REVERSE_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_FILE_ATTACHMENT_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_RICH_MEDIA_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_UNKNOWN_SUBTYPE_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_NEED_APPEARANCES_MALFORMED_PDF,
        QUANTAPDF_FLATTEN_WIDGETS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(flatten_status(
        FLATTEN_NEED_APPEARANCES_FALSE_PDF,
        QUANTAPDF_FLATTEN_WIDGETS) == QUANTAPDF_OK);
    CHECK(flatten_status(
        FLATTEN_NOOP_NEUTRAL_LINK_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_OK);
    CHECK(flatten_status(
        FLATTEN_NOOP_ZERO_WIDGET_PDF,
        QUANTAPDF_FLATTEN_WIDGETS) == QUANTAPDF_OK);
    CHECK(flatten_status(
        FLATTEN_NOOP_MALFORMED_ANNOTS_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(flatten_status(
        FLATTEN_NOOP_MALFORMED_ACROFORM_PDF,
        QUANTAPDF_FLATTEN_WIDGETS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(flatten_status(
        FLATTEN_NOOP_MALFORMED_ACROFORM_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_OK);
    CHECK(flatten_status(
        FLATTEN_WIDGET_P_MISMATCH_PDF,
        QUANTAPDF_FLATTEN_WIDGETS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(quantapdf_flatten_make_variant(
        FLATTEN_FLAG_ISOLATION_PDF,
        FLATTEN_VALID_WIDGET_P_MISMATCH_PDF,
        FLATTEN_VARIANT_VALID_WIDGET_P_MISMATCH));
    CHECK(flatten_status(
        FLATTEN_VALID_WIDGET_P_MISMATCH_PDF,
        QUANTAPDF_FLATTEN_WIDGETS) == QUANTAPDF_ERROR_FORMAT);
    CHECK(flatten_status_with_password(
        FLATTEN_ENCRYPTED_PDF,
        "user-pass",
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(flatten_status(
        FLATTEN_SIGNED_PDF,
        QUANTAPDF_FLATTEN_ANNOTATIONS) == QUANTAPDF_ERROR_UNSUPPORTED);
    return 0;
}
