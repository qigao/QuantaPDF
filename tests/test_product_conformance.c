/*
 * Product-level public C11 conformance: no private headers or backend types.
 * Keep the byte comparison within one build/SDK; cross-platform byte equality
 * requires an independently versioned golden corpus.
 */
#include <quantapdf/quantapdf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: conformance failure: %s\n", \
                __FILE__, __LINE__, #expr); \
        return EXIT_FAILURE; \
    } \
} while (0)

int main(int argc, char **argv)
{
    quantapdf_document *document = NULL;
    quantapdf_document *bad_document = NULL;
    quantapdf_output *first = NULL;
    quantapdf_output *second = NULL;
    const unsigned char *first_bytes = NULL;
    const unsigned char *second_bytes = NULL;
    size_t first_size = 0, second_size = 0;
    int page_count = -1;

    CHECK(argc == 3);
    CHECK(QUANTAPDF_ABI_VERSION == 2);
    CHECK(QUANTAPDF_VERSION_MAJOR == 2);

    fprintf(stderr, "conformance: reject invalid input\n");
    /* Error paths must not publish a handle. */
    bad_document = (quantapdf_document *) &page_count;
    CHECK(quantapdf_open(NULL, NULL, &bad_document) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(bad_document == NULL);

    bad_document = (quantapdf_document *) &page_count;
    CHECK(quantapdf_open(argv[2], NULL, &bad_document) != QUANTAPDF_OK);
    CHECK(bad_document == NULL);

    fprintf(stderr, "conformance: open fixture\n");
    CHECK(quantapdf_open(argv[1], NULL, &document) == QUANTAPDF_OK);
    CHECK(document != NULL);
    CHECK(quantapdf_page_count(document, &page_count) == QUANTAPDF_OK);
    CHECK(page_count == 1);

    fprintf(stderr, "conformance: deterministic rewrite\n");
    CHECK(quantapdf_rewrite_lossless(document, &first) == QUANTAPDF_OK);
    CHECK(first != NULL);
    CHECK(quantapdf_rewrite_lossless(document, &second) == QUANTAPDF_OK);
    CHECK(second != NULL);
    CHECK(quantapdf_output_data(first, &first_bytes, &first_size) ==
          QUANTAPDF_OK);
    CHECK(quantapdf_output_data(second, &second_bytes, &second_size) ==
          QUANTAPDF_OK);
    CHECK(first_bytes != NULL && second_bytes != NULL);
    CHECK(first_size != 0 && first_size == second_size);
    CHECK(memcmp(first_bytes, second_bytes, first_size) == 0);

    /* Outputs own bytes independently of their source document. */
    fprintf(stderr, "conformance: detached output lifetime\n");
    quantapdf_close(document);
    document = NULL;
    CHECK(quantapdf_output_data(first, &first_bytes, &first_size) ==
          QUANTAPDF_OK);
    CHECK(first_bytes != NULL && first_size != 0);
    quantapdf_drop_output(second);
    quantapdf_drop_output(first);
    quantapdf_drop_output(NULL);
    quantapdf_close(NULL);

    fprintf(stderr, "conformance: invalid rewrite guard\n");
    first = (quantapdf_output *) &page_count;
    CHECK(quantapdf_rewrite_lossless(NULL, &first) ==
          QUANTAPDF_ERROR_ARGUMENT);
    CHECK(first == NULL);
    return EXIT_SUCCESS;
}
