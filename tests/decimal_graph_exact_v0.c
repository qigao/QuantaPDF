/* #360: TEST-ONLY, provider-neutral C11 exact decimal PAGE SIZE preflight.
 * Never linked into a production SDK/backend, never paints or allocates. */
#include "decimal_graph_exact_v0.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

typedef struct decimal_v0 {
    int64_t units;
    unsigned scale;
} decimal_v0;

static const uint64_t powers10[] = {
    UINT64_C(1),UINT64_C(10),UINT64_C(100),UINT64_C(1000),
    UINT64_C(10000),UINT64_C(100000),UINT64_C(1000000),
    UINT64_C(10000000),UINT64_C(100000000),UINT64_C(1000000000)
};

quantapdf_status quantapdf_test_decimal_exact_token_v0(
    const unsigned char *bytes,size_t size,
    int64_t *out_units,unsigned *out_scale)
{
    size_t start=0,decimal_at=SIZE_MAX,stop,frac=0;
    uint64_t significand=0;
    int negative=0,any_digit=0;
    if (out_units != NULL) *out_units=0;
    if (out_scale != NULL) *out_scale=0;
    if (bytes==NULL || out_units==NULL || out_scale==NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (size==0) return QUANTAPDF_ERROR_FORMAT;
    if (size>64u) return QUANTAPDF_ERROR_UNSUPPORTED;
    if (bytes[0]=='+' || bytes[0]=='-') {
        negative=bytes[0]=='-'; ++start;
    }
    for (size_t i=start;i<size;++i) {
        const unsigned char c=bytes[i];
        if (c=='.' && decimal_at==SIZE_MAX) {
            decimal_at=i; continue;
        }
        if (c<'0' || c>'9') return QUANTAPDF_ERROR_FORMAT;
        any_digit=1;
    }
    if (!any_digit) return QUANTAPDF_ERROR_FORMAT;
    stop=size;
    if (decimal_at!=SIZE_MAX) {
        frac=size-decimal_at-1u;
        while (frac!=0u && bytes[stop-1u]=='0') {
            --stop; --frac;
        }
    }
    if (frac>9u) return QUANTAPDF_ERROR_UNSUPPORTED;
    for (size_t i=start;i<stop;++i) {
        const unsigned char c=bytes[i];
        if (c=='.') continue;
        if (significand>(((uint64_t)INT64_MAX)-(uint64_t)(c-'0'))/10u)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        significand=significand*10u+(uint64_t)(c-'0');
    }
    *out_units=negative?-(int64_t)significand:(int64_t)significand;
    *out_scale=(unsigned)frac;
    return QUANTAPDF_OK;
}

static quantapdf_status node_decimal(
    const quantapdf_graph_model *model,quantapdf_local_id id,decimal_v0 *out)
{
    const quantapdf_graph_node *node=quantapdf_graph_node_at(model,id);
    if (node==NULL) return QUANTAPDF_ERROR_BACKEND;
    if (node->kind==QUANTAPDF_GRAPH_INTEGER) {
        out->units=node->integer; out->scale=0u; return QUANTAPDF_OK;
    }
    if (node->kind!=QUANTAPDF_GRAPH_REAL) return QUANTAPDF_ERROR_FORMAT;
    if (node->data_offset>model->byte_count ||
        node->data_size>model->byte_count-node->data_offset)
        return QUANTAPDF_ERROR_BACKEND;
    return quantapdf_test_decimal_exact_token_v0(
        model->bytes+node->data_offset,node->data_size,
        &out->units,&out->scale);
}

static quantapdf_status box_decimals(
    const quantapdf_graph_model *model,quantapdf_local_id id,decimal_v0 out[4])
{
    const quantapdf_graph_node *box=quantapdf_graph_node_at(model,id);
    if (box==NULL) return QUANTAPDF_ERROR_BACKEND;
    if (box->kind!=QUANTAPDF_GRAPH_ARRAY || box->edge_count!=4u)
        return QUANTAPDF_ERROR_FORMAT;
    if (box->edge_offset>model->edge_count ||
        box->edge_count>model->edge_count-box->edge_offset)
        return QUANTAPDF_ERROR_BACKEND;
    for (size_t i=0;i<4u;++i) {
        quantapdf_status s=node_decimal(
            model,model->edges[box->edge_offset+i].target,&out[i]);
        if (s!=QUANTAPDF_OK) return s;
    }
    return QUANTAPDF_OK;
}

static quantapdf_status scaled_unit(int64_t value,uint64_t factor,int64_t *out)
{
    const int64_t f=(int64_t)factor; /* factor is at most 10^9 */
    if ((value>0 && value>INT64_MAX/f) ||
        (value<0 && value<INT64_MIN/f))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    *out=value*f;
    return QUANTAPDF_OK;
}

static quantapdf_status align_decimal(
    decimal_v0 a,decimal_v0 b,int64_t *x,int64_t *y,uint64_t *denom)
{
    const unsigned scale=a.scale>b.scale?a.scale:b.scale;
    quantapdf_status s;
    if (scale>9u) return QUANTAPDF_ERROR_UNSUPPORTED;
    s=scaled_unit(a.units,powers10[scale-a.scale],x);
    if (s!=QUANTAPDF_OK) return s;
    s=scaled_unit(b.units,powers10[scale-b.scale],y);
    if (s!=QUANTAPDF_OK) return s;
    *denom=powers10[scale];
    return QUANTAPDF_OK;
}

static quantapdf_status decimal_compare(
    decimal_v0 a,decimal_v0 b,int *out_cmp)
{
    int64_t x=0,y=0;
    uint64_t denom=0;
    quantapdf_status s=align_decimal(a,b,&x,&y,&denom);
    if (s!=QUANTAPDF_OK) return s;
    (void)denom;
    *out_cmp=x<y?-1:x>y?1:0;
    return QUANTAPDF_OK;
}

static quantapdf_status exact_span(
    decimal_v0 low,decimal_v0 high,uint64_t *span,uint64_t *denom)
{
    int64_t lo=0,hi=0;
    quantapdf_status s=align_decimal(low,high,&lo,&hi,denom);
    if (s!=QUANTAPDF_OK) return s;
    if (hi<=lo) return QUANTAPDF_ERROR_FORMAT;
    /* Well-defined unsigned subtraction even if the positive span
     * is larger than INT64_MAX. No signed subtraction overflow. */
    *span=(uint64_t)hi-(uint64_t)lo;
    return QUANTAPDF_OK;
}

static uint64_t gcd64(uint64_t a,uint64_t b)
{
    while (b!=0u) { const uint64_t t=a%b; a=b; b=t; }
    return a;
}

static quantapdf_status exact_ceiling_dimension(
    uint64_t span,uint64_t span_denom,decimal_v0 unit,
    unsigned dpi,uint32_t *out_dim)
{
    uint64_t n[3]={span,(uint64_t)unit.units,(uint64_t)dpi};
    uint64_t d[3]={span_denom,powers10[unit.scale],72u};
    uint64_t numerator=1u,denominator=1u;
    if (span==0u || unit.units<=0 || unit.scale>9u)
        return QUANTAPDF_ERROR_FORMAT;
    for (size_t i=0u;i<3u;++i)
        for (size_t j=0u;j<3u;++j) {
            const uint64_t g=gcd64(n[i],d[j]);
            if (g>1u) { n[i]/=g; d[j]/=g; }
        }
    for (size_t i=0u;i<3u;++i) {
        if (n[i]!=0u && numerator>UINT64_MAX/n[i])
            return QUANTAPDF_ERROR_UNSUPPORTED;
        numerator*=n[i];
        if (d[i]!=0u && denominator>UINT64_MAX/d[i])
            return QUANTAPDF_ERROR_UNSUPPORTED;
        denominator*=d[i];
    }
    if (denominator==0u || numerator==0u) return QUANTAPDF_ERROR_UNSUPPORTED;
    {
        const uint64_t size=numerator/denominator+
            (numerator%denominator!=0u);
        if (size==0u || size>(uint64_t)UINT32_MAX)
            return QUANTAPDF_ERROR_UNSUPPORTED;
        *out_dim=(uint32_t)size;
    }
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_test_graph_exact_decimal_grid_v0(
    const quantapdf_graph *graph,size_t page_index,size_t max_parent_steps,
    unsigned dpi,size_t max_pixels,size_t max_output_bytes,
    quantapdf_test_decimal_grid_v0 *out)
{
    quantapdf_graph_page_geometry geometry;
    quantapdf_graph_page_display display;
    const quantapdf_graph_model *model;
    const quantapdf_graph_edge *root_edge,*entry;
    quantapdf_local_id current,root,media=0,crop=0;
    decimal_v0 media_values[4]={{0}},crop_values[4]={{0}},unit={1,0};
    quantapdf_test_decimal_grid_v0 result={0};
    uint64_t span_w=0,span_h=0,denom_w=0,denom_h=0;
    uint32_t width=0,height=0;
    quantapdf_status status;
    if (out!=NULL) memset(out,0,sizeof(*out));
    if (out==NULL || graph==NULL || max_parent_steps==0 ||
        max_pixels==0 || max_output_bytes==0)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (dpi!=72u && dpi!=96u && dpi!=144u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    status=quantapdf_graph_query_page_geometry(
        graph,page_index,max_parent_steps,&geometry);
    if (status!=QUANTAPDF_OK) return status;
    status=quantapdf_graph_page_display_map(&geometry,&display);
    if (status!=QUANTAPDF_OK) return status;
    (void)display; /* Validates canonical existing crop/media admission. */
    model=quantapdf_graph_view(graph);
    if (model==NULL) return QUANTAPDF_ERROR_STATE;
    root_edge=quantapdf_graph_find(model,model->catalog,"/Pages",6u);
    if (root_edge==NULL) return QUANTAPDF_ERROR_FORMAT;
    root=root_edge->target;
    current=model->edges[model->page_offset+page_index].target;
    entry=quantapdf_graph_find(model,current,"/UserUnit",9u);
    if (entry!=NULL) {
        status=node_decimal(model,entry->target,&unit);
        if (status!=QUANTAPDF_OK) return status;
        if (unit.units<=0) return QUANTAPDF_ERROR_FORMAT;
    }
    for (size_t step=0;step<max_parent_steps && step<model->node_count;++step) {
        if (media==0u) {
            entry=quantapdf_graph_find(model,current,"/MediaBox",9u);
            if (entry!=NULL) media=entry->target;
        }
        if (crop==0u) {
            entry=quantapdf_graph_find(model,current,"/CropBox",8u);
            if (entry!=NULL) crop=entry->target;
        }
        if (current==root) break;
        entry=quantapdf_graph_find(model,current,"/Parent",7u);
        if (entry==NULL) return QUANTAPDF_ERROR_FORMAT;
        current=entry->target;
    }
    if (current!=root || media==0u) return QUANTAPDF_ERROR_FORMAT;
    if (crop==0u) crop=media;
    status=box_decimals(model,media,media_values);
    if (status!=QUANTAPDF_OK) return status;
    status=box_decimals(model,crop,crop_values);
    if (status!=QUANTAPDF_OK) return status;
    for (size_t axis=0;axis<2u;++axis) {
        int cmp=0;
        status=decimal_compare(media_values[axis],
                               media_values[axis+2u],&cmp);
        if (status!=QUANTAPDF_OK) return status;
        if (cmp>=0) return QUANTAPDF_ERROR_FORMAT;
        status=decimal_compare(crop_values[axis],
                               crop_values[axis+2u],&cmp);
        if (status!=QUANTAPDF_OK) return status;
        if (cmp>=0) return QUANTAPDF_ERROR_FORMAT;
        status=decimal_compare(crop_values[axis],
                               media_values[axis],&cmp);
        if (status!=QUANTAPDF_OK) return status;
        if (cmp<0) return QUANTAPDF_ERROR_UNSUPPORTED;
        status=decimal_compare(crop_values[axis+2u],
                               media_values[axis+2u],&cmp);
        if (status!=QUANTAPDF_OK) return status;
        if (cmp>0) return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    status=exact_span(crop_values[0],crop_values[2],&span_w,&denom_w);
    if (status!=QUANTAPDF_OK) return status;
    status=exact_span(crop_values[1],crop_values[3],&span_h,&denom_h);
    if (status!=QUANTAPDF_OK) return status;
    status=exact_ceiling_dimension(span_w,denom_w,unit,dpi,&width);
    if (status!=QUANTAPDF_OK) return status;
    status=exact_ceiling_dimension(span_h,denom_h,unit,dpi,&height);
    if (status!=QUANTAPDF_OK) return status;
    if (geometry.rotation==90 || geometry.rotation==270) {
        const uint32_t swap=width; width=height; height=swap;
    }
    if ((size_t)width>max_pixels/(size_t)height ||
        (size_t)width>SIZE_MAX/3u)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    result.width=width; result.height=height;
    result.stride_bytes=(size_t)width*3u;
    if (result.stride_bytes>max_output_bytes/(size_t)height)
        return QUANTAPDF_ERROR_UNSUPPORTED;
    result.output_bytes=result.stride_bytes*(size_t)height;
    *out=result;
    return QUANTAPDF_OK;
}
