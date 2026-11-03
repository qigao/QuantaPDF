#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>

#include "pdf_security_rewrite_test_api.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "check failed at line %d: %s\n", \
                    __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

typedef struct quantapdf_security_inspection {
    int encrypted;
    int revision;
    int version;
    int stream_aesv3;
    int string_aesv3;
    int file_aesv3;
    int extension_level;
    int encrypt_metadata;
    int allow_accessibility;
    int allow_copy;
    int allow_assemble;
    int allow_fill_forms;
    int allow_annotate_and_fill_forms;
    int allow_modify_other;
    int allow_print_low_resolution;
    int allow_print_high_quality;
    unsigned char id1[16];
    unsigned char id2[16];
    unsigned char encryption_key[32];
    size_t encryption_key_size;
} quantapdf_security_inspection;

int quantapdf_security_inspect_pdf(
    const unsigned char *data,
    size_t size,
    const char *password,
    quantapdf_security_inspection *out);

int quantapdf_security_create_metadata_fixture(
    const char *source_path,
    const char *output_path);

int quantapdf_security_create_signature_fixture(
    const char *source_path,
    const char *output_path,
    int kind,
    int encrypt);

int quantapdf_security_create_incremental_signature_fixture(
    const char *source_path,
    const char *output_path,
    const char *password);

int quantapdf_security_create_id_fixture(
    const char *source_path,
    const char *output_path,
    int malformed);

void quantapdf_security_check_public_semantics(void);

int quantapdf_security_check_static_provider_boundary(
    const char *source_path);

_Static_assert(
    QUANTAPDF_ENCRYPTION_AES_256 == 1,
    "stable encryption method value");
_Static_assert(
    QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION == (1u << 0),
    "stable low-resolution print permission");
_Static_assert(
    QUANTAPDF_PERMISSION_PRINT_HIGH_QUALITY == (1u << 6),
    "stable high-quality print permission");

static void compile_public_surface(void)
{
    quantapdf_document *document = NULL;
    quantapdf_output *output = NULL;
    quantapdf_encryption_options options = {0};

    if (0) {
        (void)quantapdf_encrypt_pdf(document, &options, &output);
        (void)quantapdf_decrypt_pdf(document, &output);
        (void)quantapdf_reencrypt_pdf(document, &options, &output);
    }
}

static int inspect_output(
    const quantapdf_output *output,
    const char *password,
    quantapdf_security_inspection *inspection)
{
    const unsigned char *data = NULL;
    size_t size = 0;

    return quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK &&
        quantapdf_security_inspect_pdf(data, size, password, inspection);
}

static int outputs_equal(
    const quantapdf_output *left,
    const quantapdf_output *right)
{
    const unsigned char *left_data = NULL;
    const unsigned char *right_data = NULL;
    size_t left_size = 0;
    size_t right_size = 0;

    if (quantapdf_output_data(left, &left_data, &left_size) != QUANTAPDF_OK ||
        quantapdf_output_data(right, &right_data, &right_size) != QUANTAPDF_OK)
        return 0;
    return left_size == right_size &&
        memcmp(left_data, right_data, left_size) == 0;
}

static int output_contains(
    const quantapdf_output *output,
    const char *needle)
{
    const unsigned char *data = NULL;
    size_t size = 0;
    size_t needle_size = strlen(needle);
    size_t index;

    if (quantapdf_output_data(output, &data, &size) != QUANTAPDF_OK ||
        needle_size == 0u || needle_size > size)
        return 0;
    for (index = 0; index <= size - needle_size; ++index) {
        if (memcmp(data + index, needle, needle_size) == 0)
            return 1;
    }
    return 0;
}

static int test_state_transitions(void)
{
    quantapdf_document *plain = NULL;
    quantapdf_document *authenticated = NULL;
    quantapdf_output *encrypted = NULL;
    quantapdf_output *decrypted = NULL;
    quantapdf_output *reencrypted = NULL;
    quantapdf_output *sentinel = (quantapdf_output *)(uintptr_t)1u;
    quantapdf_encryption_options options = {

        QUANTAPDF_ENCRYPTION_AES_256,
        "user",
        "owner",
        QUANTAPDF_PERMISSION_ALL,
        1};
    quantapdf_encryption_options replacement = options;
    quantapdf_security_inspection inspection = {0};

    replacement.user_password_utf8 = "new-user";
    replacement.owner_password_utf8 = "new-owner";

    CHECK(quantapdf_open(test_context(), SECURITY_PLAIN_PDF, NULL, &plain) == QUANTAPDF_OK);
    CHECK(quantapdf_encrypt_pdf(plain, &options, &encrypted) == QUANTAPDF_OK);
    CHECK(inspect_output(encrypted, "user", &inspection));
    CHECK(inspection.encrypted && inspection.revision == 6 &&
          inspection.version == 5 && inspection.stream_aesv3 &&
          inspection.string_aesv3 && inspection.file_aesv3);
    CHECK(inspection.extension_level == 8);
    CHECK(quantapdf_output_save_file(encrypted, SECURITY_ENCRYPTED_OUTPUT) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(test_context(),
              SECURITY_ENCRYPTED_OUTPUT, "user", &authenticated) ==
          QUANTAPDF_OK);

    CHECK(quantapdf_decrypt_pdf(authenticated, &decrypted) == QUANTAPDF_OK);
    CHECK(inspect_output(decrypted, NULL, &inspection));
    CHECK(!inspection.encrypted);

    CHECK(quantapdf_reencrypt_pdf(
              authenticated, &replacement, &reencrypted) == QUANTAPDF_OK);
    CHECK(inspect_output(reencrypted, "new-user", &inspection));
    CHECK(inspection.encrypted && inspection.revision == 6);

    CHECK(quantapdf_encrypt_pdf(authenticated, &options, &sentinel) ==
          QUANTAPDF_ERROR_STATE);
    CHECK(sentinel == NULL);
    sentinel = (quantapdf_output *)(uintptr_t)1u;
    CHECK(quantapdf_decrypt_pdf(plain, &sentinel) == QUANTAPDF_ERROR_STATE);
    CHECK(sentinel == NULL);
    sentinel = (quantapdf_output *)(uintptr_t)1u;
    CHECK(quantapdf_reencrypt_pdf(plain, &options, &sentinel) ==
          QUANTAPDF_ERROR_STATE);
    CHECK(sentinel == NULL);

    quantapdf_drop_output(reencrypted);
    quantapdf_drop_output(decrypted);
    quantapdf_drop_output(encrypted);
    quantapdf_close(authenticated);
    quantapdf_close(plain);
    return 0;
}

static int expect_encrypt_status(
    quantapdf_document *document,
    const quantapdf_encryption_options *options,
    quantapdf_status expected)
{
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1u;
    quantapdf_status const status =
        quantapdf_encrypt_pdf(document, options, &output);
    if (status != expected)
        return 0;
    if (status == QUANTAPDF_OK) {
        if (output == NULL)
            return 0;
        quantapdf_drop_output(output);
        return 1;
    }
    return output == NULL;
}

static int inspect_permissions(
    quantapdf_document *plain,
    uint32_t permissions,
    int encrypt_metadata,
    quantapdf_security_inspection *inspection)
{
    quantapdf_encryption_options options = {

        QUANTAPDF_ENCRYPTION_AES_256,
        "user",
        "owner",
        permissions,
        encrypt_metadata};
    quantapdf_output *output = NULL;
    int result;

    if (quantapdf_encrypt_pdf(plain, &options, &output) != QUANTAPDF_OK)
        return 0;
    result = inspect_output(output, "user", inspection);
    quantapdf_drop_output(output);
    return result;
}

static int test_validation_and_permissions(void)
{
    quantapdf_document *plain = NULL;
    quantapdf_encryption_options options = {

        QUANTAPDF_ENCRYPTION_AES_256,
        "user",
        "owner",
        0u,
        1};
    quantapdf_security_inspection inspection = {0};
    char password_127[128];
    char password_128[129];
    static const char control_password[] = {'a', '\n', 'b', 0};
    static const char del_password[] = {'a', 0x7f, 0};
    static const char non_ascii_password[] =
        {'m', (char)0xc3, (char)0xb6, 't', 0};
    struct extended_options {
        quantapdf_encryption_options options;
        uint64_t suffix;
    } extended = {0};

    memset(password_127, 'a', sizeof(password_127) - 1u);
    password_127[sizeof(password_127) - 1u] = '\0';
    memset(password_128, 'a', sizeof(password_128) - 1u);
    password_128[sizeof(password_128) - 1u] = '\0';
    CHECK(quantapdf_open(test_context(), SECURITY_PLAIN_PDF, NULL, &plain) == QUANTAPDF_OK);

    CHECK(expect_encrypt_status(
        plain, NULL, QUANTAPDF_ERROR_ARGUMENT));

    options.method = (quantapdf_encryption_method)99;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.method = QUANTAPDF_ENCRYPTION_AES_256;
    options.permissions = (1u << 31);
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.permissions = QUANTAPDF_PERMISSION_PRINT_HIGH_QUALITY;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.permissions = 0u;
    options.encrypt_metadata = -1;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.encrypt_metadata = 2;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.encrypt_metadata = 1;
    options.owner_password_utf8 = "";
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.owner_password_utf8 = "user";
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.owner_password_utf8 = "owner";
    options.user_password_utf8 = control_password;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.user_password_utf8 = del_password;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.user_password_utf8 = non_ascii_password;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.user_password_utf8 = password_128;
    CHECK(expect_encrypt_status(
        plain, &options, QUANTAPDF_ERROR_ARGUMENT));
    options.user_password_utf8 = password_127;
    CHECK(expect_encrypt_status(plain, &options, QUANTAPDF_OK));
    options.user_password_utf8 = "";
    CHECK(expect_encrypt_status(plain, &options, QUANTAPDF_OK));

    extended.options = options;

    extended.suffix = UINT64_C(0x8877665544332211);
    CHECK(expect_encrypt_status(
        plain, &extended.options, QUANTAPDF_OK));
    CHECK(extended.suffix == UINT64_C(0x8877665544332211));

    CHECK(inspect_permissions(plain, 0u, 1, &inspection));
    CHECK(inspection.allow_accessibility && !inspection.allow_copy &&
          !inspection.allow_assemble && !inspection.allow_fill_forms &&
          !inspection.allow_annotate_and_fill_forms &&
          !inspection.allow_modify_other &&
          !inspection.allow_print_low_resolution &&
          !inspection.allow_print_high_quality &&
          inspection.encrypt_metadata);
    CHECK(inspect_permissions(
        plain, QUANTAPDF_PERMISSION_COPY, 0, &inspection));
    CHECK(inspection.allow_copy && !inspection.encrypt_metadata);
    CHECK(inspect_permissions(
        plain, QUANTAPDF_PERMISSION_ASSEMBLE, 1, &inspection));
    CHECK(inspection.allow_assemble);
    CHECK(inspect_permissions(
        plain, QUANTAPDF_PERMISSION_FILL_FORMS, 1, &inspection));
    CHECK(inspection.allow_fill_forms &&
          !inspection.allow_annotate_and_fill_forms);
    CHECK(inspect_permissions(
        plain, QUANTAPDF_PERMISSION_ANNOTATE_AND_FILL_FORMS,
        1, &inspection));
    CHECK(!inspection.allow_fill_forms &&
          inspection.allow_annotate_and_fill_forms);
    CHECK(inspect_permissions(
        plain, QUANTAPDF_PERMISSION_MODIFY_OTHER, 1, &inspection));
    CHECK(inspection.allow_modify_other);
    CHECK(inspect_permissions(
        plain, QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION, 1, &inspection));
    CHECK(inspection.allow_print_low_resolution &&
          !inspection.allow_print_high_quality);
    CHECK(inspect_permissions(
        plain,
        QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION |
            QUANTAPDF_PERMISSION_PRINT_HIGH_QUALITY,
        1,
        &inspection));
    CHECK(inspection.allow_print_low_resolution &&
          inspection.allow_print_high_quality);

    quantapdf_close(plain);
    return 0;
}

static int test_authentication_randomness_and_lifetime(void)
{
    quantapdf_document *plain = NULL;
    quantapdf_document *encrypted_document = NULL;
    quantapdf_document *probe = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_output *decrypted_first = NULL;
    quantapdf_output *decrypted_second = NULL;
    quantapdf_output *reencrypted = NULL;
    quantapdf_encryption_options options = {

        QUANTAPDF_ENCRYPTION_AES_256,
        "user",
        "owner",
        QUANTAPDF_PERMISSION_COPY |
            QUANTAPDF_PERMISSION_PRINT_LOW_RESOLUTION,
        1};
    quantapdf_encryption_options replacement = options;
    quantapdf_security_inspection inspection = {0};
    quantapdf_security_inspection first_inspection = {0};
    quantapdf_security_inspection second_inspection = {0};

    replacement.user_password_utf8 = "new-user";
    replacement.owner_password_utf8 = "new-owner";
    replacement.permissions = QUANTAPDF_PERMISSION_FILL_FORMS;
    replacement.encrypt_metadata = 0;

    CHECK(quantapdf_open(test_context(), SECURITY_PLAIN_PDF, NULL, &plain) == QUANTAPDF_OK);
    CHECK(quantapdf_encrypt_pdf(plain, &options, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_encrypt_pdf(plain, &options, &second) == QUANTAPDF_OK);
    CHECK(!outputs_equal(first, second));
    CHECK(inspect_output(first, "user", &first_inspection));
    CHECK(inspect_output(second, "user", &second_inspection));
    CHECK(first_inspection.encryption_key_size == 32u &&
          second_inspection.encryption_key_size == 32u);
    CHECK(memcmp(
              first_inspection.encryption_key,
              second_inspection.encryption_key,
              sizeof(first_inspection.encryption_key)) != 0);
    CHECK(memcmp(
              first_inspection.id1,
              second_inspection.id1,
              sizeof(first_inspection.id1)) != 0);
    CHECK(memcmp(
              first_inspection.id2,
              second_inspection.id2,
              sizeof(first_inspection.id2)) != 0);
    CHECK(quantapdf_output_save_file(first, SECURITY_ENCRYPTED_OUTPUT) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(test_context(),
              SECURITY_ENCRYPTED_OUTPUT, "wrong", &probe) ==
          QUANTAPDF_ERROR_PASSWORD);
    CHECK(probe == NULL);
    CHECK(quantapdf_open(test_context(),
              SECURITY_ENCRYPTED_OUTPUT, "user", &probe) == QUANTAPDF_OK);
    quantapdf_close(probe);
    probe = NULL;
    CHECK(quantapdf_open(test_context(),
              SECURITY_ENCRYPTED_OUTPUT, "owner", &probe) == QUANTAPDF_OK);
    quantapdf_close(probe);
    probe = NULL;

    CHECK(quantapdf_open(test_context(),
              SECURITY_ENCRYPTED_OUTPUT, "user", &encrypted_document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_decrypt_pdf(
              encrypted_document, &decrypted_first) == QUANTAPDF_OK);
    CHECK(quantapdf_decrypt_pdf(
              encrypted_document, &decrypted_second) == QUANTAPDF_OK);
    CHECK(outputs_equal(decrypted_first, decrypted_second));
    CHECK(quantapdf_reencrypt_pdf(
              encrypted_document, &replacement, &reencrypted) ==
          QUANTAPDF_OK);
    quantapdf_close(encrypted_document);
    encrypted_document = NULL;

    CHECK(inspect_output(decrypted_first, NULL, &inspection));
    CHECK(!inspection.encrypted);
    CHECK(inspect_output(reencrypted, "new-owner", &inspection));
    CHECK(inspection.encrypted && !inspection.encrypt_metadata &&
          inspection.allow_fill_forms && !inspection.allow_copy);
    CHECK(quantapdf_output_save_file(
              decrypted_first, SECURITY_DECRYPTED_OUTPUT) == QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(
              reencrypted, SECURITY_REENCRYPTED_OUTPUT) == QUANTAPDF_OK);
    CHECK(quantapdf_open(test_context(),
              SECURITY_REENCRYPTED_OUTPUT, "user", &probe) ==
          QUANTAPDF_ERROR_PASSWORD);
    CHECK(probe == NULL);
    CHECK(quantapdf_open(test_context(),
              SECURITY_REENCRYPTED_OUTPUT, "new-user", &probe) ==
          QUANTAPDF_OK);
    quantapdf_close(probe);

    quantapdf_drop_output(reencrypted);
    quantapdf_drop_output(decrypted_second);
    quantapdf_drop_output(decrypted_first);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_close(plain);
    return 0;
}

static int test_signed_and_legacy_policy(void)
{
    quantapdf_document *plain = NULL;
    quantapdf_document *signed_document = NULL;
    quantapdf_document *legacy_document = NULL;
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1u;
    quantapdf_encryption_options options = {

        QUANTAPDF_ENCRYPTION_AES_256,
        "user",
        "owner",
        0u,
        1};
    int kind;

    CHECK(quantapdf_open(test_context(),
              SECURITY_SIGNED_PDF, NULL, &signed_document) == QUANTAPDF_OK);
    CHECK(quantapdf_encrypt_pdf(
              signed_document, &options, &output) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(output == NULL);
    quantapdf_close(signed_document);

    for (kind = 1; kind <= 12; ++kind) {
        quantapdf_status const expected =
            (kind <= 3 || kind == 5 || kind == 9 || kind == 10 || kind == 12)
            ? QUANTAPDF_ERROR_UNSUPPORTED
            : QUANTAPDF_ERROR_FORMAT;
        CHECK(quantapdf_security_create_signature_fixture(
            SECURITY_PLAIN_PDF, SECURITY_SIGNATURE_FIXTURE, kind, 0));
        signed_document = NULL;
        CHECK(quantapdf_open(test_context(),
                  SECURITY_SIGNATURE_FIXTURE,
                  NULL,
                  &signed_document) == QUANTAPDF_OK);
        output = (quantapdf_output *)(uintptr_t)1u;
        CHECK(quantapdf_encrypt_pdf(
                  signed_document, &options, &output) == expected);
        CHECK(output == NULL);
        quantapdf_close(signed_document);

        CHECK(quantapdf_security_create_signature_fixture(
            SECURITY_PLAIN_PDF, SECURITY_SIGNATURE_FIXTURE, kind, 1));
        signed_document = NULL;
        CHECK(quantapdf_open(test_context(),
                  SECURITY_SIGNATURE_FIXTURE,
                  "sig-user",
                  &signed_document) == QUANTAPDF_OK);
        output = (quantapdf_output *)(uintptr_t)1u;
        CHECK(quantapdf_decrypt_pdf(signed_document, &output) == expected);
        CHECK(output == NULL);
        output = (quantapdf_output *)(uintptr_t)1u;
        CHECK(quantapdf_reencrypt_pdf(
                  signed_document, &options, &output) == expected);
        CHECK(output == NULL);
        quantapdf_close(signed_document);
    }

    CHECK(quantapdf_security_create_incremental_signature_fixture(
        SECURITY_PLAIN_PDF, SECURITY_SIGNATURE_FIXTURE, NULL));
    CHECK(quantapdf_open(test_context(),
              SECURITY_SIGNATURE_FIXTURE,
              NULL,
              &signed_document) == QUANTAPDF_OK);
    output = (quantapdf_output *)(uintptr_t)1u;
    CHECK(quantapdf_encrypt_pdf(
              signed_document, &options, &output) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(output == NULL);
    quantapdf_close(signed_document);

    CHECK(quantapdf_open(test_context(), SECURITY_PLAIN_PDF, NULL, &plain) == QUANTAPDF_OK);
    CHECK(quantapdf_encrypt_pdf(plain, &options, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(
              output, SECURITY_ENCRYPTED_OUTPUT) == QUANTAPDF_OK);
    quantapdf_drop_output(output);
    output = NULL;
    quantapdf_close(plain);
    CHECK(quantapdf_security_create_incremental_signature_fixture(
        SECURITY_ENCRYPTED_OUTPUT,
        SECURITY_SIGNATURE_FIXTURE,
        "user"));
    CHECK(quantapdf_open(test_context(),
              SECURITY_SIGNATURE_FIXTURE,
              "user",
              &signed_document) == QUANTAPDF_OK);
    output = (quantapdf_output *)(uintptr_t)1u;
    CHECK(quantapdf_decrypt_pdf(signed_document, &output) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(output == NULL);
    output = (quantapdf_output *)(uintptr_t)1u;
    CHECK(quantapdf_reencrypt_pdf(
              signed_document, &options, &output) ==
          QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(output == NULL);
    quantapdf_close(signed_document);

    CHECK(quantapdf_open(test_context(),
              SECURITY_LEGACY_ENCRYPTED_PDF,
              "user-pass",
              &legacy_document) == QUANTAPDF_OK);
    CHECK(quantapdf_decrypt_pdf(legacy_document, &output) == QUANTAPDF_OK);
    quantapdf_drop_output(output);
    output = NULL;
    CHECK(quantapdf_reencrypt_pdf(
              legacy_document, &options, &output) == QUANTAPDF_OK);
    quantapdf_drop_output(output);
    quantapdf_close(legacy_document);
    return 0;
}

static int test_entropy_and_publication_faults(void)
{
    quantapdf_document *plain = NULL;
    quantapdf_output *output = (quantapdf_output *)(uintptr_t)1u;
    quantapdf_encryption_options options = {

        QUANTAPDF_ENCRYPTION_AES_256,
        "user",
        "owner",
        0u,
        1};
    size_t context_entries = 0;
    size_t configure_requests = 0;
    size_t write_requests = 0;
    size_t context_exits = 0;

    CHECK(quantapdf_open(test_context(), SECURITY_PLAIN_PDF, NULL, &plain) == QUANTAPDF_OK);

    quantapdf_security_test_set_fault(
        plain, QUANTAPDF_SECURITY_TEST_FAULT_ENTROPY_CONFIGURE);
    CHECK(quantapdf_encrypt_pdf(plain, &options, &output) ==
          QUANTAPDF_ERROR_BACKEND);
    CHECK(output == NULL);
    quantapdf_security_test_get_random_context_stats(
        plain, &context_entries, &configure_requests, &write_requests,
        &context_exits);
    CHECK(context_entries == 1u && configure_requests >= 1u &&
          write_requests == 0u && context_exits == 1u);

    output = (quantapdf_output *)(uintptr_t)1u;
    quantapdf_security_test_set_fault(
        plain, QUANTAPDF_SECURITY_TEST_FAULT_ENTROPY_WRITE);
    CHECK(quantapdf_encrypt_pdf(plain, &options, &output) ==
          QUANTAPDF_ERROR_BACKEND);
    CHECK(output == NULL);
    quantapdf_security_test_get_random_context_stats(
        plain, &context_entries, &configure_requests, &write_requests,
        &context_exits);
    CHECK(context_entries == 1u && configure_requests >= 1u &&
          write_requests >= 1u && context_exits == 1u);

    output = (quantapdf_output *)(uintptr_t)1u;
    quantapdf_security_test_set_fault(
        plain, QUANTAPDF_SECURITY_TEST_FAULT_OUTPUT_NOMEM);
    CHECK(quantapdf_encrypt_pdf(plain, &options, &output) ==
          QUANTAPDF_ERROR_NOMEM);
    CHECK(output == NULL);

    output = (quantapdf_output *)(uintptr_t)1u;
    quantapdf_security_test_set_fault(
        plain, QUANTAPDF_SECURITY_TEST_FAULT_BEFORE_PUBLICATION);
    CHECK(quantapdf_encrypt_pdf(plain, &options, &output) ==
          QUANTAPDF_ERROR_BACKEND);
    CHECK(output == NULL);

    CHECK(quantapdf_encrypt_pdf(plain, &options, &output) == QUANTAPDF_OK);
    quantapdf_drop_output(output);
    quantapdf_close(plain);
    return 0;
}

static int test_metadata_encryption_observability(void)
{
    static const char marker[] = "QUANTAPDF_METADATA_CLEAR_MARKER";
    static const char expected_title[] = "QuantaPDF Security Metadata";
    quantapdf_document *source = NULL;
    quantapdf_document *authenticated = NULL;
    quantapdf_output *encrypted_true = NULL;
    quantapdf_output *encrypted_false = NULL;
    quantapdf_encryption_options options = {

        QUANTAPDF_ENCRYPTION_AES_256,
        "user",
        "owner",
        0u,
        1};
    char *title = NULL;
    size_t title_size = 0;

    CHECK(quantapdf_security_create_metadata_fixture(
        SECURITY_PLAIN_PDF, SECURITY_METADATA_FIXTURE));
    CHECK(quantapdf_open(test_context(),
              SECURITY_METADATA_FIXTURE, NULL, &source) == QUANTAPDF_OK);
    CHECK(quantapdf_encrypt_pdf(source, &options, &encrypted_true) ==
          QUANTAPDF_OK);
    options.encrypt_metadata = 0;
    CHECK(quantapdf_encrypt_pdf(source, &options, &encrypted_false) ==
          QUANTAPDF_OK);
    CHECK(!output_contains(encrypted_true, marker));
    CHECK(output_contains(encrypted_false, marker));

    CHECK(quantapdf_output_save_file(
              encrypted_true, SECURITY_METADATA_ENCRYPTED_TRUE) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(
              encrypted_false, SECURITY_METADATA_ENCRYPTED_FALSE) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_open(test_context(),
              SECURITY_METADATA_ENCRYPTED_TRUE,
              "user",
              &authenticated) == QUANTAPDF_OK);
    CHECK(quantapdf_document_metadata(
              authenticated,
              QUANTAPDF_METADATA_TITLE,
              &title,
              &title_size) == QUANTAPDF_OK);
    CHECK(title_size == strlen(expected_title) &&
          memcmp(title, expected_title, title_size) == 0);
    quantapdf_free(title);
    quantapdf_close(authenticated);
    authenticated = NULL;
    title = NULL;
    title_size = 0;
    CHECK(quantapdf_open(test_context(),
              SECURITY_METADATA_ENCRYPTED_FALSE,
              "owner",
              &authenticated) == QUANTAPDF_OK);
    CHECK(quantapdf_document_metadata(
              authenticated,
              QUANTAPDF_METADATA_TITLE,
              &title,
              &title_size) == QUANTAPDF_OK);
    CHECK(title_size == strlen(expected_title) &&
          memcmp(title, expected_title, title_size) == 0);

    quantapdf_free(title);
    quantapdf_close(authenticated);
    quantapdf_drop_output(encrypted_false);
    quantapdf_drop_output(encrypted_true);
    quantapdf_close(source);
    return 0;
}

static int compare_first_page_semantics(
    quantapdf_document *left,
    quantapdf_document *right)
{
    quantapdf_page *left_page = NULL;
    quantapdf_page *right_page = NULL;
    quantapdf_bitmap *left_bitmap = NULL;
    quantapdf_bitmap *right_bitmap = NULL;
    char *left_text = NULL;
    char *right_text = NULL;
    const unsigned char *left_pixels = NULL;
    const unsigned char *right_pixels = NULL;
    size_t left_text_size = 0;
    size_t right_text_size = 0;
    size_t left_pixel_size = 0;
    size_t right_pixel_size = 0;
    int left_count = 0;
    int right_count = 0;
    int result = 0;

    if (quantapdf_page_count(left, &left_count) != QUANTAPDF_OK ||
        quantapdf_page_count(right, &right_count) != QUANTAPDF_OK ||
        left_count != right_count || left_count < 1 ||
        quantapdf_load_page(left, 0, &left_page) != QUANTAPDF_OK ||
        quantapdf_load_page(right, 0, &right_page) != QUANTAPDF_OK ||
        quantapdf_extract_text(
            left_page, &left_text, &left_text_size) != QUANTAPDF_OK ||
        quantapdf_extract_text(
            right_page, &right_text, &right_text_size) != QUANTAPDF_OK ||
        left_text_size == 0u || left_text_size != right_text_size ||
        memcmp(left_text, right_text, left_text_size) != 0 ||
        quantapdf_render_page(left_page, &left_bitmap) != QUANTAPDF_OK ||
        quantapdf_render_page(right_page, &right_bitmap) !=
            QUANTAPDF_OK ||
        quantapdf_bitmap_data(
            left_bitmap, &left_pixels, &left_pixel_size) != QUANTAPDF_OK ||
        quantapdf_bitmap_data(
            right_bitmap, &right_pixels, &right_pixel_size) != QUANTAPDF_OK ||
        left_pixel_size == 0u || left_pixel_size != right_pixel_size ||
        memcmp(left_pixels, right_pixels, left_pixel_size) != 0)
        goto cleanup;
    result = 1;

cleanup:
    quantapdf_drop_bitmap(right_bitmap);
    quantapdf_drop_bitmap(left_bitmap);
    quantapdf_free(right_text);
    quantapdf_free(left_text);
    quantapdf_drop_page(right_page);
    quantapdf_drop_page(left_page);
    return result;
}

static int test_text_and_render_semantics(void)
{
    quantapdf_document *source = NULL;
    quantapdf_document *authenticated = NULL;
    quantapdf_document *roundtrip = NULL;
    quantapdf_output *encrypted = NULL;
    quantapdf_output *decrypted = NULL;
    quantapdf_encryption_options options = {

        QUANTAPDF_ENCRYPTION_AES_256,
        "user",
        "owner",
        QUANTAPDF_PERMISSION_ALL,
        1};

    CHECK(quantapdf_open(test_context(), SECURITY_TEXT_PDF, NULL, &source) == QUANTAPDF_OK);
    CHECK(quantapdf_encrypt_pdf(source, &options, &encrypted) == QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(
              encrypted, SECURITY_SEMANTIC_ENCRYPTED) == QUANTAPDF_OK);
    CHECK(quantapdf_open(test_context(),
              SECURITY_SEMANTIC_ENCRYPTED, "user", &authenticated) ==
          QUANTAPDF_OK);
    CHECK(compare_first_page_semantics(source, authenticated));
    CHECK(quantapdf_decrypt_pdf(authenticated, &decrypted) == QUANTAPDF_OK);
    CHECK(quantapdf_output_save_file(
              decrypted, SECURITY_SEMANTIC_DECRYPTED) == QUANTAPDF_OK);
    CHECK(quantapdf_open(test_context(),
              SECURITY_SEMANTIC_DECRYPTED, NULL, &roundtrip) == QUANTAPDF_OK);
    CHECK(compare_first_page_semantics(source, roundtrip));

    quantapdf_close(roundtrip);
    quantapdf_drop_output(decrypted);
    quantapdf_close(authenticated);
    quantapdf_drop_output(encrypted);
    quantapdf_close(source);
    return 0;
}

static int test_file_identifier_policy(void)
{
    quantapdf_document *document = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    quantapdf_encryption_options options = {

        QUANTAPDF_ENCRYPTION_AES_256,
        "user",
        "owner",
        0u,
        1};
    quantapdf_security_inspection first_inspection = {0};
    quantapdf_security_inspection second_inspection = {0};

    CHECK(quantapdf_security_create_id_fixture(
        SECURITY_PLAIN_PDF, SECURITY_ID_FIXTURE, 0));
    CHECK(quantapdf_open(test_context(), SECURITY_ID_FIXTURE, NULL, &document) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_encrypt_pdf(document, &options, &first) == QUANTAPDF_OK);
    CHECK(quantapdf_encrypt_pdf(document, &options, &second) == QUANTAPDF_OK);
    CHECK(inspect_output(first, "user", &first_inspection));
    CHECK(inspect_output(second, "user", &second_inspection));
    CHECK(memcmp(
              first_inspection.id1,
              second_inspection.id1,
              sizeof(first_inspection.id1)) == 0);
    CHECK(memcmp(
              first_inspection.id2,
              second_inspection.id2,
              sizeof(first_inspection.id2)) != 0);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_close(document);

    CHECK(quantapdf_security_create_id_fixture(
        SECURITY_PLAIN_PDF, SECURITY_ID_FIXTURE, 1));
    document = NULL;
    CHECK(quantapdf_open(test_context(), SECURITY_ID_FIXTURE, NULL, &document) ==
          QUANTAPDF_OK);
    first = (quantapdf_output *)(uintptr_t)1u;
    CHECK(quantapdf_encrypt_pdf(document, &options, &first) ==
          QUANTAPDF_ERROR_FORMAT);
    CHECK(first == NULL);
    quantapdf_close(document);
    return 0;
}

int main(void)
{
    quantapdf_output *sentinel = (quantapdf_output *)(uintptr_t)1u;

    compile_public_surface();
    if (quantapdf_decrypt_pdf(NULL, &sentinel) != QUANTAPDF_ERROR_ARGUMENT ||
        sentinel != NULL) {
        fprintf(stderr, "decrypt did not clear sentinel output\n");
        return 1;
    }
    if (quantapdf_encrypt_pdf(NULL, NULL, &sentinel) !=
            QUANTAPDF_ERROR_ARGUMENT ||
        sentinel != NULL) {
        fprintf(stderr, "encrypt did not clear sentinel output\n");
        return 1;
    }
    if (quantapdf_reencrypt_pdf(NULL, NULL, &sentinel) !=
            QUANTAPDF_ERROR_ARGUMENT ||
        sentinel != NULL) {
        fprintf(stderr, "reencrypt did not clear sentinel output\n");
        return 1;
    }
    if (quantapdf_decrypt_pdf(NULL, NULL) != QUANTAPDF_ERROR_ARGUMENT) {
        fprintf(stderr, "decrypt accepted NULL output address\n");
        return 1;
    }
    CHECK(test_state_transitions() == 0);
    CHECK(test_validation_and_permissions() == 0);
    CHECK(test_authentication_randomness_and_lifetime() == 0);
    CHECK(test_signed_and_legacy_policy() == 0);
    CHECK(test_entropy_and_publication_faults() == 0);
    CHECK(test_metadata_encryption_observability() == 0);
    CHECK(test_text_and_render_semantics() == 0);
    CHECK(test_file_identifier_policy() == 0);
    quantapdf_security_check_public_semantics();
#if defined(QUANTAPDF_SECURITY_STATIC_BUILD)
    CHECK(quantapdf_security_check_static_provider_boundary(
              SECURITY_PLAIN_PDF));
#endif
    return 0;
}
