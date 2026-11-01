# M2a — opaque, ordered original Text/Image Source qualification

Tracking: [Issue #578](https://github.com/qigao/QuantaPDF/issues/578), [parent #570](https://github.com/qigao/QuantaPDF/issues/570) and [broader #208](https://github.com/qigao/QuantaPDF/issues/208).

**Status: Draft implementation and conformance.** The first mixed renderer and independent full-image reference passed [Native CI #2298](https://github.com/qigao/QuantaPDF/actions/runs/41304403199), exact HEAD `c2ce69e5db3e7b01ed405eab21ccddf19c45a141` (Linux Release/ASan-UBSan, Windows and macOS, 60/60 and installed consumers 1/1). Later budget/OOM and source-resource-closure work has its own exact HEAD CI and is **not automatically accepted by #2298**. Keep #579 Draft; M2 as a whole is not complete.

## M1 immutable baseline

Merged [#577](https://github.com/qigao/QuantaPDF/pull/577): master `cfb93d382b97b6ee1198f7990fc4b860f2958513` (tree `7b5bbb1df0098cf061b2f5e9b72beba50d3782cc`), independent master-push CI #2286 SUCCESS 3/3. Preserve all previous original PDF/reference pixels, Source V8/V9 and qualified V10 profiles, exact four historical raster malloc calls/order, numerical quotas, public ABI, existing Core Context lifetime, synchronous Composer boundary and existing build/workflows.

## Current bounded first M2a capability

Two actual, independently QPDF-authored original PDFs share a **genuine embedded CID TTF (Roboto) and 2×2 RAW RGB Image XObject** with overlapping ink. Both orderings are required:

| Fixture | Original content order | Evidence |
| --- | --- | --- |
| `native_ttf_fixture(49)` | Text show → Image Do | Complete Source IR, common C11 compiled page and public RGB |
| `native_ttf_fixture(50)` | Image Do → Text show | Complete Source IR, common C11 compiled page and public RGB |
| `native_ttf_fixture(51)` | Text → Image with an unused original ExtGState | Atomic public UNSUPPORTED; no hidden resource dropped |

This first capability supports **one opaque black embedded-TTF Text show** and **one decoded, opaque, qualified RAW/Flate Gray/RGB image** on the existing exact 72/96/144-DPI physical grid, with the Source-admitted CTM bounds, real q/Q and no Clip/Stroke/Fill paint. Text other than this explicitly admitted black run, multiple fonts, additional paints, general mixed clipping, arbitrary transforms/gradients, Form, alpha, masks, groups and public Composer rendering are **not** qualified; refuse atomically. More colors/font resources and Clip/Fill are subsequent M2 subgates, not implied by these tests.

## One interpreter, one compiler, one page-wide ledger

1. `qpdf_native_image_ir.cpp` and the **existing** `qpdf_source_text.cpp` run through the same original PDF content parse. The text callback freezes show order and forwards the sole qualified Image Do to the already existing Source image decoder. Whole-source EOF is required before the unique owned IR copies glyph commands and decoded sample bytes; no new font provider, QPDF parse or alternate raster.
2. M2a's original `/Resources` is strictly the single referenced `/Font` and single referenced `/XObject`. Extra original resources, even unused declarations, are not silently lost. The existing private IR resource closure additionally refuses unconsumed/malformed records.
3. `QP_NATIVE_PREPARED_M2A_TEXT_IMAGE` reuses one physical device plan, one Source Context lease, one compiled V5 page owner, **one** checked Source/drawn-pixel, sample/edge and requested-byte ledger. The Text outline geometry and RAW Image are its two ordered layers, not independent PDF renderers. V5's existing source-over compositor determines the actual final RGB. The initially detected 1-LSB transparent-vs-opaque Text rounding mismatch was fixed by converting already sampled exact black-on-white glyph pixels **in place** to black/coverage alpha; no new full-page malloc or separate sampler is added.
4. Successful RGBA/Bitmap publication detaches its bytes. Every failure clears output/receipt; generated geometry/IR is released before dropping the retained Core Context. Compiled page replay never resets work/placement budget. Existing Text-only, Image-only, PATH/Clip/TeXample pixels and failure allocation sequence are untouched.

## Qualification matrix

- [x] Both actual original Source orders survive complete parser/owned IR and use the same C11 native V5 compiled page.
- [x] Independent full-image RGB references for **both orders, all 72/96/144 DPI**. The oracle is the **previously reviewed** original Text-only RGBA reference plus independently evaluated 4×4 Image footprint, pixel-center nearest-neighbor source samples and opaque source-over in source order. Every RGB pixel is compared exactly, including overlapping regions, never with M2a's output as a new reference. Accepted at #2298.
- [ ] Candidate exact-HEAD **M2a Source C-allocation fault** controls; four actual TTF geometry/mask/raster malloc failures and same-Context recovery.
- [ ] M2a sample/edge/pixel, original/expanded operation, unique source/placement pixels and requested-byte N/N−1 quotas; output receipt and retained-owner balances.
- [ ] Public original unused-/Resources fail-closed negative, plus late unsupported/malformed/unqualified Text/Image/Clip/PATH matrix and full recovery.
- [ ] Every remaining new Image/final-composite allocation failure and mixed-page lifetime/rollback under sanitizer.
- [ ] Current candidate's own Linux Release/ASan-UBSan, Windows and macOS Native CI; installed consumers and exact exports, no PDFium. Existing Source/OOM 40-second gate and Context/OOM 30-second gate remain.
- [ ] Only after candidate acceptance, consider merging #579 and independently rerun actual production master CI; even then **M2b Text/Image/Fill/Clip is still open**.

No new test project, installer/consumer project, probe workflow/CMake, public ABI/provider or PDFium dependency has been introduced. Do not update the accepted original corpus count or label M2 complete until separately demonstrated.
