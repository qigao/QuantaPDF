#include "identity.h"

/* The public ABI already requires process-wide external serialization. */
static quantapdf_identity_sequence reference_scopes = {1};

quantapdf_status quantapdf_identity_next(
    quantapdf_identity_sequence *sequence, uint64_t *out_value)
{
    uint64_t value;
    if (out_value == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_value = 0;
    if (sequence == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    value = sequence->next;
    if (value == 0)
        return QUANTAPDF_ERROR_NOMEM;
    sequence->next = value == UINT64_MAX ? 0 : value + 1;
    *out_value = value;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_reference_scope_create(uint64_t *out_scope)
{
    return quantapdf_identity_next(&reference_scopes, out_scope);
}

quantapdf_status quantapdf_local_id_from_index(
    size_t index, quantapdf_local_id *out_id)
{
    if (out_id == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_id = 0;
    if (index >= UINT32_MAX)
        return QUANTAPDF_ERROR_NOMEM;
    *out_id = (quantapdf_local_id)(index + 1);
    return QUANTAPDF_OK;
}

static uint32_t reference_tag(uint64_t scope, quantapdf_local_id id)
{
    uint64_t value = scope ^ id;
    uint32_t tag;
    /* Preserve the previous check-tag mixing rule. This detects inconsistent
     * reference fields; it is not a cryptographic authentication primitive. */
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    tag = (uint32_t)(value ^ (value >> 32));
    return tag == 0 ? 1 : tag;
}

quantapdf_status quantapdf_reference_make(
    uint64_t scope, quantapdf_local_id id, uint64_t out_reference[2])
{
    if (out_reference == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    out_reference[0] = out_reference[1] = 0;
    if (scope == 0 || id == 0)
        return QUANTAPDF_ERROR_ARGUMENT;
    out_reference[0] = scope;
    out_reference[1] = ((uint64_t)reference_tag(scope, id) << 32) | id;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_reference_resolve(
    uint64_t scope, size_t count, const uint64_t reference[2], size_t *out_index)
{
    quantapdf_local_id id;
    if (out_index == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_index = SIZE_MAX;
    if (scope == 0 || reference == NULL || reference[0] != scope || count > UINT32_MAX)
        return QUANTAPDF_ERROR_ARGUMENT;
    id = (quantapdf_local_id)reference[1];
    if (id == 0 || id > count || (uint32_t)(reference[1] >> 32) != reference_tag(scope, id))
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_index = (size_t)id - 1;
    return QUANTAPDF_OK;
}
