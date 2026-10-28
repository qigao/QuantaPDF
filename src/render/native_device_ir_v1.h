#ifndef QUANTAPDF_NATIVE_DEVICE_IR_V1_H
#define QUANTAPDF_NATIVE_DEVICE_IR_V1_H

#include "render/ir_raster_scoped_mixed_v5.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private C11, no provider. Borrow a fully Source-admitted immutable IR
 * and directly raster its source geometry in 96/144-DPI device pixels.
 * The only raster implementation remains scoped_mixed_v5. The projected
 * IR and path/clip commands live on this call's stack, never persist.
 *
 * The 96-DPI IMAGE subset requires EXACT integer destination boundaries
 * after 4/3 scaling; an unqualified fractional image is UNSUPPORTED, not
 * rounded, painted at 72 DPI or retried. Fractional PATH/Clip geometry
 * is sampled on the genuine device grid. On failure output is zeroed. */
quantapdf_status quantapdf_native_device_ir_render_v1(
    const quantapdf_ir *source, unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif

#endif
