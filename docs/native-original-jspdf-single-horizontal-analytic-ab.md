# #269 — Private single-pass horizontal analytic A/B (mode5)

**Draft / DO NOT MERGE**, based on the fifth-stage source-region
attribution of the immutable published jsPDF original, SHA256
`179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`.
Parent #290 HEAD `a9da85f5e548942109bd4ec61c9e203ff0989ae0` has full
Linux Release+ASan/UBSan/Windows/macOS CI
[#38481780639](https://github.com/qigao/QuantaPDF/actions/runs/38481780639)
SUCCESS 3/3.

Existing private mode4 renders the 596×842 original ordered source Stroke
then later original W-clipped Fill with 10,979 RGB absolute error and
791 changed pixels, zero large >64; mode4 FNV64
`226faee70471ea2c`. Original-source attribution identifies the
**bottom single-pass 1pt horizontal Stroke** as the largest independent
remaining category: 5,259 RGB error over 408 pixels. At (100,139)
the native C11 alpha=128/RGB=(127,127,127), while genuine PDFium
RGB=(131,131,131). An analytic source-rectangle pixel overlap
predicts approximately RGB132. This motivates a strictly falsifiable
*horizontal-only* analytic model; no PDFium internal algorithm claimed.

An additional **explicit/private C11** entrypoint
`quantapdf_ir_raster_device_polyline_stroke_single_horizontal_analytic_preview_v0`
retains mode4 vertical analytical, repeated horizontal endpoint and
outside-corner behavior; only truly single-pass 1pt horizontal
**interior** samples use source Y-strip/device-pixel intersection
and rounded 255-point alpha. Pixels must lie at least one source radius
PLUS a full device X pixel away from both endpoints. Horizontal
retraces, overlapping other edges, diagonals, joins/caps and the later
W Clip Fill stay mode4. A separate `count*5` sample-edge budget
is charged before allocation. All failures publish zero pixels; no
fallback or public SDK/ABI changes.

Strict standalone C11 synthetic tests check 72/96/144 DPI analytical
coverage, byte-identical repeats, true source repeated horizontal
mode4 preservation, vertical-only preservation, diagonal-miter
exclusion and insufficient-work transactional refusal. The existing
**separate** original-PDF mode4 CTest (not the original-negative near
40 second test) renders new mode5 twice, composites the unchanged
original red W Clip Fill, compares **all 501,832 PDFium pixels** and
**1,505,496 RGB channels**, checks deterministic repeat, C11 color
channel identity, and rejects mode5 changes outside the bottom
horizontal interior. The independent CMake checker keeps all previous
mode4 exact-origin area/group/FNV gates plus the new mode5 status,
region, resource and digest bounds. It **does not demand a better
score** from mode5: the A/B may regress.

This is a test-only candidate. #269 remains Open; #290 stays Draft;
native-original full page positive and formal W/W* numerator remain
zero. Public SDK remains PDFium; #208 cutover blocked.
