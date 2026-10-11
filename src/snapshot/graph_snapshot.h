#ifndef QUANTAPDF_GRAPH_SNAPSHOT_H
#define QUANTAPDF_GRAPH_SNAPSHOT_H

#include "snapshot_owner.h"
#include "graph/graph_edit.h"
#include "graph/graph_write.h"
#include "graph/graph_stream.h"

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
/* Private serialization; output retains the context and outlives this snapshot.
 * No partial output is published. See graph writer design for admission policy. */
quantapdf_status quantapdf_graph_snapshot_write(const quantapdf_graph_snapshot *snapshot,
    const quantapdf_graph_write_limits *limits, quantapdf_output **out_output);
/* Private metadata query on this version. Missing/null fields return NULL/0;
 * empty strings return owned ""/0. UTF-8 results outlive the snapshot and must
 * be released with quantapdf_free. Same field/type/decoding semantics as the
 * public document metadata operation; no XMP merge or date interpretation. */
quantapdf_status quantapdf_graph_snapshot_metadata(const quantapdf_graph_snapshot *snapshot,
    quantapdf_metadata_field field, char **out_utf8, size_t *out_size);
/* Detached non-lossy decoded stream from this immutable version. The input
 * reference does not retain a graph. On success free data with quantapdf_free;
 * empty streams return NULL/0. Failure resets outputs. See graph_stream limits. */
quantapdf_status quantapdf_graph_snapshot_stream(const quantapdf_graph_snapshot *snapshot,
    const quantapdf_graph_reference *reference, const quantapdf_graph_stream_limits *limits,
    unsigned char **out_data, size_t *out_size);
#ifdef __cplusplus
}
#endif
#endif
