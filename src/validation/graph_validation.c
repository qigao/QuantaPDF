#include "validation.h"
#include "graph/graph.h"

#include <string.h>

static int span(size_t offset, size_t size, size_t count)
{
    return offset <= count && size <= count - offset;
}

static int array(const void *data, size_t count, size_t element)
{
    return count <= SIZE_MAX / element && (count == 0 || data != NULL);
}

static int real_token(const unsigned char *data, size_t size)
{
    size_t i = 0, digits = 0;
    int dot = 0;
    if (size == 0) return 0;
    if (data[0] == '+' || data[0] == '-') ++i;
    for (; i < size; ++i) {
        if (data[i] >= '0' && data[i] <= '9') ++digits;
        else if (data[i] == '.' && !dot) dot = 1;
        else return 0;
    }
    return digits != 0;
}

static int key_less(const unsigned char *a, size_t an, const unsigned char *b, size_t bn)
{
    int order = memcmp(a, b, an < bn ? an : bn);
    return order < 0 || (order == 0 && an < bn);
}

static int named_type(const quantapdf_graph_model *m, quantapdf_local_id id, const char *type)
{
    const quantapdf_graph_edge *edge = quantapdf_graph_find(m, id, "/Type", 5);
    const quantapdf_graph_node *node = edge == NULL ? NULL : quantapdf_graph_node_at(m, edge->target);
    size_t size = strlen(type);
    return node != NULL && node->kind == QUANTAPDF_GRAPH_NAME && node->data_size == size &&
        memcmp(m->bytes + node->data_offset, type, size) == 0;
}

#define REQUIRE(test, rule_value, subject_value, index_value, detail_value) do { \
    if (!(test)) { report->rule = rule_value; report->subject = subject_value; \
        report->index = index_value; report->detail = detail_value; \
        return QUANTAPDF_ERROR_BACKEND; } \
} while (0)
#define STORAGE(test, subject, index) REQUIRE(test, QUANTAPDF_VALIDATION_STORAGE, subject, index, 0)
#define VALUE(test, subject, index) REQUIRE(test, QUANTAPDF_VALIDATION_VALUE, subject, index, 0)
#define REF(test, subject, index, id) REQUIRE(test, QUANTAPDF_VALIDATION_REFERENCE, subject, index, id)

quantapdf_status quantapdf_validate_graph(const quantapdf_graph_model *m, quantapdf_validation_report *report)
{
    size_t i, j, byte_cursor = 0, edge_cursor = 0;
    const quantapdf_graph_edge *root;
    if (report != NULL) memset(report, 0, sizeof(*report));
    if (m == NULL || report == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    STORAGE(m->node_count <= UINT32_MAX && array(m->nodes, m->node_count, sizeof(*m->nodes)), QUANTAPDF_VALIDATION_GRAPH_NODE, 0);
    STORAGE(array(m->edges, m->edge_count, sizeof(*m->edges)), QUANTAPDF_VALIDATION_GRAPH_EDGE, 0);
    STORAGE(array(m->bytes, m->byte_count, 1), QUANTAPDF_VALIDATION_MODEL, 0);
    STORAGE(span(m->page_offset, m->page_count, m->edge_count) &&
        m->page_count == m->edge_count - m->page_offset, QUANTAPDF_VALIDATION_GRAPH_ROOT, 0);
    for (i = 0; i < m->node_count; ++i) {
        const quantapdf_graph_node *n = &m->nodes[i];
        int container = n->kind == QUANTAPDF_GRAPH_ARRAY || n->kind == QUANTAPDF_GRAPH_DICTIONARY || n->kind == QUANTAPDF_GRAPH_STREAM;
        int bytes = n->kind == QUANTAPDF_GRAPH_REAL || n->kind == QUANTAPDF_GRAPH_NAME || n->kind == QUANTAPDF_GRAPH_STRING || n->kind == QUANTAPDF_GRAPH_STREAM;
        VALUE(n->kind >= QUANTAPDF_GRAPH_NULL && n->kind <= QUANTAPDF_GRAPH_STREAM &&
            (n->indirect == 0 || n->indirect == 1), QUANTAPDF_VALIDATION_GRAPH_NODE, i);
        STORAGE(n->data_offset == byte_cursor && span(n->data_offset, n->data_size, m->byte_count), QUANTAPDF_VALIDATION_GRAPH_NODE, i);
        STORAGE(n->edge_offset == edge_cursor && span(n->edge_offset, n->edge_count, m->page_offset), QUANTAPDF_VALIDATION_GRAPH_NODE, i);
        VALUE((bytes || n->data_size == 0) && (container || n->edge_count == 0), QUANTAPDF_VALIDATION_GRAPH_NODE, i);
        VALUE(n->kind == QUANTAPDF_GRAPH_INTEGER ||
            (n->kind == QUANTAPDF_GRAPH_BOOL ? n->integer == 0 || n->integer == 1 : n->integer == 0), QUANTAPDF_VALIDATION_GRAPH_NODE, i);
        if (n->kind == QUANTAPDF_GRAPH_REAL)
            VALUE(n->data_size != 0 && real_token(m->bytes + n->data_offset, n->data_size), QUANTAPDF_VALIDATION_GRAPH_NODE, i);
        if (n->kind == QUANTAPDF_GRAPH_NAME)
            VALUE(n->data_size != 0 && m->bytes[n->data_offset] == '/', QUANTAPDF_VALIDATION_GRAPH_NODE, i);
        byte_cursor += n->data_size;
        for (j = 0; j < n->edge_count; ++j) {
            size_t index = n->edge_offset + j;
            const quantapdf_graph_edge *e = &m->edges[index];
            REF(e->target != 0 && e->target <= m->node_count, QUANTAPDF_VALIDATION_GRAPH_EDGE, index, e->target);
            STORAGE(e->key_offset == byte_cursor && span(e->key_offset, e->key_size, m->byte_count), QUANTAPDF_VALIDATION_GRAPH_EDGE, index);
            if (n->kind == QUANTAPDF_GRAPH_ARRAY)
                VALUE(e->key_size == 0, QUANTAPDF_VALIDATION_GRAPH_EDGE, index);
            else {
                VALUE(e->key_size != 0 && m->bytes[e->key_offset] == '/', QUANTAPDF_VALIDATION_GRAPH_EDGE, index);
                if (j != 0) {
                    const quantapdf_graph_edge *previous = &m->edges[index - 1];
                    VALUE(key_less(m->bytes + previous->key_offset, previous->key_size,
                        m->bytes + e->key_offset, e->key_size), QUANTAPDF_VALIDATION_GRAPH_EDGE, index);
                }
            }
            byte_cursor += e->key_size;
        }
        edge_cursor += n->edge_count;
    }
    STORAGE(byte_cursor == m->byte_count && edge_cursor == m->page_offset, QUANTAPDF_VALIDATION_MODEL, 0);
    REF(m->trailer != 0 && m->trailer <= m->node_count, QUANTAPDF_VALIDATION_GRAPH_ROOT, 0, m->trailer);
    REF(m->catalog != 0 && m->catalog <= m->node_count, QUANTAPDF_VALIDATION_GRAPH_ROOT, 1, m->catalog);
    VALUE(m->nodes[m->trailer - 1].kind == QUANTAPDF_GRAPH_DICTIONARY &&
        m->nodes[m->catalog - 1].kind == QUANTAPDF_GRAPH_DICTIONARY, QUANTAPDF_VALIDATION_GRAPH_ROOT, 0);
    root = quantapdf_graph_find(m, m->trailer, "/Root", 5);
    REF(root != NULL && root->target == m->catalog, QUANTAPDF_VALIDATION_GRAPH_ROOT, 0, m->catalog);
    VALUE(named_type(m, m->catalog, "/Catalog"), QUANTAPDF_VALIDATION_GRAPH_ROOT, 1);
    for (i = 0; i < m->page_count; ++i) {
        const quantapdf_graph_edge *e = &m->edges[m->page_offset + i];
        REF(e->target != 0 && e->target <= m->node_count, QUANTAPDF_VALIDATION_PAGE, i, e->target);
        VALUE(e->key_size == 0 && e->key_offset == 0 &&
            m->nodes[e->target - 1].kind == QUANTAPDF_GRAPH_DICTIONARY &&
            named_type(m, e->target, "/Page"), QUANTAPDF_VALIDATION_PAGE, i);
    }
    return QUANTAPDF_OK;
}
#undef REF
#undef VALUE
#undef STORAGE
#undef REQUIRE
