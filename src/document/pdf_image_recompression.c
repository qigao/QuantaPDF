#include "core/output_internal.h"
#include "document/document_internal.h"

#include <stdlib.h>

quantapdf_status quantapdf_recompress_images(
    quantapdf_document *document,
    const quantapdf_image_recompression_options *options,
    quantapdf_output **out_output)
{
    const size_t cap_field_size =
        offsetof(
            quantapdf_image_recompression_options,
            max_decoded_bytes_per_image) +
        sizeof(size_t);
    size_t max_decoded_bytes_per_image =
        QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES;
    quantapdf_output *output;
    quantapdf_status status;

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (document == NULL || document->backend == NULL ||
        options == NULL ||
        options->struct_size <
            QUANTAPDF_IMAGE_RECOMPRESSION_OPTIONS_V1_MIN_SIZE ||
        options->jpeg_quality < 1 || options->jpeg_quality > 100)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (options->struct_size >= cap_field_size &&
        options->max_decoded_bytes_per_image != 0)
        max_decoded_bytes_per_image = options->max_decoded_bytes_per_image;

    status = quantapdf_output_create_internal(document->context, &output);
    if (status != QUANTAPDF_OK)
        return status;

    status = document->ops->recompress_images(
        document->backend,
        options->jpeg_quality,
        max_decoded_bytes_per_image,
        &output->data,
        &output->size);
    if (status != QUANTAPDF_OK) {
        quantapdf_drop_output(output);
        return status;
    }

    *out_output = output;
    return QUANTAPDF_OK;
}
