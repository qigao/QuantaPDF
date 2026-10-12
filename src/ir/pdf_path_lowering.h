#ifndef QUANTAPDF_PDF_PATH_LOWERING_H
#define QUANTAPDF_PDF_PATH_LOWERING_H

#include "ir.h"
#include "document/page_content_ir.h"

/* Private, transactional projection from *already admitted* PDF path semantics
 * into the existing Composer IR. Coordinates supplied by the caller are in
 * canonical PDF page user space: zero-origin, unit scale, no page rotation or
 * crop offset. This function does not inspect /MediaBox or /UserUnit.
 *
 * Only values that round-trip through the Composer writer's float + four
 * decimal formatting and RGB8 encoding are admitted. W/W* clip definitions
 * become owned Clip Path / Clip Intersection resources, preserving q/Q and
 * delayed activation. A standalone n without clipping, unrepresentable
 * geometry, unsupported paint or budget exhaustion fails atomically.
 */
typedef struct quantapdf_pdf_path_lower_limits {
    size_t max_painted_paths;
    size_t max_path_commands;
} quantapdf_pdf_path_lower_limits;

#ifdef __cplusplus
extern "C" {
#endif

quantapdf_status quantapdf_pdf_path_lower_to_composer(
    const quantapdf_pdf_page_semantics *page,
    double canvas_width, double canvas_height,
    const quantapdf_pdf_path_lower_limits *limits,
    quantapdf_ir *out_ir);


/* Private source-page mapping version. The page_transform is a row-vector
 * affine matrix applied AFTER the PDF content's own graphics CTM. This
 * preserves nonzero source origins, crop translation, right-angle rotation,
 * and /UserUnit scaling for values that Composer can represent. It does not
 * change strict numeric admission or ownership. A clip chain may contain at
 * most QUANTAPDF_COMPOSER_MAX_CLIP_COMPONENTS path definitions. */
quantapdf_status quantapdf_pdf_path_lower_to_composer_mapped(
    const quantapdf_pdf_page_semantics *page,
    double canvas_width, double canvas_height,
    const double page_transform[6],
    const quantapdf_pdf_path_lower_limits *limits,
    quantapdf_ir *out_ir);

#ifdef __cplusplus
}
#endif
#endif
