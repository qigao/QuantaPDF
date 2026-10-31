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

typedef struct quantapdf_native_resource_limits_v1 {
    size_t max_source_pixels,max_drawn_pixels,max_live_bytes;
} quantapdf_native_resource_limits_v1;

typedef struct quantapdf_native_resource_usage_v1 {
    quantapdf_native_resource_limits_v1 limit;
    size_t source_pixels,drawn_pixels,borrowed_bytes,live_bytes,peak_live_bytes;
    int enabled;
} quantapdf_native_resource_usage_v1;

typedef struct quantapdf_native_work_ledger_v1 {
    quantapdf_native_work_cost_v1 limit, used, reserved, peak_reserved;
    size_t completed_leases;
    int failed, samples_exact;
    quantapdf_native_resource_usage_v1 resources;
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

/* Freeze resource caps before the first child. Already-owned Source payload
 * remains a borrowed floor; only subsequent C11 heap buffers are released by
 * this ledger. Disabled resource tracking preserves older private callers. */
static inline int quantapdf_native_resources_init_v1(
    quantapdf_native_work_ledger_v1 *ledger,
    const quantapdf_native_resource_limits_v1 *limits,size_t borrowed_bytes)
{
    if(ledger==NULL || limits==NULL)return 0;
    if(ledger->failed || ledger->resources.enabled || ledger->completed_leases!=0u ||
       ledger->used.sample_work!=0u || ledger->used.edges!=0u ||
       ledger->reserved.sample_work!=0u || ledger->reserved.edges!=0u ||
       borrowed_bytes>limits->max_live_bytes) {
        ledger->failed=1;return 0;
    }
    ledger->resources.limit=*limits;ledger->resources.enabled=1;
    ledger->resources.borrowed_bytes=borrowed_bytes;
    ledger->resources.live_bytes=borrowed_bytes;
    ledger->resources.peak_live_bytes=borrowed_bytes;
    return 1;
}

/* One complete page inventory: unique Source images once; every placement's
 * enclosing destination area, including repeated or fully clipped paints.
 * Nested Image/mask consumers borrow this charge, never repeat it. */
static inline int quantapdf_native_pixels_charge_v1(
    quantapdf_native_work_ledger_v1 *ledger,size_t source,size_t drawn)
{
    quantapdf_native_resource_usage_v1 *r;
    if(ledger==NULL || !ledger->resources.enabled)return 1;
    r=&ledger->resources;
    if(ledger->failed ||
       !qp_work_room(r->limit.max_source_pixels,r->source_pixels,0u,source) ||
       !qp_work_room(r->limit.max_drawn_pixels,r->drawn_pixels,0u,drawn)) {
        ledger->failed=1;return 0;
    }
    r->source_pixels+=source;r->drawn_pixels+=drawn;return 1;
}

static inline int quantapdf_native_memory_take_v1(
    quantapdf_native_work_ledger_v1 *ledger,size_t bytes)
{
    quantapdf_native_resource_usage_v1 *r;
    if(ledger==NULL || !ledger->resources.enabled)return 1;
    r=&ledger->resources;
    if(ledger->failed || !qp_work_room(r->limit.max_live_bytes,r->live_bytes,0u,bytes)) {
        ledger->failed=1;return 0;
    }
    r->live_bytes+=bytes;
    if(r->live_bytes>r->peak_live_bytes)r->peak_live_bytes=r->live_bytes;
    return 1;
}

/* Cleanup must release outstanding memory even after the work ledger fails.
 * A mismatched release poisons accounting without wrapping the byte count. */
static inline int quantapdf_native_memory_give_v1(
    quantapdf_native_work_ledger_v1 *ledger,size_t bytes)
{
    quantapdf_native_resource_usage_v1 *r;
    if(ledger==NULL || !ledger->resources.enabled)return 1;
    r=&ledger->resources;
    if(r->live_bytes<r->borrowed_bytes || bytes>r->live_bytes-r->borrowed_bytes) {
        ledger->failed=1;return 0;
    }
    r->live_bytes-=bytes;return 1;
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
