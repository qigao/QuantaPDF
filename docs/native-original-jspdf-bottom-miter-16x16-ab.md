# #269 — Strictly scoped source bottommost Miter join 16x16 A/B

**Draft / DO NOT MERGE, exclusively a private C11 sample-density
hypothesis.** Direct parent [#299](https://github.com/qigao/QuantaPDF/pull/299)
HEAD `68e6971dcebe4ed60fd0e9307b557c0863bbaeec`,
full [CI #38497837820](https://github.com/qigao/QuantaPDF/actions/runs/38497837820)
SUCCESS on Linux Release+ASan/UBSan, Windows, macOS.

The immutable original published 3,344-byte jsPDF SHA256
`179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`
is rendered directly by original PDFium (596x842 device pixels,
501,832 pixels, 1,505,496 source RGB channels), never by an exported
synthetic PDF. Previous independently measured PRIVATE mode7 Stroke
(upper-only 1 Alpha LSB on eligible source horizontal interior)
composited in source order with the unchanged qualified red Fill
right edge / nonzero W Clip left edge / Fill bottom fringe
shows 1,349 summed absolute RGB error, 223 differing pixels,
zero >64 severe, max delta38, white RGB FNV64
`04d883c87f2ec8bc`. Original whole-document native rendering is
still strictly UNSUPPORTED with zero published pixels; not parity.

The exact [#299](https://github.com/qigao/QuantaPDF/pull/299)
**read-only**, cross-platform 16-region source-PDF audit proves that
the previously corrected true single-pass bottom horizontal INTERIOR
has only 24 RGB error and 2 differing pixels after mode7.
In contrast, its bottom left and right source Stroke Miter joins
account for **303 + 228 = 531 RGB error across 8+6=14 changed
pixels**, with max channel delta38 at source (50,139) and
max25 at (250,139). These two source corner regions are the
largest remaining high-severity effect; no source alpha-phase
adjustment should be copied from the straight interior.

## Eighth explicitly invoked C11 Stroke hypothesis

`quantapdf_ir_raster_device_polyline_stroke_bottom_miter_fine_preview_v0`
inherits the identical, fully validated previous 1pt Stroke mode7
sampling and quantization everywhere except strictly admitted C11
source-bottom Miter corners.

Only when a source Path has an exact 1pt width, a genuinely
contiguous **90-degree axis-aligned source Miter join** whose Y
coordinate equals the SOURCE PATH'S bottommost Y, the source
adjacent horizontal/vertical segments are long enough and the
candidate device pixel touches the radius + one-device-pixel
source neighborhood may it be considered. Every other source
segment's expanded geometric bounds must NOT intersect that
device pixel; if multiple source joins compete, no pixel is
changed. All source corners and bounding regions are derived
from the existing borrowed Detached IR and NativeDeviceGrid,
never from literal jsPDF device coordinates or PDFium data.

At qualifying source corner pixels the *same pure-C11
`qp_stroke_hit` union of admitted source segment rectangles and
existing actual Miter/Bevel triangle geometry* is measured with
**16x16 deterministic source subpixel samples** in place of the
old 4x4 sampled Alpha, under straight Alpha with the original
source Stroke color. No global upsampling, normal Stroke mode
switch, PDFium fallback, source transformation, persistent state,
second owner, automatic retry, or SDK/ABI export. The previously
qualified source horizontal interior (mode7), true repeated upper
Stroke, isolated 1pt vertical and red W Clip+Fill are retained.

**Bounded accounting**: original counts & join scans retain
their prior admission. This private mode pre-admits a separate
full `count*8*grid.sample_slots` source-edge sampling pass,
then additionally charges a conservative maximum of
`count*(count-1)*36*256` source-hit evaluations (36 pixels
around each source join, 16x16 samples of all source segments),
with division-first overflow protection, against BOTH caller
grid and raster sample ceilings before allocating any result.

## No pretense that fine sampling equals PDFium

Standalone strict C11 tests verify at least one actual
finer-resolution difference on synthetic bottommost left/right
source corners, that all modified pixels remain within geometric
corner neighborhoods, immutable source RGB, deterministic repeats,
and **exact unchanged mode7 bytes** for repeated source
horizontal/vertical shapes with no eligible bottommost Miter.
Under a 350,000-sample ceiling the previous count7 mode
passes but this mode's extra count8 + corner work MUST
reject with zero partial image.

The separate real original PDF A/B CTest (not the historical
near-40s original-negative) renders this candidate Stroke
**twice**, composites with the SAME existing qualified
mode8 Fill-bottom in source order, independently measures
all actual original-source PDFium pixels and publishes
unrestricted true error/count/gross/max/FNV64. It forbids
changes outside original-source bottom x48..52 / x248..252
and y138..141 (**test-only** diagnostic source coordinates),
requires RGB source colors unchanged and repeat bytes equal.
The CMake checker freezes accepted mode9 original
**1,349 / 223 / 0 / 38** and `04d883c87f2ec8bc`,
the #299 detailed region report, original source SHA
and zero native-positive counts. An error regression is
valid evidence: NO mandatory improvement threshold.

**Keep this and all upstream stacked PRs Draft / DO NOT MERGE**,
#269 Open, original whole-native renderer UNSUPPORTED/zero pixels,
public installed SDK PDFium, #208 cutover blocked.
