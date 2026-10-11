#include "snapshot/graph_snapshot.h"
#include "document/page_content_ir.h"
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
    CHECK(memcmp(a->source.pdf_version, b->source.pdf_version, 4) == 0 && a->source.encrypted == b->source.encrypted);
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

static void test_commit_detached_metadata(void)
{
    quantapdf_context *context = NULL;
    quantapdf_document *document = NULL;
    quantapdf_graph_snapshot *base = NULL, *a = NULL, *b = NULL, *failed;
    quantapdf_output *output = NULL, *second = NULL;
    quantapdf_document *reopened = NULL;
    quantapdf_graph_snapshot *roundtrip = NULL;
    quantapdf_graph_write_limits write_limits = {262144, 128, 100000};
    const unsigned char *written, *repeated;
    size_t written_size, repeated_size;
    char *decoded = NULL;
    size_t decoded_size = 0;
    quantapdf_graph_limits budget = limits(), small;
    quantapdf_graph_change_set changes = {0};
    quantapdf_graph_replacement replacement = {0};
    quantapdf_graph_reference title_ref;
    quantapdf_validation_report report;
    const quantapdf_graph_model *model;
    const quantapdf_graph_node *original, *title;
    quantapdf_local_id title_id, resolved;
    unsigned char payload[] = {'N', 0, 0xff, 'W'};
    size_t finalized = 0, references;
    OK(quantapdf_context_create(&context)); context->test_finalizations = &finalized;
    OK(quantapdf_open_with_context(context, GRAPH_METADATA_PDF, NULL, &document));
    OK(quantapdf_document_capture_graph(document, &budget, &base));
    quantapdf_close(document); quantapdf_drop_context(context);
    context = base->owner.context;
    references = context->references;
    model = quantapdf_graph_view(base->graph);
    title_id = field(model, field(model, model->trailer, "/Info"), "/Title");
    original = quantapdf_graph_node_at(model, title_id);
    CHECK(original->kind == QUANTAPDF_GRAPH_STRING && model->bytes[original->data_offset] == 0xfe);
    OK(quantapdf_graph_reference_at(base->graph, title_id, &title_ref));
    changes.lineage = quantapdf_graph_lineage(base->graph);
    changes.base_version = quantapdf_graph_version(base->graph);
    replacement.id = title_id;
    replacement.value.kind = QUANTAPDF_GRAPH_STRING;
    replacement.value.indirect = original->indirect;
    replacement.value.data = payload;
    replacement.value.data_size = sizeof(payload);
    changes.replacements = &replacement; changes.replacement_count = 1;
    context->test_fail_allocation = 1;
    failed = (quantapdf_graph_snapshot *)(uintptr_t)1;
    CHECK(quantapdf_graph_snapshot_commit(base, &changes, &budget, &failed, &report) == QUANTAPDF_ERROR_NOMEM);
    CHECK(failed == NULL && context->references == references);
    small = budget; small.max_nodes = 1;
    CHECK(quantapdf_graph_snapshot_commit(base, &changes, &small, &failed, &report) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(failed == NULL && context->references == references);
    OK(quantapdf_graph_snapshot_commit(base, &changes, &budget, &a, &report));
    OK(quantapdf_graph_snapshot_commit(base, &changes, &budget, &b, &report));
    CHECK(context->references == references + 2);
    CHECK(quantapdf_graph_version(a->graph) != quantapdf_graph_version(b->graph));
    equal_models(quantapdf_graph_view(a->graph), quantapdf_graph_view(b->graph));
    CHECK(model->bytes[original->data_offset] == 0xfe);
    CHECK(quantapdf_graph_snapshot_commit(a, &changes, &budget, &failed, &report) == QUANTAPDF_ERROR_STATE);
    CHECK(failed == NULL && context->references == references + 2);
    payload[0] = 'X';
    OK(quantapdf_graph_snapshot_metadata(base, QUANTAPDF_METADATA_TITLE, &decoded, &decoded_size));
    CHECK(decoded_size == 13 && memcmp(decoded, "Phase 5 Caf\xc3\xa9", 13) == 0);
    quantapdf_free(decoded);
    quantapdf_graph_snapshot_drop(base); CHECK(finalized == 0);
    quantapdf_graph_snapshot_drop(a); CHECK(finalized == 0);
    OK(quantapdf_graph_resolve(b->graph, &title_ref, &resolved)); CHECK(resolved == title_id);
    model = quantapdf_graph_view(b->graph);
    title = quantapdf_graph_node_at(model, title_id);
    CHECK(title->data_size == sizeof(payload));
    CHECK(model->bytes[title->data_offset] == 'N' && model->bytes[title->data_offset + 1] == 0);
    CHECK(model->bytes[title->data_offset + 2] == 0xff);
    OK(quantapdf_graph_snapshot_metadata(b, QUANTAPDF_METADATA_TITLE, &decoded, &decoded_size));
    CHECK(decoded_size == 5 && memcmp(decoded, "N\0\xc3\xbfW", 5) == 0 && decoded[5] == 0);
    OK(quantapdf_validate_graph(model, &report));
    context->test_fail_allocation = 1;
    CHECK(quantapdf_graph_snapshot_write(b, &write_limits, &output) == QUANTAPDF_ERROR_NOMEM);
    CHECK(output == NULL && context->references == 1);
    write_limits.max_output_bytes = 1;
    CHECK(quantapdf_graph_snapshot_write(b, &write_limits, &output) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(output == NULL && context->references == 1);
    write_limits.max_output_bytes = 262144;
    OK(quantapdf_graph_snapshot_write(b, &write_limits, &output));
    OK(quantapdf_graph_snapshot_write(b, &write_limits, &second));
    OK(quantapdf_output_data(output, &written, &written_size));
    OK(quantapdf_output_data(second, &repeated, &repeated_size));
    CHECK(written_size == repeated_size && memcmp(written, repeated, written_size) == 0);
    quantapdf_drop_output(second);
    quantapdf_graph_snapshot_drop(b); CHECK(finalized == 0);
    CHECK(memcmp(decoded, "N\0\xc3\xbfW", 5) == 0); quantapdf_free(decoded);
    OK(quantapdf_output_save_file(output, "graph-writer-output.pdf"));
    OK(quantapdf_open("graph-writer-output.pdf", NULL, &reopened));
    OK(quantapdf_document_capture_graph(reopened, &budget, &roundtrip));
    model = quantapdf_graph_view(roundtrip->graph);
    title = quantapdf_graph_node_at(model, field(model, field(model, model->trailer, "/Info"), "/Title"));
    CHECK(title->data_size == sizeof(payload) && model->bytes[title->data_offset] == 'N' &&
        model->bytes[title->data_offset + 1] == 0 && model->bytes[title->data_offset + 2] == 0xff);
    quantapdf_graph_snapshot_drop(roundtrip); quantapdf_close(reopened);
    CHECK(remove("graph-writer-output.pdf") == 0);
    quantapdf_drop_output(output); CHECK(finalized == 1);
}

static void test_metadata_compatibility(void)
{
    quantapdf_document *document = NULL;
    quantapdf_graph_snapshot *snapshot = NULL;
    quantapdf_graph_limits budget = limits();
    int field;
    char *retained = NULL;
    size_t retained_size = 0;
    OK(quantapdf_open(GRAPH_METADATA_PDF, NULL, &document));
    OK(quantapdf_document_capture_graph(document, &budget, &snapshot));
    for (field = 0; field <= 9; ++field) {
        char *legacy = (char *)(uintptr_t)1, *graph = (char *)(uintptr_t)1;
        size_t legacy_size = 99, graph_size = 99;
        quantapdf_status a = quantapdf_document_metadata(document, (quantapdf_metadata_field)field, &legacy, &legacy_size);
        quantapdf_status b = quantapdf_graph_snapshot_metadata(snapshot, (quantapdf_metadata_field)field, &graph, &graph_size);
        CHECK(a == b && legacy_size == graph_size && (legacy == NULL) == (graph == NULL));
        if (legacy != NULL) CHECK(memcmp(legacy, graph, legacy_size + 1) == 0);
        quantapdf_free(legacy); quantapdf_free(graph);
    }
    quantapdf_close(document);
    OK(quantapdf_graph_snapshot_metadata(snapshot, QUANTAPDF_METADATA_TITLE, &retained, &retained_size));
    quantapdf_graph_snapshot_drop(snapshot);
    CHECK(retained_size == 13 && memcmp(retained, "Phase 5 Caf\xc3\xa9", 13) == 0);
    quantapdf_free(retained);
    retained = (char *)(uintptr_t)1; retained_size = 99;
    CHECK(quantapdf_graph_snapshot_metadata(NULL, QUANTAPDF_METADATA_TITLE, &retained, &retained_size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(retained == NULL && retained_size == 0);
}

static void test_stream_snapshot_lifetime(void)
{
    quantapdf_context *context = NULL;
    quantapdf_document *document = NULL;
    quantapdf_graph_snapshot *snapshot = NULL;
    quantapdf_graph_limits budget = limits();
    quantapdf_graph_stream_limits decode = {4096, 128, 100000};
    quantapdf_graph_reference reference;
    const quantapdf_graph_model *model;
    const quantapdf_graph_node *node;
    quantapdf_local_id id;
    unsigned char *first = NULL, *second = NULL;
    size_t first_size = 0, second_size = 0, finalized = 0;
    OK(quantapdf_context_create(&context)); context->test_finalizations = &finalized;
    OK(quantapdf_open_with_context(context, GRAPH_PDF, NULL, &document));
    OK(quantapdf_document_capture_graph(document, &budget, &snapshot));
    quantapdf_drop_context(context); quantapdf_close(document);
    CHECK(finalized == 0);
    model = quantapdf_graph_view(snapshot->graph);
    id = field(model, model->edges[model->page_offset].target, "/Contents");
    node = quantapdf_graph_node_at(model, id);
    CHECK(node->kind == QUANTAPDF_GRAPH_STREAM);
    OK(quantapdf_graph_reference_at(snapshot->graph, id, &reference));
    OK(quantapdf_graph_snapshot_stream(snapshot, &reference, &decode, &first, &first_size));
    CHECK(first_size == node->data_size && first_size != 0);
    CHECK(memcmp(first, model->bytes + node->data_offset, first_size) == 0);
    OK(quantapdf_graph_snapshot_stream(snapshot, &reference, &decode, &second, &second_size));
    quantapdf_graph_snapshot_drop(snapshot);
    CHECK(finalized == 1 && first_size == second_size && memcmp(first, second, first_size) == 0);
    quantapdf_free(first); quantapdf_free(second);
    first = (unsigned char *)(uintptr_t)1; first_size = 99;
    CHECK(quantapdf_graph_snapshot_stream(NULL, &reference, &decode, &first, &first_size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(first == NULL && first_size == 0);
}


static void test_content_ir_snapshot_version(void)
{
    static const unsigned char path[] = "q 0 0 m 10 20 l S Q";
    quantapdf_graph_limits budget = limits();
    quantapdf_graph_stream_limits decode = {4096, 128, 100000};
    quantapdf_pdf_content_limits parse = {4096, 16, 4}, small;
    quantapdf_context *context = NULL;
    quantapdf_document *document = NULL;
    quantapdf_graph_snapshot *base = NULL, *next = NULL;
    const quantapdf_graph_model *model;
    const quantapdf_graph_node *node;
    quantapdf_graph_reference reference;
    quantapdf_graph_edge_input *edges;
    quantapdf_graph_change_set changes = {0};
    quantapdf_graph_replacement replacement = {0};
    quantapdf_validation_report report;
    quantapdf_pdf_content_ir *ir = NULL;
    quantapdf_local_id id, page;
    size_t i, finalized = 0;

    OK(quantapdf_context_create(&context));
    context->test_finalizations = &finalized;
    OK(quantapdf_open_with_context(context, GRAPH_PDF, NULL, &document));
    OK(quantapdf_document_capture_graph(document, &budget, &base));
    quantapdf_close(document); quantapdf_drop_context(context);
    CHECK(finalized == 0);
    model = quantapdf_graph_view(base->graph);
    page = model->edges[model->page_offset].target;
    id = field(model, page, "/Contents");
    node = quantapdf_graph_node_at(model, id);
    CHECK(node != NULL && node->kind == QUANTAPDF_GRAPH_STREAM);
    OK(quantapdf_graph_reference_at(base->graph, id, &reference));

    /* The original fixture contains BT/text, which must not be silently
     * discarded to produce a misleading incomplete path interpretation. */
    ir = (quantapdf_pdf_content_ir *)(uintptr_t)1;
    CHECK(quantapdf_graph_snapshot_path_ir(base, &reference, &decode, &parse,
        &ir) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(ir == NULL);

    edges = (quantapdf_graph_edge_input *)calloc(
        node->edge_count ? node->edge_count : 1, sizeof(*edges));
    CHECK(edges != NULL);
    for (i = 0; i < node->edge_count; ++i) {
        const quantapdf_graph_edge *edge = &model->edges[node->edge_offset + i];
        edges[i].target = edge->target;
        edges[i].key = model->bytes + edge->key_offset;
        edges[i].key_size = edge->key_size;
    }
    changes.lineage = quantapdf_graph_lineage(base->graph);
    changes.base_version = quantapdf_graph_version(base->graph);
    replacement.id = id;
    replacement.value.kind = QUANTAPDF_GRAPH_STREAM;
    replacement.value.indirect = node->indirect;
    replacement.value.data = path;
    replacement.value.data_size = sizeof(path) - 1;
    replacement.value.edges = edges;
    replacement.value.edge_count = node->edge_count;
    changes.replacements = &replacement;
    changes.replacement_count = 1;
    OK(quantapdf_graph_snapshot_commit(base, &changes, &budget, &next, &report));
    free(edges);
    quantapdf_graph_snapshot_drop(base);
    CHECK(finalized == 0);

    small = parse; small.max_operators = 2;
    ir = (quantapdf_pdf_content_ir *)(uintptr_t)1;
    CHECK(quantapdf_graph_snapshot_path_ir(next, &reference, &decode, &small,
        &ir) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(ir == NULL);
    OK(quantapdf_graph_snapshot_path_ir(next, &reference, &decode, &parse, &ir));
    CHECK(ir != NULL && ir->count == 5);
    CHECK(ir->ops[0].opcode == QUANTAPDF_PDF_OP_SAVE);
    CHECK(ir->ops[1].opcode == QUANTAPDF_PDF_OP_MOVE);
    CHECK(ir->ops[2].opcode == QUANTAPDF_PDF_OP_LINE &&
          ir->ops[2].arguments[0] == 10.0 && ir->ops[2].arguments[1] == 20.0);
    CHECK(ir->ops[3].opcode == QUANTAPDF_PDF_OP_STROKE);
    CHECK(ir->ops[4].opcode == QUANTAPDF_PDF_OP_RESTORE);
    quantapdf_graph_snapshot_drop(next);
    CHECK(finalized == 1 && ir->ops[2].opcode == QUANTAPDF_PDF_OP_LINE);
    quantapdf_pdf_content_dispose(ir);
}


static void test_page_semantics_arrays(void)
{
    static const char a[] = "q 1 0 0 1 100 0 cm 1 0 0 rg 0 0 m 10";
    static const char b[] = "20 l S Q";
    quantapdf_graph_limits budget = limits();
    quantapdf_graph_stream_limits decode = {4096, 128, 100000};
    quantapdf_pdf_content_limits parse = {4096, 32, 8}, small;
    quantapdf_graph_snapshot *base = NULL, *next = NULL;
    quantapdf_context *context = NULL;
    quantapdf_document *document = NULL;
    const quantapdf_graph_model *model;
    const quantapdf_graph_node *original;
    quantapdf_graph_edge_input items[2] = {{0}};
    quantapdf_graph_node_input additions[2] = {{0}};
    quantapdf_graph_change_set change = {0};
    quantapdf_graph_replacement replacement = {0};
    quantapdf_validation_report report;
    quantapdf_pdf_page_semantics *semantics = NULL;
    quantapdf_local_id content_id, page_id;
    size_t finalized = 0;

    OK(quantapdf_context_create(&context));
    context->test_finalizations = &finalized;
    OK(quantapdf_open_with_context(context, GRAPH_PDF, NULL, &document));
    OK(quantapdf_document_capture_graph(document, &budget, &base));
    quantapdf_close(document);
    quantapdf_drop_context(context);
    model = quantapdf_graph_view(base->graph);
    page_id = model->edges[model->page_offset].target;
    content_id = field(model, page_id, "/Contents");
    original = quantapdf_graph_node_at(model, content_id);
    CHECK(original != NULL && original->kind == QUANTAPDF_GRAPH_STREAM);

    /* Base version contains text; no silently incomplete path-only IR. */
    semantics = (quantapdf_pdf_page_semantics *)(uintptr_t)1;
    CHECK(quantapdf_graph_snapshot_page_semantics(base, 0, &decode, &parse, 2,
        &semantics) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(semantics == NULL);

    /* One stable /Contents slot becomes an array referring to two new streams.
     * Operands and the q/Q graphics state now cross the stream boundary. */
    items[0].target = (quantapdf_local_id)(model->node_count + 1);
    items[1].target = (quantapdf_local_id)(model->node_count + 2);
    additions[0].kind = additions[1].kind = QUANTAPDF_GRAPH_STREAM;
    additions[0].indirect = additions[1].indirect = 1;
    additions[0].data = a;
    additions[0].data_size = sizeof(a) - 1;
    additions[1].data = b;
    additions[1].data_size = sizeof(b) - 1;
    replacement.id = content_id;
    replacement.value.kind = QUANTAPDF_GRAPH_ARRAY;
    replacement.value.indirect = original->indirect;
    replacement.value.edges = items;
    replacement.value.edge_count = 2;
    change.lineage = quantapdf_graph_lineage(base->graph);
    change.base_version = quantapdf_graph_version(base->graph);
    change.replacements = &replacement;
    change.replacement_count = 1;
    change.additions = additions;
    change.addition_count = 2;
    OK(quantapdf_graph_snapshot_commit(base, &change, &budget, &next, &report));
    quantapdf_graph_snapshot_drop(base);
    CHECK(finalized == 0);

    semantics = (quantapdf_pdf_page_semantics *)(uintptr_t)1;
    CHECK(quantapdf_graph_snapshot_page_semantics(next, 0, &decode, &parse, 1,
        &semantics) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(semantics == NULL);
    small = parse; small.max_input_bytes = 10;
    CHECK(quantapdf_graph_snapshot_page_semantics(next, 0, &decode, &small, 2,
        &semantics) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(semantics == NULL);
    CHECK(quantapdf_graph_snapshot_page_semantics(next, 3, &decode, &parse, 2,
        &semantics) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(semantics == NULL);
    OK(quantapdf_graph_snapshot_page_semantics(next, 0, &decode, &parse, 2,
        &semantics));
    CHECK(semantics->content != NULL && semantics->graphics != NULL);
    CHECK(semantics->content->count == 7 && semantics->graphics->path_count == 1);
    CHECK(semantics->content->ops[4].opcode == QUANTAPDF_PDF_OP_LINE);
    CHECK(semantics->content->ops[4].arguments[0] == 10);
    CHECK(semantics->content->ops[4].arguments[1] == 20);
    CHECK(semantics->graphics->paths[0].graphics.ctm[4] == 100);
    CHECK(semantics->graphics->paths[0].graphics.fill_rgb[0] == 1);

    quantapdf_graph_snapshot_drop(next);
    CHECK(finalized == 1);
    CHECK(semantics->content->ops[4].arguments[1] == 20);
    quantapdf_pdf_page_semantics_dispose(semantics);
    quantapdf_pdf_page_semantics_dispose(NULL);
}

int main(void)
{
    test_capture(); test_document_families(); test_provider_failure();
    test_commit_detached_metadata();
    test_metadata_compatibility();
    test_stream_snapshot_lifetime();
    test_content_ir_snapshot_version();
    test_page_semantics_arrays();
    return EXIT_SUCCESS;
}
