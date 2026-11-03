#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>
#include "test_pdf_trim_internal.h"

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

static quantapdf_rect page_box(
    quantapdf_document *document,
    quantapdf_page_box box)
{
    quantapdf_page *page = NULL;
    quantapdf_rect bounds = {0};

    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(page != NULL);
    CHECK(quantapdf_page_box_bounds(page, box, &bounds) == QUANTAPDF_OK);
    quantapdf_drop_page(page);
    return bounds;
}

static quantapdf_rect page_bounds(quantapdf_document *document)
{
    quantapdf_page *page = NULL;
    quantapdf_rect bounds = {0};

    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(page != NULL);
    CHECK(quantapdf_page_bounds(page, &bounds) == QUANTAPDF_OK);
    quantapdf_drop_page(page);
    return bounds;
}

static quantapdf_page_trim make_trim(quantapdf_rect bounds)
{
    quantapdf_page_trim trim;

    trim.page_index = 0;
    trim.bounds = bounds;
    return trim;
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

int trim_run_outside_crop_test(void)
{
    static const float source_media_raw[4] = {0, 0, 300, 200};
    static const float crop_raw[4] = {-20, -10, 280, 190};
    static const float changed_media_raw[4] = {10, 10, 290, 190};
    char path[1024];
    const char *output_path = "trim-outside-crop-output.pdf";
    quantapdf_document *document;
    quantapdf_document *reopened;
    quantapdf_output *baseline = NULL;
    quantapdf_output *changed = NULL;
    const unsigned char *baseline_data = NULL;
    const unsigned char *changed_data = NULL;
    size_t baseline_size = 0;
    size_t changed_size = 0;
    quantapdf_rect source_media;
    quantapdf_rect source_visible;
    quantapdf_page_trim noop;
    quantapdf_page_trim trim;

    sibling_fixture_path("crop-cropbox-outside-media.pdf", path, sizeof(path));
    document = open_document(path);
    source_media = page_box(document, QUANTAPDF_PAGE_BOX_MEDIA);
    source_visible = page_bounds(document);
    check_rect_close(source_media, (quantapdf_rect){0, -10, 300, 190});
    check_rect_close(source_visible, (quantapdf_rect){0, 0, 280, 190});

    noop = make_trim(source_media);
    CHECK(quantapdf_trim_pages(document, &noop, 1, &baseline) == QUANTAPDF_OK);
    CHECK(baseline != NULL);
    CHECK(quantapdf_output_data(
              baseline, &baseline_data, &baseline_size) == QUANTAPDF_OK);
    CHECK(trim_raw_expect_outside_relation(
              baseline_data, baseline_size, source_media_raw, crop_raw));

    trim = make_trim((quantapdf_rect){10, 0, 290, 180});
    CHECK(quantapdf_trim_pages(document, &trim, 1, &changed) == QUANTAPDF_OK);
    CHECK(changed != NULL);
    CHECK(quantapdf_output_data(
              changed, &changed_data, &changed_size) == QUANTAPDF_OK);
    CHECK(trim_raw_expect_outside_relation(
              changed_data, changed_size, changed_media_raw, crop_raw));
    CHECK(trim_raw_expect_preserved_cropbox(
              baseline_data, baseline_size, changed_data, changed_size, 0));
    CHECK(trim_raw_expect_preserved_graph(
              baseline_data, baseline_size, changed_data, changed_size));

    check_rect_close(
        page_box(document, QUANTAPDF_PAGE_BOX_MEDIA), source_media);
    check_rect_close(page_bounds(document), source_visible);

    (void)remove(output_path);
    CHECK(write_bytes(output_path, changed_data, changed_size));
    reopened = open_document(output_path);
    check_rect_close(page_bounds(reopened), (quantapdf_rect){0, 0, 270, 180});
    check_rect_close(
        page_box(reopened, QUANTAPDF_PAGE_BOX_CROP),
        (quantapdf_rect){0, 0, 270, 180});
    check_rect_close(
        page_box(reopened, QUANTAPDF_PAGE_BOX_MEDIA),
        (quantapdf_rect){0, 0, 280, 180});
    quantapdf_close(reopened);
    (void)remove(output_path);

    quantapdf_drop_output(changed);
    quantapdf_drop_output(baseline);
    quantapdf_close(document);
    return 1;
}
