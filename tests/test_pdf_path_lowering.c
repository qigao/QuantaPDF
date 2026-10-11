#include "ir/pdf_path_lowering.h"
#include "backend/qpdf_composer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(EXIT_FAILURE); } } while(0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

static quantapdf_pdf_content_limits parse_limits(void)
{
    quantapdf_pdf_content_limits v = {4096, 128, 8};
    return v;
}
static quantapdf_pdf_path_lower_limits lower_limits(void)
{
    quantapdf_pdf_path_lower_limits v = {64, 1024};
    return v;
}

static void parse_page(const char *source, quantapdf_pdf_page_semantics *page)
{
    quantapdf_pdf_content_limits limits = parse_limits();
    memset(page, 0, sizeof(*page));
    OK(quantapdf_pdf_content_parse((const unsigned char *)source, strlen(source),
        &limits, &page->content));
    OK(quantapdf_pdf_graphics_evaluate(page->content, &limits, &page->graphics));
}

static void dispose_page(quantapdf_pdf_page_semantics *page)
{
    quantapdf_pdf_graphics_dispose(page->graphics);
    quantapdf_pdf_content_dispose(page->content);
    memset(page, 0, sizeof(*page));
}

static void rejects(const char *content, quantapdf_status expected)
{
    quantapdf_pdf_page_semantics page;
    quantapdf_ir ir;
    quantapdf_pdf_path_lower_limits limits = lower_limits();
    parse_page(content, &page);
    memset(&ir, 0xab, sizeof(ir));
    CHECK(quantapdf_pdf_path_lower_to_composer(&page, 200, 120,
        &limits, &ir) == expected);
    CHECK(ir.pages == NULL && ir.operations == NULL &&
        ir.page_count == 0 && ir.operation_count == 0);
    dispose_page(&page);
}

static void test_paths_and_curves(void)
{
    const char *source =
        "q 0 1 -1 0 40 50 cm "
        "1 0 0 RG 0 0 1 rg 2 w "
        "0 0 m 10 10 20 20 30 30 c 40 40 50 50 v 60 60 70 70 y h B* "
        "Q 10 20 30 40 re f";
    quantapdf_pdf_page_semantics page;
    quantapdf_ir ir = {0};
    quantapdf_pdf_path_lower_limits limits = lower_limits();
    parse_page(source, &page);
    OK(quantapdf_pdf_path_lower_to_composer(&page, 200, 120, &limits, &ir));
    CHECK(ir.page_count == 1 && ir.operation_count == 2);
    CHECK(ir.pages[0].suppress_background == 1 &&
        ir.pages[0].height_points == 120 && ir.pages[0].width_points == 200);
    const quantapdf_composer_path_operation *first = &ir.operations[0].value.path;
    CHECK(first->command_count == 5 && first->options.stroke == 1 &&
        first->options.fill == 1 &&
        first->options.fill_rule == QUANTAPDF_COMPOSER_FILL_EVEN_ODD);
    CHECK(first->options.stroke_argb == UINT32_C(0xffff0000) &&
        first->options.fill_argb == UINT32_C(0xff0000ff) &&
        first->options.stroke_width == 2);
    CHECK(first->options.transform.a == 0 && first->options.transform.b == -1 &&
        first->options.transform.c == 1 && first->options.transform.d == 0 &&
        first->options.transform.e == -80 && first->options.transform.f == 70);
    CHECK(first->commands[0].kind == QUANTAPDF_COMPOSER_PATH_MOVE_TO &&
        first->commands[0].point1.y == 120);
    CHECK(first->commands[1].kind == QUANTAPDF_COMPOSER_PATH_CUBIC_TO &&
        first->commands[1].point1.x == 10 && first->commands[1].point3.y == 90);
    CHECK(first->commands[2].kind == QUANTAPDF_COMPOSER_PATH_CUBIC_TO &&
        first->commands[2].point1.x == 30 && first->commands[2].point3.x == 50);
    CHECK(first->commands[3].kind == QUANTAPDF_COMPOSER_PATH_CUBIC_TO &&
        first->commands[3].point2.x == 70 && first->commands[3].point3.y == 50);
    CHECK(first->commands[4].kind == QUANTAPDF_COMPOSER_PATH_CLOSE);
    const quantapdf_composer_path_operation *second = &ir.operations[1].value.path;
    CHECK(second->command_count == 5 && !second->options.stroke &&
        second->options.fill && second->commands[4].kind == QUANTAPDF_COMPOSER_PATH_CLOSE);
    CHECK(second->commands[0].point1.x == 10 && second->commands[0].point1.y == 100);
    CHECK(second->commands[2].point1.x == 40 && second->commands[2].point1.y == 60);
    dispose_page(&page);
    CHECK(ir.operations[0].value.path.commands[1].point3.x == 30);
    quantapdf_ir_dispose(&ir);
    CHECK(ir.pages == NULL && ir.operations == NULL && ir.operation_count == 0);
}

static void test_close_and_color(void)
{
    quantapdf_pdf_page_semantics page;
    quantapdf_ir ir = {0};
    quantapdf_pdf_path_lower_limits limits = lower_limits();
    parse_page("0.2 0.4 0.6 rg 1 2 m 3 4 l s "
               "0 0 m 4 0 l 4 4 l b* ", &page);
    OK(quantapdf_pdf_path_lower_to_composer(&page, 200, 120, &limits, &ir));
    CHECK(ir.operation_count == 2);
    CHECK(ir.operations[0].value.path.command_count == 3);
    CHECK(ir.operations[0].value.path.commands[2].kind ==
        QUANTAPDF_COMPOSER_PATH_CLOSE);
    CHECK(ir.operations[1].value.path.options.fill_rule ==
        QUANTAPDF_COMPOSER_FILL_EVEN_ODD);
    CHECK(ir.operations[1].value.path.options.fill_argb == UINT32_C(0xff336699));
    CHECK(ir.operations[1].value.path.command_count == 4);
    dispose_page(&page); quantapdf_ir_dispose(&ir);
}

static void test_fail_closed(void)
{
    rejects("0 0 m W n 0 0 10 10 re f", QUANTAPDF_ERROR_UNSUPPORTED);
    rejects("0 0 m 10 10 l n", QUANTAPDF_ERROR_UNSUPPORTED);
    rejects("0 0 m f", QUANTAPDF_ERROR_UNSUPPORTED);
    rejects("0.5 g 0 0 10 10 re f", QUANTAPDF_ERROR_UNSUPPORTED);
    rejects("0.12345 0 m 10 10 l S", QUANTAPDF_ERROR_UNSUPPORTED);
    rejects("65537 0 m 10 10 l S", QUANTAPDF_ERROR_UNSUPPORTED);
    rejects("1 0 0 0 0 0 cm 0 0 10 10 re f", QUANTAPDF_ERROR_UNSUPPORTED);
    {
        quantapdf_pdf_page_semantics page;
        quantapdf_ir ir = {0};
        quantapdf_pdf_path_lower_limits limits = lower_limits();
        parse_page("0 0 10 10 re f 40 0 10 10 re f", &page);
        limits.max_painted_paths = 1;
        CHECK(quantapdf_pdf_path_lower_to_composer(&page, 200, 120,
            &limits, &ir) == QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(ir.pages == NULL && ir.operations == NULL);
        limits = lower_limits(); limits.max_path_commands = 9;
        CHECK(quantapdf_pdf_path_lower_to_composer(&page, 200, 120,
            &limits, &ir) == QUANTAPDF_ERROR_UNSUPPORTED);
        CHECK(ir.pages == NULL && ir.operations == NULL);
        limits = lower_limits();
        CHECK(quantapdf_pdf_path_lower_to_composer(&page, 200, 0,
            &limits, &ir) == QUANTAPDF_ERROR_ARGUMENT);
        CHECK(ir.pages == NULL && ir.operations == NULL);
        dispose_page(&page);
    }
}

static void test_pdf_export_and_render(void)
{
    quantapdf_pdf_page_semantics page;
    quantapdf_ir ir = {0};
    quantapdf_pdf_path_lower_limits limits = lower_limits();
    unsigned char *first = NULL, *second = NULL;
    size_t first_size = 0, second_size = 0;
    quantapdf_document *document = NULL;
    quantapdf_page *render_page = NULL;
    quantapdf_bitmap *bitmap = NULL;
    const unsigned char *pixels = NULL;
    size_t bytes = 0;
    int width = 0, height = 0, stride = 0, components = 0;
    FILE *f;
    parse_page("q 1 0 0 1 25 10 cm "
               "1 0 0 RG 0 0 1 rg 2 w "
               "0 0 m 20 0 l 20 20 l 0 20 l h B Q", &page);
    OK(quantapdf_pdf_path_lower_to_composer(&page, 200, 120, &limits, &ir));
    dispose_page(&page);
    OK(quantapdf_qpdf_compose(&ir, &first, &first_size));
    OK(quantapdf_qpdf_compose(&ir, &second, &second_size));
    CHECK(first_size > 0 && first_size == second_size &&
        memcmp(first, second, first_size) == 0);
    quantapdf_ir_dispose(&ir);
    f = fopen(PATH_LOWER_OUTPUT_PDF, "wb");
    CHECK(f != NULL && fwrite(first, 1, first_size, f) == first_size && fclose(f) == 0);
    free(first); free(second);
    OK(quantapdf_open(PATH_LOWER_OUTPUT_PDF, NULL, &document));
    OK(quantapdf_load_page(document, 0, &render_page));
    OK(quantapdf_render_page(render_page, &bitmap));
    OK(quantapdf_bitmap_dimensions(bitmap, &width, &height, &stride, &components));
    CHECK(width == 200 && height == 120 && components == 3);
    OK(quantapdf_bitmap_data(bitmap, &pixels, &bytes));
    CHECK(bytes >= (size_t)height * (size_t)stride);
    {
        const unsigned char *inside = pixels + (size_t)100 * (size_t)stride + 35 * 3;
        const unsigned char *outside = pixels + (size_t)5 * (size_t)stride + 5 * 3;
        CHECK(inside[0] < 20 && inside[1] < 20 && inside[2] > 230);
        CHECK(outside[0] > 230 && outside[1] > 230 && outside[2] > 230);
    }
    quantapdf_drop_bitmap(bitmap);
    quantapdf_drop_page(render_page);
    quantapdf_close(document);
    CHECK(remove(PATH_LOWER_OUTPUT_PDF) == 0);
}


static void test_stroke_style_lowering(void)
{
    quantapdf_pdf_page_semantics page;
    quantapdf_ir ir = {0};
    quantapdf_pdf_path_lower_limits limits = lower_limits();
    parse_page("q 2 J 1 j 8 M [3 1 0 2] 1.25 d "
               "1 0 0 RG 5 w 0 0 m 80 0 l S Q "
               "0 0 m 80 0 l S", &page);
    OK(quantapdf_pdf_path_lower_to_composer(&page, 200, 120, &limits, &ir));
    CHECK(ir.operation_count == 2);
    const quantapdf_composer_path_operation *first = &ir.operations[0].value.path;
    CHECK(first->options.line_cap == QUANTAPDF_COMPOSER_LINE_CAP_SQUARE &&
        first->options.line_join == QUANTAPDF_COMPOSER_LINE_JOIN_ROUND &&
        first->options.miter_limit == 8.0f &&
        first->options.stroke_width == 5.0f);
    CHECK(first->dash_count == 4 && first->dash_phase == 1.25f);
    CHECK(first->dash_lengths[0] == 3.0f && first->dash_lengths[1] == 1.0f &&
        first->dash_lengths[2] == 0.0f && first->dash_lengths[3] == 2.0f);
    const quantapdf_composer_path_operation *second = &ir.operations[1].value.path;
    CHECK(second->options.line_cap == QUANTAPDF_COMPOSER_LINE_CAP_BUTT &&
        second->options.line_join == QUANTAPDF_COMPOSER_LINE_JOIN_MITER &&
        second->options.miter_limit == 10.0f &&
        second->dash_count == 0 && second->dash_lengths == NULL);
    dispose_page(&page);
    CHECK(ir.operations[0].value.path.dash_lengths[3] == 2.0f);
    quantapdf_ir_dispose(&ir);

    parse_page("[1.23456 2] 0 d 0 0 m 80 0 l S", &page);
    CHECK(quantapdf_pdf_path_lower_to_composer(&page, 200, 120,
        &limits, &ir) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(ir.pages == NULL && ir.operations == NULL &&
        ir.resources.clip_count == 0);
    dispose_page(&page);
    parse_page("1.23456 M 0 0 m 80 0 l S", &page);
    CHECK(quantapdf_pdf_path_lower_to_composer(&page, 200, 120,
        &limits, &ir) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(ir.pages == NULL && ir.operations == NULL);
    dispose_page(&page);
    parse_page("[2 1] 0.12345 d 0 0 m 80 0 l S", &page);
    CHECK(quantapdf_pdf_path_lower_to_composer(&page, 200, 120,
        &limits, &ir) == QUANTAPDF_ERROR_UNSUPPORTED);
    CHECK(ir.pages == NULL && ir.operations == NULL);
    dispose_page(&page);
}

int main(void)
{
    test_stroke_style_lowering();
    test_paths_and_curves();
    test_close_and_color();
    test_fail_closed();
    test_pdf_export_and_render();
    return 0;
}
