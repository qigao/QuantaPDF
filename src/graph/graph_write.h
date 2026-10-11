#ifndef QUANTAPDF_GRAPH_WRITE_H
#define QUANTAPDF_GRAPH_WRITE_H

#include "graph.h"

enum { QUANTAPDF_GRAPH_WRITE_MAX_DIRECT_DEPTH = QUANTAPDF_GRAPH_MAX_DIRECT_DEPTH };

/* Explicit private writer budgets. Depth must be in [1, 128]. Expanded visits
 * conservatively sum every node's inline expansion, including repeated children.
 * Provider object storage is separate from the persistent graph byte budget. */
typedef struct quantapdf_graph_write_limits {
    size_t max_output_bytes;
    size_t max_direct_depth;
    size_t max_direct_visits;
} quantapdf_graph_write_limits;

#endif
