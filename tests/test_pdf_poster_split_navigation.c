#include "test_context_fixture.h"
#include "test_pdf_poster_split_internal.h"

#include <quantapdf/quantapdf.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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
    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    return document;
}

static quantapdf_output *split_page_zero(
    quantapdf_document *document,
    quantapdf_status expected)
{
    quantapdf_page_poster_split split;
    quantapdf_output *output = NULL;

    split.page_index = 0;
    split.columns = 2;
    split.rows = 2;
    CHECK(quantapdf_poster_split_pages(document, &split, 1, &output) == expected);
    if (expected == QUANTAPDF_OK)
        CHECK(output != NULL);
    else
        CHECK(output == NULL);
    return output;
}

static void expect_malformed_destination(void)
{
    quantapdf_document *document = open_document(POSTER_MALFORMED_DESTINATION_PDF);
    quantapdf_output *output = split_page_zero(document, QUANTAPDF_ERROR_FORMAT);
    CHECK(output == NULL);
    quantapdf_close(document);
}

static void expect_internal_link(
    const quantapdf_link_page *links,
    size_t index,
    int target_page,
    float target_x,
    float target_y)
{
    quantapdf_link_info info;

    CHECK(quantapdf_link_get_info(links, index, &info) == QUANTAPDF_OK);
    CHECK(info.kind == QUANTAPDF_LINK_INTERNAL);
    CHECK(info.target_page == target_page);
    CHECK(close_float(info.target.x, target_x));
    CHECK(close_float(info.target.y, target_y));
}

static void expect_primary_navigation(void)
{
    quantapdf_document *source = open_document(POSTER_NAVIGATION_PDF);
    quantapdf_output *output = split_page_zero(source, QUANTAPDF_OK);
    const unsigned char *data = NULL;
    size_t size = 0;
    quantapdf_document *reopened;
    quantapdf_page *page = NULL;
    quantapdf_link_page *links = NULL;
    quantapdf_outline *outline = NULL;
    quantapdf_outline_info info;
    size_t count = 0;

    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(data != NULL && size != 0);
    CHECK(poster_raw_check_navigation(data, size));
    CHECK(quantapdf_output_save_file(output, POSTER_OUTPUT_PDF) == QUANTAPDF_OK);
    reopened = open_document(POSTER_OUTPUT_PDF);

    CHECK(quantapdf_load_page(reopened, 4, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_links(page, &links) == QUANTAPDF_OK);
    CHECK(quantapdf_link_count(links, &count) == QUANTAPDF_OK);
    CHECK(count == 5);
    expect_internal_link(links, 0, 0, 50.0f, 75.0f);
    expect_internal_link(links, 1, 1, 50.0f, 75.0f);
    /*
     * Links 2 and 3 deliberately remain named references. The existing
     * public Links contract only characterizes direct internal /XYZ targets;
     * Names/Dests and legacy /Dests are therefore proved by the raw graph
     * check above, which also proves the referring name/string is unchanged.
     */
    expect_internal_link(links, 4, 1, 0.0f, 75.0f);

    CHECK(quantapdf_document_outline(reopened, &outline) == QUANTAPDF_OK);
    CHECK(outline != NULL);
    CHECK(quantapdf_outline_count(outline, &count) == QUANTAPDF_OK);
    CHECK(count == 2);

    CHECK(quantapdf_outline_get_info(outline, 0, &info) == QUANTAPDF_OK);
    CHECK(info.destination_kind == QUANTAPDF_OUTLINE_DESTINATION_INTERNAL);
    CHECK(info.target_page == 2);
    CHECK(close_float(info.target.x, 50.0f));
    CHECK(close_float(info.target.y, 75.0f));

    CHECK(quantapdf_outline_get_info(outline, 1, &info) == QUANTAPDF_OK);
    CHECK(info.destination_kind == QUANTAPDF_OUTLINE_DESTINATION_INTERNAL);
    CHECK(info.target_page == 3);
    CHECK(close_float(info.target.x, 50.0f));
    CHECK(close_float(info.target.y, 75.0f));

    quantapdf_drop_outline(outline);
    quantapdf_drop_link_page(links);
    quantapdf_drop_page(page);
    quantapdf_close(reopened);
    quantapdf_drop_output(output);
    quantapdf_close(source);
}

static void expect_transform_navigation(const char *path)
{
    quantapdf_document *source = open_document(path);
    quantapdf_page *source_page = NULL;
    quantapdf_page *link_page = NULL;
    quantapdf_link_page *source_links = NULL;
    quantapdf_link_info source_info;
    quantapdf_rect source_bounds;
    quantapdf_output *output;
    quantapdf_document *reopened;
    quantapdf_page *output_link_page = NULL;
    quantapdf_link_page *output_links = NULL;
    quantapdf_link_info output_info;
    float mid_x;
    float mid_y;
    float tile_x0;
    float tile_y0;
    size_t row;
    size_t column;
    int expected_page;

    CHECK(quantapdf_load_page(source, 0, &source_page) == QUANTAPDF_OK);
    CHECK(quantapdf_page_bounds(source_page, &source_bounds) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(source, 1, &link_page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_links(link_page, &source_links) == QUANTAPDF_OK);

    CHECK(quantapdf_link_get_info(source_links, 0, &source_info) == QUANTAPDF_OK);
    CHECK(source_info.kind == QUANTAPDF_LINK_INTERNAL);
    CHECK(source_info.target_page == 0);

    mid_x = (source_bounds.x0 + source_bounds.x1) * 0.5f;
    mid_y = (source_bounds.y0 + source_bounds.y1) * 0.5f;
    column = source_info.target.x >= mid_x ? 1u : 0u;
    row = source_info.target.y >= mid_y ? 1u : 0u;
    expected_page = (int)(row * 2u + column);
    tile_x0 = column != 0 ? mid_x : source_bounds.x0;
    tile_y0 = row != 0 ? mid_y : source_bounds.y0;

    output = split_page_zero(source, QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(output, POSTER_OUTPUT_PDF) == QUANTAPDF_OK);
    reopened = open_document(POSTER_OUTPUT_PDF);
    CHECK(quantapdf_load_page(reopened, 4, &output_link_page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_links(output_link_page, &output_links) == QUANTAPDF_OK);

    CHECK(quantapdf_link_get_info(output_links, 0, &output_info) == QUANTAPDF_OK);
    CHECK(output_info.kind == QUANTAPDF_LINK_INTERNAL);
    CHECK(output_info.target_page == expected_page);
    CHECK(close_float(output_info.target.x, source_info.target.x - tile_x0));
    CHECK(close_float(output_info.target.y, source_info.target.y - tile_y0));

    quantapdf_drop_link_page(output_links);
    quantapdf_drop_page(output_link_page);
    quantapdf_close(reopened);
    quantapdf_drop_output(output);
    quantapdf_drop_link_page(source_links);
    quantapdf_drop_page(link_page);
    quantapdf_drop_page(source_page);
    quantapdf_close(source);
}

int poster_run_navigation_tests(void)
{
    expect_malformed_destination();
    expect_primary_navigation();
    expect_transform_navigation(POSTER_NAVIGATION_ROTATE_90_PDF);
    expect_transform_navigation(POSTER_NAVIGATION_USERUNIT_PDF);
    return 0;
}
