# #269 — Real published CropBox/Rotate original, 72/96/144 DPI device qualification

Private test-only child of [Draft #342](https://github.com/qigao/QuantaPDF/pull/342), exact HEAD
`8101e66b438c17e84bcc76b3a3601e50dc431588`, with
[full CI #38715646398](https://github.com/qigao/QuantaPDF/actions/runs/38715646398)
SUCCESS on Linux Release + ASan/UBSan, macOS, Windows.

## Source and limits

The original **unaltered independently published** py-pdf/PyMuPDF
`027-cropped-rotated-scaled.pdf` is a quarantined source with SHA-256
`cc195eac510b81123de4f55ac9f8185f2120975a7b75a106460b98615737aacd`
and four nonzero CropBoxes under /Rotate 0/90/180/270. The original
has **no /UserUnit** (default 1) and contains images, transparency, text and
annotations; its rights review [#336](https://github.com/qigao/QuantaPDF/issues/336)
and official external corpus admission remain pending.

The existing sealed Graph-only geometry test uses explicit larger, still
bounded TEST-ONLY resource limits (the separate #340/#342 experiments have
already narrowed the actual node cap cause to >10k nodes). It never
uses the private Raster, PDFium page-pixel oracle or a rewritten PDF.

## Exact device raster geometry, but NO native raster allocation

The C++ source observer now tests all 4 actual original selected pages at
**DPI 72, 96 and 144**: 12 page-mode combinations, with independent
immutable pinning of expected integer bitmap sizes, RGB8 stride and total
bytes. The following predictions account for the exact integer
`ceil(visible_points × DPI/72)` dimension rule and user-space quarter-turn
rotation:

| Original | 72 DPI | 96 DPI | 144 DPI |
|---|---|---|---|
| page 0 / Rotate 0 | 384×504 | 512×672 | 768×1008 |
| page 1 / Rotate 90 | 378×288 | 504×384 | 756×576 |
| page 2 / Rotate 180 | 352×294 | **470×392** | 704×588 |
| page 3 / Rotate 270 | 546×352 | **728×470** | 1092×704 |

For each of the 12 real source modes the test verifies:

- all 4 original CropBox corners project into exactly the zero-origin
  integer device rectangle, with no double page-affine application;
- both independent axis scales use the actual device bitmap width/height
  rather than nominal DPI;
- 4 corner pixels × 16 Q4 subpixel centers = **64 forward/inverse exact
  source-coordinate roundtrips**, 768 across all modes;
- strict deterministic repeat of the detached device grid;
- exact upper/lower explicit Q4 work budgets;
- **fail-closed zero output** when pixels or RGB8 output bytes are capped
  one below the actual requirement (24 negative cases);
- `DPI 120` remains unsupported and also resets output (4 negatives).

The separate test-only CMake gate requires the exact 12-row JSON ledger,
all 768 roundtrip probes and 12 per-mode limited-memory-rejection markers.
Missing/altered source SHA-256 is already checked before and after the C++
Graph observer; CI archives the same geometry JSON for Linux Release,
Linux ASan/UBSan, macOS and Windows.

## Exclusions

Even successful tests are **only geometry and resource contracts**:
`original_pdfium_pixel_oracle=not_run`, `native_raster=not_run`, and
`external_native_positive_increment=0`. No general XObject, SMask,
Text, annotation, W*, Stroke, arbitrary CTM or public SDK change.
An independently published and properly licensed **nonunit /UserUnit**
plus supported actual Path/Clip pixel source remains missing; [#343](https://github.com/qigao/QuantaPDF/issues/343)
tracks a separate reporter-uploaded oversized /UserUnit=7 discovery which
is not yet byte-pinned or licensed.

Keep #269 OPEN, all stacked branches Draft / DO NOT MERGE,
installed/public SDK PDFium and #208 backend cutover blocked.
