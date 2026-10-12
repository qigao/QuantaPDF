#include "output_internal.h"
#include "context_internal.h"

quantapdf_status quantapdf_output_create_internal(
    quantapdf_context *context, quantapdf_output **out_output)
{
    quantapdf_output *output;
    quantapdf_status status;
    *out_output = NULL;
    status = quantapdf_context_retain_internal(context);
    if (status != QUANTAPDF_OK) return status;
    output = (quantapdf_output *)quantapdf_context_allocate_internal(context, sizeof(*output));
    if (output == NULL) {
        quantapdf_drop_context(context);
        return QUANTAPDF_ERROR_NOMEM;
    }
    output->context = context;
    *out_output = output;
    return QUANTAPDF_OK;
}
