#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

static int write_stale_file(
    const char *path,
    const unsigned char *data,
    size_t size)
{
    static const unsigned char stale_tail[] =
        "THIS-STALE-TRAILING-DATA-MUST-BE-TRUNCATED";
    FILE *file = fopen(path, "wb");

    if (file == NULL)
        return 0;
    if (size != 0 && fwrite(data, 1, size, file) != size) {
        fclose(file);
        return 0;
    }
    if (fwrite(stale_tail, 1, sizeof(stale_tail), file) != sizeof(stale_tail)) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static void expect_exact_file_bytes(
    const char *path,
    const unsigned char *expected,
    size_t expected_size)
{
    FILE *file = fopen(path, "rb");
    unsigned char *actual = NULL;
    int next;

    CHECK(file != NULL);
    CHECK(expected_size > 0);

    actual = (unsigned char *)malloc(expected_size);
    CHECK(actual != NULL);
    CHECK(fread(actual, 1, expected_size, file) == expected_size);
    CHECK(memcmp(actual, expected, expected_size) == 0);

    next = fgetc(file);
    CHECK(next == EOF);
    CHECK(feof(file) != 0);
    CHECK(ferror(file) == 0);
    CHECK(fclose(file) == 0);
    free(actual);
}

static void expect_page(
    quantapdf_document *document,
    int page_index,
    const char *needle,
    float width,
    float height)
{
    quantapdf_page *page = NULL;
    quantapdf_rect bounds;
    char *text = NULL;
    size_t text_size = 0;

    CHECK(quantapdf_load_page(document, page_index, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_page_bounds(page, &bounds) == QUANTAPDF_OK);
    CHECK(bounds.x0 == 0.0f);
    CHECK(bounds.y0 == 0.0f);
    CHECK(bounds.x1 == width);
    CHECK(bounds.y1 == height);
    CHECK(quantapdf_extract_text(page, &text, &text_size) == QUANTAPDF_OK);
    CHECK(text != NULL);
    CHECK(strstr(text, needle) != NULL);

    quantapdf_free(text);
    quantapdf_drop_page(page);
}

static void expect_saved_pdf(
    const char *path,
    const char *first_text,
    float first_width,
    float first_height,
    const char *second_text,
    float second_width,
    float second_height)
{
    quantapdf_document *document = NULL;
    int page_count = 0;

    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_page_count(document, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 2);
    expect_page(document, 0, first_text, first_width, first_height);
    expect_page(document, 1, second_text, second_width, second_height);
    quantapdf_close(document);
}

static void remove_missing_parent_if_empty(void)
{
    (void)remove(MISSING_PARENT_PDF);
#ifdef _WIN32
    (void)_rmdir(MISSING_PARENT_DIR);
#else
    (void)rmdir(MISSING_PARENT_DIR);
#endif
}

int main(void)
{
    quantapdf_document *source = NULL;
    quantapdf_output *output = NULL;
    const unsigned char *before_data = NULL;
    const unsigned char *after_data = NULL;
    size_t before_size = 0;
    size_t after_size = 0;
    int indices[] = {2, 0};

    (void)remove(ASCII_OUTPUT_PDF);
    remove_missing_parent_if_empty();

    CHECK(quantapdf_output_save_file(NULL, ASCII_OUTPUT_PDF) ==
          QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_open(test_context(), COMPOSITION_PDF, NULL, &source) == QUANTAPDF_OK);
    CHECK(quantapdf_export_pages(source, indices, 2, &output) == QUANTAPDF_OK);
    CHECK(output != NULL);
    quantapdf_close(source);
    source = NULL;

    CHECK(quantapdf_output_data(output, &before_data, &before_size) ==
          QUANTAPDF_OK);
    CHECK(before_data != NULL);
    CHECK(before_size > 0);

    CHECK(quantapdf_output_save_file(output, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_output_save_file(output, "") ==
          QUANTAPDF_ERROR_ARGUMENT);

    CHECK(write_stale_file(ASCII_OUTPUT_PDF, before_data, before_size));
    CHECK(quantapdf_output_save_file(output, ASCII_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    expect_exact_file_bytes(ASCII_OUTPUT_PDF, before_data, before_size);
    expect_saved_pdf(
        ASCII_OUTPUT_PDF,
        "PAGE-C", 300.0f, 150.0f,
        "PAGE-A", 200.0f, 200.0f);

    CHECK(quantapdf_output_save_file(output, UTF8_OUTPUT_PDF) ==
          QUANTAPDF_OK);
    expect_saved_pdf(
        UTF8_OUTPUT_PDF,
        "PAGE-C", 300.0f, 150.0f,
        "PAGE-A", 200.0f, 200.0f);

    remove_missing_parent_if_empty();
    CHECK(quantapdf_output_save_file(output, MISSING_PARENT_PDF) ==
          QUANTAPDF_ERROR_IO);

#ifdef _WIN32
    {
        const char invalid_utf8[] = { (char)0xC3, (char)0x28, '\0' };
        CHECK(quantapdf_output_save_file(output, invalid_utf8) ==
              QUANTAPDF_ERROR_ARGUMENT);
    }
#endif

    CHECK(quantapdf_output_data(output, &after_data, &after_size) ==
          QUANTAPDF_OK);
    CHECK(after_data != NULL);
    CHECK(after_size == before_size);
    CHECK(memcmp(after_data, before_data, before_size) == 0);

    quantapdf_drop_output(output);
    (void)remove(ASCII_OUTPUT_PDF);
    return EXIT_SUCCESS;
}
