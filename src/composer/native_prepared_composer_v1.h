#ifndef QUANTAPDF_NATIVE_PREPARED_COMPOSER_V1_H
#define QUANTAPDF_NATIVE_PREPARED_COMPOSER_V1_H

#include "composer/composer_internal.h"
#include "backend/native_prepared_page_v1.h"

/* PRIVATE synchronous Composer borrow into the SAME bounded compiler.
 * The caller holds a retained, same-Context guard for the complete call and
 * externally serializes Composer mutation/drop. No prepared handle or RAW
 * pointer escapes. Publication returns detached, ordinary-dispose RGBA.
 * This does not change Composer finish or admit new public Source syntax. */
static inline int qp_composer_capacity_bytes(
    size_t *bytes,size_t count,size_t capacity,size_t element_size)
{
    return capacity>=count && qp_prepared_bytes(bytes,capacity-count,element_size);
}

static inline quantapdf_status quantapdf_native_composer_execute_v1(
    const quantapdf_composer *composer,const quantapdf_context_lease_internal *domain,
    const quantapdf_ir_raster_scoped_mixed_v5_limits *limits,
    const quantapdf_native_page_device *device,
    quantapdf_native_execution_receipt_v1 *receipt,quantapdf_ir_raster_image *out)
{
    quantapdf_ir view;
    quantapdf_composer_operation operations[4];
    quantapdf_native_prepared_page_v1 metadata;
    quantapdf_native_compiled_page_v1 compiled;
    quantapdf_status status;
    size_t extra=0u;
    if(receipt!=NULL)memset(receipt,0,sizeof(*receipt));
    if(out==NULL)return QUANTAPDF_ERROR_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(composer==NULL || limits==NULL || device==NULL ||
       !quantapdf_context_lease_valid_internal(domain) ||
       composer->context==NULL || domain->context!=composer->context)
        return QUANTAPDF_ERROR_ARGUMENT;
    if(device->page_index!=0u || composer->ir.page_count!=1u ||
       composer->ir.operation_count>4u ||
       (composer->ir.operation_count!=0u && composer->ir.operations==NULL))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    view=composer->ir;
    if(!qp_composer_capacity_bytes(&extra,view.page_count,composer->page_capacity,sizeof(*view.pages)) ||
       !qp_composer_capacity_bytes(&extra,view.operation_count,composer->operation_capacity,sizeof(*view.operations)) ||
       !qp_composer_capacity_bytes(&extra,view.resources.image_count,view.resources.image_capacity,sizeof(*view.resources.images)) ||
       !qp_composer_capacity_bytes(&extra,view.resources.graphics_state_count,view.resources.graphics_state_capacity,sizeof(*view.resources.graphics_states)) ||
       !qp_composer_capacity_bytes(&extra,view.resources.clip_count,view.resources.clip_capacity,sizeof(*view.resources.clips)))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    /* Spare allocated slots are not hidden resources. Keep ALL committed
     * records/references, and charge their actual retained capacity above. */
    view.resources.image_capacity=view.resources.image_count;
    view.resources.graphics_state_capacity=view.resources.graphics_state_count;
    view.resources.clip_capacity=view.resources.clip_count;
    for(size_t i=0u;i<view.operation_count;++i) {
        operations[i]=view.operations[i];
        if(operations[i].kind==QUANTAPDF_COMPOSER_OPERATION_PATH) {
            quantapdf_affine_transform *t=&operations[i].value.path.options.transform;
            const int zero=t->a==0.0f && t->b==0.0f && t->c==0.0f &&
                t->d==0.0f && t->e==0.0f && t->f==0.0f;
            if(!zero && !(t->a==1.0f && t->b==0.0f && t->c==0.0f &&
                          t->d==1.0f && t->e==0.0f && t->f==0.0f))
                return QUANTAPDF_ERROR_UNSUPPORTED;
            memset(t,0,sizeof(*t));
        }
    }
    view.operations=view.operation_count==0u?NULL:operations;
    status=quantapdf_native_prepare_page_v1(&view,limits,device,
        &metadata);
    if(status!=QUANTAPDF_OK)return status;
    if(!qp_prepared_charge(&metadata.usage.retained_bytes,extra,SIZE_MAX) ||
       !qp_prepared_charge(&metadata.resource_limits.max_live_bytes,extra,SIZE_MAX))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    status=quantapdf_native_compiled_page_compile_v1(&metadata,&compiled);
    if(status==QUANTAPDF_OK)
        status=quantapdf_native_compiled_page_execute_v1(&compiled,receipt,out);
    quantapdf_native_compiled_page_dispose_v1(&compiled);
    return status;
}

#endif
