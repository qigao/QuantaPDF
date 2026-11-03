#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>
#include "test_pdf_trim_internal.h"

#include <math.h>
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

static void check_point_close(quantapdf_point actual, quantapdf_point expected)
{
    CHECK(close_float(actual.x, expected.x));
    CHECK(close_float(actual.y, expected.y));
}

static quantapdf_point shifted_point(quantapdf_point point, float x, float y)
{
    point.x -= x;
    point.y -= y;
    return point;
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

static quantapdf_page_trim make_trim(
    int page_index,
    float x0,
    float y0,
    float x1,
    float y1)
{
    quantapdf_page_trim trim;

    trim.page_index = page_index;
    trim.bounds = (quantapdf_rect){x0, y0, x1, y1};
    return trim;
}

static quantapdf_point internal_link_target(quantapdf_document *document)
{
    quantapdf_page *page = NULL;
    quantapdf_link_page *links = NULL;
    quantapdf_point result = {0};
    size_t count = 0;
    size_t index;
    int found = 0;

    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_links(page, &links) == QUANTAPDF_OK);
    CHECK(quantapdf_link_count(links, &count) == QUANTAPDF_OK);
    for (index = 0; index < count; ++index) {
        quantapdf_link_info info = {0};

        CHECK(quantapdf_link_get_info(links, index, &info) == QUANTAPDF_OK);
        if (info.kind == QUANTAPDF_LINK_INTERNAL) {
            CHECK(info.target_page == 1);
            result = info.target;
            found = 1;
            break;
        }
    }
    CHECK(found);
    quantapdf_drop_link_page(links);
    quantapdf_drop_page(page);
    return result;
}

static quantapdf_point outline_target(quantapdf_document *document)
{
    quantapdf_outline *outline = NULL;
    quantapdf_outline_info info = {0};
    size_t count = 0;

    CHECK(quantapdf_document_outline(document, &outline) == QUANTAPDF_OK);
    CHECK(outline != NULL);
    CHECK(quantapdf_outline_count(outline, &count) == QUANTAPDF_OK);
    CHECK(count == 1);

    CHECK(quantapdf_outline_get_info(outline, 0, &info) == QUANTAPDF_OK);
    CHECK(info.destination_kind == QUANTAPDF_OUTLINE_DESTINATION_INTERNAL);
    CHECK(info.target_page == 1);
    quantapdf_drop_outline(outline);
    return info.target;
}

static void check_render_dimensions(
    quantapdf_document *document,
    int page_index,
    int expected_width,
    int expected_height)
{
    quantapdf_page *page = NULL;
    quantapdf_bitmap *bitmap = NULL;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;

    CHECK(quantapdf_load_page(document, page_index, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_render_page(page, &bitmap) == QUANTAPDF_OK);
    CHECK(bitmap != NULL);
    CHECK(quantapdf_bitmap_dimensions(
              bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == expected_width);
    CHECK(height == expected_height);
    CHECK(stride > 0);
    CHECK(components == 3 || components == 4);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
}

static void test_render_and_trimmed_destinations(void)
{
    const char *output_path = "trim-required-output.pdf";
    quantapdf_document *document = open_document(TRIM_INTERACTIVE_PDF);
    quantapdf_document *reopened = NULL;
    quantapdf_output *output = NULL;
    const unsigned char *data = NULL;
    size_t size = 0;
    quantapdf_point source_link = internal_link_target(document);
    quantapdf_point source_outline = outline_target(document);
    quantapdf_page_trim trims[2];

    check_render_dimensions(document, 0, 400, 300);
    trims[0] = make_trim(0, 40, 30, 360, 270);
    trims[1] = make_trim(1, 20, 30, 380, 270);

    CHECK(quantapdf_trim_pages(document, trims, 2, &output) == QUANTAPDF_OK);
    CHECK(output != NULL);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(data != NULL && size != 0);
    (void)remove(output_path);
    CHECK(write_bytes(output_path, data, size));

    reopened = open_document(output_path);
    check_render_dimensions(reopened, 0, 320, 240);
    check_point_close(
        internal_link_target(reopened), shifted_point(source_link, 20, 30));
    check_point_close(
        outline_target(reopened), shifted_point(source_outline, 20, 30));
    quantapdf_close(reopened);
    reopened = NULL;
    (void)remove(output_path);

    check_render_dimensions(document, 0, 400, 300);
    check_point_close(internal_link_target(document), source_link);
    check_point_close(outline_target(document), source_outline);

    quantapdf_drop_output(output);
    quantapdf_close(document);
}

static void test_empty_post_trim_intersection_rejected(void)
{
    char path[1024];
    quantapdf_document *document;
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1;
    quantapdf_page_trim trim;

    sibling_fixture_path("trim-preserved-crop.pdf", path, sizeof(path));
    document = open_document(path);
    trim = make_trim(0, -50, -40, -10, -5);
    CHECK(quantapdf_trim_pages(document, &trim, 1, &output) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(output == NULL);
    quantapdf_close(document);
}

int trim_run_required_observation_tests(void)
{
    test_render_and_trimmed_destinations();
    test_empty_post_trim_intersection_rejected();
    return 1;
}
