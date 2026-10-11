#include "internal.h"

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
    quantapdf_backend_image_recompression_test_stats test_stats = {0};
    int test_fault = 0;

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

    output = (quantapdf_output *)calloc(1, sizeof(*output));
    if (output == NULL)
        return QUANTAPDF_ERROR_NOMEM;
#if defined(QUANTAPDF_TESTING)
    test_fault = document->test_image_fault;
    document->test_image_fault = 0;
    document->test_image_unique_count = 0;
    document->test_image_provider_registrations = 0;
    document->test_image_provider_invocations = 0;
    document->test_image_decoded_preflight_bytes = 0;
    document->test_image_every_provider_once = 0;
#endif
    status = document->ops->recompress_images(
        document->backend,
        options->jpeg_quality,
        max_decoded_bytes_per_image,
        test_fault,
#if defined(QUANTAPDF_TESTING)
        &test_stats,
#else
        NULL,
#endif
        &output->data,
        &output->size);
#if defined(QUANTAPDF_TESTING)
    document->test_image_unique_count = test_stats.unique_images;
    document->test_image_provider_registrations =
        test_stats.provider_registrations;
    document->test_image_provider_invocations = test_stats.provider_invocations;
    document->test_image_decoded_preflight_bytes =
        test_stats.decoded_preflight_bytes;
    document->test_image_every_provider_once = test_stats.every_provider_once;
#endif
    if (status != QUANTAPDF_OK) {
        free(output->data);
        free(output);
        return status;
    }

    *out_output = output;
    return QUANTAPDF_OK;
}
