#ifndef QUANTAPDF_BACKEND_OPS_INTERNAL_H
#define QUANTAPDF_BACKEND_OPS_INTERNAL_H

#include <quantapdf/quantapdf.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct quantapdf_backend_ops quantapdf_backend_ops;
typedef struct quantapdf_backend quantapdf_backend;

/*
 * Internal provider-neutral backend contract.
 *
 * This header intentionally does not expose PDFium/qpdf types.
 * Backend implementations remain private providers.
 */
struct quantapdf_backend_ops {
    quantapdf_status (*open)(void *document);
    void (*close)(void *document);
};

struct quantapdf_backend {
    const quantapdf_backend_ops *ops;
    void *state;
};

#ifdef __cplusplus
}
#endif

#endif
