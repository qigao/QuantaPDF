#ifndef QUANTAPDF_PDF_CONTENT_IR_H
#define QUANTAPDF_PDF_CONTENT_IR_H
#include <quantapdf/quantapdf.h>
#include <stddef.h>

/* Private typed operator tape. Coordinates remain in PDF user space.
 * No rendering, graphic-state evaluation, resource mapping or Composer lowering. */
typedef enum quantapdf_pdf_content_opcode {
    QUANTAPDF_PDF_OP_SAVE = 1,
    QUANTAPDF_PDF_OP_RESTORE,
    QUANTAPDF_PDF_OP_CONCAT,
    QUANTAPDF_PDF_OP_MOVE,
    QUANTAPDF_PDF_OP_LINE,
    QUANTAPDF_PDF_OP_CURVE,
    QUANTAPDF_PDF_OP_CURVE_V,
    QUANTAPDF_PDF_OP_CURVE_Y,
    QUANTAPDF_PDF_OP_CLOSE,
    QUANTAPDF_PDF_OP_RECT,
    QUANTAPDF_PDF_OP_STROKE,
    QUANTAPDF_PDF_OP_CLOSE_STROKE,
    QUANTAPDF_PDF_OP_FILL,
    QUANTAPDF_PDF_OP_FILL_EVEN_ODD,
    QUANTAPDF_PDF_OP_FILL_STROKE,
    QUANTAPDF_PDF_OP_FILL_STROKE_EVEN_ODD,
    QUANTAPDF_PDF_OP_CLOSE_FILL_STROKE,
    QUANTAPDF_PDF_OP_CLOSE_FILL_STROKE_EVEN_ODD,
    QUANTAPDF_PDF_OP_END_PATH,
    QUANTAPDF_PDF_OP_CLIP,
    QUANTAPDF_PDF_OP_CLIP_EVEN_ODD,
    QUANTAPDF_PDF_OP_LINE_WIDTH,
    QUANTAPDF_PDF_OP_STROKE_GRAY,
    QUANTAPDF_PDF_OP_FILL_GRAY,
    QUANTAPDF_PDF_OP_STROKE_RGB,
    QUANTAPDF_PDF_OP_FILL_RGB,
    QUANTAPDF_PDF_OP_LINE_CAP,
    QUANTAPDF_PDF_OP_LINE_JOIN,
    QUANTAPDF_PDF_OP_MITER_LIMIT,
    QUANTAPDF_PDF_OP_DASH
} quantapdf_pdf_content_opcode;

/* Strict private PDF and Composer dash-array bound. This is an operator
 * grammar budget, not an installed C ABI change. */
#define QUANTAPDF_PDF_MAX_DASH_ITEMS ((size_t)64u)

typedef struct quantapdf_pdf_content_op {
    quantapdf_pdf_content_opcode opcode;
    unsigned int argument_count;
    double arguments[6];
    size_t operator_byte_offset;
    /* DASH only: indices into the detached tape-owned double dash_values.
     * The phase is arguments[0]. [] produces count 0 (solid stroke). */
    size_t dash_offset, dash_count;
} quantapdf_pdf_content_op;

typedef struct quantapdf_pdf_content_ir {
    quantapdf_pdf_content_op *ops;
    size_t count;
    double *dash_values;
    size_t dash_value_count;
} quantapdf_pdf_content_ir;

typedef struct quantapdf_pdf_content_limits {
    size_t max_input_bytes, max_operators, max_graphics_depth;
} quantapdf_pdf_content_limits;

#ifdef __cplusplus
extern "C" {
#endif
/* Budgets must be nonzero. Unsupported tokens/operators fail UNSUPPORTED.
 * Invalid syntax/state fails FORMAT. Failure publishes no partial output.
 * Success owns an independent tape and bounded dash-value pool. Arrays other
 * than the numeric dash operand of d remain unsupported. Discard source bytes
 * freely and dispose the output with quantapdf_pdf_content_dispose. */
quantapdf_status quantapdf_pdf_content_parse(const unsigned char *data, size_t size,
    const quantapdf_pdf_content_limits *limits, quantapdf_pdf_content_ir **out_ir);
void quantapdf_pdf_content_dispose(quantapdf_pdf_content_ir *ir);
#ifdef __cplusplus
}
#endif
#endif
