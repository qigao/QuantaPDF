#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>
#include "test_pdf_trim_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct trim_observation {
    quantapdf_rect page_bounds;
    quantapdf_rect media_bounds;
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
} trim_observation;

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

static quantapdf_document *open_document(const char *path)
{
    quantapdf_document *document = NULL;

    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
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

static quantapdf_rect page_bounds(
    quantapdf_document *document,
    int page_index)
{
    quantapdf_page *page = NULL;
    quantapdf_rect bounds = {0};

    CHECK(quantapdf_load_page(document, page_index, &page) == QUANTAPDF_OK);
    CHECK(page != NULL);
    CHECK(quantapdf_page_bounds(page, &bounds) == QUANTAPDF_OK);
    quantapdf_drop_page(page);
    return bounds;
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
        CHECK(quantapdf_text_line_count(text, block_index, &line_count) ==
              QUANTAPDF_OK);
        for (line_index = 0; line_index < line_count && !found; ++line_index) {
            size_t span_count = 0;
            size_t span_index;
            CHECK(quantapdf_text_span_count(
                      text, block_index, line_index, &span_count) ==
                  QUANTAPDF_OK);
            for (span_index = 0; span_index < span_count; ++span_index) {
                const char *span_text = NULL;
                size_t span_size = 0;
                quantapdf_text_span_info info = {0};

                CHECK(quantapdf_text_span_text(
                          text, block_index, line_index, span_index,
                          &span_text, &span_size) == QUANTAPDF_OK);
                if (span_text == NULL || strstr(span_text, needle) == NULL)
                    continue;

                CHECK(quantapdf_text_get_span_info(
                          text, block_index, line_index, span_index, &info) ==
                      QUANTAPDF_OK);
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
    trim_observation *observation)
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
              page, QUANTAPDF_PAGE_BOX_MEDIA, &observation->media_bounds) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_page_box_bounds(
              page, QUANTAPDF_PAGE_BOX_CROP, &observation->crop_bounds) ==
          QUANTAPDF_OK);
    observation->text_bounds = find_text_bounds(page, "TRIM-TEXT");

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
            CHECK(quantapdf_link_uri(links, index, &uri, &uri_size) ==
                  QUANTAPDF_OK);
            observation->uri_hotspot = info.hotspot;
            copy_text(observation->uri, sizeof(observation->uri), uri, uri_size);
            saw_uri = 1;
        }
        else if (info.kind == QUANTAPDF_LINK_INTERNAL) {
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
    CHECK(quantapdf_form_field_get_info(form, 0, &observation->field) ==
          QUANTAPDF_OK);
    {
        const char *name = NULL;
        size_t name_size = 0;
        quantapdf_form_value_info value_info = {0};
        const char *value = NULL;
        size_t value_size = 0;

        CHECK(quantapdf_form_field_name(form, 0, &name, &name_size) ==
              QUANTAPDF_OK);
        copy_text(observation->field_name, sizeof(observation->field_name),
                  name, name_size);
        CHECK(observation->field.value_count == 1);

        CHECK(quantapdf_form_field_value_get_info(
                  form, 0, 0, &value_info) == QUANTAPDF_OK);
        CHECK(value_info.kind == QUANTAPDF_FORM_VALUE_UTF8);
        CHECK(quantapdf_form_field_value_utf8(
                  form, 0, 0, &value, &value_size) == QUANTAPDF_OK);
        copy_text(observation->field_value, sizeof(observation->field_value),
                  value, value_size);
    }
    CHECK(quantapdf_form_widget_count(form, &count) == QUANTAPDF_OK);
    CHECK(count == 1);
    CHECK(quantapdf_form_widget_get_info(form, 0, &observation->widget) ==
          QUANTAPDF_OK);

    CHECK(quantapdf_document_outline(document, &outline) == QUANTAPDF_OK);
    CHECK(outline != NULL);
    CHECK(quantapdf_outline_count(outline, &count) == QUANTAPDF_OK);
    CHECK(count == 1);
    CHECK(quantapdf_outline_get_info(outline, 0, &observation->outline) ==
          QUANTAPDF_OK);
    {
        const char *title = NULL;
        size_t title_size = 0;
        CHECK(quantapdf_outline_title(outline, 0, &title, &title_size) ==
              QUANTAPDF_OK);
        copy_text(observation->outline_title, sizeof(observation->outline_title),
                  title, title_size);
    }

    quantapdf_drop_outline(outline);
    quantapdf_drop_form(form);
    quantapdf_drop_annotation_page(annotations);
    quantapdf_drop_link_page(links);
    quantapdf_drop_image_page(images);
    quantapdf_drop_page(page);
}

static void expect_source_same(
    const trim_observation *actual,
    const trim_observation *source)
{
    check_rect_close(actual->page_bounds, source->page_bounds);
    check_rect_close(actual->media_bounds, source->media_bounds);
    check_rect_close(actual->crop_bounds, source->crop_bounds);
    check_rect_close(actual->text_bounds, source->text_bounds);
    check_quad_shifted(actual->image_quad, source->image_quad, 0, 0);
    check_rect_close(actual->uri_hotspot, source->uri_hotspot);
    CHECK(strcmp(actual->uri, source->uri) == 0);
    check_rect_close(actual->internal_hotspot, source->internal_hotspot);
    CHECK(actual->internal_target_page == source->internal_target_page);
    check_point_close(actual->internal_target, source->internal_target);
    CHECK(actual->annotation.type == source->annotation.type);
    CHECK(actual->annotation.flags == source->annotation.flags);
    check_rect_close(actual->annotation.bounds, source->annotation.bounds);
    CHECK(strcmp(actual->annotation_contents, source->annotation_contents) == 0);
    CHECK(actual->field.type == source->field.type);
    CHECK(actual->field.flags == source->field.flags);
    CHECK(strcmp(actual->field_name, source->field_name) == 0);
    CHECK(strcmp(actual->field_value, source->field_value) == 0);
    CHECK(actual->widget.field_index == source->widget.field_index);
    CHECK(actual->widget.page_index == source->widget.page_index);
    CHECK(actual->widget.flags == source->widget.flags);
    check_rect_close(actual->widget.bounds, source->widget.bounds);
    CHECK(actual->outline.destination_kind == source->outline.destination_kind);
    CHECK(actual->outline.target_page == source->outline.target_page);
    check_point_close(actual->outline.target, source->outline.target);
    CHECK(strcmp(actual->outline_title, source->outline_title) == 0);
}

static void expect_no_crop_trimmed(
    const trim_observation *actual,
    const trim_observation *source)
{
    check_rect_close(actual->page_bounds,
                     (quantapdf_rect){0, 0, 320, 240});
    check_rect_close(actual->media_bounds, actual->page_bounds);
    check_rect_close(actual->crop_bounds, actual->page_bounds);
    check_rect_close(actual->text_bounds,
                     shifted_rect(source->text_bounds, 40, 30));
    check_quad_shifted(actual->image_quad, source->image_quad, 40, 30);
    check_rect_close(actual->uri_hotspot,
                     shifted_rect(source->uri_hotspot, 40, 30));
    CHECK(strcmp(actual->uri, source->uri) == 0);
    check_rect_close(actual->internal_hotspot,
                     shifted_rect(source->internal_hotspot, 40, 30));
    CHECK(actual->internal_target_page == source->internal_target_page);
    check_point_close(actual->internal_target, source->internal_target);
    CHECK(actual->annotation.type == source->annotation.type);
    CHECK(actual->annotation.flags == source->annotation.flags);
    check_rect_close(actual->annotation.bounds,
                     shifted_rect(source->annotation.bounds, 40, 30));
    CHECK(strcmp(actual->annotation_contents, source->annotation_contents) == 0);
    CHECK(actual->field.type == source->field.type);
    CHECK(actual->field.flags == source->field.flags);
    CHECK(strcmp(actual->field_name, "trim.text") == 0);
    CHECK(strcmp(actual->field_name, source->field_name) == 0);
    CHECK(strcmp(actual->field_value, "TRIM-VALUE") == 0);
    CHECK(strcmp(actual->field_value, source->field_value) == 0);
    CHECK(actual->widget.field_index == source->widget.field_index);
    CHECK(actual->widget.page_index == source->widget.page_index);
    CHECK(actual->widget.flags == source->widget.flags);
    check_rect_close(actual->widget.bounds,
                     shifted_rect(source->widget.bounds, 40, 30));
    CHECK(actual->outline.destination_kind == source->outline.destination_kind);
    CHECK(actual->outline.target_page == source->outline.target_page);
    check_point_close(actual->outline.target, source->outline.target);
    CHECK(strcmp(actual->outline_title, source->outline_title) == 0);
}

static quantapdf_page_trim make_trim(
    int page_index,
    quantapdf_rect bounds)
{
    quantapdf_page_trim trim;

    trim.page_index = page_index;
    trim.bounds = bounds;
    return trim;
}

static void test_no_crop_interactive(void)
{
    quantapdf_document *document = open_document(TRIM_INTERACTIVE_PDF);
    quantapdf_output *seed = NULL;
    quantapdf_output *changed = NULL;
    const unsigned char *seed_data = NULL;
    const unsigned char *changed_data = NULL;
    size_t seed_size = 0;
    size_t changed_size = 0;
    quantapdf_rect media = page_box(document, 0, QUANTAPDF_PAGE_BOX_MEDIA);
    quantapdf_page_trim full = make_trim(0, media);
    quantapdf_page_trim trim = make_trim(
        0, (quantapdf_rect){40, 30, 360, 270});
    trim_observation source;
    trim_observation source_after;
    trim_observation output_observation;
    const float expected_raw[4] = {40, 30, 360, 270};
    const char *output_path = "trim-interactive-output.pdf";
    quantapdf_document *reopened;

    capture_observation(document, &source);
    CHECK(strcmp(source.uri, "https://example.com/trim") == 0);
    CHECK(strcmp(source.annotation_contents, "TRIM-ANNOT") == 0);
    CHECK(strcmp(source.field_name, "trim.text") == 0);
    CHECK(strcmp(source.field_value, "TRIM-VALUE") == 0);

    CHECK(quantapdf_trim_pages(document, &full, 1, &seed) == QUANTAPDF_OK);
    CHECK(seed != NULL);
    CHECK(quantapdf_output_data(seed, &seed_data, &seed_size) == QUANTAPDF_OK);
    CHECK(trim_raw_expect_local_mediabox(seed_data, seed_size, 0, 0, NULL));

    CHECK(quantapdf_trim_pages(document, &trim, 1, &changed) == QUANTAPDF_OK);
    CHECK(changed != NULL);
    capture_observation(document, &source_after);
    expect_source_same(&source_after, &source);

    quantapdf_close(document);
    document = NULL;

    CHECK(quantapdf_output_data(changed, &changed_data, &changed_size) ==
          QUANTAPDF_OK);
    CHECK(trim_raw_expect_local_mediabox(
              changed_data, changed_size, 0, 1, expected_raw));
    CHECK(trim_raw_expect_preserved_cropbox(
              seed_data, seed_size, changed_data, changed_size, 0));
    CHECK(trim_raw_expect_preserved_graph(
              seed_data, seed_size, changed_data, changed_size));
    CHECK(write_bytes(output_path, changed_data, changed_size));

    reopened = open_document(output_path);
    capture_observation(reopened, &output_observation);
    expect_no_crop_trimmed(&output_observation, &source);
    quantapdf_close(reopened);
    remove(output_path);

    quantapdf_drop_output(changed);
    quantapdf_drop_output(seed);
}

static void test_preserved_crop_frames(void)
{
    char path[1024];
    const char *output_path = "trim-preserved-output.pdf";
    quantapdf_document *document;
    quantapdf_document *reopened;
    quantapdf_output *seed = NULL;
    quantapdf_output *changed = NULL;
    const unsigned char *seed_data = NULL;
    const unsigned char *changed_data = NULL;
    size_t seed_size = 0;
    size_t changed_size = 0;
    quantapdf_rect media0;
    quantapdf_rect media1;
    quantapdf_rect visible0;
    quantapdf_page_trim full;
    quantapdf_page_trim trims[2];
    const float physical_raw[4] = {20, 20, 380, 280};
    const float clipped_raw[4] = {80, 60, 380, 280};

    sibling_fixture_path("trim-preserved-crop.pdf", path, sizeof(path));
    document = open_document(path);
    media0 = page_box(document, 0, QUANTAPDF_PAGE_BOX_MEDIA);
    media1 = page_box(document, 1, QUANTAPDF_PAGE_BOX_MEDIA);
    visible0 = page_bounds(document, 0);
    check_rect_close(media0, (quantapdf_rect){-50, -40, 350, 260});
    check_rect_close(media1, media0);
    check_rect_close(visible0, (quantapdf_rect){0, 0, 300, 220});

    full = make_trim(0, media0);
    CHECK(quantapdf_trim_pages(document, &full, 1, &seed) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(seed, &seed_data, &seed_size) == QUANTAPDF_OK);
    CHECK(trim_raw_expect_local_mediabox(seed_data, seed_size, 0, 0, NULL));

    trims[0] = make_trim(0, (quantapdf_rect){
        media0.x0 + 20, media0.y0 + 20,
        media0.x1 - 20, media0.y1 - 20});
    trims[1] = make_trim(1, (quantapdf_rect){
        media1.x0 + 80, media1.y0 + 20,
        media1.x1 - 20, media1.y1 - 60});

    CHECK(quantapdf_trim_pages(document, trims, 2, &changed) == QUANTAPDF_OK);
    CHECK(changed != NULL);
    CHECK(quantapdf_output_data(changed, &changed_data, &changed_size) ==
          QUANTAPDF_OK);
    CHECK(trim_raw_expect_local_mediabox(
              changed_data, changed_size, 0, 1, physical_raw));
    CHECK(trim_raw_expect_local_mediabox(
              changed_data, changed_size, 1, 1, clipped_raw));
    CHECK(trim_raw_expect_preserved_cropbox(
              seed_data, seed_size, changed_data, changed_size, 0));
    CHECK(trim_raw_expect_preserved_cropbox(
              seed_data, seed_size, changed_data, changed_size, 1));
    CHECK(trim_raw_expect_preserved_graph(
              seed_data, seed_size, changed_data, changed_size));

    check_rect_close(
        page_box(document, 0, QUANTAPDF_PAGE_BOX_MEDIA), media0);
    check_rect_close(
        page_box(document, 1, QUANTAPDF_PAGE_BOX_MEDIA), media1);

    CHECK(write_bytes(output_path, changed_data, changed_size));
    reopened = open_document(output_path);

    check_rect_close(page_bounds(reopened, 0),
                     (quantapdf_rect){0, 0, 300, 220});
    check_rect_close(page_box(reopened, 0, QUANTAPDF_PAGE_BOX_CROP),
                     (quantapdf_rect){0, 0, 300, 220});
    check_rect_close(page_box(reopened, 0, QUANTAPDF_PAGE_BOX_MEDIA),
                     (quantapdf_rect){-30, -20, 330, 240});

    check_rect_close(page_bounds(reopened, 1),
                     (quantapdf_rect){0, 0, 270, 200});
    check_rect_close(page_box(reopened, 1, QUANTAPDF_PAGE_BOX_CROP),
                     (quantapdf_rect){0, 0, 270, 200});
    check_rect_close(page_box(reopened, 1, QUANTAPDF_PAGE_BOX_MEDIA),
                     (quantapdf_rect){0, -20, 300, 200});

    quantapdf_close(reopened);
    remove(output_path);
    quantapdf_drop_output(changed);
    quantapdf_drop_output(seed);
    quantapdf_close(document);
}

int trim_run_frame_mode_tests(void)
{
    test_no_crop_interactive();
    test_preserved_crop_frames();
    return 1;
}
