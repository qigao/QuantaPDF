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

/* Private explicit opt-in: project exactly two frozen identity-space
 * Clip PATH leaves (IDs 1 and 2) and their flat Intersection root (ID 3).
 * Both bounded command arrays, the three descriptors and two member IDs
 * are staged on this call's stack; image data and GS resources remain
 * immutable borrows. A single GS must name root 3. Cubic Clip commands
 * use the existing 72/96/144-DPI point projection and V5 two-Clip sampler.
 * Source V10 selects this entry only after publishing the exact three-node
 * graph; the original function above still refuses multi-Clip input.
 * Failure clears every output field. */
quantapdf_status quantapdf_native_device_ir_render_v1_two_clip(
    const quantapdf_ir *source, unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif

#endif
