# #269 — Private single-pass vertical analytic A/B

**Draft / DO NOT MERGE.** Fourth explicit private C11 hypothesis stacked
on #288's exact verified HEAD `4fad55e90144d1e03cdf7f623551e696d8f18650`.
No default renderer, public SDK/ABI, upstream source, or fallback change.

## Evidence

The immutable 3,344-byte upstream jsPDF, pinned by SHA-256
`179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`,
still has 26,955 RGB error and 1,060 changed pixels in private mode 3.
Cross-platform seven-bin accounting assigns 9,396 error to the right
vertical Stroke, 6,931 to left vertical: 16,327 / 26,955 (60.6%).
PDFium 72/96/144 DPI vertical samples closely match pixel/strip analytic
intersection, while native 4x4 sampling quantizes the partially covered
edge. That is a candidate, not a PDF spec or PDFium implementation claim.

## Implementation

Fourth-only symbol:
`quantapdf_ir_raster_device_polyline_stroke_single_vertical_analytic_preview_v0`.
For an opaque exactly 1pt axis-aligned vertical source segment it
computes source strip [x-0.5,x+0.5] intersection with the current
device-pixel X interval, alpha=round(255*intersection/pixel_width).
The pixel must be strictly in the segment interior, one full device
row plus radius away from endpoints, and no other width-expanded
source segment may overlap the pixel. Ambiguous joins, caps, repeats,
horizontal/diagonal/wider strokes, W Clip and Fill retain mode 3.
Mode 4 retains the existing corner candidate and conservative bounds,
and charges a separately admitted count*4 sampled-edge work budget.
No raw state persists and all failures leave a zeroed output.
Mode 0/1/2/3 execution and private entrypoints remain unchanged.

## Acceptance gate

The parent original jsPDF negative test uses ~38s of a 40s Linux
sanitizer timeout. A separate CTest for the fourth experiment
`quantapdf.external_jspdf_single_vertical_analytic_ab` avoids
extending that threshold. It lowers the very same original to owned
Detached IR, renders mode3 once and mode4 twice, then composes each
with the unchanged later W Clip Fill, compares ALL 501,832 raw PDFium
pixels and 1,505,496 RGB channels, and checks repeat equality and
source-region exclusivity. Standalone strict C11 tests cover
72/96/144 DPI geometry, unchanged horizontal strokes and
resource-budget zero-output refusal.

Source SHA and old mode3 RGB error=26955, changed=1060, gross=0
and FNV64 `1de2c62251fd22c0` are explicitly pinned by an independent
machine checker. **No acceptance threshold or guaranteed numerical
improvement is imposed on mode4.** The result stays diagnostic-only,
all native-original and official W/W* positive counts remain zero,
and public SDK continues using PDFium. Keep #269 open / #208 blocked.
