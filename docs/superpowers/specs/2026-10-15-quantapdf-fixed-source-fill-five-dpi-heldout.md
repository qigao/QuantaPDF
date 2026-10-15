# QuantaPDF #269 — fixed-source Fill-only scale/DPI held-out matrix

**Isolated Draft / DO NOT MERGE. No production Raster, Graph, ABI, public SDK or publishing change.**

Parent [Draft #357](https://github.com/qigao/QuantaPDF/pull/357), exact head `792c459f2809522bc1aac5a4ef77a913ffe69d15`, completed [CI #38747956044](https://github.com/qigao/QuantaPDF/actions/runs/38747956044) successfully across Linux Release, Linux ASan/UBSan, macOS and Windows. The four independently archived JSONs agree on 48 test-generated Fill-only originals with three held-out source-space extents, four intrinsic rotations and four CropBox variants. PDFium produced 48 binary red rectangles, but none of five prespecified fitted/nominal integer bounding models generalizes to all originals (best 24/48 exact, while nominal outward has the smallest total four-edge error of 42).

## A test of the scale hypothesis, not another snap rule

Keep each **genuine test-generated original PDF byte stream fixed**, including original Fill commands, page MediaBox, CropBox, leaf /UserUnit7 and page /Rotate. Change **only** the PDFium render options DPI. Every source is opened once through bounded Source→Graph page geometry, closes its source owner and drops the Graph before PDFium. It is then independently opened by PDFium and rendered twice at each requested DPI, checking pixel byte-identical repeat, and its source PDF bytes are compared before and after all five renders.

This child uses **24 original sources**, formed by three Fill-only source rectangles, two explicit CropBox variants (baseline and XY+0.375 points), and four intrinsic rotations:

- 6×4 source Fill: `1 0 0 rg 4 5 6 4 re f`
- 5×4 held-out source Fill: `1 0 0 rg 4 5 5 4 re f`
- Fractional 5.625×3.375 source Fill: `1 0 0 rg 4.125 5.125 5.625 3.375 re f`

The same source PDF is evaluated at **72, 96, 120, 144, and 168 DPI**, producing 24×5 = **120 bounded original-PDFium images** totaling **1,419,168 RGB pixels**. All dimensions are independently predicted from the existing Graph device grid before accepting a reference bitmap; the maximum single image remains only 164×131 (21,484 pixels). For 90°/270° the axes swap. No alternate raster or large allocation is used.

| DPI | Expected unrotated width×height | Source image pixels |
|---|---:|---:|
| 72 | 70×56 | 3,920 |
| 96 | 94×75 | 7,050 |
| 120 (held-out) | 117×94 | 10,998 |
| 144 | 140×112 | 15,680 |
| 168 (held-out) | 164×131 | 21,484 |

## Numerically compared alternative hypotheses

The original source-space rectangle's four corners are independently transformed through (A) the qualified bitmap-fitted Source→Device grid and (B) the existing Graph Source→visible-page affine at nominal DPI/72, with the necessary PDF Y-to-device Y inversion. Only values already within 1e-9 of an integer are stabilized against a one-ULP decimal parser result; nothing silently changes original geometry.

Five **test-only integer bbox candidates**, copied from #357, are measured against original PDFium opaque red bounding boxes at each DPI:

1. Fitted `floor(min), ceil(max)`.
2. Fitted `floor(min), floor(max)`.
3. Fitted `floor(origin)` with `ceil(unrounded span)`.
4. Fitted nearest integer origin with `ceil(unrounded span)`.
5. Nominal-DPI `floor(min), ceil(max)`.

Each original PDFium frame is scanned for exactly opaque red pixels, white background pixels, partially covered green pixels, and any non-red/white RGB. Record the actual observed red bbox and whether it is a complete **binary** red rectangle; do **not** assume every DPI has no antialiasing. The five candidate bboxes are compared to the observed original red bbox, reporting exact matches and summed four-edge absolute integer error **separately per DPI**. No candidate is privileged for acceptance.

CTest must validate the 24 immutable source checks, 120 bitmap frames, source-byte and per-DPI bitmap repeat identity, explicit dimension/pixel limits, content/rotation/crop identity, all five observed per-frame model values, and independent per-DPI aggregate reconciliation. Linux Release, Linux ASan/UBSan, Windows, and macOS archive original JSONs; a missing, partial or stale artifact fails.

Interpretation: If an origin/extent rule fits some DPIs but fails at held-out DPIs for the **same source PDF bytes**, this distinguishes scale-dependent integer quantization from a single fixed source-space vertex snapping contract. Exact bounds on test-generated rectangles still do not prove correct PDFium edge antialiasing or general painted Path/Clip rendering compatibility. **This is an observation, not permission to rewrite the already qualified Graph→Composer affine or private C11 Q4 Raster.**

No license or external corpus promotion: all 24 PDF sources are QPDF test-generated. The independently posted original Userunit.pdf under #343 still has unverified raw bytes/SHA-256 and document-specific redistribution rights. Keep #269 and #343 OPEN, every stacked PR Draft / DO NOT MERGE, formal external-native pixel positives unchanged, installed/public SDK PDFium and #208 native cutover blocked.
