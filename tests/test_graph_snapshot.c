#include "snapshot/graph_snapshot.h"
#include "document/document_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

static quantapdf_graph_limits limits(void)
{
    quantapdf_graph_limits value = {10000, 30000, 1048576, 1048576, 4194304, 256};
    return value;
}

static quantapdf_local_id field(const quantapdf_graph_model *m, quantapdf_local_id id, const char *name)
{
    const quantapdf_graph_edge *e = quantapdf_graph_find(m, id, name, strlen(name));
    CHECK(e != NULL); return e->target;
}

static void equal_models(const quantapdf_graph_model *a, const quantapdf_graph_model *b)
{
    size_t i;
    CHECK(a->node_count == b->node_count && a->edge_count == b->edge_count && a->byte_count == b->byte_count);
    CHECK(a->trailer == b->trailer && a->catalog == b->catalog && a->page_count == b->page_count && a->page_offset == b->page_offset);
    CHECK(memcmp(a->bytes, b->bytes, a->byte_count) == 0);
    for (i = 0; i < a->node_count; ++i) {
        const quantapdf_graph_node *x = &a->nodes[i], *y = &b->nodes[i];
        CHECK(x->kind == y->kind && x->indirect == y->indirect && x->integer == y->integer);
        CHECK(x->data_offset == y->data_offset && x->data_size == y->data_size);
        CHECK(x->edge_offset == y->edge_offset && x->edge_count == y->edge_count);
    }
    for (i = 0; i < a->edge_count; ++i) {
        CHECK(a->edges[i].target == b->edges[i].target);
        CHECK(a->edges[i].key_offset == b->edges[i].key_offset && a->edges[i].key_size == b->edges[i].key_size);
    }
}

static void test_capture(void)
{
    quantapdf_graph_limits budget = limits(), small;
    quantapdf_context *context = NULL;
    quantapdf_document *document = NULL;
    quantapdf_graph_snapshot *a = NULL, *b = NULL, *failed;
    const quantapdf_graph_model *m;
    quantapdf_local_id parent, font = 0;
    quantapdf_validation_report report;
    size_t i, finalized = 0, references;
    OK(quantapdf_context_create(&context)); context->test_finalizations = &finalized;
    OK(quantapdf_open_with_context(context, GRAPH_PDF, NULL, &document));
    references = context->references;
    context->test_fail_allocation = 1;
    failed = (quantapdf_graph_snapshot *)(uintptr_t)1;
    CHECK(quantapdf_document_capture_graph(document, &budget, &failed) == QUANTAPDF_ERROR_NOMEM);
    CHECK(failed == NULL && context->references == references);
    small = budget; small.max_nodes = 1;
    CHECK(quantapdf_document_capture_graph(document, &small, &failed) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(failed == NULL && context->references == references);
    small = budget; small.max_stream_bytes = 1;
    CHECK(quantapdf_document_capture_graph(document, &small, &failed) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(failed == NULL && context->references == references);
    small = budget; small.max_depth = 1;
    CHECK(quantapdf_document_capture_graph(document, &small, &failed) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(failed == NULL && context->references == references);
    OK(quantapdf_document_capture_graph(document, &budget, &a));
    OK(quantapdf_document_capture_graph(document, &budget, &b));
    quantapdf_close(document); quantapdf_drop_context(context);
    CHECK(finalized == 0);
    m = quantapdf_graph_view(a->graph);
    equal_models(m, quantapdf_graph_view(b->graph));
    OK(quantapdf_validate_graph(m, &report));
    CHECK(m->page_count == 3 && quantapdf_graph_storage_bytes(a->graph) <= budget.max_storage_bytes);
    parent = field(m, m->catalog, "/Pages");
    for (i = 0; i < m->page_count; ++i) {
        quantapdf_local_id page = m->edges[m->page_offset + i].target;
        quantapdf_local_id resources = field(m, page, "/Resources");
        quantapdf_local_id current = field(m, field(m, resources, "/Font"), "/F1");
        const quantapdf_graph_node *stream = quantapdf_graph_node_at(m, field(m, page, "/Contents"));
        CHECK(field(m, page, "/Parent") == parent);
        if (i == 0) font = current; else CHECK(current == font);
        CHECK(stream->kind == QUANTAPDF_GRAPH_STREAM && stream->data_size == 37);
        CHECK(memcmp(m->bytes + stream->data_offset, "BT\n/F1 18 Tf\n20 80 Td\n(PAGE-", 27) == 0);
    }
    quantapdf_graph_snapshot_drop(a); CHECK(finalized == 0);
    OK(quantapdf_validate_graph(quantapdf_graph_view(b->graph), &report));
    quantapdf_graph_snapshot_drop(b); CHECK(finalized == 1);
    quantapdf_graph_snapshot_drop(NULL);
}

static void test_document_families(void)
{
    static const struct { const char *path; const char *root_key; int trailer; const char *password; } cases[] = {
        {GRAPH_METADATA_PDF, "/Info", 1, NULL}, {GRAPH_FORM_PDF, "/AcroForm", 0, NULL},
        {GRAPH_OUTLINE_PDF, "/Outlines", 0, NULL}, {GRAPH_IMAGES_PDF, "/Pages", 0, NULL},
        {GRAPH_ENCRYPTED_PDF, "/Encrypt", 1, "user-pass"}
    };
    quantapdf_graph_limits budget = limits();
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        quantapdf_document *document = NULL;
        quantapdf_graph_snapshot *snapshot = NULL;
        const quantapdf_graph_model *m;
        quantapdf_status status;
        OK(quantapdf_open(cases[i].path, cases[i].password, &document));
        status = quantapdf_document_capture_graph(document, &budget, &snapshot);
        if (status != QUANTAPDF_OK) fprintf(stderr, "Graph capture failed: %s (status %d)\n", cases[i].path, status);
        OK(status);
        quantapdf_close(document);
        m = quantapdf_graph_view(snapshot->graph);
        CHECK(field(m, cases[i].trailer ? m->trailer : m->catalog, cases[i].root_key) != 0);
        if (i == 0) {
            quantapdf_local_id page = m->edges[m->page_offset].target;
            const quantapdf_graph_node *title = quantapdf_graph_node_at(m,
                field(m, field(m, m->trailer, "/Info"), "/Title"));
            CHECK(quantapdf_graph_find(m, page, "/Resources", 10) == NULL);
            CHECK(title->kind == QUANTAPDF_GRAPH_STRING && title->data_size >= 2);
            CHECK(m->bytes[title->data_offset] == 0xfe && m->bytes[title->data_offset + 1] == 0xff);
        }
        quantapdf_graph_snapshot_drop(snapshot);
    }
}

static quantapdf_status incomplete_graph(void *backend, const quantapdf_graph_limits *budget,
    quantapdf_graph **out_graph)
{
    quantapdf_status status = quantapdf_graph_create(budget, out_graph);
    return status == QUANTAPDF_OK ? *(const quantapdf_status *)backend : status;
}

static void test_provider_failure(void)
{
    static const quantapdf_backend_ops ops = {.capture_graph = incomplete_graph};
    quantapdf_graph_limits budget = limits();
    quantapdf_document document = {0};
    quantapdf_graph_snapshot *snapshot = NULL;
    quantapdf_status result = QUANTAPDF_OK;
    OK(quantapdf_context_create(&document.context));
    document.ops = &ops; document.backend = &result;
    /* Success without sealing is a provider contract failure. */
    CHECK(quantapdf_document_capture_graph(&document, &budget, &snapshot) == QUANTAPDF_ERROR_BACKEND);
    CHECK(snapshot == NULL && document.context->references == 1);
    result = QUANTAPDF_ERROR_FORMAT;
    CHECK(quantapdf_document_capture_graph(&document, &budget, &snapshot) == QUANTAPDF_ERROR_FORMAT);
    CHECK(snapshot == NULL && document.context->references == 1);
    quantapdf_drop_context(document.context);
}

int main(void) { test_capture(); test_document_families(); test_provider_failure(); return EXIT_SUCCESS; }
