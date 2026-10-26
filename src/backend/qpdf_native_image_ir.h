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

/* Strict original-source V2 extension: EXACTLY one opaque PATH and TWO
 * separately owned 8-bit Gray/RGB Image XObject Do paints, in any of the
 * three source-order positions. Balanced q/Q and PageTree/decoder budgets
 * are unchanged; every other operator is rejected, not skipped. All three
 * image/path operations and two owned RAW resources must be published
 * atomically; source/QPDF owners never escape the function. V1 above stays
 * limited to one PATH and one IMAGE. */
quantapdf_status quantapdf_qpdf_source_mixed_path_two_images_to_ir_v2(
    const unsigned char *original_pdf, size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    quantapdf_ir *out_ir);

/* Strict original-source V3: EXACTLY two opaque PATH paints and two unique
 * RAW Gray/RGB Image XObject Do paints in any of the six possible source
 * interleavings. Each PATH owns its own geometry/color/winding and consumes
 * a separate bounded raster share; both images own their decoded pixels.
 * The previous V1/V2 signatures and admission stay unchanged.
 * No external raster fallback or public PDFium rendering cutover. */
quantapdf_status quantapdf_qpdf_source_mixed_two_paths_two_images_to_ir_v3(
    const unsigned char *original_pdf, size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    quantapdf_ir *out_ir);

/* Strict opt-in original PDF source Clip+Image V1. Parse every decoded
 * Content Stream operator. Admit exactly one W or W* leaf Path, terminated
 * by n, and one named 8-bit Gray/RGB Image Do within that live q/Q Clip.
 * CTM is frozen at Clip path construction; supported image placement is
 * independently checked. No source, Graph or qpdf owner escapes.
 *
 * Returns one owned IMAGE operation, one RAW Image, one immutable Graphics
 * State referencing exactly one Clip PATH leaf. All unsupported source
 * text/Path painting/Color/Group/Form/extra Image operators or malformed
 * scopes cause zero published output; no PDFium fallback. Consume the IR
 * with quantapdf_ir_raster_clipped_raw_image_v1(), not the public render API. */
quantapdf_status quantapdf_qpdf_source_clipped_raw_image_to_ir_v1(
    const unsigned char *original_pdf, size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    quantapdf_ir *out_ir);

/* Opt-in Source V2: exactly two Image Do paints in order, under the
 * SAME live W/W* Clip scope. The once-defined Clip geometry and rule are
 * frozen, while each Image has its own checked CTM, placement, and owned
 * RAW resource. No partial IR for late/unknown operations, missing GS or
 * a popped Clip. The original single-Do API above stays unchanged.
 * Consume with quantapdf_ir_raster_two_clipped_raw_images_v2(). */
quantapdf_status quantapdf_qpdf_source_two_clipped_raw_images_to_ir_v2(
    const unsigned char *original_pdf, size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    quantapdf_ir *out_ir);

/* Strict original-source Clip restoration V3:
 * Exactly one frozen W/W* Clip Path with a first Image Do while active,
 * then a REAL Q pop of that Clip, then a second Image Do which MUST have
 * no inherited Clip/Graphics State. Complete q/Q and all page operators
 * are validated; missing Q, early Q, extra Do, masked/invalid resources,
 * late unqualified painting and budgets reject atomically. Both images
 * own their decoded RAW Gray/RGB bytes; qpdf/source owners are released
 * before the C11 painter. Consume with
 * quantapdf_ir_raster_restored_raw_images_v3(), not the production API.
 * Existing V1/V2 clipped-source signatures remain strictly narrower. */
quantapdf_status quantapdf_qpdf_source_restored_clipped_raw_images_to_ir_v3(
    const unsigned char *original_pdf, size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    quantapdf_ir *out_ir);

/* New opt-in V4: exactly one original W/W* Clip PATH leaf and one
 * Image XObject Do painted while it is active; then a REAL Q restores
 * the prior unclipped state, and an opaque PATH fill paints outside it.
 * Strict complete content admission, inherited resource and source work
 * budgets and owned IR publication are preserved. Neither V3 two-Do nor
 * V1/V2 mixed Path/Image signatures admit this profile implicitly.
 * Consume with quantapdf_ir_raster_clipped_image_then_path_v4(), never
 * with production quantapdf_render_page(). */
quantapdf_status quantapdf_qpdf_source_clipped_image_then_path_to_ir_v4(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir);

/* Source Scoped Mixed V5: exactly one frozen W/W* Clip, one PATH and one
 * Image Do within its q/Q scope, then one PATH and one Image Do after
 * a REAL Q unclip, each pair in either source order. Decode all contents;
 * fail atomically for missing/extra operations, orphan/invalid resource or
 * mismatched Clip/GS scope. Original source/qpdf ownership ends before the
 * detached four-layer IR is returned. Consume only with the private
 * quantapdf_ir_raster_scoped_mixed_v5(), not public PDFium rendering. */
quantapdf_status quantapdf_qpdf_source_scoped_mixed_v5_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir);

/* Opt-in Source Scoped Mixed V6: the same strict, fully decoded original
 * PDF content grammar and ONE frozen W/W* Clip leaf as V5, but with
 * 2..4 paints (at least one PATH and one Image Do), 1..2 of each kind.
 * At least one paint occurs under the frozen Clip; a real Q must restore
 * unclipped GS=0 before at least one later paint. Source ordering is kept
 * exactly in the owned IR. No Clip reentry, Text, Stroke, Form, soft mask,
 * fallback or partial publication. Consume with the EXISTING private C11
 * quantapdf_ir_raster_scoped_mixed_v5() without changing its caps.
 *
 * The previous V5 function remains exactly four paints, one of each
 * kind on either side of Q; older source modes stay narrower. */
quantapdf_status quantapdf_qpdf_source_scoped_mixed_v6_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir);

#ifdef __cplusplus
}
#endif
#endif
