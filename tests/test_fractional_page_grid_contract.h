#ifndef QUANTAPDF_TEST_FRACTIONAL_PAGE_GRID_CONTRACT_H
#define QUANTAPDF_TEST_FRACTIONAL_PAGE_GRID_CONTRACT_H

/* Included by test_render_plan.c: exercise the REAL allocation-free planner.
 * These are geometry contracts, not a native raster or a PDFium pixel oracle.
 * A displayed-page render clip is NOT a raw PDF CropBox. */
static void test_fractional_grid_extents(void)
{
    static const struct { float dpi; int width, height; } cases[] = {
        {72.0f, 596, 842}, {96.0f, 794, 1123}, {144.0f, 1191, 1684}
    };
    size_t i;
    int alpha, narrowed, repeat;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        for (alpha = 0; alpha <= 1; ++alpha) {
            for (narrowed = 0; narrowed <= 1; ++narrowed) {
                const double width = narrowed ? (double)(float)595.28 : 595.28;
                const double height = narrowed ? (double)(float)841.89 : 841.89;
                for (repeat = 0; repeat < 3; ++repeat) {
                    quantapdf_render_plan plan;
                    memset(&plan, 0xa5, sizeof(plan));
                    OK(quantapdf_render_plan_create(width, height, cases[i].dpi,
                        0, NULL, alpha, &plan));
                    CHECK(plan.width == cases[i].width && plan.height == cases[i].height);
                    CHECK(plan.aligned && plan.start_x == 0 && plan.start_y == 0);
                    CHECK(plan.size_x == plan.width && plan.size_y == plan.height);
                    CHECK(plan.components == 3 + alpha);
                    CHECK(plan.stride == plan.width * plan.components);
                    CHECK(plan.size == (size_t)plan.stride * (size_t)plan.height);
                    CHECK(plan.background_argb == (alpha ? 0u : 0xffffffffu));
                }
            }
        }
    }
}

static void test_fractional_grid_aligned_is_not_unit_scale(void)
{
    quantapdf_render_plan plan;
    quantapdf_affine_transform matrix;
    double sx, sy, center_x, center_y;
    OK(quantapdf_render_plan_create(595.28, 841.89, 72, 0, NULL, 0, &plan));
    OK(quantapdf_render_plan_map_source(&plan, &identity, &matrix));
    point(&matrix, 595.28, 841.89, 595.28, 841.89);
    CHECK(matrix.a == 1 && matrix.d == 1);
    /* Analytic consequences of the aligned draw rectangle; NOT independently
     * measured PDFium matrix/sample positions. Integration must measure them. */
    sx = (double)plan.size_x / 595.28;
    sy = (double)plan.size_y / 841.89;
    CHECK(sx > 1.0 && sy > 1.0 && fabs(sx - sy) > 0.001);
    CHECK(fabs((double)plan.width - 595.28) > 0.7);
    CHECK(fabs((double)plan.height - 841.89) > 0.1);
    center_x = (0.5 - plan.start_x) / sx;
    center_y = (0.5 - plan.start_y) / sy;
    CHECK(center_x > 0 && center_x < 0.5);
    CHECK(center_y > 0 && center_y < 0.5);
    CHECK(fabs(center_x * sx + plan.start_x - 0.5) < 1e-12);
    CHECK(fabs(center_y * sy + plan.start_y - 0.5) < 1e-12);
}

static void test_fractional_grid_render_clip(void)
{
    quantapdf_render_plan plan;
    quantapdf_affine_transform matrix;
    quantapdf_rect clip = {10.25f, 20.5f, 110.75f, 220.875f};
    OK(quantapdf_render_plan_create(595.28, 841.89, 72, 0, &clip, 1, &plan));
    CHECK(plan.width == 101 && plan.height == 201);
    CHECK(plan.size_x == 596 && plan.size_y == 842);
    CHECK(plan.start_x == -11 && plan.start_y == -21);
    CHECK(plan.stride == 404 && plan.size == 81204u);
    OK(quantapdf_render_plan_map_source(&plan, &identity, &matrix));
    point(&matrix, 10.25, 20.5, 0, 0);
    point(&matrix, 110.75, 220.875, 100.5, 200.375);
    CHECK(fabs(matrix.e - plan.start_x) == 0.75);
    CHECK(fabs(matrix.f - plan.start_y) == 0.5);
    /* Negative fractional origin is legal for a render clip; it must not be
     * silently clamped to the page. The output rectangle is still bounded. */
    clip = (quantapdf_rect){-0.25f, -0.5f, 1.25f, 2.125f};
    OK(quantapdf_render_plan_create(595.28, 841.89, 72, 0, &clip, 0, &plan));
    CHECK(plan.width == 2 && plan.height == 3);
    CHECK(plan.start_x == 0 && plan.start_y == 0);
    OK(quantapdf_render_plan_map_source(&plan, &identity, &matrix));
    point(&matrix, -0.25, -0.5, 0, 0);
    point(&matrix, 1.25, 2.125, 1.5, 2.625);
}

static void test_fractional_grid_quarter_turns(void)
{
    static const struct { float degrees; int width, height; double x0, y0, x1, y1; } cases[] = {
        {90, 842, 596, 842, 0, 0.11, 595.28},
        {180, 596, 842, 596, 842, 0.72, 0.11},
        {270, 842, 596, 0, 596, 841.89, 0.72},
        {-90, 842, 596, 0, 596, 841.89, 0.72}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        quantapdf_render_plan plan;
        quantapdf_affine_transform matrix;
        double det, source_x, source_y;
        OK(quantapdf_render_plan_create(595.28, 841.89, 72,
            cases[i].degrees, NULL, 0, &plan));
        CHECK(!plan.aligned && plan.width == cases[i].width && plan.height == cases[i].height);
        OK(quantapdf_render_plan_map_source(&plan, &identity, &matrix));
        point(&matrix, 0, 0, cases[i].x0, cases[i].y0);
        point(&matrix, 595.28, 841.89, cases[i].x1, cases[i].y1);
        /* Invert the actual mapped matrix at one device-pixel center. This is
         * algebraic sample-location coverage, not a PDFium pixel comparison. */
        det = (double)matrix.a * matrix.d - (double)matrix.b * matrix.c;
        CHECK(fabs(det) == 1.0);
        source_x = (matrix.d * (0.5 - matrix.e) - matrix.c * (0.5 - matrix.f)) / det;
        source_y = (-matrix.b * (0.5 - matrix.e) + matrix.a * (0.5 - matrix.f)) / det;
        point(&matrix, source_x, source_y, 0.5, 0.5);
    }
}

static void test_fractional_grid_producer_normalization(void)
{
    quantapdf_render_plan plan;
    quantapdf_affine_transform matrix;
    /* Synthetic producer transform: raw box origin (10.25,20.5), UserUnit=2,
     * y-up -> displayed y-down. Intrinsic geometry belongs to the producer;
     * passing the displayed extent must not apply UserUnit a second time. */
    const quantapdf_render_transform raw = {2, 0, 0, -2, -20.5, 882.89};
    OK(quantapdf_render_plan_create(595.28, 841.89, 72, 0, NULL, 0, &plan));
    OK(quantapdf_render_plan_map_source(&plan, &raw, &matrix));
    point(&matrix, 10.25, 441.445, 0, 0);
    point(&matrix, 307.89, 20.5, 595.28, 841.89);
    CHECK(plan.width == 596 && plan.height == 842);
    OK(quantapdf_render_plan_create(595.28, 841.89, 144, 90, NULL, 0, &plan));
    CHECK(plan.width == 1684 && plan.height == 1191);
    OK(quantapdf_render_plan_map_source(&plan, &raw, &matrix));
    point(&matrix, 10.25, 441.445, 1684, 0);
    point(&matrix, 307.89, 20.5, 0.22, 1190.56);
}

static void test_fractional_grid_precision_and_overflow(void)
{
    quantapdf_render_plan precise, narrowed, plan;
    quantapdf_rect clip = {0, 0, 1, 1};
    const double just_above = 595.0 + 0.000001;
    /* Counterexample to using float IR extents as a universal original-device
     * oracle. Do not "fix" this by rounding the original PDF or accepting an
     * extra native positive. A precision/admission policy remains required. */
    CHECK((float)just_above == 595.0f);
    OK(quantapdf_render_plan_create(just_above, 1, 72, 0, NULL, 0, &precise));
    OK(quantapdf_render_plan_create((double)(float)just_above, 1, 72, 0, NULL, 0, &narrowed));
    CHECK(precise.width == 596 && narrowed.width == 595);
#define FAIL_FRACTIONAL(w, h, region, alpha) do { \
    memset(&plan, 0xa5, sizeof(plan)); \
    CHECK(quantapdf_render_plan_create(w, h, 72, 0, region, alpha, &plan) == QUANTAPDF_ERROR_ARGUMENT); \
    zeroed(&plan, sizeof(plan)); \
} while (0)
    FAIL_FRACTIONAL((double)(INT_MAX / 4) + 0.25, 1, NULL, 1);
    FAIL_FRACTIONAL(1, (double)INT_MAX + 0.25, NULL, 0);
    /* A small render clip must not hide an unrepresentable full-page draw
     * rectangle passed to the aligned provider. No allocation is attempted. */
    FAIL_FRACTIONAL((double)INT_MAX + 0.25, 1, &clip, 0);
#undef FAIL_FRACTIONAL
}

static void test_fractional_page_grid_contract(void)
{
    test_fractional_grid_extents();
    test_fractional_grid_aligned_is_not_unit_scale();
    test_fractional_grid_render_clip();
    test_fractional_grid_quarter_turns();
    test_fractional_grid_producer_normalization();
    test_fractional_grid_precision_and_overflow();
    fprintf(stderr, "fractional-grid-contract: groups=6 planner=passed "
        "original_pdfium_pixel_oracle=not_run native_positive=0\n");
}
#endif
