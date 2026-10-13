# #269 — Experimental private horizontal 1pt Stroke scan-row candidate

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

**Narrow testable candidate:** only for a one-source-point-wide, strictly
horizontal segment, whose projected width is *less than two* device rows,
and only between endpoints excluding a source-radius margin, replace
partially hit scan rows by full coverage when their source-space device
pixel rectangle geometrically intersects the Stroke strip. This is not a
general AA algorithm, line snapping guarantee, or PDFium implementation
claim. Vertical and diagonal segments, endpoints, caps, joins, source width
!=1, and >=2 device-row thickness retain the existing exact 4×4 logic.

## Concrete code and ownership

`src/render/ir_device_polyline_stroke_v0.[ch]` provides an additional
**explicit** `quantapdf_ir_raster_device_polyline_stroke_thin_horizontal_preview_v0`
entrypoint. The existing
`quantapdf_ir_raster_device_polyline_stroke_v0` remains identical in
behavior. Both call the same bounded single-owner implementation with a
private flag; no fallback or user-selected global knob is introduced.
The preview adds one bounded source-segment scan per output pixel after
the existing 4×4 coverage test. Existing admission reserves up to
32 sampled-edge checks per pixel per segment, so the extra preview check
fits its already enforced upper bound; no heap buffer besides the owned
RGBA output is allocated.

An existing strict C11 standalone test proves the preview modifies only
the interior two scan rows of a synthetic 1pt horizontal line; a vertical
line and a 4pt stroke remain byte-identical to the baseline; original
stroke source bytes and resource limits do not change. An intentional
over-budget request returns with zero output.

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
