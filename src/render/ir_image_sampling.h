#ifndef QUANTAPDF_RENDER_IR_IMAGE_SAMPLING_H
#define QUANTAPDF_RENDER_IR_IMAGE_SAMPLING_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define QP_ALPHA_BILINEAR_ONE 16384u

typedef struct qp_bilinear_axis {
    size_t first, second;
    unsigned second_weight;
} qp_bilinear_axis;

static inline qp_bilinear_axis qp_bilinear_taps(double coordinate, size_t extent, unsigned one)
{
    /* Texel centers lie at (i + 0.5) / extent. Extend the outer texels
     * through the image fringe, including a covered pixel with its center
     * outside the quad. Coverage itself comes from the quad and Clip. */
    const double offset = fma(coordinate, (double)extent, -0.5);
    const double position = fmin(fmax(offset, 0.0), (double)(extent - 1u));
    /* Fixed-point weights per axis. Ties to even give complementary
     * weights when the texel axis is reflected. */
    const double fixed = position * one;
    const uint64_t base = (uint64_t)fixed;
    const double remainder = fixed - (double)base;
    const uint64_t texel = base +
        (remainder > 0.5 || (remainder == 0.5 && (base & 1u) != 0u));
    const size_t first = (size_t)(texel / one);
    const qp_bilinear_axis taps = {first, first + (first + 1u < extent),
        (unsigned)(texel % one)};
    return taps;
}

static inline size_t qp_nearest_texel(double coordinate, size_t extent)
{
    return coordinate <= 0.0 ? 0u : coordinate >= 1.0 ? extent - 1u : (size_t)(coordinate * extent);
}

typedef struct qp_alpha_sample {
    uint64_t sum, weight;
    unsigned maximum;
} qp_alpha_sample;

static inline unsigned qp_mask_texel(const unsigned char *data, size_t index, int wide)
{
    if (!wide) return data[index];
    return ((unsigned)data[2u * index] << 8u) | data[2u * index + 1u];
}

/* A separate mask grid covers the same unit square as its base image. */
static inline qp_alpha_sample qp_sample_image_mask(const unsigned char *data, size_t width,
    size_t height, int interpolate, unsigned bits, double u, double v)
{
    const int wide = bits == 16u;
    const unsigned maximum = wide ? 65535u : 255u;
    if (!interpolate) {
        const size_t x = qp_nearest_texel(u, width), y = qp_nearest_texel(v, height);
        const qp_alpha_sample result = {qp_mask_texel(data, y * width + x, wide), 1u, maximum};
        return result;
    }
    /* Ten weight bits per axis keep RAW16 color and alpha compositing
     * within uint64_t, including geometry coverage and destination alpha. */
    const unsigned one = wide ? 1024u : QP_ALPHA_BILINEAR_ONE;
    const qp_bilinear_axis x = qp_bilinear_taps(u, width, one), y = qp_bilinear_taps(v, height, one);
    const unsigned left = one - x.second_weight;
    const unsigned top = left * qp_mask_texel(data, y.first * width + x.first, wide) +
        x.second_weight * qp_mask_texel(data, y.first * width + x.second, wide);
    const unsigned bottom = left * qp_mask_texel(data, y.second * width + x.first, wide) +
        x.second_weight * qp_mask_texel(data, y.second * width + x.second, wide);
    const qp_alpha_sample result = {(uint64_t)top * (one - y.second_weight) +
        (uint64_t)bottom * y.second_weight, (uint64_t)one * one, maximum};
    return result;
}

#endif
