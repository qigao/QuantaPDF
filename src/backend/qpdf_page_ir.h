#ifndef QUANTAPDF_QPDF_PAGE_IR_H
#define QUANTAPDF_QPDF_PAGE_IR_H
#include "ir/ir.h"
#include "backend/qpdf_document.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Page work budgets; the already opened Document owns file parsing/authentication.
 * These limits describe actual content/IR work, never a source API version. */
typedef struct quantapdf_qpdf_page_limits {
    size_t max_content_bytes;
    size_t max_content_streams;
    size_t max_operations;
    size_t max_page_pixels;
    size_t max_source_pixels;
    size_t max_drawn_pixels;
    size_t max_parent_steps;
} quantapdf_qpdf_page_limits;
/* Borrow the selected opened/decrypted page synchronously. Success publishes
 * independently owned IR, including Clip commands, decoded images and font
 * geometry. Clip definitions retain all parsed contours and winding rules;
 * command storage is dynamic and raster work uses the caller's actual budgets.
 * Ordinary Fill preserves closed-path current points and zero-area subpaths;
 * empty f/f* has no retained paint. Endpoints and controls may cross the
 * viewport within the finite C11 coordinate envelope. All ordinary path
 * endpoints and controls use it. Text forwards operands to the same page owner
 * and does not copy Fill prefixes or impose a glyph-command quota on Fill.
 * W/W* definitions use that owner before or after Text; each definition owns
 * its path and parent identity, and each paint keeps its actual current Clip.
 * Publication prunes unobserved definitions and owns the resulting graph.
 * C11 Scene currently retains its four-leaf graph limit independently.
 * Caller disposes it with quantapdf_ir_dispose. Failure publishes empty IR. */
quantapdf_status quantapdf_qpdf_page_to_ir(
    quantapdf_qpdf_document *document,size_t page_index,
    const quantapdf_qpdf_page_limits *limits,quantapdf_ir *out_ir);
#ifdef __cplusplus
}
#endif
#endif
