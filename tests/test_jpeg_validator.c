#include "backend/qpdf_composer.h"
#include "composer_test_helpers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #expr);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(void)
{
    unsigned char *jpeg = NULL;
    size_t jpeg_size = 0u;
    int stage;

    CHECK(quantapdf_test_make_jpeg(&jpeg, &jpeg_size));
    for (stage = 1; stage <= 2; ++stage) {
        uint32_t width = 1u;
        uint32_t height = 1u;
        int components = 1;
        quantapdf_status status;

        quantapdf_jpeg_force_oom_for_testing(stage);
        status = quantapdf_jpeg_validate(
            jpeg, jpeg_size, (quantapdf_jpeg_limits){SIZE_MAX, QUANTAPDF_JPEG_MAX_SCAN_BLOCKS},
            &width, &height, &components);
        quantapdf_jpeg_force_oom_for_testing(0);
        CHECK(status == QUANTAPDF_ERROR_NOMEM);
        CHECK(width == 0u && height == 0u && components == 0);
    }
    free(jpeg);
    return 0;
}
