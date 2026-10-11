#ifndef QUANTAPDF_RUNTIME_INTERNAL_H
#define QUANTAPDF_RUNTIME_INTERNAL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initial runtime ownership boundary.
 * Public ABI remains unchanged.
 */
typedef struct quantapdf_runtime_context quantapdf_runtime_context;

struct quantapdf_runtime_context {
    void *allocator;
    void *backend_runtime;
};

#ifdef __cplusplus
}
#endif

#endif
