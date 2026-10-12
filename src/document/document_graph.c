#include "document_internal.h"
#include "snapshot/graph_snapshot.h"
#include "core/output_internal.h"
#include "graph/graph_query.h"

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

quantapdf_status quantapdf_graph_snapshot_metadata(const quantapdf_graph_snapshot *snapshot,
    quantapdf_metadata_field field, char **out_utf8, size_t *out_size)
{
    quantapdf_graph_text_view text;
    quantapdf_status status;
    if (out_utf8 != NULL) *out_utf8 = NULL;
    if (out_size != NULL) *out_size = 0;
    if (snapshot == NULL || out_utf8 == NULL || out_size == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    status = quantapdf_graph_query_metadata(snapshot->graph, field, &text);
    if (status != QUANTAPDF_OK || !text.present) return status;
    return snapshot->owner.context->ops->decode_pdf_text(text.data, text.size, out_utf8, out_size);
}

quantapdf_status quantapdf_graph_snapshot_stream(const quantapdf_graph_snapshot *snapshot,
    const quantapdf_graph_reference *reference, const quantapdf_graph_stream_limits *limits,
    unsigned char **out_data, size_t *out_size)
{
    if (out_data != NULL) *out_data = NULL;
    if (out_size != NULL) *out_size = 0;
    if (snapshot == NULL || reference == NULL || limits == NULL || out_data == NULL || out_size == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    return snapshot->owner.context->ops->read_graph_stream(snapshot->graph, reference, limits,
        out_data, out_size);
}


/* Private one-stream PDF content admission. No provider, decoded byte or
 * graph ownership is retained by the result. */
quantapdf_status quantapdf_graph_snapshot_path_ir(
    const quantapdf_graph_snapshot *snapshot,
    const quantapdf_graph_reference *reference,
    const quantapdf_graph_stream_limits *stream_limits,
    const quantapdf_pdf_content_limits *content_limits,
    quantapdf_pdf_content_ir **out_ir)
{
    unsigned char *decoded = NULL;
    size_t decoded_size = 0;
    quantapdf_status status;
    if (out_ir != NULL) *out_ir = NULL;
    if (snapshot == NULL || reference == NULL || stream_limits == NULL ||
        content_limits == NULL || out_ir == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    status = quantapdf_graph_snapshot_stream(snapshot, reference, stream_limits,
        &decoded, &decoded_size);
    if (status != QUANTAPDF_OK) return status;
    status = quantapdf_pdf_content_parse(decoded, decoded_size, content_limits, out_ir);
    quantapdf_free(decoded);
    return status;
}
