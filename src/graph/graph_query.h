#ifndef QUANTAPDF_GRAPH_QUERY_H
#define QUANTAPDF_GRAPH_QUERY_H

#include "graph.h"

/* Borrowed PDF string bytes; present distinguishes empty strings from missing
 * or explicit null fields. The selected graph must outlive the view. */
typedef struct quantapdf_graph_text_view {
    const unsigned char *data;
    size_t size;
    int present;
} quantapdf_graph_text_view;

#ifdef __cplusplus
extern "C" {
#endif

/* Canonical Info key shared by graph queries and the provider adapter.
 * Unknown fields return NULL. No XMP merge or date interpretation. */
const char *quantapdf_metadata_key(quantapdf_metadata_field field);

/* Reads only /Info and the selected field. Output resets first. Malformed
 * Info/value types: FORMAT; unsealed graph: STATE; bad arguments: ARGUMENT.
 * No allocation, decoding, mutation, provider call or retained graph reference. */
quantapdf_status quantapdf_graph_query_metadata(const quantapdf_graph *graph,
    quantapdf_metadata_field field, quantapdf_graph_text_view *out_text);

#ifdef __cplusplus
}
#endif
#endif
