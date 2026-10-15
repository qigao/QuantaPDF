#ifndef QUANTAPDF_GRAPH_PAGE_DEVICE_SAMPLE_H
#define QUANTAPDF_GRAPH_PAGE_DEVICE_SAMPLE_H

#include "graph_page_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private 4x4 geometric sampling only: no raster admission, antialiasing
 * parity assertion, QPDF/PDFium, allocation or output ownership.
 * Pixel/subpixel coordinates are device Y-down. Returned source
 * coordinates use the original PDF Y-up space, prior to the Graph
 * display map. Out values are zero before every failure.
 *
 * The inverse is intentionally admitted for only orthogonal PDF page
 * transforms (axis-aligned or 90-degree-swapped). Arbitrary affine
 * shear is unsupported; intrinsic rotation is already checked by
 * quantapdf_graph_page_display_map.
 */
quantapdf_status quantapdf_graph_device_grid_sample_q4(
    const quantapdf_graph_device_grid *grid,
    uint32_t pixel_x, uint32_t pixel_y,
    unsigned int sub_x, unsigned int sub_y,
    double *out_source_x, double *out_source_y);

/* 4x4 samples / pixel / source edge. Bound aggregate work BEFORE raster
 * allocation or iteration. The caller must pass explicit edge and sample
 * budgets; no truncation, wrapping, or implicit defaults.
 * This does not make a source edge or raster operation semantically valid.
 */
quantapdf_status quantapdf_graph_device_grid_admit_q4_work(
    const quantapdf_graph_device_grid *grid,
    size_t edge_count, size_t max_edges, size_t max_sample_tests);

#ifdef __cplusplus
}
#endif
#endif
