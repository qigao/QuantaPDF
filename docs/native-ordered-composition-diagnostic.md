# #269 — original jsPDF ordered full-image diagnostic (two-layer C11)

Status: a **separate Draft, unqualified pixel-comparison stage**, stacked on
#280. The public QuantaPDF SDK remains backed by PDFium. This is neither a
replacement for the old integer-only Scoped Raster nor an authorization
to mark this original as native-positive.

## Input and ordering

The unchanged, externally published jsPDF 3,344-byte W-only source is
independently loaded/lowered to owned Detached IR with Source and Graph
released before native render. It has exactly **one first Stroke Path** and
**one later Fill Path with one frozen W Clip leaf**.

Existing #279 and #280 private C11 entries independently render:
1. Stroke (the earlier S operator, **not** clipped by a future W).
2. W-clipped Fill (the later f operator, whose frozen W leaf applies only
   to the fill).

The new **private C11** `ir_ordered_layer_composite_v0.[ch]` accepts two
borrowed, identically sized **straight-alpha RGBA8** layers and composes
them with the same integer source-over contract used by the existing
Scoped Raster, in that exact paint order. The result is an independently
malloc-owned RGBA8 buffer and must be explicitly disposed. Both input
buffers and the owned original IR are unchanged.

This first composite operates on already-aggregated per-pixel coverage
alphas. At overlapping antialiasing edges it may lose per-subpixel
coverage correlation. Therefore the output is an *ordered candidate*,
not proof of PDFium's exact pixel-center/sample convention.

## Bounded behavior

- No provider, QPDF, PDFium symbol, fallback, image encoding or public ABI.
- Both inputs must have nonzero equal width/height, tightly packed stride,
  exact RGBA byte size, nonnull pixels and overflow-safe representability.
- Per-output pixels and RGBA bytes are separately bounded. Inputs are
  borrowed; no cloning, runtime-state capture or source-file mutation.
- Output resets on every valid-slot refusal and publishes no partial
  pixels. Output structure may not alias either input structure.

An independent strict C11 conformance target exercises source order,
transparent and opaque pixels, overlapping 50% alpha, exact RGBA repeats,
memory limits, invalid layer shapes and zero-output rejection.

## Original-PDFium diagnostic

Compare **every pixel and RGB channel** of the candidate white-composited
596 × 842 image (501,832 pixels; 1,505,496 RGB channels) against PDFium
rendering of the **same untouched original raw PDF**.

Publish measurable full-image statistics: total absolute RGB error,
per-channel mean absolute error, maximum delta, gross-delta (>64) pixels,
all mismatched pixels, fractional-coverage mismatches vs flat-coverage
mismatches and repeat digest. Emit metrics even when parity is poor.
The machine checker verifies actual full-image coverage, sane numeric
ranges, deterministic repeat, fixed source identity, and unaltered
negative gates. It does **not** treat a successful diagnostic as a
native-original pixel-positive.

Acceptance of true original-pixel parity requires a separate explicit
threshold decision grounded in these measured metrics, per-edge sample
analysis and cross-platform evidence, followed by source provenance/corpus
qualification. The legacy #270/#277 `UNSUPPORTED/zero pixels` checks,
formal source corpus count, W* count, #208 backend cutover prohibition
and Draft/DO NOT MERGE state remain unchanged.

## Spatial attribution of the remaining original mismatches

This follow-up does **not** change the C11 raster, original PDF bytes,
PDFium reference or any acceptance tolerance. The original 596×842 image is
independently rescanned through a bounded test-only C++20 analyzer in
`tests/original_jspdf_diff_attribution.h`. Input planes are read-only and
validated for strides and sizes. At most **12 ordered worst-site records** are
kept; memory does not scale with the number of changed pixels.

Every changed pixel is assigned to exactly one of four mutually exclusive
groups based on the *native source layer* alpha states: neither layer partially
covered, Stroke-only partially covered, Fill-only partially covered, or both.
Each group separately bills changed pixels, >64-channel-delta pixels and
absolute RGB error. The full-image totals must match the existing independent
comparison; a separate synthetic CTest tests mask-group construction, top-N
stable ranking, partial-edge 3×3 adjacency and structural refusal.

In particular the old **100 native-flat-classified changed pixels** are
divided into those with a 3×3 neighboring partial-coverage sample and those
without. This is spatial evidence, **not proof** of a PDFium AA kernel or the
specific source of an error. The analyzer also counts pixels where both
native layers are uncovered although the original PDFium pixel is nonwhite;
records the changed-pixel bounding rectangle; and reports coordinates,
reference/native RGB, per-layer alpha and maximum channel difference for the
12 most severe mismatches. Ordering is deterministic: maximum channel error
descending, RGB error sum descending, then top-to-bottom, left-to-right.

The machine CMake guard validates the four attribution sums against the
unchanged all-image counts/errors, checks bounds and sorted worst-site
coordinates, and retains `pixel_parity_qualified=false`,
`native_pixel_positive_count=0`. Do not promote this external PDF, change
SDK PDFium cutover status, or alter any legacy negative gate on diagnostic
evidence. The next decision should use real CI coordinates to determine
whether edge coverage correlation, sample centers or source geometry are the
right code correction.
