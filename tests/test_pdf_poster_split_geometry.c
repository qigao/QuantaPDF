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

static int close_float(float left, float right)
{
    return fabsf(left - right) < 0.01f;
}

static quantapdf_document *open_document(const char *path)
{
    quantapdf_document *document = NULL;
    CHECK(quantapdf_open(path, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    return document;
}

static quantapdf_rect page_bounds(
    quantapdf_document *document,
    int page_index)
{
    quantapdf_page *page = NULL;
    quantapdf_rect bounds = {0};
    CHECK(quantapdf_load_page(document, page_index, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_page_bounds(page, &bounds) == QUANTAPDF_OK);
    quantapdf_drop_page(page);
    return bounds;
}

static void expect_bounds(
    quantapdf_document *document,
    int page_index,
    float width,
    float height)
{
    quantapdf_rect bounds = page_bounds(document, page_index);
    CHECK(close_float(bounds.x0, 0.0f));
    CHECK(close_float(bounds.y0, 0.0f));
    CHECK(close_float(bounds.x1, width));
    CHECK(close_float(bounds.y1, height));
}

static void expect_text(quantapdf_document *document, int page_index, const char *needle)
{
    quantapdf_page *page = NULL;
    char *text = NULL;
    size_t size = 0;
    CHECK(quantapdf_load_page(document, page_index, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_text(page, &text, &size) == QUANTAPDF_OK);
    CHECK(text != NULL && size != 0);
    CHECK(strstr(text, needle) != NULL);
    quantapdf_free(text);
    quantapdf_drop_page(page);
}

static void compare_render(
    quantapdf_document *source,
    int source_page_index,
    quantapdf_rect source_clip,
    quantapdf_document *output,
    int output_page_index)
{
    quantapdf_page *source_page = NULL;
    quantapdf_page *output_page = NULL;
    quantapdf_bitmap *source_bitmap = NULL;
    quantapdf_bitmap *output_bitmap = NULL;
    quantapdf_render_options source_options = {0};
    quantapdf_render_options output_options = {0};
    const unsigned char *source_data = NULL;
    const unsigned char *output_data = NULL;
    size_t source_size = 0;
    size_t output_size = 0;
    int sw, sh, ss, sc;
    int ow, oh, os, oc;

    source_options.dpi = 72.0f;
    source_options.rotation_degrees = 0.0f;
    source_options.clip_enabled = 1;
    source_options.clip = source_clip;
    source_options.alpha = 0;
    output_options = source_options;
    output_options.clip_enabled = 0;

    CHECK(quantapdf_load_page(source, source_page_index, &source_page) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(output, output_page_index, &output_page) == QUANTAPDF_OK);
    CHECK(quantapdf_render_page_with_options(
              source_page, &source_options, &source_bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_render_page_with_options(
              output_page, &output_options, &output_bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(source_bitmap, &sw, &sh, &ss, &sc) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(output_bitmap, &ow, &oh, &os, &oc) == QUANTAPDF_OK);
    CHECK(sw == ow && sh == oh && ss == os && sc == oc);
    CHECK(quantapdf_bitmap_data(source_bitmap, &source_data, &source_size) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_data(output_bitmap, &output_data, &output_size) == QUANTAPDF_OK);
    CHECK(source_size == output_size);
    CHECK(source_size != 0);
    CHECK(memcmp(source_data, output_data, source_size) == 0);

    quantapdf_drop_bitmap(output_bitmap);
    quantapdf_drop_bitmap(source_bitmap);
    quantapdf_drop_page(output_page);
    quantapdf_drop_page(source_page);
}

static quantapdf_output *split_to_file(
    quantapdf_document *source,
    int page_index,
    size_t columns,
    size_t rows)
{
    quantapdf_page_poster_split split;
    quantapdf_output *output = NULL;

    split.struct_size = sizeof(split);
    split.page_index = page_index;
    split.columns = columns;
    split.rows = rows;
    CHECK(quantapdf_poster_split_pages(source, &split, 1, &output) == QUANTAPDF_OK);
    CHECK(output != NULL);
    CHECK(quantapdf_output_save_file(output, POSTER_OUTPUT_PDF) == QUANTAPDF_OK);
    return output;
}

static void test_basic(void)
{
    static const quantapdf_rect clips[4] = {
        {0.0f, 0.0f, 200.0f, 150.0f},
        {200.0f, 0.0f, 400.0f, 150.0f},
        {0.0f, 150.0f, 200.0f, 300.0f},
        {200.0f, 150.0f, 400.0f, 300.0f}
    };
    static const float raw_boxes[4][4] = {
        {0.0f, 150.0f, 200.0f, 300.0f},
        {200.0f, 150.0f, 400.0f, 300.0f},
        {0.0f, 0.0f, 200.0f, 150.0f},
        {200.0f, 0.0f, 400.0f, 150.0f}
    };
    quantapdf_document *source = open_document(POSTER_BASIC_PDF);
    quantapdf_document *reopened;
    quantapdf_output *output = split_to_file(source, 1, 2, 2);
    const unsigned char *data = NULL;
    size_t size = 0;
    int count = 0;
    int tile;

    reopened = open_document(POSTER_OUTPUT_PDF);
    CHECK(quantapdf_page_count(reopened, &count) == QUANTAPDF_OK);
    CHECK(count == 6);
    expect_text(reopened, 0, "POSTER-BEFORE");
    expect_text(reopened, 5, "POSTER-AFTER");
    for (tile = 0; tile < 4; ++tile) {
        expect_bounds(reopened, tile + 1, 200.0f, 150.0f);
        compare_render(source, 1, clips[tile], reopened, tile + 1);
    }

    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(poster_raw_check_basic_tiles(
        data, size, 1, 4, raw_boxes, 0, 1.0f));

    quantapdf_close(reopened);
    quantapdf_drop_output(output);
    quantapdf_close(source);
}

static void test_rotate_90(void)
{
    quantapdf_document *source = open_document(POSTER_ROTATE_90_PDF);
    quantapdf_output *output = split_to_file(source, 0, 2, 2);
    quantapdf_document *reopened = open_document(POSTER_OUTPUT_PDF);
    quantapdf_rect source_bounds = page_bounds(source, 0);
    quantapdf_rect clip = {
        source_bounds.x0,
        source_bounds.y0,
        source_bounds.x0 + (source_bounds.x1 - source_bounds.x0) / 2.0f,
        source_bounds.y0 + (source_bounds.y1 - source_bounds.y0) / 2.0f
    };
    int count = 0;

    CHECK(quantapdf_page_count(reopened, &count) == QUANTAPDF_OK);
    CHECK(count == 4);
    expect_bounds(reopened, 0, 150.0f, 200.0f);
    compare_render(source, 0, clip, reopened, 0);

    quantapdf_close(reopened);
    quantapdf_drop_output(output);
    quantapdf_close(source);
}

static void test_userunit(void)
{
    quantapdf_document *source = open_document(POSTER_USERUNIT_PDF);
    quantapdf_output *output = split_to_file(source, 0, 2, 2);
    quantapdf_document *reopened = open_document(POSTER_OUTPUT_PDF);
    quantapdf_rect source_bounds = page_bounds(source, 0);
    quantapdf_rect clip = {
        source_bounds.x0,
        source_bounds.y0,
        source_bounds.x0 + (source_bounds.x1 - source_bounds.x0) / 2.0f,
        source_bounds.y0 + (source_bounds.y1 - source_bounds.y0) / 2.0f
    };
    int count = 0;

    CHECK(quantapdf_page_count(reopened, &count) == QUANTAPDF_OK);
    CHECK(count == 4);
    expect_bounds(reopened, 0, 200.0f, 150.0f);
    compare_render(source, 0, clip, reopened, 0);

    quantapdf_close(reopened);
    quantapdf_drop_output(output);
    quantapdf_close(source);
}

int poster_run_geometry_tests(void)
{
    test_basic();
    test_rotate_90();
    test_userunit();
    return 0;
}
