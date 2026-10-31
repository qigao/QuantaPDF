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
 * budgets. Existing source signatures require quarter rotation zero;
 * public V10 alone admits normalized page-tree /Rotate 0/90/180/270,
 * leaf-only /UserUnit absent/null = 1 or exact quarter factor 0.25..4,
 * quarter-grid original Media/Crop intersection, no page group/annotations,
 * balanced q/Q and axis-aligned
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

/* Opt-in Source V7: the private V6 bounded 2..4 scoped Path/Image mode,
 * with finite [0,1] normalized /DeviceRGB nonstroking "rg" channels
 * quantized to opaque 8-bit components via floor(channel*255 + 0.5).
 * No implicit clamping, inherited DefaultRGB override, transparency,
 * extra Clip, larger layer count or new C11 raster backend. V6 and all
 * earlier original-source modes continue requiring exact RGB 0/1. */
quantapdf_status quantapdf_qpdf_source_scoped_mixed_v7_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir);

/* Private Source Scoped Mixed V8: retain V7's finite normalized rg and
 * bounded 2..4 opaque Path/Image + one W/W* + actual Q-restored paint,
 * while admitting finite nonstroking DeviceGray "g" in [0,1].
 * Deterministically quantize to RGB8 via floor(channel*255+0.5).
 * Q restores the inherited fill state; V1–V7 still reject g, while
 * V7 and V8 must paint their common rg source byte-identically.
 * This does not introduce a public ABI or change the C11 Raster. */
quantapdf_status quantapdf_qpdf_source_scoped_mixed_v8_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,quantapdf_ir *out_ir);

/* Private Source V9: the existing strict V8 scoped mixed Path+Image
 * profile (2..4 paints, 1..2 of each, exactly one frozen W/W* Clip and
 * a real Q restore) with independent opt-in interpretation of an Image
 * XObject's explicit /Decode. For only 8-bit DeviceGray or DeviceRGB,
 * admit one finite [0 1] (identity) or [1 0] (inversion) per channel,
 * exactly 2*components values. Normalize to canonical owned RAW bytes
 * before the unchanged pure-C11 Raster consumes them.
 *
 * All earlier V1–V8 entry points still reject explicit /Decode.
 * Reject arbitrary numeric ramps, alternate/ICC/CMYK, masks, filters,
 * unsupported groups, and malformed Decode arrays atomically. This is
 * NOT a public API, backend switch, or increased four-layer Raster cap. */
quantapdf_status quantapdf_qpdf_source_scoped_mixed_v9_image_decode_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    quantapdf_ir *out_ir);

/* Private Source V10: keep the V9 strict 2..4 Path/Image/Clip/Q profile
 * and independently opt in to /Filter /FlateDecode with one dictionary
 * /DecodeParms: explicit integer Predictor 1/2/10..15, Colors matching
 * 8-bit DeviceGray or DeviceRGB, Columns matching image Width, and
 * BitsPerComponent exactly 8. No extra keys/filter chains. QPDF's
 * existing TIFF/PNG predictor decoder is invoked only after metadata
 * and work-bounded preflight; older V1–V9 signatures still refuse
 * /DecodeParms. Private only, not a public renderer or new ABI. */
quantapdf_status quantapdf_qpdf_source_scoped_mixed_v10_flate_predictor_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    quantapdf_ir *out_ir);

/* V10-only strict original PDF page-index selection. The selected Page
 * and all of its inherited state must be validated under bounded PageTree
 * work. This publishes a local one-page IR using page slot zero solely
 * for canonical C11 raster admission, not as a substituted PDF leaf.
 * Earlier Source entry points retain exact page-zero semantics.
 *
 * The native V10 Clip profile admits one or two independent leaves,
 * each with 4..9 total commands and one aggregate W/W* winding rule.
 * A leaf may contain one or two contours, each with MOVE, at least two
 * LINE/CUBIC edges, and CLOSE. The first contour must close explicitly
 * before a second m/re; the last may close implicitly at W/n. Two
 * ordinary rectangles require ten commands and remain unsupported.
 * Painted PATHs and paths ended by plain n retain their existing
 * grammar. The first definition may follow a bounded GS0 paint prefix;
 * the second may follow first-leaf paints or real Q restoration to GS0.
 * Empty, MOVE-only and open MOVE+LINE operands use the same four-command
 * zero-edge encoding in either position. Both definition-time paths and
 * winding rules are retained: owned Clip IDs 1/2 and, for intersecting
 * scopes only, owned flat root 3.
 * Original Rotate and UserUnit apply to both leaves. Simple rectangle
 * pairs retain their exact one-leaf reduction only when no earlier
 * paint or saved q can observe the original first leaf.
 *
 * Paint before the second definition and Q restoration to the first
 * leaf use two owned GS records: GS1 names leaf 1; GS2 names root 3.
 * Both must be painted, in order 1* 2+ 1* 0*, within 2..4 total layers.
 * PATH-only admits 2..4 paints, Image-only two, and mixed profiles
 * 1..2 of each kind. A root-only page remaps to the existing one-GS
 * representation. Third definitions, unused intersections and reentry
 * remain unsupported.
 *
 * A nonempty GS0 prefix uses a separate native-only admission with
 * state order 0+ 1+ 0* for one GS or 0+ 1* 2+ 1* 0* for two GS.
 * Each published nonzero identity must be painted. Prefix staging
 * retains the existing homogeneous PATH/IMAGE or one-of-each grammar;
 * total paints remain at most four, Images at most two, and mixed
 * profiles at most two of each kind. A two-GS Image-only prefix cannot
 * fit those limits. A later GS0 suffix still requires real Q; initial
 * GS0 paints neither restore nor discard a Clip. All original owners,
 * paint order, definition CTMs and work quotas are retained.
 *
 * An independent second Clip after Q uses two owned leaves and two GS
 * records mapping GS1 to leaf 1 and GS2 to leaf 2, with no root or
 * rectangle reduction. Complete-page order is 0* 1+ 0* 2+ 0*: both
 * identities must be painted, each GS0 transition requires real Q,
 * and the second scope may stay live at EOF. Prefix staging, total
 * layers, Image/mixed cardinalities and all quotas remain unchanged.
 * Unused sibling leaves and any third Clip definition still refuse.
 *
 * The existing one-GS V10 profile admits 1..4 opaque PATH-only f/f*
 * paints with its Clip graph and no Image resources. Without a prefix,
 * the first paint uses GS=1; real Q may restore GS=0 for a suffix. No
 * Clip reentry or extra layer is admitted by the one-GS profile.
 * Older Source signatures and one-GS mixed/Image limits are unchanged.
 * All payloads publish atomically and are released by
 * quantapdf_ir_dispose, including partial OOM cleanup. */
quantapdf_status quantapdf_qpdf_source_scoped_mixed_v10_selected_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    size_t selected_page_index,quantapdf_ir *out_ir);

/* Internal original TeXample M3 Source-only admission. This uses
 * exactly the SAME V10 QPDF parser as the public selected reader,
 * but is independently opted in only by this private Source test.
 * It can publish 3 owned ordered PATH operations (f*, S, S), one
 * frozen two-ring W* leaf and a single opaque Normal GS, only after
 * validating all 24 m, 32 c, 8 h and actual q/Q operator positions.
 * Not a public renderer, installed SDK ABI or second parser. */
quantapdf_status quantapdf_qpdf_source_texample_m3_stage_to_ir(
    const unsigned char *original_pdf,size_t size,
    const quantapdf_qpdf_source_raw_image_limits *limits,
    quantapdf_ir *out_ir);

#ifdef __cplusplus
}
#endif
#endif
