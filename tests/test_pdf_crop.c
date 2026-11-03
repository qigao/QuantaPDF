#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>
#include "test_pdf_crop_internal.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct crop_observation {
    quantapdf_rect page_bounds;
    quantapdf_rect crop_bounds;
    quantapdf_rect text_bounds;
    quantapdf_quad image_quad;
    quantapdf_rect uri_hotspot;
    char uri[128];
    quantapdf_rect internal_hotspot;
    int internal_target_page;
    quantapdf_point internal_target;
    quantapdf_annotation_info annotation;
    char annotation_contents[128];
    quantapdf_form_field_info field;
    char field_name[128];
    char field_value[128];
    quantapdf_form_widget_info widget;
    quantapdf_outline_info outline;
    char outline_title[128];
} crop_observation;

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

static quantapdf_output *output_sentinel(void)
{
    return (quantapdf_output *)(uintptr_t)1;
}

static quantapdf_page_crop make_crop(
    int page_index,
    float x0,
    float y0,
    float x1,
    float y1)
{
    quantapdf_page_crop crop;

    crop.page_index = page_index;
    crop.bounds.x0 = x0;
    crop.bounds.y0 = y0;
    crop.bounds.x1 = x1;
    crop.bounds.y1 = y1;
    return crop;
}

static void expect_crop_error(
    quantapdf_document *document,
    const quantapdf_page_crop *crops,
    size_t count,
    quantapdf_status expected)
{
    quantapdf_output *output = output_sentinel();

    CHECK(quantapdf_crop_pages(document, crops, count, &output) == expected);
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

static void check_rect_close(quantapdf_rect actual, quantapdf_rect expected)
{
    CHECK(close_float(actual.x0, expected.x0));
    CHECK(close_float(actual.y0, expected.y0));
    CHECK(close_float(actual.x1, expected.x1));
    CHECK(close_float(actual.y1, expected.y1));
}

static void check_point_close(quantapdf_point actual, quantapdf_point expected)
{
    CHECK(close_float(actual.x, expected.x));
    CHECK(close_float(actual.y, expected.y));
}

static quantapdf_rect shifted_rect(quantapdf_rect rect, float x, float y)
{
    rect.x0 -= x;
    rect.x1 -= x;
    rect.y0 -= y;
    rect.y1 -= y;
    return rect;
}

static quantapdf_point shifted_point(quantapdf_point point, float x, float y)
{
    point.x -= x;
    point.y -= y;
    return point;
}

static void check_quad_shifted(
    quantapdf_quad actual,
    quantapdf_quad source,
    float x,
    float y)
{
    check_point_close(actual.ul, shifted_point(source.ul, x, y));
    check_point_close(actual.ur, shifted_point(source.ur, x, y));
    check_point_close(actual.ll, shifted_point(source.ll, x, y));
    check_point_close(actual.lr, shifted_point(source.lr, x, y));
}

static void copy_text(
    char *destination,
    size_t capacity,
    const char *source,
    size_t size)
{
    CHECK(destination != NULL);
    CHECK(capacity != 0);
    CHECK(source != NULL);
    CHECK(size < capacity);
    memcpy(destination, source, size);
    destination[size] = '\0';
}

static quantapdf_document *open_document(const char *path, const char *password)
{
    quantapdf_document *document = NULL;

    CHECK(quantapdf_open(test_context(), path, password, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    return document;
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

static quantapdf_rect find_text_bounds(
    quantapdf_page *page,
    const char *needle)
{
    quantapdf_text_page *text = NULL;
    size_t block_count = 0;
    size_t block_index;
    quantapdf_rect result = {0};
    int found = 0;

    CHECK(quantapdf_extract_structured_text(page, &text) == QUANTAPDF_OK);
    CHECK(text != NULL);
    CHECK(quantapdf_text_block_count(text, &block_count) == QUANTAPDF_OK);
    for (block_index = 0; block_index < block_count && !found; ++block_index) {
        size_t line_count = 0;
        size_t line_index;
        CHECK(quantapdf_text_line_count(
                  text, block_index, &line_count) == QUANTAPDF_OK);
        for (line_index = 0; line_index < line_count && !found; ++line_index) {
            size_t span_count = 0;
            size_t span_index;
            CHECK(quantapdf_text_span_count(
                      text, block_index, line_index, &span_count) == QUANTAPDF_OK);
            for (span_index = 0; span_index < span_count; ++span_index) {
                const char *span_text = NULL;
                size_t span_size = 0;
                quantapdf_text_span_info info = {0};

                CHECK(quantapdf_text_span_text(
                          text,
                          block_index,
                          line_index,
                          span_index,
                          &span_text,
                          &span_size) == QUANTAPDF_OK);
                if (span_text == NULL || span_size < strlen(needle) ||
                    strstr(span_text, needle) == NULL)
                    continue;

                CHECK(quantapdf_text_get_span_info(
                          text,
                          block_index,
                          line_index,
                          span_index,
                          &info) == QUANTAPDF_OK);
                result = info.bounds;
                found = 1;
                break;
            }
        }
    }
    quantapdf_drop_text_page(text);
    CHECK(found);
    return result;
}

static void capture_observation(
    quantapdf_document *document,
    crop_observation *observation)
{
    quantapdf_page *page = NULL;
    quantapdf_image_page *images = NULL;
    quantapdf_link_page *links = NULL;
    quantapdf_annotation_page *annotations = NULL;
    quantapdf_form *form = NULL;
    quantapdf_outline *outline = NULL;
    size_t count = 0;
    size_t index;
    int saw_uri = 0;
    int saw_internal = 0;

    memset(observation, 0, sizeof(*observation));

    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_page_bounds(page, &observation->page_bounds) == QUANTAPDF_OK);
    CHECK(quantapdf_page_box_bounds(
              page, QUANTAPDF_PAGE_BOX_CROP, &observation->crop_bounds) ==
          QUANTAPDF_OK);
    observation->text_bounds = find_text_bounds(page, "CROP-TEXT");

    CHECK(quantapdf_extract_images(page, &images) == QUANTAPDF_OK);
    CHECK(quantapdf_image_count(images, &count) == QUANTAPDF_OK);
    CHECK(count == 1);
    {
        quantapdf_image_info info = {0};

        CHECK(quantapdf_image_get_info(images, 0, &info) == QUANTAPDF_OK);
        observation->image_quad = info.quad;
    }

    CHECK(quantapdf_extract_links(page, &links) == QUANTAPDF_OK);
    CHECK(quantapdf_link_count(links, &count) == QUANTAPDF_OK);
    CHECK(count == 2);
    for (index = 0; index < count; ++index) {
        quantapdf_link_info info = {0};

        CHECK(quantapdf_link_get_info(links, index, &info) == QUANTAPDF_OK);
        if (info.kind == QUANTAPDF_LINK_URI) {
            const char *uri = NULL;
            size_t uri_size = 0;
            CHECK(!saw_uri);
            CHECK(quantapdf_link_uri(
                      links, index, &uri, &uri_size) == QUANTAPDF_OK);
            observation->uri_hotspot = info.hotspot;
            copy_text(observation->uri, sizeof(observation->uri), uri, uri_size);
            saw_uri = 1;
        } else if (info.kind == QUANTAPDF_LINK_INTERNAL) {
            CHECK(!saw_internal);
            observation->internal_hotspot = info.hotspot;
            observation->internal_target_page = info.target_page;
            observation->internal_target = info.target;
            saw_internal = 1;
        }
    }
    CHECK(saw_uri && saw_internal);

    CHECK(quantapdf_extract_annotations(page, &annotations) == QUANTAPDF_OK);
    CHECK(quantapdf_annotation_count(annotations, &count) == QUANTAPDF_OK);
    CHECK(count == 1);
    CHECK(quantapdf_annotation_get_info(
              annotations, 0, &observation->annotation) == QUANTAPDF_OK);
    {
        const char *contents = NULL;
        size_t contents_size = 0;
        CHECK(quantapdf_annotation_contents(
                  annotations, 0, &contents, &contents_size) == QUANTAPDF_OK);
        copy_text(
            observation->annotation_contents,
            sizeof(observation->annotation_contents),
            contents,
            contents_size);
    }

    CHECK(quantapdf_document_form(document, &form) == QUANTAPDF_OK);
    CHECK(form != NULL);
    CHECK(quantapdf_form_field_count(form, &count) == QUANTAPDF_OK);
    CHECK(count == 1);
    CHECK(quantapdf_form_field_get_info(
              form, 0, &observation->field) == QUANTAPDF_OK);
    {
        const char *name = NULL;
        size_t name_size = 0;
        quantapdf_form_value_info value_info = {0};
        const char *value = NULL;
        size_t value_size = 0;

        CHECK(quantapdf_form_field_name(
                  form, 0, &name, &name_size) == QUANTAPDF_OK);
        copy_text(
            observation->field_name,
            sizeof(observation->field_name),
            name,
            name_size);
        CHECK(observation->field.value_count == 1);

        CHECK(quantapdf_form_field_value_get_info(
                  form, 0, 0, &value_info) == QUANTAPDF_OK);
        CHECK(value_info.kind == QUANTAPDF_FORM_VALUE_UTF8);
        CHECK(quantapdf_form_field_value_utf8(
                  form, 0, 0, &value, &value_size) == QUANTAPDF_OK);
        copy_text(
            observation->field_value,
            sizeof(observation->field_value),
            value,
            value_size);
    }
    CHECK(quantapdf_form_widget_count(form, &count) == QUANTAPDF_OK);
    CHECK(count == 1);
    CHECK(quantapdf_form_widget_get_info(
              form, 0, &observation->widget) == QUANTAPDF_OK);

    CHECK(quantapdf_document_outline(document, &outline) == QUANTAPDF_OK);
    CHECK(outline != NULL);
    CHECK(quantapdf_outline_count(outline, &count) == QUANTAPDF_OK);
    CHECK(count == 1);
    CHECK(quantapdf_outline_get_info(
              outline, 0, &observation->outline) == QUANTAPDF_OK);
    {
        const char *title = NULL;
        size_t title_size = 0;
        CHECK(quantapdf_outline_title(
                  outline, 0, &title, &title_size) == QUANTAPDF_OK);
        copy_text(
            observation->outline_title,
            sizeof(observation->outline_title),
            title,
            title_size);
    }

    quantapdf_drop_outline(outline);
    quantapdf_drop_form(form);
    quantapdf_drop_annotation_page(annotations);
    quantapdf_drop_link_page(links);
    quantapdf_drop_image_page(images);
    quantapdf_drop_page(page);
}

static void expect_observation_same(
    const crop_observation *actual,
    const crop_observation *expected)
{
    check_rect_close(actual->page_bounds, expected->page_bounds);
    check_rect_close(actual->crop_bounds, expected->crop_bounds);
    check_rect_close(actual->text_bounds, expected->text_bounds);
    check_quad_shifted(actual->image_quad, expected->image_quad, 0.0f, 0.0f);
    check_rect_close(actual->uri_hotspot, expected->uri_hotspot);
    CHECK(strcmp(actual->uri, expected->uri) == 0);
    check_rect_close(actual->internal_hotspot, expected->internal_hotspot);
    CHECK(actual->internal_target_page == expected->internal_target_page);
    check_point_close(actual->internal_target, expected->internal_target);
    CHECK(actual->annotation.type == expected->annotation.type);
    CHECK(actual->annotation.flags == expected->annotation.flags);
    check_rect_close(actual->annotation.bounds, expected->annotation.bounds);
    CHECK(strcmp(
              actual->annotation_contents,
              expected->annotation_contents) == 0);
    CHECK(actual->field.type == expected->field.type);
    CHECK(actual->field.flags == expected->field.flags);
    CHECK(actual->field.value_presence == expected->field.value_presence);
    CHECK(actual->field.value_count == expected->field.value_count);
    CHECK(actual->field.option_count == expected->field.option_count);
    CHECK(actual->field.widget_count == expected->field.widget_count);
    CHECK(actual->field.is_multiselect == expected->field.is_multiselect);
    CHECK(actual->field.is_signed == expected->field.is_signed);
    CHECK(strcmp(actual->field_name, expected->field_name) == 0);
    CHECK(strcmp(actual->field_value, expected->field_value) == 0);
    CHECK(actual->widget.field_index == expected->widget.field_index);
    CHECK(actual->widget.page_index == expected->widget.page_index);
    CHECK(actual->widget.flags == expected->widget.flags);
    CHECK(actual->widget.button_option_index == expected->widget.button_option_index);
    check_rect_close(actual->widget.bounds, expected->widget.bounds);
    CHECK(actual->outline.parent_index == expected->outline.parent_index);
    CHECK(actual->outline.first_child_index == expected->outline.first_child_index);
    CHECK(actual->outline.next_sibling_index == expected->outline.next_sibling_index);
    CHECK(actual->outline.destination_kind == expected->outline.destination_kind);
    CHECK(actual->outline.target_page == expected->outline.target_page);
    CHECK(actual->outline.is_open == expected->outline.is_open);
    check_point_close(actual->outline.target, expected->outline.target);
    CHECK(strcmp(actual->outline_title, expected->outline_title) == 0);
}

static void expect_observation_cropped(
    const crop_observation *actual,
    const crop_observation *source)
{
    check_rect_close(
        actual->page_bounds,
        (quantapdf_rect){0.0f, 0.0f, 300.0f, 220.0f});
    check_rect_close(actual->crop_bounds, actual->page_bounds);
    check_rect_close(
        actual->text_bounds,
        shifted_rect(source->text_bounds, 50.0f, 40.0f));
    check_quad_shifted(actual->image_quad, source->image_quad, 50.0f, 40.0f);
    check_rect_close(
        actual->uri_hotspot,
        shifted_rect(source->uri_hotspot, 50.0f, 40.0f));
    CHECK(strcmp(actual->uri, source->uri) == 0);
    check_rect_close(
        actual->internal_hotspot,
        shifted_rect(source->internal_hotspot, 50.0f, 40.0f));
    CHECK(actual->internal_target_page == source->internal_target_page);
    check_point_close(
        actual->internal_target,
        shifted_point(source->internal_target, 20.0f, 30.0f));
    CHECK(actual->annotation.type == source->annotation.type);
    CHECK(actual->annotation.flags == source->annotation.flags);
    check_rect_close(
        actual->annotation.bounds,
        shifted_rect(source->annotation.bounds, 50.0f, 40.0f));
    CHECK(strcmp(
              actual->annotation_contents,
              source->annotation_contents) == 0);
    CHECK(actual->field.type == source->field.type);
    CHECK(actual->field.flags == source->field.flags);
    CHECK(actual->field.value_presence == source->field.value_presence);
    CHECK(actual->field.value_count == source->field.value_count);
    CHECK(actual->field.option_count == source->field.option_count);
    CHECK(actual->field.widget_count == source->field.widget_count);
    CHECK(strcmp(actual->field_name, source->field_name) == 0);
    CHECK(strcmp(actual->field_value, source->field_value) == 0);
    CHECK(actual->widget.field_index == source->widget.field_index);
    CHECK(actual->widget.page_index == source->widget.page_index);
    CHECK(actual->widget.flags == source->widget.flags);
    check_rect_close(
        actual->widget.bounds,
        shifted_rect(source->widget.bounds, 50.0f, 40.0f));
    CHECK(actual->outline.parent_index == source->outline.parent_index);
    CHECK(actual->outline.first_child_index == source->outline.first_child_index);
    CHECK(actual->outline.next_sibling_index == source->outline.next_sibling_index);
    CHECK(actual->outline.destination_kind == source->outline.destination_kind);
    CHECK(actual->outline.target_page == source->outline.target_page);
    CHECK(actual->outline.is_open == source->outline.is_open);
    check_point_close(
        actual->outline.target,
        shifted_point(source->outline.target, 20.0f, 30.0f));
    CHECK(strcmp(actual->outline_title, source->outline_title) == 0);
}

static void test_inherited_cropbox(void)
{
    quantapdf_document *document = open_document(CROP_INHERITED_PDF, NULL);
    quantapdf_output *noop = NULL;
    quantapdf_output *changed = NULL;
    const unsigned char *noop_data = NULL;
    const unsigned char *changed_data = NULL;
    size_t noop_size = 0;
    size_t changed_size = 0;
    quantapdf_rect bounds = page_bounds(document, 0);
    quantapdf_page_crop full;
    quantapdf_page_crop crop;
    const float expected_raw[4] = {30.0f, 30.0f, 370.0f, 270.0f};

    check_rect_close(bounds, (quantapdf_rect){0.0f, 0.0f, 380.0f, 260.0f});
    full = make_crop(0, 0.0f, 0.0f, 380.0f, 260.0f);
    crop = make_crop(0, 20.0f, 10.0f, 360.0f, 250.0f);

    CHECK(quantapdf_crop_pages(document, &full, 1, &noop) == QUANTAPDF_OK);
    CHECK(noop != NULL);
    CHECK(quantapdf_output_data(noop, &noop_data, &noop_size) == QUANTAPDF_OK);
    CHECK(crop_raw_expect_local_cropbox(
              noop_data, noop_size, 0, 0, NULL));

    CHECK(quantapdf_crop_pages(document, &crop, 1, &changed) == QUANTAPDF_OK);
    CHECK(changed != NULL);
    CHECK(quantapdf_output_data(
              changed, &changed_data, &changed_size) == QUANTAPDF_OK);
    CHECK(crop_raw_expect_local_cropbox(
              changed_data, changed_size, 0, 1, expected_raw));
    CHECK(crop_raw_expect_preserved_graph(
              noop_data, noop_size, changed_data, changed_size));

    quantapdf_drop_output(changed);
    quantapdf_drop_output(noop);
    quantapdf_close(document);
}

int main(void)
{
    quantapdf_document *document = NULL;
    quantapdf_document *other = NULL;
    quantapdf_document *reopened = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_output *output = output_sentinel();
    const unsigned char *first_data = NULL;
    const unsigned char *second_data = NULL;
    const unsigned char *changed_data = NULL;
    size_t first_size = 0;
    size_t second_size = 0;
    size_t changed_size = 0;
    quantapdf_rect source_before;
    quantapdf_rect source_after;
    quantapdf_page_crop full;
    quantapdf_page_crop crop;
    quantapdf_page_crop pair[2];
    quantapdf_page_crop changed_crops[2];
    quantapdf_page_crop bad;
    crop_observation source_observation;
    crop_observation source_after_observation;
    crop_observation output_observation;
    const float page0_raw[4] = {50.0f, 40.0f, 350.0f, 260.0f};
    const float page1_raw[4] = {20.0f, 30.0f, 380.0f, 270.0f};

    (void)remove(CROP_OUTPUT_PDF);
    document = open_document(CROP_INTERACTIVE_PDF, NULL);
    source_before = page_bounds(document, 0);
    check_rect_close(
        source_before,
        (quantapdf_rect){0.0f, 0.0f, 400.0f, 300.0f});

    full = make_crop(0, 0.0f, 0.0f, 400.0f, 300.0f);
    crop = make_crop(0, 50.0f, 40.0f, 350.0f, 260.0f);

    CHECK(quantapdf_crop_pages(document, &crop, 1, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    expect_crop_error(NULL, &crop, 1, QUANTAPDF_ERROR_ARGUMENT);
    expect_crop_error(document, NULL, 1, QUANTAPDF_ERROR_ARGUMENT);
    expect_crop_error(document, &crop, 0, QUANTAPDF_ERROR_ARGUMENT);

    bad = crop;

    bad = crop;

    expect_crop_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    bad = crop;
    bad.page_index = -1;
    expect_crop_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    bad = crop;
    bad.page_index = 2;
    expect_crop_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    pair[0] = crop;
    pair[1] = make_crop(0, 60.0f, 50.0f, 340.0f, 250.0f);
    expect_crop_error(document, pair, 2, QUANTAPDF_ERROR_ARGUMENT);

    bad = crop;
    bad.bounds.x0 = NAN;
    expect_crop_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);
    bad = crop;
    bad.bounds.y0 = INFINITY;
    expect_crop_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);
    bad = crop;
    bad.bounds.x1 = -INFINITY;
    expect_crop_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    bad = crop;
    bad.bounds.x1 = bad.bounds.x0;
    expect_crop_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);
    bad = crop;
    bad.bounds.y1 = bad.bounds.y0;
    expect_crop_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);
    bad = crop;
    bad.bounds.x0 = 360.0f;
    bad.bounds.x1 = 350.0f;
    expect_crop_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    bad = crop;
    bad.bounds.x0 = -1.0f;
    expect_crop_error(document, &bad, 1, QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_open(test_context(), NON_PDF, NULL, &other) == QUANTAPDF_ERROR_FORMAT);
    CHECK(other == NULL);

    other = open_document(ENCRYPTED_PDF, "user-pass");
    expect_crop_error(other, &full, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_close(other);
    other = NULL;

    other = open_document(SIGNED_PDF, NULL);
    expect_crop_error(other, &full, 1, QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_close(other);
    other = NULL;

    other = open_document(CROP_MALFORMED_BOX_PDF, NULL);
    expect_crop_error(other, &full, 1, QUANTAPDF_ERROR_FORMAT);
    quantapdf_close(other);
    other = NULL;

    other = open_document(CROP_MALFORMED_ROTATE_PDF, NULL);
    expect_crop_error(other, &full, 1, QUANTAPDF_ERROR_FORMAT);
    quantapdf_close(other);
    other = NULL;

    other = open_document(CROP_MALFORMED_USERUNIT_PDF, NULL);
    expect_crop_error(other, &full, 1, QUANTAPDF_ERROR_FORMAT);
    quantapdf_close(other);
    other = NULL;

    CHECK(quantapdf_crop_pages(document, &full, 1, &first) == QUANTAPDF_OK);
    CHECK(first != NULL);
    CHECK(quantapdf_crop_pages(document, &full, 1, &second) == QUANTAPDF_OK);
    CHECK(second != NULL);
    CHECK(quantapdf_output_data(first, &first_data, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_data, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_data != NULL);
    CHECK(second_data != NULL);
    CHECK(first_size != 0);
    CHECK(second_size == first_size);
    CHECK(memcmp(first_data, second_data, first_size) == 0);

    source_after = page_bounds(document, 0);
    check_rect_close(source_before, source_after);
    capture_observation(document, &source_observation);
    CHECK(strcmp(source_observation.uri, "https://example.com/crop") == 0);
    CHECK(strcmp(source_observation.annotation_contents, "CROP-ANNOT") == 0);
    CHECK(strcmp(source_observation.field_name, "crop.text") == 0);
    CHECK(strcmp(source_observation.field_value, "CROP-VALUE") == 0);
    CHECK(strcmp(source_observation.outline_title, "Crop target") == 0);

    changed_crops[0] = make_crop(0, 50.0f, 40.0f, 350.0f, 260.0f);
    changed_crops[1] = make_crop(1, 20.0f, 30.0f, 380.0f, 270.0f);
    output = output_sentinel();
    if (quantapdf_crop_pages(document, changed_crops, 2, &output) !=
            QUANTAPDF_OK ||
        output == NULL) {
        fprintf(stderr, "valid crop failed\n");
        CHECK(output == NULL);
        quantapdf_drop_output(first);
        quantapdf_drop_output(second);
        quantapdf_close(document);
        return EXIT_FAILURE;
    }

    capture_observation(document, &source_after_observation);
    expect_observation_same(&source_after_observation, &source_observation);

    CHECK(quantapdf_output_data(output, &changed_data, &changed_size) ==
          QUANTAPDF_OK);
    CHECK(changed_data != NULL && changed_size != 0);
    CHECK(crop_raw_expect_local_cropbox(
              changed_data, changed_size, 0, 1, page0_raw));
    CHECK(crop_raw_expect_local_cropbox(
              changed_data, changed_size, 1, 1, page1_raw));
    CHECK(crop_raw_expect_preserved_graph(
              first_data, first_size, changed_data, changed_size));

    quantapdf_close(document);
    document = NULL;
    CHECK(write_bytes(CROP_OUTPUT_PDF, changed_data, changed_size));
    reopened = open_document(CROP_OUTPUT_PDF, NULL);
    capture_observation(reopened, &output_observation);
    expect_observation_cropped(&output_observation, &source_observation);
    quantapdf_close(reopened);
    reopened = NULL;

    test_inherited_cropbox();

    quantapdf_drop_output(output);
    quantapdf_drop_output(first);
    quantapdf_drop_output(second);
    (void)remove(CROP_OUTPUT_PDF);
    return EXIT_SUCCESS;
}
