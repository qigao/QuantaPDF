# #269 — Repeated-Stroke endpoint ownership A/B (private trial)

This stacked Draft deliberately *does not* change QuantaPDF's default
pixel renderer or the already CI-validated original-internal repeated
Stroke preview from #284. It adds a second **explicit, private** C11
candidate and read-only original-PDFium whole-page tests. No PDFium
fallback, no public ABI/SDK/backend change, no generated corpus PDF.

## Grounded exact source and remaining obstruction

The immutable 3,344-byte jsPDF original's black `S` operation includes
a top horizontal 1pt segment traversed three times; the bottom line is
single-pass. The prior repeated-*interior* preview improved full original
RGB absolute error from 141,465 to **27,735**, and >64 RGB channel-error
pixels from 301 to **2**, identically across Linux, macOS, and Windows
([#284 CI #38437055375](https://github.com/qigao/QuantaPDF/actions/runs/38437055375)).

The remaining two severe candidate-original mismatches are:
- **(50,19)**: candidate RGB (127,127,127), original PDFium (0,0,0),
  max delta **127**, source Stroke alpha 128, Fill alpha 0.
- **(250,19)**: candidate RGB (159,159,159), original PDFium (76,76,76),
  max delta **83**, source Stroke alpha 96, Fill alpha 0.

A third candidate mismatch at **(250,20)** has max delta **64**,
not in the >64 bin. All three are at original black top Stroke
endpoint/Join device columns. The old private #284 helper *intentionally*
excluded endpoint columns by requiring the pixel center to stay at least
half a source-point inside the segment.

## Isolated new code hypothesis

`quantapdf_ir_raster_device_polyline_stroke_repeated_endpoint_preview_v0`
adds an **opt-in, private** endpoint-inclusive repeated-Stroke
hypothesis to the same bounded pure C11 module, using one extra bounded
source-segment scan inside the already sampled RGBA pixel loop.

The previous `_v0` entrypoint remains unchanged, and the earlier
`_thin_horizontal_preview_v0` continues executing its CI-qualified
mode-1 matching rules without endpoint inclusion.

Endpoint-inclusive mode 2 is *strictly* confined to:

- exactly **1 source-point** opaque horizontal Stroke,
- projected source stroke width **less than 2 device rows**,
- a device column whose source-space pixel rectangle **intersects**
  the repeated horizontal source interval,
- **two or more overlapping source segments** at the *same source Y*,
  with genuinely overlapping segment spans (not merely adjacent
  disjoint source lines in the same device pixel),
- only a single bounded source Stroke operation and its admitted
  ownership/resource budgets.

It does NOT move Path/Clip or device-grid coordinates, change single-pass
horizontal strokes, change vertical/diagonal strokes, enable general
caps/joins or substitute PDFium for C11 pixel generation. Preview mode
2 is a hypothesis and may overpaint true Butt Cap endpoints. **No
candidate improvement is assumed.**

## Exact original full-image falsifiable gate

The existing real Source→Graph→Owned Detached IR test independently
renders this new **isolated genuine original S Stroke** twice, verifies
it changes both known endpoint pixels, keeps the same genuine
later W-Clip Fill, and composes in the original S-before-Fill paint order.

For every one of the **501,832** original 596×842 pixels, the
test-only, original-PDFium RGB oracle independently compares all three
channels with white-composited C11 RGBA. It records:
- total/mean RGB absolute difference,
- changed and >64 delta pixel counts and max channel delta,
- bounded complete spatial attribution across native Stroke/Fill alpha
  categories, including coordinates and RGB of the worst 12 sites,
- repeated RGBA byte equality and candidate white-RGB FNV64.

The CMake guard checks exact original SHA before/after, output schema,
numeric limits, source coordinates and category totals vs the
independently measured full-image statistics. It **does not** demand
endpoint-candidate improvement or loosen any existing acceptance
threshold. Outcomes are measured, not invented.

Keep #269 Open, all stacked PRs Draft/DO NOT MERGE, old whole-page
`UNSUPPORTED/zero pixels` negative gates, `pixel_parity_qualified=false`,
formal external corpus and W* native-positive counts unchanged,
and the published SDK PDFium-backed. An endpoint trial passing a
diagnostic does **not** establish a generic PDF miter/Butt cap semantics.
