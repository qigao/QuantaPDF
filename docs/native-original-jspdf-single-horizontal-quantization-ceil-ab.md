# #269 — Original jsPDF source single-pass horizontal alpha quantization trial

**Draft / DO NOT MERGE; private C11 stroke-only hypothesis.** Stacked on
verified [#296](https://github.com/qigao/QuantaPDF/pull/296)
HEAD `40c390c16be4d2f9062fd91de2c462da9270442b`,
[full CI #38491406146](https://github.com/qigao/QuantaPDF/actions/runs/38491406146)
SUCCESS 3/3: Linux Release+ASan/UBSan, Windows, macOS.

The unchanged 3344-byte upstream original jsPDF
SHA256 `179d8552191bbd1890c72a85fac58bad3dd59ff66766fff76e9a790915a1b11e`
has 596x842 independent raw-source PDFium pixels. Previously qualified
mode5 source Stroke (round-to-nearest analytic single-horizontal interior
alpha) plus the separate right-Fill, left-W-Clip and Fill-bottom source
rectangular preview has 1,937 summed RGB absolute error / 419 changed
pixels / zero >64 gross / max channel delta38, white RGB FNV64
`7a88c595fcde2208`. The original whole-document C11 renderer still
refuses `UNSUPPORTED` and publishes zero pixels.

## Falsifiable one-domain hypothesis

The [#295](https://github.com/qigao/QuantaPDF/pull/295) source-original
16-region report found **198** pixels / **612** RGB absolute error
in the true single-pass bottom horizontal Stroke INTERIOR, with
candidate native predominantly *brighter* than PDFium by ~1 channel.
For actual original PDF pixel (100,139), mode5 alpha123 yields
gray (132,132,132), whereas original PDFium is gray
(131,131,131), suggesting **upward alpha integer quantization**,
rather than global source translation or a different geometry.

The sixth **explicit, private C11 Stroke entrypoint**
`quantapdf_ir_raster_device_polyline_stroke_single_horizontal_ceil_preview_v0`
reuses the exact existing mode5 source-width, single-pass horizontal
1pt *interior* analytical eligibility, including its conservative
device-pixel endpoint guard, diagonal/acute-miter exclusion,
source-segment expanded-bounds overlap refusal, and rejection of
truly repeated source-horizontal traversals. Modes0–5 remain
independently callable with the exact prior rounding; ONLY the newly
opted-in mode6 Stroke alpha becomes
`ceil(255 * true source horizontal Y-strip/pixel Y coverage)`.
The new candidate charges `count*6` bounded full-page edge work
BEFORE any output allocation instead of mode5's `count*5`.
It does not touch source joins, caps, endpoints, repeated top rows,
1pt verticals, wider/diagonal segments or Clip/Fill.
No PDFium fallback, runtime persistence, extra ownership channel
or public SDK ABI symbol.

## Actual original-PDF gate and synthetic refusals

The strict pure-C11 (/W4 /WX) standalone target compares mode5 and
mode6 at 72/96/144 DPI with expected nearest-integer vs ceil alpha.
Some grid phases can have no difference; determinism and precise
geometric eligibility are tested without fabricating a positive.
The changed alpha must only increase by **at most 1**, RGB color
bytes unchanged. Source horizontal retraces, vertical-only,
diagonal-miter source and an independently billed budget
(5*13,200 admitted, 6*13,200 rejected) retain mode5 bytes or
return zero transactional output.

The existing separate original jsPDF A/B CTest, not the historical
near-40s full-original native negative, renders the **new Stroke
twice**, composites it only AFTER the same verified eighth/private
Fill-bottom red image in source Stroke→Fill order, independently
compares all 501,832 original PDFium pixels / 1,505,496 RGB
channels, reports real RGB absolute/mean, changed/gross/max
and FNV64, checks exact repeat, unchanged underlying Stroke color,
source alpha delta at most +1, and no changed source pixels
outside the true original bottom x=52..248, y=138..140
**test-only** source window. The independent CMake checker freezes
the previous original mode8 **1937 / 419 / 0 / 38** and FNV64,
source SHA, all #295 source categories, legacy negative and official
positive counters. It intentionally **does not require mode6 to
improve** the original score: a regression is valid A/B evidence.

Keep #269 Open, this and upstream experimental PRs Draft /
DO NOT MERGE, original whole-document native `UNSUPPORTED` with
zero pixels, formal W/W* native-positive unchanged, public
SDK still PDFium, #208 cutover blocked until TRUE full-original
pixel-parity conformance.

## Falsifiability guard refinement

The first true-original Linux run showed that source pixel (100,139)
need **not** change under `ceil` versus nearest rounding: both integer
alphas can coincide at a given fractional source coverage, even if its
old gray pixel differs from PDFium. A test-only `old+1` requirement
was an unjustified improvement assumption and was replaced by a strict
`old <= ceil <= old+1` guard plus independent baseline/trial source
Alpha and RGB witness logging. The candidate still must reconcile ALL
source-original pixels and is explicitly allowed to regress. No C11
renderer algorithm or previous source-PDF counters were changed.
