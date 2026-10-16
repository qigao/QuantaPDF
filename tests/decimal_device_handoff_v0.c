/* #360: exercise the internal Graph owner's value-only handoff.
 * No separate copy of affine/math code remains in the test suite. */
#include "decimal_device_handoff_v0.h"
#include "graph/graph_page_decimal_grid_v0.h"

quantapdf_status quantapdf_test_graph_device_handoff_v0(
    const quantapdf_graph *graph,size_t page_index,
    size_t max_parent_steps,unsigned dpi,size_t max_pixels,
    size_t max_output_bytes,quantapdf_graph_device_grid *out_grid)
{
    return quantapdf_graph_page_decimal_handoff_v0(
        graph,page_index,max_parent_steps,dpi,max_pixels,
        max_output_bytes,out_grid);
}
