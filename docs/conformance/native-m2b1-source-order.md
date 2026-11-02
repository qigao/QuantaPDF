# M2b.1 — one original ordered Text/Fill/Image compiled page

Tracks [#582](https://github.com/qigao/QuantaPDF/issues/582), parent [#580](https://github.com/qigao/QuantaPDF/issues/580). Starts from M2a accepted production master `affc4132da73c574fd4f17da313c1a337cbde5bc`, independent #2309 SUCCESS.

## Restricted new qualification (Draft until CI)

Two new, genuinely original QPDF 128×64 pages:
- mode 66: black embedded CID TTF **Text → green rectangle Fill → RAW RGB Image**
- mode 67: **RAW RGB Image → green rectangle Fill → black embedded CID TTF Text**

The one existing Source V10/TTF interpreter sends completed original Text, opaque Fill and decoded Image to a single source-order staging graph, retaining original q/Q and paint colors; no second parser. Fully validated EOF materializes one owned three-operation IR (exactly one TTF glyph show, one closed rectangle, one Image sample record). No borrowed font/QPDF pointer escapes.

The production C11 M2a preparation and output remain untouched for two-paint pages. A distinct private `M2B1_TEXT_FILL_IMAGE` route borrows the same unique Source Context lease and compiles the three operations into one existing V5 C11 page; PATH uses its **existing** V1 geometry compiler with projected coordinates, Text retains the exact black glyph coverage conversion, and Image remains the existing RAW sample renderer. All three share the original device grid, V5 source-over painter and page-level sample/edge/pixel/byte ledger. No second renderer or full-page PDF fallback exists.

## Independent pixel oracle

In the existing native public render executable, use the **pre-M2a reviewed Roboto-hello RGBA reference** for exact TTF black coverage, independently calculate the original green rectangle coverage at every 4×4 device subpixel and the existing 2×2 Image footprint/center-nearest RGB, then apply the three operations in original authored order. Compare **every** RGB output pixel at 72, 96 and 144 DPI; never generate new goldens from the implementation.

## Still open

- [ ] Candidate exact-head full 61/61 or higher Linux Release/ASan-UBSan/Windows/macOS native CI and installed consumers 1/1; Windows exact export/no-PDFium audit.
- [ ] New route's actual edge/work/byte N/N−1 and real 3-layer C11 allocation-fault/Context recovery tests, including later failure and post-IR teardown.
- [ ] Extended negative original PDF matrix: extra Text/Fill/Image, late invalid Do/W/Stroke, unqualified Text colors/graphics states/font identities and nonidentity matrices, never partial Bitmap.
- [ ] Independent master-push CI after any eventual qualified merge.
- [ ] Later M2b.2 correlated 4×4 glyph∩Clip and wider fonts/colors remain OPEN. No new public ABI, CI/workflow/test project or PDFium.

This is a Draft capability, NOT a completed general Text/Image/Fill/Clip renderer.

## Full original-source refusal matrix (candidate)

Existing test-only author now supplies 13 additional original PDFs, modes 68–80, with a complete good Text/Fill/Image or Image/Fill/Text prefix before an unsupported late Do, extra Fill, W Clip, Stroke, unused Font/XObject/ExtGState, sheared original CTM, malformed q/Q/ET, second Do or blue text run. The **existing** Source/OOM target requires every one to refuse complete IR atomically; seven representative originals also traverse the public Bitmap API and must return no handle. Neither checks a fake IR/renderer, nor changes the M1/M2a golden corpus. This is an unaccepted candidate until exact-head CI passes.
