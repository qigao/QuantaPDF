# #269 — private finite 96 DPI integer edge-snap A/B

Parent [Draft #351](https://github.com/qigao/QuantaPDF/pull/351)
qualified exact-head CI #38736065350: four independently generated
original PDFs with offset /CropBox, /Rotate 0/90/180/270 and
leaf /UserUnit 7 produce 680 96-DPI differing boundary pixels between
original PDFium and private C11. Theoretical 4×4 Q4 sample positions
reproduce **every C11 byte** (zero RGB error), while PDFium has zero
partial green coverage pixels: its red/white edges are binary for
*this particular source fixture*. 72/144-DPI images remain exact.

## Five predeclared binary candidates — numeric only

Starting with the **qualified original Source→Graph→Device** affine,
project the same source-space red intersection rectangle [4,9]×[5,9]
to device X-down/Y-down pixel edges. For each boundary, construct
five deterministic, integer-only box candidates:

1. **nearest**: round all four device edges to nearest integer (ties
   toward positive infinity; the measured source does not have ties).
2. **floor**: floor all four edges.
3. **ceil**: ceil all four edges.
4. **outward**: floor lower edges, ceil upper edges.
5. **inward**: ceil lower edges, floor upper edges.

For each of the original 96-DPI pixels, compute a **theoretical
red/white color** for its integer cell under the five boxes and compare
directly to the source PDFium RGB. No new bitmap, alternate backend,
raster algorithm, Source-to-PDF export, or ownership path is involved.
The five measured RGB absolute errors and changed-pixel counts are
recorded per intrinsic rotation and as four-rotation aggregates in
the existing *test-owned* atomic JSON.

The CMake gate must reconcile the 12 frame rows, five candidate totals,
72-DPI exact-RGB baseline, all 36 transactional no-output negatives,
original source byte integrity, and all already qualified #351 numeric
Q4/edge ledgers. **No candidate is pre-selected as the correct PDFium
model**; even an exact result for this fixture would not establish
arbitrary PDF antialiasing compatibility or authorize a renderer
shortcut for external originals.

This candidate cannot be used as a source-PDFium fallback. Before any
private pixel-code experiment, a separate Draft must prove it applies
only to supported opaque axis-aligned rectangle Fill/W Clip under
bounded sizes, no extra content CTM, and must preserve previously
qualified 72/144 RGB parity and zero-output rejection. Production
Raster, Graph→Composer affine, ABI, SDK, corpus and publishing are
unchanged.

#269/#343 remain OPEN and all stacked PRs Draft / DO NOT MERGE.
The independently published reporter Userunit.pdf remains byte/license
unverified; external-native positive increments remain zero.
Public/installed SDK uses PDFium; #208 cutover blocked.
