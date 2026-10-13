# #269 — Experimental repeated horizontal 1pt Stroke scan-row candidate

Status: explicitly invoked **preview-only**, not a PDF-compatible production
stroke renderer and not a native original-PDF pixel positive.

## Evidence and hypothesis

Immutable externally published jsPDF original, SHA256
`179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`,
renders a horizontal **1 source-point opaque black Stroke** at displayed
`y=20` and a later clipped red Fill. The full 596×842 original native-vs-
PDFium diagnostic (#281/#282) found 1,360 changed pixels: 300/301 large
errors are in the Stroke-only partial-coverage category, while a separate 97
weak red residuals live at the Fill border `y=100`.

Independent #283 **actual original PDFium** BGRx scan rows and private C11
grid at 72/96/144 DPI show:
- 72 DPI: PDFium black rows 19/20; baseline C11 each 128 alpha.
- 96 DPI: PDFium black rows 26/27; baseline C11 255 / 64 alpha.
- 144 DPI: PDFium black rows 39/40 with a near-white row 41;
  baseline C11 255/255/0 alpha, with only an approximately 3-level edge
  difference.
- Vertical Stroke at 72/96/144 is much closer to baseline C11 than the
  horizontal; a universal half-pixel phase shift gets substantially worse
  at 144 DPI.

**Narrower testable candidate after negative first-run evidence:** the
original jsPDF S path traces its TOP horizontal line at display y=20
THREE times, but its bottom horizontal line at y=140 only ONCE. A
test-only local PDFium probe shows 1pt single-pass source line edges are
partially covered while repeated source line edges are fully opaque.
The first broad preview over-filled both horizontal edges and **FAILED
the real full-page A/B**: original baseline RGB absolute error 141,465,
first preview error 174,438 and >64 channel-delta pixels 301 -> 400.

The revised candidate promotes coverage only when at least **two
overlapping horizontal source segments of the same one Stroke path**
geometrically intersect the same pixel interior; source width must be
exactly 1pt and projected width <2 device rows. Single-pass horizontal
edges retain 4×4 partial coverage, as do vertical/diagonal, endpoints,
caps, joins, wider widths and >=2 device-row thickness. The repeated-
line candidate is an *experiment*, not a PDF spec rule, generic
antialiasing algorithm or reason to admit the original as native-positive.

## Concrete code and ownership

`src/render/ir_device_polyline_stroke_v0.[ch]` provides an additional
**explicit** `quantapdf_ir_raster_device_polyline_stroke_thin_horizontal_preview_v0`
entrypoint. The existing
`quantapdf_ir_raster_device_polyline_stroke_v0` remains identical in
behavior. Both call the same bounded single-owner implementation with a
private flag; no fallback or user-selected global knob is introduced.
The preview adds one bounded source-segment scan per output pixel after
the existing 4×4 coverage test, counting **distinct overlapping
source segments** instead of marking any single intersecting line. Existing admission reserves up to
32 sampled-edge checks per pixel per segment, so the extra preview check
fits its already enforced upper bound; no heap buffer besides the owned
RGBA output is allocated.

The strict C11 standalone test proves a one-pass 1pt horizontal line
remains byte-identical to baseline, but three coincident source-line
passes increase only the interior two scan rows. A vertical line, a
4pt Stroke and the same repeated source at 144dpi remain byte-identical
to baseline. It tests deterministic repeat, original source ownership
and zero output under insufficient budget.

## Authentic original 596×842 complete image diagnostic

The test-only original PDF lowering still uses original raw bytes, the
same Source->Graph->owned Detached IR and independent original PDFium
render. It renders the previously accepted **isolated Stroke** with the
explicit preview **in addition** to baseline, composes each with the same
unchanged W-clipped Fill, then scans every 501,832 original pixel and all
1,505,496 RGB channels. Both candidate RGBA repetitions must be byte
identical. Report true full-image total/mean absolute RGB difference,
count of changed and gross-delta>64 pixels, max channel delta and
white-composited RGB FNV64, **regardless of whether the preview improves or
regresses**. Never infer a passing threshold from the observed errors.
Preview status is always `measured_unqualified`; the public SDK keeps
PDFium and original corpus/W* and native pixel-positive counts stay
unchanged.

Only if exact-HEAD Linux Release/ASan+UBSan, Windows, macOS metrics
demonstrate a material improvement without wider regressions should an
independent strict stroke scan-conversion contract be designed. The
original negative whole-document entrypoints must continue
`UNSUPPORTED/zero pixels` throughout.
