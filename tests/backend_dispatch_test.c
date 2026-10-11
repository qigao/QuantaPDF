#include "internal.h"

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

typedef struct dispatch_probe {
    quantapdf_context *context;
    size_t finalizations;
    int loads;
    int drops;
    int closes;
    int renders;
    int text_extractions;
    int exports;
    int edit_begins;
    int edit_snapshots;
    int edit_drops;
    quantapdf_status load_result;
    quantapdf_status render_result;
    quantapdf_status export_result;
    quantapdf_status edit_result;
    quantapdf_status structured_result;
} dispatch_probe;

typedef struct probe_page {
    dispatch_probe *probe;
} probe_page;

typedef struct probe_edit {
    dispatch_probe *probe;
} probe_edit;

static quantapdf_status probe_page_count(void *document, int *out_count)
{
    CHECK(document != NULL);
    *out_count = 1;
    return QUANTAPDF_OK;
}

static quantapdf_status probe_load_page(
    void *document, int page_index, void **out_page)
{
    dispatch_probe *probe = document;
    probe_page *page;
    CHECK(page_index == 0);
    ++probe->loads;
    *out_page = NULL;
    if (probe->load_result != QUANTAPDF_OK)
        return probe->load_result;
    page = (probe_page *)malloc(sizeof(*page));
    CHECK(page != NULL);
    page->probe = probe;
    *out_page = page;
    return QUANTAPDF_OK;
}

static void probe_drop_page(void *backend_page)
{
    probe_page *page = backend_page;
    ++page->probe->drops;
    free(page);
}

static void probe_close(void *document)
{
    dispatch_probe *probe = document;
    CHECK(probe->context->references != 0 && probe->finalizations == 0);
    ++probe->closes;
}

static quantapdf_status probe_page_bounds(void *page, quantapdf_rect *out_bounds)
{
    CHECK(page != NULL);
    *out_bounds = (quantapdf_rect){ 0.0f, 0.0f, 10.0f, 20.0f };
    return QUANTAPDF_OK;
}

static quantapdf_status probe_page_user_unit(
    void *document, int page_index, double *out_unit)
{
    CHECK(document != NULL);
    CHECK(page_index == 0);
    *out_unit = 2.0;
    return QUANTAPDF_OK;
}

static quantapdf_status probe_page_box_bounds(
    void *document, int page_index, quantapdf_page_box box,
    quantapdf_rect *out_bounds)
{
    CHECK(document != NULL);
    CHECK(page_index == 0);
    CHECK(box == QUANTAPDF_PAGE_BOX_MEDIA);
    *out_bounds = (quantapdf_rect){ -2.0f, -4.0f, 10.0f, 20.0f };
    return QUANTAPDF_OK;
}

static quantapdf_status probe_render_page(
    void *backend_page, float dpi, float rotation_degrees,
    const quantapdf_rect *clip, double user_unit, int alpha,
    quantapdf_backend_bitmap *out_bitmap)
{
    probe_page *page = backend_page;
    CHECK(dpi == 72.0f);
    CHECK(rotation_degrees == 0.0f);
    CHECK(clip == NULL);
    CHECK(user_unit == 2.0);
    CHECK(alpha == 0);
    ++page->probe->renders;
    if (page->probe->render_result != QUANTAPDF_OK)
        return page->probe->render_result;
    out_bitmap->data = (unsigned char *)malloc(3);
    CHECK(out_bitmap->data != NULL);
    memset(out_bitmap->data, 0x42, 3);
    out_bitmap->size = 3;
    out_bitmap->width = 1;
    out_bitmap->height = 1;
    out_bitmap->stride = 3;
    out_bitmap->components = 3;
    return QUANTAPDF_OK;
}

static quantapdf_status probe_extract_text(
    void *backend_page, char **out_text, size_t *out_size)
{
    probe_page *page = backend_page;
    ++page->probe->text_extractions;
    *out_text = (char *)malloc(5);
    CHECK(*out_text != NULL);
    memcpy(*out_text, "text", 5);
    *out_size = 4;
    return QUANTAPDF_OK;
}

static quantapdf_status probe_extract_structured_text(
    void *backend_page, quantapdf_text_page **out_text)
{
    probe_page *page = backend_page;
    CHECK(backend_page != NULL);
    *out_text = NULL;
    if (page->probe->structured_result != QUANTAPDF_OK)
        return page->probe->structured_result;
    *out_text = (quantapdf_text_page *)calloc(1, sizeof(**out_text));
    CHECK(*out_text != NULL);
    return QUANTAPDF_OK;
}

static quantapdf_status probe_bytes(unsigned char **out_data, size_t *out_size)
{
    *out_data = (unsigned char *)malloc(3);
    CHECK(*out_data != NULL);
    memcpy(*out_data, "out", 3);
    *out_size = 3;
    return QUANTAPDF_OK;
}

static quantapdf_status probe_export_pages(
    void *document, const int *indices, size_t count,
    unsigned char **out_data, size_t *out_size)
{
    dispatch_probe *probe = document;
    CHECK(count == 1);
    CHECK(indices[0] == 0);
    ++probe->exports;
    if (probe->export_result != QUANTAPDF_OK)
        return probe->export_result;
    return probe_bytes(out_data, out_size);
}

static quantapdf_status probe_edit_begin(void *document, void **out_edit)
{
    dispatch_probe *probe = document;
    probe_edit *edit;
    ++probe->edit_begins;
    *out_edit = NULL;
    if (probe->edit_result != QUANTAPDF_OK)
        return probe->edit_result;
    edit = (probe_edit *)malloc(sizeof(*edit));
    CHECK(edit != NULL);
    edit->probe = probe;
    *out_edit = edit;
    return QUANTAPDF_OK;
}

static quantapdf_status probe_edit_snapshot(
    void *backend_edit, int *test_fault,
    unsigned char **out_data, size_t *out_size)
{
    probe_edit *edit = backend_edit;
    (void)test_fault;
    ++edit->probe->edit_snapshots;
    return probe_bytes(out_data, out_size);
}

static void probe_edit_drop(void *backend_edit)
{
    probe_edit *edit = backend_edit;
    ++edit->probe->edit_drops;
    free(edit);
}

/* This fixture supplies only the callbacks it exercises. Production tables
 * are immutable complete internal implementations, not a public injection API. */
static const quantapdf_backend_edit_ops probe_edit_ops = {
    .snapshot = probe_edit_snapshot,
    .drop = probe_edit_drop,
};

static const quantapdf_backend_ops probe_ops = {
    .close = probe_close,
    .page_count = probe_page_count,
    .load_page = probe_load_page,
    .drop_page = probe_drop_page,
    .page_bounds = probe_page_bounds,
    .page_user_unit = probe_page_user_unit,
    .page_box_bounds = probe_page_box_bounds,
    .render_page = probe_render_page,
    .extract_text = probe_extract_text,
    .extract_structured_text = probe_extract_structured_text,
    .export_pages = probe_export_pages,
    .edit_begin = probe_edit_begin,
    .edit_ops = &probe_edit_ops,
};

void quantapdf_test_backend_dispatch(void)
{
    dispatch_probe probe = {0};
    quantapdf_document *document = (quantapdf_document *)calloc(1, sizeof(*document));
    quantapdf_page *page = NULL;
    quantapdf_bitmap *bitmap = NULL;
    quantapdf_text_page *snapshot = NULL;
    quantapdf_pdf_edit *edit = NULL;
    quantapdf_output *output = NULL;
    quantapdf_rect bounds = {0};
    const unsigned char *bytes = NULL;
    char *text = NULL;
    size_t size = 0;
    size_t count = 0;
    int page_count = 0;
    int index = 0;

    CHECK(document != NULL);
    CHECK(quantapdf_context_create(&document->context) == QUANTAPDF_OK);
    probe.context = document->context;
    document->context->test_finalizations = &probe.finalizations;
    document->ops = &probe_ops;
    document->backend = &probe;
    CHECK(quantapdf_page_count(document, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 1);
    CHECK(quantapdf_load_page(document, -1, &page) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(page == NULL);
    CHECK(probe.loads == 0);
    document->context->references = SIZE_MAX;
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(page == NULL && probe.loads == 0);
    document->context->references = 1;
    document->context->test_fail_allocation = 1;
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_ERROR_NOMEM);
    CHECK(page == NULL && probe.loads == 0 && document->context->references == 1);
    probe.load_result = QUANTAPDF_ERROR_STATE;
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_ERROR_STATE);
    CHECK(page == NULL);
    CHECK(probe.loads == 1 && probe.drops == 0);
    CHECK(document->context->references == 1);
    probe.load_result = QUANTAPDF_OK;
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    CHECK(probe.loads == 2);
    CHECK(quantapdf_page_bounds(page, &bounds) == QUANTAPDF_OK);
    CHECK(bounds.x1 == 20.0f && bounds.y1 == 40.0f);
    CHECK(quantapdf_page_box_bounds(page, QUANTAPDF_PAGE_BOX_MEDIA, &bounds) == QUANTAPDF_OK);
    CHECK(bounds.x0 == -4.0f && bounds.y0 == -8.0f);
    CHECK(bounds.x1 == 20.0f && bounds.y1 == 40.0f);

    probe.render_result = QUANTAPDF_ERROR_UNSUPPORTED;
    CHECK(quantapdf_render_page(page, &bitmap) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(bitmap == NULL);
    probe.render_result = QUANTAPDF_OK;
    CHECK(quantapdf_render_page(page, &bitmap) == QUANTAPDF_OK);
    CHECK(probe.renders == 2);
    CHECK(quantapdf_bitmap_data(bitmap, &bytes, &size) == QUANTAPDF_OK);
    CHECK(size == 3 && bytes[0] == 0x42);
    quantapdf_drop_bitmap(bitmap);
    CHECK(quantapdf_extract_text(page, &text, &size) == QUANTAPDF_OK);
    CHECK(probe.text_extractions == 1);
    CHECK(size == 4 && strcmp(text, "text") == 0);
    quantapdf_free(text);
    probe.structured_result = QUANTAPDF_ERROR_NOMEM;
    CHECK(quantapdf_extract_structured_text(page, &snapshot) == QUANTAPDF_ERROR_NOMEM);
    CHECK(snapshot == NULL && document->context->references == 2);
    probe.structured_result = QUANTAPDF_OK;
    CHECK(quantapdf_extract_structured_text(page, &snapshot) == QUANTAPDF_OK);

    probe.export_result = QUANTAPDF_ERROR_FORMAT;
    CHECK(quantapdf_export_pages(document, &index, 1, &output) == QUANTAPDF_ERROR_FORMAT);
    CHECK(output == NULL);
    probe.export_result = QUANTAPDF_OK;
    CHECK(quantapdf_export_pages(document, &index, 1, &output) == QUANTAPDF_OK);
    CHECK(probe.exports == 2);
    CHECK(quantapdf_output_data(output, &bytes, &size) == QUANTAPDF_OK);
    CHECK(size == 3 && memcmp(bytes, "out", 3) == 0);
    quantapdf_drop_output(output);
    output = NULL;

    probe.edit_result = QUANTAPDF_ERROR_BACKEND;
    CHECK(quantapdf_pdf_edit_begin(document, &edit) == QUANTAPDF_ERROR_BACKEND);
    CHECK(edit == NULL && probe.edit_drops == 0);
    probe.edit_result = QUANTAPDF_OK;
    CHECK(quantapdf_pdf_edit_begin(document, &edit) == QUANTAPDF_OK);
    CHECK(probe.edit_begins == 2);
    quantapdf_drop_page(page);
    CHECK(probe.drops == 1);
    quantapdf_close(document);
    CHECK(probe.closes == 1);

    CHECK(quantapdf_text_block_count(snapshot, &count) == QUANTAPDF_OK);
    CHECK(count == 0);
    quantapdf_drop_text_page(snapshot);
    CHECK(quantapdf_pdf_edit_snapshot(edit, &output) == QUANTAPDF_OK);
    CHECK(probe.edit_snapshots == 1);
    CHECK(quantapdf_output_data(output, &bytes, &size) == QUANTAPDF_OK);
    CHECK(size == 3 && memcmp(bytes, "out", 3) == 0);
    quantapdf_drop_output(output);
    quantapdf_drop_pdf_edit(edit);
    CHECK(probe.edit_drops == 1);
    CHECK(probe.finalizations == 1);
}
