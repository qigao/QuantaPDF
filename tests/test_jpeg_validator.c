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
        quantapdf_jpeg_info image = {{1u, 1u, 1}, 1};
        quantapdf_status status;

        quantapdf_jpeg_force_oom_for_testing(stage);
        status = quantapdf_jpeg_validate(
            jpeg, jpeg_size, (quantapdf_jpeg_limits){SIZE_MAX, QUANTAPDF_JPEG_MAX_SCAN_BLOCKS},
            &image);
        quantapdf_jpeg_force_oom_for_testing(0);
        CHECK(status == QUANTAPDF_ERROR_NOMEM);
        CHECK(image.layout.width == 0u && image.layout.height == 0u &&
            image.layout.components == 0 && image.color_transform == 0);
    }
    free(jpeg);
    return 0;
}
