#ifndef QUANTAPDF_GRAPH_IDENTITY_H
#define QUANTAPDF_GRAPH_IDENTITY_H

#include <quantapdf/quantapdf.h>

/* An ordinal in one append-only table. Not a PDF object number or a complete
 * cross-version graph ID; its owner/origin supplies the identity domain. */
typedef uint32_t quantapdf_local_id;

/* Initialize next to 1. Zero is permanently exhausted. All operations require
 * external serialization. Outputs must not alias sequence or reference input. */
typedef struct quantapdf_identity_sequence {
    uint64_t next;
} quantapdf_identity_sequence;

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the current value and advances without recycling. Exhaustion: NOMEM;
 * NULL arguments: ARGUMENT. Failure resets an available output to zero. */
quantapdf_status quantapdf_identity_next(
    quantapdf_identity_sequence *sequence, uint64_t *out_value);

/* Unique ephemeral scope within this loaded runtime. ABI v2 serializes all
 * calls, including calls through distinct contexts. No retained resource. */
quantapdf_status quantapdf_reference_scope_create(uint64_t *out_scope);

/* Index -> nonzero local ordinal. Unrepresentable index: NOMEM; NULL output:
 * ARGUMENT. Failure resets an available output to zero. */
quantapdf_status quantapdf_local_id_from_index(
    size_t index, quantapdf_local_id *out_id);

/* Encodes values only; does not publish a table entry or confer ownership.
 * Invalid scope/ID/output: ARGUMENT, with an available output reset. */
quantapdf_status quantapdf_reference_make(
    uint64_t scope, quantapdf_local_id id, uint64_t out_reference[2]);

/* Validates scope, check tag and committed table range. Owner checks liveness
 * after success; deleted IDs must not be reused. All failures: ARGUMENT.
 * Success returns a zero-based index; failure sets it to SIZE_MAX if present. */
quantapdf_status quantapdf_reference_resolve(
    uint64_t scope, size_t count, const uint64_t reference[2], size_t *out_index);

#ifdef __cplusplus
}
#endif

#endif
