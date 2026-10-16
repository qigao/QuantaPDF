#ifndef QUANTAPDF_GRAPH_PAGE_DECIMAL_GRID_V0_H
#define QUANTAPDF_GRAPH_PAGE_DECIMAL_GRID_V0_H

#include "graph_page_device.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PRIVATE Graph API: C11, bounded, no PDFium/QPDF/provider access.
 * This header is not part of include/quantapdf or the exported ABI.
 *
 * The caller owns a sealed Graph until each call returns. The result is a
 * pure value; no token, pointer, snapshot or provider reference escapes.
 * All failures zero outputs before returning. No allocations or fallbacks.
 *
 * This v0 candidate admits only representable signed 64-bit significands,
 * at most 9 normalized decimal fraction digits, and DPI 72/96/144.
 * A valid source outside that bounded numerical subset is UNSUPPORTED.
 * Never snap a genuine fractional dimension using an epsilon.
 */
typedef struct quantapdf_graph_decimal_size_v0 {
    uint32_t width, height;
    size_t stride_bytes, output_bytes;
} quantapdf_graph_decimal_size_v0;

/* ASCII PDF numeric token -> canonical signed significand / base-10 scale.
 * FORMAT for invalid spelling, UNSUPPORTED for precision/capacity excess.
 * This function does NOT change legacy Graph's binary64 numeric decoder.
 * Intended to be called on immutable source lexemes while Graph is alive. */
quantapdf_status quantapdf_graph_decimal_parse_token_v0(
    const unsigned char *bytes, size_t size,
    int64_t *out_units, unsigned *out_scale);

/* Computes only integer pixel dimensions and output byte budgets from the
 * effective inherited CropBox/MediaBox, leaf UserUnit and Rotate. The exact
 * decimal span is calculated BEFORE any binary64 endpoint subtraction. */
quantapdf_status quantapdf_graph_page_decimal_exact_size_v0(
    const quantapdf_graph *graph, size_t page_index,
    size_t max_parent_steps, unsigned dpi, size_t max_pixels,
    size_t max_output_bytes, quantapdf_graph_decimal_size_v0 *out);

/* Candidate PRIVATE handoff: exact pixel extents + existing (unchanged)
 * floating Graph-display affine for bitmap-fit, in a value-only structure.
 * Must be called before Graph snapshot teardown. This does NOT authorize
 * native raster/pixel parity or replace any existing consumer entrypoint. */
quantapdf_status quantapdf_graph_page_decimal_handoff_v0(
    const quantapdf_graph *graph, size_t page_index,
    size_t max_parent_steps, unsigned dpi, size_t max_pixels,
    size_t max_output_bytes, quantapdf_graph_device_grid *out_grid);

#ifdef __cplusplus
}
#endif
#endif
