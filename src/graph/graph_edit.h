#ifndef QUANTAPDF_GRAPH_EDIT_H
#define QUANTAPDF_GRAPH_EDIT_H

#include "graph.h"

/* Borrowed descriptors, valid for the entire externally serialized commit.
 * Edges use local IDs in the selected base plus this batch's appended slots.
 * Dictionary keys are canonical bytes including '/', in strict byte order. */
typedef struct quantapdf_graph_edge_input {
    quantapdf_local_id target;
    const void *key;
    size_t key_size;
} quantapdf_graph_edge_input;

typedef struct quantapdf_graph_node_input {
    quantapdf_graph_kind kind;
    int indirect;
    int64_t integer;
    const void *data;
    size_t data_size;
    const quantapdf_graph_edge_input *edges;
    size_t edge_count;
} quantapdf_graph_node_input;

typedef struct quantapdf_graph_replacement {
    quantapdf_local_id id;
    quantapdf_graph_node_input value;
} quantapdf_graph_replacement;

typedef struct quantapdf_graph_change_set {
    uint64_t lineage, base_version;
    const quantapdf_graph_replacement *replacements;
    size_t replacement_count;
    const quantapdf_graph_node_input *additions;
    size_t addition_count;
    /* Zero preserves all roots and the page index. One replaces them together. */
    int replace_roots;
    quantapdf_local_id trailer, catalog;
    const quantapdf_local_id *pages;
    size_t page_count;
} quantapdf_graph_change_set;

#ifdef __cplusplus
extern "C" {
#endif

/* Copies and seals a new independently owned version. The base is immutable;
 * it must remain live during the call only. Sorted unique replacements preserve
 * IDs; additions use base.node_count + index + 1, with no slot recycling.
 * Storage includes provenance and conservatively charges shared lineage data.
 * Page-tree/index agreement is checked before publication; max_depth counts
 * /Pages and /Page nodes on the active path, including the root and leaf.
 * Transient validation scratch is node_count bytes plus
 * min(node_count, max_depth) page frames, outside persistent storage_bytes.
 * It excludes the live base/caller descriptors/allocator metadata. No history
 * payload is retained by the result. All allocation, commit and destruction
 * calls require external serialization, including across sibling versions.
 * ARGUMENT: descriptors/limits; STATE: unsealed or stale/foreign base;
 * UNSUPPORTED: budget; NOMEM: allocation/identity exhaustion;
 * BACKEND: invalid candidate graph (with report). Outputs/report reset first.
 * Output/report must not alias inputs. No graph-to-PDF serialization occurs. */
quantapdf_status quantapdf_graph_commit(
    const quantapdf_graph *base, const quantapdf_graph_change_set *changes,
    const quantapdf_graph_limits *limits, quantapdf_graph **out_graph,
    quantapdf_validation_report *report);

#ifdef __cplusplus
}
#endif
#endif
