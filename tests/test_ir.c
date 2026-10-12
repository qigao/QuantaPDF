#include "ir/ir.h"
#include "composer/composer_internal.h"
#include "core/context_internal.h"
#include "backend/qpdf_composer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                   \
                    __FILE__, __LINE__, #expr);                               \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int test_model_outlives_builder(void)
{
    quantapdf_context *context = NULL;
    quantapdf_composer *composer = NULL;
    quantapdf_output *reference = NULL;
    quantapdf_ir ir = {0};
    quantapdf_composer_page_options page_options = {0};
    quantapdf_composer_text_options text_options = {0};
    quantapdf_composer_image_options image_options = {0};
    quantapdf_composer_raster raster = {0};
    quantapdf_composer_image_id image_id = 0;
    quantapdf_rect text_bounds = {10, 10, 110, 35};
    quantapdf_rect image_bounds = {10, 60, 30, 80};
    unsigned char source_pixel[] = {0, 0, 255, 255};
    char source_text[] = "IR survives";
    const unsigned char *reference_data = NULL;
    unsigned char *expected = NULL;
    unsigned char *first = NULL;
    unsigned char *second = NULL;
    size_t expected_size = 0, first_size = 0, second_size = 0;
    size_t page_index = SIZE_MAX, finalizations = 0;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_bitmap *bitmap = NULL;
    const unsigned char *pixels = NULL;
    char *text = NULL;
    size_t text_size = 0, pixel_size = 0;
    int width = 0, height = 0, stride = 0, components = 0;
    FILE *file;

    CHECK(quantapdf_context_create(&context) == QUANTAPDF_OK);
    context->test_finalizations = &finalizations;
    CHECK(quantapdf_composer_create_with_context(context, NULL, &composer) ==
          QUANTAPDF_OK);
    quantapdf_drop_context(context);
    page_options.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page_options.width_points = 120;
    page_options.height_points = 100;
    page_options.background_argb = UINT32_C(0xffffffff);
    CHECK(quantapdf_composer_add_page(composer, &page_options, &page_index) ==
          QUANTAPDF_OK);
    CHECK(page_index == 0);
    text_options.struct_size = QUANTAPDF_COMPOSER_TEXT_OPTIONS_V1_SIZE;
    text_options.font = QUANTAPDF_COMPOSER_FONT_HELVETICA;
    text_options.font_size = 12;
    text_options.argb = UINT32_C(0xff000000);
    text_options.line_height_multiplier = 1;
    CHECK(quantapdf_composer_draw_text(composer, 0, source_text, &text_bounds,
                                     &text_options) == QUANTAPDF_OK);
    raster.struct_size = QUANTAPDF_COMPOSER_RASTER_V1_SIZE;
    raster.format = QUANTAPDF_COMPOSER_RASTER_RGBA32;
    raster.width = raster.height = 1;
    raster.stride = raster.size = sizeof(source_pixel);
    raster.pixels = source_pixel;
    CHECK(quantapdf_composer_add_raster(composer, &raster, &image_id) ==
          QUANTAPDF_OK);
    {
        quantapdf_composer_image_id reused = 0;
        CHECK(quantapdf_composer_add_raster(composer, &raster, &reused) ==
              QUANTAPDF_OK);
        CHECK(reused == image_id);
    }
    image_options.struct_size = QUANTAPDF_COMPOSER_IMAGE_OPTIONS_V1_SIZE;
    image_options.fit = QUANTAPDF_COMPOSER_IMAGE_FIT_STRETCH;
    CHECK(quantapdf_composer_draw_image(composer, 0, image_id, &image_bounds,
                                      &image_options) == QUANTAPDF_OK);
    memset(source_text, 'X', sizeof(source_text) - 1);
    memset(source_pixel, 0, sizeof(source_pixel));
    CHECK(quantapdf_composer_finish(composer, &reference) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(reference, &reference_data, &expected_size) ==
          QUANTAPDF_OK);
    CHECK(expected_size > 0);
    expected = (unsigned char *)malloc(expected_size);
    CHECK(expected != NULL);
    memcpy(expected, reference_data, expected_size);
    quantapdf_drop_output(reference);

    /* Transfer unique payload ownership; no shallow copy remains in builder. */
    ir = composer->ir;
    memset(&composer->ir, 0, sizeof(composer->ir));
    quantapdf_drop_composer(composer);
    CHECK(finalizations == 1);
    CHECK(ir.page_count == 1 && ir.operation_count == 2 && ir.resources.image_count == 1);
    /* Writer admission rejects an inconsistent model without publishing bytes.
     * Restoring the reference must leave deterministic export unchanged. */
    ir.operations[0].page_index = ir.page_count;
    first = (unsigned char *)(uintptr_t)1;
    first_size = SIZE_MAX;
    CHECK(quantapdf_qpdf_compose(&ir, &first, &first_size) == QUANTAPDF_ERROR_BACKEND);
    CHECK(first == NULL && first_size == 0);
    ir.operations[0].page_index = 0;
    CHECK(quantapdf_qpdf_compose(&ir, &first, &first_size) == QUANTAPDF_OK);
    CHECK(quantapdf_qpdf_compose(&ir, &second, &second_size) == QUANTAPDF_OK);
    CHECK(first_size == expected_size && second_size == first_size);
    CHECK(memcmp(first, expected, first_size) == 0);
    CHECK(memcmp(first, second, first_size) == 0);
    quantapdf_ir_dispose(&ir);
    CHECK(ir.pages == NULL && ir.operations == NULL && ir.resources.images == NULL);
    CHECK(ir.page_count == 0 && ir.operation_count == 0 && ir.resources.image_count == 0);
    quantapdf_ir_dispose(&ir);
    quantapdf_ir_dispose(NULL);
    free(expected);
    free(second);

    /* Observe the detached export through the public SDK after IR disposal. */
    file = fopen(IR_OUTPUT_PDF, "wb");
    CHECK(file != NULL);
    CHECK(fwrite(first, 1, first_size, file) == first_size);
    CHECK(fclose(file) == 0);
    free(first);
    CHECK(quantapdf_open(IR_OUTPUT_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_text(page, &text, &text_size) == QUANTAPDF_OK);
    CHECK(text_size >= strlen("IR survives"));
    CHECK(strstr(text, "IR survives") != NULL && strstr(text, "XXX") == NULL);
    quantapdf_free(text);
    CHECK(quantapdf_render_page(page, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride,
                                     &components) == QUANTAPDF_OK);
    CHECK(width == 120 && height == 100 && components == 3);
    CHECK(quantapdf_bitmap_data(bitmap, &pixels, &pixel_size) == QUANTAPDF_OK);
    CHECK(pixel_size >= (size_t)stride * (size_t)height);
    pixels += (size_t)70 * (size_t)stride + 20 * 3;
    CHECK(pixels[0] < 10 && pixels[1] < 10 && pixels[2] > 245);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    return 0;
}

int main(void)
{
    return test_model_outlives_builder();
}
