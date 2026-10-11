#ifndef QUANTAPDF_QPDF_GRAPH_WRITE_H
#define QUANTAPDF_QPDF_GRAPH_WRITE_H

#include "graph/graph_write.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Borrows a sealed graph synchronously. Success transfers malloc-owned bytes.
 * Failure resets outputs and preserves the graph; no exception crosses C.
 * No encryption/signature policy is implied: these inputs return UNSUPPORTED.
 * ARGUMENT: limits; STATE: unsealed; BACKEND: invalid serialization structure;
 * NOMEM: allocation; UNSUPPORTED: budget or unsupported preservation policy. */
quantapdf_status quantapdf_qpdf_write_graph(const quantapdf_graph *graph,
    const quantapdf_graph_write_limits *limits, unsigned char **out_data,
    size_t *out_size);

#ifdef __cplusplus
}
#endif
#endif
