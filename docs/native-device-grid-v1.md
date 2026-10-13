# #269 — Private C11 NativeDeviceGrid V1 admission

Status: production-compiled private C11 module and executable conformance only.
This stage DOES NOT connect fractional pages to Scoped Raster and DOES NOT
authorize any pixel output. Original jsPDF #270 remains a quarantined
typed=lowered / raster=UNSUPPORTED / native_pixels=0 negative.

## Device mapping

Inputs are normalized top-left DISPLAY page points. Raw CropBox, intrinsic
/Rotate and /UserUnit ownership belongs to a separate source geometry stage;
the V1 candidate accepts only zero display origin, no rotation, UserUnit 1,
and 72/96/144 DPI. Other cases fail closed, with fully cleared output.

For width/height in display points P and dpi D, the independently observed
PDFium aligned output rectangle at the supported conditions has extent
ceil(P*D/72). A native grid stores both this integer device extent and
device_extent/P, with separate X and Y ratios. In particular the unchanged
jsPDF 595.28 by 841.89 point page maps to 596x842 at 72 DPI; this is not
a 1:1 point-to-pixel mapping and X/Y ratios need not be equal.

The analytical 4x4 sample helper uses
(device_pixel + (subsample+0.5)/4) * point_extent/device_extent
in display coordinates. This is a candidate sampling convention, NOT an
independent measurement of PDFium's antialias pixel centers. It must not
be interpreted as raster or image parity.

## Precision and bounded ownership

The existing Detached IR page extents are float. Precision mode SOURCE_F32
requires the input to equal its float encoding, then checks both adjacent
float values: if either changes the outward integer device extent, return
UNSUPPORTED rather than inventing missing source precision. E.g. narrowing
595.000001 to float 595.0 creates an ambiguous boundary. An EXACT_DOUBLE
mode is available only to a future producer that independently preserves
and proves the original precise geometry: it must not be selected merely
by casting existing float IR values to double.

The caller supplies explicit maximum pixels, RGBA bytes, flattened edges and
4x4 sampled-edge tests, plus a pre-admitted total edge count. V1 validates
all multiplications by guarded division, caps width/stride for int-backed
render extent representability, and returns a value struct with no allocation.
This is a preflight for later renderer ownership; the existing scoped
renderer still has its own edge flattening and work accounting.

## Boundaries and follow-up

- No PDFium/qpdf include, provider call, heap allocation or public symbol.
- New strict C11 CTest checks three DPI extents, repeat and sample inverse,
  precision boundaries, overflow/budget rejection, and zero output on failure.
- Existing ir_scoped_operations_v1 and ir_raster_v1 are deliberately
  unchanged; neither may bypass its existing integer-page guard yet.
- Next stage must integrate the grid with both Path and Clip transforms
  (plus bounded sample work), explicitly reject unsupported stroke scaling,
  then qualify original PDFium-versus-C11 full image and repeat on three
  platforms before changing #270 from native negative to native positive.

Keep #269 open, all stacked PRs Draft / DO NOT MERGE, public SDK PDFium,
external formal corpus unchanged, W* positive count unchanged.
