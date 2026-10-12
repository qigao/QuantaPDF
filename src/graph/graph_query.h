#ifndef QUANTAPDF_GRAPH_QUERY_H
#define QUANTAPDF_GRAPH_QUERY_H

#include "graph.h"

/* Borrowed PDF string bytes; present distinguishes empty strings from missing
 * or explicit null fields. The selected graph must outlive the view. */
typedef struct quantapdf_graph_text_view {
    const unsigned char *data;
    size_t size;
    int present;
} quantapdf_graph_text_view;

/* A value reference plus borrowed type/key views. Owner is zero for flat object
 * and page enumeration; member results identify the containing dictionary or
 * array. Ordinal is local to that enumeration. Names include '/' and may contain
 * non-ASCII bytes; arrays and flat enumerations have no name. */
typedef struct quantapdf_graph_match {
    quantapdf_graph_reference reference, owner;
    const quantapdf_graph_node *node;
    const unsigned char *name;
    size_t name_size, ordinal;
} quantapdf_graph_match;

/* Borrowed cursor: the selected immutable graph must outlive this value and all
 * returned views. Copy to bookmark; discard to cancel. No allocation or drop.
 * Fields are implementation state and must not be modified by consumers. */
typedef struct quantapdf_graph_cursor {
    const quantapdf_graph *graph;
    size_t next, count, offset;
    quantapdf_local_id owner;
    int objects;
} quantapdf_graph_cursor;

#ifdef __cplusplus
extern "C" {
#endif

/* Canonical Info key shared by graph queries and the provider adapter.
 * Unknown fields return NULL. No XMP merge or date interpretation. */
const char *quantapdf_metadata_key(quantapdf_metadata_field field);

/* Stored objects in ascending local-ID order (including disconnected objects),
 * pages in the graph's page-index order, or members in array/key order. Each
 * enumeration is finite even when the object graph contains cycles. Members
 * require an array, dictionary or stream; references are checked against this
 * graph's lineage/origin. Initializers clear the cursor on failure. */
quantapdf_status quantapdf_graph_query_objects(const quantapdf_graph *graph,
    quantapdf_graph_cursor *out_cursor);
quantapdf_status quantapdf_graph_query_pages(const quantapdf_graph *graph,
    quantapdf_graph_cursor *out_cursor);
quantapdf_status quantapdf_graph_query_members(const quantapdf_graph *graph,
    const quantapdf_graph_reference *reference, quantapdf_graph_cursor *out_cursor);

/* Success with found=0 means exhaustion. Clears outputs before every call;
 * advances only on success with found=1. Uninitialized cursor: STATE. */
quantapdf_status quantapdf_graph_query_next(quantapdf_graph_cursor *cursor,
    quantapdf_graph_match *out_match, int *out_found);

/* Exact canonical dictionary/stream key lookup. Null values are present.
 * The input reference may alias out_match->reference for chained lookups. */
quantapdf_status quantapdf_graph_query_member(const quantapdf_graph *graph,
    const quantapdf_graph_reference *reference, const void *key, size_t key_size,
    quantapdf_graph_match *out_match, int *out_found);

/* Whole /Resources dictionary from the page or nearest /Pages ancestor.
 * Null/missing entries inherit; an empty dictionary stops inheritance. No merge
 * or provider repair. Owner identifies the page/ancestor supplying the value.
 * max_parent_steps counts visited dictionaries including the page, and must be
 * nonzero. Wrong encountered types: FORMAT; exhausted work budget: UNSUPPORTED.
 * A chain longer than node_count proves a cycle and returns FORMAT. This query
 * stops at the first resource dictionary; it does not validate the whole tree. */
quantapdf_status quantapdf_graph_query_resources(const quantapdf_graph *graph,
    size_t page_index, size_t max_parent_steps, quantapdf_graph_match *out_match,
    int *out_found);

/* Reads only /Info and the selected field. Output resets first. Malformed
 * Info/value types: FORMAT; unsealed graph: STATE; bad arguments: ARGUMENT.
 * No allocation, decoding, mutation, provider call or retained graph reference. */
quantapdf_status quantapdf_graph_query_metadata(const quantapdf_graph *graph,
    quantapdf_metadata_field field, quantapdf_graph_text_view *out_text);

#ifdef __cplusplus
}
#endif
#endif
