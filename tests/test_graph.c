#include "graph/graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

static quantapdf_graph_limits limits(void)
{
    quantapdf_graph_limits value = {64, 128, 4096, 4096, 65536, 32};
    return value;
}

static quantapdf_graph *build(void)
{
    quantapdf_graph_limits budget = limits();
    quantapdf_graph *g = NULL;
    quantapdf_local_id id;
    size_t i;
    const unsigned char binary[] = {0, 0xff, 'A'};
    OK(quantapdf_graph_create(&budget, &g));
    for (i = 0; i < 7; ++i) { OK(quantapdf_graph_reserve_node(g, 1, &id)); CHECK(id == i + 1); }
    CHECK(quantapdf_graph_view(g) == NULL);
    CHECK(quantapdf_graph_begin_node(g, 2, QUANTAPDF_GRAPH_NULL, 0) == QUANTAPDF_ERROR_STATE);
    OK(quantapdf_graph_begin_node(g, 1, QUANTAPDF_GRAPH_DICTIONARY, 0));
    OK(quantapdf_graph_append_edge(g, 2, "/Root", 5));
    OK(quantapdf_graph_end_node(g));
    OK(quantapdf_graph_begin_node(g, 2, QUANTAPDF_GRAPH_DICTIONARY, 0));
    OK(quantapdf_graph_append_edge(g, 2, "/Loop", 5));
    OK(quantapdf_graph_append_edge(g, 3, "/Payload", 8));
    OK(quantapdf_graph_append_edge(g, 4, "/Type", 5));
    OK(quantapdf_graph_end_node(g));
    OK(quantapdf_graph_begin_node(g, 3, QUANTAPDF_GRAPH_STREAM, 0));
    OK(quantapdf_graph_append_data(g, binary, sizeof(binary)));
    OK(quantapdf_graph_append_edge(g, 3, "/Self", 5));
    OK(quantapdf_graph_append_edge(g, 5, "/Type", 5));
    CHECK(quantapdf_graph_append_data(g, binary, 1) == QUANTAPDF_ERROR_STATE);
    OK(quantapdf_graph_end_node(g));
    OK(quantapdf_graph_begin_node(g, 4, QUANTAPDF_GRAPH_NAME, 0));
    OK(quantapdf_graph_append_data(g, "/Catalog", 8)); OK(quantapdf_graph_end_node(g));
    OK(quantapdf_graph_begin_node(g, 5, QUANTAPDF_GRAPH_NAME, 0));
    OK(quantapdf_graph_append_data(g, "/Blob", 5)); OK(quantapdf_graph_end_node(g));
    OK(quantapdf_graph_begin_node(g, 6, QUANTAPDF_GRAPH_DICTIONARY, 0));
    OK(quantapdf_graph_append_edge(g, 2, "/Resources", 10));
    OK(quantapdf_graph_append_edge(g, 7, "/Type", 5)); OK(quantapdf_graph_end_node(g));
    OK(quantapdf_graph_begin_node(g, 7, QUANTAPDF_GRAPH_NAME, 0));
    OK(quantapdf_graph_append_data(g, "/Page", 5)); OK(quantapdf_graph_end_node(g));
    return g;
}

static void test_model(void)
{
    quantapdf_graph *g = build();
    quantapdf_validation_report report;
    quantapdf_local_id page = 6, id = 99;
    const quantapdf_graph_model *m;
    quantapdf_graph_model bad;
    quantapdf_graph_node nodes[7];
    quantapdf_graph_edge edges[9];
    CHECK(quantapdf_graph_finish(g, 0, 2, &page, 1, &report) == QUANTAPDF_ERROR_BACKEND);
    CHECK(report.rule == QUANTAPDF_VALIDATION_REFERENCE && quantapdf_graph_view(g) == NULL);
    OK(quantapdf_graph_finish(g, 1, 2, &page, 1, &report));
    CHECK(report.rule == QUANTAPDF_VALIDATION_OK);
    m = quantapdf_graph_view(g);
    CHECK(m != NULL && m->node_count == 7 && m->edge_count == 9 && m->page_count == 1);
    CHECK(quantapdf_graph_find(m, 2, "/Loop", 5)->target == 2);
    CHECK(quantapdf_graph_find(m, 3, "/Self", 5)->target == 3);
    CHECK(m->bytes[m->nodes[2].data_offset] == 0 && m->bytes[m->nodes[2].data_offset + 1] == 0xff);
    CHECK(quantapdf_graph_reserve_node(g, 0, &id) == QUANTAPDF_ERROR_STATE && id == 0);
    CHECK(quantapdf_graph_begin_node(g, 1, QUANTAPDF_GRAPH_NULL, 0) == QUANTAPDF_ERROR_STATE);
    CHECK(quantapdf_graph_finish(g, 1, 2, &page, 1, &report) == QUANTAPDF_ERROR_STATE);
    bad = *m; memcpy(nodes, m->nodes, sizeof(nodes)); memcpy(edges, m->edges, sizeof(edges));
    bad.nodes = nodes; bad.edges = edges;
    nodes[2].data_offset = SIZE_MAX;
    CHECK(quantapdf_validate_graph(&bad, &report) == QUANTAPDF_ERROR_BACKEND);
    CHECK(report.rule == QUANTAPDF_VALIDATION_STORAGE && report.index == 2);
    nodes[2] = m->nodes[2]; edges[1].target = 99;
    CHECK(quantapdf_validate_graph(&bad, &report) == QUANTAPDF_ERROR_BACKEND);
    CHECK(report.rule == QUANTAPDF_VALIDATION_REFERENCE && report.detail == 99);
    edges[1] = m->edges[1]; edges[8].target = 2;
    CHECK(quantapdf_validate_graph(&bad, &report) == QUANTAPDF_ERROR_BACKEND);
    CHECK(report.subject == QUANTAPDF_VALIDATION_PAGE);
    CHECK(quantapdf_validate_graph(NULL, &report) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(report.rule == QUANTAPDF_VALIDATION_OK);
    quantapdf_graph_drop(g);
}

static void test_source(void)
{
    quantapdf_graph *g = build();
    quantapdf_graph_source source = {"2.0", 1};
    quantapdf_validation_report report;
    quantapdf_local_id page = 6;
    quantapdf_graph_model bad;
    source.pdf_version[3] = 'X';
    CHECK(quantapdf_graph_set_source(g, &source) == QUANTAPDF_ERROR_ARGUMENT);
    source.pdf_version[3] = 0; source.encrypted = 2;
    CHECK(quantapdf_graph_set_source(g, &source) == QUANTAPDF_ERROR_ARGUMENT);
    source.encrypted = 1;
    OK(quantapdf_graph_set_source(g, &source));
    source.pdf_version[0] = '1';
    OK(quantapdf_graph_finish(g, 1, 2, &page, 1, &report));
    CHECK(strcmp(quantapdf_graph_view(g)->source.pdf_version, "2.0") == 0);
    CHECK(quantapdf_graph_view(g)->source.encrypted == 1);
    CHECK(quantapdf_graph_set_source(g, &source) == QUANTAPDF_ERROR_STATE);
    bad = *quantapdf_graph_view(g); bad.source.pdf_version[1] = 'X';
    CHECK(quantapdf_validate_graph(&bad, &report) == QUANTAPDF_ERROR_BACKEND);
    CHECK(report.rule == QUANTAPDF_VALIDATION_VALUE && report.subject == QUANTAPDF_VALIDATION_MODEL);
    quantapdf_graph_drop(g);
}

static void test_report_storage_independence(void)
{
    quantapdf_graph *graphs[2] = {build(), build()};
    quantapdf_graph_model models[2];
    quantapdf_graph_edge edges[2][9];
    quantapdf_validation_report report;
    quantapdf_local_id page = 6;
    size_t i;
    for (i = 0; i < 2; ++i) {
        OK(quantapdf_graph_finish(graphs[i], 1, 2, &page, 1, &report));
        models[i] = *quantapdf_graph_view(graphs[i]);
        memcpy(edges[i], models[i].edges, sizeof(edges[i]));
        models[i].edges = edges[i];
        edges[i][1].target = 99;
        models[i].catalog = 0;
    }
    CHECK(models[0].nodes != models[1].nodes && models[0].bytes != models[1].bytes);
    for (i = 0; i < 2; ++i) {
        memset(&report, i == 0 ? 0xa5 : 0x5a, sizeof(report));
        CHECK(quantapdf_validate_graph(&models[i], &report) == QUANTAPDF_ERROR_BACKEND);
        CHECK(report.rule == QUANTAPDF_VALIDATION_REFERENCE && report.subject == QUANTAPDF_VALIDATION_GRAPH_EDGE);
        CHECK(report.index == 1 && report.detail == 99);
        edges[i][1] = quantapdf_graph_view(graphs[i])->edges[1];
        CHECK(quantapdf_validate_graph(&models[i], &report) == QUANTAPDF_ERROR_BACKEND);
        CHECK(report.rule == QUANTAPDF_VALIDATION_REFERENCE && report.subject == QUANTAPDF_VALIDATION_GRAPH_ROOT);
        CHECK(report.index == 1 && report.detail == 0);
        models[i].catalog = 2;
        OK(quantapdf_validate_graph(&models[i], &report));
        CHECK(report.rule == QUANTAPDF_VALIDATION_OK && report.subject == QUANTAPDF_VALIDATION_MODEL);
        CHECK(report.index == 0 && report.detail == 0);
    }
    quantapdf_graph_drop(graphs[0]); quantapdf_graph_drop(graphs[1]);
}

static void test_limits(void)
{
    quantapdf_graph_limits budget = limits();
    quantapdf_graph *g = NULL;
    quantapdf_local_id id;
    size_t base;
    OK(quantapdf_graph_create(&budget, &g)); base = quantapdf_graph_storage_bytes(g); quantapdf_graph_drop(g);
    budget.max_storage_bytes = base + sizeof(quantapdf_graph_node);
    OK(quantapdf_graph_create(&budget, &g));
    OK(quantapdf_graph_reserve_node(g, 0, &id)); CHECK(id == 1);
    CHECK(quantapdf_graph_storage_bytes(g) == budget.max_storage_bytes);
    CHECK(quantapdf_graph_reserve_node(g, 0, &id) == QUANTAPDF_ERROR_UNSUPPORTED && id == 0);
    quantapdf_graph_drop(g);
    budget = limits(); budget.max_nodes = 1; budget.max_value_bytes = 1; budget.max_stream_bytes = 1;
    OK(quantapdf_graph_create(&budget, &g)); OK(quantapdf_graph_reserve_node(g, 0, &id));
    CHECK(quantapdf_graph_reserve_node(g, 0, &id) == QUANTAPDF_ERROR_UNSUPPORTED);
    OK(quantapdf_graph_begin_node(g, 1, QUANTAPDF_GRAPH_STRING, 0));
    CHECK(quantapdf_graph_append_data(g, "ab", 2) == QUANTAPDF_ERROR_UNSUPPORTED);
    OK(quantapdf_graph_append_data(g, "a", 1));
    CHECK(quantapdf_graph_append_data(g, "b", 1) == QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_graph_drop(g);
    OK(quantapdf_graph_create(&budget, &g)); OK(quantapdf_graph_reserve_node(g, 0, &id));
    OK(quantapdf_graph_begin_node(g, 1, QUANTAPDF_GRAPH_STREAM, 0));
    CHECK(quantapdf_graph_append_data(g, "ab", 2) == QUANTAPDF_ERROR_UNSUPPORTED);
    OK(quantapdf_graph_append_data(g, "a", 1)); quantapdf_graph_drop(g);
    budget = limits(); budget.max_edges = 0;
    OK(quantapdf_graph_create(&budget, &g)); OK(quantapdf_graph_reserve_node(g, 0, &id));
    OK(quantapdf_graph_begin_node(g, 1, QUANTAPDF_GRAPH_ARRAY, 0));
    CHECK(quantapdf_graph_append_edge(g, 1, NULL, 0) == QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_graph_drop(g);
    budget.max_nodes = 0;
    CHECK(quantapdf_graph_create(&budget, &g) == QUANTAPDF_ERROR_ARGUMENT && g == NULL);
    quantapdf_graph_drop(NULL);
}

static void test_scalars(void)
{
    static const struct {
        quantapdf_graph_kind kind; int64_t integer; const char *data; int valid;
    } cases[] = {
        {QUANTAPDF_GRAPH_NULL, 0, NULL, 1}, {QUANTAPDF_GRAPH_NULL, 1, NULL, 0},
        {QUANTAPDF_GRAPH_BOOL, 0, NULL, 1}, {QUANTAPDF_GRAPH_BOOL, 1, NULL, 1},
        {QUANTAPDF_GRAPH_BOOL, 2, NULL, 0}, {QUANTAPDF_GRAPH_INTEGER, INT64_MIN, NULL, 1},
        {QUANTAPDF_GRAPH_INTEGER, INT64_MAX, NULL, 1},
        {QUANTAPDF_GRAPH_REAL, 0, "+.5", 1}, {QUANTAPDF_GRAPH_REAL, 0, "-12.", 1},
        {QUANTAPDF_GRAPH_REAL, 0, "00.10", 1}, {QUANTAPDF_GRAPH_REAL, 0, ".", 0},
        {QUANTAPDF_GRAPH_REAL, 0, "1e3", 0}, {QUANTAPDF_GRAPH_REAL, 0, "1.2.3", 0},
        {QUANTAPDF_GRAPH_REAL, 0, "", 0}, {QUANTAPDF_GRAPH_REAL, 0, "+", 0},
        {QUANTAPDF_GRAPH_REAL, 0, " 1", 0}, {QUANTAPDF_GRAPH_NAME, 0, "/", 1},
        {QUANTAPDF_GRAPH_NAME, 0, "bad", 0}, {QUANTAPDF_GRAPH_STRING, 0, "", 1}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        quantapdf_graph *g = build();
        quantapdf_local_id id;
        quantapdf_validation_report report;
        quantapdf_status status;
        OK(quantapdf_graph_reserve_node(g, 0, &id));
        OK(quantapdf_graph_begin_node(g, id, cases[i].kind, cases[i].integer));
        if (cases[i].data != NULL)
            OK(quantapdf_graph_append_data(g, cases[i].data, strlen(cases[i].data)));
        OK(quantapdf_graph_end_node(g));
        status = quantapdf_graph_finish(g, 1, 2, NULL, 0, &report);
        CHECK(status == (cases[i].valid ? QUANTAPDF_OK : QUANTAPDF_ERROR_BACKEND));
        if (!cases[i].valid) {
            CHECK(report.rule == QUANTAPDF_VALIDATION_VALUE);
            CHECK(report.subject == QUANTAPDF_VALIDATION_GRAPH_NODE && report.index == id - 1);
            CHECK(quantapdf_graph_view(g) == NULL);
        }
        quantapdf_graph_drop(g);
    }
}

static void test_dictionary_order(void)
{
    static const char *keys[] = {"/A", "/AA", "/A\x80", "/Type", "/\xff"};
    quantapdf_graph_limits budget = limits();
    size_t scenario, i;
    for (scenario = 0; scenario < 3; ++scenario) {
        quantapdf_graph *g = NULL;
        quantapdf_local_id id;
        quantapdf_validation_report report;
        OK(quantapdf_graph_create(&budget, &g));
        for (i = 0; i < 3; ++i) OK(quantapdf_graph_reserve_node(g, 1, &id));
        OK(quantapdf_graph_begin_node(g, 1, QUANTAPDF_GRAPH_DICTIONARY, 0));
        OK(quantapdf_graph_append_edge(g, 2, "/Root", 5)); OK(quantapdf_graph_end_node(g));
        OK(quantapdf_graph_begin_node(g, 2, QUANTAPDF_GRAPH_DICTIONARY, 0));
        for (i = 0; i < 5; ++i) {
            const char *key = i == 1 && scenario != 0 ? (scenario == 1 ? "/A" : "/0") : keys[i];
            OK(quantapdf_graph_append_edge(g, i == 3 ? 3 : 2, key, strlen(key)));
        }
        OK(quantapdf_graph_end_node(g));
        OK(quantapdf_graph_begin_node(g, 3, QUANTAPDF_GRAPH_NAME, 0));
        OK(quantapdf_graph_append_data(g, "/Catalog", 8)); OK(quantapdf_graph_end_node(g));
        if (scenario == 0) {
            const quantapdf_graph_model *m;
            OK(quantapdf_graph_finish(g, 1, 2, NULL, 0, &report));
            m = quantapdf_graph_view(g);
            for (i = 0; i < 5; ++i) CHECK(quantapdf_graph_find(m, 2, keys[i], strlen(keys[i])) != NULL);
            CHECK(quantapdf_graph_find(m, 2, "/AB", 3) == NULL);
            CHECK(quantapdf_graph_find(m, 2, "/", 1) == NULL);
            CHECK(quantapdf_graph_find(m, 2, "/\xffX", 3) == NULL);
        } else {
            CHECK(quantapdf_graph_finish(g, 1, 2, NULL, 0, &report) == QUANTAPDF_ERROR_BACKEND);
            CHECK(report.rule == QUANTAPDF_VALIDATION_VALUE);
            CHECK(report.subject == QUANTAPDF_VALIDATION_GRAPH_EDGE && report.index == 2);
        }
        quantapdf_graph_drop(g);
    }
}

int main(void) { test_model(); test_source(); test_report_storage_independence(); test_limits(); test_scalars(); test_dictionary_order(); return EXIT_SUCCESS; }
