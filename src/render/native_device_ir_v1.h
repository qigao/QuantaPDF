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

/* Source V10 PATH-only Clip admission. One..four bounded PATH fills,
 * one frozen identity-space Clip or two leaves plus their flat root,
 * and a single GS=1 prefix followed by an optional GS=0 suffix. All
 * commands have four..nine elements; no Image resources are admitted.
 * Reuses the same bounded stack projection and V5 subpixel sampler.
 * Older device entrypoints still refuse clipped PATH-only input. */
quantapdf_status quantapdf_native_device_ir_render_v1_clipped_paths(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image);

/* Private exact two-GS profile. IDs 1/2 are owned frozen Clip PATH
 * leaves, ID 3 their flat Intersection. GS 1 names leaf 1 and GS 2
 * names root 3; both are full-alpha Normal and both must be painted.
 * Two..four operations follow 1* 2+ 1* 0*: painting the first leaf,
 * its intersection, Q-restoring the first leaf, and finally no Clip.
 * PATH-only permits two..four fills, IMAGE-only exactly two paints,
 * mixed one..two of each, within the unchanged four-layer ceiling.
 * Each PATH/Clip is one closed contour with four..nine commands. The
 * older entrypoints still refuse two GS records. Projection is
 * stack-only, all borrows immutable, output owned
 * and detached on success and zeroed on failure. */
quantapdf_status quantapdf_native_device_ir_render_v1_clip_states(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image);

/* Explicit compound-Clip extension of the exact two-GS profile above.
 * Each Clip leaf contains one or two closed contours, four..nine total
 * commands. Every contour is MOVE, at least two LINE/CUBIC commands,
 * CLOSE. Original single-contour inputs remain a subset; painted PATHs
 * still contain exactly one contour. The old clip_states entry keeps
 * rejecting compound leaves. State order, resource cardinalities,
 * budgets, immutable stack projection and output ownership are shared. */
quantapdf_status quantapdf_native_device_ir_render_v1_compound_clip_states(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image);

/* Explicit paint-before-W profile: an actual nonempty GS0 prefix, then
 * one GS in order 0+ 1+ 0*, or two GS in order 0+ 1* 2+ 1* 0* with both
 * nonzero identities painted. One GS selects a single frozen Clip leaf
 * or the flat two-leaf root; two GS select leaf 1 and root 3 as above.
 * Exact capacities, two..four total paints, at most two Images and two
 * PATHs when mixed. PATH-only may use four; Image-only exactly two, so
 * it cannot also use both nonzero GS identities. Clip leaves keep one
 * or two closed contours/four..nine commands; painted PATHs stay one
 * closed contour. All older entrypoints retain their own admission.
 * Budgets, immutable stack projection and detached output are shared. */
quantapdf_status quantapdf_native_device_ir_render_v1_unclipped_prefix(
    const quantapdf_ir *source,unsigned dpi,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    quantapdf_ir_raster_image *out_image);

#ifdef __cplusplus
}
#endif

#endif
