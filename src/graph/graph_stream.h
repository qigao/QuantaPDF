#ifndef QUANTAPDF_GRAPH_STREAM_H
#define QUANTAPDF_GRAPH_STREAM_H

#include "graph.h"

/* Decoded output capacity and transient graph replay work. No implicit limits.
 * Depth is in [1, QUANTAPDF_GRAPH_MAX_DIRECT_DEPTH]. The decode budget bounds
 * the owned result, not all allocations internal to the provider/codecs. */
typedef struct quantapdf_graph_stream_limits {
    size_t max_decoded_bytes, max_direct_depth, max_direct_visits;
} quantapdf_graph_stream_limits;

#endif
