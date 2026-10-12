#include "document_internal.h"
#include "snapshot/graph_snapshot.h"

#include <stdlib.h>

static void dispose_graph(quantapdf_snapshot *owner)
{
    quantapdf_graph_snapshot *snapshot = (quantapdf_graph_snapshot *)owner;
    quantapdf_graph_drop(snapshot->graph);
    free(snapshot);
}

quantapdf_status quantapdf_document_capture_graph(
    quantapdf_document *document, const quantapdf_graph_limits *limits,
    quantapdf_graph_snapshot **out_snapshot)
{
    quantapdf_graph_snapshot *snapshot;
    quantapdf_status status;
    if (out_snapshot == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    *out_snapshot = NULL;
    if (document == NULL || limits == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    status = quantapdf_context_retain_internal(document->context);
    if (status != QUANTAPDF_OK) return status;
    snapshot = (quantapdf_graph_snapshot *)quantapdf_context_allocate_internal(
        document->context, sizeof(*snapshot));
    if (snapshot == NULL) {
        quantapdf_drop_context(document->context);
        return QUANTAPDF_ERROR_NOMEM;
    }
    quantapdf_snapshot_bind(&snapshot->owner, document->context, dispose_graph);
    status = document->ops->capture_graph(document->backend, limits, &snapshot->graph);
    if (status != QUANTAPDF_OK) {
        quantapdf_graph_snapshot_drop(snapshot);
        return status;
    }
    if (quantapdf_graph_view(snapshot->graph) == NULL) {
        quantapdf_graph_snapshot_drop(snapshot);
        return QUANTAPDF_ERROR_BACKEND;
    }
    *out_snapshot = snapshot;
    return QUANTAPDF_OK;
}

void quantapdf_graph_snapshot_drop(quantapdf_graph_snapshot *snapshot)
{
    quantapdf_snapshot_drop((quantapdf_snapshot *)snapshot);
}
