#include "graph/identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); \
    exit(EXIT_FAILURE); \
} } while (0)

static void test_sequence(void)
{
    quantapdf_identity_sequence sequence = {UINT64_MAX - 1};
    uint64_t value = 7, first, second;
    CHECK(quantapdf_identity_next(&sequence, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(sequence.next == UINT64_MAX - 1);
    CHECK(quantapdf_identity_next(NULL, &value) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(value == 0);
    CHECK(quantapdf_identity_next(&sequence, &value) == QUANTAPDF_OK);
    CHECK(value == UINT64_MAX - 1 && sequence.next == UINT64_MAX);
    CHECK(quantapdf_identity_next(&sequence, &value) == QUANTAPDF_OK);
    CHECK(value == UINT64_MAX && sequence.next == 0);
    CHECK(quantapdf_identity_next(&sequence, &value) == QUANTAPDF_ERROR_NOMEM);
    CHECK(value == 0 && sequence.next == 0);
    value = 7;
    CHECK(quantapdf_identity_next(&sequence, &value) == QUANTAPDF_ERROR_NOMEM);
    CHECK(value == 0 && sequence.next == 0);
    CHECK(quantapdf_reference_scope_create(&first) == QUANTAPDF_OK);
    CHECK(quantapdf_reference_scope_create(NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_reference_scope_create(&second) == QUANTAPDF_OK);
    CHECK(first != 0 && second == first + 1);
}

static void test_local_ids(void)
{
    quantapdf_local_id id = 7;
    CHECK(quantapdf_local_id_from_index(0, &id) == QUANTAPDF_OK && id == 1);
    CHECK(quantapdf_local_id_from_index(1, &id) == QUANTAPDF_OK && id == 2);
    CHECK(quantapdf_local_id_from_index(UINT32_MAX - 1, &id) == QUANTAPDF_OK);
    CHECK(id == UINT32_MAX);
    CHECK(quantapdf_local_id_from_index(UINT32_MAX, &id) == QUANTAPDF_ERROR_NOMEM);
    CHECK(id == 0);
    CHECK(quantapdf_local_id_from_index(SIZE_MAX, &id) == QUANTAPDF_ERROR_NOMEM);
    CHECK(id == 0);
    CHECK(quantapdf_local_id_from_index(0, NULL) == QUANTAPDF_ERROR_ARGUMENT);
}

static void test_references(void)
{
    uint64_t scope, other_scope, reference[2], again[2], other[2], invalid[2];
    quantapdf_local_id id;
    size_t index = 7;
    CHECK(quantapdf_reference_scope_create(&scope) == QUANTAPDF_OK);
    CHECK(quantapdf_reference_scope_create(&other_scope) == QUANTAPDF_OK);
    CHECK(quantapdf_local_id_from_index(2, &id) == QUANTAPDF_OK);
    CHECK(quantapdf_reference_make(scope, id, reference) == QUANTAPDF_OK);
    CHECK(quantapdf_reference_make(scope, id, again) == QUANTAPDF_OK);
    CHECK(memcmp(reference, again, sizeof(reference)) == 0);
    CHECK(quantapdf_reference_make(other_scope, id, other) == QUANTAPDF_OK);
    CHECK(memcmp(reference, other, sizeof(reference)) != 0);
    CHECK(quantapdf_reference_resolve(scope, 3, reference, &index) == QUANTAPDF_OK);
    CHECK(index == 2);
    CHECK(quantapdf_reference_resolve(other_scope, 3, reference, &index) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(index == SIZE_MAX);
    CHECK(quantapdf_reference_resolve(scope, 3, other, &index) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(index == SIZE_MAX);
    CHECK(quantapdf_reference_resolve(scope, 2, reference, &index) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(index == SIZE_MAX);
    CHECK(quantapdf_reference_resolve(scope, 0, reference, &index) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(index == SIZE_MAX);
    CHECK(quantapdf_reference_resolve(0, 3, reference, &index) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(index == SIZE_MAX);
    CHECK(quantapdf_reference_resolve(scope, 3, NULL, &index) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(index == SIZE_MAX);
    CHECK(quantapdf_reference_resolve(scope, 3, reference, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    memcpy(invalid, reference, sizeof(invalid));
    invalid[1] ^= UINT64_C(1) << 32;
    CHECK(quantapdf_reference_resolve(scope, 3, invalid, &index) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(index == SIZE_MAX);
    invalid[1] = reference[1] & (UINT64_MAX << 32);
    CHECK(quantapdf_reference_resolve(scope, 3, invalid, &index) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(index == SIZE_MAX);
    CHECK(quantapdf_reference_make(scope, 0, invalid) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(invalid[0] == 0 && invalid[1] == 0);
    CHECK(quantapdf_reference_make(0, id, invalid) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(invalid[0] == 0 && invalid[1] == 0);
    CHECK(quantapdf_reference_make(scope, id, NULL) == QUANTAPDF_ERROR_ARGUMENT);

    CHECK(quantapdf_local_id_from_index(UINT32_MAX - 1, &id) == QUANTAPDF_OK);
    CHECK(quantapdf_reference_make(UINT64_MAX, id, reference) == QUANTAPDF_OK);
    CHECK(quantapdf_reference_resolve(UINT64_MAX, UINT32_MAX, reference, &index) == QUANTAPDF_OK);
    CHECK(index == UINT32_MAX - 1);
    if (SIZE_MAX > UINT32_MAX) {
        CHECK(quantapdf_reference_resolve(UINT64_MAX, SIZE_MAX, reference, &index) == QUANTAPDF_ERROR_ARGUMENT);
        CHECK(index == SIZE_MAX);
    }
}

int main(void)
{
    test_sequence();
    test_local_ids();
    test_references();
    return EXIT_SUCCESS;
}
