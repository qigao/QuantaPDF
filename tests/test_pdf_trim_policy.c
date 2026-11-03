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

static quantapdf_page_trim make_trim(int page_index, quantapdf_rect bounds)
{
    quantapdf_page_trim trim;

    trim.page_index = page_index;
    trim.bounds = bounds;
    return trim;
}

static void run_transformed_case(
    const char *fixture_name,
    float inset_x,
    float inset_y,
    const float expected_raw[4])
{
    char path[1024];
    char output_path[128];
    quantapdf_document *document;
    quantapdf_document *reopened;
    quantapdf_output *baseline = NULL;
    quantapdf_output *changed = NULL;
    const unsigned char *baseline_data = NULL;
    const unsigned char *changed_data = NULL;
    size_t baseline_size = 0;
    size_t changed_size = 0;
    quantapdf_rect source_media;
    quantapdf_rect source_after;
    quantapdf_rect requested;
    quantapdf_page_trim full;
    quantapdf_page_trim trim;

    sibling_fixture_path(fixture_name, path, sizeof(path));
    snprintf(output_path, sizeof(output_path), "trim-policy-%s", fixture_name);
    document = open_document(path);
    source_media = page_box(document, 0, QUANTAPDF_PAGE_BOX_MEDIA);
    CHECK(source_media.x1 - source_media.x0 > 2.0f * inset_x);
    CHECK(source_media.y1 - source_media.y0 > 2.0f * inset_y);

    full = make_trim(0, source_media);
    requested = (quantapdf_rect){
        source_media.x0 + inset_x,
        source_media.y0 + inset_y,
        source_media.x1 - inset_x,
        source_media.y1 - inset_y};
    trim = make_trim(0, requested);

    CHECK(quantapdf_trim_pages(document, &full, 1, &baseline) == QUANTAPDF_OK);
    CHECK(baseline != NULL);
    CHECK(quantapdf_output_data(
              baseline, &baseline_data, &baseline_size) == QUANTAPDF_OK);
    CHECK(baseline_data != NULL && baseline_size != 0);

    CHECK(quantapdf_trim_pages(document, &trim, 1, &changed) == QUANTAPDF_OK);
    CHECK(changed != NULL);
    CHECK(quantapdf_output_data(
              changed, &changed_data, &changed_size) == QUANTAPDF_OK);
    CHECK(changed_data != NULL && changed_size != 0);
    CHECK(trim_raw_expect_local_mediabox(
              changed_data, changed_size, 0, 1, expected_raw));
    CHECK(trim_raw_expect_preserved_cropbox(
              baseline_data, baseline_size, changed_data, changed_size, 0));
    CHECK(trim_raw_expect_preserved_graph(
              baseline_data, baseline_size, changed_data, changed_size));

    source_after = page_box(document, 0, QUANTAPDF_PAGE_BOX_MEDIA);
    check_rect_close(source_after, source_media);

    (void)remove(output_path);
    CHECK(write_bytes(output_path, changed_data, changed_size));
    reopened = open_document(output_path);
    check_rect_close(
        page_bounds(reopened, 0),
        (quantapdf_rect){
            0.0f,
            0.0f,
            requested.x1 - requested.x0,
            requested.y1 - requested.y0});
    check_rect_close(
        page_box(reopened, 0, QUANTAPDF_PAGE_BOX_MEDIA),
        page_bounds(reopened, 0));
    check_rect_close(
        page_box(reopened, 0, QUANTAPDF_PAGE_BOX_CROP),
        page_bounds(reopened, 0));

    quantapdf_close(reopened);
    quantapdf_drop_output(changed);
    quantapdf_drop_output(baseline);
    quantapdf_close(document);
    (void)remove(output_path);
}

static void test_default_boxes(void)
{
    static const float bleed[4] = {10, 10, 390, 290};
    static const float trim_box[4] = {20, 20, 380, 280};
    static const float art[4] = {30, 30, 370, 270};
    static const float changed0[4] = {20, 20, 380, 280};
    static const float changed1[4] = {40, 30, 360, 270};
    char path[1024];
    quantapdf_document *document;
    quantapdf_output *baseline = NULL;
    quantapdf_output *changed = NULL;
    const unsigned char *baseline_data = NULL;
    const unsigned char *changed_data = NULL;
    size_t baseline_size = 0;
    size_t changed_size = 0;
    quantapdf_rect media0;
    quantapdf_rect media1;
    quantapdf_page_trim noops[2];
    quantapdf_page_trim trims[2];

    sibling_fixture_path("trim-default-boxes.pdf", path, sizeof(path));
    document = open_document(path);
    media0 = page_box(document, 0, QUANTAPDF_PAGE_BOX_MEDIA);
    media1 = page_box(document, 1, QUANTAPDF_PAGE_BOX_MEDIA);
    check_rect_close(media0, (quantapdf_rect){0, 0, 400, 300});
    check_rect_close(media1, media0);

    noops[0] = make_trim(0, media0);
    noops[1] = make_trim(1, media1);
    CHECK(quantapdf_trim_pages(document, noops, 2, &baseline) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              baseline, &baseline_data, &baseline_size) == QUANTAPDF_OK);
    CHECK(trim_raw_expect_production_boxes(
              baseline_data, baseline_size, 0,
              0, NULL, 0, NULL, 0, NULL));
    CHECK(trim_raw_expect_production_boxes(
              baseline_data, baseline_size, 1,
              1, bleed, 1, trim_box, 1, art));

    trims[0] = make_trim(0, (quantapdf_rect){20, 20, 380, 280});
    trims[1] = make_trim(1, (quantapdf_rect){40, 30, 360, 270});
    CHECK(quantapdf_trim_pages(document, trims, 2, &changed) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              changed, &changed_data, &changed_size) == QUANTAPDF_OK);
    CHECK(trim_raw_expect_local_mediabox(
              changed_data, changed_size, 0, 1, changed0));
    CHECK(trim_raw_expect_local_mediabox(
              changed_data, changed_size, 1, 1, changed1));
    CHECK(trim_raw_expect_production_boxes(
              changed_data, changed_size, 0,
              0, NULL, 0, NULL, 0, NULL));
    CHECK(trim_raw_expect_production_boxes(
              changed_data, changed_size, 1,
              1, bleed, 1, trim_box, 1, art));
    CHECK(trim_raw_expect_preserved_graph(
              baseline_data, baseline_size, changed_data, changed_size));

    check_rect_close(page_box(document, 0, QUANTAPDF_PAGE_BOX_MEDIA), media0);
    check_rect_close(page_box(document, 1, QUANTAPDF_PAGE_BOX_MEDIA), media1);

    quantapdf_drop_output(changed);
    quantapdf_drop_output(baseline);
    quantapdf_close(document);
}

int trim_run_policy_tests(void)
{
    static const float rotate_raw[4] = {20, 20, 380, 280};
    static const float userunit_raw[4] = {10, 10, 190, 140};

    run_transformed_case("trim-rotate-90.pdf", 20.0f, 20.0f, rotate_raw);
    run_transformed_case("trim-userunit.pdf", 20.0f, 20.0f, userunit_raw);
    test_default_boxes();
    return 1;
}
