#ifndef QUANTAPDF_CONTEXT_LEASE_INTERNAL_H
#define QUANTAPDF_CONTEXT_LEASE_INTERNAL_H

#include <quantapdf/quantapdf.h>
#include <string.h>

/* PRIVATE unique retained Context reference, not a provider/plugin contract.
 * Acquisition uses the existing Core retain; C11 payload owners only move
 * the reference and invoke its trusted release after their own cleanup. */
typedef struct quantapdf_context_lease_internal {
    quantapdf_context *context;
    void (*drop)(quantapdf_context *context);
} quantapdf_context_lease_internal;

static inline int quantapdf_context_lease_valid_internal(
    const quantapdf_context_lease_internal *lease)
{
    return lease!=NULL && lease->context!=NULL && lease->drop!=NULL;
}

static inline int quantapdf_context_lease_move_internal(
    quantapdf_context_lease_internal *to,quantapdf_context_lease_internal *from)
{
    if(to==NULL || to==from || to->context!=NULL || to->drop!=NULL ||
       !quantapdf_context_lease_valid_internal(from))return 0;
    *to=*from;memset(from,0,sizeof(*from));return 1;
}

static inline void quantapdf_context_lease_drop_internal(quantapdf_context_lease_internal *lease)
{
    quantapdf_context_lease_internal held;
    if(lease==NULL)return;
    held=*lease;memset(lease,0,sizeof(*lease));
    if(held.context!=NULL && held.drop!=NULL)held.drop(held.context);
}

#endif
