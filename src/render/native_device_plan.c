#include "render/native_device_plan.h"

#include <stdint.h>
#include <string.h>

#define QP_DECIMAL_LIMIT UINT64_C(9007199254740991)
#define QP_MAX_BOX_ASCII 95u
#define QP_MAX_TOKEN_ASCII 20u
#define QP_MAX_DECIMAL_PLACES 12u

typedef struct qp_fixed_decimal {
    uint64_t significand;
    unsigned places;
    int negative;
} qp_fixed_decimal;

typedef struct qp_exact_span {
    uint64_t numerator, denominator, pixels;
} qp_exact_span;

static const uint64_t qp_powers_of_ten[13] = {
    UINT64_C(1), UINT64_C(10), UINT64_C(100), UINT64_C(1000),
    UINT64_C(10000), UINT64_C(100000), UINT64_C(1000000),
    UINT64_C(10000000), UINT64_C(100000000), UINT64_C(1000000000),
    UINT64_C(10000000000), UINT64_C(100000000000),
    UINT64_C(1000000000000)
};

static int qp_checked_mul(uint64_t x, uint64_t y, uint64_t *out)
{
    if (y != 0u && x > UINT64_MAX / y) return 0;
    *out = x * y;
    return 1;
}

static int qp_parse_decimal(const char *begin, size_t length,
    qp_fixed_decimal *out)
{
    size_t i = 0u;
    uint64_t n = 0u;
    unsigned places = 0u;
    int negative = 0, seen_dot = 0, seen_digit = 0;
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
        if (ch < '0' || ch > '9' ||
            n > (QP_DECIMAL_LIMIT - (uint64_t)(ch - '0')) / UINT64_C(10))
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

static int qp_bounded_length(const char *ascii, size_t limit, size_t *out)
{
    size_t length = 0u;
    if (ascii == NULL) return 0;
    while (length <= limit && ascii[length] != '\0') ++length;
    if (length == 0u || length > limit) return 0;
    *out = length;
    return 1;
}

static int qp_parse_box(const char *ascii, qp_fixed_decimal out[4])
{
    size_t bytes, offset = 0u, i;
    if (!qp_bounded_length(ascii, QP_MAX_BOX_ASCII, &bytes)) return 0;
    for (i = 0u; i < 4u; ++i) {
        size_t begin;
        while (offset < bytes && ascii[offset] == ' ') ++offset;
        begin = offset;
        while (offset < bytes && ascii[offset] != ' ') ++offset;
        if (!qp_parse_decimal(ascii + begin, offset - begin, &out[i]))
            return 0;
    }
    while (offset < bytes && ascii[offset] == ' ') ++offset;
    return offset == bytes;
}

static int qp_signed_rescale(qp_fixed_decimal n, unsigned common,
    int64_t *out)
{
    uint64_t magnitude;
    if (common > QP_MAX_DECIMAL_PLACES || common < n.places ||
        !qp_checked_mul(n.significand,
            qp_powers_of_ten[common - n.places], &magnitude) ||
        magnitude > (uint64_t)INT64_MAX)
        return 0;
    *out = n.negative ? -(int64_t)magnitude : (int64_t)magnitude;
    return 1;
}

/* A signed comparison on a common exact decimal lattice. No binary64
 * endpoint conversion is used for box ordering or containment. */
static int qp_compare(qp_fixed_decimal a, qp_fixed_decimal b, int *out)
{
    const unsigned common = a.places > b.places ? a.places : b.places;
    int64_t av, bv;
    if (!qp_signed_rescale(a, common, &av) ||
        !qp_signed_rescale(b, common, &bv)) return 0;
    *out = av < bv ? -1 : av > bv ? 1 : 0;
    return 1;
}

static int qp_contained_box(const qp_fixed_decimal media[4],
    const qp_fixed_decimal crop[4])
{
    int comparison;
    for (size_t axis = 0u; axis < 2u; ++axis) {
        if (!qp_compare(media[axis], media[axis + 2u], &comparison) ||
            comparison >= 0 ||
            !qp_compare(crop[axis], crop[axis + 2u], &comparison) ||
            comparison >= 0 ||
            !qp_compare(media[axis], crop[axis], &comparison) ||
            comparison > 0 ||
            !qp_compare(crop[axis + 2u], media[axis + 2u], &comparison) ||
            comparison > 0)
            return 0;
    }
    return 1;
}

static int qp_span(qp_fixed_decimal lower, qp_fixed_decimal upper,
    qp_fixed_decimal unit, unsigned dpi, qp_exact_span *out)
{
    const unsigned common = lower.places > upper.places ?
                            lower.places : upper.places;
    int64_t lo, hi;
    uint64_t numerator, denominator, pixel_numerator, pixel_denominator;
    if (!qp_signed_rescale(lower, common, &lo) ||
        !qp_signed_rescale(upper, common, &hi) || hi <= lo ||
        (lo < 0 && hi > INT64_MAX + lo))
        return 0;
    if (!qp_checked_mul((uint64_t)(hi - lo), unit.significand, &numerator) ||
        !qp_checked_mul(qp_powers_of_ten[common],
            qp_powers_of_ten[unit.places], &denominator) ||
        !qp_checked_mul(numerator, (uint64_t)dpi, &pixel_numerator) ||
        !qp_checked_mul(denominator, UINT64_C(72), &pixel_denominator))
        return 0;
    out->numerator = numerator;
    out->denominator = denominator;
    out->pixels = pixel_numerator / pixel_denominator +
                  (pixel_numerator % pixel_denominator != 0u);
    return out->pixels != 0u;
}

static double qp_decimal_to_double(qp_fixed_decimal value)
{
    const double number = (double)value.significand /
                          (double)qp_powers_of_ten[value.places];
    return value.negative ? -number : number;
}

static quantapdf_status qp_device_plan_create(
    const char *media_ascii, const char *crop_ascii, const char *unit_ascii,
    int rotation, unsigned dpi, size_t max_pixels,
    unsigned mode, quantapdf_native_device_plan *out)
{
    qp_fixed_decimal media[4] = {{0}}, crop[4] = {{0}}, unit = {0};
    qp_exact_span x = {0}, y = {0}, temporary;
    quantapdf_native_device_plan result = {0};
    size_t unit_length;
    double c[4], u, sx, sy;
    if (out == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (media_ascii == NULL || crop_ascii == NULL || unit_ascii == NULL ||
        max_pixels == 0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (max_pixels > QUANTAPDF_NATIVE_DEVICE_MAX_PIXELS ||
        (dpi != 72u && dpi != 96u && dpi != 144u) ||
        (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) ||
        !qp_parse_box(media_ascii, media) || !qp_parse_box(crop_ascii, crop) ||
        !qp_bounded_length(unit_ascii, QP_MAX_TOKEN_ASCII, &unit_length) ||
        !qp_parse_decimal(unit_ascii, unit_length, &unit) ||
        unit.negative || unit.significand == 0u ||
        unit.significand > UINT64_C(128) * qp_powers_of_ten[unit.places] ||
        !qp_contained_box(media, crop) ||
        !qp_span(crop[0], crop[2], unit, dpi, &x) ||
        !qp_span(crop[1], crop[3], unit, dpi, &y))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    if (rotation == 90 || rotation == 270) {
        temporary = x; x = y; y = temporary;
    }
    if (x.pixels > (uint64_t)max_pixels || y.pixels > (uint64_t)max_pixels ||
        x.pixels > (uint64_t)max_pixels / y.pixels ||
        x.pixels > (uint64_t)SIZE_MAX / UINT64_C(4) ||
        y.pixels > (uint64_t)SIZE_MAX / (x.pixels * UINT64_C(4)))
        return QUANTAPDF_ERROR_UNSUPPORTED;

    result.width = (size_t)x.pixels;
    result.height = (size_t)y.pixels;
    result.stride = result.width * 4u;
    result.size = result.stride * result.height;
    result.dpi = dpi;
    result.mode = mode;
    /* The exact acceptance decision is complete. Floating conversion is
     * solely for the explicitly bounded geometric sample mapping. */
    result.width_points = (double)x.numerator / (double)x.denominator;
    result.height_points = (double)y.numerator / (double)y.denominator;
    u = qp_decimal_to_double(unit);
    for (size_t i = 0u; i < 4u; ++i) c[i] = qp_decimal_to_double(crop[i]);
    switch (rotation) {
    case 0:
        result.source_to_display[0] = result.source_to_display[3] = u;
        result.source_to_display[4] = -c[0] * u;
        result.source_to_display[5] = -c[1] * u;
        break;
    case 90:
        result.source_to_display[1] = -u;
        result.source_to_display[2] = u;
        result.source_to_display[4] = -c[1] * u;
        result.source_to_display[5] = c[2] * u;
        break;
    case 180:
        result.source_to_display[0] = result.source_to_display[3] = -u;
        result.source_to_display[4] = c[2] * u;
        result.source_to_display[5] = c[3] * u;
        break;
    case 270:
        result.source_to_display[1] = u;
        result.source_to_display[2] = -u;
        result.source_to_display[4] = c[3] * u;
        result.source_to_display[5] = -c[0] * u;
        break;
    default:
        return QUANTAPDF_ERROR_UNSUPPORTED;
    }
    sx = mode == QUANTAPDF_NATIVE_DEVICE_PHYSICAL_V1 ?
        (double)dpi / 72.0 : (double)result.width / result.width_points;
    sy = mode == QUANTAPDF_NATIVE_DEVICE_PHYSICAL_V1 ?
        (double)dpi / 72.0 : (double)result.height / result.height_points;
    result.source_to_device[0] = result.source_to_display[0] * sx;
    result.source_to_device[1] = -result.source_to_display[1] * sy;
    result.source_to_device[2] = result.source_to_display[2] * sx;
    result.source_to_device[3] = -result.source_to_display[3] * sy;
    result.source_to_device[4] = result.source_to_display[4] * sx;
    result.source_to_device[5] =
        (result.height_points - result.source_to_display[5]) * sy;
    result.from_composer[0] = sx;
    result.from_composer[3] = sy;
    /* Legacy Composer lowering uses its FLOAT canvas height. Physical
     * Source IR already flips Y around the EXACT decimal height; preserve
     * those coordinates instead of applying the float-height correction
     * twice. Physical consumers explicitly require this source convention. */
    result.from_composer[5] = mode == QUANTAPDF_NATIVE_DEVICE_PHYSICAL_V1 ?
        0.0 : (result.height_points - (double)(float)result.height_points) * sy;
    if (!quantapdf_native_device_plan_matches_page_mode(&result, mode,
            (float)result.width_points, (float)result.height_points, max_pixels))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    *out = result;
    return QUANTAPDF_OK;
}


/* Keep FIT callers on their qualified historical mapping. Physical DPI is
 * an explicit private entry: only buffer allocation uses decimal ceil. */
quantapdf_status quantapdf_native_device_plan_create(
    const char *media_ascii, const char *crop_ascii, const char *unit_ascii,
    int rotation, unsigned dpi, size_t max_pixels,
    quantapdf_native_device_plan *out)
{
    return qp_device_plan_create(media_ascii, crop_ascii, unit_ascii,
        rotation, dpi, max_pixels, QUANTAPDF_NATIVE_DEVICE_FIT_V1, out);
}

quantapdf_status quantapdf_native_device_plan_create_physical(
    const char *media_ascii, const char *crop_ascii, const char *unit_ascii,
    int rotation, unsigned dpi, size_t max_pixels,
    quantapdf_native_device_plan *out)
{
    return qp_device_plan_create(media_ascii, crop_ascii, unit_ascii,
        rotation, dpi, max_pixels, QUANTAPDF_NATIVE_DEVICE_PHYSICAL_V1, out);
}
