#ifndef QUANTAPDF_NATIVE_DEVICE_IR_V1_H
#define QUANTAPDF_NATIVE_DEVICE_IR_V1_H

#include "render/ir_raster_scoped_mixed_v5.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private C11, no provider. Borrow a fully Source-admitted immutable IR
 * and directly raster its source geometry in 72/96/144-DPI device pixels.
 * The only raster implementation remains scoped_mixed_v5. The projected
 * IR and path/clip commands live on this call's stack, never persist.
 *
 * At 96 DPI, IMAGE fractional placement is retained (never rounded);
 * the same V5 Raster builds a bounded 4x4 subpixel rectangle/Clip
 * intersection mask and samples nearest-neighbor source color per device
 * pixel center. This is NOT separate 72-DPI rescaling or a second image
 * provider. On failure every output field remains zeroed. */
quantapdf_status quantapdf_native_device_ir_render_v1(
    const quantapdf_ir *source, unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif

#endif
