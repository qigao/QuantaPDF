#ifndef QUANTAPDF_GRAPH_PAGE_DEVICE_H
#define QUANTAPDF_GRAPH_PAGE_DEVICE_H

#include "graph_page_geometry.h"
#include <stddef.h>
#include <stdint.h>

/* Private C11 page/device contract. No backing graph, source, image or
 * renderer pointers escape. The affine maps original PDF Y-up coordinates
 * directly into device Y-down coordinates (pixel edges, not pixel centers).
 * Inputs are the selected Graph geometry and explicit caller limits.
 * A consumer samples pixel (i,j) at (i+0.5,j+0.5).
 * DPI chooses ceil(width_points * DPI/72) and ceil(height_points * DPI/72).
 * Affine scales then use those EXACT integer bitmap extents divided by
 * the unrounded displayed-page width/height, independently for X and Y.
 * Only 72, 96 and 144 DPI and RGB8 (3 bytes/pixel) are admitted in v0.
 */
typedef struct quantapdf_graph_device_grid {
    uint32_t width;
    uint32_t height;
    size_t stride_bytes;
    size_t output_bytes;
    double from_source[6]; /* X=a*x+c*y+e; Y=b*x+d*y+f */
} quantapdf_graph_device_grid;

/* Zero output before any failure, reject unsupported geometries before
 * allocation, and check width/height, pixels, stride and bytes explicitly.
 * max_pixels/max_output_bytes must both be nonzero. Crop outside MediaBox is
 * intentionally unsupported by the upstream page_display_map.
 * This only calculates geometry; it does NOT admit native rendering.
 */
#ifdef __cplusplus
extern "C" {
#endif

quantapdf_status quantapdf_graph_page_device_grid(
    const quantapdf_graph_page_geometry *geometry,
    unsigned int dpi, size_t max_pixels, size_t max_output_bytes,
    quantapdf_graph_device_grid *out_grid);

#ifdef __cplusplus
}
#endif

#endif
