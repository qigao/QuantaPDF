#include "document_internal.h"
#include "snapshot/graph_snapshot.h"
#include "core/output_internal.h"

#include <stdlib.h>

static void dispose_graph(quantapdf_snapshot *owner)
{
    quantapdf_graph_snapshot *snapshot = (quantapdf_graph_snapshot *)owner;
    quantapdf_graph_drop(snapshot->graph);
    free(snapshot);
}

static quantapdf_status reserve_snapshot(quantapdf_context *context,
    quantapdf_graph_snapshot **out_snapshot)
{
    quantapdf_graph_snapshot *snapshot;
    quantapdf_status status;
    *out_snapshot = NULL;
    status = quantapdf_context_retain_internal(context);
    if (status != QUANTAPDF_OK) return status;
    snapshot = (quantapdf_graph_snapshot *)quantapdf_context_allocate_internal(
        context, sizeof(*snapshot));
    if (snapshot == NULL) {
        quantapdf_drop_context(context);
        return QUANTAPDF_ERROR_NOMEM;
    }
    quantapdf_snapshot_bind(&snapshot->owner, context, dispose_graph);
    *out_snapshot = snapshot;
    return QUANTAPDF_OK;
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
    status = reserve_snapshot(document->context, &snapshot);
    if (status != QUANTAPDF_OK) return status;
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

quantapdf_status quantapdf_graph_snapshot_commit(
    const quantapdf_graph_snapshot *base, const quantapdf_graph_change_set *changes,
    const quantapdf_graph_limits *limits, quantapdf_graph_snapshot **out_snapshot,
    quantapdf_validation_report *report)
{
    quantapdf_graph_snapshot *snapshot;
    quantapdf_status status;
    if (report != NULL) *report = (quantapdf_validation_report){0};
    if (out_snapshot == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    *out_snapshot = NULL;
    if (base == NULL || changes == NULL || limits == NULL || report == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    status = reserve_snapshot(base->owner.context, &snapshot);
    if (status != QUANTAPDF_OK) return status;
    status = quantapdf_graph_commit(base->graph, changes, limits, &snapshot->graph, report);
    if (status != QUANTAPDF_OK) {
        quantapdf_graph_snapshot_drop(snapshot);
        return status;
    }
    *out_snapshot = snapshot;
    return QUANTAPDF_OK;
}

void quantapdf_graph_snapshot_drop(quantapdf_graph_snapshot *snapshot)
{
    quantapdf_snapshot_drop((quantapdf_snapshot *)snapshot);
}

quantapdf_status quantapdf_graph_snapshot_write(const quantapdf_graph_snapshot *snapshot,
    const quantapdf_graph_write_limits *limits, quantapdf_output **out_output)
{
    quantapdf_output *output;
    quantapdf_status status;
    if (out_output == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (snapshot == NULL || limits == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    status = quantapdf_output_create_internal(snapshot->owner.context, &output);
    if (status != QUANTAPDF_OK) return status;
    status = snapshot->owner.context->ops->write_graph(snapshot->graph, limits,
        &output->data, &output->size);
    if (status != QUANTAPDF_OK) { quantapdf_drop_output(output); return status; }
    *out_output = output;
    return QUANTAPDF_OK;
}
