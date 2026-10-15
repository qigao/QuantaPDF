# #269 — independently rendered original PDFium Fill/W Clip mask decomposition

**Test-only experiment on an isolated Draft branch. Do not merge or publish.**

The parent #354 Source→Graph/C11/PDFium matrix qualified 12 genuine
**QPDF test-generated** original PDFs, each with an offset CropBox,
intrinsic quarter-turn rotation, leaf /UserUnit7 and one source
`W n` Clip followed by opaque red Fill. At 96 DPI the original PDFium
image has binary red/white edges but native C11 uses 4×4 coverage.
Three small fractional CropBox shifts change PDFium's observed snapped
integer boundary offsets, so neither a constant per-rotation rule nor
a universal floor/nearest candidate is established.

## Question isolated here

Does original PDFium's combined Fill-under-W-Clip bitmap equal a
pixelwise **AND** of two independently rendered original PDFium
binary masks with identical page geometry?

For each of **four** crop variants (baseline, X+0.125, Y+0.125,
XY+0.375 PDF source units) and **four** intrinsic rotations
(0/90/180/270), produce three **separate genuine test-generated
original PDF files**. All three use the same page MediaBox, effective
CropBox, /Rotate, leaf /UserUnit7 and QPDF PDF 1.6 writer:

- **Combined:** `3 4 6 6 re W n 1 0 0 rg 4 5 6 4 re f`.
- **Fill only:** `1 0 0 rg 4 5 6 4 re f`.
- **Clip only:** `3 4 6 6 re W n 1 0 0 rg 0 0 20 30 re f`.

The Clip-only source's opaque Fill covers the MediaBox and cannot
itself trim the visible Clip. Crucially none of the three reference
images is a PDF re-export of Composer IR. All 48 source files enter
the normal test Source→bounded Graph page metadata capture and source
owner closing first. Their original PDF bytes are independently
reopened and rendered **twice** with the public **PDFium-backed** test
API at 96 DPI, and verified byte-identical per PDF. Source and Graph
owners are dropped before rendering, source file bytes are verified
unchanged before removal, and every input PDF is test-owned.

Bitmap extents remain 94×75 or 75×94 and are independently checked
against the **existing** bounded Graph→Device geometry: 7,050 pixels
per original image, 16 modes × three content variants = **48 original
PDF sources / 338,400 source-image pixels**. No large images,
native-raster replacement, secondary owner, automatic fallback or
external provider is added.

## Measurements and acceptance

The diagnostic classifies exact opaque red and exact white pixels
in each of the three source-PDFium bitmaps. It separately counts
non-binary pixels rather than presuming every original uses a binary
mask. It calculates each original's red bounding rectangle, total
red pixels and whether that rectangle is completely filled and
binary. The predicted combined mask is the **logical AND** of
independent Fill-only and Clip-only red masks. Against the actual
combined source PDFium image, record:

- Source red pixel counts for combined, Fill-only and Clip-only.
- Predicted intersection red pixel count.
- False negatives (pixel red in Fill AND Clip but not combined).
- False positives (red in combined but missing from Fill AND Clip).
- Total differing pixels and whether all three images are binary.
- An **exact intersection** flag only when all inputs are binary and
  the actual combined mask equals independent AND at every pixel.

A separate strict CMake checker must independently reconcile every
per-source and aggregate count, source filenames and variants,
source byte-repeat count, page geometry, bounding ranges, binary
flags, exact-intersection flags and no-external-admission fields.
It must **not** assume the masks compose exactly until the original
PDFium results prove it. Output JSON is atomic and per-platform CI
archives Linux Release, Linux ASan/UBSan, Windows, and macOS reports.

If actual combined PDFium equals independent binary AND on all
16 modes, the error is within the independent source rectangle
scan conversions and their integer edge rounding; if it does not,
Clip-state activation/composition contributes independently. Either
result is narrowly scoped to these QPDF **test-generated** source
fixtures, not a general PDFium specification or an independently
published original positive. The parent Source→detached-IR→C11
benchmark and 72/144DPI qualification remain untouched.

Keep #269 and #343 OPEN. All stacked PRs remain Draft /
DO NOT MERGE. The original independent reporter Userunit.pdf still
lacks verified raw bytes and redistribution permission. No changes
to production C11 Raster, Graph→Composer affine, ABI, SDK or release.
The installed/public SDK remains PDFium; #208 cutover blocked.
