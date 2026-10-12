#include "internal.h"

#include <stdlib.h>

quantapdf_status quantapdf_merge_outputs(
    const quantapdf_output *const *inputs,
    size_t input_count,
    quantapdf_output **out_output)
{
    const unsigned char **data;
    size_t *sizes;
    quantapdf_output *output;
    quantapdf_status status;
    size_t index;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (inputs == NULL || input_count == 0 ||
        input_count > SIZE_MAX / sizeof(*data) ||
        input_count > SIZE_MAX / sizeof(*sizes))
        return QUANTAPDF_ERROR_ARGUMENT;
    for (index = 0; index < input_count; ++index) {
        if (inputs[index] == NULL)
            return QUANTAPDF_ERROR_ARGUMENT;
    }

    data = (const unsigned char **)malloc(input_count * sizeof(*data));
    sizes = (size_t *)malloc(input_count * sizeof(*sizes));
    output = (quantapdf_output *)calloc(1, sizeof(*output));
    if (data == NULL || sizes == NULL || output == NULL) {
        free(data);
        free(sizes);
        free(output);
        return QUANTAPDF_ERROR_NOMEM;
    }
    for (index = 0; index < input_count; ++index) {
        data[index] = inputs[index]->data;
        sizes[index] = inputs[index]->size;
    }
    status = quantapdf_builtin_backend_ops()->merge_memory(
        data,
        sizes,
        input_count,
        &output->data,
        &output->size);
    free(data);
    free(sizes);
    if (status != QUANTAPDF_OK) {
        free(output);
        return status;
    }
    *out_output = output;
    return QUANTAPDF_OK;
}
