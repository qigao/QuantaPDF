#ifndef QUANTAPDF_QPDF_NATIVE_IMAGE_IR_H
#define QUANTAPDF_QPDF_NATIVE_IMAGE_IR_H

#include "ir/ir.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private, tightly bounded source Image XObject admission. Consumes an
 * original PDF as bytes, parses EVERY content operator, and publishes an
 * entirely owned one-page Composer IMAGE IR only on full success.
 * Neither PDFium nor any raster provider is consulted.
 *
 * A name's repeated Do invocation counts separately toward source/draw
 * budgets. Supported page profile: [0,0,W,H] visible page, quarter rotation
 * zero, UserUnit=1, no page group/annotations; balanced q/Q; axis-aligned
 * positive integer placement cm; named /Image Do; 8-bit DeviceGray/DeviceRGB
 * tightly decoded RAW streams, at most 4 draws. Unsupported operations or
 * graphics state return UNSUPPORTED, malformed sources return FORMAT.
 * Caller disposes successful IR with quantapdf_ir_dispose. */
typedef struct quantapdf_qpdf_source_raw_image_limits {
    size_t max_pdf_bytes;
    size_t max_content_bytes;
    size_t max_content_streams;
    size_t max_operations;
    size_t max_page_pixels;
    size_t max_source_pixels;
    size_t max_drawn_pixels;
    /* Maximum inherited PageTree nodes inspected for /Resources, including
     * the selected leaf. Independent of decoder/stream and image budgets. */
    size_t max_parent_steps;
} quantapdf_qpdf_source_raw_image_limits;

quantapdf_status quantapdf_qpdf_source_raw_images_to_ir(
    const unsigned char *original_pdf, size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    quantapdf_ir *out_ir);

/* Private original-source mixed mode: one opaque filled m/l/h/re PATH and
 * one strictly referenced RAW Image Do, in either source order. Reuses the
 * Image-only parser's bounded q/Q, cm, page/resources/decoder admission;
 * no unsupported page paint can be silently omitted. Path values are
 * resolved into integer top-left pixel geometry under the current CTM;
 * only exact 0/1 RGB channels and positive axis-aligned transforms are
 * admitted initially. All source/qpdf owners are discarded before the
 * caller passes the returned uniquely-owned Composer IR to the pure C11
 * mixed Path/Image raster. This is NOT a public page renderer or API. */
quantapdf_status quantapdf_qpdf_source_mixed_path_image_to_ir(
    const unsigned char *original_pdf, size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    quantapdf_ir *out_ir);

#ifdef __cplusplus
}
#endif
#endif
