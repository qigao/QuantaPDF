#include "test_context_fixture.h"
#include <quantapdf/quantapdf.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_impl(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check_impl((expression), #expression, __LINE__)

int main(void)
{
    static const unsigned char expected_prefix[] = {
        'H', 'e', 'l', 'l', 'o', ' ', 'C', 'a', 'f', 0xC3, 0xA9
    };
    int sentinel = 0;
    quantapdf_document *document = NULL;
    quantapdf_page *page = NULL;
    char *text = (char *)&sentinel;
    size_t size = 123;
    size_t i;

    CHECK(quantapdf_open(test_context(), TEXT_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);

    CHECK(quantapdf_extract_text(NULL, &text, &size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);
    CHECK(size == 0);

    size = 123;
    CHECK(quantapdf_extract_text(page, NULL, &size) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(size == 0);

    text = (char *)&sentinel;
    CHECK(quantapdf_extract_text(page, &text, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(text == NULL);

    CHECK(quantapdf_extract_text(page, &text, &size) == QUANTAPDF_OK);
    CHECK(text != NULL);
    CHECK(size >= sizeof(expected_prefix));

    /* The returned text owns its bytes independently of backend handles. */
    quantapdf_drop_page(page);
    quantapdf_close(document);
    page = NULL;
    document = NULL;

    CHECK(memcmp(text, expected_prefix, sizeof(expected_prefix)) == 0);
    for (i = 0; i < size; ++i)
        CHECK(text[i] != '\0');
    CHECK(text[size] == '\0');
    quantapdf_free(text);

    CHECK(quantapdf_open(test_context(), ONE_PAGE_PDF, NULL, &document) == QUANTAPDF_OK);
    CHECK(quantapdf_load_page(document, 0, &page) == QUANTAPDF_OK);
    text = (char *)&sentinel;
    size = 123;
    CHECK(quantapdf_extract_text(page, &text, &size) == QUANTAPDF_OK);
    CHECK(text != NULL);
    CHECK(size == 0);
    CHECK(text[0] == '\0');
    quantapdf_free(text);

    quantapdf_drop_page(page);
    quantapdf_close(document);
    quantapdf_free(NULL);
    return EXIT_SUCCESS;
}
