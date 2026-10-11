#ifndef QUANTAPDF_QPDF_GRAPH_STREAM_H
#define QUANTAPDF_QPDF_GRAPH_STREAM_H

#include "graph/graph_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reads one stream through QPDF's non-lossy generalized/specialized filters.
 * No external files, Crypt processing, raw fallback or partial publication.
 * Reference belongs to the selected sealed graph. Non-stream/foreign reference:
 * ARGUMENT; unsealed graph: STATE; malformed decoding: FORMAT; unsupported filter
 * or exhausted budget: UNSUPPORTED. Detached malloc bytes use quantapdf_free;
 * an empty decoded stream succeeds with NULL/0. Failure also resets outputs. */
quantapdf_status quantapdf_qpdf_graph_stream(const quantapdf_graph *graph,
    const quantapdf_graph_reference *reference, const quantapdf_graph_stream_limits *limits,
    unsigned char **out_data, size_t *out_size);

#ifdef __cplusplus
}
#endif
#endif
