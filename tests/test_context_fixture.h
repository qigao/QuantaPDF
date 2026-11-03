#ifndef QUANTAPDF_TEST_CONTEXT_FIXTURE_H
#define QUANTAPDF_TEST_CONTEXT_FIXTURE_H

#include <quantapdf/quantapdf.h>
#include <stdlib.h>

/* One fixture Context per test translation unit, passed to current factories.
 * Tests of independent domains create and drop their own Contexts explicitly. */
static quantapdf_context *test_context_instance;
static void drop_test_context(void)
{
    quantapdf_drop_context(test_context_instance);
}
static quantapdf_context *test_context(void)
{
    if (test_context_instance == NULL) {
        if (quantapdf_context_create(&test_context_instance) != QUANTAPDF_OK)
            exit(EXIT_FAILURE);
        atexit(drop_test_context);
    }
    return test_context_instance;
}

#endif
