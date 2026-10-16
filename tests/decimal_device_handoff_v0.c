/* #360 TEST-ONLY value-only device-grid handoff. Never link into SDK. */
#include "decimal_device_handoff_v0.h"

#include <math.h>
#include <string.h>

quantapdf_status quantapdf_test_graph_device_handoff_v0(
    const quantapdf_graph *graph,size_t page_index,
    size_t max_parent_steps,unsigned dpi,size_t max_pixels,
    size_t max_output_bytes,quantapdf_graph_device_grid *out_grid)
{
    quantapdf_test_decimal_grid_v0 exact={0};
    quantapdf_graph_page_geometry geometry={0};
    quantapdf_graph_page_display display={0};
    quantapdf_graph_device_grid prepared={0};
    double scale_x,scale_y;
    quantapdf_status status;

    if (out_grid!=NULL) memset(out_grid,0,sizeof(*out_grid));
    if (out_grid==NULL || graph==NULL)
        return QUANTAPDF_ERROR_ARGUMENT;

    /* The exact integer extent is the sole source of width/height:
     * NO ceil of display.width_points or subtraction of rounded doubles. */
    status=quantapdf_test_graph_exact_decimal_grid_v0(
        graph,page_index,max_parent_steps,dpi,max_pixels,
        max_output_bytes,&exact);
    if (status!=QUANTAPDF_OK) return status;

    /* Only read Graph while its unique owner is alive. The affine formula
     * remains the existing floating Graph->display->device mapping, with
     * NO change to CropBox, rotation, source phase or pixel-center rules. */
    status=quantapdf_graph_query_page_geometry(
        graph,page_index,max_parent_steps,&geometry);
    if (status!=QUANTAPDF_OK) return status;
    status=quantapdf_graph_page_display_map(&geometry,&display);
    if (status!=QUANTAPDF_OK) return status;

    if (!isfinite(display.width_points) ||
        !isfinite(display.height_points) ||
        !(display.width_points>0.0) ||
        !(display.height_points>0.0))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    scale_x=(double)exact.width/display.width_points;
    scale_y=(double)exact.height/display.height_points;
    if (!isfinite(scale_x) || !isfinite(scale_y) ||
        !(scale_x>0.0) || !(scale_y>0.0))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    prepared.width=exact.width;
    prepared.height=exact.height;
    prepared.stride_bytes=exact.stride_bytes;
    prepared.output_bytes=exact.output_bytes;
    /* This is intentionally verbatim the qualified float-affine equation
     * in src/graph/graph_page_device.c, only the pre-ceil size changes. */
    prepared.from_source[0]=scale_x*display.from_source[0];
    prepared.from_source[1]=-scale_y*display.from_source[1];
    prepared.from_source[2]=scale_x*display.from_source[2];
    prepared.from_source[3]=-scale_y*display.from_source[3];
    prepared.from_source[4]=scale_x*display.from_source[4];
    prepared.from_source[5]=scale_y*
        (display.height_points-display.from_source[5]);
    for (size_t i=0;i<6u;++i)
        if (!isfinite(prepared.from_source[i]))
            return QUANTAPDF_ERROR_UNSUPPORTED;

    /* Publish only once, after all exact arithmetic, budgets and finite
     * affine checks succeed. Values alone survive Snapshot/DSO disposal. */
    *out_grid=prepared;
    return QUANTAPDF_OK;
}
