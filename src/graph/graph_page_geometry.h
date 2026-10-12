#ifndef QUANTAPDF_GRAPH_PAGE_GEOMETRY_H
#define QUANTAPDF_GRAPH_PAGE_GEOMETRY_H

#include "graph.h"

/* Selected effective PDF page attributes in default user space. The media,
 * crop and rotation values inherit from the nearest /Pages ancestor; UserUnit
 * belongs to the leaf /Page only. No source-owned pointers escape. */
typedef struct quantapdf_graph_page_geometry {
    double media_box[4];
    double crop_box[4];
    double user_unit;
    int rotation; /* normalized to 0, 90, 180 or 270 */
} quantapdf_graph_page_geometry;

#ifdef __cplusplus
extern "C" {
#endif

/* Validate the indexed page's real /Parent -> /Kids ancestry all the way to
 * Catalog /Pages. max_parent_steps counts the leaf and root, is nonzero and
 * bounds traversal; an actual cycle/mismatch is FORMAT. Returned values
 * belong to the selected sealed graph version, never a source provider. */
quantapdf_status quantapdf_graph_query_page_geometry(
    const quantapdf_graph *graph, size_t page_index, size_t max_parent_steps,
    quantapdf_graph_page_geometry *out_geometry);

/* Strict subset supported by the current Composer path lowering. No page
 * transform is synthesized: require zero-origin equal Media/Crop boxes,
 * effective rotation 0 and UserUnit 1. Other valid geometry: UNSUPPORTED.
 * Always reset dimension outputs before failure. */
quantapdf_status quantapdf_graph_canonical_page_dimensions(
    const quantapdf_graph_page_geometry *geometry,
    double *out_width, double *out_height);


/* Source default user space -> visible unrotated Composer PDF canvas.
 * The original /CropBox (when present) defines the visible region; a page
 * /Rotate applies clockwise viewing rotation, and /UserUnit scales points.
 * The row-vector matrix maps [x y 1] to output PDF user coordinates.
 * Unlike canonical_page_dimensions this supports translated, cropped,
 * right-angle-rotated, nonunit page geometry. Non-cropped MediaBox pixels
 * are not transferred. A CropBox extending beyond MediaBox is UNSUPPORTED
 * until its intersection semantics are independently qualified.
 * No PDF parser or rendering provider is called. */
typedef struct quantapdf_graph_page_display {
    double width_points, height_points;
    double from_source[6]; /* [a b c d e f] row-vector affine */
} quantapdf_graph_page_display;

/* Malformed values: ARGUMENT. Valid but unrepresentable/overflow dimensions
 * or CropBox beyond MediaBox: UNSUPPORTED. Always clear the output first. */
quantapdf_status quantapdf_graph_page_display_map(
    const quantapdf_graph_page_geometry *geometry,
    quantapdf_graph_page_display *out_display);

#ifdef __cplusplus
}
#endif
#endif
