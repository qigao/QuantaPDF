#ifndef QUANTAPDF_PDF_GRAPHICS_STATE_H
#define QUANTAPDF_PDF_GRAPHICS_STATE_H
#include "pdf_content_ir.h"

/* CTM uses PDF row vectors: [x y 1] * [a b 0;c d 0;e f 1].
 * Initial CTM is identity relative to the page's default user space;
 * page rotation, boxes and /UserUnit are outside this stream. */
typedef struct quantapdf_pdf_graphics_state {
    double ctm[6];
    double stroke_rgb[3];
    double fill_rgb[3];
    double line_width;
    double miter_limit, dash_phase;
    size_t dash_offset, dash_count; /* into the owning Content IR pool */
    int line_cap, line_join; /* PDF J/j values 0..2 */
    size_t clip_id; /* 0 = full page, else 1-based immutable clip chain */
} quantapdf_pdf_graphics_state;

typedef struct quantapdf_pdf_clip_record {
    size_t parent_clip_id;
    size_t path_first_operator;
    size_t clip_operator;
    size_t terminal_operator;
    int even_odd;
} quantapdf_pdf_clip_record;

typedef struct quantapdf_pdf_path_record {
    size_t path_first_operator;
    size_t terminal_operator;
    quantapdf_pdf_content_opcode paint_opcode; /* n means no paint */
    quantapdf_pdf_graphics_state graphics; /* before clip is applied */
    size_t clip_after;
} quantapdf_pdf_path_record;

typedef struct quantapdf_pdf_graphics_ir {
    quantapdf_pdf_path_record *paths;
    size_t path_count;
    quantapdf_pdf_clip_record *clips;
    size_t clip_count;
} quantapdf_pdf_graphics_ir;

#ifdef __cplusplus
extern "C" {
#endif
/* Pure C, transactional evaluation of a fully admitted operator tape.
 * Graphics state is saved/restored; paths are not. W/W* becomes effective
 * only after the path is terminated, with a parent chain for q/Q.
 * Does not rasterize or compute geometric clip intersections. */
quantapdf_status quantapdf_pdf_graphics_evaluate(
    const quantapdf_pdf_content_ir *source,
    const quantapdf_pdf_content_limits *limits,
    quantapdf_pdf_graphics_ir **out_graphics);
void quantapdf_pdf_graphics_dispose(quantapdf_pdf_graphics_ir *graphics);
#ifdef __cplusplus
}
#endif
#endif
