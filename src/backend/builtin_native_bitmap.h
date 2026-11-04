#ifndef QUANTAPDF_BUILTIN_NATIVE_BITMAP_H
#define QUANTAPDF_BUILTIN_NATIVE_BITMAP_H
#include "backend/builtin_native_page.h"
#include "backend/document_backend.h"
#ifdef __cplusplus
extern "C" {
#endif
quantapdf_status quantapdf_builtin_render_bitmap(
    void *builtin_page,
    const quantapdf_native_page_limits *limits,
    const quantapdf_native_page_device *device,
    quantapdf_context_lease_internal *lease,quantapdf_backend_bitmap *out);
#ifdef __cplusplus
}
#endif
#endif
