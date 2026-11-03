#include "core/output_internal.h"
#include "core/context_internal.h"

#include <stdlib.h>

quantapdf_status quantapdf_merge_outputs(
    quantapdf_context *context,
    const quantapdf_output *const *inputs,
    size_t input_count,
    quantapdf_output **out_output)
{
    const unsigned char **data;
    size_t *sizes;
    quantapdf_output *output;
    quantapdf_status status;
    size_t index;

    *out_output = NULL;
    status = quantapdf_output_create_internal(context, &output);
    if (status != QUANTAPDF_OK)
        return status;
    data = (const unsigned char **)malloc(input_count * sizeof(*data));
    sizes = (size_t *)malloc(input_count * sizeof(*sizes));
    if (data == NULL || sizes == NULL) {
        free(data);
        free(sizes);
        quantapdf_drop_output(output);
        return QUANTAPDF_ERROR_NOMEM;
    }
    for (index = 0; index < input_count; ++index) {
        data[index] = inputs[index]->data;
        sizes[index] = inputs[index]->size;
    }
    status = context->ops->merge_memory(
        data,
        sizes,
        input_count,
        &output->data,
        &output->size);
    free(data);
    free(sizes);
    if (status != QUANTAPDF_OK) {
        quantapdf_drop_output(output);
        return status;
    }
    *out_output = output;
    return QUANTAPDF_OK;
}
