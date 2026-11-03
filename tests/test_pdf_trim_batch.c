#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>
#include "test_pdf_trim_internal.h"

#include <math.h>
#include <stdint.h>
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

static quantapdf_output *output_sentinel(void)
{
    return (quantapdf_output *)(uintptr_t)1;
}

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

static quantapdf_document *open_document(const char *path)
{
    quantapdf_document *document = NULL;

    CHECK(quantapdf_open(test_context(), path, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    return document;
}

static quantapdf_rect page_media(
    quantapdf_document *document,
    int page_index)
{
    quantapdf_page *page = NULL;
    quantapdf_rect bounds = {0};

    CHECK(quantapdf_load_page(document, page_index, &page) == QUANTAPDF_OK);
    CHECK(page != NULL);
    CHECK(quantapdf_page_box_bounds(
              page, QUANTAPDF_PAGE_BOX_MEDIA, &bounds) == QUANTAPDF_OK);
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

int trim_run_batch_tests(void)
{
    static const float changed0_raw[4] = {40, 30, 360, 270};
    static const float changed1_raw[4] = {20, 30, 380, 270};
    const char *output_path = "trim-batch-output.pdf";
    quantapdf_document *document = open_document(TRIM_INTERACTIVE_PDF);
    quantapdf_document *reopened = NULL;
    quantapdf_output *noop_a = NULL;
    quantapdf_output *noop_b = NULL;
    quantapdf_output *mixed = NULL;
    quantapdf_output *changed_a = NULL;
    quantapdf_output *changed_b = NULL;
    quantapdf_output *lifetime = NULL;
    quantapdf_output *failed = output_sentinel();
    const unsigned char *noop_a_data = NULL;
    const unsigned char *noop_b_data = NULL;
    const unsigned char *mixed_data = NULL;
    const unsigned char *changed_a_data = NULL;
    const unsigned char *changed_b_data = NULL;
    const unsigned char *lifetime_data = NULL;
    size_t noop_a_size = 0;
    size_t noop_b_size = 0;
    size_t mixed_size = 0;
    size_t changed_a_size = 0;
    size_t changed_b_size = 0;
    size_t lifetime_size = 0;
    quantapdf_rect source0 = page_media(document, 0);
    quantapdf_rect source1 = page_media(document, 1);
    quantapdf_page_trim noops[2];
    quantapdf_page_trim mixed_trims[2];
    quantapdf_page_trim changed[2];
    quantapdf_page_trim invalid[2];

    check_rect_close(source0, (quantapdf_rect){0, 0, 400, 300});
    check_rect_close(source1, source0);

    noops[0] = make_trim(0, source0);
    noops[1] = make_trim(1, source1);
    CHECK(quantapdf_trim_pages(document, noops, 2, &noop_a) == QUANTAPDF_OK);
    CHECK(quantapdf_trim_pages(document, noops, 2, &noop_b) == QUANTAPDF_OK);
    CHECK(noop_a != NULL && noop_b != NULL);
    CHECK(quantapdf_output_data(noop_a, &noop_a_data, &noop_a_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(noop_b, &noop_b_data, &noop_b_size) ==
          QUANTAPDF_OK);
    CHECK(noop_a_data != NULL && noop_a_size != 0);
    CHECK(noop_b_data != NULL && noop_b_size == noop_a_size);
    CHECK(memcmp(noop_a_data, noop_b_data, noop_a_size) == 0);
    CHECK(trim_raw_expect_local_mediabox(
              noop_a_data, noop_a_size, 0, 0, NULL));
    CHECK(trim_raw_expect_local_mediabox(
              noop_a_data, noop_a_size, 1, 0, NULL));

    mixed_trims[0] = noops[0];
    mixed_trims[1] = make_trim(
        1, (quantapdf_rect){20, 30, 380, 270});
    CHECK(quantapdf_trim_pages(document, mixed_trims, 2, &mixed) ==
          QUANTAPDF_OK);
    CHECK(mixed != NULL);
    CHECK(quantapdf_output_data(mixed, &mixed_data, &mixed_size) ==
          QUANTAPDF_OK);
    CHECK(trim_raw_expect_local_mediabox(
              mixed_data, mixed_size, 0, 0, NULL));
    CHECK(trim_raw_expect_local_mediabox(
              mixed_data, mixed_size, 1, 1, changed1_raw));
    CHECK(trim_raw_expect_preserved_graph(
              noop_a_data, noop_a_size, mixed_data, mixed_size));

    changed[0] = make_trim(
        0, (quantapdf_rect){40, 30, 360, 270});
    changed[1] = make_trim(
        1, (quantapdf_rect){20, 30, 380, 270});
    CHECK(quantapdf_trim_pages(document, changed, 2, &changed_a) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_trim_pages(document, changed, 2, &changed_b) ==
          QUANTAPDF_OK);
    CHECK(changed_a != NULL && changed_b != NULL);
    CHECK(quantapdf_output_data(
              changed_a, &changed_a_data, &changed_a_size) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              changed_b, &changed_b_data, &changed_b_size) == QUANTAPDF_OK);
    CHECK(changed_a_data != NULL && changed_a_size != 0);
    CHECK(changed_b_data != NULL && changed_b_size == changed_a_size);
    CHECK(memcmp(changed_a_data, changed_b_data, changed_a_size) == 0);
    CHECK(trim_raw_expect_local_mediabox(
              changed_a_data, changed_a_size, 0, 1, changed0_raw));
    CHECK(trim_raw_expect_local_mediabox(
              changed_a_data, changed_a_size, 1, 1, changed1_raw));
    CHECK(trim_raw_expect_preserved_graph(
              noop_a_data, noop_a_size, changed_a_data, changed_a_size));

    invalid[0] = changed[0];
    invalid[1] = noops[1];
    invalid[1].bounds.x1 = source1.x1 + 1.0f;
    failed = output_sentinel();
    CHECK(quantapdf_trim_pages(document, invalid, 2, &failed) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(failed == NULL);
    check_rect_close(page_media(document, 0), source0);
    check_rect_close(page_media(document, 1), source1);

    CHECK(quantapdf_trim_pages(document, changed, 2, &lifetime) ==
          QUANTAPDF_OK);
    CHECK(lifetime != NULL);
    quantapdf_close(document);
    document = NULL;
    CHECK(quantapdf_output_data(lifetime, &lifetime_data, &lifetime_size) ==
          QUANTAPDF_OK);
    CHECK(lifetime_data != NULL && lifetime_size != 0);
    CHECK(lifetime_size == changed_a_size);
    CHECK(memcmp(lifetime_data, changed_a_data, lifetime_size) == 0);

    (void)remove(output_path);
    CHECK(write_bytes(output_path, lifetime_data, lifetime_size));
    reopened = open_document(output_path);
    check_rect_close(
        page_media(reopened, 0), (quantapdf_rect){0, 0, 320, 240});
    check_rect_close(
        page_media(reopened, 1), (quantapdf_rect){0, 0, 360, 240});
    quantapdf_close(reopened);
    reopened = NULL;
    (void)remove(output_path);

    quantapdf_drop_output(lifetime);
    quantapdf_drop_output(changed_b);
    quantapdf_drop_output(changed_a);
    quantapdf_drop_output(mixed);
    quantapdf_drop_output(noop_b);
    quantapdf_drop_output(noop_a);
    return 1;
}
