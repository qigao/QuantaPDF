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

static void check_render_sample(quantapdf_page *page,
    const quantapdf_render_options *options, int expected_width, int expected_height,
    int red_x, int red_y)
{
    quantapdf_bitmap *first = NULL, *second = NULL;
    const unsigned char *data, *repeat, *pixel;
    size_t size, repeat_size;
    int width, height, stride, components;
    CHECK(quantapdf_render_page_with_options(page, options, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(first, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == expected_width && height == expected_height && components == 3);
    CHECK(quantapdf_bitmap_data(first, &data, &size) == QUANTAPDF_OK);
    pixel = data + (size_t)red_y * (size_t)stride + (size_t)red_x * 3u;
    if (pixel[0] != 255 || pixel[1] != 0 || pixel[2] != 0) {
        fprintf(stderr, "rotation=%g dpi=%g clip=%d sample=(%d,%d) RGB=(%u,%u,%u)\n",
            options->rotation_degrees, options->dpi, options->clip_enabled,
            red_x, red_y, pixel[0], pixel[1], pixel[2]);
    }
    CHECK(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0);
    pixel = data + 2u * (size_t)stride + 2u * 3u;
    CHECK(pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 255);
    CHECK(quantapdf_render_page_with_options(page, options, &second) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_data(second, &repeat, &repeat_size) == QUANTAPDF_OK);
    CHECK(size == repeat_size && memcmp(data, repeat, size) == 0);
    quantapdf_drop_bitmap(second);
    quantapdf_drop_bitmap(first);
}

static void test_pattern_geometry(void)
{
    quantapdf_composer *composer = NULL;
    quantapdf_output *output = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_composer_page_options page_options = {0};
    quantapdf_composer_path_options style = {0};
    quantapdf_composer_path_command path[5] = {0};
    quantapdf_render_options options = {sizeof(options), 72, 0, 0, {0}, 0};
    size_t page_index;
    page_options.struct_size = QUANTAPDF_COMPOSER_PAGE_OPTIONS_V1_SIZE;
    page_options.width_points = 72; page_options.height_points = 36;
    page_options.background_argb = UINT32_C(0xffffffff);
    style.struct_size = QUANTAPDF_COMPOSER_PATH_OPTIONS_V1_SIZE;
    style.fill = 1; style.fill_argb = UINT32_C(0xffff0000);
    style.stroke_argb = UINT32_C(0xff000000); style.stroke_width = 1;
    style.fill_rule = QUANTAPDF_COMPOSER_FILL_NONZERO;
    style.line_cap = QUANTAPDF_COMPOSER_LINE_CAP_BUTT;
    style.line_join = QUANTAPDF_COMPOSER_LINE_JOIN_MITER; style.miter_limit = 10;
    path[0].kind = QUANTAPDF_COMPOSER_PATH_MOVE_TO; path[0].point1 = (quantapdf_point){12, 6};
    path[1].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO; path[1].point1 = (quantapdf_point){36, 6};
    path[2].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO; path[2].point1 = (quantapdf_point){36, 18};
    path[3].kind = QUANTAPDF_COMPOSER_PATH_LINE_TO; path[3].point1 = (quantapdf_point){12, 18};
    path[4].kind = QUANTAPDF_COMPOSER_PATH_CLOSE;
    CHECK(quantapdf_composer_create(NULL, &composer) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_add_page(composer, &page_options, &page_index) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_draw_path(composer, page_index, path, 5, &style) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(output, RENDER_PATTERN_PDF) == QUANTAPDF_OK);
    quantapdf_drop_output(output); quantapdf_drop_composer(composer);
    CHECK(quantapdf_open(RENDER_PATTERN_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    check_render_sample(page, &options, 72, 36, 24, 12);
    options.rotation_degrees = 90;
    check_render_sample(page, &options, 36, 72, 24, 24);
    options.rotation_degrees = -90;
    check_render_sample(page, &options, 36, 72, 12, 48);
    options.rotation_degrees = 45;
    check_render_sample(page, &options, 77, 77, 34, 25);
    options.rotation_degrees = 90; options.dpi = 144;
    check_render_sample(page, &options, 72, 144, 48, 48);
    options.dpi = 72; options.clip_enabled = 1;
    options.clip = (quantapdf_rect){6, 0, 42, 24};
    check_render_sample(page, &options, 24, 36, 12, 18);
    {
        quantapdf_page_crop crop = {sizeof(crop), 0, {6, 3, 66, 33}};
        CHECK(quantapdf_crop_pages(document, &crop, 1, &output) == QUANTAPDF_OK);
        CHECK(quantapdf_output_save_file(output, RENDER_CROPPED_PATTERN_PDF) == QUANTAPDF_OK);
        quantapdf_drop_output(output);
    }
    quantapdf_drop_page(page); quantapdf_close(document);
    CHECK(quantapdf_open(RENDER_CROPPED_PATTERN_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    options.clip_enabled = 0;
    check_render_sample(page, &options, 30, 60, 21, 18);
    quantapdf_drop_page(page); quantapdf_close(document);
}

static void test_displayed_page_rotation(const char *filename)
{
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_bitmap *base = NULL, *rotated = NULL;
    quantapdf_render_options options = {sizeof(options), 72, 90, 0, {0}, 0};
    const unsigned char *original, *turned;
    size_t size;
    int width, height, stride, components, turn_width, turn_height, turn_stride, turn_components;
    int quadrant;
    CHECK(quantapdf_open(filename, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_render_page(page, &base) == QUANTAPDF_OK);
    CHECK(quantapdf_render_page_with_options(page, &options, &rotated) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(base, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(rotated, &turn_width, &turn_height, &turn_stride, &turn_components) == QUANTAPDF_OK);
    CHECK(turn_width == height && turn_height == width && components == 3 && turn_components == 3);
    CHECK(quantapdf_bitmap_data(base, &original, &size) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_data(rotated, &turned, &size) == QUANTAPDF_OK);
    /* Four distinct painted quadrants avoid anti-aliased text and boundary
     * samples. Requested rotation is relative to the displayed source page. */
    CHECK(original[(size_t)(height / 8) * stride + (size_t)(width / 8) * 3] !=
          original[(size_t)(7 * height / 8) * stride + (size_t)(7 * width / 8) * 3]);
    for (quadrant = 0; quadrant < 4; ++quadrant) {
        int x = (quadrant & 1) ? 7 * width / 8 : width / 8;
        int y = (quadrant & 2) ? 7 * height / 8 : height / 8;
        const unsigned char *a = original + (size_t)y * stride + (size_t)x * 3;
        const unsigned char *b = turned + (size_t)x * turn_stride + (size_t)(height - 1 - y) * 3;
        if (memcmp(a, b, 3) != 0)
            fprintf(stderr, "%s quadrant=%d base=(%u,%u,%u) rotated=(%u,%u,%u)\n",
                filename, quadrant, a[0], a[1], a[2], b[0], b[1], b[2]);
        CHECK(memcmp(a, b, 3) == 0);
    }
    quantapdf_drop_bitmap(rotated); quantapdf_drop_bitmap(base);
    quantapdf_drop_page(page); quantapdf_close(document);
}

int main(void)
{
    int sentinel = 0;
    quantapdf_document *doc = NULL;
    quantapdf_page *page = NULL;
    quantapdf_bitmap *bitmap = (quantapdf_bitmap *)&sentinel;
    quantapdf_render_options options = { sizeof(options), 144.0f, 0.0f, 0, { 0 } };
    const unsigned char *data = NULL;
    size_t data_size = 0;
    size_t i;
    int width = 0;
    int height = 0;
    int stride = 0;
    int components = 0;

    CHECK(quantapdf_open(ONE_PAGE_PDF, NULL, &doc) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(doc, 0, &page) == QUANTAPDF_OK);

    CHECK(quantapdf_render_page_with_options(NULL, &options, &bitmap) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    CHECK(quantapdf_render_page_with_options(page, NULL, &bitmap) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    CHECK(quantapdf_render_page_with_options(page, &options, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    options.struct_size = 0;
    bitmap = (quantapdf_bitmap *)&sentinel;
    CHECK(quantapdf_render_page_with_options(page, &options, &bitmap) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);

    options.struct_size = sizeof(options);
    options.dpi = 0.0f;
    bitmap = (quantapdf_bitmap *)&sentinel;
    CHECK(quantapdf_render_page_with_options(page, &options, &bitmap) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);

    options.dpi = 144.0f;
    CHECK(quantapdf_render_page_with_options(page, &options, &bitmap) == QUANTAPDF_OK);
    CHECK(bitmap != NULL);
    CHECK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 144);
    CHECK(height == 144);
    CHECK(stride == 144 * 3);
    CHECK(components == 3);
    quantapdf_drop_bitmap(bitmap);

    /* An older caller-provided options size must ignore a newer alpha field. */
    options.struct_size = offsetof(quantapdf_render_options, alpha);
    options.dpi = 72.0f;
    options.rotation_degrees = 0.0f;
    options.clip_enabled = 0;
    options.alpha = 1;
    bitmap = NULL;
    CHECK(quantapdf_render_page_with_options(page, &options, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 72);
    CHECK(height == 72);
    CHECK(stride == 72 * 3);
    CHECK(components == 3);
    quantapdf_drop_bitmap(bitmap);

    options.struct_size = sizeof(options);
    options.alpha = 1;
    bitmap = NULL;
    CHECK(quantapdf_render_page_with_options(page, &options, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 72);
    CHECK(height == 72);
    CHECK(stride == 72 * 4);
    CHECK(components == 4);
    CHECK(quantapdf_bitmap_data(bitmap, &data, &data_size) == QUANTAPDF_OK);
    CHECK(data_size == (size_t)72 * 72 * 4);
    for (i = 0; i < data_size; ++i)
        CHECK(data[i] == 0);
    quantapdf_drop_bitmap(bitmap);

    options.alpha = 2;
    bitmap = (quantapdf_bitmap *)&sentinel;
    CHECK(quantapdf_render_page_with_options(page, &options, &bitmap) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    options.alpha = 0;

    bitmap = (quantapdf_bitmap *)&sentinel;
    CHECK(quantapdf_render_thumbnail(NULL, 144, 144, &bitmap) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    CHECK(quantapdf_render_thumbnail(page, 0, 144, &bitmap) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    CHECK(quantapdf_render_thumbnail(page, 144, 0, &bitmap) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);
    CHECK(quantapdf_render_thumbnail(page, 144, 144, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    /* Thumbnail policy never upscales above the page's 72-DPI size. */
    CHECK(quantapdf_render_thumbnail(page, 144, 144, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 72);
    CHECK(height == 72);
    CHECK(stride == 72 * 3);
    CHECK(components == 3);
    quantapdf_drop_bitmap(bitmap);

    quantapdf_drop_page(page);
    quantapdf_close(doc);

    doc = NULL;
    page = NULL;
    bitmap = NULL;
    CHECK(quantapdf_open(PAGE_BOXES_PDF, NULL, &doc) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(doc, 0, &page) == QUANTAPDF_OK);

    /* A caller using the v1 struct size gets the new field's default behavior. */
    options.struct_size = offsetof(quantapdf_render_options, rotation_degrees);
    options.dpi = 72.0f;
    options.rotation_degrees = 90.0f;
    CHECK(quantapdf_render_page_with_options(page, &options, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 180);
    CHECK(height == 60);
    quantapdf_drop_bitmap(bitmap);

    options.struct_size = offsetof(quantapdf_render_options, clip_enabled);
    options.rotation_degrees = 90.0f;
    options.clip_enabled = 1;
    bitmap = NULL;
    CHECK(quantapdf_render_page_with_options(page, &options, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 60);
    CHECK(height == 180);
    quantapdf_drop_bitmap(bitmap);

    options.struct_size = sizeof(options);
    options.rotation_degrees = 90.0f;
    options.clip_enabled = 0;
    options.alpha = 0;
    bitmap = NULL;
    CHECK(quantapdf_render_page_with_options(page, &options, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 60);
    CHECK(height == 180);
    CHECK(stride == 60 * 3);
    CHECK(components == 3);
    quantapdf_drop_bitmap(bitmap);

    options.rotation_degrees = NAN;
    bitmap = (quantapdf_bitmap *)&sentinel;
    CHECK(quantapdf_render_page_with_options(page, &options, &bitmap) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);

    options.rotation_degrees = 0.0f;
    options.clip_enabled = 1;
    options.clip.x0 = 20.0f;
    options.clip.y0 = 10.0f;
    options.clip.x1 = 80.0f;
    options.clip.y1 = 40.0f;
    bitmap = NULL;
    CHECK(quantapdf_render_page_with_options(page, &options, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 60);
    CHECK(height == 30);
    CHECK(stride == 60 * 3);
    CHECK(components == 3);
    quantapdf_drop_bitmap(bitmap);

    options.alpha = 1;
    bitmap = NULL;
    CHECK(quantapdf_render_page_with_options(page, &options, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 60);
    CHECK(height == 30);
    CHECK(stride == 60 * 4);
    CHECK(components == 4);
    CHECK(quantapdf_bitmap_data(bitmap, &data, &data_size) == QUANTAPDF_OK);
    CHECK(data_size == (size_t)60 * 30 * 4);
    for (i = 0; i < data_size; ++i)
        CHECK(data[i] == 0);
    quantapdf_drop_bitmap(bitmap);
    options.alpha = 0;

    options.rotation_degrees = 90.0f;
    bitmap = NULL;
    CHECK(quantapdf_render_page_with_options(page, &options, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 30);
    CHECK(height == 60);
    quantapdf_drop_bitmap(bitmap);

    options.rotation_degrees = 0.0f;
    options.clip.x1 = options.clip.x0;
    bitmap = (quantapdf_bitmap *)&sentinel;
    CHECK(quantapdf_render_page_with_options(page, &options, &bitmap) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);

    options.clip.x1 = 80.0f;
    options.clip.y0 = NAN;
    bitmap = (quantapdf_bitmap *)&sentinel;
    CHECK(quantapdf_render_page_with_options(page, &options, &bitmap) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bitmap == NULL);

    /* Fit within the requested pixel box while preserving aspect ratio. */
    bitmap = NULL;
    CHECK(quantapdf_render_thumbnail(page, 90, 90, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 90);
    CHECK(height == 30);
    CHECK(stride == 90 * 3);
    CHECK(components == 3);
    quantapdf_drop_bitmap(bitmap);

    bitmap = NULL;
    CHECK(quantapdf_render_thumbnail(page, 100, 20, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components) == QUANTAPDF_OK);
    CHECK(width == 60);
    CHECK(height == 20);
    CHECK(stride == 60 * 3);
    CHECK(components == 3);
    quantapdf_drop_bitmap(bitmap);

    quantapdf_drop_page(page);
    quantapdf_close(doc);
    test_pattern_geometry();
    test_displayed_page_rotation(RENDER_INTRINSIC_ROTATION_PDF);
    test_displayed_page_rotation(RENDER_USERUNIT_PDF);
    return EXIT_SUCCESS;
}
