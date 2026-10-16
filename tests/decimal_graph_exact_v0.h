#ifndef QUANTAPDF_TEST_DECIMAL_GRAPH_EXACT_V0_H
#define QUANTAPDF_TEST_DECIMAL_GRAPH_EXACT_V0_H

#include "graph/graph_page_geometry.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Test-only: accepts a deliberately bounded *exact* PDF decimal token.
 * Produces signed significand and base-10 scale, canonicalizing signed zero
 * and trailing fractional zeros. Never exposes Graph/provider ownership. */
quantapdf_status quantapdf_test_decimal_exact_token_v0(
    const unsigned char *bytes, size_t size,
    int64_t *out_units, unsigned *out_scale);

/* Test-only Source->Graph exact-decimal PAGE SIZE observation. This does not
 * supply an affine or paint pixels and MUST NOT replace the public renderer.
 * On any failure, all output fields remain zero. Exacts use the selected
 * inherited /CropBox (or /MediaBox), leaf /UserUnit and /Rotate.
 *
 * Only private Grid v0's 72/96/144 DPI are admitted. Inputs must be representable
 * within the bounded signed-significand/scale and 64-bit rational operations;
 * otherwise fail closed without floating epsilon or a provider fallback. */
typedef struct quantapdf_test_decimal_grid_v0 {
    uint32_t width, height;
    size_t stride_bytes, output_bytes;
} quantapdf_test_decimal_grid_v0;

quantapdf_status quantapdf_test_graph_exact_decimal_grid_v0(
    const quantapdf_graph *graph, size_t page_index,
    size_t max_parent_steps, unsigned dpi, size_t max_pixels,
    size_t max_output_bytes, quantapdf_test_decimal_grid_v0 *out);

#ifdef __cplusplus
}
#endif

#endif
