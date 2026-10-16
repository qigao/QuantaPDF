/* #360: tests now execute the real, privately compiled Graph module.
 * These adapters retain the prequalified test conformance signatures,
 * with NO duplicate numerical implementation or fallback. */
#include "decimal_graph_exact_v0.h"
#include "graph/graph_page_decimal_grid_v0.h"
#include <string.h>

quantapdf_status quantapdf_test_decimal_exact_token_v0(
    const unsigned char *bytes,size_t size,
    int64_t *out_units,unsigned *out_scale)
{
    return quantapdf_graph_decimal_parse_token_v0(
        bytes,size,out_units,out_scale);
}

quantapdf_status quantapdf_test_graph_exact_decimal_grid_v0(
    const quantapdf_graph *graph,size_t page_index,
    size_t max_parent_steps,unsigned dpi,size_t max_pixels,
    size_t max_output_bytes,quantapdf_test_decimal_grid_v0 *out)
{
    quantapdf_graph_decimal_size_v0 exact={0};
    quantapdf_status status;
    if(out!=NULL) memset(out,0,sizeof(*out));
    if(out==NULL) return QUANTAPDF_ERROR_ARGUMENT;
    status=quantapdf_graph_page_decimal_exact_size_v0(
        graph,page_index,max_parent_steps,dpi,max_pixels,
        max_output_bytes,&exact);
    if(status!=QUANTAPDF_OK) return status;
    out->width=exact.width;
    out->height=exact.height;
    out->stride_bytes=exact.stride_bytes;
    out->output_bytes=exact.output_bytes;
    return QUANTAPDF_OK;
}
