# #269 — 96 DPI edge coverage A/B: fit-grid vs nominal DPI

**Draft / DO NOT MERGE, test only, no renderer changes.**

Parent Draft #349 measured **four test-generated original** /UserUnit=7,
rotated CropBox Fill/W Clip PDFs across 72/96/144 DPI.
On macOS at its first qualified HEAD, 72 and 144 DPI were exactly
RGB-identical, but 96 DPI produced aggregate RGB absolute error 153,712
on 680 pixels (170 per rotation); original dimensions still matched.
This is *diagnostic*, not a compatible 96DPI native pixel positive.

The source Clip rectangle [3,9]×[4,10] and red Fill rectangle
[4,10]×[5,9] intersect to an analytically bounded source-space red
rectangle **[4,9]×[5,9]**. Since the content CTM is identity and paint
is opaque normal, ideal pixel area coverage can be calculated without
changing the actual C11 raster or invoking a fallback.

## Measured alternatives

For each page, reconstruct the red rectangle from the already verified
Graph source-to-device grid affine. Test two mutually exclusive
**analytic diagnostic models** in the existing original PDFium comparison:

1. **Bitmap-fitted rectangle area:** project the original rectangle
   through `grid.from_source`, which maps the full unrounded displayed
   page to the integer image extents (94×75 at 96 DPI). Compute exact
   per-pixel rectangle overlap against unit pixel squares, composite
   red over white, and measure total RGB distance to original PDFium.
2. **Nominal-DPI rectangle area:** scale those same four projected
   rectangle edges by the ratio of requested 96/72 nominal scale to
   the independently fitted width/visible-page-width (and corresponding
   height ratio). Repeat exact pixel-overlap comparison against PDFium.
   This represents the previous nominal-DPI phase hypothesis, not
   authorized rendering geometry.

Both alternatives are purely numerical A/B **observations**. They
neither allocate an alternate bitmap nor replace the original 4×4
native raster output. A third diagnostic counts raster↔PDFium mismatches
that fall outside a 1.5-pixel band around the **fitted** source
rectangle edges, identifying whether error extends into interior or
blank background.

The strict 72DPI zero-RGB gate and original 12-frame, 36-zero-output
matrix remain unchanged. JSON now records for all 12 frames
`fitted_area_abs_rgb`, `nominal_area_abs_rgb`, and
`off_ideal_edge_differences`, plus aggregate 96DPI totals. CI
checks the totals against all frame rows but does **not** require a
particular candidate to win or declare exact 96DPI parity.

Only data obtained from the original PDFium bitmap can determine
the next correct model; a lower error is not a source/provenance
admission. The test-generated sources are **not** independent
publisher originals; #343 original Userunit.pdf attachment bytes
and rights are still unverified. No external positive increment,
ABI change, SDK switch, automatic fallback, or release. Keep
#269/#343 OPEN, all PRs Draft / DO NOT MERGE, public SDK PDFium.
