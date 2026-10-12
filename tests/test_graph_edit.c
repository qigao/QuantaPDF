#include "graph/graph_edit.h"
#include "graph/graph_query.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

static quantapdf_graph_limits limits(void)
{
    quantapdf_graph_limits result = {64, 128, 4096, 4096, 65536, 32};
    return result;
}

static quantapdf_graph *base_graph(void)
{
    quantapdf_graph *g = NULL;
    quantapdf_graph_limits budget = limits();
    quantapdf_local_id id, page = 8;
    quantapdf_validation_report report;
    size_t i;
    OK(quantapdf_graph_create(&budget, &g));
    for (i = 0; i < 9; ++i) OK(quantapdf_graph_reserve_node(g, 1, &id));
    OK(quantapdf_graph_begin_node(g, 1, QUANTAPDF_GRAPH_DICTIONARY, 0));
    OK(quantapdf_graph_append_edge(g, 2, "/Root", 5)); OK(quantapdf_graph_end_node(g));
    OK(quantapdf_graph_begin_node(g, 2, QUANTAPDF_GRAPH_DICTIONARY, 0));
    OK(quantapdf_graph_append_edge(g, 3, "/Pages", 6));
    OK(quantapdf_graph_append_edge(g, 2, "/Self", 5));
    OK(quantapdf_graph_append_edge(g, 4, "/Type", 5)); OK(quantapdf_graph_end_node(g));
    OK(quantapdf_graph_begin_node(g, 3, QUANTAPDF_GRAPH_DICTIONARY, 0));
    OK(quantapdf_graph_append_edge(g, 5, "/Count", 6));
    OK(quantapdf_graph_append_edge(g, 6, "/Kids", 5));
    OK(quantapdf_graph_append_edge(g, 7, "/Type", 5)); OK(quantapdf_graph_end_node(g));
    OK(quantapdf_graph_begin_node(g, 4, QUANTAPDF_GRAPH_NAME, 0));
    OK(quantapdf_graph_append_data(g, "/Catalog", 8)); OK(quantapdf_graph_end_node(g));
    OK(quantapdf_graph_begin_node(g, 5, QUANTAPDF_GRAPH_INTEGER, 1)); OK(quantapdf_graph_end_node(g));
    OK(quantapdf_graph_begin_node(g, 6, QUANTAPDF_GRAPH_ARRAY, 0));
    OK(quantapdf_graph_append_edge(g, 8, NULL, 0)); OK(quantapdf_graph_end_node(g));
    OK(quantapdf_graph_begin_node(g, 7, QUANTAPDF_GRAPH_NAME, 0));
    OK(quantapdf_graph_append_data(g, "/Pages", 6)); OK(quantapdf_graph_end_node(g));
    OK(quantapdf_graph_begin_node(g, 8, QUANTAPDF_GRAPH_DICTIONARY, 0));
    OK(quantapdf_graph_append_edge(g, 3, "/Parent", 7));
    OK(quantapdf_graph_append_edge(g, 9, "/Type", 5)); OK(quantapdf_graph_end_node(g));
    OK(quantapdf_graph_begin_node(g, 9, QUANTAPDF_GRAPH_NAME, 0));
    OK(quantapdf_graph_append_data(g, "/Page", 5)); OK(quantapdf_graph_end_node(g));
    OK(quantapdf_graph_finish(g, 1, 2, &page, 1, &report));
    return g;
}

static quantapdf_graph_change_set selection(const quantapdf_graph *base)
{
    quantapdf_graph_change_set changes = {0};
    changes.lineage = quantapdf_graph_lineage(base);
    changes.base_version = quantapdf_graph_version(base);
    return changes;
}

static void equal_models(const quantapdf_graph *a, const quantapdf_graph *b)
{
    const quantapdf_graph_model *x = quantapdf_graph_view(a), *y = quantapdf_graph_view(b);
    size_t i;
    CHECK(x->node_count == y->node_count && x->edge_count == y->edge_count && x->byte_count == y->byte_count);
    CHECK(x->trailer == y->trailer && x->catalog == y->catalog);
    CHECK(x->page_offset == y->page_offset && x->page_count == y->page_count);
    CHECK(memcmp(x->bytes, y->bytes, x->byte_count) == 0);
    CHECK(memcmp(x->source.pdf_version, y->source.pdf_version, 4) == 0 && x->source.encrypted == y->source.encrypted);
    for (i = 0; i < x->node_count; ++i) {
        CHECK(x->nodes[i].kind == y->nodes[i].kind && x->nodes[i].indirect == y->nodes[i].indirect);
        CHECK(x->nodes[i].integer == y->nodes[i].integer);
        CHECK(x->nodes[i].data_offset == y->nodes[i].data_offset && x->nodes[i].data_size == y->nodes[i].data_size);
        CHECK(x->nodes[i].edge_offset == y->nodes[i].edge_offset && x->nodes[i].edge_count == y->nodes[i].edge_count);
    }
    for (i = 0; i < x->edge_count; ++i) {
        CHECK(x->edges[i].target == y->edges[i].target);
        CHECK(x->edges[i].key_offset == y->edges[i].key_offset && x->edges[i].key_size == y->edges[i].key_size);
    }
}

static quantapdf_graph_reference reference(const quantapdf_graph *g, quantapdf_local_id id)
{
    quantapdf_graph_reference ref;
    OK(quantapdf_graph_reference_at(g, id, &ref));
    CHECK(ref.lineage != 0 && ref.origin != 0 && ref.id == id);
    return ref;
}

static void resolves(const quantapdf_graph *g, const quantapdf_graph_reference *ref)
{
    quantapdf_local_id id = 0;
    OK(quantapdf_graph_resolve(g, ref, &id)); CHECK(id == ref->id);
}

static void rejects(const quantapdf_graph *g, const quantapdf_graph_reference *ref)
{
    quantapdf_local_id id = 99;
    CHECK(quantapdf_graph_resolve(g, ref, &id) == QUANTAPDF_ERROR_ARGUMENT && id == 0);
}

static void test_branches_and_owned_data(void)
{
    quantapdf_graph *base = base_graph(), *foreign = base_graph(), *a = NULL, *b = NULL, *c = NULL;
    quantapdf_graph_limits budget = limits();
    quantapdf_graph_change_set changes = selection(base), next;
    quantapdf_graph_reference original = reference(base, 8), added_a, added_b, last;
    quantapdf_validation_report report;
    unsigned char payload[] = {0, 0xff, 'A'};
    quantapdf_graph_edge_input edges[] = {{10, "/Custom", 7}, {3, "/Parent", 7}, {9, "/Type", 5}};
    quantapdf_graph_edge_input cycle[] = {{10, NULL, 0}, {11, NULL, 0}};
    quantapdf_graph_replacement replace = {8, {QUANTAPDF_GRAPH_DICTIONARY, 1, 0, NULL, 0, edges, 3}};
    quantapdf_graph_node_input additions[] = {
        {QUANTAPDF_GRAPH_STRING, 0, 0, payload, sizeof(payload), NULL, 0},
        {QUANTAPDF_GRAPH_ARRAY, 1, 0, NULL, 0, cycle, 2}
    };
    quantapdf_graph_node_input boolean = {QUANTAPDF_GRAPH_BOOL, 0, 1, NULL, 0, NULL, 0};
    const quantapdf_graph_model *m;
    size_t i;
    changes.replacements = &replace; changes.replacement_count = 1;
    changes.additions = additions; changes.addition_count = 2;
    OK(quantapdf_graph_commit(base, &changes, &budget, &a, &report));
    OK(quantapdf_graph_commit(base, &changes, &budget, &b, &report));
    CHECK(quantapdf_graph_version(a) != quantapdf_graph_version(b));
    CHECK(quantapdf_graph_lineage(a) == quantapdf_graph_lineage(base));
    equal_models(a, b); equal_models(base, foreign);
    resolves(a, &original); resolves(b, &original); rejects(foreign, &original);
    added_a = reference(a, 10); added_b = reference(b, 10);
    CHECK(added_a.id == added_b.id && added_a.origin != added_b.origin);
    rejects(b, &added_a); rejects(a, &added_b); rejects(base, &added_a);
    {
        quantapdf_graph_reference array_ref = reference(a, 11);
        quantapdf_graph_cursor cursor;
        CHECK(quantapdf_graph_query_members(b, &array_ref, &cursor) == QUANTAPDF_ERROR_ARGUMENT);
        CHECK(cursor.graph == NULL);
        OK(quantapdf_graph_query_members(a, &array_ref, &cursor));
    }
    next = selection(a); next.additions = &boolean; next.addition_count = 1;
    OK(quantapdf_graph_commit(a, &next, &budget, &c, &report));
    last = reference(c, 12); rejects(a, &last);
    for (i = 1; i <= 11; ++i) {
        quantapdf_graph_reference inherited = reference(a, (quantapdf_local_id)i);
        resolves(c, &inherited);
    }
    payload[0] = 42; edges[0].target = 2; cycle[0].target = 2;
    quantapdf_graph_drop(base); quantapdf_graph_drop(a); quantapdf_graph_drop(b); quantapdf_graph_drop(foreign);
    resolves(c, &original); resolves(c, &added_a); resolves(c, &last);
    m = quantapdf_graph_view(c);
    CHECK(m->nodes[9].data_size == 3 && m->bytes[m->nodes[9].data_offset] == 0);
    CHECK(quantapdf_graph_find(m, 8, "/Custom", 7)->target == 10);
    CHECK(m->edges[m->nodes[10].edge_offset].target == 10);
    CHECK(m->edges[m->nodes[10].edge_offset + 1].target == 11);
    CHECK(m->nodes[11].integer == 1);
    OK(quantapdf_validate_graph(m, &report));
    CHECK(quantapdf_graph_storage_bytes(c) <= budget.max_storage_bytes);
    quantapdf_graph_drop(c);
}

static void expect_failure(const quantapdf_graph *base, const quantapdf_graph_change_set *changes,
    const quantapdf_graph_limits *budget, quantapdf_status expected)
{
    quantapdf_graph *result = (quantapdf_graph *)(uintptr_t)1;
    quantapdf_validation_report report;
    memset(&report, 0x5a, sizeof(report));
    CHECK(quantapdf_graph_commit(base, changes, budget, &result, &report) == expected);
    CHECK(result == NULL);
    if (expected != QUANTAPDF_ERROR_BACKEND)
        CHECK(report.rule == QUANTAPDF_VALIDATION_OK && report.index == 0 && report.detail == 0);
}

static void test_failures_and_retry(void)
{
    quantapdf_graph *base = base_graph(), *copy = NULL, *empty = NULL;
    quantapdf_graph_limits budget = limits(), small;
    quantapdf_graph_change_set changes = selection(base);
    quantapdf_validation_report report;
    quantapdf_graph_reference ref = reference(base, 8), failed_ref = {1, 1, 1};
    quantapdf_graph_replacement replacements[] = {
        {7, {QUANTAPDF_GRAPH_NAME, 1, 0, "/Pages", 6, NULL, 0}},
        {9, {QUANTAPDF_GRAPH_NAME, 1, 0, "/Wrong", 6, NULL, 0}}
    };
    quantapdf_graph_node_input addition = {QUANTAPDF_GRAPH_NULL, 0, 0, NULL, 0, NULL, 0};
    size_t owner_size;
    OK(quantapdf_graph_create(&budget, &empty));
    owner_size = quantapdf_graph_storage_bytes(empty);
    CHECK(quantapdf_graph_lineage(empty) == 0 && quantapdf_graph_version(empty) == 0);
    CHECK(quantapdf_graph_reference_at(empty, 1, &failed_ref) == QUANTAPDF_ERROR_STATE);
    CHECK(failed_ref.lineage == 0 && failed_ref.origin == 0 && failed_ref.id == 0);
    expect_failure(empty, &changes, &budget, QUANTAPDF_ERROR_STATE);
    quantapdf_graph_drop(empty);
    expect_failure(NULL, &changes, &budget, QUANTAPDF_ERROR_ARGUMENT);
    expect_failure(base, NULL, &budget, QUANTAPDF_ERROR_ARGUMENT);
    expect_failure(base, &changes, NULL, QUANTAPDF_ERROR_ARGUMENT);
    changes.lineage = 0; expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_STATE);
    changes = selection(base); changes.base_version = 0;
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_STATE);
    changes = selection(base); changes.replacement_count = 1;
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_ARGUMENT);
    changes.replacements = replacements; changes.replacement_count = 2;
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_BACKEND);
    replacements[1].id = 7;
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_ARGUMENT);
    replacements[1].id = 6;
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_ARGUMENT);
    replacements[1].id = 10;
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_ARGUMENT);
    replacements[1].id = 9; replacements[1].value.data = "/Page"; replacements[1].value.data_size = 5;
    small = budget; small.max_storage_bytes = owner_size;
    expect_failure(base, &changes, &small, QUANTAPDF_ERROR_UNSUPPORTED);
    small = budget; small.max_value_bytes = 1;
    expect_failure(base, &changes, &small, QUANTAPDF_ERROR_UNSUPPORTED);
    small = budget; small.max_edges = 0;
    expect_failure(base, &changes, &small, QUANTAPDF_ERROR_UNSUPPORTED);
    small = budget; small.max_nodes = 8;
    expect_failure(base, &changes, &small, QUANTAPDF_ERROR_UNSUPPORTED);
    changes.additions = &addition; changes.addition_count = 1;
    small.max_nodes = 9;
    expect_failure(base, &changes, &small, QUANTAPDF_ERROR_UNSUPPORTED);
    changes.addition_count = 0; changes.additions = NULL;
    OK(quantapdf_graph_commit(base, &changes, &budget, &copy, &report));
    equal_models(base, copy); resolves(base, &ref); resolves(copy, &ref);
    CHECK(report.rule == QUANTAPDF_VALIDATION_OK);
    expect_failure(copy, &changes, &budget, QUANTAPDF_ERROR_STATE);
    quantapdf_graph_drop(base);
    changes = selection(copy);
    OK(quantapdf_graph_commit(copy, &changes, &budget, &base, &report));
    equal_models(base, copy); resolves(base, &ref);
    quantapdf_graph_drop(copy); quantapdf_graph_drop(base);
}

static void test_root_page_batch(void)
{
    quantapdf_graph *base = base_graph(), *result = NULL;
    quantapdf_graph_limits budget = limits();
    quantapdf_graph_change_set changes = selection(base);
    quantapdf_validation_report report;
    quantapdf_local_id pages[] = {10, 8};
    quantapdf_graph_edge_input kids[] = {{10, NULL, 0}, {8, NULL, 0}};
    quantapdf_graph_edge_input page[] = {{3, "/Parent", 7}, {9, "/Type", 5}};
    quantapdf_graph_replacement replacements[] = {
        {5, {QUANTAPDF_GRAPH_INTEGER, 1, 2, NULL, 0, NULL, 0}},
        {6, {QUANTAPDF_GRAPH_ARRAY, 1, 0, NULL, 0, kids, 2}}
    };
    quantapdf_graph_node_input addition = {QUANTAPDF_GRAPH_DICTIONARY, 1, 0, NULL, 0, page, 2};
    const quantapdf_graph_model *model;
    changes.replacements = replacements; changes.replacement_count = 2;
    changes.additions = &addition; changes.addition_count = 1;
    changes.replace_roots = 1; changes.trailer = 1; changes.catalog = 2;
    changes.pages = pages; changes.page_count = 2;
    pages[0] = 3;
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_BACKEND);
    pages[0] = 10;
    pages[1] = 10;
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_BACKEND);
    pages[0] = 8; pages[1] = 10;
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_BACKEND);
    pages[0] = 10; pages[1] = 8;
    OK(quantapdf_graph_commit(base, &changes, &budget, &result, &report));
    model = quantapdf_graph_view(result);
    CHECK(model->page_count == 2 && model->edges[model->page_offset].target == 10);
    CHECK(model->edges[model->page_offset + 1].target == 8);
    CHECK(model->nodes[4].integer == 2);
    CHECK(model->edges[model->nodes[5].edge_offset].target == 10);
    {
        quantapdf_graph_cursor cursor;
        quantapdf_graph_match match;
        quantapdf_graph_reference kids_ref = reference(result, 6);
        int found;
        size_t i;
        OK(quantapdf_graph_query_pages(result, &cursor));
        for (i = 0; i < 2; ++i) {
            OK(quantapdf_graph_query_next(&cursor, &match, &found));
            CHECK(found && match.reference.id == pages[i] && match.ordinal == i);
        }
        OK(quantapdf_graph_query_members(result, &kids_ref, &cursor));
        for (i = 0; i < 2; ++i) {
            OK(quantapdf_graph_query_next(&cursor, &match, &found));
            CHECK(found && match.reference.id == pages[i] && match.owner.id == 6);
            CHECK(match.name == NULL && match.name_size == 0 && match.ordinal == i);
        }
    }
    CHECK(quantapdf_graph_view(base)->page_count == 1 && quantapdf_graph_view(base)->nodes[4].integer == 1);
    quantapdf_graph_drop(base); quantapdf_graph_drop(result);
}

static void test_page_tree_admission(void)
{
    quantapdf_graph *base = base_graph(), *result = NULL;
    quantapdf_graph_limits budget = limits();
    quantapdf_graph_change_set changes = selection(base);
    quantapdf_validation_report report;
    quantapdf_graph_edge_input kids[] = {{8, NULL, 0}, {8, NULL, 0}};
    quantapdf_graph_replacement replacement =
        {6, {QUANTAPDF_GRAPH_ARRAY, 1, 0, NULL, 0, kids, 2}};
    quantapdf_graph_edge_input page[] = {{3, "/Parent", 7}, {9, "/Type", 5}};
    quantapdf_local_id pages[] = {8, 8};
    unsigned char seen[9];
    quantapdf_validation_page_frame stack[2];
    const quantapdf_graph_model *model = quantapdf_graph_view(base);

    /* The existing /Self and /Parent cycles are outside the page-tree walk. */
    OK(quantapdf_validate_graph_pages(model, seen, 9, stack, 2, &report));
    CHECK(quantapdf_validate_graph_pages(model, seen, 8, stack, 2, &report) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_validate_graph_pages(model, seen, 9, stack, 1, &report) == QUANTAPDF_ERROR_UNSUPPORTED);
    budget.max_depth = 1;
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_UNSUPPORTED);
    budget.max_depth = 2;

    /* Missing and extra index entries are rejected even when each ID is valid. */
    changes.replace_roots = 1; changes.trailer = 1; changes.catalog = 2;
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_BACKEND);
    changes.pages = pages; changes.page_count = 2;
    CHECK(quantapdf_graph_commit(base, &changes, &budget, &result, &report) == QUANTAPDF_ERROR_BACKEND);
    CHECK(result == NULL && report.subject == QUANTAPDF_VALIDATION_PAGE && report.index == 1);
    changes.page_count = 1;

    changes.replacements = &replacement; changes.replacement_count = 1;
    CHECK(quantapdf_graph_commit(base, &changes, &budget, &result, &report) == QUANTAPDF_ERROR_BACKEND);
    CHECK(result == NULL && report.subject == QUANTAPDF_VALIDATION_GRAPH_NODE && report.index == 7);
    replacement.value.edge_count = 1; kids[0].target = 3;
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_BACKEND);
    kids[0].target = 5;
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_BACKEND);
    replacement.value.kind = QUANTAPDF_GRAPH_INTEGER;
    replacement.value.edge_count = 0;
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_BACKEND);
    replacement.id = 7; replacement.value.kind = QUANTAPDF_GRAPH_NAME;
    replacement.value.data = "/Wrong"; replacement.value.data_size = 6;
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_BACKEND);
    replacement.id = 8;
    replacement.value = (quantapdf_graph_node_input){QUANTAPDF_GRAPH_DICTIONARY, 0, 0, NULL, 0, page, 2};
    expect_failure(base, &changes, &budget, QUANTAPDF_ERROR_BACKEND);

    changes = selection(base);
    OK(quantapdf_graph_commit(base, &changes, &budget, &result, &report));
    equal_models(base, result);
    CHECK(report.rule == QUANTAPDF_VALIDATION_OK);
    quantapdf_graph_drop(base); quantapdf_graph_drop(result);
}

static void test_stream_values_and_noop_history(void)
{
    quantapdf_graph *base = base_graph(), *result = NULL, *next = NULL;
    quantapdf_graph_limits budget = limits(), small;
    quantapdf_graph_change_set changes = selection(base);
    quantapdf_validation_report report;
    const unsigned char name[] = {'/', 0xff};
    unsigned char data[] = {1, 0, 0xfe};
    quantapdf_graph_edge_input stream[] = {{11, "/Type", 5}};
    quantapdf_graph_node_input additions[] = {
        {QUANTAPDF_GRAPH_REAL, 0, 0, "+01.500", 7, NULL, 0},
        {QUANTAPDF_GRAPH_NAME, 0, 0, name, sizeof(name), NULL, 0},
        {QUANTAPDF_GRAPH_STREAM, 1, 0, data, sizeof(data), stream, 1},
        {QUANTAPDF_GRAPH_STRING, 0, 0, NULL, 0, NULL, 0}
    };
    quantapdf_graph_reference ref;
    const quantapdf_graph_model *model;
    size_t storage, i;
    changes.additions = additions; changes.addition_count = 4;
    small = budget; small.max_stream_bytes = sizeof(data) - 1;
    expect_failure(base, &changes, &small, QUANTAPDF_ERROR_UNSUPPORTED);
    small.max_stream_bytes = sizeof(data);
    small.max_value_bytes = quantapdf_graph_view(base)->byte_count + 7 + sizeof(name) + 5;
    OK(quantapdf_graph_commit(base, &changes, &small, &result, &report));
    data[0] = 99;
    model = quantapdf_graph_view(result);
    CHECK(model->nodes[9].data_size == 7 && memcmp(model->bytes + model->nodes[9].data_offset, "+01.500", 7) == 0);
    CHECK(model->nodes[11].data_size == 3 && model->bytes[model->nodes[11].data_offset] == 1);
    CHECK(model->nodes[12].data_size == 0);
    ref = reference(result, 12);
    quantapdf_graph_drop(base);
    changes = selection(result);
    OK(quantapdf_graph_commit(result, &changes, &budget, &next, &report));
    equal_models(result, next);
    quantapdf_graph_drop(result); result = next;
    storage = quantapdf_graph_storage_bytes(result);
    for (i = 0; i < 16; ++i) {
        uint64_t previous = quantapdf_graph_version(result);
        changes = selection(result);
        OK(quantapdf_graph_commit(result, &changes, &budget, &next, &report));
        CHECK(quantapdf_graph_version(next) > previous);
        CHECK(quantapdf_graph_storage_bytes(next) == storage);
        equal_models(result, next); resolves(next, &ref);
        quantapdf_graph_drop(result); result = next;
    }
    quantapdf_graph_drop(result);
}

static void test_metadata_queries(void)
{
    quantapdf_graph *base = base_graph(), *version = NULL, *empty = NULL;
    quantapdf_graph_limits budget = limits();
    quantapdf_graph_change_set changes = selection(base);
    quantapdf_validation_report report;
    quantapdf_graph_text_view view;
    quantapdf_graph_edge_input trailer[] = {{10, "/Info", 5}, {2, "/Root", 5}};
    quantapdf_graph_edge_input info[] = {{11, "/Title", 6}};
    quantapdf_graph_replacement replacement = {1,
        {QUANTAPDF_GRAPH_DICTIONARY, 1, 0, NULL, 0, trailer, 2}};
    unsigned char bytes[] = {0xfe, 0xff, 0, 'A', 0, 0};
    quantapdf_graph_node_input additions[] = {
        {QUANTAPDF_GRAPH_DICTIONARY, 1, 0, NULL, 0, info, 1},
        {QUANTAPDF_GRAPH_STRING, 0, 0, bytes, sizeof(bytes), NULL, 0}
    };
    int scenario;
    OK(quantapdf_graph_query_metadata(base, QUANTAPDF_METADATA_TITLE, &view));
    CHECK(!view.present && view.data == NULL && view.size == 0);
    OK(quantapdf_graph_create(&budget, &empty));
    CHECK(quantapdf_graph_query_metadata(empty, QUANTAPDF_METADATA_TITLE, &view) == QUANTAPDF_ERROR_STATE);
    quantapdf_graph_drop(empty);
    changes.replacements = &replacement; changes.replacement_count = 1;
    changes.additions = additions; changes.addition_count = 2;
    for (scenario = 0; scenario < 6; ++scenario) {
        if (scenario == 1) { additions[1].data_size = 0; additions[1].data = NULL; }
        if (scenario == 2) additions[1].kind = QUANTAPDF_GRAPH_NULL;
        if (scenario == 3) additions[1].kind = QUANTAPDF_GRAPH_INTEGER;
        if (scenario == 4) { additions[0].kind = QUANTAPDF_GRAPH_NULL; additions[0].edge_count = 0; }
        if (scenario == 5) additions[0].kind = QUANTAPDF_GRAPH_INTEGER;
        OK(quantapdf_graph_commit(base, &changes, &budget, &version, &report));
        memset(&view, 0xa5, sizeof(view));
        if (scenario == 3 || scenario == 5) {
            CHECK(quantapdf_graph_query_metadata(version, QUANTAPDF_METADATA_TITLE, &view) == QUANTAPDF_ERROR_FORMAT);
            CHECK(!view.present && view.data == NULL && view.size == 0);
        } else {
            OK(quantapdf_graph_query_metadata(version, QUANTAPDF_METADATA_TITLE, &view));
            CHECK(view.present == (scenario < 2));
            if (scenario == 0) CHECK(view.size == sizeof(bytes) && memcmp(view.data, bytes, sizeof(bytes)) == 0);
            else CHECK(view.data == NULL && view.size == 0);
        }
        CHECK(quantapdf_graph_query_metadata(version, (quantapdf_metadata_field)99, &view) == QUANTAPDF_ERROR_ARGUMENT);
        CHECK(!view.present && view.data == NULL && view.size == 0);
        quantapdf_graph_drop(version);
    }
    quantapdf_graph_drop(base);
}

static void test_query_cursors_and_resources(void)
{
    quantapdf_graph *base = base_graph(), *g = NULL, *other = base_graph(), *changed = NULL;
    quantapdf_graph_limits budget = limits();
    quantapdf_graph_change_set changes = selection(base);
    quantapdf_graph_cursor cursor, bookmark, uninitialized = {0};
    quantapdf_graph_match match, inherited;
    quantapdf_validation_report report;
    quantapdf_graph_reference root, resource, foreign = reference(other, 3);
    int found;
    size_t i;
    quantapdf_graph_edge_input pages[] = {{5, "/Count", 6}, {6, "/Kids", 5}, {10, "/Resources", 10}, {7, "/Type", 5}};
    quantapdf_graph_edge_input resources[] = {{11, "/Font", 5}, {12, "/XObject", 8}, {13, "/\x80", 2}};
    quantapdf_graph_edge_input fonts[] = {{14, "/F1", 3}};
    quantapdf_graph_edge_input self[] = {{14, "/Self", 5}};
    quantapdf_graph_node_input additions[] = {
        {QUANTAPDF_GRAPH_DICTIONARY, 1, 0, NULL, 0, resources, 3},
        {QUANTAPDF_GRAPH_DICTIONARY, 0, 0, NULL, 0, fonts, 1},
        {QUANTAPDF_GRAPH_DICTIONARY, 0, 0, NULL, 0, NULL, 0},
        {QUANTAPDF_GRAPH_NULL, 0, 0, NULL, 0, NULL, 0},
        {QUANTAPDF_GRAPH_DICTIONARY, 1, 0, NULL, 0, self, 1},
        {QUANTAPDF_GRAPH_BOOL, 0, 1, NULL, 0, NULL, 0} /* disconnected stored value */
    };
    quantapdf_graph_replacement replacement = {3, {QUANTAPDF_GRAPH_DICTIONARY, 1, 0, NULL, 0, pages, 4}};
    changes.replacements = &replacement; changes.replacement_count = 1;
    changes.additions = additions; changes.addition_count = 6;
    OK(quantapdf_graph_commit(base, &changes, &budget, &g, &report));
    CHECK(quantapdf_graph_query_next(&uninitialized, &match, &found) == QUANTAPDF_ERROR_STATE);
    CHECK(!found && match.node == NULL && match.reference.id == 0);
    OK(quantapdf_graph_query_objects(g, &cursor));
    found = 99;
    CHECK(quantapdf_graph_query_next(&cursor, NULL, &found) == QUANTAPDF_ERROR_ARGUMENT && !found);
    for (i = 0; i < 15; ++i) {
        OK(quantapdf_graph_query_next(&cursor, &match, &found));
        CHECK(found && match.reference.id == i + 1 && match.ordinal == i);
        CHECK(match.owner.id == 0 && match.name == NULL && match.name_size == 0);
        CHECK(match.node == &quantapdf_graph_view(g)->nodes[i]);
        if (i == 2) bookmark = cursor;
    }
    for (i = 0; i < 2; ++i) {
        OK(quantapdf_graph_query_next(&cursor, &match, &found));
        CHECK(!found && match.node == NULL && match.reference.id == 0);
    }
    OK(quantapdf_graph_query_next(&bookmark, &match, &found)); CHECK(found && match.reference.id == 4);
    OK(quantapdf_graph_query_pages(g, &cursor));
    OK(quantapdf_graph_query_next(&cursor, &match, &found)); CHECK(found && match.reference.id == 8 && match.ordinal == 0);
    CHECK(quantapdf_graph_query_resources(g, 0, 1, &match, &found) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(!found && match.node == NULL);
    OK(quantapdf_graph_query_resources(g, 0, 2, &inherited, &found));
    CHECK(found && inherited.reference.id == 10 && inherited.owner.id == 3);
    resource = inherited.reference;
    OK(quantapdf_graph_query_members(g, &resource, &cursor));
    for (i = 0; i < 3; ++i) {
        OK(quantapdf_graph_query_next(&cursor, &match, &found));
        CHECK(found && match.owner.id == 10 && match.ordinal == i && match.reference.id == 11 + i);
        CHECK(match.name_size == resources[i].key_size && memcmp(match.name, resources[i].key, match.name_size) == 0);
    }
    OK(quantapdf_graph_query_next(&cursor, &match, &found)); CHECK(!found);
    OK(quantapdf_graph_query_member(g, &resource, "/\x80", 2, &match, &found));
    CHECK(found && match.node->kind == QUANTAPDF_GRAPH_NULL);
    OK(quantapdf_graph_query_member(g, &resource, "/Font", 5, &match, &found)); CHECK(found);
    OK(quantapdf_graph_query_member(g, &match.reference, "/F1", 3, &match, &found));
    CHECK(found && match.reference.id == 14 && match.owner.id == 11);
    OK(quantapdf_graph_query_member(g, &resource, "/Missing", 8, &match, &found)); CHECK(!found && match.reference.id == 0);
    CHECK(quantapdf_graph_query_members(g, &foreign, &cursor) == QUANTAPDF_ERROR_ARGUMENT && cursor.graph == NULL);
    CHECK(quantapdf_graph_query_member(g, &foreign, "/Resources", 10, &match, &found) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(!found && match.reference.id == 0);

    /* A nearer empty dictionary replaces inherited resources as a whole. */
    changes = selection(g);
    {
        quantapdf_graph_edge_input local[] = {{3, "/Parent", 7}, {12, "/Resources", 10}, {9, "/Type", 5}};
        replacement = (quantapdf_graph_replacement){8, {QUANTAPDF_GRAPH_DICTIONARY, 1, 0, NULL, 0, local, 3}};
        changes.replacements = &replacement; changes.replacement_count = 1;
        OK(quantapdf_graph_commit(g, &changes, &budget, &changed, &report));
        OK(quantapdf_graph_query_resources(changed, 0, 1, &match, &found));
        CHECK(found && match.reference.id == 12 && match.owner.id == 8 && match.node->edge_count == 0);
        resolves(changed, &resource); quantapdf_graph_drop(changed);
        local[1].target = 13;
        OK(quantapdf_graph_commit(g, &changes, &budget, &changed, &report));
        OK(quantapdf_graph_query_resources(changed, 0, 2, &match, &found));
        CHECK(found && match.reference.id == resource.id && match.owner.id == 3);
        quantapdf_graph_drop(changed);
        local[1].target = 5;
        OK(quantapdf_graph_commit(g, &changes, &budget, &changed, &report));
        CHECK(quantapdf_graph_query_resources(changed, 0, 2, &match, &found) == QUANTAPDF_ERROR_FORMAT);
        CHECK(!found && match.reference.id == 0); quantapdf_graph_drop(changed);
    }
    /* No resource dictionary plus a cyclic parent chain is bounded. */
    {
        quantapdf_graph_edge_input cycle[] = {{5, "/Count", 6}, {6, "/Kids", 5}, {3, "/Parent", 7}, {7, "/Type", 5}};
        replacement = (quantapdf_graph_replacement){3, {QUANTAPDF_GRAPH_DICTIONARY, 1, 0, NULL, 0, cycle, 4}};
        changes = selection(g); changes.replacements = &replacement; changes.replacement_count = 1;
        OK(quantapdf_graph_commit(g, &changes, &budget, &changed, &report));
        CHECK(quantapdf_graph_query_resources(changed, 0, 3, &match, &found) == QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(quantapdf_graph_query_resources(changed, 0, 100, &match, &found) == QUANTAPDF_ERROR_FORMAT);
        CHECK(!found && match.node == NULL); quantapdf_graph_drop(changed);
    }
    OK(quantapdf_graph_query_resources(base, 0, 2, &match, &found)); CHECK(!found);
    CHECK(quantapdf_graph_query_resources(g, 1, 2, &match, &found) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_graph_query_resources(g, 0, 0, &match, &found) == QUANTAPDF_ERROR_ARGUMENT);
    root = reference(g, 3);
    changes = selection(g); OK(quantapdf_graph_commit(g, &changes, &budget, &changed, &report));
    quantapdf_graph_drop(base); quantapdf_graph_drop(other); quantapdf_graph_drop(g);
    OK(quantapdf_graph_query_member(changed, &root, "/Resources", 10, &match, &found));
    CHECK(found && match.reference.origin == resource.origin);
    quantapdf_graph_drop(changed);
}

int main(void)
{
    test_branches_and_owned_data();
    test_failures_and_retry();
    test_root_page_batch();
    test_page_tree_admission();
    test_stream_values_and_noop_history();
    test_metadata_queries();
    test_query_cursors_and_resources();
    return EXIT_SUCCESS;
}
