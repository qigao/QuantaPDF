#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>

#include "image_recompression_test_helpers.h"
#include "pdf_image_recompression_test_api.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(
    QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES ==
        (size_t)64u * (size_t)1024u * (size_t)1024u,
    "unexpected image recompression default decoded-byte cap");

static void check_impl(int ok, const char *expression, int line)
{
    if (!ok) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(expression) check_impl((expression), #expression, __LINE__)

void image_recompression_check_public_semantics(void);

static quantapdf_output *output_sentinel(void)
{
    return (quantapdf_output *)(uintptr_t)1;
}

static void expect_failure(
    quantapdf_document *document,
    const quantapdf_image_recompression_options *options,
    quantapdf_status expected)
{
    quantapdf_output *output = output_sentinel();

    CHECK(quantapdf_recompress_images(document, options, &output) == expected);
    CHECK(output == NULL);
}

static void expect_success(
    quantapdf_document *document,
    const quantapdf_image_recompression_options *options)
{
    quantapdf_output *output = NULL;

    CHECK(quantapdf_recompress_images(document, options, &output) == QUANTAPDF_OK);
    CHECK(output != NULL);
    quantapdf_drop_output(output);
}

static void check_policy_cap(
    quantapdf_document *document,
    size_t cap,
    int expect_boundary_rewritten,
    size_t expected_rewritten)
{
    quantapdf_image_recompression_options options = { 90, cap};
    quantapdf_output *output = NULL;
    quantapdf_test_image_recompression_stats stats = {0};
    const unsigned char *data = NULL;
    size_t size = 0;

    CHECK(
        quantapdf_recompress_images(document, &options, &output) ==
        QUANTAPDF_OK);
    CHECK(output != NULL);
    CHECK(quantapdf_output_data(output, &data, &size) == QUANTAPDF_OK);
    CHECK(data != NULL && size != 0);
    CHECK(
        quantapdf_output_save_file(output, POLICY_OUTPUT_PDF) ==
        QUANTAPDF_OK);
    CHECK(image_recompression_check_policy_output(
        POLICY_FIXTURE_PDF, data, size, expect_boundary_rewritten));
    quantapdf_test_image_recompression_get_stats(document, &stats);
    CHECK(stats.unique_images == expected_rewritten);
    CHECK(stats.provider_registrations == expected_rewritten);
    CHECK(stats.provider_invocations == expected_rewritten);
    CHECK(stats.every_provider_once);
    quantapdf_drop_output(output);
}

static void check_malformed_inputs(void)
{
    quantapdf_image_recompression_options options = {
        90,
        QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES};
    int fixture;

    for (fixture = IMAGE_RECOMPRESSION_MALFORMED_RESOURCES;
         fixture <= IMAGE_RECOMPRESSION_MALFORMED_DECODED_OVERSIZE;
         ++fixture) {
        quantapdf_document *document = NULL;
        CHECK(image_recompression_create_malformed_fixture(
            POSITIVE_SOURCE_PDF,
            MALFORMED_FIXTURE_PDF,
            (image_recompression_malformed_fixture)fixture));
        CHECK(
            quantapdf_open(test_context(), MALFORMED_FIXTURE_PDF, NULL, &document) ==
            QUANTAPDF_OK);
        CHECK(document != NULL);
        expect_failure(document, &options, QUANTAPDF_ERROR_FORMAT);
        if (fixture == IMAGE_RECOMPRESSION_MALFORMED_DECODED_OVERSIZE) {
            quantapdf_test_image_recompression_stats stats = {0};
            quantapdf_test_image_recompression_get_stats(document, &stats);
            CHECK(stats.decoded_preflight_bytes == 2);
        }
        quantapdf_close(document);
    }
}

static void check_security_fail_closed(void)
{
    quantapdf_image_recompression_options options = {
        90,
        QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES};
    quantapdf_document *document = NULL;

    CHECK(quantapdf_open(test_context(), SIGNED_PDF, NULL, &document) == QUANTAPDF_OK);
    expect_failure(document, &options, QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_close(document);
    document = NULL;

    CHECK(
        quantapdf_open(test_context(), ENCRYPTED_PDF, "user-pass", &document) ==
        QUANTAPDF_OK);
    expect_failure(document, &options, QUANTAPDF_ERROR_UNSUPPORTED);
    quantapdf_close(document);
}

static uint64_t observation_hash_bytes(
    uint64_t hash,
    const void *data,
    size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    size_t index;
    for (index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t document_observation_hash(quantapdf_document *document)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    int page_count = 0;
    int page_index;
    CHECK(quantapdf_page_count(document, &page_count) == QUANTAPDF_OK);
    hash = observation_hash_bytes(hash, &page_count, sizeof(page_count));
    for (page_index = 0; page_index < page_count; ++page_index) {
        quantapdf_page *page = NULL;
        quantapdf_bitmap *bitmap = NULL;
        quantapdf_rect bounds = {0};
        const unsigned char *data = NULL;
        size_t size = 0;
        int dimensions[4] = {0};
        CHECK(quantapdf_load_page(document, page_index, &page) == QUANTAPDF_OK);
        CHECK(quantapdf_page_bounds(page, &bounds) == QUANTAPDF_OK);
        CHECK(quantapdf_render_page(page, &bitmap) == QUANTAPDF_OK);
        CHECK(quantapdf_bitmap_dimensions(
            bitmap,
            &dimensions[0],
            &dimensions[1],
            &dimensions[2],
            &dimensions[3]) == QUANTAPDF_OK);
        CHECK(quantapdf_bitmap_data(bitmap, &data, &size) == QUANTAPDF_OK);
        hash = observation_hash_bytes(hash, &bounds, sizeof(bounds));
        hash = observation_hash_bytes(hash, dimensions, sizeof(dimensions));
        hash = observation_hash_bytes(hash, data, size);
        quantapdf_drop_bitmap(bitmap);
        quantapdf_drop_page(page);
    }
    return hash;
}

static void check_fault_atomicity(void)
{
    static const struct {
        quantapdf_test_image_recompression_fault fault;
        quantapdf_status expected;
    } cases[] = {
        {QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_BEFORE_PROVIDER_NOMEM,
         QUANTAPDF_ERROR_NOMEM},
        {QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_PROVIDER_NOMEM,
         QUANTAPDF_ERROR_NOMEM},
        {QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_PROVIDER_BACKEND,
         QUANTAPDF_ERROR_BACKEND},
        {QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_BEFORE_PUBLICATION,
         QUANTAPDF_ERROR_BACKEND},
        {QUANTAPDF_TEST_IMAGE_RECOMPRESSION_FAULT_WORK_BUDGET,
         QUANTAPDF_ERROR_UNSUPPORTED}};
    quantapdf_image_recompression_options options = {
        90,
        QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES};
    quantapdf_document *document = NULL;
    uint64_t baseline;
    size_t index;

    CHECK(quantapdf_open(test_context(), POSITIVE_FIXTURE_PDF, NULL, &document) == QUANTAPDF_OK);
    baseline = document_observation_hash(document);
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        quantapdf_test_image_recompression_set_fault(
            document, cases[index].fault);
        expect_failure(document, &options, cases[index].expected);
        CHECK(document_observation_hash(document) == baseline);
        expect_success(document, &options);
        CHECK(document_observation_hash(document) == baseline);
    }
    quantapdf_close(document);
}

static unsigned long long render_difference(
    const char *source_path,
    const char *output_path,
    int enforce_tolerance)
{
    quantapdf_document *source = NULL;
    quantapdf_document *output = NULL;
    int source_pages = 0;
    int output_pages = 0;
    int page_index;
    unsigned int maximum_delta = 0;
    unsigned long long total_delta = 0;
    size_t total_samples = 0;

    CHECK(quantapdf_open(test_context(), source_path, NULL, &source) == QUANTAPDF_OK);
    CHECK(quantapdf_open(test_context(), output_path, NULL, &output) == QUANTAPDF_OK);
    CHECK(quantapdf_page_count(source, &source_pages) == QUANTAPDF_OK);
    CHECK(quantapdf_page_count(output, &output_pages) == QUANTAPDF_OK);
    CHECK(source_pages == output_pages);
    for (page_index = 0; page_index < source_pages; ++page_index) {
        quantapdf_page *source_page = NULL;
        quantapdf_page *output_page = NULL;
        quantapdf_bitmap *source_bitmap = NULL;
        quantapdf_bitmap *output_bitmap = NULL;
        quantapdf_rect source_bounds = {0, 0, 0, 0};
        quantapdf_rect output_bounds = {0, 0, 0, 0};
        const unsigned char *source_data = NULL;
        const unsigned char *output_data = NULL;
        size_t source_size = 0;
        size_t output_size = 0;
        int source_width = 0;
        int source_height = 0;
        int source_stride = 0;
        int source_components = 0;
        int output_width = 0;
        int output_height = 0;
        int output_stride = 0;
        int output_components = 0;
        size_t index;

        CHECK(
            quantapdf_load_page(source, page_index, &source_page) ==
            QUANTAPDF_OK);
        CHECK(
            quantapdf_load_page(output, page_index, &output_page) ==
            QUANTAPDF_OK);
        CHECK(quantapdf_page_bounds(source_page, &source_bounds) == QUANTAPDF_OK);
        CHECK(quantapdf_page_bounds(output_page, &output_bounds) == QUANTAPDF_OK);
        CHECK(memcmp(&source_bounds, &output_bounds, sizeof(source_bounds)) == 0);
        CHECK(quantapdf_render_page(source_page, &source_bitmap) == QUANTAPDF_OK);
        CHECK(quantapdf_render_page(output_page, &output_bitmap) == QUANTAPDF_OK);
        CHECK(
            quantapdf_bitmap_dimensions(
                source_bitmap,
                &source_width,
                &source_height,
                &source_stride,
                &source_components) == QUANTAPDF_OK);
        CHECK(
            quantapdf_bitmap_dimensions(
                output_bitmap,
                &output_width,
                &output_height,
                &output_stride,
                &output_components) == QUANTAPDF_OK);
        CHECK(source_width == output_width);
        CHECK(source_height == output_height);
        CHECK(source_stride == output_stride);
        CHECK(source_components == output_components);
        CHECK(
            quantapdf_bitmap_data(
                source_bitmap, &source_data, &source_size) == QUANTAPDF_OK);
        CHECK(
            quantapdf_bitmap_data(
                output_bitmap, &output_data, &output_size) == QUANTAPDF_OK);
        CHECK(source_size == output_size);
        for (index = 0; index < source_size; ++index) {
            unsigned int const delta = source_data[index] > output_data[index]
                ? (unsigned int)(source_data[index] - output_data[index])
                : (unsigned int)(output_data[index] - source_data[index]);
            if (delta > maximum_delta)
                maximum_delta = delta;
            total_delta += delta;
        }
        total_samples += source_size;
        quantapdf_drop_bitmap(source_bitmap);
        quantapdf_drop_bitmap(output_bitmap);
        quantapdf_drop_page(source_page);
        quantapdf_drop_page(output_page);
    }
    CHECK(total_samples != 0);
    if (enforce_tolerance) {
        CHECK(maximum_delta <= 24);
        CHECK(total_delta <= (unsigned long long)total_samples * 2u);
    }
    quantapdf_close(source);
    quantapdf_close(output);
    return total_delta;
}

static void check_render_equivalence(
    const char *source_path,
    const char *output_path)
{
    (void)render_difference(source_path, output_path, 1);
}

static void check_quality_render_order(
    const char *source_path,
    const char *lower_quality_path,
    const char *higher_quality_path)
{
    unsigned long long const lower_error =
        render_difference(source_path, lower_quality_path, 0);
    unsigned long long const higher_error =
        render_difference(source_path, higher_quality_path, 0);
    CHECK(higher_error <= lower_error);
}

int main(void)
{
    struct extended_options {
        quantapdf_image_recompression_options v1;
        uint32_t ignored_tail;
    } extended = {{sizeof(extended), 90, 1024}, UINT32_C(0xa5a5a5a5)};
    quantapdf_document *document = NULL;
    quantapdf_output *positive_output = NULL;
    quantapdf_output *repeated_output = NULL;
    quantapdf_output *quality_output = NULL;
    quantapdf_image_recompression_options options = {
        90,
        QUANTAPDF_IMAGE_RECOMPRESSION_DEFAULT_MAX_DECODED_BYTES};
    quantapdf_image_recompression_options minimum_options = {

        90,
        1};
    quantapdf_test_image_recompression_stats stats = {0};
    int positive_page_count = 0;

    CHECK(image_recompression_check_work_budget_arithmetic());

    CHECK(
        quantapdf_recompress_images(NULL, &options, NULL) ==
        QUANTAPDF_ERROR_ARGUMENT);
    expect_failure(NULL, &options, QUANTAPDF_ERROR_ARGUMENT);
    CHECK(quantapdf_open(test_context(), ONE_PAGE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);

    expect_failure(document, NULL, QUANTAPDF_ERROR_ARGUMENT);

    options.jpeg_quality = 0;
    expect_failure(document, &options, QUANTAPDF_ERROR_ARGUMENT);
    options.jpeg_quality = 101;
    expect_failure(document, &options, QUANTAPDF_ERROR_ARGUMENT);

    expect_success(document, &minimum_options);
    options.jpeg_quality = 90;
    options.max_decoded_bytes_per_image = 0;
    expect_success(document, &options);
    expect_success(document, &extended.v1);

    quantapdf_close(document);

    CHECK(image_recompression_create_positive_fixture(
        POSITIVE_SOURCE_PDF, POSITIVE_FIXTURE_PDF));
    CHECK(quantapdf_open(test_context(), POSITIVE_FIXTURE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    CHECK(
        quantapdf_page_count(document, &positive_page_count) ==
        QUANTAPDF_OK);
    expect_success(document, &minimum_options);
    quantapdf_test_image_recompression_get_stats(document, &stats);
    CHECK(stats.unique_images == 8);

    options.jpeg_quality = 90;
    options.max_decoded_bytes_per_image = 0;
    CHECK(
        quantapdf_recompress_images(document, &options, &positive_output) ==
        QUANTAPDF_OK);
    CHECK(positive_output != NULL);
    quantapdf_test_image_recompression_get_stats(document, &stats);
    CHECK(stats.unique_images == 8);
    CHECK(stats.provider_registrations == 8);
    CHECK(stats.provider_invocations == 8);
    CHECK(stats.every_provider_once);
    {
        const unsigned char *data = NULL;
        const unsigned char *repeated_data = NULL;
        const unsigned char *quality_data = NULL;
        size_t size = 0;
        size_t repeated_size = 0;
        size_t quality_size = 0;
        int page_count = 0;
        CHECK(
            quantapdf_output_data(positive_output, &data, &size) ==
            QUANTAPDF_OK);
        CHECK(data != NULL && size != 0);
        CHECK(image_recompression_matches_expected_base64(
            data, size, EXPECTED_Q90_BASE64));
        CHECK(image_recompression_check_structural_preservation(
            POSITIVE_FIXTURE_PDF, data, size));

        CHECK(
            quantapdf_recompress_images(document, &options, &repeated_output) ==
            QUANTAPDF_OK);
        CHECK(repeated_output != NULL);
        CHECK(
            quantapdf_output_data(
                repeated_output, &repeated_data, &repeated_size) ==
            QUANTAPDF_OK);
        CHECK(repeated_size == size);
        CHECK(memcmp(data, repeated_data, size) == 0);

        options.jpeg_quality = 40;
        CHECK(
            quantapdf_recompress_images(document, &options, &quality_output) ==
            QUANTAPDF_OK);
        CHECK(quality_output != NULL);
        CHECK(
            quantapdf_output_data(
                quality_output, &quality_data, &quality_size) ==
            QUANTAPDF_OK);
        CHECK(
            quality_size != size ||
            memcmp(data, quality_data, size) != 0);
        CHECK(image_recompression_compare_quality_outputs(
            quality_data, quality_size, data, size, 8));
        CHECK(quantapdf_output_save_file(
            quality_output, QUALITY40_OUTPUT_PDF) == QUANTAPDF_OK);
        quantapdf_drop_output(quality_output);
        quality_output = NULL;
        quality_data = NULL;
        quality_size = 0;
        options.jpeg_quality = 100;
        CHECK(
            quantapdf_recompress_images(document, &options, &quality_output) ==
            QUANTAPDF_OK);
        CHECK(quality_output != NULL);
        CHECK(
            quantapdf_output_data(
                quality_output, &quality_data, &quality_size) ==
            QUANTAPDF_OK);
        CHECK(image_recompression_check_positive_output(
            quality_data, quality_size, 8));
        CHECK(
            quantapdf_output_save_file(
                positive_output, POSITIVE_OUTPUT_PDF) == QUANTAPDF_OK);

        quantapdf_close(document);
        document = NULL;
        CHECK(image_recompression_check_positive_output(data, size, 8));
        CHECK(
            quantapdf_open(test_context(), POSITIVE_OUTPUT_PDF, NULL, &document) ==
            QUANTAPDF_OK);
        CHECK(quantapdf_page_count(document, &page_count) == QUANTAPDF_OK);
        CHECK(page_count == positive_page_count);
        quantapdf_close(document);
        document = NULL;
        check_render_equivalence(
            POSITIVE_FIXTURE_PDF, POSITIVE_OUTPUT_PDF);
        check_quality_render_order(
            POSITIVE_FIXTURE_PDF,
            QUALITY40_OUTPUT_PDF,
            POSITIVE_OUTPUT_PDF);
    }
    quantapdf_drop_output(positive_output);
    quantapdf_drop_output(repeated_output);
    quantapdf_drop_output(quality_output);

    CHECK(image_recompression_create_policy_fixture(
        POSITIVE_SOURCE_PDF, POLICY_FIXTURE_PDF));
    CHECK(quantapdf_open(test_context(), POLICY_FIXTURE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    check_policy_cap(document, 5, 0, 1);
    check_policy_cap(document, 6, 1, 2);
    quantapdf_close(document);
    check_malformed_inputs();
    check_security_fail_closed();
    check_fault_atomicity();
    image_recompression_check_public_semantics();
    return 0;
}
