#include "document/document_internal.h"
#include "core/bitmap_internal.h"
#include "core/output_internal.h"
#include "composer/composer_internal.h"
#include "snapshot/text_snapshot.h"
#include "snapshot/image_snapshot.h"
#include "snapshot/link_snapshot.h"
#include "snapshot/annotation_snapshot.h"
#include "snapshot/outline_snapshot.h"
#include "document/pdf_edit_internal.h"
#include "snapshot/form_snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int ok, const char *expression, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(x) check_impl((x), #x, __LINE__)
#define SENTINEL(type) ((type *)(uintptr_t)1)

static void check_pdf(const quantapdf_output *output)
{
    const unsigned char *data = NULL;
    size_t size = 0;
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(size > 5 && memcmp(data, "%PDF-", 5) == 0);
}

static void add_page(quantapdf_composer *composer)
{
    quantapdf_composer_page_options options = {0};
    size_t index = SIZE_MAX;
    options.struct_size = sizeof(options);
    options.width_points = options.height_points = 72.0f;
    options.background_argb = UINT32_C(0xffffffff);
    CHECK(quantapdf_composer_add_page(composer, &options, &index) == QUANTAPDF_OK);
}

static void test_arguments_and_failed_open(void)
{
    quantapdf_context *context = SENTINEL(quantapdf_context);
    quantapdf_document *document = SENTINEL(quantapdf_document);
    quantapdf_composer *composer = SENTINEL(quantapdf_composer);
    quantapdf_output *output = SENTINEL(quantapdf_output);
    size_t finalized = 0;
    CHECK(quantapdf_context_create(NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_context_create(&context) == QUANTAPDF_OK);
    CHECK(context->references == 1);
    context->test_finalizations = &finalized;
    CHECK(quantapdf_open_with_context(NULL, TEXT_PDF, NULL, &document) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(document == NULL);
    CHECK(quantapdf_composer_create_with_context(NULL, NULL, &composer) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(composer == NULL);
    CHECK(quantapdf_merge_outputs_with_context(NULL, NULL, 0, &output) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(output == NULL);
    CHECK(quantapdf_open_with_context(context, TEXT_PDF, NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_composer_create_with_context(context, NULL, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_merge_outputs_with_context(context, NULL, 0, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_open_with_context(context, NON_PDF, NULL, &document) == QUANTAPDF_ERROR_FORMAT);
    CHECK(document == NULL && context->references == 1);
    CHECK(quantapdf_open_with_context(context, "", NULL, &document) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(document == NULL && context->references == 1);
    quantapdf_drop_context(context);
    CHECK(finalized == 1);
    quantapdf_drop_context(NULL);
}

static void test_shared_domain_and_detached_results(void)
{
    quantapdf_context *context = NULL;
    quantapdf_document *document = NULL, *other = NULL;
    quantapdf_page *page = NULL;
    quantapdf_bitmap *bitmap = NULL;
    quantapdf_text_page *text = NULL;
    quantapdf_link_page *links = NULL;
    quantapdf_annotation_page *annotations = NULL;
    quantapdf_outline *outline = NULL;
    quantapdf_form *form = NULL, *edit_form = NULL;
    quantapdf_pdf_edit *edit = NULL;
    quantapdf_output *output = NULL;
    char *raw = NULL;
    size_t size = 0, count = 0, finalized = 0;
    int pages = 0;
    CHECK(quantapdf_context_create(&context) == QUANTAPDF_OK);
    context->test_finalizations = &finalized;
    CHECK(quantapdf_open_with_context(context, TEXT_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_open_with_context(context, TEXT_PDF, NULL, &other) == QUANTAPDF_OK);
    CHECK(context->references == 3);
    quantapdf_drop_context(context);
    CHECK(document->context == other->context && document->context->references == 2);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_render_page(page, &bitmap) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_text(page, &raw, &size) == QUANTAPDF_OK && size > 0);
    CHECK(quantapdf_extract_structured_text(page, &text) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_links(page, &links) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_annotations(page, &annotations) == QUANTAPDF_OK);
    CHECK(quantapdf_document_outline(document, &outline) == QUANTAPDF_OK);
    CHECK(quantapdf_document_form(document, &form) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_begin(document, &edit) == QUANTAPDF_OK);
    CHECK(page->context == document->context && bitmap->context == page->context);
    CHECK(text->context == page->context && links->context == page->context);
    CHECK(annotations->context == page->context && outline->context == page->context);
    CHECK(form->context == page->context && edit->context == page->context);
    quantapdf_drop_page(page);
    quantapdf_drop_bitmap(bitmap);
    quantapdf_close(document);
    CHECK(quantapdf_page_count(other, &pages) == QUANTAPDF_OK && pages == 1);
    quantapdf_close(other);
    CHECK(finalized == 0);
    CHECK(quantapdf_text_block_count(text, &count) == QUANTAPDF_OK && count > 0);
    CHECK(quantapdf_link_count(links, &count) == QUANTAPDF_OK);
    CHECK(quantapdf_annotation_count(annotations, &count) == QUANTAPDF_OK);
    CHECK(quantapdf_outline_count(outline, &count) == QUANTAPDF_OK);
    CHECK(quantapdf_form_field_count(form, &count) == QUANTAPDF_OK);
    CHECK(quantapdf_pdf_edit_form_snapshot(edit, &edit_form) == QUANTAPDF_OK);
    CHECK(edit_form->context == edit->context);
    CHECK(quantapdf_pdf_edit_snapshot(edit, &output) == QUANTAPDF_OK);
    CHECK(output->context == edit->context);
    quantapdf_drop_pdf_edit(edit);
    quantapdf_drop_text_page(text);
    quantapdf_drop_link_page(links);
    quantapdf_drop_annotation_page(annotations);
    quantapdf_drop_outline(outline);
    quantapdf_drop_form(form);
    quantapdf_drop_form(edit_form);
    CHECK(finalized == 0 && output->context->references == 1);
    check_pdf(output);
    quantapdf_drop_output(output);
    CHECK(finalized == 1);
    CHECK(raw[size] == '\0' && strlen(raw) > 0);
    quantapdf_free(raw);
}

static void test_image_domain(void)
{
    quantapdf_context *context = NULL;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    quantapdf_image_page *images = NULL;
    quantapdf_bitmap *bitmap = NULL;
    size_t finalized = 0, count = 0;
    CHECK(quantapdf_context_create(&context) == QUANTAPDF_OK);
    context->test_finalizations = &finalized;
    CHECK(quantapdf_open_with_context(context, IMAGES_PDF, NULL, &document) == QUANTAPDF_OK);
    quantapdf_drop_context(context);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_images(page, &images) == QUANTAPDF_OK);
    CHECK(images->context == document->context);
    quantapdf_drop_page(page);
    CHECK(quantapdf_image_count(images, &count) == QUANTAPDF_OK && count > 0);
    CHECK(quantapdf_image_render(images, 0, &bitmap) == QUANTAPDF_OK);
    CHECK(bitmap->context == images->context);
    quantapdf_drop_image_page(images);
    CHECK(bitmap->size > 0 && bitmap->data != NULL);
    quantapdf_drop_bitmap(bitmap);
    CHECK(finalized == 0 && document->context->references == 1);
    quantapdf_close(document);
    CHECK(finalized == 1);
}

typedef struct builder_state {
    quantapdf_context *owned_context;
    quantapdf_composer *parent;
    quantapdf_status result;
    int called;
} builder_state;

static quantapdf_status release_context_builder(
    quantapdf_composer *child, size_t page_index, void *user_data)
{
    builder_state *state = user_data;
    CHECK(page_index == 0 && child->context == state->parent->context);
    CHECK(child->context->references == 3);
    quantapdf_drop_context(state->owned_context);
    state->owned_context = NULL;
    CHECK(child->context->references == 2);
    ++state->called;
    return state->result;
}

static void test_builder_release(int pattern, int fail)
{
    quantapdf_context *context = NULL;
    quantapdf_composer *composer = NULL;
    quantapdf_output *first = NULL, *second = NULL;
    quantapdf_composer_form_options form = {0};
    quantapdf_composer_tiling_pattern_options tile = {0};
    quantapdf_composer_form_id form_id = 0;
    quantapdf_composer_paint_id paint_id = 0;
    builder_state state = {0};
    size_t finalized = 0;
    quantapdf_status status;
    CHECK(quantapdf_context_create(&context) == QUANTAPDF_OK);
    context->test_finalizations = &finalized;
    CHECK(quantapdf_composer_create_with_context(context, NULL, &composer) == QUANTAPDF_OK);
    add_page(composer);
    state.owned_context = context;
    state.parent = composer;
    state.result = fail ? QUANTAPDF_ERROR_STATE : QUANTAPDF_OK;
    form.struct_size = sizeof(form);
    form.width_points = form.height_points = 10;
    tile.struct_size = sizeof(tile);
    tile.width_points = tile.height_points = tile.x_step = tile.y_step = 10;
    tile.transform.a = tile.transform.d = 1;
    if (pattern)
        status = quantapdf_composer_add_tiling_pattern(composer, &tile, release_context_builder, &state, &paint_id);
    else
        status = quantapdf_composer_add_form(composer, &form, release_context_builder, &state, &form_id);
    CHECK(status == state.result && state.called == 1 && finalized == 0);
    CHECK(composer->context->references == 1);
    CHECK(quantapdf_composer_finish(composer, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(composer, &second) == QUANTAPDF_OK);
    CHECK(first->context == composer->context && second->context == first->context);
    quantapdf_drop_composer(composer);
    check_pdf(first);
    check_pdf(second);
    quantapdf_drop_output(first);
    CHECK(finalized == 0);
    quantapdf_drop_output(second);
    CHECK(finalized == 1);
}

static void test_independent_roots_and_merge(void)
{
    quantapdf_context *destination = NULL;
    quantapdf_composer *a = NULL, *b = NULL;
    quantapdf_output *left = NULL, *right = NULL, *merged = NULL, *legacy = NULL;
    const quantapdf_output *inputs[2];
    size_t finalized_a = 0, finalized_b = 0, finalized_merge = 0;
    CHECK(quantapdf_composer_create(NULL, &a) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_create(NULL, &b) == QUANTAPDF_OK);
    CHECK(a->context != b->context && a->context->references == 1 && b->context->references == 1);
    a->context->test_finalizations = &finalized_a;
    b->context->test_finalizations = &finalized_b;
    add_page(a);
    add_page(b);
    CHECK(quantapdf_composer_finish(a, &left) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_finish(b, &right) == QUANTAPDF_OK);
    quantapdf_drop_composer(a);
    quantapdf_drop_composer(b);
    inputs[0] = left;
    inputs[1] = right;
    CHECK(quantapdf_context_create(&destination) == QUANTAPDF_OK);
    destination->test_finalizations = &finalized_merge;
    CHECK(quantapdf_merge_outputs_with_context(destination, inputs, 2, &merged) == QUANTAPDF_OK);
    CHECK(merged->context == destination && destination->references == 2);
    CHECK(left->context->references == 1 && right->context->references == 1);
    CHECK(quantapdf_merge_outputs(inputs, 2, &legacy) == QUANTAPDF_OK);
    CHECK(legacy->context != destination && legacy->context != left->context && legacy->context != right->context);
    quantapdf_drop_context(destination);
    quantapdf_drop_output(left);
    quantapdf_drop_output(right);
    CHECK(finalized_a == 1 && finalized_b == 1 && finalized_merge == 0);
    check_pdf(merged);
    check_pdf(legacy);
    quantapdf_drop_output(merged);
    quantapdf_drop_output(legacy);
    CHECK(finalized_merge == 1);
}

static void test_construction_failures(int overflow)
{
    quantapdf_context *context = NULL;
    quantapdf_document *document = NULL, *new_document = NULL;
    quantapdf_page *page = NULL, *new_page = NULL;
    quantapdf_image_page *images = NULL, *new_images = NULL;
    quantapdf_bitmap *bitmap = NULL;
    quantapdf_pdf_edit *edit = NULL, *new_edit = NULL;
    quantapdf_composer *composer = NULL, *new_composer = NULL;
    quantapdf_output *input = NULL, *output = NULL;
    quantapdf_form *form = NULL;
    quantapdf_text_page *text = NULL;
    quantapdf_link_page *links = NULL;
    quantapdf_annotation_page *annotations = NULL;
    quantapdf_outline *outline = NULL;
    const quantapdf_output *inputs[1];
    quantapdf_page_crop crop = {sizeof(crop), 0, {0, 0, 10, 10}};
    quantapdf_page_trim trim = {sizeof(trim), 0, {0, 0, 10, 10}};
    quantapdf_page_poster_split split = {sizeof(split), 0, 1, 1};
    quantapdf_image_recompression_options recompress = {sizeof(recompress), 80, 0};
    size_t references;
    int index = 0;
    quantapdf_status expected = overflow ? QUANTAPDF_ERROR_UNSUPPORTED : QUANTAPDF_ERROR_NOMEM;
    CHECK(quantapdf_context_create(&context) == QUANTAPDF_OK);
    CHECK(quantapdf_open_with_context(context, IMAGES_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(quantapdf_extract_images(page, &images) == QUANTAPDF_OK);
    CHECK(images->count > 0);
    CHECK(quantapdf_pdf_edit_begin(document, &edit) == QUANTAPDF_OK);
    CHECK(quantapdf_composer_create_with_context(context, NULL, &composer) == QUANTAPDF_OK);
    add_page(composer);
    CHECK(quantapdf_composer_finish(composer, &input) == QUANTAPDF_OK);
    inputs[0] = input;
    references = context->references;

    /* Exercise real facade acquisition/allocation failure and output reset.
     * The private count is restored before any handle is released. */
#define FAIL_CONSTRUCTION(call, slot) do { \
    (slot) = (void *)(uintptr_t)1; \
    if (overflow) context->references = SIZE_MAX; \
    else context->test_fail_allocation = 1; \
    CHECK((call) == expected); \
    CHECK((slot) == NULL); \
    CHECK(context->references == (overflow ? SIZE_MAX : references)); \
    CHECK(context->test_fail_allocation == 0); \
    context->references = references; \
} while (0)
    FAIL_CONSTRUCTION(quantapdf_open_with_context(context, IMAGES_PDF, NULL, &new_document), new_document);
    FAIL_CONSTRUCTION(quantapdf_composer_create_with_context(context, NULL, &new_composer), new_composer);
    FAIL_CONSTRUCTION(quantapdf_merge_outputs_with_context(context, inputs, 1, &output), output);
    FAIL_CONSTRUCTION(quantapdf_load_page(document, 0, &new_page), new_page);
    FAIL_CONSTRUCTION(quantapdf_render_page(page, &bitmap), bitmap);
    FAIL_CONSTRUCTION(quantapdf_image_render(images, 0, &bitmap), bitmap);
    FAIL_CONSTRUCTION(quantapdf_document_form(document, &form), form);
    FAIL_CONSTRUCTION(quantapdf_pdf_edit_begin(document, &new_edit), new_edit);
    FAIL_CONSTRUCTION(quantapdf_pdf_edit_form_snapshot(edit, &form), form);
    FAIL_CONSTRUCTION(quantapdf_pdf_edit_snapshot(edit, &output), output);
    FAIL_CONSTRUCTION(quantapdf_composer_finish(composer, &output), output);
    FAIL_CONSTRUCTION(quantapdf_export_pages(document, &index, 1, &output), output);
    FAIL_CONSTRUCTION(quantapdf_crop_pages(document, &crop, 1, &output), output);
    FAIL_CONSTRUCTION(quantapdf_trim_pages(document, &trim, 1, &output), output);
    FAIL_CONSTRUCTION(quantapdf_poster_split_pages(document, &split, 1, &output), output);
    FAIL_CONSTRUCTION(quantapdf_rewrite_lossless(document, &output), output);
    FAIL_CONSTRUCTION(quantapdf_recompress_images(document, &recompress, &output), output);
    FAIL_CONSTRUCTION(quantapdf_flatten_interactive(document, QUANTAPDF_FLATTEN_ANNOTATIONS, &output), output);
    FAIL_CONSTRUCTION(quantapdf_sanitize(document, QUANTAPDF_SANITIZE_ALL, &output), output);
    FAIL_CONSTRUCTION(quantapdf_decrypt_pdf(document, &output), output);
    if (overflow) {
        FAIL_CONSTRUCTION(quantapdf_extract_structured_text(page, &text), text);
        FAIL_CONSTRUCTION(quantapdf_extract_images(page, &new_images), new_images);
        FAIL_CONSTRUCTION(quantapdf_extract_links(page, &links), links);
        FAIL_CONSTRUCTION(quantapdf_extract_annotations(page, &annotations), annotations);
        FAIL_CONSTRUCTION(quantapdf_document_outline(document, &outline), outline);
    }
#undef FAIL_CONSTRUCTION
    CHECK(quantapdf_composer_finish(composer, &output) == QUANTAPDF_OK);
    check_pdf(output);
    quantapdf_drop_output(output);
    quantapdf_drop_output(input);
    quantapdf_drop_composer(composer);
    quantapdf_drop_pdf_edit(edit);
    quantapdf_drop_image_page(images);
    quantapdf_drop_page(page);
    quantapdf_close(document);
    CHECK(context->references == 1);
    quantapdf_drop_context(context);
}

static void test_transform_output_domains(void)
{
    quantapdf_document *document = NULL;
    quantapdf_output *outputs[8] = {0};
    quantapdf_page_crop crop = {sizeof(crop), 0, {0, 0, 10, 10}};
    quantapdf_page_trim trim = {sizeof(trim), 0, {0, 0, 10, 10}};
    quantapdf_page_poster_split split = {sizeof(split), 0, 1, 1};
    quantapdf_image_recompression_options recompress = {sizeof(recompress), 80, 0};
    size_t finalized = 0, i;
    int index = 0;
    CHECK(quantapdf_open(TEXT_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(document->context->references == 1);
    document->context->test_finalizations = &finalized;
    CHECK(quantapdf_export_pages(document, &index, 1, &outputs[0]) == QUANTAPDF_OK);
    CHECK(quantapdf_crop_pages(document, &crop, 1, &outputs[1]) == QUANTAPDF_OK);
    CHECK(quantapdf_trim_pages(document, &trim, 1, &outputs[2]) == QUANTAPDF_OK);
    CHECK(quantapdf_poster_split_pages(document, &split, 1, &outputs[3]) == QUANTAPDF_OK);
    CHECK(quantapdf_rewrite_lossless(document, &outputs[4]) == QUANTAPDF_OK);
    CHECK(quantapdf_recompress_images(document, &recompress, &outputs[5]) == QUANTAPDF_OK);
    CHECK(quantapdf_flatten_interactive(document, QUANTAPDF_FLATTEN_ANNOTATIONS, &outputs[6]) == QUANTAPDF_OK);
    CHECK(quantapdf_sanitize(document, QUANTAPDF_SANITIZE_ALL, &outputs[7]) == QUANTAPDF_OK);
    for (i = 0; i < 8; ++i)
        CHECK(outputs[i]->context == document->context);
    CHECK(document->context->references == 9);
    quantapdf_close(document);
    for (i = 0; i < 8; ++i) {
        CHECK(finalized == 0);
        check_pdf(outputs[i]);
        quantapdf_drop_output(outputs[i]);
    }
    CHECK(finalized == 1);
}

int main(void)
{
    test_arguments_and_failed_open();
    test_shared_domain_and_detached_results();
    test_image_domain();
    test_builder_release(0, 0);
    test_builder_release(0, 1);
    test_builder_release(1, 0);
    test_builder_release(1, 1);
    test_independent_roots_and_merge();
    test_construction_failures(0);
    test_construction_failures(1);
    test_transform_output_domains();
    return EXIT_SUCCESS;
}
