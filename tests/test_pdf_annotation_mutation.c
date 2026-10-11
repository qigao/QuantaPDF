#include <quantapdf/quantapdf.h>
#include "pdf_edit_test_api.h"

#include <math.h>
#include <stddef.h>
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

static int close_float(float a, float b)
{
    float d = a - b;
    return (d < 0.0f ? -d : d) < 0.01f;
}

static void zero_ref(quantapdf_annotation_ref *ref)
{
    ref->opaque[0] = 0;
    ref->opaque[1] = 0;
}

static int ref_is_zero(const quantapdf_annotation_ref *ref)
{
    return ref->opaque[0] == 0 && ref->opaque[1] == 0;
}

static quantapdf_pdf_edit *begin_edit_path(const char *path, const char *password)
{
    quantapdf_document *source = NULL;
    quantapdf_pdf_edit *edit = NULL;

    CHECK(quantapdf_open(path, password, &source) == QUANTAPDF_OK);
    CHECK(source != NULL);
    CHECK(quantapdf_pdf_edit_begin(source, &edit) == QUANTAPDF_OK);
    CHECK(edit != NULL);
    quantapdf_close(source);
    return edit;
}

static quantapdf_annotation_ref get_ref(
    quantapdf_pdf_edit *edit,
    int page_index,
    size_t index)
{
    quantapdf_annotation_ref ref = {{0, 0}};

    CHECK(quantapdf_pdf_edit_annotation_ref_at(
              edit, page_index, index, &ref) == QUANTAPDF_OK);
    CHECK(!ref_is_zero(&ref));
    return ref;
}

static quantapdf_annotation_info get_live_info(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref)
{
    quantapdf_annotation_info info = {0};

    info.struct_size = sizeof(info);
    CHECK(quantapdf_pdf_edit_annotation_get_info(
              edit, ref, &info) == QUANTAPDF_OK);
    return info;
}

static void expect_live_info(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    quantapdf_annotation_type type,
    quantapdf_rect bounds,
    uint32_t flags)
{
    quantapdf_annotation_info info = get_live_info(edit, ref);

    CHECK(info.type == type);
    CHECK(close_float(info.bounds.x0, bounds.x0));
    CHECK(close_float(info.bounds.y0, bounds.y0));
    CHECK(close_float(info.bounds.x1, bounds.x1));
    CHECK(close_float(info.bounds.y1, bounds.y1));
    CHECK(info.flags == flags);
}

static void expect_live_contents(
    quantapdf_pdf_edit *edit,
    const quantapdf_annotation_ref *ref,
    int present,
    const char *expected,
    size_t expected_size)
{
    char *text = (char *)(uintptr_t)1;
    size_t size = (size_t)-1;

    CHECK(quantapdf_pdf_edit_annotation_contents(
              edit, ref, &text, &size) == QUANTAPDF_OK);

    if (!present) {
        CHECK(text == NULL);
        CHECK(size == 0);
        return;
    }

    CHECK(text != NULL);
    CHECK(size == expected_size);
    if (expected_size != 0)
        CHECK(memcmp(text, expected, expected_size) == 0);
    CHECK(text[size] == '\0');
    quantapdf_free(text);
}

static void save_output(const quantapdf_output *output, const char *path)
{
    CHECK(quantapdf_output_save_file(output, path) == QUANTAPDF_OK);
}

static void expect_snapshot_annotation(
    const char *path,
    int page_index,
    size_t index,
    quantapdf_annotation_type type,
    quantapdf_rect bounds,
    uint32_t flags,
    int contents_present,
    const char *contents,
    size_t contents_size)
{
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_annotation_page *annotations = NULL;
    quantapdf_annotation_info info = {0};
    const char *text = (const char *)(uintptr_t)1;
    size_t size = (size_t)-1;

    CHECK(quantapdf_open(path, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, page_index, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_annotations(page, &annotations) == QUANTAPDF_OK);

    info.struct_size = sizeof(info);
    CHECK(quantapdf_annotation_get_info(annotations, index, &info) ==
          QUANTAPDF_OK);
    CHECK(info.type == type);
    CHECK(close_float(info.bounds.x0, bounds.x0));
    CHECK(close_float(info.bounds.y0, bounds.y0));
    CHECK(close_float(info.bounds.x1, bounds.x1));
    CHECK(close_float(info.bounds.y1, bounds.y1));
    CHECK(info.flags == flags);

    CHECK(quantapdf_annotation_contents(
              annotations, index, &text, &size) == QUANTAPDF_OK);
    if (!contents_present) {
        CHECK(text == NULL);
        CHECK(size == 0);
    } else {
        CHECK(text != NULL);
        CHECK(size == contents_size);
        if (contents_size != 0)
            CHECK(memcmp(text, contents, contents_size) == 0);
        CHECK(text[size] == '\0');
    }

    quantapdf_drop_annotation_page(annotations);
    quantapdf_drop_page(page);
    quantapdf_close(document);
}

static void expect_metadata_title(const char *path, const char *expected)
{
    quantapdf_document *document = NULL;
    char *text = NULL;
    size_t size = 0;
    size_t expected_size = strlen(expected);

    CHECK(quantapdf_open(path, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_document_metadata(
              document, QUANTAPDF_METADATA_TITLE, &text, &size) ==
          QUANTAPDF_OK);
    CHECK(text != NULL);
    CHECK(size == expected_size);
    CHECK(memcmp(text, expected, size) == 0);
    CHECK(text[size] == '\0');
    quantapdf_free(text);
    quantapdf_close(document);
}

static quantapdf_annotation_ref create_rect_annot(
    quantapdf_pdf_edit *edit,
    int page_index,
    quantapdf_annotation_type type,
    quantapdf_rect bounds,
    uint32_t flags,
    const char *contents,
    size_t contents_size)
{
    quantapdf_annotation_create_options options = {0};
    quantapdf_annotation_ref ref = {{0, 0}};

    options.struct_size = sizeof(options);
    options.type = type;
    options.bounds = bounds;
    options.flags = flags;
    options.contents_utf8 = contents;
    options.contents_size = contents_size;

    CHECK(quantapdf_pdf_edit_annotation_create(
              edit, page_index, &options, &ref) == QUANTAPDF_OK);
    CHECK(!ref_is_zero(&ref));
    return ref;
}

static void test_arguments(void)
{
    int sentinel = 0;
    quantapdf_pdf_edit *edit = (quantapdf_pdf_edit *)&sentinel;
    quantapdf_annotation_ref ref = {{UINT64_MAX, UINT64_MAX}};
    quantapdf_annotation_info info = {0};
    quantapdf_output *output = (quantapdf_output *)&sentinel;
    char *text = (char *)(uintptr_t)1;
    size_t size = 99;
    size_t count = 99;
    quantapdf_document *source = NULL;
    quantapdf_annotation_ref live_ref;
    quantapdf_annotation_create_options create_small = {0};
    quantapdf_annotation_update update_small = {0};
    quantapdf_annotation_update bad_bits = {0};
    struct create_larger {
        quantapdf_annotation_create_options v1;
        uint64_t future;
    } create_big = {{0}, 0};
    struct update_larger {
        quantapdf_annotation_update v1;
        uint64_t future;
    } update_big = {{0}, 0};

    CHECK(quantapdf_pdf_edit_begin(NULL, &edit) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(edit == NULL);
    CHECK(quantapdf_pdf_edit_begin(NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_pdf_edit_annotation_count(NULL, 0, &count) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(count == 0);
    CHECK(quantapdf_pdf_edit_annotation_count(NULL, 0, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_pdf_edit_annotation_ref_at(NULL, 0, 0, &ref) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(ref_is_zero(&ref));
    CHECK(quantapdf_pdf_edit_annotation_ref_at(NULL, 0, 0, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    info.struct_size = offsetof(quantapdf_annotation_info, flags);
    CHECK(quantapdf_pdf_edit_annotation_get_info(NULL, &ref, &info) ==
          QUANTAPDF_ERROR_ARGUMENT);

    text = (char *)(uintptr_t)1;
    size = 99;
    CHECK(quantapdf_pdf_edit_annotation_contents(
              NULL, &ref, &text, &size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);
    CHECK(size == 0);

    text = (char *)(uintptr_t)1;
    CHECK(quantapdf_pdf_edit_annotation_contents(
              NULL, &ref, &text, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);

    size = 99;
    CHECK(quantapdf_pdf_edit_annotation_contents(
              NULL, &ref, NULL, &size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(size == 0);

    output = (quantapdf_output *)&sentinel;
    CHECK(quantapdf_pdf_edit_snapshot(NULL, &output) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(output == NULL);
    CHECK(quantapdf_pdf_edit_snapshot(NULL, NULL) ==
          QUANTAPDF_ERROR_ARGUMENT);

    quantapdf_drop_pdf_edit(NULL);
    CHECK(strcmp(quantapdf_status_string(QUANTAPDF_ERROR_STATE),
                 "invalid state") == 0);

    CHECK(quantapdf_open(MUTATION_PDF, NULL, &source) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_begin(source, &edit) == QUANTAPDF_OK);
    quantapdf_close(source);
    source = NULL;
    live_ref = get_ref(edit, 0, 1);

    create_small.struct_size =
        offsetof(quantapdf_annotation_create_options, contents_size);
    zero_ref(&ref);
    CHECK(quantapdf_pdf_edit_annotation_create(
              edit, 0, &create_small, &ref) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(ref_is_zero(&ref));

    update_small.struct_size =
        offsetof(quantapdf_annotation_update, contents_size);
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &live_ref, &update_small) == QUANTAPDF_ERROR_ARGUMENT);

    create_big.v1.struct_size = sizeof(create_big);
    create_big.v1.type = QUANTAPDF_ANNOTATION_TEXT;
    create_big.v1.bounds = (quantapdf_rect){150, 150, 170, 170};
    create_big.v1.flags = 0;
    create_big.v1.contents_utf8 = NULL;
    create_big.v1.contents_size = 0;
    zero_ref(&ref);
    CHECK(quantapdf_pdf_edit_annotation_create(
              edit, 0, &create_big.v1, &ref) == QUANTAPDF_OK);
    CHECK(!ref_is_zero(&ref));

    update_big.v1.struct_size = sizeof(update_big);
    update_big.v1.fields = 0;
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &live_ref, &update_big.v1) == QUANTAPDF_OK);

    bad_bits.struct_size = sizeof(bad_bits);
    bad_bits.fields = UINT32_C(0x80000000);
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &live_ref, &bad_bits) == QUANTAPDF_ERROR_ARGUMENT);

    count = 99;
    CHECK(quantapdf_pdf_edit_annotation_count(edit, -1, &count) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(count == 0);
    count = 99;
    CHECK(quantapdf_pdf_edit_annotation_count(edit, 2, &count) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(count == 0);

    ref.opaque[0] = UINT64_MAX;
    ref.opaque[1] = UINT64_MAX;
    CHECK(quantapdf_pdf_edit_annotation_ref_at(edit, -1, 0, &ref) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(ref_is_zero(&ref));
    ref.opaque[0] = UINT64_MAX;
    ref.opaque[1] = UINT64_MAX;
    CHECK(quantapdf_pdf_edit_annotation_ref_at(edit, 0, 99, &ref) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(ref_is_zero(&ref));

    quantapdf_drop_pdf_edit(edit);
}

static void test_begin_lifetime_and_fail_closed(void)
{
    quantapdf_document *source = NULL;
    quantapdf_pdf_edit *edit = NULL;
    quantapdf_output *output = NULL;

    CHECK(quantapdf_open(MUTATION_PDF, NULL, &source) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_begin(source, &edit) == QUANTAPDF_OK);
    CHECK(edit != NULL);
    quantapdf_close(source);
    source = NULL;
    CHECK(quantapdf_pdf_edit_snapshot(edit, &output) == QUANTAPDF_OK);
    CHECK(output != NULL);
    quantapdf_drop_output(output);
    quantapdf_drop_pdf_edit(edit);

    CHECK(quantapdf_open(ENCRYPTED_PDF, "user-pass", &source) ==
          QUANTAPDF_OK);
    edit = (quantapdf_pdf_edit *)(uintptr_t)1;
    CHECK(quantapdf_pdf_edit_begin(source, &edit) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(edit == NULL);
    quantapdf_close(source);

    CHECK(quantapdf_open(SIGNED_PDF, NULL, &source) == QUANTAPDF_OK);
    edit = (quantapdf_pdf_edit *)(uintptr_t)1;
    CHECK(quantapdf_pdf_edit_begin(source, &edit) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(edit == NULL);
    quantapdf_close(source);

    CHECK(quantapdf_open(UNSIGNED_SIGNATURE_PDF, NULL, &source) ==
          QUANTAPDF_OK);
    edit = NULL;
    CHECK(quantapdf_pdf_edit_begin(source, &edit) == QUANTAPDF_OK);
    CHECK(edit != NULL);
    quantapdf_drop_pdf_edit(edit);
    quantapdf_close(source);

    CHECK(quantapdf_open(NON_PDF, NULL, &source) == QUANTAPDF_ERROR_FORMAT);
    CHECK(source == NULL);
}

static void test_discovery_and_refs(void)
{
    quantapdf_pdf_edit *a = begin_edit_path(MUTATION_PDF, NULL);
    quantapdf_pdf_edit *b = begin_edit_path(MUTATION_PDF, NULL);
    quantapdf_annotation_ref text_ref = {{0, 0}};
    quantapdf_annotation_ref square_ref = {{0, 0}};
    quantapdf_annotation_ref square_again = {{0, 0}};
    quantapdf_annotation_ref invalid = {{UINT64_MAX, UINT64_MAX}};
    quantapdf_annotation_info info = {0};
    size_t count = 0;

    CHECK(quantapdf_pdf_edit_annotation_count(a, 0, &count) ==
          QUANTAPDF_OK);
    CHECK(count == 5);
    CHECK(quantapdf_pdf_edit_annotation_count(a, 1, &count) ==
          QUANTAPDF_OK);
    CHECK(count == 2);

    CHECK(quantapdf_pdf_edit_annotation_ref_at(a, 0, 0, &text_ref) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_annotation_ref_at(a, 0, 1, &square_ref) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_annotation_ref_at(a, 0, 1, &square_again) ==
          QUANTAPDF_OK);
    CHECK(memcmp(&square_ref, &square_again, sizeof(square_ref)) == 0);

    expect_live_info(
        a, &text_ref, QUANTAPDF_ANNOTATION_TEXT,
        (quantapdf_rect){10, 160, 30, 180}, UINT32_C(2147483649));
    expect_live_contents(a, &text_ref, 1, "text-a", 6);
    expect_live_info(
        a, &square_ref, QUANTAPDF_ANNOTATION_SQUARE,
        (quantapdf_rect){40, 120, 70, 150}, 4);
    expect_live_contents(a, &square_ref, 1, "square-b", 8);

    info.struct_size = sizeof(info);
    info.type = QUANTAPDF_ANNOTATION_CIRCLE;
    info.flags = UINT32_MAX;
    CHECK(quantapdf_pdf_edit_annotation_get_info(
              b, &square_ref, &info) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(info.type == QUANTAPDF_ANNOTATION_UNKNOWN);
    CHECK(info.flags == 0);

    CHECK(quantapdf_pdf_edit_annotation_ref_at(a, -1, 0, &invalid) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(ref_is_zero(&invalid));
    invalid.opaque[0] = UINT64_MAX;
    invalid.opaque[1] = UINT64_MAX;
    CHECK(quantapdf_pdf_edit_annotation_ref_at(a, 2, 0, &invalid) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(ref_is_zero(&invalid));
    invalid.opaque[0] = UINT64_MAX;
    invalid.opaque[1] = UINT64_MAX;
    CHECK(quantapdf_pdf_edit_annotation_ref_at(a, 0, 99, &invalid) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(ref_is_zero(&invalid));

    quantapdf_drop_pdf_edit(a);
    quantapdf_drop_pdf_edit(b);
}

static void test_malformed_discovery_is_atomic(void)
{
    quantapdf_pdf_edit *edit = begin_edit_path(LATE_MALFORMED_PDF, NULL);
    quantapdf_annotation_ref ref = {{UINT64_MAX, UINT64_MAX}};
    size_t count = 99;

    CHECK(quantapdf_pdf_edit_annotation_count(edit, 0, &count) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(count == 0);

    CHECK(quantapdf_pdf_edit_annotation_ref_at(edit, 0, 0, &ref) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(ref_is_zero(&ref));

    ref.opaque[0] = UINT64_MAX;
    ref.opaque[1] = UINT64_MAX;
    CHECK(quantapdf_pdf_edit_annotation_ref_at(edit, 0, 0, &ref) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(ref_is_zero(&ref));

    quantapdf_drop_pdf_edit(edit);
}

static void test_create(void)
{
    quantapdf_pdf_edit *edit = begin_edit_path(MUTATION_PDF, NULL);
    quantapdf_annotation_ref text;
    quantapdf_annotation_ref free_text;
    quantapdf_annotation_ref square;
    quantapdf_annotation_ref circle;
    quantapdf_annotation_ref ref = {{0, 0}};
    quantapdf_annotation_create_options options = {0};
    size_t count = 0;

    text = create_rect_annot(
        edit, 0, QUANTAPDF_ANNOTATION_TEXT,
        (quantapdf_rect){20, 20, 35, 35}, 1,
        "new-text", sizeof("new-text") - 1);
    free_text = create_rect_annot(
        edit, 1, QUANTAPDF_ANNOTATION_FREE_TEXT,
        (quantapdf_rect){20, 20, 90, 55}, 4,
        "new-free", sizeof("new-free") - 1);
    square = create_rect_annot(
        edit, 1, QUANTAPDF_ANNOTATION_SQUARE,
        (quantapdf_rect){100, 20, 150, 70}, 8,
        NULL, 0);
    circle = create_rect_annot(
        edit, 1, QUANTAPDF_ANNOTATION_CIRCLE,
        (quantapdf_rect){20, 90, 70, 140}, 16,
        "", 0);

    expect_live_info(
        edit, &text, QUANTAPDF_ANNOTATION_TEXT,
        (quantapdf_rect){20, 20, 35, 35}, 1);
    expect_live_contents(edit, &text, 1, "new-text", 8);
    expect_live_info(
        edit, &free_text, QUANTAPDF_ANNOTATION_FREE_TEXT,
        (quantapdf_rect){20, 20, 90, 55}, 4);
    expect_live_contents(edit, &free_text, 1, "new-free", 8);
    expect_live_info(
        edit, &square, QUANTAPDF_ANNOTATION_SQUARE,
        (quantapdf_rect){100, 20, 150, 70}, 8);
    expect_live_contents(edit, &square, 0, NULL, 0);
    expect_live_info(
        edit, &circle, QUANTAPDF_ANNOTATION_CIRCLE,
        (quantapdf_rect){20, 90, 70, 140}, 16);
    expect_live_contents(edit, &circle, 1, "", 0);

    CHECK(quantapdf_pdf_edit_annotation_count(edit, 0, &count) ==
          QUANTAPDF_OK);
    CHECK(count == 6);
    CHECK(quantapdf_pdf_edit_annotation_count(edit, 1, &count) ==
          QUANTAPDF_OK);
    CHECK(count == 5);

    options.struct_size = sizeof(options);
    options.type = QUANTAPDF_ANNOTATION_HIGHLIGHT;
    options.bounds = (quantapdf_rect){20, 20, 50, 50};
    options.flags = 0;
    options.contents_utf8 = NULL;
    options.contents_size = 0;
    zero_ref(&ref);
    CHECK(quantapdf_pdf_edit_annotation_create(
              edit, 0, &options, &ref) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(ref_is_zero(&ref));

    options.type = QUANTAPDF_ANNOTATION_UNKNOWN;
    zero_ref(&ref);
    CHECK(quantapdf_pdf_edit_annotation_create(
              edit, 0, &options, &ref) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(ref_is_zero(&ref));

    options.type = QUANTAPDF_ANNOTATION_TEXT;
    options.bounds = (quantapdf_rect){50, 50, 20, 20};
    zero_ref(&ref);
    CHECK(quantapdf_pdf_edit_annotation_create(
              edit, 0, &options, &ref) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(ref_is_zero(&ref));

    options.bounds = (quantapdf_rect){20, 20, NAN, 50};
    zero_ref(&ref);
    CHECK(quantapdf_pdf_edit_annotation_create(
              edit, 0, &options, &ref) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(ref_is_zero(&ref));

    quantapdf_drop_pdf_edit(edit);
}

static void test_update_delete(void)
{
    quantapdf_pdf_edit *edit = begin_edit_path(MUTATION_PDF, NULL);
    quantapdf_pdf_edit *other = begin_edit_path(MUTATION_PDF, NULL);
    quantapdf_annotation_ref text_ref = get_ref(edit, 0, 0);
    quantapdf_annotation_ref square_ref = get_ref(edit, 0, 1);
    quantapdf_annotation_ref current_square;
    quantapdf_annotation_ref unknown_ref;
    quantapdf_annotation_ref highlight_ref;
    quantapdf_annotation_ref new_ref = {{0, 0}};
    quantapdf_annotation_info before = {0};
    quantapdf_annotation_info after = {0};
    quantapdf_annotation_update update = {0};
    quantapdf_annotation_update zero = {0};
    quantapdf_annotation_create_options options = {0};
    char *before_text = NULL;
    char *after_text = NULL;
    size_t before_size = 0;
    size_t after_size = 0;
    size_t before_count = 0;
    size_t after_count = 0;

    CHECK(quantapdf_pdf_edit_annotation_delete(edit, &text_ref) ==
          QUANTAPDF_OK);

    before.struct_size = sizeof(before);
    CHECK(quantapdf_pdf_edit_annotation_get_info(
              edit, &text_ref, &before) == QUANTAPDF_ERROR_STATE);
    CHECK(quantapdf_pdf_edit_annotation_contents(
              edit, &text_ref, &before_text, &before_size) ==
          QUANTAPDF_ERROR_STATE);
    CHECK(before_text == NULL);
    CHECK(before_size == 0);

    zero.struct_size = sizeof(zero);
    zero.fields = 0;
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &text_ref, &zero) == QUANTAPDF_ERROR_STATE);
    CHECK(quantapdf_pdf_edit_annotation_delete(edit, &text_ref) ==
          QUANTAPDF_ERROR_STATE);

    current_square = get_ref(edit, 0, 0);
    CHECK(memcmp(&square_ref, &current_square, sizeof(square_ref)) == 0);

    update.struct_size = sizeof(update);
    update.fields = QUANTAPDF_ANNOTATION_UPDATE_BOUNDS |
                    QUANTAPDF_ANNOTATION_UPDATE_FLAGS |
                    QUANTAPDF_ANNOTATION_UPDATE_CONTENTS;
    update.bounds = (quantapdf_rect){45, 115, 90, 155};
    update.flags = 33;
    update.contents_utf8 = "square-updated";
    update.contents_size = sizeof("square-updated") - 1;
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &square_ref, &update) == QUANTAPDF_OK);

    expect_live_info(
        edit, &square_ref, QUANTAPDF_ANNOTATION_SQUARE,
        (quantapdf_rect){45, 115, 90, 155}, 33);
    expect_live_contents(
        edit, &square_ref, 1, "square-updated",
        sizeof("square-updated") - 1);

    unknown_ref = get_ref(edit, 0, 1);
    highlight_ref = get_ref(edit, 0, 2);

    update.struct_size = sizeof(update);
    update.fields = QUANTAPDF_ANNOTATION_UPDATE_BOUNDS;
    update.bounds = (quantapdf_rect){20, 20, 100, 100};
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &highlight_ref, &update) ==
          QUANTAPDF_ERROR_UNSUPPORTED);

    update.struct_size = sizeof(update);
    update.fields = QUANTAPDF_ANNOTATION_UPDATE_FLAGS |
                    QUANTAPDF_ANNOTATION_UPDATE_CONTENTS;
    update.flags = 77;
    update.contents_utf8 = "highlight-updated";
    update.contents_size = sizeof("highlight-updated") - 1;
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &highlight_ref, &update) == QUANTAPDF_OK);
    expect_live_contents(
        edit, &highlight_ref, 1, "highlight-updated",
        sizeof("highlight-updated") - 1);

    zero.struct_size = sizeof(zero);
    zero.fields = 0;
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &unknown_ref, &zero) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &text_ref, &zero) == QUANTAPDF_ERROR_STATE);
    CHECK(quantapdf_pdf_edit_annotation_update(
              other, &square_ref, &zero) == QUANTAPDF_ERROR_ARGUMENT);

    before.struct_size = sizeof(before);
    CHECK(quantapdf_pdf_edit_annotation_get_info(
              edit, &square_ref, &before) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_annotation_contents(
              edit, &square_ref, &before_text, &before_size) == QUANTAPDF_OK);

    update.struct_size = sizeof(update);
    update.fields = QUANTAPDF_ANNOTATION_UPDATE_BOUNDS |
                    QUANTAPDF_ANNOTATION_UPDATE_CONTENTS;
    update.bounds = (quantapdf_rect){20, 20, 90, 90};
    update.contents_utf8 = "rollback-new";
    update.contents_size = sizeof("rollback-new") - 1;
    quantapdf_test_pdf_edit_set_fault(
        edit, QUANTAPDF_TEST_PDF_EDIT_FAULT_AFTER_FIRST_UPDATE_FIELD);
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &square_ref, &update) != QUANTAPDF_OK);
    quantapdf_test_pdf_edit_set_fault(
        edit, QUANTAPDF_TEST_PDF_EDIT_FAULT_NONE);

    after.struct_size = sizeof(after);
    CHECK(quantapdf_pdf_edit_annotation_get_info(
              edit, &square_ref, &after) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_annotation_contents(
              edit, &square_ref, &after_text, &after_size) == QUANTAPDF_OK);
    CHECK(close_float(before.bounds.x0, after.bounds.x0));
    CHECK(close_float(before.bounds.y0, after.bounds.y0));
    CHECK(close_float(before.bounds.x1, after.bounds.x1));
    CHECK(close_float(before.bounds.y1, after.bounds.y1));
    CHECK(before.flags == after.flags);
    CHECK(before_size == after_size);
    CHECK(before_size == 0 ||
          memcmp(before_text, after_text, before_size) == 0);
    quantapdf_free(before_text);
    quantapdf_free(after_text);
    before_text = NULL;
    after_text = NULL;

    CHECK(quantapdf_pdf_edit_annotation_count(
              edit, 0, &before_count) == QUANTAPDF_OK);
    options.struct_size = sizeof(options);
    options.type = QUANTAPDF_ANNOTATION_TEXT;
    options.bounds = (quantapdf_rect){120, 120, 150, 150};
    options.flags = 0;
    options.contents_utf8 = "rollback-create";
    options.contents_size = sizeof("rollback-create") - 1;

    quantapdf_test_pdf_edit_set_fault(
        edit, QUANTAPDF_TEST_PDF_EDIT_FAULT_AFTER_CREATE_MUTATION);
    zero_ref(&new_ref);
    CHECK(quantapdf_pdf_edit_annotation_create(
              edit, 0, &options, &new_ref) != QUANTAPDF_OK);
    quantapdf_test_pdf_edit_set_fault(
        edit, QUANTAPDF_TEST_PDF_EDIT_FAULT_NONE);
    CHECK(ref_is_zero(&new_ref));
    CHECK(quantapdf_pdf_edit_annotation_count(
              edit, 0, &after_count) == QUANTAPDF_OK);
    CHECK(after_count == before_count);

    CHECK(quantapdf_pdf_edit_annotation_create(
              edit, 0, &options, &new_ref) == QUANTAPDF_OK);
    CHECK(memcmp(&new_ref, &text_ref, sizeof(new_ref)) != 0);
    CHECK(quantapdf_pdf_edit_annotation_get_info(
              edit, &text_ref, &before) == QUANTAPDF_ERROR_STATE);
    expect_live_contents(edit, &new_ref, 1, "rollback-create",
        sizeof("rollback-create") - 1);

    quantapdf_drop_pdf_edit(edit);
    quantapdf_drop_pdf_edit(other);
}

static void test_contents_flags(void)
{
    static const char counted[3] = {'c', 'a', 't'};
    static const char bad_utf8[2] = {(char)0xC0, (char)0xAF};
    static const char embedded_nul[3] = {'a', '\0', 'b'};
    quantapdf_pdf_edit *edit = begin_edit_path(MUTATION_PDF, NULL);
    quantapdf_annotation_ref text_ref = get_ref(edit, 0, 0);
    quantapdf_annotation_info info = {0};
    quantapdf_annotation_update update = {0};
    quantapdf_output *output = NULL;
    char *old_copy = NULL;
    char *text = NULL;
    size_t old_size = 0;
    size_t size = 0;

    info.struct_size = sizeof(info);
    CHECK(quantapdf_pdf_edit_annotation_get_info(
              edit, &text_ref, &info) == QUANTAPDF_OK);
    CHECK(info.flags == UINT32_C(2147483649));

    update.struct_size = sizeof(update);
    update.fields = QUANTAPDF_ANNOTATION_UPDATE_FLAGS;
    update.flags = UINT32_MAX;
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &text_ref, &update) == QUANTAPDF_OK);

    info.struct_size = sizeof(info);
    CHECK(quantapdf_pdf_edit_annotation_get_info(
              edit, &text_ref, &info) == QUANTAPDF_OK);
    CHECK(info.flags == UINT32_MAX);

    update.struct_size = sizeof(update);
    update.fields = QUANTAPDF_ANNOTATION_UPDATE_CONTENTS;
    update.contents_utf8 = counted;
    update.contents_size = sizeof(counted);
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &text_ref, &update) == QUANTAPDF_OK);

    CHECK(quantapdf_pdf_edit_annotation_contents(
              edit, &text_ref, &old_copy, &old_size) == QUANTAPDF_OK);
    CHECK(old_copy != NULL);
    CHECK(old_size == 3);
    CHECK(memcmp(old_copy, "cat", 3) == 0);
    CHECK(old_copy[3] == '\0');

    update.contents_utf8 = "dog";
    update.contents_size = 3;
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &text_ref, &update) == QUANTAPDF_OK);
    CHECK(memcmp(old_copy, "cat", 3) == 0);
    CHECK(old_copy[3] == '\0');
    quantapdf_free(old_copy);
    old_copy = NULL;

    update.contents_utf8 = NULL;
    update.contents_size = 0;
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &text_ref, &update) == QUANTAPDF_OK);
    text = (char *)(uintptr_t)1;
    size = 99;
    CHECK(quantapdf_pdf_edit_annotation_contents(
              edit, &text_ref, &text, &size) == QUANTAPDF_OK);
    CHECK(text == NULL);
    CHECK(size == 0);

    update.contents_utf8 = "";
    update.contents_size = 0;
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &text_ref, &update) == QUANTAPDF_OK);
    expect_live_contents(edit, &text_ref, 1, "", 0);

    update.contents_utf8 = bad_utf8;
    update.contents_size = sizeof(bad_utf8);
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &text_ref, &update) == QUANTAPDF_ERROR_ARGUMENT);
    expect_live_contents(edit, &text_ref, 1, "", 0);

    update.contents_utf8 = embedded_nul;
    update.contents_size = sizeof(embedded_nul);
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &text_ref, &update) == QUANTAPDF_ERROR_ARGUMENT);
    expect_live_contents(edit, &text_ref, 1, "", 0);

    update.contents_utf8 = NULL;
    update.contents_size = 1;
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &text_ref, &update) == QUANTAPDF_ERROR_ARGUMENT);
    expect_live_contents(edit, &text_ref, 1, "", 0);

    CHECK(quantapdf_pdf_edit_snapshot(edit, &output) == QUANTAPDF_OK);
    save_output(output, OUTPUT_B_PDF);
    expect_snapshot_annotation(
        OUTPUT_B_PDF, 0, 0, QUANTAPDF_ANNOTATION_TEXT,
        (quantapdf_rect){10, 160, 30, 180}, UINT32_MAX,
        1, "", 0);
    quantapdf_drop_output(output);
    quantapdf_drop_pdf_edit(edit);
}

static void test_snapshot_isolation(void)
{
    quantapdf_document *source = NULL;
    quantapdf_page *source_page = NULL;
    quantapdf_annotation_page *source_annotations = NULL;
    quantapdf_pdf_edit *edit = NULL;
    quantapdf_annotation_ref text_ref;
    quantapdf_annotation_update update = {0};
    quantapdf_output *a = NULL;
    quantapdf_output *repeat = NULL;
    quantapdf_output *b = NULL;
    quantapdf_output *after_failed = NULL;
    quantapdf_output *failed = (quantapdf_output *)(uintptr_t)1;
    quantapdf_annotation_info source_info = {0};
    const char *source_text = NULL;
    size_t source_text_size = 0;
    const unsigned char *a_data = NULL;
    const unsigned char *repeat_data = NULL;
    const unsigned char *b_data = NULL;
    const unsigned char *after_failed_data = NULL;
    size_t a_size = 0;
    size_t repeat_size = 0;
    size_t b_size = 0;
    size_t after_failed_size = 0;
    unsigned char *a_copy = NULL;

    CHECK(quantapdf_open(MUTATION_PDF, NULL, &source) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(source, 0, &source_page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_annotations(
              source_page, &source_annotations) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_begin(source, &edit) == QUANTAPDF_OK);
    CHECK(edit != NULL);

    quantapdf_drop_page(source_page);
    source_page = NULL;
    quantapdf_close(source);
    source = NULL;

    source_info.struct_size = sizeof(source_info);
    CHECK(quantapdf_annotation_get_info(
              source_annotations, 0, &source_info) == QUANTAPDF_OK);
    CHECK(source_info.type == QUANTAPDF_ANNOTATION_TEXT);
    CHECK(close_float(source_info.bounds.x0, 10));
    CHECK(close_float(source_info.bounds.y0, 160));
    CHECK(close_float(source_info.bounds.x1, 30));
    CHECK(close_float(source_info.bounds.y1, 180));
    CHECK(source_info.flags == UINT32_C(2147483649));
    CHECK(quantapdf_annotation_contents(
              source_annotations, 0, &source_text, &source_text_size) ==
          QUANTAPDF_OK);
    CHECK(source_text != NULL);
    CHECK(source_text_size == 6);
    CHECK(memcmp(source_text, "text-a", 6) == 0);

    text_ref = get_ref(edit, 0, 0);

    CHECK(quantapdf_pdf_edit_snapshot(edit, &a) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_snapshot(edit, &repeat) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(a, &a_data, &a_size) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              repeat, &repeat_data, &repeat_size) == QUANTAPDF_OK);
    CHECK(a_size == repeat_size);
    CHECK(memcmp(a_data, repeat_data, a_size) == 0);

    a_copy = (unsigned char *)malloc(a_size);
    CHECK(a_copy != NULL);
    memcpy(a_copy, a_data, a_size);

    update.struct_size = sizeof(update);
    update.fields = QUANTAPDF_ANNOTATION_UPDATE_CONTENTS;
    update.contents_utf8 = "snapshot-b";
    update.contents_size = sizeof("snapshot-b") - 1;
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &text_ref, &update) == QUANTAPDF_OK);

    source_info.struct_size = sizeof(source_info);
    CHECK(quantapdf_annotation_get_info(
              source_annotations, 0, &source_info) == QUANTAPDF_OK);
    CHECK(source_info.flags == UINT32_C(2147483649));
    CHECK(quantapdf_annotation_contents(
              source_annotations, 0, &source_text, &source_text_size) ==
          QUANTAPDF_OK);
    CHECK(source_text_size == 6);
    CHECK(memcmp(source_text, "text-a", 6) == 0);

    CHECK(quantapdf_pdf_edit_snapshot(edit, &b) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(a, &a_data, &a_size) == QUANTAPDF_OK);
    CHECK(memcmp(a_data, a_copy, a_size) == 0);
    CHECK(quantapdf_output_data(b, &b_data, &b_size) == QUANTAPDF_OK);

    save_output(a, OUTPUT_A_PDF);
    save_output(b, OUTPUT_B_PDF);
    expect_snapshot_annotation(
        OUTPUT_A_PDF, 0, 0, QUANTAPDF_ANNOTATION_TEXT,
        (quantapdf_rect){10, 160, 30, 180}, UINT32_C(2147483649),
        1, "text-a", 6);
    expect_snapshot_annotation(
        OUTPUT_B_PDF, 0, 0, QUANTAPDF_ANNOTATION_TEXT,
        (quantapdf_rect){10, 160, 30, 180}, UINT32_C(2147483649),
        1, "snapshot-b", sizeof("snapshot-b") - 1);

    quantapdf_test_pdf_edit_set_fault(
        edit, QUANTAPDF_TEST_PDF_EDIT_FAULT_SNAPSHOT_BEFORE_PUBLISH);
    CHECK(quantapdf_pdf_edit_snapshot(edit, &failed) != QUANTAPDF_OK);
    CHECK(failed == NULL);
    quantapdf_test_pdf_edit_set_fault(
        edit, QUANTAPDF_TEST_PDF_EDIT_FAULT_NONE);

    update.contents_utf8 = "after-failed-snapshot";
    update.contents_size = sizeof("after-failed-snapshot") - 1;
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &text_ref, &update) == QUANTAPDF_OK);
    expect_live_contents(
        edit, &text_ref, 1, "after-failed-snapshot",
        sizeof("after-failed-snapshot") - 1);
    CHECK(quantapdf_pdf_edit_snapshot(edit, &after_failed) ==
          QUANTAPDF_OK);
    save_output(after_failed, OUTPUT_B_PDF);
    expect_snapshot_annotation(
        OUTPUT_B_PDF, 0, 0, QUANTAPDF_ANNOTATION_TEXT,
        (quantapdf_rect){10, 160, 30, 180}, UINT32_C(2147483649),
        1, "after-failed-snapshot", sizeof("after-failed-snapshot") - 1);

    quantapdf_drop_pdf_edit(edit);
    edit = NULL;

    CHECK(quantapdf_output_data(a, &a_data, &a_size) == QUANTAPDF_OK);
    CHECK(memcmp(a_data, a_copy, a_size) == 0);
    CHECK(quantapdf_output_data(b, &b_data, &b_size) == QUANTAPDF_OK);
    CHECK(quantapdf_output_data(
              after_failed, &after_failed_data, &after_failed_size) ==
          QUANTAPDF_OK);
    CHECK(b_size != 0);
    CHECK(after_failed_size != 0);

    free(a_copy);
    quantapdf_drop_output(a);
    quantapdf_drop_output(repeat);
    quantapdf_drop_output(b);
    quantapdf_drop_output(after_failed);
    quantapdf_drop_annotation_page(source_annotations);
}

static void test_javascript_disabled(void)
{
    quantapdf_document *source = NULL;
    quantapdf_pdf_edit *edit = NULL;
    quantapdf_annotation_ref text_ref;
    quantapdf_annotation_update update = {0};
    quantapdf_output *output = NULL;
    char *title = NULL;
    size_t title_size = 0;

    CHECK(quantapdf_open(JS_PDF, NULL, &source) == QUANTAPDF_OK);
    CHECK(quantapdf_document_metadata(
              source, QUANTAPDF_METADATA_TITLE, &title, &title_size) ==
          QUANTAPDF_OK);
    CHECK(title != NULL);
    CHECK(title_size == 4);
    CHECK(memcmp(title, "SAFE", 4) == 0);
    quantapdf_free(title);
    title = NULL;

    CHECK(quantapdf_pdf_edit_begin(source, &edit) == QUANTAPDF_OK);
    quantapdf_close(source);
    source = NULL;

    text_ref = get_ref(edit, 0, 0);
    update.struct_size = sizeof(update);
    update.fields = QUANTAPDF_ANNOTATION_UPDATE_CONTENTS;
    update.contents_utf8 = "js-mutated";
    update.contents_size = sizeof("js-mutated") - 1;
    CHECK(quantapdf_pdf_edit_annotation_update(
              edit, &text_ref, &update) == QUANTAPDF_OK);

    CHECK(quantapdf_pdf_edit_snapshot(edit, &output) == QUANTAPDF_OK);
    save_output(output, OUTPUT_JS_PDF);
    expect_metadata_title(OUTPUT_JS_PDF, "SAFE");

    quantapdf_drop_output(output);
    quantapdf_drop_pdf_edit(edit);
}

static int selected(int argc, char **argv, const char *name)
{
    return argc == 1 || (argc == 2 && strcmp(argv[1], name) == 0);
}

int main(int argc, char **argv)
{
    CHECK(argc <= 2);

    if (selected(argc, argv, "arguments"))
        test_arguments();
    if (selected(argc, argv, "begin"))
        test_begin_lifetime_and_fail_closed();
    if (selected(argc, argv, "discovery")) {
        test_discovery_and_refs();
        test_malformed_discovery_is_atomic();
    }
    if (selected(argc, argv, "create"))
        test_create();
    if (selected(argc, argv, "update-delete"))
        test_update_delete();
    if (selected(argc, argv, "contents-flags"))
        test_contents_flags();
    if (selected(argc, argv, "snapshot"))
        test_snapshot_isolation();
    if (selected(argc, argv, "javascript"))
        test_javascript_disabled();

    return EXIT_SUCCESS;
}
