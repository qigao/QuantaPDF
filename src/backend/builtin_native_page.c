#include "backend/builtin_native_page.h"
#include "backend/builtin_page_view.h"
#include "backend/native_prepared_page_v1.h"
#include <string.h>

quantapdf_status quantapdf_builtin_render_page(
    void *builtin_page,
    const quantapdf_native_page_limits *limits,
    const quantapdf_native_page_device *device,
    quantapdf_context_lease_internal *lease,
    quantapdf_ir_raster_image *out)
{
    quantapdf_ir ir={0};
    quantapdf_native_source_page_v1 prepared={0};
    memset(out,0,sizeof(*out));
    const quantapdf_builtin_page_view page=quantapdf_builtin_page_borrow(builtin_page);
    quantapdf_status status=quantapdf_qpdf_page_to_ir(page.document,page.page_index,&limits->source,&ir);
    if(status==QUANTAPDF_OK)
        status=quantapdf_native_source_page_prepare_context_v1(
            &ir,&limits->raster,device,lease,&prepared);
    if(status==QUANTAPDF_OK)
        status=quantapdf_native_source_page_execute_v1(&prepared,NULL,out);
    quantapdf_native_source_page_dispose_v1(&prepared);
    quantapdf_ir_dispose(&ir);
    return status;
}
