# #269 — original PDFium source primitive bbox projection A/B

**Test-only numeric analysis, Draft / DO NOT MERGE.**
Parent [Draft #355](https://github.com/qigao/QuantaPDF/pull/355)
separately produces and renders three genuine **QPDF test-generated
original PDFs** at every page-geometry mode: Combined Fill+W Clip,
Fill-only, and W Clip-only. Original PDFium RGB masks are compared to
an independent per-pixel Fill-mask AND Clip-mask prediction. No PDF is
exported from Composer IR and no product native Raster has changed.

## Why inspect individual original primitives?

The 96 DPI parent #353/#354 matrices established that for the tested
rectangles, the original PDFium reference is binary red/white but
its per-edge pixel coordinates change with CropBox fractional offsets
and intrinsic rotation. A single global floor/ceil choice failed to
match the **combined** original. This can reflect primitive-specific
scan conversion or clipping.

This child independently examines all three source-original PDFium
red bounding rectangles at **4 CropBox variants × 4 intrinsic /Rotate
= 16 modes**. It computes exactly two predeclared, deterministic
**numerical** bounding-rectangle candidates per primitive:

1. **Qualified fitted Graph device geometry:** query the unchanged
   actual source→Graph page affine for the selected original PDF,
   map all four corners of the primitive source rectangle to the
   existing integer page grid (96 DPI with actual 94×75/75×94 extent),
   then floor lower X/Y edges and ceil exclusive upper X/Y edges.
2. **Nominal 96/72 DPI geometry:** use the same original Graph
   source→visible-page affine and multiply coordinates by 96/72,
   with a top-down Y inversion. Floor minima and ceil maxima as above.
   This is an observation candidate, **not** a new pixel mapping.

Source primitive rectangles are fixed and independently described:
the combined intersection [4,9]×[5,9], standalone opaque Fill
[4,10]×[5,9], and standalone W Clip [3,9]×[4,10].
The test writer changes only page geometry and which source
content operator sequence is used. A numeric guard normalizes only
projection results within 1e-9 of an integer, to avoid Graph's
bounded decimal parsing introducing one-ULP integer-boundary errors.
No source PDF coordinate or device extent is rounded in advance.

For each original PDFium source, preserve its already measured
binary mask and exclusive observed red bbox. Separately record
the two predicted four-edge bboxes, their **four-edge absolute
integer distance to original PDFium**, and exact four-edge match
indicators. Aggregate **16 source cases per primitive** by each
model, and independently reconcile every per-frame bbox and error
total in the strict CMake gate. Neither candidate is assumed
successful before exact-head CI.

The parent experiment already checks source bytes unchanged,
Source/Graph owners bounded and closed, PDFium byte-exact repeat,
finite device dimensions, red mask bounds, 16-mode mask-AND
composition and no partial/malformed report success. This child
retains those gates unchanged, and adds no extra raster
allocation, PDFium fallback, native owner or alternate rendering
backend. CI archives Linux Release, Linux ASan/UBSan, macOS and
Windows JSONs.

Even if one candidate explains every standalone Clip rectangle,
the evidence applies only to the 16 **test-generated opaque
axis-aligned rectangles**. More independently published/licensed
original sources, arbitrary Path/Stroke/W*/CTM, requested rotation,
mixed alpha and PDFium-free installed SDK validation are separate.
Keep #269/#343 OPEN, all stacked PRs Draft / DO NOT MERGE,
external native pixel positives unchanged, installed SDK PDFium
and #208 backend cutover blocked.
