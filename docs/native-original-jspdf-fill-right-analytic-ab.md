# #269 — Original jsPDF single clipped-rectangle Fill right edge A/B

**Draft / DO NOT MERGE.** Stacked exactly on accepted [#291](https://github.com/qigao/QuantaPDF/pull/291)
HEAD `fa56d1b8bba7deabf3f0c9c3359286b7ff79aa31`
([full CI #38482764344](https://github.com/qigao/QuantaPDF/actions/runs/38482764344)
SUCCESS 3/3: Linux Release+ASan/UBSan, Windows, macOS).

## Correct ownership of the original red boundary

The original jsPDF PDF content stream explicitly defines:

- Stroke followed by nonzero W Clip **rectangle x=50..250, display y=20..140**.
- Later solid red Fill **rectangle x=0..150, display y=0..100**.
- The resulting visible red area is **x=50..150, y=20..100**.

Therefore the red right boundary at original device x=150 comes from the
**Fill rectangle**, not the right W Clip edge at x=250. The source
right boundary at x=150 falls strictly INSIDE device column 150 because
the page is 595.28pt wide but PDFium allocates 596 device columns at
72 DPI. Original PDFium shows RGB (255,209,209) versus mode5 C11
(255,191,191), reflecting analytic alpha approximately 46 versus 4x4
sample-alpha 64. This is a reason to test *Fill-edge* geometry only;
it does not identify the internal PDFium antialiasing implementation.

## Private C11 proposal

`quantapdf_ir_raster_rect_fill_right_preview_v0` is an independent
explicit test-only renderer. It first *strictly admits* a single
opaque, normal-composited filled Path and one used nonzero W Clip
rectangle from owned Detached IR. Both Path command sequences must
describe a clockwise, axis-aligned rectangle, with optional
two-edge upper horizontal retraces from the producer. Source
`(x,-y)+H` translations are normalized numerically; no literal
device/source coordinates, hardcoded PDF producer ID or screen-phase
shifts are used by the candidate.

The preview relies on the previously admitted pure-C11 V0 layer, then
modifies only its actual partially covered **right Fill boundary**
pixel in rows whose entire device-pixel height is strictly within the
effective rectangular intersection. The new alpha is
`round(255 × source_overlap_x / device_pixel_width)`.
All corners, other Clip/Fill edges, W Clip topology, repeated
source rectangle border traversals, and RGB color ownership remain
unchanged. A conservative extra edge sample-work unit is charged
`fill_edges + clip_edges + 1` before raster allocation. Unsupported
source structure rejects with fully zero output; no PDFium fallback,
second owner, SDK symbol or automatic native backend switch.

## Independent tests

New strict standalone C11 target checks an axis-aligned fill and W
rectangle, original-style retraced top boundaries, analytically
expected right-cell alpha, source color immutability, byte-identical
repeat, out-of-region unchanged bytes, malformed/nonrectangular
clip refusal, wrong blend/alpha and insufficient-work zero-output.

The independent original-PDF mode5 A/B CTest now renders this
candidate Fill layer twice and composes each with the **same already
qualified mode5 Stroke** in correct source order, compares every
501,832 original PDFium pixels / 1,505,496 RGB channels and
retains all existing mode0–mode5/negative and source SHA gates.
The machine report measures true error/gross/mean/FNV64 and ensures
changes are confined to the source Fill x=150 inner right-edge class.
**A/B may regress; no mandatory improvement threshold is invented.**

Original native pixel-positive remains zero, formal W/W* counts
unchanged, public SDK still PDFium, #208 cutover blocked and #269
Open. The branch remains **Draft / DO NOT MERGE**.
