#ifndef QUANTAPDF_TEST_DECIMAL_RATIONAL_PROBE_H
#define QUANTAPDF_TEST_DECIMAL_RATIONAL_PROBE_H

/* TEST-ONLY bounded exact-rational page-grid candidate for Issue #360.
 * Never installed/exported as a QuantaPDF SDK ABI and not production code.
 * CropBox contains four ASCII decimal tokens separated by literal spaces.
 * Denominators are powers of 10; subtraction and DPI scaling are exact
 * unsigned/signed integer operations until the final ceiling division. */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct quantapdf_test_rational_grid {
    size_t width;
    size_t height;
} quantapdf_test_rational_grid;

/* Returns 1 for admitted grid, 0 otherwise. On all failures, *out is zeroed.
 * This narrow test-only admission supports:
 * - at most 96 bytes for all four tokens and 20 bytes/token;
 * - at most 12 decimal places and significands <= 2^53 - 1;
 * - rotation 0,90,180,270 and integer UserUnit 1..128;
 * - DPI 72,96,144 and max_pixels 1..1,000,000.
 * Refuses any arithmetic overflow, invalid extents or capacity exceedance.
 * No floating epsilon, locale parsing, allocation, PDFium or Graph calls. */
int quantapdf_test_rational_page_grid(
    const char *crop_ascii, int rotation, unsigned user_unit,
    unsigned dpi, size_t max_pixels, quantapdf_test_rational_grid *out);

/* Separate decimal UserUnit test-only path (positive rational <= 128).
 * Unit uses the same strict bounded decimal grammar as CropBox coordinates.
 * Endpoints and UserUnit retain integer significand/scale through the final
 * checked device-grid ceiling. Numeric products outside uint64 are rejected
 * transactionally instead of approximated or retried via binary64.
 * The integer-UserUnit entry above stays independently qualified. */
int quantapdf_test_rational_page_grid_decimal_unit(
    const char *crop_ascii, int rotation, const char *user_unit_ascii,
    unsigned dpi, size_t max_pixels, quantapdf_test_rational_grid *out);

#ifdef __cplusplus
}
#endif
#endif
