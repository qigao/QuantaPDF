#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>
#include <stdio.h>
#include <stdlib.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

static void trace_step(const char *step)
{
    fprintf(stderr, "[quantapdf.document] %s\n", step);
    fflush(stderr);
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static void test_arguments_and_errors(void)
{
    int sentinel = 0;
    quantapdf_document *doc = (quantapdf_document *)&sentinel;
    int pages = 123;

    trace_step("argument validation");
    CHECK(quantapdf_open(test_context(), NULL, NULL, &doc) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(doc == NULL);

    doc = (quantapdf_document *)&sentinel;
    CHECK(quantapdf_open(test_context(), "", NULL, &doc) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(doc == NULL);



    trace_step("missing file");
    doc = (quantapdf_document *)&sentinel;
    CHECK(quantapdf_open(test_context(), MISSING_PDF, NULL, &doc) == QUANTAPDF_ERROR_IO);
    CHECK(doc == NULL);

    trace_step("encrypted file without password");
    doc = (quantapdf_document *)&sentinel;
    CHECK(quantapdf_open(test_context(), ENCRYPTED_PDF, NULL, &doc) == QUANTAPDF_ERROR_PASSWORD);
    CHECK(doc == NULL);

    trace_step("encrypted file with wrong password");
    doc = (quantapdf_document *)&sentinel;
    CHECK(quantapdf_open(test_context(), ENCRYPTED_PDF, "wrong", &doc) == QUANTAPDF_ERROR_PASSWORD);
    CHECK(doc == NULL);

    trace_step("encrypted file with correct password");
    doc = NULL;
    CHECK(quantapdf_open(test_context(), ENCRYPTED_PDF, "user-pass", &doc) == QUANTAPDF_OK);
    CHECK(doc != NULL);
    quantapdf_close(doc);

    trace_step("truncated file");
    doc = (quantapdf_document *)&sentinel;
    CHECK(quantapdf_open(test_context(), TRUNCATED_PDF, NULL, &doc) == QUANTAPDF_ERROR_FORMAT);
    CHECK(doc == NULL);

    trace_step("non-PDF file");
    doc = (quantapdf_document *)&sentinel;
    CHECK(quantapdf_open(test_context(), NON_PDF, NULL, &doc) == QUANTAPDF_ERROR_FORMAT);
    CHECK(doc == NULL);

    trace_step("page-count argument validation");
    pages = 123;
    CHECK(quantapdf_page_count(NULL, &pages) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(pages == 123);

    doc = NULL;
    CHECK(quantapdf_open(test_context(), ONE_PAGE_PDF, NULL, &doc) == QUANTAPDF_OK);
    CHECK(quantapdf_page_count(doc, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    quantapdf_close(doc);
}

static void test_repeated_lifecycle(void)
{
    int i;
    trace_step("repeated lifecycle start");
    for (i = 0; i < 100; ++i) {
        quantapdf_document *doc = NULL;
        int pages = 0;
        if ((i % 10) == 0) {
            fprintf(stderr, "[quantapdf.document] repeated lifecycle iteration %d\n", i);
            fflush(stderr);
        }
        CHECK(quantapdf_open(test_context(), ONE_PAGE_PDF, NULL, &doc) == QUANTAPDF_OK);
        CHECK(quantapdf_page_count(doc, &pages) == QUANTAPDF_OK);
        CHECK(pages == 1);
        quantapdf_close(doc);
    }
    trace_step("repeated lifecycle complete");
}

static void test_handle_isolation(void)
{
    quantapdf_document *a = NULL;
    quantapdf_document *b = NULL;
    int a_pages = 0;
    int b_pages = 0;

    trace_step("handle isolation");
    CHECK(quantapdf_open(test_context(), ONE_PAGE_PDF, NULL, &a) == QUANTAPDF_OK);
    CHECK(quantapdf_open(test_context(), TWO_PAGE_PDF, NULL, &b) == QUANTAPDF_OK);
    CHECK(quantapdf_page_count(b, &b_pages) == QUANTAPDF_OK);
    CHECK(quantapdf_page_count(a, &a_pages) == QUANTAPDF_OK);
    CHECK(a_pages == 1);
    CHECK(b_pages == 2);
    quantapdf_close(a);
    quantapdf_close(b);
}

static void test_page_lifecycle(void)
{
    int sentinel = 0;
    quantapdf_document *doc = NULL;
    quantapdf_page *page = (quantapdf_page *)&sentinel;

    trace_step("page lifecycle");
    CHECK(quantapdf_load_page(NULL, 0, &page) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(page == NULL);

    CHECK(quantapdf_open(test_context(), ONE_PAGE_PDF, NULL, &doc) == QUANTAPDF_OK);

    page = (quantapdf_page *)&sentinel;
    CHECK(quantapdf_load_page(doc, -1, &page) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(page == NULL);

    page = (quantapdf_page *)&sentinel;
    CHECK(quantapdf_load_page(doc, 1, &page) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(page == NULL);

    CHECK(quantapdf_load_page(doc, 0, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    page = NULL;
    CHECK(quantapdf_load_page(doc, 0, &page) == QUANTAPDF_OK);
    CHECK(page != NULL);
    quantapdf_drop_page(page);
    quantapdf_drop_page(NULL);

    quantapdf_close(doc);
}

static void test_page_bounds(void)
{
    quantapdf_document *doc = NULL;
    quantapdf_page *page = NULL;
    quantapdf_rect bounds = { -1.0f, -2.0f, -3.0f, -4.0f };

    trace_step("page bounds");
    CHECK(quantapdf_page_bounds(NULL, &bounds) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bounds.x0 == -1.0f);
    CHECK(bounds.y0 == -2.0f);
    CHECK(bounds.x1 == -3.0f);
    CHECK(bounds.y1 == -4.0f);

    CHECK(quantapdf_open(test_context(), ONE_PAGE_PDF, NULL, &doc) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(doc, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_page_bounds(page, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_page_bounds(page, &bounds) == QUANTAPDF_OK);
    CHECK(bounds.x0 == 0.0f);
    CHECK(bounds.y0 == 0.0f);
    CHECK(bounds.x1 == 72.0f);
    CHECK(bounds.y1 == 72.0f);
    quantapdf_drop_page(page);
    quantapdf_close(doc);
}

static void test_page_box_bounds(void)
{
    quantapdf_document *doc = NULL;
    quantapdf_page *page = NULL;
    quantapdf_rect media = { 0 };
    quantapdf_rect crop = { 0 };
    quantapdf_rect sentinel = { -1.0f, -2.0f, -3.0f, -4.0f };

    trace_step("page box bounds");
    CHECK(quantapdf_page_box_bounds(NULL, QUANTAPDF_PAGE_BOX_MEDIA, &sentinel) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(sentinel.x0 == -1.0f);
    CHECK(sentinel.y0 == -2.0f);
    CHECK(sentinel.x1 == -3.0f);
    CHECK(sentinel.y1 == -4.0f);

    CHECK(quantapdf_open(test_context(), PAGE_BOXES_PDF, NULL, &doc) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(doc, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_page_box_bounds(page, QUANTAPDF_PAGE_BOX_MEDIA, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_page_box_bounds(page, (quantapdf_page_box)99, &sentinel) == QUANTAPDF_ERROR_ARGUMENT);

    /* Box bounds use display page space: CropBox top-left is (0,0), y increases down. */
    CHECK(quantapdf_page_box_bounds(page, QUANTAPDF_PAGE_BOX_MEDIA, &media) == QUANTAPDF_OK);
    CHECK(media.x0 == -10.0f);
    CHECK(media.y0 == -20.0f);
    CHECK(media.x1 == 190.0f);
    CHECK(media.y1 == 80.0f);

    CHECK(quantapdf_page_box_bounds(page, QUANTAPDF_PAGE_BOX_CROP, &crop) == QUANTAPDF_OK);
    CHECK(crop.x0 == 0.0f);
    CHECK(crop.y0 == 0.0f);
    CHECK(crop.x1 == 180.0f);
    CHECK(crop.y1 == 60.0f);

    quantapdf_drop_page(page);
    quantapdf_close(doc);
}

static void test_page_render(void)
{
    int sentinel = 0;
    quantapdf_document *doc = NULL;
    quantapdf_page *page = NULL;
    quantapdf_bitmap *bitmap = (quantapdf_bitmap *)&sentinel;
    int width = -1;
    int height = -1;
    int stride = -1;
    int components = -1;
    const unsigned char *data = NULL;
    size_t size = 0;
    size_t i;

    trace_step("page render 72 dpi rgb");
    CHECK(quantapdf_render_page(NULL, &bitmap) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);

    CHECK(quantapdf_open(test_context(), ONE_PAGE_PDF, NULL, &doc) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(doc, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_render_page(page, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    bitmap = NULL;
    CHECK(quantapdf_render_page(page, &bitmap) == QUANTAPDF_OK);
    CHECK(bitmap != NULL);

    /* The rendered bitmap does not depend on the page handle after creation. */
    quantapdf_drop_page(page);
    page = NULL;

    CHECK(quantapdf_bitmap_dimensions(NULL, &width, &height, &stride, &components) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_bitmap_dimensions(bitmap, NULL, &height, &stride, &components) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 72);
    CHECK(height == 72);
    CHECK(stride == 72 * 3);
    CHECK(components == 3);

    CHECK(quantapdf_bitmap_data(NULL, &data, &size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_bitmap_data(bitmap, NULL, &size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_bitmap_data(bitmap, &data, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_bitmap_data(bitmap, &data, &size) == QUANTAPDF_OK);
    CHECK(data != NULL);
    CHECK(size == (size_t)stride * (size_t)height);
    for (i = 0; i < size; ++i)
        CHECK(data[i] == 255);

    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_bitmap(NULL);
    quantapdf_close(doc);
}

static void test_utf8_path(void)
{
    quantapdf_document *doc = NULL;
    int pages = -1;

    trace_step("utf8 path");
    CHECK(quantapdf_open(test_context(), UTF8_PDF, NULL, &doc) == QUANTAPDF_OK);
    CHECK(quantapdf_page_count(doc, &pages) == QUANTAPDF_OK);
    CHECK(pages == 1);
    quantapdf_close(doc);
}

int main(void)
{
    test_arguments_and_errors();
    test_repeated_lifecycle();
    test_handle_isolation();
    test_page_lifecycle();
    test_page_bounds();
    test_page_box_bounds();
    test_page_render();
    test_utf8_path();
    trace_step("close null");
    quantapdf_close(NULL);
    trace_step("complete");
    return EXIT_SUCCESS;
}
