/* Test-only bounded exact-rational PDF page-grid arithmetic (#360).
 * C11, no runtime provider, allocation, PDFium, QPDF or Graph dependency. */
#include "decimal_rational_probe.h"
#include <stdint.h>
#include <string.h>

#define QP_DECIMAL_LIMIT UINT64_C(9007199254740991)
#define QP_MAX_CROP_ASCII 96u
#define QP_MAX_TOKEN_ASCII 20u
#define QP_MAX_DECIMAL_PLACES 12u
#define QP_MAX_PIXELS 1000000u

typedef struct qp_fixed_decimal {
    uint64_t significand;
    unsigned places;
    int negative;
} qp_fixed_decimal;

static const uint64_t qp_powers_of_ten[13] = {
    UINT64_C(1), UINT64_C(10), UINT64_C(100), UINT64_C(1000),
    UINT64_C(10000), UINT64_C(100000), UINT64_C(1000000),
    UINT64_C(10000000), UINT64_C(100000000), UINT64_C(1000000000),
    UINT64_C(10000000000), UINT64_C(100000000000),
    UINT64_C(1000000000000)
};

static int qp_parse_decimal(const char *begin, size_t length,
                            qp_fixed_decimal *out)
{
    size_t i = 0u;
    uint64_t n = 0u;
    unsigned places = 0u;
    int negative = 0, seen_dot = 0, seen_digit = 0;
    if (out == NULL) return 0;
    memset(out, 0, sizeof(*out));
    if (begin == NULL || length == 0u || length > QP_MAX_TOKEN_ASCII)
        return 0;
    if (begin[i] == '-' || begin[i] == '+') {
        negative = begin[i] == '-';
        ++i;
    }
    for (; i < length; ++i) {
        const unsigned char ch = (unsigned char)begin[i];
        if (ch == '.' && !seen_dot) {
            seen_dot = 1;
            continue;
        }
        if (ch < '0' || ch > '9') return 0;
        if (n > (QP_DECIMAL_LIMIT - (uint64_t)(ch - '0')) / UINT64_C(10))
            return 0;
        n = n * UINT64_C(10) + (uint64_t)(ch - '0');
        if (seen_dot && ++places > QP_MAX_DECIMAL_PLACES) return 0;
        seen_digit = 1;
    }
    if (!seen_digit) return 0;
    out->significand = n;
    out->places = places;
    out->negative = negative && n != 0u;
    return 1;
}

static int qp_parse_box(const char *ascii, qp_fixed_decimal out[4])
{
    size_t bytes = 0u, offset = 0u, i;
    qp_fixed_decimal temporary[4] = {{0}};
    if (ascii == NULL || out == NULL) return 0;
    memset(out, 0, 4u * sizeof(*out));
    /* Bounded read: the terminator must occur by byte 96. */
    while (bytes <= QP_MAX_CROP_ASCII && ascii[bytes] != '\0') ++bytes;
    if (bytes == 0u || bytes > QP_MAX_CROP_ASCII) return 0;
    for (i = 0u; i < 4u; ++i) {
        size_t begin;
        while (offset < bytes && ascii[offset] == ' ') ++offset;
        begin = offset;
        while (offset < bytes && ascii[offset] != ' ') ++offset;
        if (!qp_parse_decimal(ascii + begin, offset - begin, &temporary[i]))
            return 0;
    }
    while (offset < bytes && ascii[offset] == ' ') ++offset;
    if (offset != bytes) return 0; /* no fifth token/garbage */
    memcpy(out, temporary, sizeof(temporary));
    return 1;
}

static int qp_checked_mul(uint64_t x, uint64_t y, uint64_t *out)
{
    if (out == NULL) return 0;
    *out = 0u;
    if (y != 0u && x > UINT64_MAX / y) return 0;
    *out = x * y;
    return 1;
}

static int qp_signed_rescale(qp_fixed_decimal n, unsigned common,
                             int64_t *out)
{
    uint64_t magnitude, factor;
    if (out == NULL || common > QP_MAX_DECIMAL_PLACES ||
        common < n.places) return 0;
    factor = qp_powers_of_ten[common - n.places];
    if (!qp_checked_mul(n.significand, factor, &magnitude) ||
        magnitude > (uint64_t)INT64_MAX) return 0;
    *out = n.negative ? -(int64_t)magnitude : (int64_t)magnitude;
    return 1;
}

static int qp_span_pixels(qp_fixed_decimal lower, qp_fixed_decimal upper,
                          unsigned unit, unsigned dpi, uint64_t *pixels)
{
    const unsigned common = lower.places > upper.places ?
                            lower.places : upper.places;
    int64_t lo = 0, hi = 0, delta;
    uint64_t scaled, numerator, denominator, result;
    if (pixels == NULL) return 0;
    *pixels = 0u;
    if (!qp_signed_rescale(lower, common, &lo) ||
        !qp_signed_rescale(upper, common, &hi) ||
        hi <= lo) return 0;
    /* Check signed subtraction explicitly before computing hi - lo. */
    if (lo < 0 && hi > INT64_MAX + lo) return 0;
    delta = hi - lo;
    if (delta <= 0) return 0;
    if (!qp_checked_mul((uint64_t)delta, (uint64_t)unit, &scaled) ||
        !qp_checked_mul(scaled, (uint64_t)dpi, &numerator) ||
        !qp_checked_mul(qp_powers_of_ten[common], UINT64_C(72),
                        &denominator) ||
        denominator == 0u) return 0;
    /* Exact ceiling with NO +denominator-1 overflow risk. */
    result = numerator / denominator + (numerator % denominator != 0u);
    if (result == 0u) return 0;
    *pixels = result;
    return 1;
}

int quantapdf_test_rational_page_grid(
    const char *crop_ascii, int rotation, unsigned user_unit,
    unsigned dpi, size_t max_pixels, quantapdf_test_rational_grid *out)
{
    qp_fixed_decimal values[4] = {{0}};
    uint64_t w = 0u, h = 0u, tmp;
    if (out == NULL) return 0;
    out->width = out->height = 0u;
    if (crop_ascii == NULL ||
        (rotation != 0 && rotation != 90 &&
         rotation != 180 && rotation != 270) ||
        user_unit == 0u || user_unit > 128u ||
        (dpi != 72u && dpi != 96u && dpi != 144u) ||
        max_pixels == 0u || max_pixels > QP_MAX_PIXELS ||
        max_pixels > SIZE_MAX / 4u ||
        !qp_parse_box(crop_ascii, values) ||
        !qp_span_pixels(values[0], values[2], user_unit, dpi, &w) ||
        !qp_span_pixels(values[1], values[3], user_unit, dpi, &h))
        return 0;
    if (rotation == 90 || rotation == 270) {
        tmp = w; w = h; h = tmp;
    }
    if (w > (uint64_t)max_pixels || h > (uint64_t)max_pixels ||
        w > (uint64_t)max_pixels / h) return 0;
    out->width = (size_t)w;
    out->height = (size_t)h;
    return 1;
}
