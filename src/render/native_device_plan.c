#include "render/native_device_plan.h"

#include <stdint.h>
#include <string.h>
#include <float.h>

_Static_assert(FLT_RADIX==2 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128 && FLT_MIN_EXP==-125,
    "Native DPI is a binary32 value");

/* A retained page fraction has 64-bit numerator/denominator. Binary32 DPI
 * contributes at most 24 significand bits and shifts -149..104. Even the
 * largest denominator times the last sample index (<2^23) uses <244 bits.
 * Eight 32-bit limbs cover this complete domain, without heap allocation,
 * compiler integer extensions, approximate quotients or overflow retries. */
typedef struct qp_wide { uint32_t limb[8]; } qp_wide;
typedef struct qp_dpi { uint32_t significand; int exponent; } qp_dpi;

static qp_wide qp_wide_from_u64(uint64_t value)
{
    qp_wide out={{0}};
    out.limb[0]=(uint32_t)value;out.limb[1]=(uint32_t)(value>>32u);
    return out;
}
static qp_wide qp_wide_mul(qp_wide value,uint32_t factor)
{
    qp_wide out={{0}};uint64_t carry=0u;
    for(size_t i=0u;i<8u;++i) {
        const uint64_t product=(uint64_t)value.limb[i]*factor+carry;
        out.limb[i]=(uint32_t)product;carry=product>>32u;
    }
    return out;
}
static qp_wide qp_wide_shift(qp_wide value,unsigned bits)
{
    qp_wide out={{0}};const unsigned words=bits/32u,shift=bits%32u;
    for(size_t i=0u;i<8u-words;++i) {
        const uint64_t part=(uint64_t)value.limb[i]<<shift;
        out.limb[i+words]|=(uint32_t)part;
        if(i+words+1u<8u)out.limb[i+words+1u]|=(uint32_t)(part>>32u);
    }
    return out;
}
static int qp_wide_compare(qp_wide a,qp_wide b)
{
    for(size_t i=8u;i>0u;--i)
        if(a.limb[i-1u]!=b.limb[i-1u])return a.limb[i-1u]<b.limb[i-1u]?-1:1;
    return 0;
}
static qp_dpi qp_dpi_exact(float dpi)
{
    int exponent=0;const float fraction=frexpf(dpi,&exponent);
    qp_dpi out={(uint32_t)ldexpf(fraction,24),exponent-24};
    while((out.significand&1u)==0u) {out.significand>>=1u;++out.exponent;}
    return out;
}

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
    uint64_t numerator, denominator, pixels, samples;
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
    qp_fixed_decimal unit, qp_dpi dpi,size_t max_pixels,qp_exact_span *out)
{
    const unsigned common = lower.places > upper.places ?
                            lower.places : upper.places;
    int64_t lower_value, upper_value;
    uint64_t numerator, denominator;
    if (!qp_signed_rescale(lower, common, &lower_value) ||
        !qp_signed_rescale(upper, common, &upper_value) || upper_value <= lower_value ||
        (lower_value < 0 && upper_value > INT64_MAX + lower_value))
        return 0;
    if (!qp_checked_mul((uint64_t)(upper_value - lower_value), unit.significand, &numerator) ||
        !qp_checked_mul(qp_powers_of_ten[common],
            qp_powers_of_ten[unit.places], &denominator))
        return 0;
    out->numerator = numerator;
    out->denominator = denominator;
    qp_wide a=qp_wide_mul(qp_wide_from_u64(numerator),dpi.significand);
    qp_wide b=qp_wide_mul(qp_wide_from_u64(denominator),72u);
    if(dpi.exponent>=0)a=qp_wide_shift(a,(unsigned)dpi.exponent);
    else b=qp_wide_shift(b,(unsigned)-dpi.exponent);
    if(qp_wide_compare(a,qp_wide_mul(b,(uint32_t)max_pixels))>0)return 0;
    uint32_t lo=0u,hi=(uint32_t)max_pixels;
    while(lo<hi) {
        const uint32_t mid=lo+(hi-lo)/2u;
        if(qp_wide_compare(a,qp_wide_mul(b,mid))>0)lo=mid+1u;else hi=mid;
    }
    out->pixels=lo;
    // Sample c is visible iff (2*c+1)*b < 8*a, including tiny subnormal DPI.
    a=qp_wide_shift(a,3u);lo=0u;hi=4u*(uint32_t)out->pixels;
    while(lo<hi) {
        const uint32_t mid=lo+(hi-lo)/2u;
        if(qp_wide_compare(qp_wide_mul(b,2u*mid+1u),a)<0)lo=mid+1u;else hi=mid;
    }
    out->samples=lo;
    return 1;
}

static double qp_decimal_to_double(qp_fixed_decimal value)
{
    const double number = (double)value.significand /
                          (double)qp_powers_of_ten[value.places];
    return value.negative ? -number : number;
}

static quantapdf_status qp_device_plan_create(
    const char *media_ascii, const char *crop_ascii, const char *unit_ascii,
    int rotation, float dpi, size_t max_pixels,
    unsigned mode, quantapdf_native_device_plan *out)
{
    qp_fixed_decimal media[4] = {{0}}, crop[4] = {{0}}, unit = {0};
    qp_exact_span x = {0}, y = {0}, temporary;
    quantapdf_native_device_plan result = {0};
    size_t unit_length;
    qp_dpi rate;
    double c[4], u, sx, sy;
    if (out == NULL) return QUANTAPDF_ERROR_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (media_ascii == NULL || crop_ascii == NULL || unit_ascii == NULL ||
        max_pixels == 0u)
        return QUANTAPDF_ERROR_ARGUMENT;
    if (max_pixels > QUANTAPDF_NATIVE_DEVICE_MAX_PIXELS ||
        !isfinite(dpi) || dpi <= 0.0f ||
        (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) ||
        !qp_parse_box(media_ascii, media) || !qp_parse_box(crop_ascii, crop) ||
        !qp_bounded_length(unit_ascii, QP_MAX_TOKEN_ASCII, &unit_length) ||
        !qp_parse_decimal(unit_ascii, unit_length, &unit) ||
        unit.negative || unit.significand == 0u ||
        unit.significand > UINT64_C(128) * qp_powers_of_ten[unit.places] ||
        !qp_contained_box(media, crop))
        return QUANTAPDF_ERROR_UNSUPPORTED;
    rate=qp_dpi_exact(dpi);
    if(!qp_span(crop[0],crop[2],unit,rate,max_pixels,&x) ||
       !qp_span(crop[1],crop[3],unit,rate,max_pixels,&y))
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
    result.sample_width = mode == QUANTAPDF_NATIVE_DEVICE_PHYSICAL_V1 ?
        (size_t)x.samples : result.width * 4u;
    result.sample_height = mode == QUANTAPDF_NATIVE_DEVICE_PHYSICAL_V1 ?
        (size_t)y.samples : result.height * 4u;
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
    int rotation, float dpi, size_t max_pixels,
    quantapdf_native_device_plan *out)
{
    return qp_device_plan_create(media_ascii, crop_ascii, unit_ascii,
        rotation, dpi, max_pixels, QUANTAPDF_NATIVE_DEVICE_FIT_V1, out);
}

quantapdf_status quantapdf_native_device_plan_create_physical(
    const char *media_ascii, const char *crop_ascii, const char *unit_ascii,
    int rotation, float dpi, size_t max_pixels,
    quantapdf_native_device_plan *out)
{
    return qp_device_plan_create(media_ascii, crop_ascii, unit_ascii,
        rotation, dpi, max_pixels, QUANTAPDF_NATIVE_DEVICE_PHYSICAL_V1, out);
}
