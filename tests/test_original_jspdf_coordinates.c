#include <fpdfview.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Read-only, direct PDFium oracle. No QuantaPDF planner, raster or PDF writer.
 * API device inputs are INTEGER anchors, NOT antialiasing sample centers.
 * rotate is PDFium's rectangle orientation, NOT QuantaPDF's extra rotation. */
#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
    return 0; \
} } while (0)

typedef struct coordinate_case {
    int dpi, rotate, start_x, start_y, draw_width, draw_height;
    int output_width, output_height;
} coordinate_case;

/* Fixed independent rectangles from #269, not values calculated by RenderPlan.
 * For clips the full-page DRAW extent must not become the OUTPUT extent. */
static const coordinate_case cases[] = {
    {72, 0, 0, 0, 596, 842, 596, 842},
    {72, 1, 0, 0, 842, 596, 842, 596},
    {72, 2, 0, 0, 596, 842, 596, 842},
    {72, 3, 0, 0, 842, 596, 842, 596},
    {72, 0, -11, -21, 596, 842, 101, 201},
    {72, 0, 0, 0, 596, 842, 2, 3},
    {96, 0, 0, 0, 794, 1123, 794, 1123},
    {96, 1, 0, 0, 1123, 794, 1123, 794},
    {96, 2, 0, 0, 794, 1123, 794, 1123},
    {96, 3, 0, 0, 1123, 794, 1123, 794},
    {96, 0, -14, -28, 794, 1123, 134, 268},
    {96, 0, 0, 0, 794, 1123, 2, 4},
    {144, 0, 0, 0, 1191, 1684, 1191, 1684},
    {144, 1, 0, 0, 1684, 1191, 1684, 1191},
    {144, 2, 0, 0, 1191, 1684, 1191, 1684},
    {144, 3, 0, 0, 1684, 1191, 1684, 1191},
    {144, 0, -21, -41, 1191, 1684, 201, 401},
    {144, 0, 0, 1, 1191, 1684, 3, 6}
};

enum { CASE_COUNT = 18, POINT_COUNT = 9, REPEAT_COUNT = 3 };
_Static_assert(sizeof(cases) / sizeof(cases[0]) == CASE_COUNT, "case table");

static int qualify(FPDF_PAGE page)
{
    const double tolerance = 0.0005; /* points; float-backed API, NOT pixels */
    const double width = FPDF_GetPageWidthF(page);
    const double height = FPDF_GetPageHeightF(page);
    FS_RECTF box = {0};
    double baseline[CASE_COUNT][POINT_COUNT][2] = {{{0}}};
    double maximum_error = 0.0, inverse_dx, inverse_dy;
    int checks = 0, repeat, ci, pi;
    REQUIRE(isfinite(width) && isfinite(height));
    REQUIRE(fabs(width - 595.28) < 0.0001 && fabs(height - 841.89) < 0.0001);
    REQUIRE(FPDF_GetPageBoundingBox(page, &box));
    REQUIRE(box.left == 0.0f && box.bottom == 0.0f);
    REQUIRE((double)box.right == width && (double)box.top == height);

    for (repeat = 0; repeat < REPEAT_COUNT; ++repeat) {
        for (ci = 0; ci < CASE_COUNT; ++ci) {
            const coordinate_case *c = &cases[ci];
            const int points[POINT_COUNT][2] = {
                {c->start_x, c->start_y},
                {c->start_x + c->draw_width, c->start_y},
                {c->start_x, c->start_y + c->draw_height},
                {c->start_x + c->draw_width, c->start_y + c->draw_height},
                {0, 0}, {1, 0}, {0, 1},
                {c->output_width / 2, c->output_height / 2},
                {c->output_width - 1, c->output_height - 1}
            };
            for (pi = 0; pi < POINT_COUNT; ++pi) {
                const int dx = points[pi][0], dy = points[pi][1];
                const double nx = (double)(dx - c->start_x) / c->draw_width;
                const double ny = (double)(dy - c->start_y) / c->draw_height;
                double px = NAN, py = NAN, expected_x, expected_y, error;
                int back_x = -9999, back_y = -9999;
                switch (c->rotate) {
                case 0: expected_x = nx * width; expected_y = (1 - ny) * height; break;
                case 1: expected_x = ny * width; expected_y = nx * height; break;
                case 2: expected_x = (1 - nx) * width; expected_y = ny * height; break;
                default: expected_x = (1 - ny) * width; expected_y = (1 - nx) * height; break;
                }
                REQUIRE(FPDF_DeviceToPage(page, c->start_x, c->start_y,
                    c->draw_width, c->draw_height, c->rotate, dx, dy, &px, &py));
                REQUIRE(isfinite(px) && isfinite(py));
                error = fmax(fabs(px - expected_x), fabs(py - expected_y));
                if (error > maximum_error) maximum_error = error;
                if (error > tolerance) {
                    fprintf(stderr, "case=%d dpi=%d rotate=%d device=(%d,%d) "
                        "actual=(%.17g,%.17g) expected=(%.17g,%.17g)\n",
                        ci, c->dpi, c->rotate, dx, dy, px, py, expected_x, expected_y);
                }
                REQUIRE(error <= tolerance);
                REQUIRE(FPDF_PageToDevice(page, c->start_x, c->start_y,
                    c->draw_width, c->draw_height, c->rotate, px, py, &back_x, &back_y));
                REQUIRE(back_x == dx && back_y == dy);
                if (repeat == 0) {
                    baseline[ci][pi][0] = px;
                    baseline[ci][pi][1] = py;
                } else {
                    REQUIRE(px == baseline[ci][pi][0] && py == baseline[ci][pi][1]);
                }
                ++checks;
            }
        }
    }
    REQUIRE(checks == CASE_COUNT * POINT_COUNT * REPEAT_COUNT);
    /* Measured differences of integer-anchor API observations. In particular,
     * the aligned 72-DPI rectangle does not map one device unit to one point. */
    inverse_dx = baseline[0][5][0] - baseline[0][4][0];
    inverse_dy = baseline[0][6][1] - baseline[0][4][1];
    REQUIRE(inverse_dx > 0 && inverse_dx < 1 && fabs(inverse_dx - 1) > 0.001);
    REQUIRE(inverse_dy < 0 && fabs(fabs(inverse_dy) - 1) > 0.00005);
    printf("{\n  \"schema_version\": 1,\n"
        "  \"result\": \"original_pdfium_coordinate_api_passed\",\n"
        "  \"document_id\": \"jspdf.w3s-clip-v1\",\n"
        "  \"coordinate_cases\": %d,\n  \"point_checks\": %d,\n"
        "  \"repeats\": %d,\n  \"numeric_repeat_equal\": true,\n"
        "  \"page_width_provider\": %.17g,\n  \"page_height_provider\": %.17g,\n"
        "  \"max_error_points\": %.17g,\n  \"tolerance_points\": %.17g,\n"
        "  \"measured_72dpi_inverse_dx\": %.17g,\n"
        "  \"measured_72dpi_inverse_dy\": %.17g,\n"
        "  \"pixel_center_observations\": 0,\n  \"raster_invocations\": 0,\n"
        "  \"native_pixel_positive_count\": 0,\n  \"official_corpus_admitted\": false,\n"
        "  \"public_requested_rotation_qualified\": false\n}\n",
        CASE_COUNT, checks, REPEAT_COUNT, width, height, maximum_error, tolerance,
        inverse_dx, inverse_dy);
    REQUIRE(fflush(stdout) == 0 && !ferror(stdout));
    return 1;
}

int main(int argc, char **argv)
{
    FPDF_DOCUMENT document;
    FPDF_PAGE page = NULL;
    int success = 0;
    if (argc != 2 || !argv[1][0]) return EXIT_FAILURE;
    FPDF_InitLibrary();
    document = FPDF_LoadDocument(argv[1], NULL);
    if (document != NULL && FPDF_GetPageCount(document) == 1) {
        page = FPDF_LoadPage(document, 0);
        if (page != NULL) success = qualify(page);
    }
    if (!success) fprintf(stderr, "original jsPDF coordinate oracle failed\n");
    if (page != NULL) FPDF_ClosePage(page);
    if (document != NULL) FPDF_CloseDocument(document);
    FPDF_DestroyLibrary();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
