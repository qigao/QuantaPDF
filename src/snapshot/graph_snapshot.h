#ifndef QUANTAPDF_GRAPH_SNAPSHOT_H
#define QUANTAPDF_GRAPH_SNAPSHOT_H

#include "snapshot_owner.h"
#include "graph/graph.h"

typedef struct quantapdf_graph_snapshot {
    quantapdf_snapshot owner;
    quantapdf_graph *graph;
} quantapdf_graph_snapshot;

#ifdef __cplusplus
extern "C" {
#endif
/* Private capture API, no public ABI. Copies parsed values under explicit limits.
 * Success owns a sealed graph/context; failure leaves NULL. Source may then close.
 * Existing public document APIs do not acquire new graph admission limits. */
quantapdf_status quantapdf_document_capture_graph(
    quantapdf_document *document, const quantapdf_graph_limits *limits,
    quantapdf_graph_snapshot **out_snapshot);
void quantapdf_graph_snapshot_drop(quantapdf_graph_snapshot *snapshot);
#ifdef __cplusplus
}
#endif
#endif
