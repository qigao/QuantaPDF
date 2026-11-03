#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static void test_decode_retained_occurrence(void)
{
    static const unsigned char expected[] = {
        0xff, 0x00, 0x00,
        0x00, 0xff, 0x00
    };
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_image_page *images = NULL;
    quantapdf_bitmap *bitmap = NULL;
    const unsigned char *data = NULL;
    size_t size = 0;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;

    CHECK(quantapdf_open(test_context(), PAGE_IMAGES_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_images(page, &images) == QUANTAPDF_OK);

    /* Decode must use the retained occurrence, not the source page. */
    quantapdf_drop_page(page);
    page = NULL;

    CHECK(quantapdf_image_render(images, 0, &bitmap) == QUANTAPDF_OK);
    CHECK(bitmap != NULL);

    /* The returned bitmap owns its decoded pixels independently. */
    quantapdf_drop_image_page(images);
    images = NULL;

    CHECK(quantapdf_bitmap_dimensions(
        bitmap,
        &width,
        &height,
        &stride,
        &components) == QUANTAPDF_OK);
    CHECK(width == 2);
    CHECK(height == 1);
    CHECK(stride == 6);
    CHECK(components == 3);

    CHECK(quantapdf_bitmap_data(bitmap, &data, &size) == QUANTAPDF_OK);
    CHECK(data != NULL);
    CHECK(size == sizeof(expected));
    CHECK(memcmp(data, expected, sizeof(expected)) == 0);

    quantapdf_drop_bitmap(bitmap);
    quantapdf_close(document);
}

static void test_decode_soft_mask(void)
{
    static const unsigned char expected[] = {
        0x80, 0x00, 0x00, 0x80
    };
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_image_page *images = NULL;
    quantapdf_bitmap *bitmap = NULL;
    quantapdf_image_info info = { sizeof(info) };
    const unsigned char *data = NULL;
    size_t count = 0;
    size_t size = 0;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;

    CHECK(quantapdf_open(test_context(), PAGE_IMAGES_ALPHA_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_images(page, &images) == QUANTAPDF_OK);
    quantapdf_drop_page(page);
    page = NULL;

    CHECK(quantapdf_image_count(images, &count) == QUANTAPDF_OK);
    CHECK(count == 1);
    CHECK(quantapdf_image_get_info(images, 0, &info) == QUANTAPDF_OK);
    CHECK(info.has_alpha == 1);

    CHECK(quantapdf_image_render(images, 0, &bitmap) == QUANTAPDF_OK);
    CHECK(bitmap != NULL);
    quantapdf_drop_image_page(images);
    images = NULL;

    CHECK(quantapdf_bitmap_dimensions(
        bitmap,
        &width,
        &height,
        &stride,
        &components) == QUANTAPDF_OK);
    CHECK(width == 1);
    CHECK(height == 1);
    CHECK(stride == 4);
    CHECK(components == 4);

    CHECK(quantapdf_bitmap_data(bitmap, &data, &size) == QUANTAPDF_OK);
    CHECK(size == sizeof(expected));
    CHECK(memcmp(data, expected, sizeof(expected)) == 0);

    quantapdf_drop_bitmap(bitmap);
    quantapdf_close(document);
}

static void test_argument_contract(void)
{
    int sentinel = 0;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_image_page *images = NULL;
    quantapdf_bitmap *bitmap = (quantapdf_bitmap *)&sentinel;

    CHECK(quantapdf_image_render(NULL, 0, &bitmap) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    CHECK(quantapdf_image_render(NULL, 0, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_open(test_context(), PAGE_IMAGES_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_images(page, &images) == QUANTAPDF_OK);
    quantapdf_drop_page(page);

    bitmap = (quantapdf_bitmap *)&sentinel;
    CHECK(quantapdf_image_render(images, 99, &bitmap) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    CHECK(quantapdf_image_render(images, 0, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_drop_image_page(images);
    quantapdf_close(document);
}

int main(void)
{
    test_decode_retained_occurrence();
    test_decode_soft_mask();
    test_argument_contract();
    return EXIT_SUCCESS;
}
