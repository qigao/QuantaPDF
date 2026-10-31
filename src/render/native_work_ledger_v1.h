#ifndef QUANTAPDF_NATIVE_WORK_LEDGER_V1_H
#define QUANTAPDF_NATIVE_WORK_LEDGER_V1_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* PRIVATE synchronous page ledger. One owner, no allocation or global state.
 * A lease is a unique, non-copyable borrow. Reserve BEFORE invoking a child;
 * the child's unchanged local limits bound its work until settlement. Used
 * plus outstanding reservations never exceeds either page-wide cap. */
typedef struct quantapdf_native_work_cost_v1 {
    size_t sample_work, edges;
} quantapdf_native_work_cost_v1;

typedef struct quantapdf_native_work_ledger_v1 {
    quantapdf_native_work_cost_v1 limit, used, reserved, peak_reserved;
    size_t completed_leases;
    int failed, samples_exact;
} quantapdf_native_work_ledger_v1;

typedef struct quantapdf_native_work_lease_v1 {
    quantapdf_native_work_ledger_v1 *owner;
    quantapdf_native_work_cost_v1 cap;
} quantapdf_native_work_lease_v1;

static inline void quantapdf_native_work_init_v1(
    quantapdf_native_work_ledger_v1 *ledger,size_t samples,size_t edges)
{
    memset(ledger,0,sizeof(*ledger));
    ledger->limit.sample_work=samples;ledger->limit.edges=edges;
    ledger->samples_exact=1;
}

static inline int qp_work_room(size_t cap,size_t used,size_t held,size_t add)
{
    return used<=cap && held<=cap-used && add<=cap-used-held;
}

static inline int quantapdf_native_work_reserve_v1(
    quantapdf_native_work_ledger_v1 *ledger,size_t samples,size_t edges,
    quantapdf_native_work_lease_v1 *lease)
{
    if(ledger==NULL || lease==NULL || lease->owner!=NULL || ledger->failed ||
       !qp_work_room(ledger->limit.sample_work,ledger->used.sample_work,
                     ledger->reserved.sample_work,samples) ||
       !qp_work_room(ledger->limit.edges,ledger->used.edges,
                     ledger->reserved.edges,edges))
        return 0;
    ledger->reserved.sample_work+=samples;ledger->reserved.edges+=edges;
    if(ledger->reserved.sample_work>ledger->peak_reserved.sample_work)
        ledger->peak_reserved.sample_work=ledger->reserved.sample_work;
    if(ledger->reserved.edges>ledger->peak_reserved.edges)
        ledger->peak_reserved.edges=ledger->reserved.edges;
    lease->owner=ledger;lease->cap.sample_work=samples;lease->cap.edges=edges;
    return 1;
}

/* An unsuccessful child may already have sampled. Never roll its work back
 * into reusable credit: conservatively commit its whole reservation and
 * poison the page. Only a new page execution may obtain a fresh ledger. */
static inline void quantapdf_native_work_abort_v1(
    quantapdf_native_work_lease_v1 *lease)
{
    quantapdf_native_work_ledger_v1 *ledger=lease->owner;
    if(ledger==NULL)return;
    ledger->reserved.sample_work-=lease->cap.sample_work;
    ledger->reserved.edges-=lease->cap.edges;
    ledger->used.sample_work+=lease->cap.sample_work;
    ledger->used.edges+=lease->cap.edges;
    ledger->failed=1;ledger->samples_exact=0;lease->owner=NULL;
}

static inline int quantapdf_native_work_settle_v1(
    quantapdf_native_work_lease_v1 *lease,size_t samples,size_t edges,int exact)
{
    quantapdf_native_work_ledger_v1 *ledger=lease->owner;
    if(ledger==NULL)return 0;
    if(ledger->failed || samples>lease->cap.sample_work || edges>lease->cap.edges ||
       ledger->completed_leases==SIZE_MAX) {
        quantapdf_native_work_abort_v1(lease);return 0;
    }
    ledger->reserved.sample_work-=lease->cap.sample_work;
    ledger->reserved.edges-=lease->cap.edges;
    ledger->used.sample_work+=samples;ledger->used.edges+=edges;
    ledger->samples_exact=ledger->samples_exact && exact;
    ++ledger->completed_leases;lease->owner=NULL;
    return 1;
}

#endif
