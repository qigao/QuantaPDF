#ifndef QUANTAPDF_GRAPH_H
#define QUANTAPDF_GRAPH_H

#include "identity.h"
#include "validation/validation.h"

typedef enum quantapdf_graph_kind {
    QUANTAPDF_GRAPH_UNDEFINED = 0,
    QUANTAPDF_GRAPH_NULL,
    QUANTAPDF_GRAPH_BOOL,
    QUANTAPDF_GRAPH_INTEGER,
    QUANTAPDF_GRAPH_REAL,
    QUANTAPDF_GRAPH_NAME,
    QUANTAPDF_GRAPH_STRING,
    QUANTAPDF_GRAPH_ARRAY,
    QUANTAPDF_GRAPH_DICTIONARY,
    QUANTAPDF_GRAPH_STREAM
} quantapdf_graph_kind;

typedef struct quantapdf_graph_node {
    quantapdf_graph_kind kind;
    int indirect;
    int64_t integer;
    size_t data_offset, data_size;
    size_t edge_offset, edge_count;
} quantapdf_graph_node;

typedef struct quantapdf_graph_edge {
    quantapdf_local_id target;
    size_t key_offset, key_size;
} quantapdf_graph_edge;

typedef struct quantapdf_graph_source {
    char pdf_version[4]; /* Canonical header M.m, or all zero when unknown. */
    int encrypted;       /* Immutable provenance; no credentials or key material. */
} quantapdf_graph_source;

/* Owned by graph; all reachable data is const to consumers. Names/keys include
 * '/'; strings are binary. Stream bytes are encoded, after provider decryption.
 * Node and byte/edge partitions follow ID order. Zero IDs are invalid. */
typedef struct quantapdf_graph_model {
    const quantapdf_graph_node *nodes;
    size_t node_count;
    const quantapdf_graph_edge *edges;
    size_t edge_count;
    const unsigned char *bytes;
    size_t byte_count;
    quantapdf_local_id trailer, catalog;
    size_t page_offset, page_count;
    quantapdf_graph_source source;
} quantapdf_graph_model;

typedef struct quantapdf_graph_limits {
    size_t max_nodes, max_edges;
    size_t max_value_bytes, max_stream_bytes, max_storage_bytes;
    size_t max_depth;
} quantapdf_graph_limits;

typedef struct quantapdf_graph quantapdf_graph;

enum { QUANTAPDF_GRAPH_MAX_DIRECT_DEPTH = 128 };

/* Runtime-only identity. References borrow no storage and confer no liveness
 * beyond their resolved graph. Origin is the version that created this slot. */
typedef struct quantapdf_graph_reference {
    uint64_t lineage, origin;
    quantapdf_local_id id;
} quantapdf_graph_reference;

#ifdef __cplusplus
extern "C" {
#endif

/* Unique owner, no provider/context dependencies. Invalid limits: ARGUMENT;
 * allocation failure: NOMEM. Outputs reset on failure. No implicit defaults. */
quantapdf_status quantapdf_graph_create(
    const quantapdf_graph_limits *limits, quantapdf_graph **out_graph);
void quantapdf_graph_drop(quantapdf_graph *graph);
/* Builder-only source facts; copied into every committed version. */
quantapdf_status quantapdf_graph_set_source(
    quantapdf_graph *graph, const quantapdf_graph_source *source);

/* Unpublished builder operations. Limits: UNSUPPORTED; allocation: NOMEM;
 * invalid arguments: ARGUMENT; invalid phase/redefinition: STATE. Reservations
 * may occur during a definition. Definitions must follow increasing ID order.
 * Inputs must not alias graph storage. Failed appends publish no logical data. */
quantapdf_status quantapdf_graph_reserve_node(
    quantapdf_graph *graph, int indirect, quantapdf_local_id *out_id);
quantapdf_status quantapdf_graph_begin_node(
    quantapdf_graph *graph, quantapdf_local_id id,
    quantapdf_graph_kind kind, int64_t integer);
quantapdf_status quantapdf_graph_append_data(
    quantapdf_graph *graph, const void *data, size_t size);
quantapdf_status quantapdf_graph_append_edge(
    quantapdf_graph *graph, quantapdf_local_id target, const void *key, size_t key_size);
quantapdf_status quantapdf_graph_end_node(quantapdf_graph *graph);

/* Validates and seals. Failed validation rolls back roots/pages and preserves
 * owned capacity. Reports follow validate_graph. Further mutation fails STATE. */
quantapdf_status quantapdf_graph_finish(
    quantapdf_graph *graph, quantapdf_local_id trailer, quantapdf_local_id catalog,
    const quantapdf_local_id *pages, size_t page_count, quantapdf_validation_report *report);

/* NULL until sealed; then borrows the complete model until graph_drop. */
const quantapdf_graph_model *quantapdf_graph_view(const quantapdf_graph *graph);
/* Includes graph, capacities, origin ranges and the shared lineage owner's full
 * size (charged conservatively per version), not allocator metadata. */
size_t quantapdf_graph_storage_bytes(const quantapdf_graph *graph);
/* Zero until sealed. Tokens are not persisted or part of model equality. */
uint64_t quantapdf_graph_lineage(const quantapdf_graph *graph);
uint64_t quantapdf_graph_version(const quantapdf_graph *graph);
/* References survive value replacement in descendants. Appended sibling nodes
 * are distinct even at equal local IDs. Unsealed owner: STATE; foreign/missing
 * reference or invalid argument: ARGUMENT. Outputs reset on failure and must
 * not alias input references or graph storage. */
quantapdf_status quantapdf_graph_reference_at(
    const quantapdf_graph *graph, quantapdf_local_id id,
    quantapdf_graph_reference *out_reference);
quantapdf_status quantapdf_graph_resolve(
    const quantapdf_graph *graph, const quantapdf_graph_reference *reference,
    quantapdf_local_id *out_id);
/* Borrowed node/edge lookup in a validated model. Missing ID/key returns NULL.
 * Dictionary keys must be sorted; find uses unsigned-byte binary search. */
const quantapdf_graph_node *quantapdf_graph_node_at(
    const quantapdf_graph_model *model, quantapdf_local_id id);
const quantapdf_graph_edge *quantapdf_graph_find(
    const quantapdf_graph_model *model, quantapdf_local_id id, const void *key, size_t size);

#ifdef __cplusplus
}
#endif
#endif
