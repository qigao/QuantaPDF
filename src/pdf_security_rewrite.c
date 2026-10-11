#include "internal.h"

#include <stdlib.h>
#include <string.h>

typedef enum quantapdf_security_operation_internal {
    QUANTAPDF_SECURITY_ENCRYPT_INTERNAL = 1,
    QUANTAPDF_SECURITY_DECRYPT_INTERNAL = 2,
    QUANTAPDF_SECURITY_REENCRYPT_INTERNAL = 3
} quantapdf_security_operation_internal;

static quantapdf_status quantapdf_validate_password_ascii(
    const char *password,
    size_t *out_size)
{
    size_t size;

    if (password == NULL || out_size == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    for (size = 0; size <= 127u; ++size) {
        unsigned char const value = (unsigned char)password[size];
        if (value == 0u) {
            *out_size = size;
            return QUANTAPDF_OK;
        }
        if (value < 0x20u || value > 0x7eu)
            return QUANTAPDF_ERROR_ARGUMENT;
    }
    return QUANTAPDF_ERROR_ARGUMENT;
}

static quantapdf_status quantapdf_validate_encryption_options(
    const quantapdf_encryption_options *options)
{
    size_t user_size;
    size_t owner_size;

    if (options == NULL ||
        options->struct_size < QUANTAPDF_ENCRYPTION_OPTIONS_V1_MIN_SIZE ||
        options->method != QUANTAPDF_ENCRYPTION_AES_256 ||
        options->user_password_utf8 == NULL ||
        options->owner_password_utf8 == NULL ||
        (options->permissions & ~QUANTAPDF_PERMISSION_ALL) != 0u ||
        (options->encrypt_metadata != 0 && options->encrypt_metadata != 1) ||
        ((options->permissions &
          QUANTAPDF_PERMISSION_PRINT_HIGH_QUALITY) != 0u &&
         (options->permissions &
          QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION) == 0u))
        return QUANTAPDF_ERROR_ARGUMENT;
    if (quantapdf_validate_password_ascii(
            options->user_password_utf8, &user_size) != QUANTAPDF_OK ||
        quantapdf_validate_password_ascii(
            options->owner_password_utf8, &owner_size) != QUANTAPDF_OK ||
        owner_size == 0u ||
        (user_size == owner_size &&
         memcmp(options->user_password_utf8,
                options->owner_password_utf8,
                user_size) == 0))
        return QUANTAPDF_ERROR_ARGUMENT;
    return QUANTAPDF_OK;
}

static quantapdf_status quantapdf_security_rewrite(
    quantapdf_document *document,
    const quantapdf_encryption_options *options,
    quantapdf_security_operation_internal operation,
    quantapdf_output **out_output)
{
    quantapdf_output *output;
    quantapdf_status status;
    int test_fault = 0;
    quantapdf_backend_security_test_stats test_stats = {0};

    if (out_output == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_output = NULL;
    if (document == NULL || document->backend == NULL)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (operation != QUANTAPDF_SECURITY_DECRYPT_INTERNAL) {
        status = quantapdf_validate_encryption_options(options);
        if (status != QUANTAPDF_OK)
            return status;
    }

#if defined(QUANTAPDF_TESTING)
    test_fault = document->test_security_fault;
    document->test_security_fault = QUANTAPDF_TEST_SECURITY_FAULT_NONE;
    document->test_security_context_entries = 0;
    document->test_security_configure_requests = 0;
    document->test_security_write_requests = 0;
    document->test_security_context_exits = 0;
    if (test_fault == QUANTAPDF_TEST_SECURITY_FAULT_OUTPUT_NOMEM)
        return QUANTAPDF_ERROR_NOMEM;
#endif

    output = (quantapdf_output *)calloc(1, sizeof(*output));
    if (output == NULL)
        return QUANTAPDF_ERROR_NOMEM;
    if (operation == QUANTAPDF_SECURITY_ENCRYPT_INTERNAL) {
        status = document->ops->encrypt_pdf(
            document->backend, options, test_fault, &test_stats,
            &output->data, &output->size);
    } else if (operation == QUANTAPDF_SECURITY_DECRYPT_INTERNAL) {
        status = document->ops->decrypt_pdf(
            document->backend, &output->data, &output->size);
    } else {
        status = document->ops->reencrypt_pdf(
            document->backend, options, test_fault, &test_stats,
            &output->data, &output->size);
    }
#if defined(QUANTAPDF_TESTING)
    document->test_security_context_entries = test_stats.context_entries;
    document->test_security_configure_requests = test_stats.configure_requests;
    document->test_security_write_requests = test_stats.write_requests;
    document->test_security_context_exits = test_stats.context_exits;
#endif
    if (status != QUANTAPDF_OK) {
        free(output->data);
        free(output);
        return status;
    }
#if defined(QUANTAPDF_TESTING)
    if (test_fault == QUANTAPDF_TEST_SECURITY_FAULT_BEFORE_PUBLICATION) {
        free(output->data);
        free(output);
        return QUANTAPDF_ERROR_BACKEND;
    }
#endif
    *out_output = output;
    return QUANTAPDF_OK;
}

quantapdf_status quantapdf_encrypt_pdf(
    quantapdf_document *document,
    const quantapdf_encryption_options *options,
    quantapdf_output **out_output)
{
    return quantapdf_security_rewrite(
        document, options, QUANTAPDF_SECURITY_ENCRYPT_INTERNAL, out_output);
}

quantapdf_status quantapdf_decrypt_pdf(
    quantapdf_document *document,
    quantapdf_output **out_output)
{
    return quantapdf_security_rewrite(
        document, NULL, QUANTAPDF_SECURITY_DECRYPT_INTERNAL, out_output);
}

quantapdf_status quantapdf_reencrypt_pdf(
    quantapdf_document *document,
    const quantapdf_encryption_options *options,
    quantapdf_output **out_output)
{
    return quantapdf_security_rewrite(
        document, options, QUANTAPDF_SECURITY_REENCRYPT_INTERNAL, out_output);
}
