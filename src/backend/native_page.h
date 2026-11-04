#ifndef QUANTAPDF_NATIVE_PAGE_H
#define QUANTAPDF_NATIVE_PAGE_H
#include "backend/qpdf_page_ir.h"
#include "core/context_lease_internal.h"
#include "render/ir_raster_scoped_mixed_v5.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct quantapdf_native_page_limits {
    quantapdf_qpdf_page_limits source;
    quantapdf_ir_raster_scoped_mixed_v5_limits raster;
} quantapdf_native_page_limits;
typedef struct quantapdf_native_page_device {
    size_t page_index;
    float dpi;
    float rotation_degrees;
    int clip_enabled;
    int alpha;
    int thumbnail;
} quantapdf_native_page_device;
#ifdef __cplusplus
}
#endif
#endif
