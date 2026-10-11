# Page display transform: CropBox, /Rotate, and /UserUnit

Continuation of QuantaPDF #145 and #154. This is a private, versioned
Graph → Content → Graphics → Composer lowering increment, stacked on
Draft #202 → #201 → #200 → #199 → #198.

## Problem and decision

#202 validates immutable page-tree ancestry and derives effective MediaBox,
CropBox, rotation and leaf UserUnit from the selected graph version. It only
admits canonical [0 0 W H] / zero-rotation / UserUnit=1 pages.

For other valid pages, do **not** alter source content-stream coordinates or
pretend the visible page has an origin at (0,0). Instead, derive a page affine
transform `P` from original PDF Y-up user coordinates into the zero-origin,
unrotated, visible destination PDF canvas. Apply `P` **after** the content
graphics-state CTM: a path point is rendered as `p · CTM · P`.
The existing Composer writer then handles its established Y-down coordinate
conversion. This keeps source path coordinates and stroke widths intact.

The original page's visible CropBox supplies the viewport. The new canvas
uses `/MediaBox = [0 0 width height]`, no rotation and UserUnit=1.
Clip by the new page bounds: no synthetic Composer Clip resource is required
for the CropBox viewport. This is safe only when the entire CropBox lies
within the MediaBox. CropBox outside MediaBox remains UNSUPPORTED until
provider-equivalent crop/media intersection rules are independently qualified.

## PDF page display affine map

Let `C = [L, B, R, T]` be the effective CropBox,
`U` the leaf /UserUnit and `p = (x,y)` the original PDF Y-up point.
The destination coordinates `(X,Y)` (also PDF Y-up) are:

| /Rotate | X | Y | visible dimensions |
|---|---|---|---|
| 0 | U (x-L) | U (y-B) | U(R-L) × U(T-B) |
| 90 | U (y-B) | U (R-x) | U(T-B) × U(R-L) |
| 180 | U (R-x) | U (T-y) | U(R-L) × U(T-B) |
| 270 | U (T-y) | U (x-L) | U(T-B) × U(R-L) |

These follow the existing PDFium page-point rotation convention when
converted back from its displayed top-left coordinates to PDF Y-up space.
Store a six-component row-vector affine matrix and detached output
dimensions. The calculation has no QPDF/PDFium dependencies and resets
the output on invalid/unsupported input.

Compose a path's already interpreted CTM with this mapping **on demand**
in `quantapdf_pdf_path_lower_to_composer_mapped`; retain the old
`quantapdf_pdf_path_lower_to_composer` as an identity-map wrapper.
The mapped path still passes every existing strict float, 4-decimal,
RGB8, singularity, and clip admission gate. There is no approximation
or raw-PDF fallback on failure.

## Coverage and lifecycle

- Nonzero MediaBox origins and CropBox subsets inside MediaBox
- /Rotate 0/90/180/270 (including inherited effective values)
- Leaf /UserUnit=1 and 2; correct scaling of output canvas and path CTM
- Content CTM translation composed before the page affine map
- Graph version-isolation when /Rotate is replaced in a descendant
- Fully detached Composer IR surviving source document and snapshot drop

The real PDF regression uses QPDF to create a page with inherited media
geometry, an explicit offset CropBox and translated blue rectangle. It
renders the original with the existing PDFium SDK, lowers via the selected
Graph Snapshot, exports the resulting Composer IR through existing QPDF
Writer, and rasterizes again with PDFium. It compares dimensions, interior
blue pixels and white exterior pixels across the four rotations and
90-degree UserUnit=2 case. A separate version test replaces rotation
after capture and proves base/descendant dimension isolation.

## Explicit limitations

This does not interpret text, XObjects/images, color spaces beyond the
already admitted paths, complex clipping geometry, arbitrary gradient
or transparency state, or page CropBoxes outside MediaBox. Unsupported
or numerically unrepresentable cases fail closed. No public C ABI,
existing SDK symbol, dependency, Writer output policy or provider runtime
is changed.

CI acceptance remains complete Linux Release, fatal ASan/UBSan, static SDK
and Poppler qualification; Windows/macOS tests remain independent gates.
