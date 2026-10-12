#include "snapshot_owner.h"

#include <assert.h>
#include <stddef.h>

void quantapdf_snapshot_bind(
    quantapdf_snapshot *snapshot, quantapdf_context *retained_context,
    quantapdf_snapshot_dispose_fn dispose)
{
    assert(snapshot != NULL && retained_context != NULL && dispose != NULL);
    assert(snapshot->context == NULL && snapshot->dispose == NULL);
    snapshot->context = retained_context;
    snapshot->dispose = dispose;
}

void quantapdf_snapshot_drop(quantapdf_snapshot *snapshot)
{
    quantapdf_context *context;
    if (snapshot == NULL)
        return;
    assert(snapshot->context != NULL && snapshot->dispose != NULL);
    context = snapshot->context;
    snapshot->dispose(snapshot);
    quantapdf_drop_context(context);
}
