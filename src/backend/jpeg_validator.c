#include "qpdf_composer.h"

#include <stddef.h>
#include <stdio.h>
#include <jpeglib.h>
#include <jerror.h>

#include <limits.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
typedef struct quantapdf_jpeg_error_context {
    struct jpeg_error_mgr manager;
    jmp_buf jump;
    unsigned char *scanline;
    unsigned char *pixels;
    quantapdf_status status;
    int destroyable;
} quantapdf_jpeg_error_context;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

static void quantapdf_jpeg_fail(j_common_ptr common)
{
    quantapdf_jpeg_error_context *context =
        (quantapdf_jpeg_error_context *)common->err;
    context->status = common->err->msg_code == JERR_OUT_OF_MEMORY
        ? QUANTAPDF_ERROR_NOMEM
        : QUANTAPDF_ERROR_FORMAT;
    longjmp(context->jump, 1);
}

static void quantapdf_jpeg_emit_message(
    j_common_ptr common,
    int message_level)
{
    if (message_level < 0)
        quantapdf_jpeg_fail(common);
}

static int quantapdf_size_multiply(
    size_t left,
    size_t right,
    size_t *out)
{
    if (left != 0u && right > SIZE_MAX / left)
        return 0;
    *out = left * right;
    return 1;
}

#if defined(QUANTAPDF_TESTING)
static int quantapdf_jpeg_force_oom;

void quantapdf_jpeg_force_oom_for_testing(int enabled)
{
    quantapdf_jpeg_force_oom = enabled;
}
#endif

static quantapdf_status quantapdf_jpeg_read(
    const unsigned char *data,
    size_t size,
    size_t max_working_bytes,
    int color_transform,
    unsigned char **out_pixels,
    uint32_t *out_width,
    uint32_t *out_height,
    int *out_components)
{
    struct jpeg_decompress_struct *decoder = NULL;
    quantapdf_jpeg_error_context *errors = NULL;
    quantapdf_status status = QUANTAPDF_ERROR_FORMAT;
    size_t row_size = 0u;
    size_t decoded_size = 0u;

    if (out_width == NULL || out_height == NULL || out_components == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_width = 0u;
    *out_height = 0u;
    *out_components = 0;
    if (out_pixels != NULL)
        *out_pixels = NULL;
    if (color_transform < -1 || color_transform > 1)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (data == NULL || size == 0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (size > (size_t)ULONG_MAX)
        return QUANTAPDF_ERROR_UNSUPPORTED;

    decoder = (struct jpeg_decompress_struct *)calloc(1u, sizeof(*decoder));
    errors = (quantapdf_jpeg_error_context *)calloc(1u, sizeof(*errors));
    if (errors != NULL)
        errors->status = QUANTAPDF_ERROR_NOMEM;
    if (decoder == NULL || errors == NULL) {
        status = QUANTAPDF_ERROR_NOMEM;
        goto cleanup;
    }
    decoder->err = jpeg_std_error(&errors->manager);
    errors->manager.error_exit = quantapdf_jpeg_fail;
    errors->manager.emit_message = quantapdf_jpeg_emit_message;
    errors->status = QUANTAPDF_ERROR_FORMAT;
    if (setjmp(errors->jump) != 0)
        goto cleanup;
    errors->destroyable = 1;
#if defined(QUANTAPDF_TESTING)
    if (quantapdf_jpeg_force_oom == 1) {
        errors->manager.msg_code = JERR_OUT_OF_MEMORY;
        quantapdf_jpeg_fail((j_common_ptr)decoder);
    }
#endif
    jpeg_create_decompress(decoder);
#if defined(QUANTAPDF_TESTING)
    if (quantapdf_jpeg_force_oom == 2) {
        errors->manager.msg_code = JERR_OUT_OF_MEMORY;
        quantapdf_jpeg_fail((j_common_ptr)decoder);
    }
#endif
    jpeg_mem_src(decoder, data, (unsigned long)size);
    if (jpeg_read_header(decoder, TRUE) != JPEG_HEADER_OK ||
        (decoder->num_components != 1 && decoder->num_components != 3))
        goto cleanup;
    if (decoder->num_components == 3 && color_transform >= 0)
        decoder->jpeg_color_space = color_transform == 0 ? JCS_RGB : JCS_YCbCr;
    decoder->out_color_space = decoder->num_components == 1
        ? JCS_GRAYSCALE
        : JCS_RGB;
    if (!quantapdf_size_multiply(
            (size_t)decoder->image_width,
            (size_t)decoder->num_components,
            &row_size) ||
        !quantapdf_size_multiply(
            row_size, (size_t)decoder->image_height, &decoded_size) ||
        size > max_working_bytes ||
        decoded_size > max_working_bytes - size || decoder->progressive_mode) {
        errors->status = QUANTAPDF_ERROR_UNSUPPORTED;
        goto cleanup;
    }
    if (!jpeg_start_decompress(decoder) || decoder->output_width == 0u ||
        decoder->output_height == 0u ||
        (decoder->output_components != 1 && decoder->output_components != 3) ||
        !quantapdf_size_multiply(
            (size_t)decoder->output_width,
            (size_t)decoder->output_components,
            &row_size) ||
        !quantapdf_size_multiply(
            row_size, (size_t)decoder->output_height, &decoded_size) ||
        size > max_working_bytes ||
        decoded_size > max_working_bytes - size) {
        errors->status = QUANTAPDF_ERROR_UNSUPPORTED;
        goto cleanup;
    }
    if (out_pixels != NULL)
        errors->pixels = (unsigned char *)malloc(decoded_size);
    else
        errors->scanline = (unsigned char *)malloc(row_size);
    if (errors->scanline == NULL && errors->pixels == NULL) {
        errors->status = QUANTAPDF_ERROR_NOMEM;
        goto cleanup;
    }
    while (decoder->output_scanline < decoder->output_height) {
        JSAMPROW row = errors->pixels != NULL
            ? errors->pixels + (size_t)decoder->output_scanline * row_size
            : errors->scanline;
        if (jpeg_read_scanlines(decoder, &row, 1u) != 1u)
            quantapdf_jpeg_fail((j_common_ptr)decoder);
    }
    if (!jpeg_finish_decompress(decoder))
        quantapdf_jpeg_fail((j_common_ptr)decoder);

    *out_width = (uint32_t)decoder->output_width;
    *out_height = (uint32_t)decoder->output_height;
    *out_components = decoder->output_components;
    errors->status = QUANTAPDF_OK;

cleanup:
    if (errors != NULL) {
        status = errors->status;
        if (status == QUANTAPDF_OK && out_pixels != NULL) {
            *out_pixels = errors->pixels;
            errors->pixels = NULL;
        }
        free(errors->scanline);
        free(errors->pixels);
        if (errors->destroyable)
            jpeg_destroy_decompress(decoder);
    }
    free(errors);
    free(decoder);
    return status;
}

quantapdf_status quantapdf_jpeg_validate(
    const unsigned char *data, size_t size, size_t max_working_bytes,
    uint32_t *out_width, uint32_t *out_height, int *out_components)
{
    return quantapdf_jpeg_read(data, size, max_working_bytes, -1, NULL,
        out_width, out_height, out_components);
}

quantapdf_status quantapdf_jpeg_decode(
    const unsigned char *data, size_t size, size_t max_working_bytes,
    int color_transform, unsigned char **out_pixels,
    uint32_t *out_width, uint32_t *out_height, int *out_components)
{
    if (out_pixels == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_pixels = NULL;
    return quantapdf_jpeg_read(data, size, max_working_bytes, color_transform,
        out_pixels, out_width, out_height, out_components);
}
