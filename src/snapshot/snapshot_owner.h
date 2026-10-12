#ifndef QUANTAPDF_SNAPSHOT_OWNER_H
#define QUANTAPDF_SNAPSHOT_OWNER_H

#include <quantapdf/quantapdf.h>

/* First member of a uniquely owned typed snapshot. Payloads become immutable
 * when the facade publishes the completed object; views expire at its drop. */
typedef struct quantapdf_snapshot quantapdf_snapshot;
typedef void (*quantapdf_snapshot_dispose_fn)(quantapdf_snapshot *snapshot);

struct quantapdf_snapshot {
    quantapdf_context *context;
    quantapdf_snapshot_dispose_fn dispose;
};

#ifdef __cplusplus
extern "C" {
#endif

/* Consumes one already retained context reference. The zero-initialized owner
 * must be unbound. dispose releases the typed payload and its whole allocation. */
void quantapdf_snapshot_bind(
    quantapdf_snapshot *snapshot, quantapdf_context *retained_context,
    quantapdf_snapshot_dispose_fn dispose);
/* Accepts NULL; otherwise requires a bound owner. Context outlives disposal. */
void quantapdf_snapshot_drop(quantapdf_snapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif
