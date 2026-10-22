#include "render/native_device_plan.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #x); \
    return EXIT_FAILURE; \
} } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

static int zero_plan(const quantapdf_native_device_plan *plan)
{
    const unsigned char *bytes = (const unsigned char *)plan;
    for (size_t i = 0u; i < sizeof(*plan); ++i)
        if (bytes[i] != 0u) return 0;
    return 1;
}

static int near(double actual, double expected)
{
    return fabs(actual - expected) <= 1e-10 * (1.0 + fabs(expected));
}

static void map(const double matrix[6], double x, double y,
    double *out_x, double *out_y)
{
    *out_x = matrix[0] * x + matrix[2] * y + matrix[4];
    *out_y = matrix[1] * x + matrix[3] * y + matrix[5];
}

static int test_exact_grid_and_unit(void)
{
    const unsigned dpis[3] = {72u, 96u, 144u};
    const size_t expected_width[3] = {70u, 94u, 140u};
    const size_t expected_height[3] = {56u, 75u, 112u};
    quantapdf_native_device_plan p;
    /* Independent authored extents are exactly 10 x 8, even though endpoint
     * binary64 subtraction 16.1 - 6.1 may sit above the integer boundary. */
    for (size_t d = 0u; d < 3u; ++d) {
        for (int rotation = 0; rotation < 360; rotation += 90) {
            const int swap = rotation == 90 || rotation == 270;
            OK(quantapdf_native_device_plan_create("0 0 20 20",
                "6.1 3.1 16.1 11.1", "+07.0", rotation, dpis[d], 1000000u, &p));
            CHECK(p.width == (swap ? expected_height[d] : expected_width[d]));
            CHECK(p.height == (swap ? expected_width[d] : expected_height[d]));
            CHECK(p.width_points == (swap ? 56.0 : 70.0));
            CHECK(p.height_points == (swap ? 70.0 : 56.0));
            CHECK(p.stride == p.width * 4u && p.size == p.stride * p.height);
            CHECK(quantapdf_native_device_plan_matches_page(&p,
                (float)p.width_points, (float)p.height_points, 1000000u));
        }
    }
    {
        const char *units[] = {"0.1", ".125", ".5", ".875", "1.1", "1.25", "1.5"};
        const unsigned thousandths[] = {100u, 125u, 500u, 875u, 1100u, 1250u, 1500u};
        for (size_t u = 0u; u < sizeof(units) / sizeof(units[0]); ++u) {
            for (size_t d = 0u; d < 3u; ++d) {
                const uint64_t denominator = UINT64_C(72000);
                const uint64_t nw = UINT64_C(10) * thousandths[u] * dpis[d];
                const uint64_t nh = UINT64_C(8) * thousandths[u] * dpis[d];
                const size_t w = (size_t)(nw / denominator + (nw % denominator != 0u));
                const size_t h = (size_t)(nh / denominator + (nh % denominator != 0u));
                OK(quantapdf_native_device_plan_create("-20 -20 20 20",
                    "-6.1 -3.1 3.9 4.9", units[u], 0, dpis[d], 1000000u, &p));
                CHECK(p.width == w && p.height == h);
            }
        }
    }
    OK(quantapdf_native_device_plan_create("0 0 10 8", "0 0 10 8",
        "1", 0, 72u, 80u, &p));
    CHECK(p.width == 10u && p.height == 8u);
    memset(&p, 0xa5, sizeof(p));
    CHECK(quantapdf_native_device_plan_create("0 0 10 8", "0 0 10 8",
        "1", 0, 72u, 79u, &p) == QUANTAPDF_ERROR_UNSUPPORTED && zero_plan(&p));
    return 0;
}

static int test_crop_rotation_and_samples(void)
{
    const double corners[4][2] = {
        {10.25, 20.5}, {18.75, 20.5}, {18.75, 26.75}, {10.25, 26.75}
    };
    const unsigned expected[4][4][2] = {
        {{0u, 1u}, {1u, 1u}, {1u, 0u}, {0u, 0u}},
        {{0u, 0u}, {0u, 1u}, {1u, 1u}, {1u, 0u}},
        {{1u, 0u}, {0u, 0u}, {0u, 1u}, {1u, 1u}},
        {{1u, 1u}, {1u, 0u}, {0u, 0u}, {0u, 1u}}
    };
    for (unsigned r = 0u; r < 4u; ++r) {
        quantapdf_native_device_plan p;
        OK(quantapdf_native_device_plan_create("0 0 30 40",
            "10.25 20.5 18.75 26.75", "1.25", (int)(r * 90u),
            96u, 1000000u, &p));
        for (size_t c = 0u; c < 4u; ++c) {
            double dx, dy, px, py, cx, cy;
            map(p.source_to_device, corners[c][0], corners[c][1], &dx, &dy);
            CHECK(near(dx, expected[r][c][0] * (double)p.width));
            CHECK(near(dy, expected[r][c][1] * (double)p.height));
            map(p.source_to_display, corners[c][0], corners[c][1], &px, &py);
            map(p.from_composer, px, (double)(float)p.height_points - py, &cx, &cy);
            CHECK(near(cx, dx) && near(cy, dy));
        }
        {
            double dx, dy;
            map(p.source_to_device, 14.5, 23.625, &dx, &dy);
            CHECK(near(dx, (double)p.width / 2.0));
            CHECK(near(dy, (double)p.height / 2.0));
        }
    }
    {
        quantapdf_native_device_plan p;
        double sx, sy, x, y, dx, dy, cx, cy;
        OK(quantapdf_native_device_plan_create("0 0 595.28 841.89",
            "0 0 595.28 841.89", "1", 0, 72u, 1000000u, &p));
        CHECK(p.width == 596u && p.height == 842u);
        CHECK(p.width_points == 595.28 && p.height_points == 841.89);
        CHECK(p.from_composer[5] != 0.0);
        sx = 596.0 / 595.28;
        sy = 842.0 / 841.89;
        /* Independently invert the FIT viewport at the last pixel center.
         * Physical-DPI x=595.5 would be a different source location. */
        x = (596.0 - 0.5) / sx;
        y = 841.89 - (842.0 - 0.5) / sy;
        CHECK(x < 595.28 && x != 595.5);
        map(p.source_to_device, x, y, &dx, &dy);
        CHECK(near(dx, 595.5) && near(dy, 841.5));
        map(p.from_composer, x, (double)(float)p.height_points - y, &cx, &cy);
        CHECK(near(cx, dx) && near(cy, dy));
        /* The four antialias subpixel centers are explicit device samples. */
        for (unsigned sample = 0u; sample < 4u; ++sample) {
            const double center = ((double)sample + 0.5) / 4.0;
            x = center / sx;
            y = 841.89 - center / sy;
            map(p.source_to_device, x, y, &dx, &dy);
            CHECK(near(dx, center) && near(dy, center));
        }
        OK(quantapdf_native_device_plan_create("0 0 595.28 841.89",
            "0 0 595.28 841.89", "1", 0, 96u, 1000000u, &p));
        CHECK(p.width == 794u && p.height == 1123u);
        memset(&p, 0xa5, sizeof(p));
        CHECK(quantapdf_native_device_plan_create("0 0 595.28 841.89",
            "0 0 595.28 841.89", "1", 0, 144u, 1000000u, &p) ==
            QUANTAPDF_ERROR_UNSUPPORTED && zero_plan(&p));
    }
    return 0;
}

static int test_fail_closed(void)
{
    static const struct {
        const char *media, *crop, *unit;
        int rotation;
        unsigned dpi;
        size_t cap;
    } bad[] = {
        {"0 0 10 8", "0 0 10 8", "1", 45, 72u, 1000000u},
        {"0 0 10 8", "0 0 10 8", "1", 0, 73u, 1000000u},
        {"0 0 10 8", "0 0 10 8", "1", 0, 72u, 1000001u},
        {"0 0 10 8", "0 0 10.000000000001 8", "1", 0, 72u, 1000000u},
        {"0 0 10 8", "-.000000000001 0 10 8", "1", 0, 72u, 1000000u},
        {"0 0 10 8", "0 0 0 8", "1", 0, 72u, 1000000u},
        {"0 0 10 8", "0 0 11 8", "1", 0, 72u, 1000000u},
        {"0 0 10 8", "0 0 10", "1", 0, 72u, 1000000u},
        {"0 0 10 8", "0 0 10 8 1", "1", 0, 72u, 1000000u},
        {"0 0 10 8", "0 0 1e1 8", "1", 0, 72u, 1000000u},
        {"0 0 10 8", "0 0 NaN 8", "1", 0, 72u, 1000000u},
        {"0 0 10 8", "0 0 10\t8", "1", 0, 72u, 1000000u},
        {"0 0 10 8", "0 0 10 8", "0", 0, 72u, 1000000u},
        {"0 0 10 8", "0 0 10 8", "-1", 0, 72u, 1000000u},
        {"0 0 10 8", "0 0 10 8", "128.000000000001", 0, 72u, 1000000u},
        {"0 0 10 8", "0 0 10 8", "1.0000000000001", 0, 72u, 1000000u},
        {"0 0 9007199254740992 8", "0 0 10 8", "1", 0, 72u, 1000000u},
        {"-9007199254740991 0 9007199254740991 8",
         "-9007199254740991 0 9007199254740991 8", "128", 0, 144u, 1000000u},
        {"0 0 10 8", "0 0 1.000000000001 1", "1.000000000001", 0, 72u, 1000000u},
        {"0 0 10 8", "0 0 .000000000001 1", "1", 0, 72u, 1000000u}
    };
    quantapdf_native_device_plan p, valid;
    for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        memset(&p, 0xa5, sizeof(p));
        CHECK(quantapdf_native_device_plan_create(bad[i].media, bad[i].crop,
            bad[i].unit, bad[i].rotation, bad[i].dpi, bad[i].cap, &p) ==
            QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(zero_plan(&p));
    }
    memset(&p, 0xa5, sizeof(p));
    CHECK(quantapdf_native_device_plan_create(NULL, "0 0 10 8", "1",
        0, 72u, 1000000u, &p) == QUANTAPDF_ERROR_ARGUMENT && zero_plan(&p));
    memset(&p, 0xa5, sizeof(p));
    CHECK(quantapdf_native_device_plan_create("0 0 10 8", "0 0 10 8", "1",
        0, 72u, 0u, &p) == QUANTAPDF_ERROR_ARGUMENT && zero_plan(&p));
    CHECK(quantapdf_native_device_plan_create("0 0 10 8", "0 0 10 8", "1",
        0, 72u, 1000000u, NULL) == QUANTAPDF_ERROR_ARGUMENT);
    OK(quantapdf_native_device_plan_create("0 0 10 8", "0 0 10 8", "1",
        0, 72u, 1000000u, &valid));
    CHECK(!quantapdf_native_device_plan_matches_page(&valid, 11.0f, 8.0f, 1000000u));
    CHECK(!quantapdf_native_device_plan_matches_page(&valid, 10.0f, 8.0f, 79u));
    p = valid; p.mode = 0u;
    CHECK(!quantapdf_native_device_plan_matches_page(&p, 10.0f, 8.0f, 1000000u));
    p = valid; p.stride = SIZE_MAX;
    CHECK(!quantapdf_native_device_plan_matches_page(&p, 10.0f, 8.0f, 1000000u));
    p = valid; p.from_composer[0] = NAN;
    CHECK(!quantapdf_native_device_plan_matches_page(&p, 10.0f, 8.0f, 1000000u));
    p = valid; p.source_to_device[4] = 1.0;
    CHECK(!quantapdf_native_device_plan_matches_page(&p, 10.0f, 8.0f, 1000000u));
    p = valid; p.source_to_display[1] = 0.25;
    CHECK(!quantapdf_native_device_plan_matches_page(&p, 10.0f, 8.0f, 1000000u));
    p = valid; p.height = 0u;
    CHECK(!quantapdf_native_device_plan_matches_page(&p, 10.0f, 8.0f, 1000000u));
    return 0;
}

int main(void)
{
    if (test_exact_grid_and_unit() || test_crop_rotation_and_samples() ||
        test_fail_closed())
        return EXIT_FAILURE;
    puts("native device FIT plan: exact grids, source maps and refusal contracts passed");
    return EXIT_SUCCESS;
}
