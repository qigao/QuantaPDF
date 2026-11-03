#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>
#include <math.h>
#include <stddef.h>
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

static int close_float(float a, float b)
{
    float d = a - b;
    if (d < 0.0f)
        d = -d;
    return d < 0.01f;
}

static void quad_bounds(
    const quantapdf_quad *quad,
    float *x0,
    float *y0,
    float *x1,
    float *y1)
{
    const quantapdf_point points[4] = {
        quad->ul, quad->ur, quad->ll, quad->lr
    };
    size_t i;

    *x0 = *x1 = points[0].x;
    *y0 = *y1 = points[0].y;
    for (i = 1; i < 4; ++i) {
        if (points[i].x < *x0)
            *x0 = points[i].x;
        if (points[i].x > *x1)
            *x1 = points[i].x;
        if (points[i].y < *y0)
            *y0 = points[i].y;
        if (points[i].y > *y1)
            *y1 = points[i].y;
    }
}

static void check_zero_quad(const quantapdf_quad *quad)
{
    CHECK(quad->ul.x == 0.0f);
    CHECK(quad->ul.y == 0.0f);
    CHECK(quad->ur.x == 0.0f);
    CHECK(quad->ur.y == 0.0f);
    CHECK(quad->ll.x == 0.0f);
    CHECK(quad->ll.y == 0.0f);
    CHECK(quad->lr.x == 0.0f);
    CHECK(quad->lr.y == 0.0f);
}

static void check_info(
    const quantapdf_image_info *info,
    float expected_x0,
    float expected_y0,
    float expected_x1,
    float expected_y1)
{
    float x0;
    float y0;
    float x1;
    float y1;

    CHECK(info->pixel_width == 2);
    CHECK(info->pixel_height == 1);
    CHECK(info->components == 3);
    CHECK(info->bits_per_component == 8);
    CHECK(info->has_alpha == 0);

    quad_bounds(&info->quad, &x0, &y0, &x1, &y1);
    CHECK(close_float(x0, expected_x0));
    CHECK(close_float(y0, expected_y0));
    CHECK(close_float(x1, expected_x1));
    CHECK(close_float(y1, expected_y1));
}

static void test_occurrences_and_lifetime(void)
{
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_image_page *images = NULL;
    quantapdf_image_info info0 = { sizeof(info0) };
    quantapdf_image_info info1 = { sizeof(info1) };
    size_t count = 0;

    CHECK(quantapdf_open(test_context(), PAGE_IMAGES_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_images(page, &images) == QUANTAPDF_OK);
    CHECK(images != NULL);

    /* The image snapshot may outlive the page, but not the document. */
    quantapdf_drop_page(page);
    page = NULL;

    CHECK(quantapdf_image_count(images, &count) == QUANTAPDF_OK);
    CHECK(count == 2);

    CHECK(quantapdf_image_get_info(images, 0, &info0) == QUANTAPDF_OK);
    CHECK(quantapdf_image_get_info(images, 1, &info1) == QUANTAPDF_OK);

    check_info(&info0, 10.0f, 60.0f, 50.0f, 80.0f);
    check_info(&info1, 100.0f, 10.0f, 120.0f, 50.0f);

    quantapdf_drop_image_page(images);
    quantapdf_close(document);
}

static void test_argument_and_version_contract(void)
{
    int sentinel = 0;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_image_page *images = (quantapdf_image_page *)&sentinel;
    quantapdf_image_info invalid_info;
    quantapdf_image_info undersized;
    quantapdf_image_info minimum;
    size_t minimum_size = offsetof(quantapdf_image_info, has_alpha) + sizeof(minimum.has_alpha);
    size_t count = 99;
    size_t i;
    struct {
        quantapdf_image_info info;
        unsigned char tail[16];
    } oversized;

    CHECK(quantapdf_extract_images(NULL, &images) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(images == NULL);
    CHECK(quantapdf_extract_images(NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_image_count(NULL, &count) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(count == 0);
    CHECK(quantapdf_image_count(NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_image_get_info(NULL, 0, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_open(test_context(), PAGE_IMAGES_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_images(page, &images) == QUANTAPDF_OK);
    quantapdf_drop_page(page);
    page = NULL;

    memset(&invalid_info, 0x5a, sizeof(invalid_info));

    CHECK(quantapdf_image_get_info(images, 99, &invalid_info) == QUANTAPDF_ERROR_ARGUMENT);

    check_zero_quad(&invalid_info.quad);
    CHECK(invalid_info.pixel_width == 0);
    CHECK(invalid_info.pixel_height == 0);
    CHECK(invalid_info.components == 0);
    CHECK(invalid_info.bits_per_component == 0);
    CHECK(invalid_info.has_alpha == 0);

    memset(&undersized, 0x5a, sizeof(undersized));

    memset(&minimum, 0, sizeof(minimum));

    CHECK(quantapdf_image_get_info(images, 0, &minimum) == QUANTAPDF_OK);

    CHECK(minimum.pixel_width == 2);
    CHECK(minimum.pixel_height == 1);
    CHECK(minimum.components == 3);
    CHECK(minimum.bits_per_component == 8);
    CHECK(minimum.has_alpha == 0);

    memset(&oversized, 0xa5, sizeof(oversized));

    CHECK(quantapdf_image_get_info(images, 0, &oversized.info) == QUANTAPDF_OK);

    CHECK(oversized.info.pixel_width == 2);
    for (i = 0; i < sizeof(oversized.tail); ++i)
        CHECK(oversized.tail[i] == 0xa5);

    quantapdf_drop_image_page(images);
    quantapdf_close(document);
    quantapdf_drop_image_page(NULL);
}

static void test_empty_page(void)
{
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_image_page *images = NULL;
    size_t count = 99;

    CHECK(quantapdf_open(test_context(), ONE_PAGE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_images(page, &images) == QUANTAPDF_OK);
    CHECK(images != NULL);
    quantapdf_drop_page(page);

    CHECK(quantapdf_image_count(images, &count) == QUANTAPDF_OK);
    CHECK(count == 0);

    quantapdf_drop_image_page(images);
    quantapdf_close(document);
}

int main(void)
{
    test_occurrences_and_lifetime();
    test_argument_and_version_contract();
    test_empty_page();
    return EXIT_SUCCESS;
}
