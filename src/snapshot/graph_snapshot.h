#ifndef QUANTAPDF_GRAPH_SNAPSHOT_H
#define QUANTAPDF_GRAPH_SNAPSHOT_H

#include "snapshot_owner.h"
#include "graph/graph_edit.h"

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
/* Atomic private graph commit. Borrows the base and all descriptors for this
 * call; the result owns independent model storage and retains the same context.
 * The base/source may be dropped after success. See graph_commit for statuses
 * and structural validation scope. No existing public edit path is rerouted. */
quantapdf_status quantapdf_graph_snapshot_commit(
    const quantapdf_graph_snapshot *base, const quantapdf_graph_change_set *changes,
    const quantapdf_graph_limits *limits, quantapdf_graph_snapshot **out_snapshot,
    quantapdf_validation_report *report);
void quantapdf_graph_snapshot_drop(quantapdf_graph_snapshot *snapshot);
#ifdef __cplusplus
}
#endif
#endif
