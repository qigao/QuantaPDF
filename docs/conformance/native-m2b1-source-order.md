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

## Independent bounded public error CTest (candidate)

The seven representative original public API refusal cases now run in a **separate 45-second invocation of the exact existing `quantapdf_native_public_render` executable**, via `--m2b1-negatives`. The unchanged 45-second existing public whole-image golden CTest retains its M1/M2a and newly admitted M2b.1 original pixel cases. All thirteen original negative PDFs still run in the existing Source/OOM invocation under its unchanged 40s deadline. This is a real product conformance split, not a probe CI or installer/consumer project; **native CTest count becomes 62** upon acceptance.

## Original Flate XObject full-image parity (candidate)

Modes 81/82 are two additional original QPDF-authored PDFs with `/FlateDecode` XObject data (same 2×2 DeviceRGB decoded sample pixels), plus the genuine embedded CID FontFile2 and opaque green rectangular Fill. Test-only zlib encodes the original image; QPDF preserves its Filter and the existing Source decoder materializes one owned RAW image without re-parsing the PDF. One standalone `quantapdf.native_m2b1_flate` CTest *reuses the same existing public executable* and independently compares every output RGB pixel against the prior Roboto reference plus analytic 4×4 Fill/Image coverage at 72, 96 and 144 DPI in both authored directions. Its separate 45s budget avoids expanding the legacy whole-corpus public render CTest. Source/OOM also requires the 3-op decoded IR and shared C11 compiled page for both original Flate orders. This is a pending exact-head candidate until CI passes.

## Original RAW 3-paint pixel CI isolation (new exact-head candidate)

Exact [Native CI #2317](https://github.com/qigao/QuantaPDF/actions/runs/41419387706) accepted **63/63 per native configuration, installed consumers 1/1**, but the Linux ASan `quantapdf.native_public_render` elapsed **44.02 of its original 45-second ceiling**. To prevent the earlier postmerge M2a timeout regression, schedule modes 66 and 67 in a separate named `quantapdf.native_m2b1_raw` CTest invocation of the SAME native public binary, with the SAME 45-second limit. Modes 49/50/64/65 remain in the original public corpus; modes 81/82 remain in the independent Flate test. All EIGHT original pages continue to use the identical independently derived full RGB oracle at 72/96/144 DPI. Existing M1/M2a bytes, sanitizer, ABI, Source/OOM and all failure cases stay unchanged. Native CTest count should become **64** on the next accepted HEAD. No new executable, CI workflow, installer or consumer project.
