#include "ir/pdf_content_ir.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(EXIT_FAILURE); } } while (0)
#define OK(x) CHECK((x) == QUANTAPDF_OK)

static quantapdf_pdf_content_limits budget(void)
{
    quantapdf_pdf_content_limits limits = {4096, 64, 8};
    return limits;
}

static void fails(const char *source, quantapdf_pdf_content_limits limits, quantapdf_status expected)
{
    quantapdf_pdf_content_ir *ir = (quantapdf_pdf_content_ir *)(uintptr_t)1;
    CHECK(quantapdf_pdf_content_parse((const unsigned char *)source, strlen(source),
        &limits, &ir) == expected);
    CHECK(ir == NULL);
}

static void test_path_tape(void)
{
    unsigned char input[] = "\t% comment\r\nq 1 0 0 1 2.5 -3 cm\n2 w 0.2 0.4 .6 rg\n0 0 m 10 0 l 10 10 l h f* Q";
    quantapdf_pdf_content_ir *ir = NULL;
    quantapdf_pdf_content_limits limits = budget();
    OK(quantapdf_pdf_content_parse(input, sizeof(input) - 1, &limits, &ir));
    CHECK(ir != NULL && ir->count == 10);
    CHECK(ir->ops[0].opcode == QUANTAPDF_PDF_OP_SAVE);
    CHECK(ir->ops[1].opcode == QUANTAPDF_PDF_OP_CONCAT && ir->ops[1].argument_count == 6);
    CHECK(ir->ops[1].arguments[4] == 2.5 && ir->ops[1].arguments[5] == -3.0);
    CHECK(ir->ops[2].opcode == QUANTAPDF_PDF_OP_LINE_WIDTH);
    CHECK(ir->ops[3].opcode == QUANTAPDF_PDF_OP_FILL_RGB &&
        ir->ops[3].arguments[2] > 0.599 && ir->ops[3].arguments[2] < 0.601);
    CHECK(ir->ops[4].opcode == QUANTAPDF_PDF_OP_MOVE);
    CHECK(ir->ops[5].opcode == QUANTAPDF_PDF_OP_LINE);
    CHECK(ir->ops[6].opcode == QUANTAPDF_PDF_OP_LINE);
    CHECK(ir->ops[7].opcode == QUANTAPDF_PDF_OP_CLOSE);
    CHECK(ir->ops[8].opcode == QUANTAPDF_PDF_OP_FILL_EVEN_ODD);
    CHECK(ir->ops[9].opcode == QUANTAPDF_PDF_OP_RESTORE);
    memset(input, 'X', sizeof(input) - 1);
    CHECK(ir->ops[1].arguments[4] == 2.5);
    quantapdf_pdf_content_dispose(ir);
}

static void test_extended_paths(void)
{
    const char *source = "0 0 m 1 1 2 2 3 3 c 4 4 5 5 v 6 6 7 7 y W n "
                         "1 2 3 4 re s 0 0 m b* 1 2 3 4 re F";
    quantapdf_pdf_content_ir *ir = NULL;
    quantapdf_pdf_content_limits limits = budget();
    OK(quantapdf_pdf_content_parse((const unsigned char *)source, strlen(source), &limits, &ir));
    CHECK(ir->count == 12);
    CHECK(ir->ops[1].opcode == QUANTAPDF_PDF_OP_CURVE);
    CHECK(ir->ops[2].opcode == QUANTAPDF_PDF_OP_CURVE_V);
    CHECK(ir->ops[3].opcode == QUANTAPDF_PDF_OP_CURVE_Y);
    CHECK(ir->ops[4].opcode == QUANTAPDF_PDF_OP_CLIP);
    CHECK(ir->ops[5].opcode == QUANTAPDF_PDF_OP_END_PATH);
    CHECK(ir->ops[7].opcode == QUANTAPDF_PDF_OP_CLOSE_STROKE);
    CHECK(ir->ops[9].opcode == QUANTAPDF_PDF_OP_CLOSE_FILL_STROKE_EVEN_ODD);
    CHECK(ir->ops[11].opcode == QUANTAPDF_PDF_OP_FILL);
    quantapdf_pdf_content_dispose(ir);
}

static void test_fail_closed(void)
{
    quantapdf_pdf_content_limits limits = budget();
    fails("BT /F1 12 Tf (hi) Tj ET", limits, QUANTAPDF_ERROR_UNSUPPORTED);
    fails("/Im1 Do", limits, QUANTAPDF_ERROR_UNSUPPORTED);
    fails("BI /W 1 /H 1 ID x EI", limits, QUANTAPDF_ERROR_UNSUPPORTED);
    fails("[1 2] 0 d", limits, QUANTAPDF_ERROR_UNSUPPORTED);
    fails("0 0 m 10 l", limits, QUANTAPDF_ERROR_FORMAT);
    fails("1 2 l", limits, QUANTAPDF_ERROR_FORMAT);
    fails("Q", limits, QUANTAPDF_ERROR_FORMAT);
    fails("q", limits, QUANTAPDF_ERROR_FORMAT);
    fails("1e2 0 m", limits, QUANTAPDF_ERROR_FORMAT);
    fails("1 0 w", limits, QUANTAPDF_ERROR_FORMAT);
    fails("-1 w", limits, QUANTAPDF_ERROR_FORMAT);
    fails("1 2", limits, QUANTAPDF_ERROR_FORMAT);
    fails("1 2 3 4 5 6 7 cm", limits, QUANTAPDF_ERROR_UNSUPPORTED);
    limits.max_operators = 1;
    fails("0 0 m S", limits, QUANTAPDF_ERROR_UNSUPPORTED);
    limits = budget(); limits.max_graphics_depth = 1;
    fails("q q Q Q", limits, QUANTAPDF_ERROR_UNSUPPORTED);
    limits = budget(); limits.max_input_bytes = 1;
    fails("0 0 m", limits, QUANTAPDF_ERROR_UNSUPPORTED);
    limits = budget(); limits.max_input_bytes = 0;
    fails("", limits, QUANTAPDF_ERROR_ARGUMENT);
}

static void test_empty_detached(void)
{
    quantapdf_pdf_content_ir *ir = (quantapdf_pdf_content_ir *)(uintptr_t)1;
    quantapdf_pdf_content_limits limits = budget();
    CHECK(quantapdf_pdf_content_parse(NULL, 0, &limits, &ir) == QUANTAPDF_OK);
    CHECK(ir != NULL && ir->count == 0 && ir->ops == NULL);
    quantapdf_pdf_content_dispose(ir);
    quantapdf_pdf_content_dispose(NULL);
    ir = (quantapdf_pdf_content_ir *)(uintptr_t)1;
    CHECK(quantapdf_pdf_content_parse(NULL, 4, &limits, &ir) == QUANTAPDF_ERROR_ARGUMENT);
    CHECK(ir == NULL);
}

int main(void)
{
    test_path_tape();
    test_extended_paths();
    test_fail_closed();
    test_empty_detached();
    return 0;
}
