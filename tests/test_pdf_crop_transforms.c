#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>
#include "test_pdf_crop_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int quantapdf_pdf_crop_base_main(void);

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

static quantapdf_document *open_document(const char *path)
{
    quantapdf_document *document = NULL;

    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    return document;
}

static quantapdf_rect page_bounds_at(
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

static quantapdf_rect page_bounds(quantapdf_document *document)
{
    return page_bounds_at(document, 0);
}

static void check_rect_close(quantapdf_rect actual, quantapdf_rect expected)
{
    CHECK(close_float(actual.x0, expected.x0));
    CHECK(close_float(actual.y0, expected.y0));
    CHECK(close_float(actual.x1, expected.x1));
    CHECK(close_float(actual.y1, expected.y1));
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

static void run_transformed_case(
    const char *path,
    float inset_x,
    float inset_y,
    const float expected_raw[4])
{
    quantapdf_document *document = open_document(path);
    quantapdf_document *reopened = NULL;
    quantapdf_output *baseline = NULL;
    quantapdf_output *changed = NULL;
    const unsigned char *baseline_data = NULL;
    const unsigned char *changed_data = NULL;
    size_t baseline_size = 0;
    size_t changed_size = 0;
    quantapdf_rect source_bounds = page_bounds(document);
    quantapdf_rect source_after;
    quantapdf_rect output_bounds;
    quantapdf_page_crop full;
    quantapdf_page_crop crop;

    CHECK(source_bounds.x0 < source_bounds.x1);
    CHECK(source_bounds.y0 < source_bounds.y1);
    CHECK(source_bounds.x1 - source_bounds.x0 > 2.0f * inset_x);
    CHECK(source_bounds.y1 - source_bounds.y0 > 2.0f * inset_y);

    full = make_crop(
        0,
        source_bounds.x0,
        source_bounds.y0,
        source_bounds.x1,
        source_bounds.y1);
    crop = make_crop(
        0,
        source_bounds.x0 + inset_x,
        source_bounds.y0 + inset_y,
        source_bounds.x1 - inset_x,
        source_bounds.y1 - inset_y);

    CHECK(quantapdf_crop_pages(document, &full, 1, &baseline) == QUANTAPDF_OK);
    CHECK(baseline != NULL);
    CHECK(quantapdf_output_data(
              baseline, &baseline_data, &baseline_size) == QUANTAPDF_OK);
    CHECK(baseline_data != NULL && baseline_size != 0);
    CHECK(crop_raw_expect_no_local_default_boxes(
              baseline_data, baseline_size, 0));

    CHECK(quantapdf_crop_pages(document, &crop, 1, &changed) == QUANTAPDF_OK);
    CHECK(changed != NULL);
    CHECK(quantapdf_output_data(
              changed, &changed_data, &changed_size) == QUANTAPDF_OK);
    CHECK(changed_data != NULL && changed_size != 0);
    CHECK(crop_raw_expect_local_cropbox(
              changed_data, changed_size, 0, 1, expected_raw));
    CHECK(crop_raw_expect_no_local_default_boxes(
              changed_data, changed_size, 0));
    CHECK(crop_raw_expect_preserved_graph(
              baseline_data, baseline_size, changed_data, changed_size));

    source_after = page_bounds(document);
    check_rect_close(source_after, source_bounds);

    (void)remove(CROP_OUTPUT_PDF);
    CHECK(write_bytes(CROP_OUTPUT_PDF, changed_data, changed_size));
    reopened = open_document(CROP_OUTPUT_PDF);
    output_bounds = page_bounds(reopened);
    check_rect_close(
        output_bounds,
        (quantapdf_rect){
            0.0f,
            0.0f,
            crop.bounds.x1 - crop.bounds.x0,
            crop.bounds.y1 - crop.bounds.y0});

    quantapdf_close(reopened);
    quantapdf_drop_output(changed);
    quantapdf_drop_output(baseline);
    quantapdf_close(document);
    (void)remove(CROP_OUTPUT_PDF);
}

static void run_batch_cases(void)
{
    static const float full_raw[4] = {0.0f, 0.0f, 400.0f, 300.0f};
    static const float changed0_raw[4] = {50.0f, 40.0f, 350.0f, 260.0f};
    static const float changed1_raw[4] = {20.0f, 30.0f, 380.0f, 270.0f};
    quantapdf_document *document = open_document(CROP_INTERACTIVE_PDF);
    quantapdf_document *reopened = NULL;
    quantapdf_output *noop_a = NULL;
    quantapdf_output *noop_b = NULL;
    quantapdf_output *mixed_output = NULL;
    quantapdf_output *changed_a = NULL;
    quantapdf_output *changed_b = NULL;
    quantapdf_output *failed = output_sentinel();
    const unsigned char *noop_a_data = NULL;
    const unsigned char *noop_b_data = NULL;
    const unsigned char *mixed_data = NULL;
    const unsigned char *changed_a_data = NULL;
    const unsigned char *changed_b_data = NULL;
    size_t noop_a_size = 0;
    size_t noop_b_size = 0;
    size_t mixed_size = 0;
    size_t changed_a_size = 0;
    size_t changed_b_size = 0;
    quantapdf_rect source0 = page_bounds_at(document, 0);
    quantapdf_rect source1 = page_bounds_at(document, 1);
    quantapdf_rect source0_after;
    quantapdf_rect source1_after;
    quantapdf_page_crop noops[2];
    quantapdf_page_crop mixed[2];
    quantapdf_page_crop changed[2];
    quantapdf_page_crop invalid[2];

    check_rect_close(source0, (quantapdf_rect){0.0f, 0.0f, 400.0f, 300.0f});
    check_rect_close(source1, (quantapdf_rect){0.0f, 0.0f, 400.0f, 300.0f});

    noops[0] = make_crop(0, source0.x0, source0.y0, source0.x1, source0.y1);
    noops[1] = make_crop(1, source1.x0, source1.y0, source1.x1, source1.y1);

    CHECK(quantapdf_crop_pages(document, noops, 2, &noop_a) == QUANTAPDF_OK);
    CHECK(noop_a != NULL);
    CHECK(quantapdf_crop_pages(document, noops, 2, &noop_b) == QUANTAPDF_OK);
    CHECK(noop_b != NULL);
    CHECK(quantapdf_output_data(noop_a, &noop_a_data, &noop_a_size) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(noop_b, &noop_b_data, &noop_b_size) == QUANTAPDF_OK);
    CHECK(noop_a_data != NULL && noop_a_size != 0);
    CHECK(noop_b_data != NULL && noop_b_size == noop_a_size);
    CHECK(memcmp(noop_a_data, noop_b_data, noop_a_size) == 0);
    CHECK(crop_raw_expect_local_cropbox(
              noop_a_data, noop_a_size, 0, 1, full_raw));
    CHECK(crop_raw_expect_local_cropbox(
              noop_a_data, noop_a_size, 1, 1, full_raw));

    (void)remove(CROP_OUTPUT_PDF);
    CHECK(write_bytes(CROP_OUTPUT_PDF, noop_a_data, noop_a_size));
    reopened = open_document(CROP_OUTPUT_PDF);
    check_rect_close(page_bounds_at(reopened, 0), source0);
    check_rect_close(page_bounds_at(reopened, 1), source1);
    quantapdf_close(reopened);
    reopened = NULL;
    (void)remove(CROP_OUTPUT_PDF);

    mixed[0] = noops[0];
    mixed[1] = make_crop(1, 20.0f, 30.0f, 380.0f, 270.0f);
    CHECK(quantapdf_crop_pages(document, mixed, 2, &mixed_output) == QUANTAPDF_OK);
    CHECK(mixed_output != NULL);
    CHECK(quantapdf_output_data(
              mixed_output, &mixed_data, &mixed_size) == QUANTAPDF_OK);
    CHECK(mixed_data != NULL && mixed_size != 0);
    CHECK(crop_raw_expect_local_cropbox(
              mixed_data, mixed_size, 0, 1, full_raw));
    CHECK(crop_raw_expect_local_cropbox(
              mixed_data, mixed_size, 1, 1, changed1_raw));
    CHECK(crop_raw_expect_preserved_graph(
              noop_a_data, noop_a_size, mixed_data, mixed_size));

    changed[0] = make_crop(0, 50.0f, 40.0f, 350.0f, 260.0f);
    changed[1] = make_crop(1, 20.0f, 30.0f, 380.0f, 270.0f);
    CHECK(quantapdf_crop_pages(document, changed, 2, &changed_a) == QUANTAPDF_OK);
    CHECK(changed_a != NULL);
    CHECK(quantapdf_crop_pages(document, changed, 2, &changed_b) == QUANTAPDF_OK);
    CHECK(changed_b != NULL);
    CHECK(quantapdf_output_data(
              changed_a, &changed_a_data, &changed_a_size) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              changed_b, &changed_b_data, &changed_b_size) == QUANTAPDF_OK);
    CHECK(changed_a_data != NULL && changed_a_size != 0);
    CHECK(changed_b_data != NULL && changed_b_size == changed_a_size);
    CHECK(memcmp(changed_a_data, changed_b_data, changed_a_size) == 0);
    CHECK(crop_raw_expect_local_cropbox(
              changed_a_data, changed_a_size, 0, 1, changed0_raw));
    CHECK(crop_raw_expect_local_cropbox(
              changed_a_data, changed_a_size, 1, 1, changed1_raw));
    CHECK(crop_raw_expect_preserved_graph(
              noop_a_data, noop_a_size, changed_a_data, changed_a_size));

    (void)remove(CROP_OUTPUT_PDF);
    CHECK(write_bytes(CROP_OUTPUT_PDF, changed_a_data, changed_a_size));
    reopened = open_document(CROP_OUTPUT_PDF);
    check_rect_close(
        page_bounds_at(reopened, 0),
        (quantapdf_rect){0.0f, 0.0f, 300.0f, 220.0f});
    check_rect_close(
        page_bounds_at(reopened, 1),
        (quantapdf_rect){0.0f, 0.0f, 360.0f, 240.0f});
    quantapdf_close(reopened);
    reopened = NULL;
    (void)remove(CROP_OUTPUT_PDF);

    invalid[0] = changed[0];
    invalid[1] = noops[1];
    invalid[1].bounds.x1 = source1.x1 + 1.0f;
    failed = output_sentinel();
    CHECK(quantapdf_crop_pages(document, invalid, 2, &failed) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(failed == NULL);

    source0_after = page_bounds_at(document, 0);
    source1_after = page_bounds_at(document, 1);
    check_rect_close(source0_after, source0);
    check_rect_close(source1_after, source1);

    quantapdf_drop_output(changed_b);
    quantapdf_drop_output(changed_a);
    quantapdf_drop_output(mixed_output);
    quantapdf_drop_output(noop_b);
    quantapdf_drop_output(noop_a);
    quantapdf_close(document);
}

int main(void)
{
    static const float rotate_raw[4] = {20.0f, 20.0f, 380.0f, 280.0f};
    static const float userunit_raw[4] = {10.0f, 10.0f, 190.0f, 140.0f};
    static const float outside_raw[4] = {10.0f, 10.0f, 270.0f, 180.0f};
    int result = quantapdf_pdf_crop_base_main();

    if (result != EXIT_SUCCESS)
        return result;

    run_transformed_case(CROP_ROTATE_90_PDF, 20.0f, 20.0f, rotate_raw);
    run_transformed_case(CROP_USERUNIT_PDF, 20.0f, 20.0f, userunit_raw);
    run_transformed_case(
        CROP_OUTSIDE_MEDIA_PDF, 10.0f, 10.0f, outside_raw);
    run_batch_cases();
    return EXIT_SUCCESS;
}
