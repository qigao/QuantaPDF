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
 * decimal formatting and RGB8 encoding are admitted. Any clipping, malformed
 * path, unpainted path, lossy value or unsupported paint returns UNSUPPORTED
 * or FORMAT and leaves out_ir entirely zeroed. No implicit approximation.
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

#ifdef __cplusplus
}
#endif
#endif
